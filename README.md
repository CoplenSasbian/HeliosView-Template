# HeliosView App Template

One repo, one tag per frontend framework: **`master` holds only this index**,
and every tag is a complete, runnable HeliosView desktop app template —
a C++ window with an embedded WebView (WebView2) + a Vite frontend,
pre-configured for a different frontend framework.

## Tags

| Tag | Frontend | Status |
| --- | --- | --- |
| `vue-js` | Vue 3 (JavaScript, Vite) | ✅ current |
| `react-js` | React (JavaScript) | planned |
| `svelte-js` | Svelte (JavaScript) | planned |
| `solid-js` | Solid (JavaScript) | planned |
| `preact-js` | Preact (JavaScript) | planned |
| `lit-js` | Lit (JavaScript) | planned |
| `vanilla-js` | Vanilla JS (no framework) | planned |

All tags share the same C++ architecture — `AppContext` (UI loop + thread
pool), `MainWindow` (WebView + native ⇄ JS bridge), dev/prod build modes and
cross-platform scripts (`scripts/*.cmd` for Windows, `scripts/*.sh` for
macOS/Linux/Git Bash). The only difference between tags is the checked-in
frontend. Every tag also ships a `scripts/setup` (re)scaffold script, so you
can switch the frontend to any other framework without changing branches.

## Getting started

```sh
git clone --branch vue-js https://github.com/CoplenSasbian/HeliosView-Template.git
```

(Or `git clone` then `git checkout vue-js` — tags are listed above.)

## How this repo is organized

- `master` — this index only (tag list + conventions).
- `vue-js` — the current template (Vue 3).
- A new framework gets a new `<framework>-js` tag (TypeScript variants can
  use the built-in `setup` script to re-scaffold as `-ts` templates).

## Docs

The full documentation (prerequisites, dev/prod modes, the native ⇄ JS
bridge, CLion workflow) lives in the README inside each tag.
