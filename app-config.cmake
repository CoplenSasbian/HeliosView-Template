# app-config.cmake - the single place to configure the app's identity.
#
# Edit the values below and rebuild - nothing else needs to change:
#   - the executable (target) name -> the .exe file name
#   - the process identity (AppUserModelID / bundle id / application id)
#   - the window title
#   - the app name reported by the 'appInfo' bridge call
#
# CMakeLists.txt includes this file. scripts\dev.cmd / build.cmd find the
# executable automatically (by scanning the build output), so they do NOT
# need to be updated when you rename the app.

# Executable (target) name: the .exe file name and the CMake target name.
set(HELIOSVIEW_TEMPLATE_APP_NAME "GameTrigger")

# Process identity (UTF-8): Windows AppUserModelID, macOS bundle identifier,
# Linux application id. src/main.cpp passes it to helios::App::setAppId() at
# startup, and it is the default id for helios::notificationInit().
# NOTE: on Windows this string also names the Start Menu shortcut the toast
# backend creates (%APPDATA%\...\Programs\<id>.lnk), which is what Windows shows
# as the notification's app name — hence the display-style "Game Trigger" kept
# from the previous hard-coded notificationInit("Game Trigger") call. Switch to
# a reverse-DNS id (e.g. "com.coplensasbian.gametrigger") once the app ships on
# macOS/Linux too; the toast header will then show that id instead.
set(HELIOSVIEW_TEMPLATE_APP_ID "Game Trigger")

# Window title shown in the title bar.
set(HELIOSVIEW_TEMPLATE_APP_TITLE "Game Trigger")
