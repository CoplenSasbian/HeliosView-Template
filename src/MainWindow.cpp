// MainWindow implementation: native <-> JS bridge bindings and frontend loading.
//
// Build modes (selected by CMake):
//   - Dev  (HELIOSVIEW_TEMPLATE_DEV=ON):  the WebView loads the frontend dev
//     server (Vite, default http://localhost:5173). HMR works.
//   - Prod (default):                     the WebView loads the built frontend
//     from appRoot/assets/index.html (copied there by the build; appRoot is
//     one level above the exe — e.g. dist\assets for dist\bin\GameTrigger.exe).

#include "MainWindow.h"

#include "AppContext.h"
#include "Logger.h"
#include "utils/AppFilePath.h"
#include "utils/AutoStart.h"
#include <algorithm>
#include <boost/json.hpp>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <string_view>

namespace
{
// Fire-and-forget settings persist (used by the tray toggles and window close);
// defined below in the anonymous namespace.
std::execution::task<void> SaveSettingsAsync(AppSettings& settings, helios::Async& async);

// processRules is stored as std::pair<exe,config> (serializes as ["exe","cfg"]);
// the frontend prefers [{exe, config}]. Convert for the bridge boundary.
boost::json::value processRulesToJson(const std::vector<AppSettings::ProcessRule>& rules)
{
    boost::json::array arr;
    for (const auto& [exe, cfg] : rules)
        arr.push_back(boost::json::object{{"exe", exe}, {"config", cfg}});
    return arr;
}

// Accept [{exe, config}, ...] from the frontend and rebuild the pair list.
bool processRulesFromJson(const boost::json::value& v,
                          std::vector<AppSettings::ProcessRule>& out)
{
    if (!v.is_array())
        return false;
    std::vector<AppSettings::ProcessRule> result;
    for (const auto& item : v.as_array())
    {
        if (!item.is_object())
            return false;
        const auto& o = item.as_object();
        const auto e = o.if_contains("exe");
        const auto c = o.if_contains("config");
        if (!e || !e->is_string() || !c || !c->is_string())
            return false;
        result.emplace_back(e->as_string().c_str(), c->as_string().c_str());
    }
    out = std::move(result);
    return true;
}

// Absolute path to the brand icon next to the executable (icon.ico is staged
// beside the exe by CMake), used for the window icon + system tray icon.
// Returns an empty string if it cannot be resolved.
std::string appIconPath()
{
    std::wstring buf(512, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    const auto icon = std::filesystem::path(buf).parent_path() / "icon.ico";
    if (!std::filesystem::exists(icon)) return {};
    const auto u8 = icon.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
} // namespace

MainWindow::MainWindow(int width, int height, const char* title, bool silent)
    : WebViewWindow(width, height, title,helios::WindowStyle::Frameless)
    , m_pluginManager(AppContext::instance()->logger())
    , m_processMonitor(AppContext::instance()->app())
    , m_silent(silent)
{
    // Minimum client size so the UI is never crushed. Recalculated after the
    // layout grew (full-bleed content, 240px settings control column, param
    // overviews on plugin cards, theme-mode tiles). The widest layout is the
    // settings rows: label block (~300px for the longest descriptions) + 16px
    // gap + 240px fixed control column ≈ 556px of content, plus the 232px
    // sidebar and 32px side padding → ~852px viewport; 920 keeps it
    // comfortable. Height 640 matches the default window (the home page's log
    // console stays fully usable).
    setMinimumSize(960, 640);

    AppContext::instance()->guard().notifyReceived.connect([this]() {
        restore(); focus(); flashUntilFocus();
    });

    // App-level settings load async in setupSettings() (off the UI thread);
    // until then the defaults are active.

    // Start initialization as one task. The native window (hwnd) already
    // exists from Window's constructor, so no first show() is needed: the
    // icon/tray/WebView are set up on the UI thread (started inline here),
    // settings + plugins load on the background pool, and the window shows
    // itself from navigationCompleted once the page is actually rendered.
    AppContext::instance()->app().scope().spawn(
        std::execution::upon_error(InitAsync(), [](auto&&) noexcept {}));

    // Show the window once the page reports it finished loading: move it to
    // the saved position — or center it on the primary monitor for a fresh
    // install — and show it fully rendered, so the first visible frame is the
    // loaded page at its final position: no flash, no jump. Silent startup
    // stays in the tray.
    navigationCompleted.connect([this](int /*error*/) {
        if (m_silent || m_frontendShown) return;
        m_frontendShown = true;

        if (m_settings.windowWidth > 0 && m_settings.windowHeight > 0 &&
            m_settings.windowX >= 0 && m_settings.windowY >= 0)
        {
            setGeometry(m_settings.windowX, m_settings.windowY,
                        m_settings.windowWidth, m_settings.windowHeight);
        }
        else
        {
            int32_t w = 0, h = 0;
            if (size(w, h)) {
                helios::Rect wa{};
                if (helios::primaryWorkArea(wa))
                    setGeometry(wa.x + (wa.width - w) / 2, wa.y + (wa.height - h) / 2, w, h);
                else
                    move(100, 100);
            }
        }
        show();
    });

    // Remember the main-window geometry: track position/size in memory, then
    // persist once when the window is closed (setupSettings restores it on the
    // next launch).
    resized.connect([this](int w, int h) {
        m_settings.windowWidth = w;
        m_settings.windowHeight = h;
    });
    moved.connect([this](int x, int y) {
        m_settings.windowX = x;
        m_settings.windowY = y;
    });
    closeRequested.connect(&Window::hide,static_cast<Window*>(this));

    // Process auto-switch: when a watched process starts, activate its config;
    // when the last watched process exits, fall back to the built-in "close".
    // The monitor posts its signal emissions to the UI thread, so these slots
    // run on the message-loop thread.
    //
    // IMPORTANT: these are SYNC slots and must stay that way. The previous
    // version was an async (fire-and-forget) slot coroutine that hopped to the
    // pool with `co_await schedule(...)` and then accessed `this` members —
    // resuming such a task from the pool resumed with a corrupted `this`
    // (stdexec/MSVC coroutine-frame issue, 0xDD-fill → access violation in
    // PluginConfig::switchConfig). activateConfig runs directly on the UI
    // thread instead; concurrent activations (processMatched / allExited /
    // plugins_activate) are serialized inside PluginManager::activateConfig.
    m_processMonitor.processMatched.connect(
        [this](const std::string& config, unsigned long pid) {
            auto& cfg = m_pluginManager.getPluginConfig();
            if (config == cfg.getActiveConfigName()) return;
            const auto& names = cfg.getConfigNameList();
            if (std::find(names.begin(), names.end(), config) == names.end())
            {
                AppContext::instance()->logger().Warning("procmon", "auto-switch target config '{}' not found, skipped", config);
                return;
            }
            AppContext::instance()->logger().Info("procmon", "process matched (pid {}): auto-switch to config '{}'", pid, config);
            try { ActivateConfig(config, "process"); }
            catch (const std::exception& e)
            {
                AppContext::instance()->logger().Error("procmon", "activateConfig '{}' failed: {}", config, e.what());
            }
        });

    m_processMonitor.allExited.connect([this]() {
        auto& cfg = m_pluginManager.getPluginConfig();
        if (cfg.getActiveConfigName() == "close") return;
        const auto& names = cfg.getConfigNameList();
        if (std::find(names.begin(), names.end(), "close") == names.end()) return;
        AppContext::instance()->logger().Info("procmon", "all watched processes exited, switching to 'close'");
        try { ActivateConfig("close", "process"); }
        catch (const std::exception& e)
        {
            AppContext::instance()->logger().Error("procmon", "activateConfig 'close' failed: {}", e.what());
        }
    });

    // (Plugins are loaded by setupPlugins(), called from InitAsync above.)



}

MainWindow::~MainWindow()
{
    AppContext::instance()->logger().removeLogListener(m_logSinkId);
}

// ---- native <-> JS bridge --------------------------------------------------
//
// Callable from the frontend via window.helios.call(...). Implement your own
// bindings here with bindJson<...>(...):
//   // lambda form:
//   bindJson<boost::json::value>("myFn", [](boost::json::value req)
//       -> std::execution::task<boost::json::value> {
//           // ... native logic ...
//           co_return boost::json::value{{"ok", true}};
//       });
//   // member-function form (preferred — see setupBridge below):
//   bindJson<std::string>("myFn", this, &MainWindow::myFn);
//
// The demo bindings (appInfo / ping / add) were removed. Add the functions
// your frontend needs here.

namespace
{
// Persist app settings off the UI thread (fire-and-forget for tray toggles).
// The tray menu itself runs on the UI thread (Tray::notify is UI-thread only),
// so persistence is detached instead of awaited from a pool thread.
std::execution::task<void> SaveSettingsAsync(AppSettings& settings, helios::Async& async)
{
    try { co_await settings.save(async); }
    catch (...) { /* persist failure: the in-memory value still applies */ }
    co_return;
}

const char* paramTypeName(PluginParameterType type)
{
    switch (type)
    {
    case PluginParameterType::Int: return "int";
    case PluginParameterType::Double: return "double";
    case PluginParameterType::String: return "string";
    case PluginParameterType::Boolean: return "bool";
    case PluginParameterType::Datetime: return "datetime";
    case PluginParameterType::File: return "file";
    case PluginParameterType::Folder: return "folder";
    case PluginParameterType::Select: return "select";
    }
    return "unknown";
}

// Serialize the parameter METADATA (PluginParameterInfo) — the frontend uses
// this to render the form (control kind, label, min/max, defaults).
boost::json::object infoToJson(const PluginParameterInfo& info)
{
    boost::json::object obj;
    obj["name"] = info.name;
    obj["label"] = info.label ? info.label : "";
    obj["desc"] = info.description ? info.description : "";
    obj["type"] = paramTypeName(info.type);
    switch (info.type)
    {
    case PluginParameterType::Int:
        obj["min"] = info.intValue.minValue;
        obj["max"] = info.intValue.maxValue;
        obj["defaultValue"] = info.intValue.defaultValue;
        break;
    case PluginParameterType::Double:
        obj["min"] = info.doubleValue.minValue;
        obj["max"] = info.doubleValue.maxValue;
        obj["step"] = info.doubleValue.step;
        obj["defaultValue"] = info.doubleValue.defaultValue;
        break;
    case PluginParameterType::String:
        obj["defaultValue"] = info.stringValue.defaultValue ? info.stringValue.defaultValue : "";
        break;
    case PluginParameterType::Boolean:
        obj["defaultValue"] = info.boolValue.defaultValue;
        break;
    case PluginParameterType::Datetime:
        obj["defaultValue"] = info.datetimeValue.defaultValue;
        break;
    case PluginParameterType::File:
        obj["defaultValue"] = info.fileValue.defaultValue ? info.fileValue.defaultValue : "";
        obj["filter"] = info.fileValue.filter ? info.fileValue.filter : "";
        break;
    case PluginParameterType::Folder:
        obj["defaultValue"] = info.folderValue.defaultValue ? info.folderValue.defaultValue : "";
        break;
    case PluginParameterType::Select:
    {
        boost::json::array options;
        if (info.selectValue.options && info.selectValue.optionCount > 0)
            for (int i = 0; i < info.selectValue.optionCount; i++)
                options.push_back(info.selectValue.options[i] ? info.selectValue.options[i] : "");
        obj["options"] = std::move(options);
        obj["defaultValue"] = info.selectValue.defaultValue;
        break;
    }
    }
    return obj;
}

// Serialize the CURRENT VALUE of one parameter (from the active config).
boost::json::value paramValueToJson(PluginParameterValue& value, const PluginParameterInfo& info)
{
    switch (info.type)
    {
    case PluginParameterType::Int: return value.getInt64Value(info.name);
    case PluginParameterType::Double: return value.getDoubleValue(info.name);
    case PluginParameterType::String: return value.getStringValue(info.name);
    case PluginParameterType::Boolean: return value.getBoolValue(info.name);
    case PluginParameterType::Datetime: return value.getDateTimeValue(info.name);
    case PluginParameterType::File: return value.getFileValue(info.name);
    case PluginParameterType::Folder: return value.getFolderValue(info.name);
    case PluginParameterType::Select: return value.getInt64Value(info.name);
    }
    return nullptr;
}

// Write a JSON value into a plugin parameter, according to its declared type.
void setParamValue(PluginParameterValue& value, const char* name, const boost::json::value& v)
{
    // 特殊参数: _enabled — 配置中禁用/启用该插件 (不是插件声明的参数)
    if (name && std::string_view{name} == "_enabled")
    {
        value.setBoolValue(name, v.is_bool() ? v.as_bool() : true);
        return;
    }

    switch (value.getType(name))
    {
    case PluginParameterType::Int:
        value.setInt64Value(name, v.to_number<int64_t>());
        break;
    case PluginParameterType::Double:
        value.setDoubleValue(name, v.to_number<double>());
        break;
    case PluginParameterType::String: value.setStringValue(name, v.as_string().c_str()); break;
    case PluginParameterType::Boolean: value.setBoolValue(name, v.as_bool()); break;
    case PluginParameterType::Datetime:
        value.setDateTimeValue(name, v.to_number<int64_t>());
        break;
    case PluginParameterType::File: value.setFileValue(name, v.as_string().c_str()); break;
    case PluginParameterType::Folder: value.setFolderValue(name, v.as_string().c_str()); break;
    case PluginParameterType::Select: value.setInt64Value(name, v.to_number<int64_t>()); break;
    }
}
} // namespace

void MainWindow::setupBridge()
{
    // Handlers are bound directly as member functions (the library wraps
    // (obj, method) into the handler for us); only the Args... stay explicit.
    // Reads are consolidated into a single whole-object config_get; the action
    // handlers below stay separate (they mutate state, not just read it).
    bindJson<>("config_get", this, &MainWindow::config_get);
    bindJson<std::string>("plugins_getParamValues", this, &MainWindow::pluginsGetParamValues);
    bindJson<std::string>("plugins_activate", this, &MainWindow::pluginsActivate);
    bindJson<std::string, std::string, std::string>("plugins_pickPath", this, &MainWindow::pluginsPickPath);
    bindJson<std::string, std::vector<ParamSetReq>>("plugins_setParams", this, &MainWindow::pluginsSetParams);
    bindJson<std::string>("plugins_createConfig", this, &MainWindow::pluginsCreateConfig);
    bindJson<std::string>("plugins_deleteConfig", this, &MainWindow::pluginsDeleteConfig);

    bindJson<>("settings_get", this, &MainWindow::settings_get);
    bindJson<boost::json::value>("settings_set", this, &MainWindow::settings_set);

    bindJson<bool, int>("wallpaper_fetch", this, &MainWindow::wallpaperFetch);
    bindJson<>("bg_list", this, &MainWindow::bgList);
    bindJson<std::string>("bg_load", this, &MainWindow::bgLoad);
    bindJson<std::string>("bg_loadThumb", this, &MainWindow::bgLoadThumb);
    bindJson<std::string, std::string>("shell_reveal", this, &MainWindow::shellReveal);
    bindJson<std::string>("shell_openDir", this, &MainWindow::shellOpenDir);
}

// ---- whole-object config state ----------------------------------------------

std::execution::task<boost::json::value> MainWindow::config_get()
{
    auto& cfg = m_pluginManager.getPluginConfig();
    const auto& activeConfig = cfg.getActiveConfigName();

    boost::json::object result;
    result["activeConfig"] = activeConfig;

    boost::json::array configs;
    for (const auto& name : m_pluginManager.getConfigNameList())
        configs.push_back(boost::json::value(name));
    result["configs"] = std::move(configs);

    boost::json::array plugins;
    for (const auto& name : m_pluginManager.getPluginNames())
    {
        IPlugin* plugin = m_pluginManager.getPluginByName(name);
        plugins.push_back(boost::json::object{{"name", name},
                                              {"version", plugin ? plugin->version() : 0},
                                              {"description", plugin ? plugin->description() : ""}});
    }
    result["plugins"] = std::move(plugins);

    // Parameter metadata per plugin (renders the form for any config).
    boost::json::object infos;
    for (const auto& pluginName : m_pluginManager.getPluginNames())
    {
        boost::json::array arr;
        for (const auto& pi : cfg.getInfo(pluginName))
        {
            if (!pi.name) continue;
            arr.push_back(infoToJson(pi));
        }
        infos[pluginName] = std::move(arr);
    }
    result["paramInfos"] = std::move(infos);

    // Parameter values for the ACTIVE config (plugins_getParamValues(config)
    // reads a non-active one; that stays a separate parameterized read).
    result["paramValues"] = co_await paramValuesFor(activeConfig);

    AppContext::instance()->logger().Info("bridge", "config_get -> active='{}', {} configs, {} plugins",
                   activeConfig, result["configs"].as_array().size(),
                   result["plugins"].as_array().size());
    co_return std::move(result);
}

// Read the parameter values for a named (possibly non-active) config.
std::execution::task<boost::json::object> MainWindow::paramValuesFor(const std::string& config)
{
    auto& cfg = m_pluginManager.getPluginConfig();
    const auto& activeConfig = cfg.getActiveConfigName();

    const bool switched = config != activeConfig;
    if (switched && !cfg.switchConfig(config))
        throw std::runtime_error(std::format("Config '{}' not found", config));

    boost::json::object params;
    if (!config.empty())
    {
        for (const auto& pluginName : m_pluginManager.getPluginNames())
        {
            PluginParameterValue* value = cfg.getCurrentConfigParameter(pluginName);
            if (!value) continue;
            boost::json::object obj;
            for (const auto& pi : cfg.getInfo(pluginName))
            {
                if (!pi.name) continue;
                obj[pi.name] = paramValueToJson(*value, pi);
            }
            obj["_enabled"] = cfg.isPluginEnabled(pluginName);
            params[pluginName] = std::move(obj);
        }
    }

    if (switched) cfg.switchConfig(activeConfig);
    co_return params;
}

std::execution::task<boost::json::object> MainWindow::pluginsGetParamValues(std::string config)
{
    // Empty config = the active one; a named config reads that specific one.
    auto& cfg = m_pluginManager.getPluginConfig();
    const std::string target = config.empty() ? cfg.getActiveConfigName() : config;
    co_return co_await paramValuesFor(target);
}

std::execution::task<bool> MainWindow::pluginsActivate(std::string config)
{
    // activateConfig may block in plugin execute() — switch to the pool
    // scheduler inside this coroutine instead of blocking the UI thread.
    co_await std::execution::schedule(AppContext::instance()->async().get_scheduler());
    ActivateConfig(std::move(config), "activate"); // throws → JS promise rejects
    co_return true;
}

std::execution::task<PickPathResp> MainWindow::pluginsPickPath(std::string type, std::string title, std::string filter)
{
    std::string path;
    bool ok = false;
    if (type == "folder")
    {
        ok = helios::selectFolder(nativeHandle(), title.c_str(), path);
    }
    else
    {
        // "exe" (process-monitor browse) restricts the dialog to executables;
        // plugin `file` params pass their own filter ("" = all files).
        const char* f = nullptr;
        if (type == "exe")
            f = "可执行文件 (*.exe)|*.exe";
        else if (!filter.empty())
            f = filter.c_str();
        auto files = helios::openFiles(nativeHandle(), title.c_str(), f);
        ok = !files.empty();
        if (ok) path = files.front();
    }
    co_return PickPathResp{ok, ok ? path : ""};
}

std::execution::task<bool> MainWindow::pluginsSetParams(std::string config, std::vector<ParamSetReq> params)
{
    auto& cfg = m_pluginManager.getPluginConfig();

    // Empty config = the active one. Editing a non-active config temporarily
    // switches to it, writes, saves, then switches back.
    const std::string prev = cfg.getActiveConfigName();
    const std::string target = config.empty() ? prev : config;
    const bool switched = target != prev;
    if (switched && !cfg.switchConfig(target))
        throw std::runtime_error(std::format("Config '{}' not found", target));

    try
    {
        for (auto& item : params)
        {
            PluginParameterValue* value = cfg.getCurrentConfigParameter(item.plugin);
            if (!value)
                throw std::runtime_error(std::format(
                    "No active parameter set for plugin '{}' (active config: '{}')",
                    item.plugin, cfg.getActiveConfigName()));
            auto declaredType = value->getType(item.name.c_str());
            AppContext::instance()->logger().Info("bridge", "plugins_setParams: config='{}' plugin='{}' param='{}' declaredType={} value={}",
                           target, item.plugin, item.name,
                           paramTypeName(declaredType), boost::json::serialize(item.value));
            setParamValue(*value, item.name.c_str(), item.value);
        }
        co_await cfg.save();
    }
    catch (...)
    {
        if (switched) cfg.switchConfig(prev);
        throw;
    }
    if (switched) cfg.switchConfig(prev);
    BroadcastParamsSaved();
    co_return true;
}

std::execution::task<bool> MainWindow::pluginsCreateConfig(std::string name)
{
    if (name.empty())
        throw std::runtime_error("配置名称不能为空");

    bool ok = co_await m_pluginManager.getPluginConfig().createConfig(name);
    if (!ok)
        throw std::runtime_error(std::format("配置 '{}' 已存在", name));

    // Same as pluginsActivate: the activation may block in plugin execute(),
    // so hop to the pool before running it.
    co_await std::execution::schedule(AppContext::instance()->async().get_scheduler());
    ActivateConfig(name, "create");
    BroadcastConfigsChanged(); // config list changed too
    co_return true;
}

std::execution::task<bool> MainWindow::pluginsDeleteConfig(std::string name)
{
    // The built-in "close" config is the off state — never deletable (the UI
    // also disables it; this guards direct bridge calls).
    if (name == "close")
        throw std::runtime_error("内置配置 'close' 不能删除");
    bool ok = m_pluginManager.getPluginConfig().deleteConfig(name);
    if (!ok)
        throw std::runtime_error(std::format("配置 '{}' 不存在", name));
    BroadcastConfigsChanged(); // deletion may also fall the active config back
    co_return true;
}

// ---- app settings -----------------------------------------------------------

std::execution::task<boost::json::value> MainWindow::settings_get()
{
    // Whole object via the BOOST_DESCRIBE'd AppSettings reflection, PLUS the
    // runtime fields that are never persisted: the process-monitor running
    // state, the currently active config, and the OS auto-start registration
    // (autoStart). One call gives a connected page everything it needs.
    auto obj = boost::json::value_from(m_settings).as_object();
    // processRules is stored as pair<string,string> ([]"exe","config"]); expose
    // it to the frontend in its natural {exe, config} object form.
    obj["processRules"] = processRulesToJson(m_settings.processRules);
    obj["processRunning"] = m_processMonitor.IsRunning();
    obj["activeConfig"]   = m_pluginManager.getPluginConfig().getActiveConfigName();
    // autoStart is not persisted: it is the OS registration (Task Scheduler
    // task) and is reported live from the OS.
    obj["autoStart"] = IsAutoStartEnabled();
    AppContext::instance()->logger().Info("bridge", "settings_get");
    co_return std::move(obj);
}

std::execution::task<boost::json::value> MainWindow::settings_set(boost::json::value patch)
{
    // Merge the partial patch onto current values, then write the whole thing.
    if (!patch.is_object())
        throw std::runtime_error("settings_set: expected an object");

    auto obj = boost::json::value_from(m_settings).as_object();
    bool hasProcessRules = false;
    std::vector<AppSettings::ProcessRule> incomingRules;
    for (const auto& [key, val] : patch.as_object())
    {
        // Only persisted AppSettings keys are accepted; read-only runtime
        // fields (reported by settings_get) are ignored.
        if (key == "processRunning" || key == "activeConfig")
            continue;
        if (key == "autoStart")
        {
            // autoStart is NOT persisted — the OS registration (Task Scheduler
            // logon task) is the single source of truth, so apply it directly
            // with its failure path. Whatever the outcome, the response below
            // reports the ACTUAL OS state, so a denied registration surfaces.
            if (!val.is_bool())
                throw std::runtime_error("settings_set: bad autoStart");
            if (val.as_bool() != IsAutoStartEnabled())
                SetAutoStartEnabled(val.as_bool());
            continue;
        }
        if (key == "processRules")
        {
            // Frontend sends [{exe,config}]; rebuild the stored pair list and
            // apply below (it can't be value_to'd in object form).
            if (!processRulesFromJson(val, incomingRules))
                throw std::runtime_error("settings_set: bad processRules");
            hasProcessRules = true;
            continue;
        }
        obj[key] = val;
    }

    boost::json::value merged = std::move(obj);
    m_settings = boost::json::value_to<AppSettings>(merged);
    if (hasProcessRules)
        m_settings.processRules = std::move(incomingRules);

    // A process change triggers the monitor (processAutoSwitch / processRules).
    ApplyProcessMonitor();

    co_await m_settings.save(AppContext::instance()->async());
    BroadcastSettingsChanged(); // keep UI + tray menu in sync
    AppContext::instance()->logger().Info("bridge", "settings_set applied");
    // Return the same flattened view as settings_get so callers stay consistent.
    auto out = boost::json::value_from(m_settings).as_object();
    out["processRules"] = processRulesToJson(m_settings.processRules);
    out["processRunning"] = m_processMonitor.IsRunning();
    out["autoStart"] = IsAutoStartEnabled();
    co_return std::move(out);
}

void MainWindow::Broadcast(const char* channel, const char* reason)
{
    if (!m_frontendReady) return; // WebView not up yet — broadcast would be dropped
    boost::json::object obj;
    obj["reason"] = reason;
    obj["activeConfig"] = m_pluginManager.getPluginConfig().getActiveConfigName();
    broadcast(channel, boost::json::serialize(obj).c_str());
}

void MainWindow::BroadcastConfigActivated(const char* reason) { Broadcast("configActivated", reason); }
void MainWindow::BroadcastParamsSaved() { Broadcast("paramsSaved", "params"); }
void MainWindow::BroadcastConfigsChanged() { Broadcast("configsChanged", "configs"); }

void MainWindow::BroadcastSettingsChanged()
{
    if (!m_frontendReady) return; // WebView not up yet — broadcast would be dropped

    // Whole AppSettings via reflection, plus the live process-monitor state
    // (a runtime value that isn't part of the persisted AppSettings).
    auto obj = boost::json::value_from(m_settings).as_object();
    obj["processRules"]   = processRulesToJson(m_settings.processRules);
    obj["processRunning"] = m_processMonitor.IsRunning();
    obj["autoStart"]      = IsAutoStartEnabled();
    broadcast("settingsChanged", boost::json::serialize(obj).c_str());
}

void MainWindow::ActivateConfig(const std::string& config, const char* reason)
{
    m_pluginManager.activateConfig(config); // may throw — caller decides
    BroadcastConfigActivated(reason);

    // System notification on config switch (manual, tray or auto-switch).
    if (m_tray && m_tray->valid())
    {
        const bool off = config == "close";
        const std::string msg = off ? "当前未启用任何配置" : ("当前配置：" + config);
        m_tray->notify(off ? "配置已关闭" : "配置已切换", msg.c_str());
    }
}

// ---- system tray + context menu -------------------------------------------

void MainWindow::setupTray()
{
    // Brand icon next to the exe (staged by CMake); "" falls back to the
    // library's default tray icon.
    const std::string icon = appIconPath();
    m_tray = std::make_unique<helios::Tray>(nativeHandle(), "GameTrigger",
                                            icon.empty() ? nullptr : icon.c_str());
    if (!m_tray->valid())
    {
        AppContext::instance()->logger().Warning("tray", "failed to create system tray icon");
        m_tray.reset();
        return;
    }
    AppContext::instance()->logger().Info("tray", "system tray created");

    m_tray->leftDoubleClicked.connect([this] { showNormal();  focus();});
    m_tray->rightClicked.connect([this] { ShowTrayMenu(); });
}

void MainWindow::ShowTrayMenu()
{
    RebuildMenu();
    if (m_menu && m_menu->valid())
        m_menu->show(nativeHandle());
}

void MainWindow::RebuildMenu()
{
    m_menu = std::make_unique<helios::Menu>(nativeHandle());
    if (!m_menu->valid())
    {
        m_menu.reset();
        return;
    }

    // Add an item and connect its trigger; a failed addItem is skipped instead
    // of dereferencing nullptr.
    const auto addItem = [](helios::Menu* menu, const char* label, auto&& onTrigger) {
        if (auto* item = menu->addItem(label))
            item->triggered.connect(std::forward<decltype(onTrigger)>(onTrigger));
    };
    // Same, but with an on-screen checkmark (initial state = checked).
    const auto addCheckItem = [](helios::Menu* menu, const char* label, bool checked, auto&& onTrigger) {
        if (auto* item = menu->addCheckItem(label, checked))
            item->triggered.connect(std::forward<decltype(onTrigger)>(onTrigger));
    };

    // 打开主界面（从托盘恢复/显示窗口）
    addItem(m_menu.get(), "打开主界面", [this] { showNormal(); });

    // 切换配置（二级菜单：关闭 + 全部配置，当前激活的打勾）
    if (auto* switchMenu = m_menu->addSubmenu("切换配置"))
    {
        const auto activeConfig = m_pluginManager.getPluginConfig().getActiveConfigName();
        const auto names = m_pluginManager.getConfigNameList(); // copy: read under the lock

        addCheckItem(switchMenu, "关闭（不启用）", activeConfig == "close", [this] {
            try { ActivateConfig("close", "tray"); }
            catch (const std::exception& e)
            {
                AppContext::instance()->logger().Error("tray", "切换配置 'close' 失败: {}", e.what());
            }
        });
        for (const auto& name : names)
        {
            if (name == "close") continue;
            addCheckItem(switchMenu, name.c_str(), name == activeConfig, [this, name] {
                try { ActivateConfig(name, "tray"); }
                catch (const std::exception& e)
                {
                    AppContext::instance()->logger().Error("tray", "切换配置 '{}' 失败: {}", name, e.what());
                }
            });
        }
    }

    m_menu->addSeparator();

    // 开机启动（可勾选项，每次打开菜单时重建，勾选态读 OS 注册；点击切换。
    // 状态只存在于任务计划程序里，不写入 app.json）
    addCheckItem(m_menu.get(), "开机启动", IsAutoStartEnabled(), [this] {
        const bool on = !IsAutoStartEnabled();
        const bool ok = SetAutoStartEnabled(on);
        if (m_tray && m_tray->valid())
            m_tray->notify("开机启动", ok ? (on ? "已开启开机启动" : "已关闭开机启动")
                                          : "设置失败（任务计划程序拒绝）");
        BroadcastSettingsChanged(); // keep UI + tray menu in sync
    });

    // 进程监控（可勾选项，点击切换；UI 线程执行，保存异步 fire-and-forget）
    addCheckItem(m_menu.get(), "进程监控", m_settings.processAutoSwitch, [this] {
        m_settings.processAutoSwitch = !m_settings.processAutoSwitch;
        ApplyProcessMonitor();
        const bool on = m_settings.processAutoSwitch;
        if (m_tray && m_tray->valid())
            m_tray->notify("进程监控", on ? "已开启进程自动切换" : "已关闭进程自动切换");
        AppContext::instance()->app().scope().spawn(SaveSettingsAsync(m_settings, AppContext::instance()->async()));
        BroadcastSettingsChanged(); // keep UI + tray menu in sync
    });

    m_menu->addSeparator();

    addItem(m_menu.get(), "退出", [this]
    {
        std::execution::sync_wait( SaveSettingsAsync(m_settings, AppContext::instance()->async()));
        AppContext::instance()->app().quit();
    });
}

void MainWindow::ApplyProcessMonitor()
{
    ProcessMonitor::ProcessMap map;
    for (const auto& [exe, config] : m_settings.processRules)
        map.emplace_back(exe, config);
    m_processMonitor.SetWatchMap(map);

    if (m_settings.processAutoSwitch && !map.empty())
    {
        if (m_processMonitor.Start())
            AppContext::instance()->logger().Info("procmon", "process monitor started ({} rules)", map.size());
        else
            AppContext::instance()->logger().Error("procmon", "process monitor failed to start");
    }
    else
    {
        m_processMonitor.Stop();
        AppContext::instance()->logger().Info("procmon", "process monitor stopped (enabled={}, rules={})",
                       m_settings.processAutoSwitch, map.size());
    }
}

// ---- Bing wallpaper ---------------------------------------------------------

std::execution::task<boost::json::value> MainWindow::wallpaperFetch(bool fresh, int idx)
{
    // Runs on the Async pool inside Wallpaper::fetch; returns a data: URL for
    // Bing's wallpaper `idx` days ago (0 = today, -1 = yesterday, ...).
    const std::string dataUrl = co_await m_wallpaper.fetch(fresh, idx);
    AppContext::instance()->logger().Info("bridge", "wallpaper_fetch(fresh={}, idx={}) -> {}", fresh, idx,
                   dataUrl.empty() ? "<fallback gradient>" : "ok");
    co_return boost::json::value{{"ok", !dataUrl.empty()}, {"url", dataUrl}};
}

// ---- local background gallery (AppRoot()/bg) --------------------------------

std::execution::task<boost::json::value> MainWindow::bgList()
{
    // Image names only — the grid builds thumbnails by calling bg_load lazily.
    const auto names = m_bgImages.list();
    boost::json::array arr;
    for (const auto& n : names)
        arr.push_back(boost::json::value(n));
    co_return boost::json::value{{"ok", true}, {"images", std::move(arr)}};
}

std::execution::task<boost::json::value> MainWindow::bgLoad(std::string name)
{
    // Reading + base64-coding a full (often multi-MB 4K) image off the UI thread:
    // hop to the background pool, do the heavy I/O, then return on the UI loop.
    co_await std::execution::schedule(AppContext::instance()->async().get_scheduler());
    const std::string data = m_bgImages.load(name);
    co_return boost::json::value{{"ok", !data.empty()}, {"name", std::move(name)},
                                 {"url", data}};
}

std::execution::task<boost::json::value> MainWindow::bgLoadThumb(std::string name)
{
    // GDI+ decode + resize can take tens of ms on big files — keep it off the UI
    // thread too so the picker grid scrolls smoothly.
    co_await std::execution::schedule(AppContext::instance()->async().get_scheduler());
    const std::string data = m_bgImages.loadThumb(name, 320);
    co_return boost::json::value{{"ok", !data.empty()}, {"name", std::move(name)},
                                 {"url", data}};
}

// Reveal one specific file in Explorer with it selected: a plugin dll (by its
// display name) or a background image (by its file name). helios::showInFolder
// opens Explorer and selects the file.
std::execution::task<bool> MainWindow::shellReveal(std::string type, std::string name)
{
    std::filesystem::path file;
    if (type == "plugin")
    {
        const std::string p = m_pluginManager.getPluginPath(name);
        if (!p.empty())
            file = p;
    }
    else if (type == "bg")
    {
        file = BgImages::bgDir() / name;
    }
    if (file.empty())
        co_return false;
    co_return helios::showInFolder(file.string());
}

// Open an app directory in Explorer and navigate INTO it (unlike shellReveal,
// which selects a file in its parent). Passing the folder path to
// helios::openUrl makes ShellExecute "open" it with Explorer.
std::execution::task<bool> MainWindow::shellOpenDir(std::string type)
{
    std::filesystem::path dir;
    if (type == "logs")
        dir = LogDir();
    else if (type == "plugins")
        dir = PluginDir();
    else if (type == "bg")
        dir = BgImages::bgDir();
    else
        dir = AppRoot();

    if (dir.empty())
        co_return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    co_return helios::openUrl(dir.string());
}

// ---- frontend loading ------------------------------------------------------

#ifdef HELIOSVIEW_TEMPLATE_DEV
// ---------------- dev mode: the frontend dev server -------------------------
static const char* startUrl()
{
    return HELIOSVIEW_TEMPLATE_DEV_URL;   // e.g. "http://localhost:5173"
}
#else
// ---------------- prod mode: built assets at the app root (../assets) -------

// The directory holding the built frontend (AppRoot()/assets — one level above
// the exe, so an exe in bin\ reads ../assets). AppRoot() is argv-independent
// (GetModuleFileName*): the exe may be launched from anywhere.
static std::string assetsDir()
{
    const auto p = AppRoot() / "assets";
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
#endif

void MainWindow::loadFrontend()
{
    setupBridge();  // must run after createWebView(): binds are dropped otherwise
    m_frontendReady = true; // broadcasts are safe once the WebView is up

    // Connect logger entries to the WebView broadcast channel.
    // The listener is called from the log-writing thread; postTask delivers
    // it to the UI thread where broadcast() must run.
    m_logSinkId = AppContext::instance()->logger().addLogListener([this](const std::string& json) {
        AppContext::instance()->app().postTask([this, json] {
            broadcast("log", json.c_str());
        });
    });

#ifdef HELIOSVIEW_TEMPLATE_DEV
    const std::string url = startUrl();
    std::println("[GameTrigger] dev mode:  loading {}", url);
#else
    // Prod builds are packaged for end users: keep WebView2 DevTools
    // (F12, right-click Inspect) closed. The setting is stored on the
    // webview and applied when it becomes ready.
    setDevToolsEnabled(false);

    // The Vite output is ES modules; WebView2 blocks module scripts from
    // file:// with a CORS error (file is not a supported scheme), so a
    // file:// URL would show a blank page. Instead map the built frontend
    // to the virtual host "app.local" (WebView2 restricts mappings to the
    // .local suffix) and load it over https://, a supported scheme. The
    // mapping is queued by the library until the WebView is initialized.
    mapLocalFolder("app.local", assetsDir().c_str());
    const std::string url = "https://app.local/index.html";
    std::println("[GameTrigger] prod mode: loading {}", url);
#endif
    navigate(url.c_str());
}

// One-shot startup task, spawned by the constructor (see AppContext.h: every
// window/WebView API must run on the message-loop thread — the constructor
// runs on it, and spawn starts the task inline there, before the first
// co_await hops to the background pool). Settings/plugins are pool-safe; the
// icon/tray/WebView work happens on the UI thread before the hop. The window
// is shown by the navigationCompleted handler once the page finishes loading.
std::execution::task<void> MainWindow::InitAsync()
{
    // UI thread (inline from the constructor): brand icon + tray first — both
    // only need the native window, which exists from Window's constructor.
    if (const std::string icon = appIconPath(); !icon.empty())
        setIcon(icon.c_str());
    setupTray();

    // WebView: create it and load the frontend. navigate() is queued by the
    // library until the WebView finishes initializing.
    //
    // WebView2's browser data (profile, cache, cookies) goes to the per-user
    // folder reported by WebView2DataDir() instead of the default
    // "<exe>.WebView2" next to the executable — required once the exe lives in
    // a read-only location (Program Files, ...). The folder is locked in at
    // environment creation, so it must be passed here, before navigation.
    if (const std::filesystem::path udf = WebView2DataDir(); !udf.empty()) {
        const auto u8 = udf.u8string();
        createWebView(std::string(reinterpret_cast<const char*>(u8.data()), u8.size()).c_str());
    } else {
        createWebView();  // no per-user dir available: keep the runtime default
    }
    loadFrontend();

    // Background pool: settings (file I/O), then plugins (DLL loading). Each
    // step is isolated: one failure must not silently kill the other, or the
    // frontend is left with no configs/plugins (observed: a throwing
    // AppSettings::load on a fresh install used to abort InitAsync before
    // setupPlugins ran — empty UI until the app was restarted). Exceptions are
    // logged here and swallowed so the app keeps running.
    try { co_await setupSettings(); }
    catch (const std::exception& e)
    {
        AppContext::instance()->logger().Error("init", "setupSettings failed: {}", e.what());
    }
    try { co_await setupPlugins(); }
    catch (const std::exception& e)
    {
        AppContext::instance()->logger().Error("init", "setupPlugins failed: {}", e.what());
    }
    co_return;
}

// App settings + process monitor — separate init step from plugin loading,
// so each concern is independent (the monitor callbacks guard configs).
std::execution::task<void> MainWindow::setupSettings()
{
    // Load async on the background pool, into a LOCAL. Never write m_settings
    // from the pool: the UI thread mutates it in-place (settings_set full
    // replacement, resized/moved geometry tracking, tray toggles), and a full
    // *this = value_to(...) here raced those writes. Observed: the persisted
    // processAutoSwitch=true was loaded and logged, yet ApplyProcessMonitor
    // read false a millisecond later, so the monitor silently never started.
    AppSettings loaded;
    if (co_await loaded.load(AppContext::instance()->async()))
        AppContext::instance()->logger().Info("settings", "loaded app settings (autoStart={}, popupPosition={}, processAutoSwitch={})",
                       IsAutoStartEnabled(), loaded.popupPosition, loaded.processAutoSwitch);
    else
        AppContext::instance()->logger().Info("settings", "no app settings yet, using defaults");

    // Apply on the UI thread — the single owner of m_settings — then reconcile
    // the process monitor and push the authoritative snapshot to the frontend.
    // (This runs before navigationCompleted shows the window, so the geometry
    // handling there still sees the loaded values.)
    AppContext::instance()->app().postTask([this, loaded = std::move(loaded)]() mutable {
        m_settings = std::move(loaded);
        ApplyProcessMonitor();
        BroadcastSettingsChanged();
    });
    co_return;
}

std::execution::task<void> MainWindow::setupPlugins()
{
    co_await m_pluginManager.loadPlugins();
}
