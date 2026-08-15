// MainWindow implementation: native <-> JS bridge bindings and frontend loading.
//
// Build modes (selected by CMake):
//   - Dev  (HELIOSVIEW_TEMPLATE_DEV=ON):  the WebView loads the frontend dev
//     server (Vite, default http://localhost:5173). HMR works.
//   - Prod (default):                     the WebView loads the built frontend
//     from exe-dir/assets/index.html (copied there by the build).

#include "MainWindow.h"

#include "AppContext.h"

#include <cctype>
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
// Threading (v1.0.0): bind handlers run on the UI thread; resolve/reject/
// broadcast are thread-safe. The library removed its thread pool
// (helios::Async), so background work runs on your own workers; hand results
// back to the UI thread with App::postTask (see the ping handler).
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

    // Worker round trip: run the background work off the UI thread on a plain
    // std::thread (v1.0.0 has no library thread pool — use your own bounded
    // pool in a real app), then push the result to the page's
    // BroadcastChannel('ping'). broadcast() is thread-safe, so the worker can
    // post directly; co_return resolves the Promise on the UI thread.
    bindJson<nlohmann::json>("ping", [this](nlohmann::json req)
                                 -> std::execution::task<helios::JsonResp<nlohmann::json>> {
        const std::string msg = req.value("msg", "ping");

        std::thread([this, msg] {
            // ... background work goes here ...
            broadcast("ping", nlohmann::json{ { "msg", msg } }.dump().c_str());
        }).detach();

        co_return helios::JsonResp<nlohmann::json>{ "pong", {
            { "msg",    msg },
            { "thread", "worker" },
        }};
    });

    bindJson<double,double>("add",this,&MainWindow::add);
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
}

std::execution::task<double> MainWindow::add(double num1,double num2)
{
    co_return num1 + num2;
}
