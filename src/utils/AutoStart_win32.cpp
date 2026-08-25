// AutoStart_win32.cpp — Windows implementation of the AutoStart API.
//
// The exe has a requireAdministrator manifest (WMI process traces, see
// src/app.manifest), so the classic per-user Run key cannot auto-start it:
// explorer launches Run-key entries unelevated at logon, and an elevated-only
// exe simply refuses to start (ERROR_ELEVATION_REQUIRED). Instead we register
// a per-user Task Scheduler logon task with "Run with highest privileges":
// the scheduler service itself performs the elevation, so the app starts
// silently at logon with no UAC prompt.
//
// Guarded by _WIN32; non-Windows builds simply get no implementation
// (link the matching _<platform>.cpp there).

#include "AutoStart.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <lmcons.h>     // UNLEN (GetUserNameW buffer size)
#include <taskschd.h>   // Task Scheduler COM interfaces

#include <string>

namespace
{
// Task identity — one logon task per machine user (root folder keeps it
// visible in the Task Scheduler console and Task Manager's startup tab).
constexpr const wchar_t* kTaskFolder = L"\\";
constexpr const wchar_t* kTaskName   = L"GameTrigger";

// Legacy auto-start location (pre-task versions). The value is inert for an
// elevated exe — it can never launch — but still shows up in Task Manager's
// startup list, so it is removed whenever we touch the registration.
constexpr const wchar_t* kRunKey        = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValueName  = L"GameTrigger";

// ---- Tiny COM helpers (anonymous namespace) -------------------------------

// CoInitializeEx scope guard. S_FALSE means the thread already had a
// compatible apartment; RPC_E_CHANGED_MODE means it already had a *different*
// one — in both cases we must NOT CoUninitialize, we just reuse it (the Task
// Scheduler object is apartment-agnostic, "Both" threading model).
class ComSession
{
public:
    HRESULT hr = S_FALSE;
    bool uninit = false;

    ComSession()
    {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        uninit = hr == S_OK; // S_FALSE = already initialized, RPC_E_* = leave alone
    }
    ~ComSession() { if (uninit) CoUninitialize(); }

    // COM is usable when we initialized it OR an apartment already existed
    // (same mode → S_FALSE; different mode → RPC_E_CHANGED_MODE — the Task
    // Scheduler object is "Both", so it works on STA too).
    bool usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

template <class T>
class ComPtr
{
public:
    T* p = nullptr;
    ComPtr() = default;
    ~ComPtr() { if (p) p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// BSTR holder for both directions: `Bstr(kName).p` as an in-param, or
// `Bstr out; api->get_X(&out.p);` for out-params (caller frees → we free).
class Bstr
{
public:
    BSTR p = nullptr;
    Bstr() = default;
    explicit Bstr(const wchar_t* s) : p(SysAllocString(s)) {}
    ~Bstr() { if (p) SysFreeString(p); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
};

// VT_EMPTY VARIANT — this SDK's ITaskService::Connect /
// ITaskFolder::RegisterTaskDefinition take VARIANTs; an empty one means
// "not specified" (local machine / current user / no password / no sddl).
class Variant
{
public:
    VARIANT v;
    Variant() { VariantInit(&v); }
    ~Variant() { VariantClear(&v); }
    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
};

// ---- Path / user helpers --------------------------------------------------

std::wstring GetExePath()
{
    wchar_t buf[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    return std::wstring(buf, len);
}

std::wstring ExeDirOf(const std::wstring& exe)
{
    const auto pos = exe.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring{} : exe.substr(0, pos + 1);
}

std::wstring CurrentUserName()
{
    wchar_t buf[UNLEN + 1]{};
    DWORD size = UNLEN + 1;
    if (!GetUserNameW(buf, &size)) return {};
    return std::wstring(buf, size);
}

// ---- Registry (legacy cleanup only) ---------------------------------------

void RemoveLegacyRunValue()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(key, kRunValueName);
    RegCloseKey(key);
}

// ---- Task Scheduler plumbing ----------------------------------------------

// Connect to the local Task Scheduler service and open the root folder.
// On success both ComPtrs hold a live reference; on failure both are empty.
bool ConnectService(ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& folder)
{
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITaskService, reinterpret_cast<void**>(&service.p))))
        return false;
    if (FAILED(service->Connect(Variant().v, Variant().v, Variant().v, Variant().v)))
        return false;
    return SUCCEEDED(service->GetFolder(Bstr(kTaskFolder).p, &folder.p));
}

// ---- Logon-task registration ----------------------------------------------

// Create-or-update the logon task that launches THIS exe with --silent at the
// highest privilege level. No stored password: the task runs only when the
// registering user is interactively logged on (TASK_LOGON_INTERACTIVE_TOKEN),
// which keeps it a per-user auto-start like the old Run key.
bool RegisterTask(const std::wstring& exe)
{
    ComSession com;
    if (!com.usable()) return false;

    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    if (!ConnectService(service, folder)) return false;

    ComPtr<ITaskDefinition> def;
    if (FAILED(service->NewTask(0, &def.p))) return false;

    // RegistrationInfo — cosmetic, shown in the Task Scheduler console.
    if (ComPtr<IRegistrationInfo> ri; SUCCEEDED(def->get_RegistrationInfo(&ri.p)))
    {
        ri->put_Author(Bstr(L"GameTrigger").p);
        ri->put_Description(Bstr(L"启动 GameTrigger（管理员权限，静默到托盘）").p);
    }

    // Principal: interactive logon + HIGHEST — the elevation lives here.
    if (ComPtr<IPrincipal> principal; SUCCEEDED(def->get_Principal(&principal.p)))
    {
        principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
    }

    // Trigger: fire at logon, scoped to the account that enabled it.
    if (ComPtr<ITriggerCollection> triggers; SUCCEEDED(def->get_Triggers(&triggers.p)))
    {
        ComPtr<ITrigger> trigger;
        if (SUCCEEDED(triggers->Create(TASK_TRIGGER_LOGON, &trigger.p)))
        {
            ComPtr<ILogonTrigger> logon;
            if (SUCCEEDED(trigger->QueryInterface(IID_ILogonTrigger,
                                                  reinterpret_cast<void**>(&logon.p))))
            {
                const std::wstring user = CurrentUserName();
                if (!user.empty()) logon->put_UserId(Bstr(user.c_str()).p);
            }
        }
    }

    // Action: "<exe>" --silent, working dir = exe dir (relative paths stay valid).
    if (ComPtr<IActionCollection> actions; SUCCEEDED(def->get_Actions(&actions.p)))
    {
        ComPtr<IAction> action;
        if (SUCCEEDED(actions->Create(TASK_ACTION_EXEC, &action.p)))
        {
            ComPtr<IExecAction> exec;
            if (SUCCEEDED(action->QueryInterface(IID_IExecAction,
                                                 reinterpret_cast<void**>(&exec.p))))
            {
                exec->put_Path(Bstr(exe.c_str()).p);
                exec->put_Arguments(Bstr(L"--silent").p);
                const std::wstring dir = ExeDirOf(exe);
                if (!dir.empty()) exec->put_WorkingDirectory(Bstr(dir.c_str()).p);
            }
        }
    }

    // Settings that matter for a long-lived tray GUI:
    //  - no execution-time limit (the 72h default would kill the app);
    //  - allowed on battery;
    //  - retry if a scheduled start was missed at logon;
    //  - never stack a second instance when one is already running.
    if (ComPtr<ITaskSettings> settings; SUCCEEDED(def->get_Settings(&settings.p)))
    {
        settings->put_ExecutionTimeLimit(Bstr(L"PT0S").p);
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        settings->put_StartWhenAvailable(VARIANT_TRUE);
        settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    }

    ComPtr<IRegisteredTask> result;
    const HRESULT hr = folder->RegisterTaskDefinition(
        Bstr(kTaskName).p,
        def.p,
        TASK_CREATE_OR_UPDATE,
        Variant().v /* userId — empty = registering user */,
        Variant().v /* password — interactive token needs none */,
        TASK_LOGON_INTERACTIVE_TOKEN,
        Variant().v /* sddl */,
        &result.p);
    return SUCCEEDED(hr);
}

// Delete the task; already-gone counts as success (idempotent disable).
bool DeleteTask()
{
    ComSession com;
    if (!com.usable()) return false;

    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    if (!ConnectService(service, folder)) return false;

    const HRESULT hr = folder->DeleteTask(Bstr(kTaskName).p, 0);
    return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

// True when our task exists, is enabled, and still launches THIS exe with
// --silent (protects against stale copies / the app having moved).
bool TaskMatches(const std::wstring& exe)
{
    ComSession com;
    if (!com.usable()) return false;

    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    if (!ConnectService(service, folder)) return false;

    ComPtr<IRegisteredTask> task;
    if (FAILED(folder->GetTask(Bstr(kTaskName).p, &task.p))) return false;

    TASK_STATE state = TASK_STATE_UNKNOWN;
    if (FAILED(task->get_State(&state)) || state == TASK_STATE_DISABLED) return false;

    ComPtr<ITaskDefinition> def;
    if (FAILED(task->get_Definition(&def.p))) return false;

    ComPtr<IActionCollection> actions;
    if (FAILED(def->get_Actions(&actions.p))) return false;

    ComPtr<IAction> action; // get_Item is 1-based; we only ever create one action
    if (FAILED(actions->get_Item(1, &action.p))) return false;

    ComPtr<IExecAction> exec;
    if (FAILED(action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&exec.p))))
        return false;

    Bstr path;
    Bstr args;
    if (FAILED(exec->get_Path(&path.p)) || FAILED(exec->get_Arguments(&args.p)))
        return false;

    if (!path.p || !args.p) return false;
    return std::wstring(path.p) == exe && std::wstring(args.p) == L"--silent";
}
} // namespace

bool IsAutoStartEnabled()
{
    // Migrate: drop the old Run key value (inert for an elevated exe, but it
    // lingers in Task Manager's startup list). Runs on every query, so a stale
    // entry disappears the first time a new build starts.
    RemoveLegacyRunValue();

    const std::wstring exe = GetExePath();
    if (exe.empty()) return false;
    return TaskMatches(exe);
}

bool SetAutoStartEnabled(bool enable)
{
    RemoveLegacyRunValue();

    if (enable)
    {
        const std::wstring exe = GetExePath();
        if (exe.empty()) return false;
        if (!RegisterTask(exe)) return false;
        return IsAutoStartEnabled(); // verify the registration round-trip
    }

    return DeleteTask();
}

#endif // _WIN32