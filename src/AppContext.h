#pragma once

// AppContext — the application-wide services shared by all windows:
//
//   app()   the UI loop: message pump + event queue + idle tasks
//           (also a std::execution scheduler for UI-thread delivery)
//
// The HeliosView library wraps the loop (helios::App); this class just owns it
// for the app. main() creates the context and every window/native service
// receives an AppContext& — pass it down instead of grabbing globals.
// AppContext::instance() is the escape hatch for code that has no context
// reference.
//
// Threading (HeliosView v1.0.0): every window/WebView API must run on the
// message-loop thread (the thread running app().exec()). The exceptions —
// safe from any thread — are app().postTask(...) (deliver work to the UI
// thread), App::quit, and the WebView resolve/reject/broadcast calls. The
// library no longer ships a background thread pool (helios::Async was removed
// in v1.0.0): run background work on your own workers and hand results back
// to the UI thread with app().postTask(...) (see the ping handler in
// MainWindow.cpp).

#include <HeliosViewCore/HeliosView.h>

class AppContext {
public:
    AppContext() { s_instance = this; }
    ~AppContext() { if (s_instance == this) s_instance = nullptr; }

    // A process owns a single context (like helios::App)
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    // The current context, or nullptr before any AppContext is constructed
    static AppContext* instance() { return s_instance; }

    // The UI loop: message pump, event queue, idle tasks. Run it with
    // app().exec(); deliver UI-thread work with app().postTask(...).
    helios::App& app() noexcept { return m_app; }

private:
    helios::App m_app;          // UI loop (must outlive every window)

    static inline AppContext* s_instance = nullptr;
};
