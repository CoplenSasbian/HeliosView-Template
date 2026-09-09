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
//     from appRoot/assets/index.html (copied there by the build; appRoot is
//     one level above the exe — e.g. dist\assets for dist\bin\GameTrigger.exe).

#include "AppContext.h"
#include "MainWindow.h"
#include "utils/SingleInstanceGuard.h"

#include <HeliosViewCore/Notification.h>
#include <print>
#include <string_view>
// Platform-independent app entry, called by entry.cpp (WinMain on Windows,
// main elsewhere). argc/argv are the parsed command line (ANSI on Windows).
int AppMain(int argc, char* argv[])
{


    // Parse --silent: start minimized to tray, no window shown.
    bool silent = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--silent")
            silent = true;
    }

    // 1) The context: the UI loop + the app-scoped background pool
    //    (AppContext::async(), see AppContext.h).
    AppContext ctx;

    ctx.guard().start();

    if (ctx.guard().isAnotherInstanceRunning())
    {
        ctx.guard().notifyAnotherInstance();
        return 2;
    }

    // Process identity, before any window exists: Windows AppUserModelID,
    // macOS bundle identifier, Linux application id. It is also the id the
    // notification backend registers below (App::setAppId is its default), and
    // on Windows it names the Start Menu shortcut that makes toasts work.
    // Configured in app-config.cmake.
    helios::App::setAppId(HELIOSVIEW_TEMPLATE_APP_ID);

    // OS toast backend: registers the AppUserModelID + Start Menu shortcut an
    // unpackaged app needs before any notificationShow() can work. Must be
    // called once at startup; the toast API is what lets us set a real custom
    // notification title (the classic tray balloon only ever shows the exe name).
    // No argument: the id comes from App::setAppId above.
    helios::notificationInit();



    // 2) The main window. The native window (hwnd) exists from Window's
    //    constructor; the MainWindow constructor kicks off async init (icon,
    //    tray, WebView + frontend load, settings, plugins). The window shows
    //    itself from navigationCompleted once the page has actually rendered —
    //    no off-screen parking, no firstShown dance.
    MainWindow window(960, 640, HELIOSVIEW_TEMPLATE_APP_TITLE, silent);

    // 3) Run the UI loop; exits when the last window closes.
    std::println("[GameTrigger] entering UI loop (close the window to exit)");
    const int rc = ctx.app().exec();

    // Signal async slots (InitAsync's setupSettings/setupPlugins, etc.) are tracked
    // by the app-wide scope and run on the pool while touching window members.
    // Stop & join them BEFORE the window is destroyed (the App — and its
    // ~App request_stop — lives until after this function returns, too late).
    ctx.app().scope().request_stop();
    std::execution::sync_wait(ctx.app().scope().on_empty());

    return rc;
}
