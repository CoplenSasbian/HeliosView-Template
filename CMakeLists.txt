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

# ---------------------------------------------------------------------------
# Build mode
#   Dev  (default):                     the WebView loads the frontend dev
#         server (Vite, HMR). Start it with scripts/vite.cmd (or dev.cmd).
#   Prod (HELIOSVIEW_TEMPLATE_DEV=OFF): the WebView loads the built frontend,
#         copied next to the exe as assets/. Packaging only - set
#         automatically by scripts/build.cmd / build.sh.
#
# Note: the option is cached per build dir. Changing the default here does
# not touch existing build directories - reconfigure them (or clear the
# cache) to pick up a new value.
# ---------------------------------------------------------------------------
option(HELIOSVIEW_TEMPLATE_DEV "Run against the frontend dev server instead of the built assets" ON)

set(HELIOSVIEW_TEMPLATE_DEV_URL "http://localhost:5173" CACHE STRING
    "Frontend dev server URL (dev mode; keep in sync with the Vite port in scripts/dev.ps1)")

set(HELIOSVIEW_TEMPLATE_FRONTEND_DIST "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist" CACHE PATH
    "Built frontend output directory (prod mode)")

# ---------------------------------------------------------------------------
# The HeliosView library, checked in as a git submodule (HeliosView/), pinned
# to the v1.0.1 tag. Its own dependencies (stdexec + nlohmann/json + Boost as
# nested submodules, WebView2 SDK from NuGet at configure time) are handled by
# the library itself — nothing else is needed.
#
#   git clone --recursive <this repo>         (or after a plain clone:)
#   git submodule update --init               (checkout HeliosView/)
#   git -C HeliosView submodule update --init (checkout stdexec/json/Boost)
#
# The Boost libs HeliosView needs are initialized automatically at CMake
# configure time (HeliosView v1.0.1 vendors a Boost superproject; do NOT run
# `git submodule update --init --recursive` — it would fetch all ~160 Boost
# libraries).
#
# The submodule is pinned to a specific HeliosView commit (see .gitmodules /
# the gitlink in the index) for reproducible builds.
# ---------------------------------------------------------------------------
set(HELIOSVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)  # skip the library's own demos

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/HeliosView/CMakeLists.txt")
    message(FATAL_ERROR
        "HeliosView submodule is not checked out.\n"
        "Run: git submodule update --init  (or clone with --recursive)\n"
        "in:  ${CMAKE_CURRENT_SOURCE_DIR}")
endif()
add_subdirectory(HeliosView)

# ---------------------------------------------------------------------------
# The app: AppContext (UI loop + asio-backed background pool) + MainWindow
# ---------------------------------------------------------------------------
add_executable(HeliosViewApp
    src/main.cpp
    src/AppContext.h
    src/MainWindow.cpp
    src/MainWindow.h
)
target_link_libraries(HeliosViewApp PRIVATE HeliosView::Core)

target_compile_definitions(HeliosViewApp PRIVATE
    HELIOSVIEW_TEMPLATE_VERSION="${PROJECT_VERSION}"
)

if(HELIOSVIEW_TEMPLATE_DEV)
    target_compile_definitions(HeliosViewApp PRIVATE
        HELIOSVIEW_TEMPLATE_DEV
        HELIOSVIEW_TEMPLATE_DEV_URL="${HELIOSVIEW_TEMPLATE_DEV_URL}"
    )
    message(STATUS "HeliosViewTemplate: DEV mode (frontend: ${HELIOSVIEW_TEMPLATE_DEV_URL})")
else()
    # Prod: copy the built frontend next to the exe on every build.
    add_custom_command(TARGET HeliosViewApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${HELIOSVIEW_TEMPLATE_FRONTEND_DIST}"
            "$<TARGET_FILE_DIR:HeliosViewApp>/assets"
        COMMENT "Copying frontend build output to $<TARGET_FILE_DIR:HeliosViewApp>/assets"
        VERBATIM)
    message(STATUS "HeliosViewTemplate: PROD mode (assets: ${HELIOSVIEW_TEMPLATE_FRONTEND_DIST})")
    message(STATUS "HeliosViewTemplate: hint: PROD is for packaging - build with scripts/build.cmd / build.sh")
endif()
