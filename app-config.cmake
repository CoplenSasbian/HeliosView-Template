# app-config.cmake - the single place to configure the app's identity.
#
# Edit the values below and rebuild - nothing else needs to change:
#   - the executable (target) name -> the .exe file name
#   - the window title
#   - the app name reported by the 'appInfo' bridge call
#
# CMakeLists.txt includes this file. scripts\dev.cmd / build.cmd find the
# executable automatically (by scanning the build output), so they do NOT
# need to be updated when you rename the app.

# Executable (target) name: the .exe file name and the CMake target name.
set(HELIOSVIEW_TEMPLATE_APP_NAME "HeliosViewApp")

# Window title shown in the title bar.
set(HELIOSVIEW_TEMPLATE_APP_TITLE "HeliosView App")
