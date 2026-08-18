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
        "Frontend dev server URL (dev mode; keep in sync with the Vite port in scripts/dev.cmd / dev.sh)")

set(HELIOSVIEW_TEMPLATE_FRONTEND_DIST "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist" CACHE PATH
        "Built frontend output directory (prod mode)")

# ---------------------------------------------------------------------------
# The HeliosView library, checked in as a git submodule (HeliosView/) tracking
# the master branch. Its own dependencies (stdexec + nlohmann/json + the Boost
# superproject as nested submodules, WebView2 SDK pulled from NuGet at
# configure time) are handled entirely by the library itself.
#
# This script only ensures that the HeliosView source tree itself is checked
# out (by initializing the top-level submodule) so that add_subdirectory()
# can be called. Anything deeper is left to HeliosView's own CMake logic.
#
# `git submodule update --init` is idempotent, so it runs unconditionally on
# every configure — no existence checks, no retries; it converges to the
# commit recorded in the index whether the checkout is fresh, partial or
# already complete.
#
# IMPORTANT: Do NOT use `--recursive` here. One level is exactly right:
# only HeliosView itself is fetched. Nested submodules are not handled here.
# ---------------------------------------------------------------------------
set(HELIOSVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)  # skip the library's own demos

find_package(Git REQUIRED)

execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init -- HeliosView
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE git_result
        ERROR_VARIABLE git_error
)
if(NOT git_result EQUAL 0)
    message(FATAL_ERROR "Failed to init HeliosView: ${git_error}")
endif()

# HeliosView's nested submodules are NOT initialized here.
# They will be taken care of by HeliosView's own build system
# (e.g., its cmake/ensure-submodule.cmake or similar scripts)
# when add_subdirectory(HeliosView) is processed.