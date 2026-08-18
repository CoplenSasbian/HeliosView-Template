#pragma once

// AppContext — the application-wide services shared by all windows:
//
//   app()   the UI loop: message pump + event queue + idle tasks
//           (also a std::execution scheduler for UI-thread delivery)
//   async() the background thread pool (helios::Async, asio-backed) for
//           off-UI-thread work in bridge handlers / native services
//
// The HeliosView library wraps the loop (helios::App) and ships the pool
// (helios::Async); this class just owns them for the app. main() creates the
// context and every window/native service receives an AppContext& — pass it
// down instead of grabbing globals. AppContext::instance() is the escape
// hatch for code that has no context reference.
//
// Threading: every window/WebView API must run on the message-loop thread
// (the thread running app().exec()). The exceptions — safe from any thread —
// are app().postTask(...) (deliver work to the UI thread), App::quit, and the
// WebView resolve/reject/broadcast calls. Long-running work goes on the
// background pool: `co_await schedule(async().get_scheduler())` hops a bridge
// handler off the UI thread (see the ping handler in MainWindow.cpp). The
// pool is owned here, app-scoped, so it outlives every window and binding.

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

    // The background thread pool (asio-backed helios::Async): off-UI-thread
    // work for bridge handlers / native services. App-scoped, so it outlives
    // every window and binding.
    helios::Async& async() noexcept { return m_async; }

private:
    helios::App m_app;          // UI loop (must outlive every window)
    helios::Async m_async;      // background pool (app-scoped: outlives windows)

    static inline AppContext* s_instance = nullptr;
};
