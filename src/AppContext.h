#pragma once

// AppContext — the application-wide services shared by all windows:
//
//   app()        the UI loop: message pump + event queue + idle tasks
//                (also a std::execution scheduler for UI-thread delivery)
//   threadPool() the background thread pool: worker tasks + async file/TCP I/O
//                (IOCP-based; also a std::execution scheduler for workers)
//
// The HeliosView library already wraps both (helios::App, helios::Async);
// this class just owns them for the app. main() creates the context and every
// window/native service receives an AppContext& — pass it down instead of
// grabbing globals. AppContext::instance() is the escape hatch for code that
// has no context reference.

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

    // The background thread pool: worker tasks + async file/TCP I/O.
    // Schedule with threadPool().post(...) or the std::execution scheduler.
    helios::Async& threadPool() noexcept { return m_threadPool; }

private:
    helios::App m_app;          // UI loop (must outlive every window)
    helios::Async m_threadPool; // background I/O thread pool

    static inline AppContext* s_instance = nullptr;
};
