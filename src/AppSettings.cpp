#include "AppSettings.h"

#include "utils/AppFilePath.h"
#include "utils/AutoStart.h"

#include <HeliosViewCore/HeliosView.h> // helios::Async definition
#include <boost/asio/stream_file.hpp>
#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace
{
// Fill `dst` with every key of `defaults` it is missing, recursing into nested
// objects. boost::json's value_to for a described struct fails the WHOLE
// conversion on any missing non-optional member (error::size_mismatch), so a
// config written before a member was added (e.g. DesignPrefs::density) would
// otherwise silently invalidate the entire file and reset every setting on the
// next launch. The top-level-only loop can't cover members of nested objects,
// so this descends into them (design, ...) and applies the same defaults.
void FillMissingDefaults(boost::json::object& dst, const boost::json::object& defaults)
{
    for (const auto& [key, def] : defaults)
    {
        auto it = dst.find(key);
        if (it == dst.end())
        {
            dst[key] = def;
        }
        else if (it->value().is_object() && def.is_object())
        {
            FillMissingDefaults(it->value().as_object(), def.as_object());
        }
    }
}
} // namespace

std::execution::task<bool> AppSettings::load(helios::Async& async)
{
    const auto path = SettingDir() / "app.json";
    const auto executor = async.get_executor();

    // Synchronous open is cheap; the actual read runs on the pool.
    // Use the error_code open (NOT the throwing constructor): a missing
    // settings\app.json on first launch is normal and must fall through to
    // defaults. The non-error_code constructor would raise
    // boost::system::system_error here and kill the whole init chain —
    // setupPlugins never runs, so no default "close" config is created and the
    // frontend shows an empty plugins/config list until the next launch.
    boost::asio::stream_file file(executor);
    boost::system::error_code openEc;
    file.open(path.string(), boost::asio::file_base::read_only, openEc);
    if (openEc)
        co_return false;

    const auto size = file.size();
    std::string data;
    data.resize(size);

    const auto bytesRead = co_await async.readAsync(file, boost::asio::buffer(data));
    if (bytesRead != size) co_return false;

    boost::json::value value;
    try { value = boost::json::parse(data); }
    catch (...) { co_return false; }
    if (!value.is_object()) co_return false;

    // Merge defaults for keys added after the file was written (old configs),
    // then map the whole object into *this in one value_to. The fill descends
    // into nested objects (DesignPrefs etc.) so an old design block missing a
    // newer member can't invalidate the whole file (see FillMissingDefaults).
    auto& obj = value.as_object();
    const auto defaults = boost::json::value_from(AppSettings{});
    FillMissingDefaults(obj, defaults.as_object());

    // autoStart is NOT a stored setting anymore (the OS registration — the
    // Task Scheduler logon task — is the single source of truth), so there is
    // nothing to reconcile with the OS here. One legacy migration remains:
    // app.json written by older builds may still carry "autoStart":true; honor
    // it once by registering the task now. The key drops out of the file on
    // the next save() (value_from only emits described members).
    if (const auto* legacyStart = obj.if_contains("autoStart");
        legacyStart && legacyStart->is_bool() && legacyStart->as_bool()
        && !IsAutoStartEnabled())
        SetAutoStartEnabled(true);

    try { *this = boost::json::value_to<AppSettings>(value); }
    catch (...) { co_return false; }

    co_return true;
}

std::execution::task<bool> AppSettings::save(helios::Async& async) const
{
    auto dir = SettingDir();
    std::error_code ec;
    fs::create_directories(dir, ec);

    // One value_from for the whole object — no hand-built JSON.
    const auto payload = boost::json::serialize(boost::json::value_from(*this));

    const auto path = dir / "app.json";
    const auto executor = async.get_executor();

    // Bounded retry on transient open failures (e.g. the file is briefly
    // locked); the write itself runs on the pool.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        boost::asio::stream_file file{
            executor, path.string(),
            boost::asio::stream_file::read_write
            | boost::asio::stream_file::create
            | boost::asio::stream_file::truncate
        };
        if (!file.is_open()) continue;

        const auto n = co_await async.writeAsync(file, boost::asio::buffer(payload));
        co_return n > 0;
    }
    co_return false;
}
