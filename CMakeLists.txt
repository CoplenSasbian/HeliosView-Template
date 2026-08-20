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
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(ensure-submodule.cmake)

add_subdirectory(HeliosView)

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
)
target_link_libraries(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE HeliosView::Core)

target_compile_definitions(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE
    HELIOSVIEW_TEMPLATE_VERSION="${PROJECT_VERSION}"
    HELIOSVIEW_TEMPLATE_APP_NAME="${HELIOSVIEW_TEMPLATE_APP_NAME}"
    HELIOSVIEW_TEMPLATE_APP_TITLE="${HELIOSVIEW_TEMPLATE_APP_TITLE}"
)

if(HELIOSVIEW_TEMPLATE_DEV)
    target_compile_definitions(${HELIOSVIEW_TEMPLATE_APP_NAME} PRIVATE
        HELIOSVIEW_TEMPLATE_DEV
        HELIOSVIEW_TEMPLATE_DEV_URL="${HELIOSVIEW_TEMPLATE_DEV_URL}"
    )
    message(STATUS "HeliosViewTemplate: DEV mode (frontend: ${HELIOSVIEW_TEMPLATE_DEV_URL})")
else()
    # Prod: copy the built frontend next to the exe on every build.
    add_custom_command(TARGET ${HELIOSVIEW_TEMPLATE_APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${HELIOSVIEW_TEMPLATE_FRONTEND_DIST}"
            "$<TARGET_FILE_DIR:${HELIOSVIEW_TEMPLATE_APP_NAME}>/assets"
        COMMENT "Copying frontend build output to $<TARGET_FILE_DIR:${HELIOSVIEW_TEMPLATE_APP_NAME}>/assets"
        VERBATIM)
    message(STATUS "HeliosViewTemplate: PROD mode (assets: ${HELIOSVIEW_TEMPLATE_FRONTEND_DIST})")
    message(STATUS "HeliosViewTemplate: hint: PROD is for packaging - build with scripts/build.cmd")
endif()
