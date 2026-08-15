// HeliosView application template — main entry.
//
// Wire-up is intentionally thin: create the AppContext (the UI loop, wrapped
// by the HeliosView library), create the MainWindow (a WebViewWindow subclass
// that owns the native <-> JS bridge and frontend loading), then run the UI
// loop. The window is destroyed before the context, so no binding or pending
// task can outlive the loop.
//
// Build modes (selected by CMake):
//   - Dev  (HELIOSVIEW_TEMPLATE_DEV=ON):  the WebView loads the frontend dev
//     server (Vite, default http://localhost:5173). HMR works.
//   - Prod (default):                     the WebView loads the built frontend
//     from exe-dir/assets/index.html (copied there by the build).

#include "AppContext.h"
#include "MainWindow.h"

#include <print>

int main()
{
    // 1) The context: the UI loop (v1.0.0 has no library thread pool).
    AppContext ctx;

    // 2) The main window (bridge bindings are set up in its constructor).
    //    Title is UTF-8 since HeliosView v1.0.0.
    MainWindow window(ctx, 960, 640, "HeliosView App");
    window.show();
    window.createWebView();   // async; navigate() calls queue until it's ready
    window.loadFrontend();    // dev server URL (dev) or built static files (prod)

    // 3) Run the UI loop; exits when the last window closes.
    std::println("[HeliosViewApp] entering UI loop (close the window to exit)");
    return ctx.app().exec();
}
