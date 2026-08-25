#pragma once
#include <memory>

#include <HeliosViewCore/Signal.h>

// Detects whether another instance is already running, using a TCP loopback
// socket as the single-instance lock. The listening port is stored in a file
// under the system temp directory so a second instance can discover it.
//
// Server side:
//   start()  → bind to 127.0.0.1:0, write port to file (async), begin
//              async_accept loop on the helios asio pool.
//   stop()   → close acceptor, delete port file.
//
// Client side:
//   start()  → read port from file (sync probe), try connect.
//   notifyAnotherInstance() → connect + write (synchronous one-shot), then exit.
//
// Server-side consumers connect to notifyReceived:
//   guard.notifyReceived.connect([] { bringWindowToFront(); });
class SingleInstanceGuard
{
public:
    SingleInstanceGuard();
    ~SingleInstanceGuard();

    void start();
    void stop();

    bool isAnotherInstanceRunning();
    void notifyAnotherInstance();

    // Server side: emitted on the UI thread when another instance calls
    // notifyAnotherInstance().
    helios::Signal<> notifyReceived;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
