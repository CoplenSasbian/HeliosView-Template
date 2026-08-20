# HeliosView App Template

Desktop app templates built on **HeliosView** — a small C++ library that
embeds a WebView (WebView2) into a native window, with a UI message loop and
a native ⇄ JS bridge (`window.helios.call(...)` → `Promise`).

This repo is the template family for that library: **one branch per frontend
framework**, under the `template/` prefix. `master` holds only this index, and
every `template/*` branch is a complete, runnable template — C++ app (window +
bridge + dev/prod build modes) + a Vite frontend + cross-platform scripts —
pre-configured for a different frontend framework.

- **HeliosView library** (C++): <https://github.com/CoplenSasbian/HeliosView>
- **This template repo**: <https://github.com/CoplenSasbian/HeliosView-Template>

Maintained by [CoplenSasbian](https://github.com/CoplenSasbian).

## Templates (branches)

| Branch | Frontend | Status |
| --- | --- | --- |
| `template/vue-js` | Vue 3 (JavaScript, Vite) | ✅ current |
| `template/react-js` | React (JavaScript, Vite) | ✅ current |
| `template/vanilla-js` | Vanilla JS (no framework, Vite) | ✅ current |

All templates share the same C++ architecture — `AppContext` (UI loop),
`MainWindow` (WebView + native ⇄ JS bridge), dev/prod build modes and
cross-platform scripts (`scripts/*.cmd` for Windows, `scripts/*.sh` for
macOS/Linux/Git Bash). The only difference between branches is the checked-in
frontend. Every template also ships a `scripts/setup` (re)scaffold script, so
you can switch the frontend to any other framework without changing branches.

## Getting started

```sh
git clone --recursive -b template/vue-js https://github.com/CoplenSasbian/HeliosView-Template.git
```

(or `-b template/react-js` / `-b template/vanilla-js`). `--recursive` fetches
the **HeliosView** library (a git submodule tracking the **master** branch),
which each template checks in as a git submodule (`HeliosView/`, with stdexec
+ nlohmann/json as nested submodules, WebView2 SDK pulled from NuGet at
configure time). (Or
`git clone`, then `git checkout template/vue-js` + `git submodule update
--init` + `git -C HeliosView submodule update --init`.)

The templates **auto-configure**: their `CMakeLists.txt` runs
`ensure-submodule.cmake`, which initializes any missing submodules (HeliosView
and then its nested stdexec/json) at configure time — so `git clone` (even
without `--recursive`) followed by a build works out of the box. Init is one
level deep only, so it never pulls a recursive Boost superproject.

## How this repo is organized

- `master` — this index only (template list + conventions).
- `template/vue-js`, `template/react-js`, `template/vanilla-js` — the current
  templates.
- A new framework gets a new `template/<framework>-js` branch (TypeScript
  variants can use the built-in `setup` script to re-scaffold as `-ts`
  templates).

## Docs

The full documentation (prerequisites, dev/prod modes, the native ⇄ JS
bridge, CLion workflow) lives in the README inside each template branch.
