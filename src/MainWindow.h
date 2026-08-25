#pragma once

// MainWindow — the app's main window class. Derives from helios::WebViewWindow
// (a helios::Window with an embedded WebView2), adds the native <-> JS bridge
// bindings, and knows how to load the frontend in both build modes.
//
// This is where native-side app logic starts: bind functions callable from JS
// (window.helios.call), subscribe to BroadcastChannel messages, and react to
// window events (signals like resized/keyPressed on the base class).

#include <HeliosViewCore/HeliosView.h>
#include <HeliosViewCore/Menu.h>
#include <HeliosViewCore/Tray.h>
#include <boost/describe.hpp>
#include <boost/json.hpp>
#include <execution>
#include <string>
#include <vector>
#include "AppSettings.h"
#include "ProcessMonitor.h"
#include "Wallpaper.h"
#include "BgImages.h"
#include "plugin/PluginManager.h"
class Logger;

// Bridge request/response DTOs: any type boost::json::value_to / value_from can
// convert (BOOST_DESCRIBE_STRUCT-annotated structs, strings, containers, ...),
// so handlers don't have to hand-parse boost::json::value. Multi-field requests
// are passed as separate bindJson<...> arguments (args[i] -> Args[i]); DTOs are
// only needed for array elements and multi-field responses.
struct ParamSetReq {
    std::string plugin;
    std::string name;
    boost::json::value value;   // type varies per param: keep the raw JSON
};
BOOST_DESCRIBE_STRUCT(ParamSetReq, (), (plugin, name, value))

struct PickPathResp {
    bool ok = false;
    std::string path;
};
BOOST_DESCRIBE_STRUCT(PickPathResp, (), (ok, path))

// One process-monitor rule: watch `exe` (full path) and auto-activate `config`.
struct ProcessRule {
    std::string exe;
    std::string config;
};
BOOST_DESCRIBE_STRUCT(ProcessRule, (), (exe, config))

class MainWindow : public helios::WebViewWindow {
public:
    // Title is UTF-8 (HeliosView v1.0.0 switched the C API to UTF-8 strings).
    // Uses AppContext::instance() internally — no context reference needed.
    MainWindow(int width, int height, const char* title, bool silent = false);
    ~MainWindow();
    // Load the frontend: the dev server URL in dev builds, the built static
    // files (AppRoot()/assets/index.html — ../assets from the exe) in prod
    // builds. Call after
    // createWebView(); navigation is queued automatically while the WebView
    // is still initializing. Registers the bridge bindings first (bind
    // requires a live WebView: bindings set before createWebView() are
    // silently dropped by the C layer).
    void loadFrontend();
protected:
    // One-shot startup task spawned by the constructor: icon/tray/WebView on
    // the UI thread (the native window exists from construction), then
    // settings/plugins on the background pool. The window shows itself from
    // navigationCompleted once the page is rendered.
    std::execution::task<void> InitAsync();
    std::execution::task<void> setupSettings();  // app settings load + process monitor sync
    std::execution::task<void> setupPlugins();   // plugin loading
private:
    void setupBridge();  // native <-> JS bridge bindings (requires the WebView)

    // Bridge handlers: each corresponds to a window.helios.call() function.
    // Reads are consolidated into one whole-object config_get; the handlers
    // below are actions (they mutate state) or the parameterized value read.
    std::execution::task<boost::json::value> config_get();
    // Read parameter values for a named config (may temporarily switch away
    // from the active one; restores it). Shared by config_get and the
    // parameterized plugins_getParamValues read.
    std::execution::task<boost::json::object> paramValuesFor(const std::string& config);
    std::execution::task<boost::json::object> pluginsGetParamValues(std::string config);
    std::execution::task<bool> pluginsActivate(std::string config);
    std::execution::task<PickPathResp> pluginsPickPath(std::string type, std::string title, std::string filter);
    std::execution::task<bool> pluginsSetParams(std::string config, std::vector<ParamSetReq> params);
    std::execution::task<bool> pluginsCreateConfig(std::string name);
    std::execution::task<bool> pluginsDeleteConfig(std::string name);

    // App settings — whole-object get/set over the bridge (boost::json value_from
    // / value_to on AppSettings, which is BOOST_DESCRIBE'd). settings_get returns
    // the whole object; settings_set merges a partial object, persists to app.json
    // and broadcasts settingsChanged. Adding a field = adding one member to
    // AppSettings; no per-field bridge code. Exception: the autoStart key is
    // intercepted before the merge — it is NOT persisted (OS registration is the
    // only state) and is added to get/broadcast responses as a live runtime field.
    std::execution::task<boost::json::value> settings_get();
    std::execution::task<boost::json::value> settings_set(boost::json::value patch);
    // Sync the monitor's watch map + running state with the settings
    // (processAutoSwitch / processRules live in AppSettings; toggling them via
    // settings_set calls this).
    void ApplyProcessMonitor();

    // Frontend subscribes to per-topic channels instead of one catch-all:
    //   configActivated — a config was activated/switched (manual, auto, create)
    //   paramsSaved     — plugin parameters were saved
    //   configsChanged  — configs were created/deleted (activeConfig may have
    //                     fallen back when the active one was deleted)
    void BroadcastConfigActivated(const char* reason);
    void BroadcastParamsSaved();
    void BroadcastConfigsChanged();
    // Broadcast the full current app settings/process state on "settingsChanged".
    // Emitted from EVERY write point (frontend bridge handlers AND tray menu
    // actions) so the UI and the tray menu stay in sync — one source of truth.
    void BroadcastSettingsChanged();
    // Common broadcast impl: skips until the WebView is up; payload carries
    // reason + current activeConfig.
    void Broadcast(const char* channel, const char* reason);

    // Activate a config and broadcast the change ("stateChanged", reason).
    // Exceptions from PluginManager::activateConfig propagate to the caller
    // (bridge handlers reject the JS promise; signal slots catch + log).
    void ActivateConfig(const std::string& config, const char* reason);

    // ---- system tray + context menu ----
    // Created from InitAsync (only needs the native window, which exists from
    // construction). The tray menu is rebuilt each time it opens so
    // configs/labels stay current.
    void setupTray();
    void ShowTrayMenu();
    void RebuildMenu();

    // Window appearance / misc bridge handlers.
    std::execution::task<boost::json::value> wallpaperFetch(bool fresh, int idx);
    // Background gallery: list image names in the bg dir, and load one image
    // as a base64 data URL.
    std::execution::task<boost::json::value> bgList();
    std::execution::task<boost::json::value> bgLoad(std::string name);
    std::execution::task<boost::json::value> bgLoadThumb(std::string name);
    // Reveal a specific file in Explorer, selecting it: type "plugin" (name =
    // plugin display name) or "bg" (name = image file name).
    std::execution::task<bool> shellReveal(std::string type, std::string name);
    // Open an app directory in Explorer, navigating INTO it (logs).
    std::execution::task<bool> shellOpenDir(std::string type);

    PluginManager m_pluginManager;
    AppSettings m_settings;
    ProcessMonitor m_processMonitor; // declared last → destroyed (joined) first
    Wallpaper m_wallpaper;
    BgImages m_bgImages;

    // System tray icon + popup menu (destroyed before the window/base, since
    // the C layer unbinds them when their window goes away).
    std::unique_ptr<helios::Tray> m_tray;
    std::unique_ptr<helios::Menu> m_menu;
    bool m_silent = false;  // --silent: start minimized to tray

    // True once the WebView is up (loadFrontend): broadcasts before that would
    // be dropped by the C layer.
    bool m_frontendReady = false;
    // True once the window has been shown (navigationCompleted) — guard so a
    // later navigation cannot re-show it.
    bool m_frontendShown = false;
    size_t m_logSinkId = 0;   // logger listener id, removed in destructor
};
