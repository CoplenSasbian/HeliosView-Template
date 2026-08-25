// Wallpaper — fetch the Bing daily wallpaper for use as the app background.
//
// Why native: the Bing HPImageArchive API has no CORS headers, so a WebView
// fetch from the frontend (dev origin localhost, prod origin https://app.local)
// would be blocked. This service downloads on the Async pool with HeliosView's
// https client (boost::beast/asio + OpenSSL against the bundled cacert.pem) and
// hands the frontend a ready-to-use data: URL. The image is cached on disk
// (AppRoot()/wallpaper/bing_latest.jpg) so an offline or slow network still gets
// a background instead of a blank page, and we don't re-download Bing every launch.
#pragma once

#include <HeliosViewCore/Http.h>

#include <boost/json.hpp>
#include <execution>
#include <string>
#include <string_view>

class Wallpaper
{
public:
    // fetch(fresh, idx):
    //   fresh=false: serve the cached image for `idx` if present (no network).
    //   fresh=true:  always re-fetch Bing for `idx` (used for "换一张").
    // idx is the day offset into Bing history: 0 = today, -1 = yesterday,
    // -2 = two days ago, ... (the Bing API accepts negative idx). Roaming by
    // offset is how "换一张" actually gets a DIFFERENT image — idx=0 is always
    // the same "today" wallpaper.
    // Returns a `data:image/jpeg;base64,...` URI, or "" on failure (the caller
    // falls back to the built-in gradient background).
    std::execution::task<std::string> fetch(bool fresh, int idx /* 0, -1, -2, ... */) const;

private:
    // Download the Bing index JSON and image bytes for day `idx` (throws on
    // network/TLS errors).
    std::execution::task<bool> downloadToAsync(int idx) const;
};
