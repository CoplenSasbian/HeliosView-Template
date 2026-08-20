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
│    AppContext    UI loop (helios::App)                   │
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

The HeliosView library is a **git submodule** (`HeliosView/`) tracking the
**master** branch (no release tag is published yet — the v1.0.0 tag is one
commit behind master, and that commit only touches the CMake submodule
bootstrap, so the code is identical), including its own dependencies (stdexec
+ nlohmann/json + the Boost superproject as nested submodules, WebView2 SDK
pulled from NuGet at configure time) — no vcpkg/conan. `git clone --recursive`
fetches it all.

> **Auto-configure:** a fresh clone builds out of the box. `CMakeLists.txt`
> runs `ensure-submodule.cmake`, which initializes any missing submodules at
> configure time — the HeliosView submodule and then its own nested ones
> (stdexec + json). You don't have to run `git submodule update` by hand:
>
> ```bat
> git submodule update --init -- HeliosView              REM HeliosView/
> git -C HeliosView submodule update --init              REM stdexec + json
> ```
>
> This stays one level deep (it never recurses into the Boost superproject's
> ~160 libraries). HeliosView's own `cmake/ensure-submodule.cmake` then
> initializes just the Boost libs it needs (each `--depth 1`) at configure
> time.

## Platforms

| | Windows | macOS / Linux |
| --- | --- | --- |
| scripts | `scripts/*.cmd` (pure cmd batch — no PowerShell, no execution policy involved) | not shipped yet — Windows-only for now |
| C++ backend | Win32 + WebView2 | not shipped yet — the library only has a Win32 backend today |

The scripts are Windows-only for now. The frontend tooling (dev server,
`vite build`) works on any OS, and the C++ side builds as soon as HeliosView
gains non-Windows backends — the `*.sh` variants will come back then.

## Getting started

A React frontend (and the HeliosView submodule) is checked in, so the very
first run needs nothing but the two commands below. Configure-time
`ensure-submodule.cmake` fetches the library automatically. `npm install` only
runs the first time (the scripts do it automatically).

```bat
REM Windows:
REM 1a. Develop (C++ + Vite dev server with HMR)
scripts\dev.cmd

REM 1b. Build for distribution (C++ + compiled frontend)
scripts\build.cmd
dist\bin\HeliosViewApp.exe
```

### Switching the frontend framework (React, Svelte, ...)

The frontend is a plain Vite project, so you can re-scaffold it with any
official template (react, vue, svelte, solid, preact, lit, vanilla — JS or TS):

```bat
scripts\setup.cmd -Template react-ts -Force    REM replaces frontend/
```

The scripts run the official `npm create vite` scaffold. Without
`-Force`/`-f` the scripts refuse to touch an existing `frontend/`.

## How the two build modes work

**Dev is the CMake default** — only packaging switches to prod
(`scripts/build.cmd` sets `-DHELIOSVIEW_TEMPLATE_DEV=OFF` itself):

| | Dev (default) | Prod (`build.cmd`) |
| --- | --- | --- |
| frontend | `vite dev --port 5173 --strictPort` (HMR) | `vite build` → `frontend/dist` |
| C++ | `navigate("http://localhost:5173")` | `HELIOSVIEW_TEMPLATE_DEV=OFF` → `navigate("file:///…/assets/index.html")` |
| assets | served by Vite | copied next to the exe as `assets/` on every build |

`base: './'` in `vite.config.js` makes Vite emit relative asset paths, so the
built page works over `file://` without any custom protocol handler. All DLLs
(`HeliosView.dll`, `WebView2Loader.dll`, the OpenSSL dlls), `cacert.pem` and
`assets/` sit in `build/*/bin` next to the exe. `scripts\build.cmd` assembles
**`dist\`** with `cmake --install` (non-destructive — it follows the install
rules in the top-level `CMakeLists.txt` and HeliosView's own, and never
deletes anything in `dist\`). `dist\bin` holds everything needed to run the
app — the whole `dist\` folder is directly distributable.

The mode is a CMake option (cached per build dir) — you can also configure
manually:

```sh
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug                  # dev is the default
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DHELIOSVIEW_TEMPLATE_DEV=OFF
```

If an existing build dir was configured before the default flip, it still
holds the old cached value — reconfigure it explicitly (`-DHELIOSVIEW_TEMPLATE_DEV=ON`)
or clear the cache.

### App name & window title

Configured in **one file**: `app-config.cmake` at the repo root (included by
`CMakeLists.txt`). Edit the values there and rebuild — nothing else needs to
change:

| Variable in `app-config.cmake` | Default | Used for |
| --- | --- | --- |
| `HELIOSVIEW_TEMPLATE_APP_NAME` | `HeliosViewApp` | executable/target name (the `.exe` file name); also the app name reported by the `appInfo` bridge call |
| `HELIOSVIEW_TEMPLATE_APP_TITLE` | `HeliosView App` | window title |

The C++ side and the build pick the values up automatically, and
`scripts\dev.cmd` / `scripts\build.cmd` find the executable by scanning the
build output — no other place to keep in sync.

### CLion workflow (IDE builds/runs the C++ app)

If you develop in CLion (or another IDE), you don't need the full dev loop —
CLion builds and runs the app, and a dedicated script just serves the
frontend. Dev is the default, so no CMake configuration is needed:

1. Run the frontend dev server in a terminal (Ctrl+C stops it):
   ```bat
   scripts\vite.cmd
   ```
2. Run `HeliosViewApp` from CLion — it loads `http://localhost:5173` (HMR).
3. Packaging: `scripts\build.cmd` — it flips to prod automatically.

## Architecture

`AppMain()` (`src/main.cpp`, called from the platform entry in
`src/entry.cpp` — `WinMain` on Windows, `main` elsewhere) wires up exactly
three things:

1. **`AppContext`** (`src/AppContext.h`) — the application-wide services,
   created first so it outlives every window:
   - `app()` — the **UI loop**: message pump, event queue, idle tasks
     (`helios::App`, also a `std::execution` scheduler). Run with
     `app().exec()`; deliver work to the UI thread with `app().postTask(...)`.
   Threading (HeliosView v1.0.0): every window/WebView API runs on the
   message-loop thread; `postTask`/`quit` and the WebView
   resolve/reject/broadcast calls are safe from any thread. The library no
   longer ships a thread pool (`helios::Async` was removed in v1.0.0) — run
   background work on your own workers and hand results back with
   `app().postTask(...)` (see the `ping` binding).
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

The `ping` binding demonstrates the v1.0.0 threading model: it spawns a plain
`std::thread` worker (v1.0.0 ships no library thread pool — use your own
bounded pool in a real app), the worker pushes the result to the page's
`BroadcastChannel('ping')` via the thread-safe `broadcast()`, and the Promise
resolves on the UI thread — one round trip through the bridge.

More from the library README (DTO `Req` types, bidirectional
`BroadcastChannel`, error shapes, async slots):

- `call(name, ...args)` → `Promise` — native functions bound with `bindJson`
- `new BroadcastChannel(name)` — bidirectional: native `broadcast()` and JS
  `postMessage()` (via `subscribeJson`)
- handler results: DTO / number / string / `nlohmann::json` / `JsonResp<T>` /
  `JsonError<T>` / `void`

## Project layout

```
CMakeLists.txt       ensure-submodule + add_subdirectory(HeliosView) + the app target + dev/prod mode
app-config.cmake     app identity: program name, window title (edit these)
ensure-submodule.cmake  auto-fetch the HeliosView submodule (and its nested deps) at configure time
HeliosView/          HeliosView library as a git submodule (tracks master; concrete commit in the index)
src/AppContext.h     the context: UI loop (helios::App)
src/MainWindow.h/.cpp  the window: WebViewWindow subclass, bridge bindings, frontend URL
src/entry.cpp        the process entry: WinMain (Windows) / main (elsewhere) → AppMain
src/main.cpp         AppMain: create the context + window, run the UI loop
frontend/            React + Vite project (switch frameworks with scripts/setup)
scripts/setup.cmd            (re)scaffold the frontend (framework picker, -Force to replace)
scripts/vite.cmd             run the Vite dev server only (for CLion/IDE workflows)
scripts/dev.cmd              dev loop: Vite dev server + C++ app
scripts/build.cmd            release: vite build + C++ prod build
```

## Customizing

- **Dev server port** — change `scripts/dev.cmd`'s `-Port`,
  `frontend/vite.config.js` and keep
  `HELIOSVIEW_TEMPLATE_DEV_URL` in sync (or pass `-DHELIOSVIEW_TEMPLATE_DEV_URL=…`
  to CMake).
- **Track the library** — the HeliosView submodule (`HeliosView/`) follows the
  **master** branch (set in `.gitmodules`); update it with
  `git submodule update --remote HeliosView`. The gitlink in the index still
  records a concrete commit, so every build stays reproducible.
- **Window** — size/title in `src/main.cpp`; see the HeliosView README for
  `WindowStyle`, signals/slots, coroutines.
- **Distribution** — run `scripts\build.cmd`: it assembles `dist\` with
  `cmake --install`. `dist\bin` is everything the app needs to run (exe +
  HeliosView.dll + WebView2/OpenSSL dlls + `cacert.pem` + `assets\`);
  `dist\include` and `dist\lib` are the HeliosView library's headers/libs
  (only needed when building against the installed library — omit them if
  you ship just the app). The whole `dist\` folder is self-contained and
  directly distributable; a WiX/MSIX installer can be added later.
