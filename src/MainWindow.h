#pragma once

// MainWindow — the app's main window class. Derives from helios::WebViewWindow
// (a helios::Window with an embedded WebView2), adds the native <-> JS bridge
// bindings, and knows how to load the frontend in both build modes.
//
// This is where native-side app logic starts: bind functions callable from JS
// (window.helios.call), subscribe to BroadcastChannel messages, and react to
// window events (signals like resized/keyPressed on the base class).

#include <HeliosViewCore/HeliosView.h>

class AppContext;

class MainWindow : public helios::WebViewWindow {
public:
    // ctx: the application context (UI loop + thread pool) that this window's
    //      native logic runs against. The context must outlive the window.
    MainWindow(AppContext& ctx, int width, int height, const char* title);

    // Load the frontend: the dev server URL in dev builds, the built static
    // files (exe-dir/assets/index.html) in prod builds. Call after
    // createWebView(); navigation is queued automatically while the WebView
    // is still initializing. Registers the bridge bindings first (bind
    // requires a live WebView: bindings set before createWebView() are
    // silently dropped by the C layer).
    void loadFrontend();

private:
    void setupBridge();  // native <-> JS bridge bindings (requires the WebView)

    AppContext& m_ctx;
};
