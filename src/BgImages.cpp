// BgImages.cpp — enumerate + read background images (see BgImages.h).

#include "BgImages.h"

#include "utils/AppFilePath.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // Windows.h defines min/max macros that break std::min/std::max
#endif
#include <Windows.h>
#include <gdiplus.h>
#endif

namespace
{
namespace fs = std::filesystem;

#ifdef _WIN32
// One-shot GDI+ startup (static init). Returns a status usable by the caller.
bool gdiplusStarted()
{
    static Gdiplus::GdiplusStartupInput input;
    static ULONG_PTR token = 0;
    static const bool started = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    return started;
}

#pragma comment(lib, "gdiplus")
#endif

// Exe-adjacent directory (the folder the running exe lives in), "", on failure.
fs::path exeDir()
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
    return fs::path(buf).parent_path();
#else
    return {};
#endif
}

// The background-image folder. We look in BOTH common homes so it works in dev
// (exe in build\release\bin → AppRoot() = build\release) and prod (exe in
// dist\bin → AppRoot() = dist), and also accept an exe-adjacent "bg" folder:
//   1. <exe-dir>/bg        (e.g. dist\bin\bg — "next to the exe")
//   2. <AppRoot()>/bg      (e.g. dist\bg, build\release\bg — "software dir/bg")
// The first that exists wins.
fs::path BgDir()
{
    if (!exeDir().empty())
    {
        const fs::path e = exeDir() / "bg";
        std::error_code ec;
        if (fs::exists(e, ec))
            return e;
    }
    return AppRoot() / "bg";
}

// Case-insensitive image extension check.
bool isImage(const fs::path& p)
{
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" ||
           ext == ".webp" || ext == ".bmp" || ext == ".gif";
}

std::string mimeFor(const fs::path& p)
{
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".png") return "image/png";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif") return "image/gif";
    if (ext == ".bmp") return "image/bmp";
    return "image/jpeg"; // .jpg/.jpeg and anything else assume JPEG
}

const std::string kBase64Tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string_view bin)
{
    std::string out;
    out.reserve(((bin.size() + 2) / 3) * 4);
    size_t i = 0;
    const auto add = [&](unsigned v) { out += kBase64Tbl[(v >> 18) & 0x3F]; };
    while (i + 2 < bin.size())
    {
        const unsigned v =
            (static_cast<unsigned char>(bin[i]) << 16) |
            (static_cast<unsigned char>(bin[i + 1]) << 8) |
            static_cast<unsigned char>(bin[i + 2]);
        out += kBase64Tbl[(v >> 18) & 0x3F];
        out += kBase64Tbl[(v >> 12) & 0x3F];
        out += kBase64Tbl[(v >> 6) & 0x3F];
        out += kBase64Tbl[v & 0x3F];
        i += 3;
    }
    const size_t rem = bin.size() - i;
    if (rem == 1)
    {
        const unsigned v = static_cast<unsigned char>(bin[i]) << 16;
        out += kBase64Tbl[(v >> 18) & 0x3F];
        out += kBase64Tbl[(v >> 12) & 0x3F];
        out += "==";
    }
    else if (rem == 2)
    {
        const unsigned v =
            (static_cast<unsigned char>(bin[i]) << 16) |
            (static_cast<unsigned char>(bin[i + 1]) << 8);
        out += kBase64Tbl[(v >> 18) & 0x3F];
        out += kBase64Tbl[(v >> 12) & 0x3F];
        out += kBase64Tbl[(v >> 6) & 0x3F];
        out += "=";
    }
    return out;
}
} // namespace

std::vector<std::string> BgImages::list() const
{
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::exists(BgDir(), ec))
        return names;
    for (const auto& entry : fs::directory_iterator(BgDir(), ec))
    {
        if (entry.is_regular_file(ec) && isImage(entry.path()))
            names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string BgImages::load(const std::string& name) const
{
    if (name.empty() || name.find_first_of("\\/") != std::string::npos)
        return {}; // reject path traversal
    const fs::path file = BgDir() / name;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || !isImage(file))
        return {};

    std::ifstream in(file, std::ios::binary);
    if (!in)
        return {};
    in.seekg(0, std::ios::end);
    const std::streamoff sz = in.tellg();
    in.seekg(0, std::ios::beg);
    if (sz <= 0)
        return {};
    std::string data(static_cast<size_t>(sz), '\0');
    if (!in.read(data.data(), sz))
        return {};

    return "data:" + mimeFor(file) + ";base64," + base64Encode(data);
}

// ---- thumbnail (native downscale via GDI+, not the multi-MB full image) ----

// Resolve + validate a requested image path (rejects traversal / non-images).
static bool resolveImage(const std::string& name, fs::path& out)
{
    if (name.empty() || name.find_first_of("\\/") != std::string::npos)
        return false;
    const fs::path file = BgDir() / name;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || !isImage(file))
        return false;
    out = file;
    return true;
}

// Encode a GDI+ Bitmap as a base64 JPEG data: URL string.
static std::string encodeJpegDataUrl(Gdiplus::Bitmap* bmp, int quality)
{
#ifdef _WIN32
    CLSID encoder;
    UINT n = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&n, &sz);
    if (sz == 0)
        return {};
    std::vector<Gdiplus::ImageCodecInfo> codecs(sz / sizeof(Gdiplus::ImageCodecInfo));
    Gdiplus::GetImageEncoders(n, sz, codecs.data());
    int idx = -1;
    for (UINT i = 0; i < n; i++)
    {
        const std::wstring mime = codecs[i].MimeType;
        if (mime == L"image/jpeg") { idx = (int)i; break; }
    }
    if (idx < 0)
        return {};

    // JPEG quality encoder parameter.
    Gdiplus::EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
        return {};
    bmp->Save(stream, &codecs[idx].Clsid, &params);

    // Copy the stream into a std::string.
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    ULARGE_INTEGER size = st.cbSize;
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
    stream->Release();
    if (read == 0)
        return {};
    bytes.resize(read);
    return "data:image/jpeg;base64," + base64Encode(bytes);
#else
    (void)bmp; (void)quality;
    return {};
#endif
}

std::string BgImages::loadThumb(const std::string& name, int maxWidth) const
{
#ifdef _WIN32
    fs::path file;
    if (!resolveImage(name, file) || !gdiplusStarted())
        return {};
    std::wstring wpath = file.wstring();

    Gdiplus::Bitmap src(wpath.c_str(), FALSE);
    if (src.GetLastStatus() != Gdiplus::Ok)
        return {};

    // Bound the width (and its matching height), skipping upscale for tiny images.
    const int sw = src.GetWidth(), sh = src.GetHeight();
    if (sw <= 0 || sh <= 0)
        return {};
    int w = std::min(sw, maxWidth > 0 ? maxWidth : 320);
    int h = std::max(1, (int)((long long)sh * w / sw));

    Gdiplus::Bitmap dst(w, h, src.GetPixelFormat());
    Gdiplus::Graphics g(&dst);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.DrawImage(&src, Gdiplus::Rect(0, 0, w, h), 0, 0, sw, sh, Gdiplus::UnitPixel);
    return encodeJpegDataUrl(&dst, 78);
#else
    (void)name; (void)maxWidth;
    return {};
#endif
}

// Public accessor for the bg folder (the internal BgDir() lives in the
// anonymous namespace above — reused so the resolution stays identical).
std::filesystem::path BgImages::bgDir()
{
    return BgDir();
}
