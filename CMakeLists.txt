cmake_minimum_required(VERSION 4.3)

project(HeliosViewTemplate
        VERSION 0.1.0
        DESCRIPTION "HeliosView application template: one C++ window + a Vite web frontend"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# HeliosView.dll, WebView2Loader.dll and the app exe all land in build/bin
# together (same layout as the library), so no PATH setup is needed at runtime.
# The built frontend goes to the software root (build/assets in prod mode) —
# a sibling of bin/, so bin + assets share one top-level directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_PLUGINS_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

include(ensure-submodule.cmake)

add_subdirectory(HeliosView)
add_subdirectory(PluginInterface)
# ---------------------------------------------------------------------------
# The app: AppContext (UI loop) + MainWindow.
# entry.cpp provides the process entry (WinMain on Windows — WIN32/GUI
# subsystem, no console — main elsewhere) and forwards to AppMain() in
# main.cpp, which holds the actual app logic.
# ---------------------------------------------------------------------------

# App identity: program name, window title, ... — edit app-config.cmake
# (the single place to configure these).
include(app-config.cmake)

add_executable(${HELIOSVIEW_TEMPLATE_APP_NAME} WIN32
    src/entry.cpp
    src/main.cpp
    src/AppContext.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/Logger.cpp
    src/Logger.h
    src/AppSettings.cpp
    src/AppSettings.h
    src/ProcessMonitor.h
    src/ProcessMonitor_win32.cpp
        src/Wallpaper.h
        src/Wallpaper.cpp
        src/BgImages.h
        src/BgImages.cpp
        src/plugin/PluginManager.cpp
        src/plugin/PluginManager.h
        src/utils/AppFilePath.cpp
        src/utils/AppFilePath.h
        src/utils/AutoStart.h
        src/utils/AutoStart_win32.cpp
        src/utils/concurrentqueue.h
        src/plugin/DynamicLibraryApi_win32.cpp
        src/plugin/DynamicLibraryApi.h
        plugins/test_plugin/test_plugin_impl.cpp
        src/utils/SingleInstanceGuard.cpp
        src/utils/SingleInstanceGuard.h

)
target_link_libraries(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE HeliosView::Core PluginInterface)
if(WIN32)
    # BgImages uses GDI+ to decode + downscale the background gallery thumbnails.
    target_link_libraries(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE gdiplus)
    # AutoStart registers a Task Scheduler logon task (COM, taskschd.h).
    # ole32 / oleaut32 are in the MSVC default link set, but listed here so
    # non-MSVC toolchains link them too.
    target_link_libraries(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE taskschd ole32 oleaut32)
endif()

# The app links HeliosView::Core (stdexec + Boost stdexec/http templates). These
# instantiate a large number of COMDAT sections per TU, which trips MSVC's default
# section-count limit (C1128). /bigobj lifts it — same fix HeliosView applies to
# its own targets (see HeliosView/CMakeLists.txt). The INTERFACE library does NOT
# propagate its compile options, so the app target must set it itself.
if(MSVC)
    target_compile_options(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE /bigobj)
endif()

target_compile_definitions(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE
    HELIOSVIEW_TEMPLATE_VERSION="${PROJECT_VERSION}"
    HELIOSVIEW_TEMPLATE_APP_NAME="${HELIOSVIEW_TEMPLATE_APP_NAME}"
    HELIOSVIEW_TEMPLATE_APP_TITLE="${HELIOSVIEW_TEMPLATE_APP_TITLE}"
)

# ---- Application icon ------------------------------------------------------
# Embed icon.ico (generated at project root) into the exe. Works with MSVC
# (VS_ICON property) and any generator that supports RC files (icon.rc).
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
    set_target_properties(${HELIOSVIEW_TEMPLATE_APP_NAME} PROPERTIES
        VS_ICON "${CMAKE_SOURCE_DIR}/icon.ico"
    )
    # Generate a minimal .rc file so non-MSVC generators also pick up the icon.
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/icon.rc")
    file(WRITE "${APP_ICON_RC}" "IDI_ICON1 ICON DISCARDABLE \"${CMAKE_SOURCE_DIR}/icon.ico\"\n")
    target_sources(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE "${APP_ICON_RC}")

    # Also stage the standalone icon.ico next to the exe on every build — the
    # app uses it at runtime for the native window icon and system tray icon
    # (MainWindow::appIconPath). Copy it to the runtime output dir and install
    # it alongside the exe so the dist\bin folder is self-contained.
    add_custom_command(TARGET ${HELIOSVIEW_TEMPLATE_APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/icon.ico"
            "$<TARGET_FILE_DIR:${HELIOSVIEW_TEMPLATE_APP_NAME}>/icon.ico"
        COMMENT "Copying application icon next to the exe"
        VERBATIM)
    install(FILES "${CMAKE_SOURCE_DIR}/icon.ico"
            DESTINATION bin)
endif()

# The app requires elevation (WMI process traces, see src/app.manifest).
# CMake's default manifest declares asInvoker, which conflicts on merge —
# disable the default and embed our manifest in a post-build step instead.
if(WIN32 AND CMAKE_MT)
    target_link_options(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE "/MANIFEST:NO")
    add_custom_command(TARGET ${HELIOSVIEW_TEMPLATE_APP_NAME} POST_BUILD
        COMMAND "${CMAKE_MT}" -nologo
            -manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest"
            "-outputresource:$<TARGET_FILE:${HELIOSVIEW_TEMPLATE_APP_NAME}>;#1"
        COMMENT "Embedding app manifest (requireAdministrator)"
        VERBATIM)
endif()

if(HELIOSVIEW_TEMPLATE_DEV)
    target_compile_definitions(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE
        HELIOSVIEW_TEMPLATE_DEV
        HELIOSVIEW_TEMPLATE_DEV_URL="${HELIOSVIEW_TEMPLATE_DEV_URL}"
    )
    message(STATUS "HeliosViewTemplate: DEV mode (frontend: ${HELIOSVIEW_TEMPLATE_DEV_URL})")
else()
    # Prod: copy the built frontend to the software root (a sibling of the
    # bin\ directory that holds the exe) on every build: bin + assets live in
    # the same folder, so the whole thing is one distributable directory.
    add_custom_command(TARGET ${HELIOSVIEW_TEMPLATE_APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${HELIOSVIEW_TEMPLATE_FRONTEND_DIST}"
            "${CMAKE_BINARY_DIR}/assets"
        COMMENT "Copying frontend build output to ${CMAKE_BINARY_DIR}/assets"
        VERBATIM)
    message(STATUS "HeliosViewTemplate: PROD mode (assets: ${HELIOSVIEW_TEMPLATE_FRONTEND_DIST})")
    message(STATUS "HeliosViewTemplate: hint: PROD is for packaging - build with scripts/build.cmd")
endif()

add_subdirectory(plugins)

# ---- install: assemble the distributable (scripts/build.cmd runs
#      `cmake --install build/release --prefix dist`) -----------------------
# Installs the app exe into bin\ and the built frontend into assets\ — bin and
# assets end up as siblings in the software root (dist\). HeliosView's own
# install rules bring its runtime files (HeliosView.dll, WebView2Loader.dll,
# the OpenSSL dlls and cacert.pem) into bin/. Non-destructive: nothing in the
# prefix is ever deleted, and new install rules (own targets, library updates)
# are picked up automatically.
install(TARGETS ${HELIOSVIEW_TEMPLATE_APP_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY "${HELIOSVIEW_TEMPLATE_FRONTEND_DIST}/" DESTINATION assets)

# The library's install rules also install its dev files (headers into
# include\, static libs + cmake config into lib\) - the app does not need
# them at runtime, so drop them from the dist\ staging. Only these two
# freshly-installed directories are removed; nothing else in the prefix is
# touched. If you add your own install rules that put files into include\
# or lib\, remove this block.
install(CODE "
    file(REMOVE_RECURSE
        \"\${CMAKE_INSTALL_PREFIX}/include\"
        \"\${CMAKE_INSTALL_PREFIX}/lib\")
")
