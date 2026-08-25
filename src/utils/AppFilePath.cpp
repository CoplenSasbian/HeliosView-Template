//
// Created by futurvo on 2026/8/18.
//

#include "AppFilePath.h"

#ifdef _WIN32
#include  <Windows.h>
namespace fs = std::filesystem;

static fs::path GetAppPath() noexcept
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return fs::path(buffer).parent_path();
}

#endif




fs::path AppRoot() noexcept
{
    // AppRoot() is the directory holding the app's own data (settings/logs) -
    // one level above the executable (exe in dist\bin, data in dist\).
    return GetAppPath().parent_path();
}

fs::path SettingDir() noexcept
{
    return AppRoot() / "settings";
}

// User plugin configs live one level below the settings dir, keeping
// SettingDir() itself reserved for app-level configuration.
fs::path ConfigDir() noexcept
{
    return SettingDir() / "configs";
}

// The plugins live under the app root (dist\plugins), kept apart from the exe
// (dist\bin). AppRoot() is the directory holding the app's data (the install
// stages the plugin dlls to <prefix>/plugins via cmake --install).
fs::path PluginDir() noexcept
{
    return AppRoot() / "plugins";
}

fs::path LogDir() noexcept
{
    return AppRoot() / "logs";
}

// WebView2 browser data lives per-user, not next to the exe: the runtime's
// default (<exe>.WebView2) is unwritable when the exe is installed under
// Program Files. %LOCALAPPDATA% is always present for the interactive user.
fs::path WebView2DataDir() noexcept
{
    wchar_t localAppData[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0)
        return {};
    return fs::path(localAppData) / HELIOSVIEW_TEMPLATE_APP_NAME / "WebView2";
}
