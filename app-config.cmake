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
set(HELIOSVIEW_TEMPLATE_APP_NAME "HeliosViewApp")

# Process identity (UTF-8): Windows AppUserModelID, macOS bundle identifier,
# Linux application id. src/main.cpp passes it to helios::App::setAppId() at
# startup; the OS also uses it to group taskbar entries / toasts, and it is the
# default id for helios::notificationInit(). Use your own reverse-DNS id.
set(HELIOSVIEW_TEMPLATE_APP_ID "com.example.heliosview.app")

# Window title shown in the title bar.
set(HELIOSVIEW_TEMPLATE_APP_TITLE "HeliosView App")
