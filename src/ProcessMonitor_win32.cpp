// ProcessMonitor_win32.cpp — Windows implementation of ProcessMonitor using
// WMI process start/stop traces (event-driven), plus an initial snapshot and a
// periodic reconciliation: watched processes that are already running when the
// monitor starts are still registered (silently — no auto-switch is triggered
// for them), and pids whose stop event WMI dropped are pruned — so allExited
// ("switch back to close") fires even when a watched process was running
// before the monitor subscribed. Guarded by _WIN32; non-Windows builds link
// the matching _<platform>.cpp instead.

#include "ProcessMonitor.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <HeliosViewCore/App.h>
#include <comdef.h>
#include <Wbemidl.h>

#include <map>
#include <mutex>
#include <print>
#include <set>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "wbemuuid.lib")
#include "AppContext.h"
namespace
{

bool InitWmi(IWbemServices** services)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        AppContext::instance()->logger().Info("ProcMon", " CoInitializeEx failed: 0x{:08X}", static_cast<unsigned long>(hr));
        std::println(" CoInitializeEx failed: 0x{:08X}", static_cast<unsigned long>(hr));
        return false;
    }

    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                              RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                              nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE)
    {
        AppContext::instance()->logger().Info("ProcMon", " CoInitializeSecurity failed: 0x{:08X}", static_cast<unsigned long>(hr));
        CoUninitialize();
        return false;
    }

    IWbemLocator* locator = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(hr))
    {
        AppContext::instance()->logger().Info("ProcMon", " CoCreateInstance(WbemLocator) failed: 0x{:08X}", static_cast<unsigned long>(hr));
        CoUninitialize();
        return false;
    }

    hr = locator->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr,
                                0, nullptr, nullptr, services);
    locator->Release();
    if (FAILED(hr))
    {
        AppContext::instance()->logger().Info("ProcMon", " ConnectServer failed: 0x{:08X}", static_cast<unsigned long>(hr));
        CoUninitialize();
        return false;
    }

    hr = CoSetProxyBlanket(*services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr, EOAC_NONE);
    if (FAILED(hr))
    {
        AppContext::instance()->logger().Info("ProcMon", " CoSetProxyBlanket failed: 0x{:08X}", static_cast<unsigned long>(hr));
        (*services)->Release();
        CoUninitialize();
        return false;
    }
    return true;
}

IEnumWbemClassObject* SubscribeWmi(IWbemServices* services, const wchar_t* query, const char* what)
{
    IEnumWbemClassObject* enumerator = nullptr;
    const HRESULT hr = services->ExecNotificationQuery(
        _bstr_t(L"WQL"), _bstr_t(query),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &enumerator);
    if (FAILED(hr))
        AppContext::instance()->logger().Info("ProcMon", " ExecNotificationQuery({}) failed: 0x{:08X}", what, static_cast<unsigned long>(hr));
    return FAILED(hr) ? nullptr : enumerator;
}

std::string BstrToUtf8(const wchar_t* str)
{
    if (!str) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, str, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out;
    out.resize(static_cast<size_t>(len) - 1);
    WideCharToMultiByte(CP_UTF8, 0, str, -1, out.data(), len, nullptr, nullptr);
    return out;
}

} // namespace

struct ProcessMonitor::Impl
{
    Impl(ProcessMonitor* owner_, helios::App& app_) : owner(owner_), app(&app_) {}

    // ---- watcher thread state ----
    ProcessMonitor* owner;   // to emit the processMatched / allExited signals
    helios::App* app;        // posts emissions to the UI thread (thread-safe)
    HANDLE thread = nullptr;
    HANDLE stopEvent = nullptr;
    bool running = false;

    // ---- shared with the owner thread (mutex-guarded) ----
    mutable std::mutex mutex;
    std::map<std::string, std::string> watchMap; // exe path → config name

    // ---- worker-thread-only PID tracking ----
    std::map<std::string, std::set<DWORD>> pidsByConfig;
    std::map<DWORD, std::string> configByPid;

    // Emit on the UI thread (never touch the signal from this worker thread).
    template <class Fn>
    void PostToUi(Fn&& fn)
    {
        app->postTask([fn = std::forward<Fn>(fn)] { fn(); });
    }

    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();
    // Sync the tracked-pid set with reality: register (silently) watched
    // processes that are running but not tracked yet, and prune tracked pids
    // whose process no longer exists. Called once at startup (initial snapshot)
    // and periodically (safety net for dropped stop events).
    void Reconcile(IWbemServices* services);
    // Match a process against the watch map and, on a match, register it.
    // fullPath may be empty (only the filename is compared then). No-op if the
    // pid is already tracked. When emitSignal is true (a live start event) a
    // processMatched is posted to the UI thread so the app auto-switches; the
    // snapshot/reconcile path passes false to register silently — emitting
    // there would race with config loading at startup.
    void RegisterProcess(DWORD pid, const std::string& name, const std::string& fullPath, bool emitSignal);
    std::string GetProcessPath(DWORD pid) const;
    void HandleStart(DWORD pid, const std::string& name);
    void HandleStop(DWORD pid);
};

ProcessMonitor::ProcessMonitor(helios::App& app) : m_(std::make_unique<Impl>(this, app)) {}

ProcessMonitor::~ProcessMonitor() { Stop(); }

bool ProcessMonitor::Start()
{
    auto& i = *m_;
    if (i.running) return true;

    // Reclaim handles of a previous watcher that already exited (e.g. WMI
    // init failed): running == false here, so that thread is done.
    if (i.thread) { CloseHandle(i.thread); i.thread = nullptr; }
    if (i.stopEvent) { CloseHandle(i.stopEvent); i.stopEvent = nullptr; }

    i.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!i.stopEvent) return false;

    // Set the flag BEFORE CreateThread: the worker clears it when it exits,
    // so IsRunning() reflects reality even if the watcher dies immediately.
    i.running = true;
    i.thread = CreateThread(nullptr, 0, Impl::ThreadProc, m_.get(), 0, nullptr);
    if (!i.thread)
    {
        i.running = false;
        CloseHandle(i.stopEvent);
        i.stopEvent = nullptr;
        return false;
    }
    AppContext::instance()->logger().Info("ProcMon", "Watcher thread started");
    return true;
}

void ProcessMonitor::Stop()
{
    auto& i = *m_;
    if (!i.running) return;
    i.running = false;

    if (i.stopEvent) SetEvent(i.stopEvent);
    if (i.thread)
    {
        WaitForSingleObject(i.thread, 3000);
        CloseHandle(i.thread);
        i.thread = nullptr;
    }
    if (i.stopEvent)
    {
        CloseHandle(i.stopEvent);
        i.stopEvent = nullptr;
    }
}

bool ProcessMonitor::IsRunning() const { return m_->running; }

void ProcessMonitor::SetWatchMap(const ProcessMap& map)
{
    std::lock_guard<std::mutex> lock(m_->mutex);
    m_->watchMap.clear();
    for (const auto& [path, config] : map)
        m_->watchMap[path] = config;
}

DWORD WINAPI ProcessMonitor::Impl::ThreadProc(LPVOID param)
{
    auto* impl = static_cast<Impl*>(param);
    impl->Run();
    return 0;
}

void ProcessMonitor::Impl::Run()
{
    // A previous watcher run may have left stale PID registrations (e.g. the
    // monitor was stopped while watched processes were still running). These
    // maps are worker-thread-only, so it is safe to reset them here.
    pidsByConfig.clear();
    configByPid.clear();

    IWbemServices* services = nullptr;
    if (!InitWmi(&services))
    {
        AppContext::instance()->logger().Info("ProcMon", "WMI init failed, watcher thread exiting");
        running = false;
        return;
    }

    IEnumWbemClassObject* startEnum = SubscribeWmi(services, L"SELECT * FROM Win32_ProcessStartTrace", "start");
    IEnumWbemClassObject* stopEnum = SubscribeWmi(services, L"SELECT * FROM Win32_ProcessStopTrace", "stop");
    if (!startEnum || !stopEnum)
    {
        AppContext::instance()->logger().Info("ProcMon", "WMI subscription failed, watcher thread exiting");
        if (startEnum) startEnum->Release();
        if (stopEnum) stopEnum->Release();
        services->Release();
        CoUninitialize();
        running = false;
        return;
    }

    // Initial snapshot: the traces only deliver events for processes started
    // AFTER the subscription, so without this a watched process that is
    // already running would never be registered — and when it exits, allExited
    // never fires (the "didn't switch back to close" symptom).
    Reconcile(services);
    AppContext::instance()->logger().Info("ProcMon", "Monitoring started (start & stop traces + snapshot)");

    ULONGLONG lastSync = GetTickCount64();
    while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
    {
        // ---- process start events ----
        IWbemClassObject* event = nullptr;
        ULONG returned = 0;
        if (SUCCEEDED(startEnum->Next(200, 1, &event, &returned)) && returned > 0)
        {
            DWORD pid = 0;
            std::string name;
            VARIANT vtPid{}, vtName{};
            if (SUCCEEDED(event->Get(L"ProcessID", 0, &vtPid, nullptr, nullptr)) && vtPid.vt == VT_I4)
            {
                pid = static_cast<DWORD>(vtPid.lVal);
                VariantClear(&vtPid);
            }
            if (SUCCEEDED(event->Get(L"ProcessName", 0, &vtName, nullptr, nullptr)) && vtName.vt == VT_BSTR)
            {
                name = BstrToUtf8(vtName.bstrVal);
                VariantClear(&vtName);
            }
            event->Release();
            if (!name.empty())
                HandleStart(pid, name);
        }

        // ---- process stop events ----
        event = nullptr;
        returned = 0;
        if (SUCCEEDED(stopEnum->Next(200, 1, &event, &returned)) && returned > 0)
        {
            DWORD pid = 0;
            VARIANT vtPid{};
            if (SUCCEEDED(event->Get(L"ProcessID", 0, &vtPid, nullptr, nullptr)) && vtPid.vt == VT_I4)
            {
                pid = static_cast<DWORD>(vtPid.lVal);
                VariantClear(&vtPid);
            }
            event->Release();
            if (pid != 0)
                HandleStop(pid);
        }

        // WMI stop traces can be dropped (process killed hard, WMI service
        // under load) and rules can be added while a watched process is
        // already running — reconcile periodically so neither case can leave
        // the tracked set out of sync and block allExited forever.
        const ULONGLONG now = GetTickCount64();
        if (now - lastSync >= 5000)
        {
            lastSync = now;
            Reconcile(services);
        }
    }

    startEnum->Release();
    stopEnum->Release();
    services->Release();
    CoUninitialize();

    AppContext::instance()->logger().Info("ProcMon", "Monitoring stopped");

    running = false;
}

std::string ProcessMonitor::Impl::GetProcessPath(DWORD pid) const
{
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};

    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::string result;
    if (QueryFullProcessImageNameW(hProc, 0, path, &size))
        result = BstrToUtf8(path);
    CloseHandle(hProc);
    return result;
}

void ProcessMonitor::Impl::RegisterProcess(DWORD pid, const std::string& name, const std::string& fullPath, bool emitSignal)
{
    if (configByPid.contains(pid))
        return; // already tracked (e.g. a start event raced the snapshot)

    std::map<std::string, std::string> map;
    {
        std::lock_guard<std::mutex> lock(mutex);
        map = watchMap;
    }
    if (map.empty()) return;

    for (const auto& [exePath, config] : map)
    {
        bool matched = false;
        if (!fullPath.empty() && _stricmp(fullPath.c_str(), exePath.c_str()) == 0)
            matched = true;
        if (!matched)
        {
            const auto pos = exePath.find_last_of("/\\");
            const std::string filename = pos == std::string::npos ? exePath : exePath.substr(pos + 1);
            if (_stricmp(name.c_str(), filename.c_str()) == 0)
                matched = true;
        }
        if (matched)
        {
            pidsByConfig[config].insert(pid);
            configByPid[pid] = config;
            if (emitSignal)
            {
                PostToUi([owner = owner, config, pid] {
                    owner->processMatched(config, pid);
                });
            }
            return;
        }
    }
}

void ProcessMonitor::Impl::HandleStart(DWORD pid, const std::string& name)
{
    RegisterProcess(pid, name, GetProcessPath(pid), true);
}

void ProcessMonitor::Impl::Reconcile(IWbemServices* services)
{
    // Enumerate every running process once, then:
    //   1) register (and emit processMatched for) watched processes that are
    //      running but not tracked yet — covers processes already running when
    //      monitoring started and rules added while such a process was running;
    //   2) prune tracked pids whose process no longer exists — covers stop
    //      events WMI failed to deliver, so allExited still fires when the
    //      last watched process exits.
    // Worker thread only: all map access here is single-threaded.
    IEnumWbemClassObject* snapshot = nullptr;
    HRESULT hr = services->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(L"SELECT ProcessId, Name, ExecutablePath FROM Win32_Process"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &snapshot);
    if (FAILED(hr))
    {
        std::println("[ProcMon] Win32_Process snapshot failed: 0x{:08X}", static_cast<unsigned long>(hr));
        return;
    }

    std::set<DWORD> alive;
    for (;;)
    {
        IWbemClassObject* event = nullptr;
        ULONG returned = 0;
        hr = snapshot->Next(200, 1, &event, &returned);
        if (FAILED(hr) || returned == 0)
            break;

        DWORD pid = 0;
        std::string name, exePath;
        VARIANT vt{};
        if (SUCCEEDED(event->Get(L"ProcessId", 0, &vt, nullptr, nullptr)) && vt.vt == VT_I4)
        {
            pid = static_cast<DWORD>(vt.lVal);
            VariantClear(&vt);
        }
        if (SUCCEEDED(event->Get(L"Name", 0, &vt, nullptr, nullptr)) && vt.vt == VT_BSTR)
        {
            name = BstrToUtf8(vt.bstrVal);
            VariantClear(&vt);
        }
        if (SUCCEEDED(event->Get(L"ExecutablePath", 0, &vt, nullptr, nullptr)) && vt.vt == VT_BSTR)
        {
            exePath = BstrToUtf8(vt.bstrVal);
            VariantClear(&vt);
        }
        event->Release();

        alive.insert(pid);
        if (pid != 0 && !configByPid.contains(pid))
            RegisterProcess(pid, name, exePath, false); // silent: never auto-switch for pre-existing processes
    }
    snapshot->Release();

    std::vector<DWORD> dead;
    for (const auto& entry : configByPid)
    {
        const DWORD pid = entry.first;
        if (!alive.contains(pid))
            dead.push_back(pid);
    }
    for (const DWORD pid : dead)
        HandleStop(pid);
}

void ProcessMonitor::Impl::HandleStop(DWORD pid)
{
    auto it = configByPid.find(pid);
    if (it == configByPid.end())
        return; // not a watched process

    const std::string config = it->second;
    configByPid.erase(it);

    auto& pids = pidsByConfig[config];
    pids.erase(pid);
    if (!pids.empty())
        return; // other instances still running for this config

    pidsByConfig.erase(config);
    if (!pidsByConfig.empty())
        return; // other configs still active

    PostToUi([owner = owner] {
        owner->allExited(); // last watched process exited
    });
}

#endif // _WIN32
