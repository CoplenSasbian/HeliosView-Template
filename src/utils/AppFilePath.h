#pragma once


#include <filesystem>



std::filesystem::path AppRoot()noexcept;
std::filesystem::path SettingDir()noexcept;
std::filesystem::path ConfigDir()noexcept;
std::filesystem::path PluginDir()noexcept;
std::filesystem::path LogDir()noexcept;

// WebView2 browser data for this app: %LOCALAPPDATA%\<AppName>\WebView2, kept
// out of the exe directory so the app also runs from read-only install
// locations (Program Files, ...). Empty path on failure -> the runtime's
// default (next to the exe) is kept.
std::filesystem::path WebView2DataDir()noexcept;

