// BgImages — list and load background images from the app's bg directory.
//
// The WebView (dev localhost / prod https://app.local) cannot read the local
// filesystem directly, so the native side enumerates the folder and serves each
// image to the frontend as a base64 data: URL (no CORS, no file:// path issues).
//
// Directory: AppRoot()/bg — the app's data directory (AppRoot() is the parent of
// the exe), so a shipped app carries a "bg/" folder full of full-bleed images.
// Change BgDir() below if you want it elsewhere (e.g. exe-dir/bg).
#pragma once

#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

class BgImages
{
public:
    // The background-image folder (exe-adjacent "bg" first, then AppRoot()/bg).
    static std::filesystem::path bgDir();

    // Names of image files (*.jpg/*.jpeg/*.png/*.webp/*.gif) in the bg dir,
    // sorted lexically. Empty if the dir is missing.
    std::vector<std::string> list() const;

    // Full base64 data: URL for one image name ("" if it cannot be read).
    // The caller decides whether to embed it in the picker grid or apply it.
    std::string load(const std::string& name) const;

    // Downscaled base64 data: URL (JPEG) of one image, bounded to `maxWidth`
    // pixels wide — a few KB instead of the multi-MB full image. Used for the
    // picker grid so it doesn't stall on big 4K backgrounds. "" on failure.
    std::string loadThumb(const std::string& name, int maxWidth) const;
};
