// Wallpaper.cpp — Bing wallpaper fetcher (see Wallpaper.h).
//
// Bing API: https://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1
//   -> { images: [ { url: "/th?id=...&w=1920&h=1080&..." , ... } ] }
// The API responses have no CORS headers, hence native download (see header).

#include "Wallpaper.h"

#include "AppContext.h"
#include "utils/AppFilePath.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{
namespace fs = std::filesystem;
constexpr const char* kBingBase = "https://www.bing.com";

// Build the HPImageArchive URL for day `idx` (0 = today, -1 = yesterday, ...).
// idx must be <= 0 (Bing indexes the past as negatives; positive idx = "current"
// is not what we want for roaming). abs(idx) is used as `n` so one call returns
// that day as the first element.
std::string apiUrl(int idx)
{
    const int day = (idx <= 0) ? idx : 0;
    return "https://www.bing.com/HPImageArchive.aspx?format=js&idx="
           + std::to_string(day) + "&n=1";
}

// Per-day cache file so previously-viewed days work offline too. idx<0 uses the
// absolute value (e.g. idx=-3 -> bing_3.jpg); idx=0 -> bing_0.jpg.
fs::path cacheFile(int idx)
{
    const int n = (idx <= 0) ? -idx : 0; // -(-3)=3, -(0)=0
    return AppRoot() / "wallpaper" / ("bing_" + std::to_string(n) + ".jpg");
}

// Exe-adjacent cacert.pem (copied by CMake) so HTTPS verification has a bundle.
std::string caBundlePath()
{
#ifdef _WIN32
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
    const auto pem = fs::path(buf).parent_path() / "cacert.pem";
    if (!fs::exists(pem))
        return {};
    const auto u8 = pem.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    return {};
#endif
}

// base64-encode raw bytes (compact, no line wrapping) — enough for a data URI.
std::string base64Encode(const std::string_view bin)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bin.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < bin.size())
    {
        const unsigned v =
            (static_cast<unsigned char>(bin[i]) << 16) |
            (static_cast<unsigned char>(bin[i + 1]) << 8) |
            static_cast<unsigned char>(bin[i + 2]);
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >> 6) & 0x3F];
        out += tbl[v & 0x3F];
        i += 3;
    }
    const size_t rem = bin.size() - i;
    if (rem == 1)
    {
        const unsigned v = static_cast<unsigned char>(bin[i]) << 16;
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += "==";
    }
    else if (rem == 2)
    {
        const unsigned v =
            (static_cast<unsigned char>(bin[i]) << 16) |
            (static_cast<unsigned char>(bin[i + 1]) << 8);
        out += tbl[(v >> 18) & 0x3F];
        out += tbl[(v >> 12) & 0x3F];
        out += tbl[(v >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

// Read a whole file into a string (empty string if the file cannot be read).
std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return {};
    in.seekg(0, std::ios::end);
    const std::streamoff sz = in.tellg();
    in.seekg(0, std::ios::beg);
    if (sz <= 0)
        return {};
    std::string data(static_cast<size_t>(sz), '\0');
    in.read(data.data(), sz);
    return data;
}
} // namespace

std::execution::task<std::string> Wallpaper::fetch(bool fresh, int idx) const
{
    const fs::path cache = cacheFile(idx);

    // 1) Serve from a per-day cache when not forced fresh.
    if (!fresh && fs::exists(cache))
    {
        const std::string img = readFile(cache);
        if (!img.empty())
            co_return "data:image/jpeg;base64," + base64Encode(img);
    }

    // 2) Otherwise (re)download Bing for this day.
    try
    {
        const bool ok = co_await downloadToAsync(idx);
        if (ok)
        {
            const std::string img = readFile(cache);
            if (!img.empty())
                co_return "data:image/jpeg;base64," + base64Encode(img);
            // Tried to save but the write/read back failed — fall through.
        }
    }
    catch (const std::exception&)
    {
        // Network/TLS failure — fall through to the cache / gradient fallback.
    }
    // 3) Last resort: an old cached copy for THIS day (download failed but one exists).
    const std::string img = readFile(cache);
    if (!img.empty())
        co_return "data:image/jpeg;base64," + base64Encode(img);
    co_return {};
}

std::execution::task<bool> Wallpaper::downloadToAsync(int idx) const
{
    auto* ctx = AppContext::instance();
    if (!ctx)
        co_return false;

    // TLS-verify against the CA bundle shipped next to the exe (cacert.pem).
    helios::http::Client client{ctx->async(), std::chrono::seconds(15),
                                caBundlePath()};

    // a) Fetch the index JSON for this day.
    auto api = co_await client.get(apiUrl(idx));
    if (api.status != 200 || api.body.empty())
        co_return false;

    boost::json::value jv;
    try { jv = boost::json::parse(api.body); }
    catch (...) { co_return false; }

    const auto& images = jv.at("images").as_array();
    if (images.empty())
        co_return false;
    const auto& img0 = images[0].as_object();
    const auto it = img0.find("url");
    if (it == img0.end() || !it->value().is_string())
        co_return false;
    std::string imgUrl = it->value().as_string().c_str();
    // Ensure a 1920px image (Bing returns variant sizes).
    if (imgUrl.find('&') == std::string_view::npos)
        imgUrl += "&w=1920&h=1080&cvol=0";
    const std::string full = std::string(kBingBase) + imgUrl;

    // b) Download the image bytes.
    auto img = co_await client.get(full);
    if (img.status != 200 || img.body.empty())
        co_return false;

    // c) Cache to disk atomically (write temp then rename). Per-day file.
    const fs::path cache = cacheFile(idx);
    std::error_code ec;
    fs::create_directories(cache.parent_path(), ec);
    const fs::path tmp = cache.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(img.body.data(), static_cast<std::streamsize>(img.body.size()));
    }
    fs::rename(tmp, cache, ec);
    if (ec)
    {
        // Rename may fail if dest exists on some filesystems; try removing then moving.
        fs::remove(cache, ec);
        fs::rename(tmp, cache, ec);
    }
    co_return !ec;
}
