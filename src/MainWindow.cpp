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

#include <boost/json.hpp>

namespace {

// boost::json::value has no j.value("key", default) accessor (nlohmann::json
// did); jget provides the equivalent: value_to<T>(j["key"]) or `fallback`
// when the key is missing.
template <class T>
T jget(const boost::json::value& j, std::string_view key, T fallback)
{
    if (const auto* obj = j.if_object())
        if (const auto* it = obj->if_contains(key))
            return boost::json::value_to<T>(*it);
    return fallback;
}

} // namespace

MainWindow::MainWindow(AppContext& ctx, int width, int height, const char* title)
    : WebViewWindow(width, height, title)
    , m_ctx(ctx)
{
    // Note: the bridge bindings are NOT registered here - bind requires a
    // live WebView, which only exists after createWebView(). loadFrontend()
    // calls setupBridge() first, then navigates.

    // Close button (×) / Alt+F4 does NOT auto-close anymore — it only emits
    // closeRequested. Connect here and call close() to actually close.
    closeRequested.connect([this] {
        std::println("[MainWindow] close requested -> closing");
        close();
    });
}

// ---- native <-> JS bridge --------------------------------------------------
//
// Callable from the frontend:
//   const info = await window.helios.call('appInfo', {});   → {"app": {...}}
//   const pong = await window.helios.call('ping', {...});   → {"pong": {...}}
//
// Handlers are detached std::execution::task coroutines; the JS Promise is
// resolved when the task completes. Arguments and return values are
// Boost.JSON: bindJson deserializes each JS call argument with
// boost::json::value_to and serializes the handler's task<Resp> completion
// value with boost::json::value_from.
//
// Threading: bind handlers start on the UI thread. Off-UI-thread work runs on
// the app's background pool (AppContext::async(), an asio-backed helios::Async
// thread pool) via `co_await schedule(async().get_scheduler())`; resolve /
// reject / broadcast are thread-safe, so the task may complete on a pool
// thread without marshalling back (see the ping handler).
void MainWindow::setupBridge()
{
    // Plain binding: runs on the UI thread, returns app info.
    bindJson<boost::json::value>("appInfo", [](boost::json::value)
                                     -> std::execution::task<boost::json::value> {
        co_return boost::json::value{{ "app", {
            { "name",    HELIOSVIEW_TEMPLATE_APP_NAME },
            { "version", HELIOSVIEW_TEMPLATE_VERSION },
            { "helios",  helios::version() },
        }}};
    });

    // Worker round trip: hop off the UI thread onto the app's background pool,
    // push the result to the page's BroadcastChannel('ping'), and resolve the
    // Promise. broadcast()/resolve are thread-safe, so completing on a pool
    // thread needs no marshalling back.
    bindJson<boost::json::value>("ping", [this](boost::json::value req)
                                     -> std::execution::task<boost::json::value> {
        const std::string msg = jget(req, "msg", std::string("ping"));

        co_await std::execution::schedule(m_ctx.async().get_scheduler());

        // ... background work goes here ...
        broadcast("ping", boost::json::serialize(boost::json::value{{ "msg", msg }}).c_str());

        co_return boost::json::value{{ "pong", {
            { "msg",    msg },
            { "thread", "worker" },
        }}};
    });

    bindJson<double, double>("add", this, &MainWindow::add);
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

// The directory holding the built frontend (exe-dir/assets). GetModuleFileNameW
// is argv-independent: the exe may be launched from anywhere.
static std::string assetsDir()
{
    std::wstring buf(512, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return {};
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    const auto p = std::filesystem::path(buf).parent_path() / "assets";
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
#endif

void MainWindow::loadFrontend()
{
    setupBridge();  // must run after createWebView(): binds are dropped otherwise

#ifdef HELIOSVIEW_TEMPLATE_DEV
    const std::string url = startUrl();
    std::println("[HeliosViewApp] dev mode:  loading {}", url);
#else
    // Prod builds are packaged for end users: keep WebView2 DevTools
    // (F12, right-click Inspect) closed. The setting is stored on the
    // webview and applied when it becomes ready.
    setDevToolsEnabled(false);

    // The Vite output is ES modules; WebView2 blocks module scripts from
    // file:// with a CORS error (file is not a supported scheme), so a
    // file:// URL would show a blank page. Instead map the built frontend
    // to the virtual host "app.local" (WebView2 restricts mappings to the
    // .local suffix) and load it over https://, a supported scheme. The
    // mapping is queued by the library until the WebView is initialized.
    mapLocalFolder("app.local", assetsDir().c_str());
    const std::string url = "https://app.local/index.html";
    std::println("[HeliosViewApp] prod mode: loading {}", url);
#endif
    navigate(url.c_str());
}

std::execution::task<double> MainWindow::add(double num1,double num2)
{
    co_return num1 + num2;
}
