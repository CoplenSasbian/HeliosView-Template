// wallpaper.js — apply a background image from the local library.
//
// The WebView cannot read the filesystem, so the native side (BgImages.cpp)
// enumerates AppRoot()/bg and serves each image as a base64 data: URL. We apply
// the chosen image as an inline background on the .app shell (inline beats the
// aurora gradient in theme.css) and flip <html data-theme> by the image's
// average luminance: bright → light theme (dark text), dim → dark theme.
//
// Persistence: the background file name and the theme prefs live in native
// app.json (AppSettings::backgroundName / themeMode / themeThreshold), not in
// localStorage — so they survive as reliably as every other app setting. This
// module keeps a lightweight in-memory cache so the synchronous getters stay
// sync; loadAppearance() hydrates it from settings_get at startup.
import { call } from './bridge'

const DEFAULT_PREFS = { mode: 'auto', threshold: 128 }

// The shipped accent. A custom accent ALSO re-tints the aurora backdrop
// (applyDesign → --aur1..4); the default keeps the curated multi-colour one.
export const DEFAULT_ACCENT = '#5ea6ff'

// Font-size presets: a multiplier applied to the browser default (16px) root
// font-size so every rem-based value scales together.
const FONT_SIZE_MAP = { sm: 15, md: 16, lg: 18 }
// UI density: a scale factor on spacing + control metrics. Only this master is
// a user setting; every --sp-* / --ctrl-* value is derived from it here.
const DENSITY_MAP = { sm: 0.85, md: 1, lg: 1.15 }
// Base spacing scale (px) from theme.css, applied as density * base.
const SPACE_BASE = {
  '--sp-2xs': 4, '--sp-xs': 8, '--sp-sm': 12, '--sp-md': 16,
  '--sp-lg': 24, '--sp-xl': 32, '--sp-2xl': 48,
}
const CTRL_BASE = { '--ctrl-height': 38, '--ctrl-pad-x': 14 }

// In-memory appearance snapshot (background + theme + design prefs).
// `backgroundName` = a photo file name (from bg/) or "" (aurora); `solidColor`
// = a chosen CSS solid fill or "" (none). The two are mutually exclusive:
// picking one clears the other, so the persisted JSON keeps each field clean
// (no sentinels smuggled into the file-name field).
let appearance = {
  backgroundName: '',
  solidColor: '',
  mode: DEFAULT_PREFS.mode,
  threshold: DEFAULT_PREFS.threshold,
  design: { glassBlur: 6, cornerRadius: 6, accentColor: DEFAULT_ACCENT, fontSize: 'md', density: 'md' },
}

// ---- unified (debounced) native persist -------------------------------------
// EVERY setting change funnels through schedulePersist: it keeps the latest
// whole-settings payload and writes to native only once the caller settles
// (updates merge, so rapid slider drags collapse into a single app.json write).
let _persistTimer = null
let _pending = null

function schedulePersist(patch) {
  _pending = { ..._pending, ...patch } // last patch wins across calls
  clearTimeout(_persistTimer)
  _persistTimer = setTimeout(async () => {
    const payload = _pending
    _pending = null
    if (!payload) return
    try {
      await call('settings_set', payload)
    } catch (e) {
      console.error('persist settings failed', e)
    }
  }, 400)
}

// Flush any pending write immediately (best-effort on demand).
export function flushSettings() {
  if (_persistTimer) {
    clearTimeout(_persistTimer)
    _persistTimer = null
  }
  if (!_pending) return
  const payload = _pending
  _pending = null
  return call('settings_set', payload).catch(() => {})
}

// Read persisted design data from a settings object (defaults if absent).
function designFrom(s) {
  const d = s?.design ?? {}
  const pick = (v, def) => (typeof v === 'string' && v.startsWith('#') ? v : def)
  return {
    glassBlur: Number.isFinite(d.glassBlur) ? d.glassBlur : 28,
    cornerRadius: Number.isFinite(d.cornerRadius) ? d.cornerRadius : 12,
    accentColor: pick(d.accentColor, DEFAULT_ACCENT),
    fontSize: ['sm', 'md', 'lg'].includes(d.fontSize) ? d.fontSize : 'md',
    density: ['sm', 'md', 'lg'].includes(d.density) ? d.density : 'md',
  }
}

// #hex → rgba(r,g,b,a) helper for deriving translucent accents.
function hexToRgba(hex, alpha) {
  let h = String(hex).replace('#', '')
  if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2]
  const n = parseInt(h, 16)
  if (Number.isNaN(n)) return null
  return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${alpha})`
}

// Lighten/darken a #hex by mixing toward white (amt>0) or black (amt<0).
function shadeHex(hex, amt) {
  let h = String(hex).replace('#', '')
  if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2]
  const n = parseInt(h, 16)
  if (Number.isNaN(n)) return hex
  let r = (n >> 16) & 255, g = (n >> 8) & 255, b = n & 255
  if (amt >= 0) { r += (255 - r) * amt; g += (255 - g) * amt; b += (255 - b) * amt }
  else { r *= 1 + amt; g *= 1 + amt; b *= 1 + amt }
  const c = (v) => Math.round(Math.max(0, Math.min(255, v))).toString(16).padStart(2, '0')
  return `#${c(r)}${c(g)}${c(b)}`
}

// Rotate a #hex color's hue by `deg`, keeping its saturation/lightness, so the
// aurora hues derived from the user's accent keep the same "weight" as it.
function shiftHueHex(hex, deg) {
  let h = String(hex).replace('#', '')
  if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2]
  const n = parseInt(h, 16)
  if (Number.isNaN(n)) return hex
  const r = ((n >> 16) & 255) / 255, g = ((n >> 8) & 255) / 255, b = (n & 255) / 255
  const max = Math.max(r, g, b), min = Math.min(r, g, b)
  const l = (max + min) / 2
  const d = max - min
  let s = 0, hue = 0
  if (d !== 0) {
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min)
    switch (max) {
      case r: hue = (g - b) / d + (g < b ? 6 : 0); break
      case g: hue = (b - r) / d + 2; break
      default: hue = (r - g) / d + 4
    }
    hue *= 60
  }
  hue = (((hue + deg) % 360) + 360) % 360
  const hue2rgb = (p, q, t) => {
    if (t < 0) t += 1
    if (t > 1) t -= 1
    if (t < 1 / 6) return p + (q - p) * 6 * t
    if (t < 1 / 2) return q
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6
    return p
  }
  let rr, gg, bb
  if (s === 0) { rr = gg = bb = l }
  else {
    const q = l < 0.5 ? l * (1 + s) : l + s - l * s
    const p = 2 * l - q
    rr = hue2rgb(p, q, hue / 360 + 1 / 3)
    gg = hue2rgb(p, q, hue / 360)
    bb = hue2rgb(p, q, hue / 360 - 1 / 3)
  }
  const c = (v) => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0')
  return `#${c(rr)}${c(gg)}${c(bb)}`
}

// A small SVG fractal-noise tile, blended into the gradient with a soft-light
// mix (see paintSolid's background-blend-mode) so it reads as photographic film
// grain / a real surface rather than grey speckles floating on top. Higher
// frequency + extra octaves = finer, more organic grain like a photo.
const NOISE_SVG =
  "<svg xmlns='http://www.w3.org/2000/svg' width='180' height='180'>" +
  "<filter id='n'><feTurbulence type='fractalNoise' baseFrequency='0.9' " +
  "numOctaves='3' stitchTiles='stitch'/></filter>" +
  "<rect width='100%' height='100%' filter='url(#n)' opacity='0.5'/></svg>"
const NOISE_DATA_URI = `data:image/svg+xml,${encodeURIComponent(NOISE_SVG)}`

// CSS background for a "solid" color that isn't quite flat: an off-center
// radial gradient (light spot drifts upper-left, like a subtle key-light)
// with a vignette that darkens the four corners, plus a faint grain on top.
// The gradient always drifts toward the mid-tone — a LIGHT base darkens at
// the center (and lifts at the edges), a DARK base does the opposite — so
// it reads as solid but breathes with a photographic quality.
export function solidBackgroundCss(hex) {
  if (typeof hex !== 'string' || !hex.startsWith('#')) return hex
  const lum = luminanceOfHex(hex) ?? 128
  const baseLight = lum >= 140            // base color reads as light
  const centerAmt = 0.08                  // center shift (toward mid-tone)
  const edgeAmt = 0.04                     // edge shift (away from mid-tone)
  const center = baseLight ? shadeHex(hex, -centerAmt) : shadeHex(hex, centerAmt)
  const edge = baseLight ? shadeHex(hex, edgeAmt) : shadeHex(hex, -edgeAmt)
  // Vignette: darken corners with a subtle radial that goes from transparent
  // at center to a slightly darkened version of the base at the edges.
  const vignette = baseLight ? shadeHex(hex, -0.12) : shadeHex(hex, -0.15)
  return (
    `url("${NOISE_DATA_URI}"), ` +
    // Vignette layer: four-corner darkening (conic gradient trick)
    `radial-gradient(ellipse 85% 85% at 50% 50%, transparent 50%, ${vignette} 100%), ` +
    // Main off-center key-light gradient (upper-left drift)
    `radial-gradient(ellipse 70% 70% at 38% 35%, ${center} 0%, ${hex} 55%, ${edge} 100%)`
  )
}

// Pull the persisted appearance (background + theme + design) from native
// settings and apply the design tokens to the CSS variables.
export async function loadAppearance() {
  try {
    const s = await call('settings_get')
    const mode = ['auto', 'dark', 'light'].includes(s?.themeMode) ? s.themeMode : DEFAULT_PREFS.mode
    const threshold = Number.isFinite(s?.themeThreshold) ? s.themeThreshold : DEFAULT_PREFS.threshold
    appearance = {
      backgroundName: typeof s?.backgroundName === 'string' ? s.backgroundName : '',
      solidColor: typeof s?.solidColor === 'string' ? s.solidColor : '',
      mode,
      threshold,
      design: designFrom(s),
    }
    applyDesign(appearance.design)
    return appearance
  } catch (e) {
    console.error('loadAppearance failed', e)
    return appearance
  }
}

// Write the user-tunable design tokens onto :root as inline CSS variables,
// overriding the theme.css defaults. No restart needed. Only the PRIMARY
// values (accent / blur / radius / font size) are user settings; every derived
// tone (strong/soft/glass/glow, aurora hues, scaled radii/fonts) is computed
// here from them — the UI never exposes the derived ones.
export function applyDesign(d) {
  const css = document.documentElement.style
  const blur = Math.max(4, Math.min(48, d?.glassBlur ?? 28))
  css.setProperty('--glass-blur', `blur(${blur}px) saturate(1.7) brightness(1.08)`)
  css.setProperty('--background-blur', `blur(${Math.round(blur * 0.6)}px)`)

  const r = Math.max(4, Math.min(32, d?.cornerRadius ?? 12))
  css.setProperty('--r-sm', `${Math.round(r * 0.7)}px`)
  css.setProperty('--r-md', `${r}px`)
  css.setProperty('--r-lg', `${Math.round(r * 1.3)}px`)
  css.setProperty('--r-xl', `${Math.round(r * 1.8)}px`)

  // Accent + its derived tones (kept in the same hue).
  if (d?.accentColor) {
    const a = d.accentColor
    css.setProperty('--c-accent', a)
    css.setProperty('--c-accent-strong', shadeHex(a, 0.12))
    css.setProperty('--c-accent-soft', hexToRgba(a, 0.16) ?? 'transparent')
    css.setProperty('--c-accent-glass', hexToRgba(a, 0.22) ?? 'transparent')
    css.setProperty('--c-accent-glow', hexToRgba(a, 0.32) ?? 'transparent')
  }

  // Aurora backdrop follows a CUSTOM accent: derive a 4-hue family from it so
  // the whole screen stays in one palette. The default accent instead keeps
  // the curated multi-colour aurora — dropping the inline --aur1..4 lets the
  // theme.css defaults (incl. the light-theme soft variants) win again.
  const accent = d?.accentColor
  if (accent && accent.toLowerCase() !== DEFAULT_ACCENT) {
    css.setProperty('--aur1', accent)
    css.setProperty('--aur2', shiftHueHex(accent, 42))
    css.setProperty('--aur3', shiftHueHex(accent, -48))
    css.setProperty('--aur4', shiftHueHex(accent, 168))
  } else {
    css.removeProperty('--aur1')
    css.removeProperty('--aur2')
    css.removeProperty('--aur3')
    css.removeProperty('--aur4')
  }

  // Font-size: a global root scaling so every rem-based value moves together.
  const fs = FONT_SIZE_MAP[d?.fontSize] ?? FONT_SIZE_MAP.md
  css.setProperty('--root-font-size', `${fs}px`)

  // Density: scale spacing + control metrics from the master factor.
  const dens = DENSITY_MAP[d?.density] ?? DENSITY_MAP.md
  for (const [k, v] of Object.entries(SPACE_BASE))
    css.setProperty(k, `${Math.round(v * dens)}px`)
  for (const [k, v] of Object.entries(CTRL_BASE))
    css.setProperty(k, `${Math.round(v * dens)}px`)
}

// Persist + apply a full design object (always send the whole object so the
// native nested merge doesn't lose the other tokens).
export function setDesign(next) {
  const merged = { ...appearance.design, ...next }
  appearance = { ...appearance, design: merged }
  applyDesign(merged) // instant local effect
  schedulePersist({ design: merged }) // debounced native write
  return merged
}

// ---- helpers ----------------------------------------------------------------

// Resolve the full-bleed app shell (.app). Inline backgroundImage here beats the
// stylesheet aurora gradient, so the chosen photo cannot be hidden.
const shell = () => document.querySelector('.app')

// Average relative luminance (0..255) of an image source, or null on error.
function averageLuminance(src) {
  return new Promise((resolve) => {
    const img = new Image()
    img.onload = () => {
      try {
        const c = document.createElement('canvas')
        c.width = 16
        c.height = 16
        const ctx = c.getContext('2d', { willReadFrequently: true })
        ctx.drawImage(img, 0, 0, 16, 16)
        const { data } = ctx.getImageData(0, 0, 16, 16)
        let sum = 0
        for (let i = 0; i < data.length; i += 4) {
          sum += 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2]
        }
        resolve(sum / (data.length / 4))
      } catch { resolve(null) }
    }
    img.onerror = () => resolve(null)
    img.src = src
  })
}

// ---- theme (luminance-driven) -------------------------------------------------

export function getThemePrefs() {
  return { mode: appearance.mode, threshold: appearance.threshold }
}

// The currently chosen solid color ("" = none → photo or aurora).
export function getSolidColor() {
  return appearance.solidColor || ''
}

// Persist theme prefs (and background) to native app.json — debounced through
// the unified schedulePersist (the frontend re-pulls the authoritative snapshot
// from settings_get when it needs to confirm).
export function setAppearancePrefs(patch) {
  const next = {
    backgroundName: typeof patch?.backgroundName === 'string'
      ? patch.backgroundName : appearance.backgroundName,
    solidColor: typeof patch?.solidColor === 'string'
      ? patch.solidColor : appearance.solidColor,
    mode: ['auto', 'dark', 'light'].includes(patch?.mode)
      ? patch.mode : appearance.mode,
    threshold: Number.isFinite(patch?.threshold)
      ? patch.threshold : appearance.threshold,
  }
  appearance = {
    backgroundName: next.backgroundName,
    solidColor: next.solidColor,
    mode: next.mode,
    threshold: next.threshold,
    design: appearance.design,
  }
  schedulePersist({
    backgroundName: next.backgroundName,
    solidColor: next.solidColor,
    themeMode: next.mode,
    themeThreshold: next.threshold,
  })
  return { ...appearance }
}

// The current tunable design tokens (blur / radius / colors).
export function getDesign() {
  return { ...appearance.design }
}

export function applyThemeForImage(src) {
  const { mode, threshold } = getThemePrefs()
  if (mode !== 'auto') {
    document.documentElement.dataset.theme = mode
    return mode
  }
  return averageLuminance(src).then((lum) => {
    if (lum == null) return null
    const theme = lum >= threshold ? 'light' : 'dark'
    document.documentElement.dataset.theme = theme
    return theme
  })
}

// Resolve light/dark from a solid color's OWN luminance (no image to measure).
// Lets "auto" mode flip correctly for CSS-only backgrounds.
function applyThemeForColor(hex) {
  const { mode, threshold } = getThemePrefs()
  if (mode !== 'auto') {
    document.documentElement.dataset.theme = mode
    return mode
  }
  const lum = luminanceOfHex(hex)
  const theme = lum == null ? 'dark' : lum >= threshold ? 'light' : 'dark'
  document.documentElement.dataset.theme = theme
  return theme
}

// Relative luminance (0..255) of a #hex, or null if it can't be parsed.
function luminanceOfHex(hex) {
  let h = String(hex).replace('#', '')
  if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2]
  const n = parseInt(h, 16)
  if (Number.isNaN(n)) return null
  return 0.299 * ((n >> 16) & 255) + 0.587 * ((n >> 8) & 255) + 0.114 * (n & 255)
}

export function applyThemePrefsOnly() {
  const { mode } = getThemePrefs()
  if (mode !== 'auto') {
    document.documentElement.dataset.theme = mode
    return mode
  }
  return null
}

// ---- background library ------------------------------------------------------

// List image file names available in the bg dir. Returns { ok, names?, error? }:
//   ok=false, error="bridge"  → the native bg_list endpoint is missing (the C++
//                                exe wasn't rebuilt), not an empty dir.
//   ok=true, names=[]          → the bg dir exists but has no supported images.
//   ok=false, error="read"     → some other bridge failure.
export async function bgList() {
  try {
    const res = await call('bg_list')
    return { ok: true, names: Array.isArray(res?.images) ? res.images : [] }
  } catch (e) {
    console.error('bgList failed', e)
    const msg = String(e?.error ?? e ?? '')
    return {
      ok: false,
      error: /bg_list|not (a function|found)|Cannot read/i.test(msg) ? 'bridge' : 'read',
    }
  }
}

// Load one image as a data: URL (for the picker grid or to apply).
export async function bgLoad(name) {
  try {
    const res = await call('bg_load', name)
    return res?.url || ''
  } catch (e) {
    console.error('bgLoad failed', e)
    return ''
  }
}

// A lightweight (~320px, few KB) data: URL for the picker thumbnails.
// Full 4K images are multi-MB; using them as grid previews stalls the page.
export async function bgLoadThumb(name) {
  try {
    const res = await call('bg_loadThumb', name)
    return res?.url || ''
  } catch (e) {
    console.error('bgLoadThumb failed', e)
    return ''
  }
}

// The currently selected background file name ("" = none = aurora gradient),
// from the native-persisted appearance cache.
export function getCurrentBg() {
  return appearance.backgroundName || ''
}

// Paint the shell WITHOUT persisting — used both for live switches and at
// startup (where loadAppearance already hydrated the cache).
function paintPhoto(url) {
  const el = shell()
  if (!el) return false
  el.classList.add('has-wallpaper')
  el.style.backgroundSize = 'cover'
  el.style.backgroundPosition = 'center'
  el.style.backgroundImage = `url("${url}")`
  el.style.backgroundColor = ''
  return true
}

// Paint a "solid" color with an off-center key-light, corner vignette, and
// faint grain — looks solid but breathes with a photographic quality. Reusing
// the `has-wallpaper` shell hides the aurora blob layer underneath.
function paintSolid(hex) {
  const el = shell()
  if (!el) return false
  el.classList.add('has-wallpaper')
  el.style.backgroundColor = hex                 // fallback / base
  el.style.backgroundImage = solidBackgroundCss(hex)
  // 3 layers: noise tile, vignette ellipse, main gradient (all cover-sized)
  el.style.backgroundSize = '180px 180px, cover, cover'
  el.style.backgroundRepeat = 'repeat, no-repeat, no-repeat'
  el.style.backgroundPosition = 'center, center, center'
  // grain=vignette=soft-light, main gradient=normal
  el.style.backgroundBlendMode = 'soft-light, soft-light, normal'
  return true
}

// Apply a PHOTO background by file name: load its data URL, paint, set the theme
// (luminance-driven in "auto"), clear any solid color, and persist.
export async function applyBackground(name) {
  const url = await bgLoad(name)
  if (!url) return ''
  setAppearancePrefs({ backgroundName: name, solidColor: '' })
  flushSettings() // decisive single action: persist now, not after the debounce
  paintPhoto(url)
  applyThemeForImage(url)
  rememberWallpaper(url)
  return url
}

// Apply a CSS-only SOLID background: never touches the filesystem. Picking a
// solid clears the photo name (the two are mutually exclusive). Persist is the
// debounced writer — the color picker fires onChange every frame while dragging,
// so we must NOT flushSettings() here (that would hammer native with writes);
// the visual paint below is instant and the write settles 400ms after the drag.
export async function applySolidBg(hex) {
  setAppearancePrefs({ backgroundName: '', solidColor: hex })
  paintSolid(hex)
  applyThemeForColor(hex)
  rememberWallpaper(null) // not a photo → don't re-measure a stale image
  return 'solid'
}

// Clear the background back to the aurora gradient (persists both fields empty).
export async function clearBackground() {
  setAppearancePrefs({ backgroundName: '', solidColor: '' })
  flushSettings() // decisive single action: persist now
  const el = shell()
  if (el) {
    el.classList.remove('has-wallpaper')
    el.style.backgroundImage = ''
    el.style.backgroundSize = ''
    el.style.backgroundPosition = ''
    el.style.backgroundRepeat = ''
    el.style.backgroundBlendMode = ''
    el.style.backgroundColor = ''
  }
  // Forget the removed image: reapplyTheme must not re-measure its luminance.
  // Then re-resolve the theme — a bright wallpaper left data-theme="light",
  // and in auto mode the aurora gradient needs the dark default back.
  _lastWallpaper = null
  reapplyTheme()
}

// On startup: load the persisted appearance, then paint the last chosen
// background (solid color wins over a photo; neither → aurora). No persist —
// loadAppearance already hydrated the cache.
export async function applyStoredBackground() {
  await loadAppearance()
  const solid = getSolidColor()
  if (solid) {
    paintSolid(solid)
    applyThemeForColor(solid)
    return 'solid'
  }
  const name = getCurrentBg()
  if (name) {
    const url = await bgLoad(name)
    if (url) {
      paintPhoto(url)
      applyThemeForImage(url)
      return url
    }
  }
  return ''
}

// Re-apply the theme after prefs change, using the last-applied image.
// `prefs` may carry the JUST-chosen mode/threshold; without it the module
// cache is read (call setAppearancePrefs first so it isn't stale — see the
// SettingsPage theme switcher, whose old order applied the PREVIOUS mode).
let _lastWallpaper = null
export function rememberWallpaper(url) { _lastWallpaper = url }
export function reapplyTheme(prefs) {
  const mode = prefs?.mode ?? getThemePrefs().mode
  if (mode !== 'auto') {
    document.documentElement.dataset.theme = mode
    return mode
  }
  if (appearance.solidColor) return applyThemeForColor(appearance.solidColor)
  if (_lastWallpaper) return applyThemeForImage(_lastWallpaper)
  // auto + no wallpaper: the aurora gradient is always dark → drop any
  // leftover data-theme (e.g. "light" from a previously bright wallpaper).
  delete document.documentElement.dataset.theme
  return 'dark'
}
