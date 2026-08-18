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
# configure time) are handled by the library itself — this include only makes
# sure everything is checked out so a freshly cloned repository builds without
# a manual `git submodule update`.
#
# Auto-configure: clone with --recursive, or let this script fetch what's
# missing at configure time. It is safe to skip `git submodule update` by
# hand entirely — CMake does it for you:
#
#   git submodule update --init -- HeliosView            (HeliosView/)
#   git -C HeliosView submodule update --init            (HeliosView's nested: stdexec + json)
#
# NOTE: do NOT use `git submodule update --init --recursive` here. One level
# is exactly right: it checks out HeliosView's direct submodules (stdexec +
# nlohmann/json + the Boost superproject, no libs). HeliosView's own
# cmake/ensure-submodule.cmake then fetches only the Boost libs it needs
# (each `--depth 1`) at configure time.
#
# The submodule is recorded as a specific HeliosView commit (see .gitmodules /
# the gitlink in the index) for reproducible builds.
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
    message(STATUS "Retrying with --depth=1...")
    execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --depth=1 -- HeliosView
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMAND_ECHO STDOUT
            RESULT_VARIABLE git_result
            ERROR_VARIABLE git_error
    )
endif()
if(NOT git_result EQUAL 0)
    message(FATAL_ERROR "Failed to init HeliosView: ${git_error}")
endif()

# HeliosView's own nested submodules (stdexec + nlohmann/json + the Boost
# superproject, checked out at the commits recorded in HeliosView's index).
# One level deep only — do not recurse (the Boost libs are fetched
# individually, `--depth 1`, by HeliosView's own cmake at configure time).
execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/HeliosView
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE nested_result
        ERROR_VARIABLE nested_error
)
if(NOT nested_result EQUAL 0)
    message(STATUS "Retrying nested with --depth=1...")
    execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --depth=1
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/HeliosView
            COMMAND_ECHO STDOUT
            RESULT_VARIABLE nested_result
            ERROR_VARIABLE nested_error
    )
endif()
if(NOT nested_result EQUAL 0)
    message(FATAL_ERROR "Failed to init HeliosView's submodules: ${nested_error}")
endif()
