# HeliosView App Template

> **Repo layout:** this template lives in the **`template/react-js`** branch.
> The `master` branch holds only a template index (React = `template/react-js`,
> Vue = `template/vue-js`, …). Get this template with:
> `git clone --recursive -b template/react-js https://github.com/CoplenSasbian/HeliosView-Template.git`

This is the **React** template in the HeliosView template family. Its sibling
templates — `template/vue-js` (Vue 3) and `template/vanilla-js` (no framework)
— share the same C++ backend and scripts; only the checked-in `frontend/`
differs, so pick the branch whose frontend you want and keep the C++ side
identical. You can also re-scaffold the frontend to any other framework with
`scripts/setup`.

Built on the **HeliosView** C++ library (WebView2 + native ⇄ JS bridge):
<https://github.com/CoplenSasbian/HeliosView>

A ready-to-hack-on starting point for a **HeliosView** desktop app: one C++
window with an embedded WebView (WebView2) + a **React** web frontend (Vite).
Fork it and start building — the plumbing is already wired up:

```
┌────────────────────────── C++ ──────────────────────────┐
│  main()                                                  │
│    AppContext    UI loop (helios::App) + thread pool     │
│                  (helios::Async, IOCP-based)             │
│    MainWindow    : helios::WebViewWindow (WebView2)      │
│        │  window.helios.call('appInfo', {}) → Promise    │
│        ▼                                                 │
└──── frontend (React + Vite) ───────────────────────────┘
     dev : vite dev server on :5173   (HMR)
     prod: built assets in exe-dir/assets/  (file://)
```

## Prerequisites

| tool      | needed for                  |
| --------- | --------------------------- |
| CMake ≥ 4.3 | the C++ build (see below) |
| C++23 compiler | MSVC (Windows) or Clang/GCC |
| ninja     | the C++ build on macOS/Linux (Windows falls back to Visual Studio) |
| Node.js ≥ 20 | the frontend (Vite)      |

The HeliosView library is a **git submodule** (`HeliosView/`, pinned to a
specific commit), including its own dependencies (stdexec, nlohmann/json,
WebView2 SDK) — no vcpkg/conan. `git clone --recursive` fetches it for you;
for an existing checkout run `git submodule update --init`.

## Platforms

| | Windows | macOS / Linux |
| --- | --- | --- |
| scripts | `scripts/*.cmd` (pure cmd batch — no PowerShell, no execution policy involved) | `scripts/*.sh` (bash), also works under Git Bash on Windows |
| C++ backend | Win32 + WebView2 | not shipped yet — the library only has a Win32 backend today |

The scripts come in two flavors (dev/build/setup behave identically). On
macOS/Linux the frontend tooling (dev server, `vite build`) works out of the
box, and the C++ side builds as soon as HeliosView gains non-Windows backends.

## Getting started

A React frontend (and the HeliosView submodule) is checked in, so the very
first run needs nothing but the two commands below. `git clone --recursive`
or `git submodule update --init` fetches the library. `npm install` only runs
the first time (the scripts do it automatically).

```bat
REM Windows:
REM 1a. Develop (C++ + Vite dev server with HMR)
scripts\dev.cmd

REM 1b. Build for distribution (C++ + compiled frontend)
scripts\build.cmd
build\release\bin\HeliosViewApp.exe
```

```sh
# macOS / Linux (or Git Bash on Windows):
./scripts/dev.sh          # develop (HMR)
./scripts/build.sh        # release build
build/release/bin/HeliosViewApp
```

### Switching the frontend framework (React, Svelte, ...)

The frontend is a plain Vite project, so you can re-scaffold it with any
official template (react, vue, svelte, solid, preact, lit, vanilla — JS or TS):

```bat
scripts\setup.cmd -Template react-ts -Force    REM replaces frontend/
```
```sh
./scripts/setup.sh -t react-ts -f          # replaces frontend/
```

The scripts run the official `npm create vite` scaffold. TS templates also
get `frontend/src/helios.d.ts` with typings for the native bridge. Without
`-Force`/`-f` the scripts refuse to touch an existing `frontend/`.

## How the two build modes work

**Dev is the CMake default** — only packaging switches to prod
(`scripts/build.cmd` / `build.sh` set `-DHELIOSVIEW_TEMPLATE_DEV=OFF`
themselves):

| | Dev (default) | Prod (`build.cmd` / `build.sh`) |
| --- | --- | --- |
| frontend | `vite dev --port 5173 --strictPort` (HMR) | `vite build` → `frontend/dist` |
| C++ | `navigate("http://localhost:5173")` | `HELIOSVIEW_TEMPLATE_DEV=OFF` → `navigate("file:///…/assets/index.html")` |
| assets | served by Vite | copied next to the exe as `assets/` on every build |

`base: './'` in `vite.config.js` makes Vite emit relative asset paths, so the
built page works over `file://` without any custom protocol handler. All DLLs
(`HeliosView.dll`, `WebView2Loader.dll`) and `assets/` sit in `build/*/bin`
next to the exe, so the whole folder is directly distributable.

The mode is a CMake option (cached per build dir) — you can also configure
manually:

```sh
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug                  # dev is the default
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DHELIOSVIEW_TEMPLATE_DEV=OFF
```

If an existing build dir was configured before the default flip, it still
holds the old cached value — reconfigure it explicitly (`-DHELIOSVIEW_TEMPLATE_DEV=ON`)
or clear the cache.

### CLion workflow (IDE builds/runs the C++ app)

If you develop in CLion (or another IDE), you don't need the full dev loop —
CLion builds and runs the app, and a dedicated script just serves the
frontend. Dev is the default, so no CMake configuration is needed:

1. Run the frontend dev server in a terminal (Ctrl+C stops it):
   ```bat
   scripts\vite.cmd
   ```
   ```sh
   ./scripts/vite.sh
   ```
2. Run `HeliosViewApp` from CLion — it loads `http://localhost:5173` (HMR).
3. Packaging: `scripts\build.cmd` (or `build.sh`) — it flips to prod
   automatically.

## Architecture

`main()` wires up exactly three things:

1. **`AppContext`** (`src/AppContext.h`) — the application-wide services,
   created first so it outlives every window:
   - `app()` — the **UI loop**: message pump, event queue, idle tasks
     (`helios::App`). Run with `app().exec()`; deliver work to the UI thread
     with `app().postTask(...)`.
   - `threadPool()` — the background **thread pool** (`helios::Async`,
     IOCP-based async file/TCP I/O). Schedule with `threadPool().post(...)`
     or as a `std::execution` scheduler.
   Both are also `std::execution` schedulers (`get_scheduler()`).
2. **`MainWindow`** (`src/MainWindow.h/.cpp`) — inherits
   `helios::WebViewWindow`; its constructor registers the native ⇄ JS bridge,
   `loadFrontend()` navigates to the dev server (dev) or the built assets
   (prod).
3. **The UI loop** — `return ctx.app().exec()`, which exits when the last
   window closes.

## The native ↔ JS bridge

HeliosView injects `window.helios` into every page. From the frontend:

```js
const info = await window.helios.call('appInfo', {});   // → { app: { name, version, helios } }
```

On the C++ side, `MainWindow::setupBridge()` binds it (handlers are detached
`std::execution::task` coroutines; the Promise resolves when the task
completes, from any thread):

```cpp
bindJson<nlohmann::json>("appInfo", [](nlohmann::json)
                             -> std::execution::task<helios::JsonResp<nlohmann::json>> {
    co_return helios::JsonResp<nlohmann::json>{ "app", {
        { "name",    "HeliosViewApp" },
        { "version", HELIOSVIEW_TEMPLATE_VERSION },
        { "helios",  helios::version() },
    }};
});
```

The `ping` binding demonstrates the full context: it hops to the thread pool
(`co_await schedule(threadPool().get_scheduler())`), returns to the UI thread
via `app().postTask(...)`, and broadcasts the result back to the page's
`BroadcastChannel('ping')` — one round trip through all three layers.

More from the library README (DTO `Req` types, bidirectional
`BroadcastChannel`, error shapes, async slots, async file/TCP I/O):

- `call(name, ...args)` → `Promise` — native functions bound with `bindJson`
- `new BroadcastChannel(name)` — bidirectional: native `broadcast()` and JS
  `postMessage()` (via `subscribeJson`)
- handler results: DTO / number / string / `nlohmann::json` / `JsonResp<T>` /
  `JsonError<T>` / `void`

## Project layout

```
CMakeLists.txt       add_subdirectory(HeliosView) + the app target + dev/prod mode
HeliosView/          HeliosView library as a git submodule (pinned commit)
src/AppContext.h     the context: UI loop (helios::App) + thread pool (helios::Async)
src/MainWindow.h/.cpp  the window: WebViewWindow subclass, bridge bindings, frontend URL
src/main.cpp         create the context + window, run the UI loop
frontend/            React + Vite project (switch frameworks with scripts/setup)
scripts/setup.cmd/.sh          (re)scaffold the frontend (framework picker, -Force/-f to replace)
scripts/vite.cmd/.sh           run the Vite dev server only (for CLion/IDE workflows)
scripts/dev.cmd/.sh            dev loop: Vite dev server + C++ app
scripts/build.cmd/.sh          release: vite build + C++ prod build
scripts/helios.d.ts            bridge typings template (copied into TS scaffolds)
```

## Customizing

- **Dev server port** — change `scripts/dev.cmd`'s `-Port` / `scripts/dev.sh`'s
  first argument, `frontend/vite.config.js` and keep
  `HELIOSVIEW_TEMPLATE_DEV_URL` in sync (or pass `-DHELIOSVIEW_TEMPLATE_DEV_URL=…`
  to CMake).
- **Pin the library** — the HeliosView submodule (`HeliosView/`) is pinned to
  a concrete commit in the index/.gitmodules; update it with
  `git submodule update --remote` (or bump the pinned commit manually) for a
  reproducible build.
- **Window** — size/title in `src/main.cpp`; see the HeliosView README for
  `WindowStyle`, signals/slots, async I/O, coroutines.
- **Distribution** — copy `build/release/bin/*` (exe + DLLs + `assets/`).
  A WiX/MSIX installer can be added later; the folder is already self-contained.
