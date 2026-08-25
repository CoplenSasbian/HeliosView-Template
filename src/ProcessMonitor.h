#pragma once

#include <HeliosViewCore/Signal.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace helios { class App; }

// Watches for watched processes starting/stopping and reports matches, so the
// app can auto-activate a config when a game launches. When the last watched
// process exits it reports allExited, letting the app fall back to the
// built-in "close" config. Processes already running when monitoring starts
// are snapshotted and counted, and the tracked pids are reconciled
// periodically, so an exit is detected even if the corresponding WMI stop
// event is missed. The Windows implementation (WMI process start/stop traces)
// lives in ProcessMonitor_win32.cpp; the header stays platform-neutral via a
// pimpl.
//
// Decoupling via signals (helios::Signal): interested parties connect their
// own slots, e.g.
//   mon.processMatched.connect([...](const std::string& config, unsigned long pid) {...});
//   mon.allExited.connect([...] {...});
//
// The worker thread never touches the signals directly: matches are POSTED to
// the UI thread (helios::App::postTask, thread-safe) and emitted there, so
// slots always run on the message-loop thread like every other helios::Signal.
// The app must outlive the monitor.
class ProcessMonitor
{
public:
    // exe path → config name
    using ProcessMap = std::vector<std::pair<std::string, std::string>>;

    // A watched process started: config name + pid.
    helios::Signal<std::string, unsigned long> processMatched;
    // The last watched process exited — no watched process is running anymore.
    helios::Signal<> allExited;

    // app: used to post signal emissions to the UI thread.
    explicit ProcessMonitor(helios::App& app);
    ~ProcessMonitor();
    ProcessMonitor(const ProcessMonitor&) = delete;
    ProcessMonitor& operator=(const ProcessMonitor&) = delete;

    // Start/stop the watcher thread. Start() is idempotent.
    bool Start();
    void Stop();
    bool IsRunning() const;

    // Replace the whole watch map. Thread-safe.
    void SetWatchMap(const ProcessMap& map);

private:
    struct Impl;
    std::unique_ptr<Impl> m_;
};
