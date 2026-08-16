// MainWindow implementation: native <-> JS bridge bindings and frontend loading.
//
// Build modes (selected by CMake):
//   - Dev  (HELIOSVIEW_TEMPLATE_DEV=ON):  the WebView loads the frontend dev
//     server (Vite, default http://localhost:5173). HMR works.
//   - Prod (default):                     the WebView loads the built frontend
//     from exe-dir/assets/index.html (copied there by the build).

#include "MainWindow.h"

#include "AppContext.h"

#include <chrono>
#include <cctype>
#include <functional>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>




MainWindow::MainWindow(AppContext& ctx, int width, int height, const char* title)
    : WebViewWindow(width, height, title)
    , m_ctx(ctx)
{
    // Note: the bridge bindings are NOT registered here - bind requires a
    // live WebView, which only exists after createWebView(). loadFrontend()
    // calls setupBridge() first, then navigates.
}

// ---- native <-> JS bridge --------------------------------------------------
//
// Callable from the frontend:
//   const info = await window.helios.call('appInfo', {});   → {"app": {...}}
//   const pong = await window.helios.call('ping', {...});   → {"pong": {...}}
//
// Handlers are detached std::execution::task coroutines; the JS Promise is
// resolved when the task completes (from any thread — resolve is thread-safe).
// Threading (v1.0.1): bind handlers start on the UI thread; hop off it onto
// the background pool with
// `co_await std::execution::schedule(async().get_scheduler())` (see the ping
// handler below). resolve/reject/broadcast are thread-safe, so a task may
// finish on a pool thread without marshalling back; use App::postTask only to
// touch UI state.
void MainWindow::setupBridge()
{
    // Plain binding: runs on the UI thread, returns app info.
    bindJson<nlohmann::json>("appInfo", [](nlohmann::json)
                                 -> std::execution::task<helios::JsonResp<nlohmann::json>> {
        co_return helios::JsonResp<nlohmann::json>{ "app", {
            { "name",    "HeliosViewApp" },
            { "version", HELIOSVIEW_TEMPLATE_VERSION },
            { "helios",  helios::version() },
        }};
    });

    // Async round trip (HeliosView v1.0.1): hop off the UI thread onto the
    // background pool (helios::Async, asio-backed), sleep briefly, then push
    // the result to the page's BroadcastChannel('ping'). broadcast() is
    // thread-safe, so the pool thread posts directly; co_return resolves the
    // Promise (also thread-safe) on the pool thread.
    bindJson<nlohmann::json>("ping", [this](nlohmann::json req)
                                 -> std::execution::task<helios::JsonResp<nlohmann::json>> {
        const std::string msg = req.value("msg", "ping");

        co_await std::execution::schedule(m_ctx.async().get_scheduler());  // pool thread
        // ... background work goes here ...
        co_await m_ctx.async().timer(std::chrono::milliseconds(100));      // sleep off the UI thread
        broadcast("ping", nlohmann::json{ { "msg", msg } }.dump().c_str());

        co_return helios::JsonResp<nlohmann::json>{ "pong", {
            { "msg",    msg },
            { "thread", "pool" },
        }};
    });

    bindJson<double,double>("add",this,&MainWindow::add);
}

// ---- async demo: a repeating timer from the background pool ----------------
//
// Small demo of helios::Async (asio-backed, v1.0.1): every second the pool
// fires a 'tick' broadcast to the page (BroadcastChannel('tick')) and prints
// to the console. broadcast() is thread-safe, so the pool thread posts
// directly — no marshalling back to the UI thread needed. The callback chain
// re-arms itself via the shared_ptr-held std::function. Delete this (and the
// pool usage in the ping handler) when you start the real app.
void MainWindow::startAsyncDemo()
{
    auto tick = std::make_shared<std::function<void()>>();
    *tick = [this, tick] {
        m_ctx.async().sleep(std::chrono::seconds(1), [this, tick](helios::asio::error_code ec) {
            if (ec)
                return;
            std::println("[HeliosViewApp] async tick from pool thread {}", std::this_thread::get_id());
            broadcast("tick", nlohmann::json{ { "at", "pool" } }.dump().c_str());
            (*tick)();  // re-arm
        });
    };
    (*tick)();
}

// ---- frontend loading ------------------------------------------------------

#ifdef HELIOSVIEW_TEMPLATE_DEV
// ---------------- dev mode: the frontend dev server -------------------------
static const char* startUrl()
{
    return HELIOSVIEW_TEMPLATE_DEV_URL;   // e.g. "http://localhost:5173"
}
#else
// ---------------- prod mode: built assets next to the exe -------------------
#include <windows.h>

#include <filesystem>

// Percent-encode the characters that would break a URL (path is UTF-8;
// backslashes become URL separators).
static std::string urlEncodePath(std::string_view s)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        if (c == '\\') {
            out += '/';
        } else if (std::isalnum(c) || c == '/' || c == ':' || c == '.' || c == '-'
                   || c == '_' || c == '~' || c == '+') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

static std::string startUrl()
{
    // Locate assets/ next to the executable (GetModuleFileNameW is
    // argv-independent: the exe may be launched from anywhere).
    std::wstring buf(512, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return "about:blank";
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }

    const auto p = std::filesystem::path(buf).parent_path() / "assets" / "index.html";
    const auto u8 = p.u8string();
    return "file:///" + urlEncodePath(
        std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
}
#endif

void MainWindow::loadFrontend()
{
    setupBridge();  // must run after createWebView(): binds are dropped otherwise

    const std::string url = startUrl();
#ifdef HELIOSVIEW_TEMPLATE_DEV
    std::println("[HeliosViewApp] dev mode:  loading {}", url);
#else
    std::println("[HeliosViewApp] prod mode: loading {}", url);
#endif
    navigate(url.c_str());

    startAsyncDemo();  // async timer demo (background pool → BroadcastChannel 'tick')
}

std::execution::task<double> MainWindow::add(double num1,double num2)
{
    co_return num1 + num2;
}
