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
     prod: built assets served from software-root/assets/ via https://app.local/
```

## Prerequisites

| tool      | needed for                  |
| --------- | --------------------------- |
| CMake ≥ 4.3 | the C++ build (see below) |
| C++23 compiler | MSVC (Windows) or Clang/GCC |
| ninja     | the C++ build on macOS/Linux (Windows falls back to Visual Studio) |
| Node.js ≥ 20 | the frontend (Vite)      |

The HeliosView library is a **git submodule** (`HeliosView/`) tracking the
**master** branch. It is not standalone: it needs its own `third_party/` tree
(stdexec, the Boost superproject with the ~30 Boost libraries it uses, blend2d
and asmjit as nested submodules, plus the WebView2 SDK and OpenSSL 3.5.2 from
NuGet and a CA bundle) — no vcpkg/conan. `git clone --recursive` fetches the
submodule itself, but **not** those nested dependencies.

## Dependencies

One script owns the whole dependency graph — `scripts\setup-dependencies.cmd`
(the `.ps1` / `.py` it wraps hold the logic):

| step | what it fetches | where it lands |
| --- | --- | --- |
| `[1/4]` | the `HeliosView` submodule | `HeliosView/` |
| `[2/4]` | **HeliosView's own dependencies**, delegated to HeliosView's own `scripts/setup-dependencies.*` | `HeliosView/third_party/` |
| `[3/4]` | frontend npm packages (`npm install`) | `frontend/node_modules/` |
| `[4/4]` | verification of every artifact the CMake configure step requires | — |

Step `[2/4]` is a delegation on purpose: HeliosView owns its dependency list
(stdexec, Boost + the libraries it uses, blend2d, asmjit, OpenSSL 3.5.2,
`cacert.pem`, WebView2 SDK) and its CMake **hard-fails** instead of fetching at
configure time — so the list is never duplicated in this repo, and a configure
that succeeds is a build that can proceed.

**You normally never run it by hand.** `scripts\dev.cmd` and
`scripts\build.cmd` first call the internal pre-flight `scripts\_deps.cmd`,
which probes the markers below and runs the setup script only when something is
missing — so a fresh clone builds on the first `scripts\dev.cmd`:

```
HeliosView/.git                                              the submodule itself
HeliosView/third_party/stdexec/.git                          HeliosView's own
HeliosView/third_party/boost/.git                            nested submodules
HeliosView/third_party/blend2d/.git
HeliosView/third_party/asmjit/.git
HeliosView/third_party/boost/libs/json/.git                  one Boost library as a marker
HeliosView/third_party/openssl/build/native/include/openssl/ssl.h
HeliosView/third_party/cacert.pem
HeliosView/third_party/webview2-sdk/build/native/include/WebView2.h
```

Run it explicitly when you want to control it (it is idempotent — every step
probes first and only fetches what is missing):

```bat
scripts\setup-dependencies.cmd                  REM fetch whatever is missing
scripts\setup-dependencies.cmd -Force           REM re-fetch everything
scripts\setup-dependencies.cmd -SkipDownloads   REM git submodules only
scripts\setup-dependencies.cmd -SkipFrontend    REM C++ dependencies only
scripts\setup-dependencies.cmd -Proxy http://127.0.0.1:7890
                                                REM behind a proxy (routes through the .py variant)
```

And to get back to the pristine post-clone state — submodule working trees, the
downloaded packages and `node_modules` go away; tracked files, your branch and
uncommitted code are never touched:

```bat
scripts\reset-dependencies.cmd
scripts\reset-dependencies.cmd -OnlySubmodules   REM keep the downloaded packages
scripts\reset-dependencies.cmd -KeepGitCache     REM keep the git object cache
```

## Platforms

| | Windows | macOS / Linux |
| --- | --- | --- |
| scripts | `scripts/*.cmd` (pure cmd batch — no PowerShell, no execution policy involved) | not shipped yet — Windows-only for now |
| C++ backend | Win32 + WebView2 | not shipped yet — the library only has a Win32 backend today |

The scripts are Windows-only for now. The frontend tooling (dev server,
`vite build`) works on any OS, and the C++ side builds as soon as HeliosView
gains non-Windows backends — the `*.sh` variants will come back then.

## Getting started

A React frontend is checked in, so the very first run needs nothing but the
commands below: the scripts fetch any missing C++ dependency (the HeliosView
submodule *and* everything HeliosView itself needs) and run `npm install` for
you — see **Dependencies** above. `git clone --recursive` is not enough on its
own, and nothing is fetched at CMake configure time.

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
| C++ | `navigate("http://localhost:5173")` | `HELIOSVIEW_TEMPLATE_DEV=OFF` → maps `assets/` to `https://app.local/` and navigates there |
| assets | served by Vite | copied to the software root as `assets/` (a sibling of `bin\`) on every build |

The built page is served from the `assets\` folder at the software root — a
sibling of `bin\`, which holds the exe — through a WebView2 virtual-host
mapping (`https://app.local/`, see `mapLocalFolder` + `localUrl` in
`src/MainWindow.cpp` — the URL shape is engine-defined, so it is not hard-coded):
file:// cannot serve the Vite ES-module output, so `app.local` is the
supported scheme the prod build navigates to. All DLLs (`HeliosView.dll`,
`WebView2Loader.dll`, the OpenSSL dlls) and `cacert.pem` sit in `bin\` next to
the exe. `scripts\build.cmd` assembles **`dist\`** with `cmake --install` from
the install rules (top-level `CMakeLists.txt` + HeliosView's own): `dist\bin`
(exe + DLLs + `cacert.pem`) and `dist\assets` (the frontend) are siblings. The
library's dev files (headers/libs) are dropped, so `dist\` holds only what the
app needs to run — the whole folder is directly distributable.

Two runtime details baked into the template: the WebView2 user data folder
(profile, cache, cookies) lives under the user's app data directory
(`%LOCALAPPDATA%\<AppName>`, see `webviewUserDataFolder()` in
`src/main.cpp`) instead of a `<exe>.WebView2` folder next to the exe; and the
window stays hidden until the initial page load completes — `MainWindow`
shows itself from its `navigationCompleted` signal (see its constructor), so
no blank window flashes while the frontend loads.

The mode is a CMake option (cached per build dir) — you can also configure
manually:

```sh
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug                  # dev is the default
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DHELIOSVIEW_TEMPLATE_DEV=OFF
```

If an existing build dir was configured before the default flip, it still
holds the old cached value — reconfigure it explicitly (`-DHELIOSVIEW_TEMPLATE_DEV=ON`)
or clear the cache.

### App identity, name & window title

Configured in **one file**: `app-config.cmake` at the repo root (included by
`CMakeLists.txt`). Edit the values there and rebuild — nothing else needs to
change:

| Variable in `app-config.cmake` | Default | Used for |
| --- | --- | --- |
| `HELIOSVIEW_TEMPLATE_APP_NAME` | `HeliosViewApp` | executable/target name (the `.exe` file name); also the app name reported by the `appInfo` bridge call |
| `HELIOSVIEW_TEMPLATE_APP_ID` | `com.example.heliosview.app` | process identity: Windows AppUserModelID, macOS bundle identifier, Linux application id; also the default id for `helios::notificationInit()` |
| `HELIOSVIEW_TEMPLATE_APP_TITLE` | `HeliosView App` | window title |

The C++ side and the build pick the values up automatically, and
`scripts\dev.cmd` / `scripts\build.cmd` find the executable by scanning the
build output — no other place to keep in sync.

`src/main.cpp` hands the id to the library before any window is created:

```cpp
helios::App::setAppId(HELIOSVIEW_TEMPLATE_APP_ID);   // Win AUMID / macOS bundle id / Linux app id
```

Its companion is `helios::App::setActivationPolicy(...)`: a tray-only (or
menu-bar-only) app wants `helios::ActivationPolicy::Accessory` so macOS drops
the otherwise useless Dock icon. Both are process-wide and must run before the
first window — see the HeliosView README ("Process identity / activation
policy").

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
app-config.cmake     app identity: program name, process id, window title (edit these)
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
scripts/_deps.cmd            internal pre-flight: fetch missing C++ dependencies (used by dev/build)
scripts/setup-dependencies.* the dependency pipeline: HeliosView + its own deps + frontend
scripts/reset-dependencies.* undo it, back to the pristine post-clone state
scripts/_toolchain.cmd       internal: MSVC environment + cmake/ninja discovery
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
  `cmake --install` and drops the library's dev files (headers/libs). `dist\`
  holds only what the app needs to run (`dist\bin`: exe + HeliosView.dll +
  WebView2/OpenSSL dlls + `cacert.pem`; `dist\assets`: the built frontend —
  bin and assets are siblings). The whole folder is self-contained and
  directly distributable; a WiX/MSIX installer can be added later.
