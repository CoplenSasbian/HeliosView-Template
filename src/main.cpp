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
//     from exe-dir/assets/index.html (copied there by the build).

#include "AppContext.h"
#include "MainWindow.h"

#include <print>

// Platform-independent app entry, called by entry.cpp (WinMain on Windows,
// main elsewhere). argc/argv are the parsed command line (ANSI on Windows).
int AppMain(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // 1) The context: the UI loop + the app-scoped background pool
    //    (AppContext::async(), see AppContext.h).
    AppContext ctx;

    // 2) The main window (bridge bindings are set up in its constructor).
    //    Title comes from CMake (HELIOSVIEW_TEMPLATE_APP_TITLE, UTF-8 since
    //    HeliosView v1.0.0).
    MainWindow window(ctx, 960, 640, HELIOSVIEW_TEMPLATE_APP_TITLE);

    // 3) Window::ready fires once, on the first show() — the native window is
    //    created in the constructor (HeliosView 49e8884) and is now visible.
    //    Connect before show(); it is the right moment to wire up the WebView
    //    and load the frontend (both need the live native window).
    window.ready.connect([&window] {
        window.createWebView();   // async; navigate() calls queue until it's ready
        window.loadFrontend();    // dev server URL (dev) or built static files (prod)
    });

    // 4) Show the window; the first show() fires Window::ready.
    window.show();

    // 5) Run the UI loop; exits when the last window closes.
    std::println("[HeliosViewApp] entering UI loop (close the window to exit)");
    return ctx.app().exec();
}
