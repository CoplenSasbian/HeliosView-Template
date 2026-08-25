// HeliosView application template — app entry (platform-independent).
//
// Wire-up is intentionally thin: create the AppContext (the UI loop, wrapped
// by the HeliosView library), create the MainWindow (a WebViewWindow subclass
// that owns the native <-> JS bridge and frontend loading), then run the UI
// loop. The window is destroyed before the context, so no binding or pending
// task can outlive the loop.
//
// This file defines AppMain(), the platform-independent app entry. The actual
// process entry point lives in entry.cpp, which just forwards to AppMain():
//   - Windows: WinMain (the Win32 GUI entry — the exe is built with the WIN32
//     CMake flag, i.e. the GUI subsystem, so no console window appears)
//   - macOS / Linux: main
//
// Build modes (selected by CMake):
//   - Dev  (HELIOSVIEW_TEMPLATE_DEV=ON):  the WebView loads the frontend dev
//     server (Vite, default http://localhost:5173). HMR works.
//   - Prod (default):                     the WebView loads the built frontend
//     from software-root/assets/index.html (copied there by the build; assets
//     is a sibling of bin\, which holds the exe).
//
// Window visibility is deferred: the main window is NOT shown here —
// MainWindow shows itself from its navigationCompleted signal, once the
// initial page load has finished (so no blank window flashes while the
// frontend loads).

#include "AppContext.h"
#include "MainWindow.h"

#include <cstdlib>
#include <filesystem>
#include <print>
#include <string>

namespace {

// The app's per-user WebView2 data folder. WebView2's default is a
// "<exe>.WebView2" folder next to the executable; pointing it at a folder in
// the user's app data directory instead keeps the profile, cache and cookies
// per-user and out of the distribution directory (which may be read-only,
// e.g. Program Files). The folder is created by WebView2 if missing; an empty
// result keeps the library default (see createWebView in MainWindow.h).
std::string webviewUserDataFolder()
{
    std::filesystem::path base;
#if defined(_WIN32)
    // %LOCALAPPDATA% — e.g. C:\Users\<name>\AppData\Local. Set for
    // interactive sessions; otherwise fall back to the library default.
    if (const char* la = std::getenv("LOCALAPPDATA"))
        base = la;
#elif defined(__APPLE__)
    // ~/Library/Application Support
    if (const char* home = std::getenv("HOME"))
        base = std::filesystem::path(home) / "Library" / "Application Support";
#else
    // XDG data home, or ~/.local/share pre-XDG.
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        base = xdg;
    else if (const char* home = std::getenv("HOME"))
        base = std::filesystem::path(home) / ".local" / "share";
#endif
    if (base.empty())
        return {};  // no user data dir available: keep the WebView2 default
    return (base / HELIOSVIEW_TEMPLATE_APP_NAME).string();
}

} // namespace

// Platform-independent app entry, called by entry.cpp (WinMain on Windows,
// main elsewhere). argc/argv are the parsed command line (ANSI on Windows).
int AppMain(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // 1) The context: the UI loop + the app-scoped background pool
    //    (AppContext::async(), see AppContext.h).
    AppContext ctx;

    // 2) The main window. Title comes from CMake
    //    (HELIOSVIEW_TEMPLATE_APP_TITLE, UTF-8 since HeliosView v1.0.0). The
    //    native window is created in the constructor but stays hidden:
    //    MainWindow shows it from navigationCompleted (see its constructor),
    //    once the initial page load has finished.
    MainWindow window(ctx, 960, 640, HELIOSVIEW_TEMPLATE_APP_TITLE);

    // 3) Create the WebView and load the frontend right away. The WebView2
    //    user data folder lives under the user's app data directory (see
    //    webviewUserDataFolder) — passed at creation, since WebView2
    //    environment options are locked in then. Initialization is
    //    asynchronous: navigate() (from loadFrontend) queues until it is
    //    ready.
    window.createWebView(webviewUserDataFolder().c_str());
    window.loadFrontend();  // dev server URL (dev) or built static files (prod)

    // 4) Run the UI loop; exits when the last window closes.
    std::println("[HeliosViewApp] entering UI loop (window shows when the page has loaded)");
    return ctx.app().exec();
}
