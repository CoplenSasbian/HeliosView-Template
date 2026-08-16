#pragma once

// AppContext — the application-wide services shared by all windows:
//
//   app()    the UI loop: message pump + event queue + idle tasks
//            (also a std::execution scheduler for UI-thread delivery)
//   async()  the background pool: an asio-backed thread pool (helios::Async)
//            for compute, timers and socket I/O off the UI thread
//
// The HeliosView library wraps the loop (helios::App) and the pool
// (helios::Async); this class just owns them for the app. main() creates the
// context and every window/native service receives an AppContext& — pass it
// down instead of grabbing globals. AppContext::instance() is the escape
// hatch for code that has no context reference.
//
// Threading (HeliosView v1.0.1): every window/WebView API must run on the
// message-loop thread (the thread running app().exec()). The exceptions —
// safe from any thread — are app().postTask(...) (deliver work to the UI
// thread), App::quit, and the WebView resolve/reject/broadcast calls.
// Background work runs on the asio pool (async()): hop off the UI thread with
// `co_await std::execution::schedule(async().get_scheduler())`, sleep with
// async().timer(...), and hand results back to the UI thread with
// app().postTask(...) — or resolve/reject/broadcast straight from the pool
// (see the ping handler and the tick timer demo in MainWindow.cpp).

#include <HeliosViewCore/HeliosView.h>
#include <HeliosViewCore/Async.h>

class AppContext {
public:
    AppContext() { s_instance = this; }
    ~AppContext() { if (s_instance == this) s_instance = nullptr; }

    // A process owns a single context (like helios::App / helios::Async)
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    // The current context, or nullptr before any AppContext is constructed
    static AppContext* instance() { return s_instance; }

    // The UI loop: message pump, event queue, idle tasks. Run it with
    // app().exec(); deliver UI-thread work with app().postTask(...).
    helios::App& app() noexcept { return m_app; }

    // The background pool: asio-backed thread pool (HeliosView v1.0.1+).
    // Compute, timers and socket I/O run here; resolve/reject/broadcast are
    // safe from pool threads. The pool must outlive every pending sender it
    // created (i.e. outlive the windows and bindings that use it).
    helios::Async& async() noexcept { return m_async; }

private:
    helios::App m_app;          // UI loop (must outlive every window)
    helios::Async m_async;      // background pool (must outlive every binding)

    static inline AppContext* s_instance = nullptr;
};
