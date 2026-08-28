#pragma once

// AppContext — the application-wide services shared by all components:
//
//   app()   the UI loop: message pump + event queue + idle tasks
//           (also a std::execution scheduler for UI-thread delivery)
//   async() the background thread pool (helios::Async, asio-backed) for
//           off-UI-thread work in bridge handlers / native services
//   logger() the app-scoped logger (file + console + optional listener)
//
// Singleton: created once in main(), accessible everywhere via
// AppContext::instance(). No need to pass context references — any
// component that needs app(), async() or logger() calls the static
// accessor directly.
//
// Threading: every window/WebView API must run on the message-loop thread
// (the thread running app().exec()). The exceptions — safe from any thread —
// are app().postTask(...) (deliver work to the UI thread) and App::quit;
// WebView calls (broadcast, resolve/reject) are UI-thread calls — the C layer
// no longer marshals off-thread calls. Long-running work goes on the
// background pool: `co_await schedule(async().get_scheduler())` hops a bridge
// handler off the UI thread, and `co_await schedule(app().get_scheduler())`
// brings it back before it touches the WebView or completes (see the
// pool-executed bridge handlers in MainWindow.cpp: pluginsActivate/bgLoad
// and co. all end with a UI-thread hop). The pool is owned here, app-scoped,
// so it outlives every window and binding.

#include <HeliosViewCore/HeliosView.h>
#include "Logger.h"
#include "utils/SingleInstanceGuard.h"
class AppContext {
public:
    AppContext() { s_instance = this; }
    ~AppContext() { if (s_instance == this) s_instance = nullptr; }

    // Non-copyable, non-movable — the singleton lives for the whole process.
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    // The global instance. Returns nullptr before the context is constructed
    // or after it is destroyed.
    static AppContext* instance() { return s_instance; }

    // The UI loop: message pump, event queue, idle tasks. Run it with
    // app().exec(); deliver UI-thread work with app().postTask(...).
    helios::App& app() noexcept { return m_app; }

    // The background thread pool (asio-backed helios::Async): off-UI-thread
    // work for bridge handlers / native services. App-scoped, so it outlives
    // every window and binding.
    helios::Async& async() noexcept { return m_async; }

    Logger& logger() noexcept { return m_logger; }

    SingleInstanceGuard& guard() noexcept { return m_guard; }
private:
    helios::App m_app;          // UI loop (must outlive every window)
    helios::Async m_async;      // background pool (app-scoped: outlives windows)
    Logger m_logger;
    SingleInstanceGuard m_guard;
    static inline AppContext* s_instance = nullptr;
};
