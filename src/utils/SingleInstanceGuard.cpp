
#include "SingleInstanceGuard.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/stream_file.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <print>

#include "../AppContext.h"

namespace net = boost::asio::ip;

// Port file: temp/<AppName>/instanceGuardPort
static constexpr wchar_t kPortFileName[] = L"instanceGuardPort";

struct SingleInstanceGuard::Impl
{
    SingleInstanceGuard* owner;
    helios::App* app = nullptr;
    helios::Async* async = nullptr;

    bool isAnotherInstanceRunning = false;

    // ---- Server ----
    std::unique_ptr<net::tcp::acceptor> acceptor;
    std::filesystem::path portFilePath;

    // Lifetime guard: the async_accept handler captures this to bail out
    // if the Impl has already been destroyed.
    std::shared_ptr<std::atomic<bool>> alive =
        std::make_shared<std::atomic<bool>>(true);

    explicit Impl(SingleInstanceGuard* self) : owner(self) {}

    ~Impl()
    {
        alive->store(false);
        stop();
    }

    template <class Fn>
    void PostToUi(Fn&& fn)
    {
        app->postTask([fn = std::forward<Fn>(fn)] { fn(); });
    }

    // ---- Async port-file write (helios asio pool) ----
    void WritePortFile(uint16_t port)
    {
        std::filesystem::create_directories(portFilePath.parent_path());

        auto executor = async->get_executor();
        auto file = std::make_shared<boost::asio::stream_file>(
            executor, portFilePath.string(),
            boost::asio::stream_file::read_write
                | boost::asio::stream_file::create
                | boost::asio::stream_file::truncate);

        auto data = std::make_shared<std::string>(std::to_string(port));

        boost::asio::async_write(
            *file, boost::asio::buffer(*data),
            [file, data](boost::system::error_code ec, size_t)
            {
                if (ec)
                    std::println("SingleInstanceGuard: write port file: {}",
                                 ec.message());
            });
    }

    void RemovePortFile()
    {
        std::error_code ec;
        std::filesystem::remove(portFilePath, ec);
    }

    // ---- Async accept loop (asio thread pool) ----
    void DoAccept()
    {
        if (!acceptor || !acceptor->is_open())
            return;

        auto peer =
            std::make_shared<net::tcp::socket>(acceptor->get_executor());
        auto aliveGuard = alive;

        acceptor->async_accept(
            *peer,
            [this, peer, aliveGuard](boost::system::error_code ec)
            {
                if (!aliveGuard->load())
                    return;   // Impl destroyed — bail out

                if (!ec)
                {
                    // Client connected — drain notification (best-effort).
                    char buf[8]{};
                    boost::system::error_code readEc;
                    boost::asio::read(*peer, boost::asio::buffer(buf), readEc);

                    peer->close();

                    // Emit signal on the UI thread.
                    PostToUi([owner = owner]
                             { owner->notifyReceived(); });

                    // Accept the next client.
                    DoAccept();
                }
                // On error (shutdown / cancel) — don't loop.
            });
    }

    void stop()
    {
        if (acceptor)
        {
            boost::system::error_code ec;
            acceptor->close(ec);
            acceptor.reset();
            RemovePortFile();
        }
    }
};

// ──────────────────────────────────────────────────────────────────
SingleInstanceGuard::SingleInstanceGuard()
    : m(std::make_unique<Impl>(this))
{
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    m->stop();
}

void SingleInstanceGuard::start()
{
    m->app   = &AppContext::instance()->app();
    m->async = &AppContext::instance()->async();

    m->portFilePath = std::filesystem::temp_directory_path()
                      / HELIOSVIEW_TEMPLATE_APP_NAME
                      / kPortFileName;

    // ── Probe: is another instance already listening? ──────────
    {
        std::ifstream ifs(m->portFilePath);
        if (ifs.is_open())
        {
            uint16_t port = 0;
            ifs >> port;
            ifs.close();

            if (port > 0)
            {
                // Try a quick connect to see if the port is alive.
                auto executor                = m->async->get_executor();
                net::tcp::socket probe(executor);
                boost::system::error_code ec;
                probe.connect(
                    net::tcp::endpoint(
                      boost::asio::ip::address_v4::loopback(), port),
                    ec);
                probe.close();

                if (!ec)
                {
                    m->isAnotherInstanceRunning = true;
                    return;
                }
            }
            // Stale port file (server crashed) — fall through.
        }
    }

    // ── No valid server → we are the server ───────────────────
    m->isAnotherInstanceRunning = false;

    auto executor = m->async->get_executor();
    m->acceptor   = std::make_unique<net::tcp::acceptor>(
        executor,
        net::tcp::endpoint(boost::asio::ip::address_v4::loopback(), 0));

    const uint16_t port = m->acceptor->local_endpoint().port();

    // Write port file (async) then start accepting.
    m->WritePortFile(port);
    m->DoAccept();
}

void SingleInstanceGuard::stop()
{
    m->stop();
}

bool SingleInstanceGuard::isAnotherInstanceRunning()
{
    return m->isAnotherInstanceRunning;
}

void SingleInstanceGuard::notifyAnotherInstance()
{
    if (!m->isAnotherInstanceRunning)
        return;

    // Read the port synchronously (one-shot client operation).
    std::ifstream ifs(m->portFilePath);
    if (!ifs.is_open())
        return;

    uint16_t port = 0;
    ifs >> port;
    ifs.close();

    if (port == 0)
        return;

    // Connect + write synchronously, then exit.
    auto executor = AppContext::instance()->async().get_executor();
    net::tcp::socket socket(executor);
    boost::system::error_code ec;
    socket.connect(
        net::tcp::endpoint(boost::asio::ip::address_v4::loopback(), port),
        ec);
    if (ec)
        return;

    boost::asio::write(socket, boost::asio::buffer("notify", 6), ec);
    socket.close();
}
