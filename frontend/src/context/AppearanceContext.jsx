// AppearanceContext.jsx — appearance management: theme (light/dark/auto/threshold),
// background (photo/solid/aurora), design tokens (glassBlur, cornerRadius, accentColor,
// fontSize, density), and luminance-driven theme resolution.
// Replaces the old wallpaper.js module with a unified React context.

import React, { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react'
import { call } from '../bridge'

export const DEFAULT_ACCENT = '#5ea6ff'
export const SOLID_KEY = 'solid:'
const DEFAULT_PREFS = { mode: 'auto', threshold: 128 }

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

// ---- unified (debounced) native persist -------------------------------------
let _persistTimer = null
let _pending = null

function schedulePersist(patch) {
  _pending = { ..._pending, ...patch }
  clearTimeout(_persistTimer)
  _persistTimer = setTimeout(async () => {
    const payload = _pending
    _pending = null
    if (!payload) return
    try {
      await call('settings_set', payload)
    } catch (e) {
      console.error('AppearanceContext: persist settings failed', e)
    }
  }, 400)
}

// Flush any pending write immediately.
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
    glassBlur: Number.isFinite(d.glassBlur) ? d.glassBlur : 6,
    cornerRadius: Number.isFinite(d.cornerRadius) ? d.cornerRadius : 6,
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

// Rotate a #hex color's hue by `deg`.
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

const NOISE_SVG =
  "<svg xmlns='http://www.w3.org/2000/svg' width='180' height='180'>" +
  "<filter id='n'><feTurbulence type='fractalNoise' baseFrequency='0.9' " +
  "numOctaves='3' stitchTiles='stitch'/></filter>" +
  "<rect width='100%' height='100%' filter='url(#n)' opacity='0.5'/></svg>"
const NOISE_DATA_URI = `data:image/svg+xml,${encodeURIComponent(NOISE_SVG)}`

// CSS background for a "solid" color with key-light, vignette, and grain.
export function solidBackgroundCss(hex) {
  if (typeof hex !== 'string' || !hex.startsWith('#')) return hex
  const lum = luminanceOfHex(hex) ?? 128
  const baseLight = lum >= 140
  const centerAmt = 0.08
  const edgeAmt = 0.04
  const center = baseLight ? shadeHex(hex, -centerAmt) : shadeHex(hex, centerAmt)
  const edge = baseLight ? shadeHex(hex, edgeAmt) : shadeHex(hex, -edgeAmt)
  const vignette = baseLight ? shadeHex(hex, -0.12) : shadeHex(hex, -0.15)
  return (
    `url("${NOISE_DATA_URI}"), ` +
    `radial-gradient(ellipse 85% 85% at 50% 50%, transparent 50%, ${vignette} 100%), ` +
    `radial-gradient(ellipse 70% 70% at 38% 35%, ${center} 0%, ${hex} 55%, ${edge} 100%)`
  )
}

// Write the design tokens onto :root as inline CSS variables.
export function applyDesign(d) {
  const css = document.documentElement.style
  const blur = Math.max(4, Math.min(48, d?.glassBlur ?? 6))
  css.setProperty('--glass-blur', `blur(${blur}px) saturate(1.7) brightness(1.08)`)
  css.setProperty('--background-blur', `blur(${Math.round(blur * 0.6)}px)`)

  const r = Math.max(4, Math.min(32, d?.cornerRadius ?? 6))
  css.setProperty('--r-sm', `${Math.round(r * 0.7)}px`)
  css.setProperty('--r-md', `${r}px`)
  css.setProperty('--r-lg', `${Math.round(r * 1.3)}px`)
  css.setProperty('--r-xl', `${Math.round(r * 1.8)}px`)

  if (d?.accentColor) {
    const a = d.accentColor
    css.setProperty('--c-accent', a)
    css.setProperty('--c-accent-strong', shadeHex(a, 0.12))
    css.setProperty('--c-accent-soft', hexToRgba(a, 0.16) ?? 'transparent')
    css.setProperty('--c-accent-glass', hexToRgba(a, 0.22) ?? 'transparent')
    css.setProperty('--c-accent-glow', hexToRgba(a, 0.32) ?? 'transparent')
  }

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

  const fs = FONT_SIZE_MAP[d?.fontSize] ?? FONT_SIZE_MAP.md
  css.setProperty('--root-font-size', `${fs}px`)

  const dens = DENSITY_MAP[d?.density] ?? DENSITY_MAP.md
  for (const [k, v] of Object.entries(SPACE_BASE))
    css.setProperty(k, `${Math.round(v * dens)}px`)
  for (const [k, v] of Object.entries(CTRL_BASE))
    css.setProperty(k, `${Math.round(v * dens)}px`)
}

const shell = () => document.querySelector('.app')

// Relative luminance (0..255) of the central 60% of an image source.
function averageLuminance(src) {
  return new Promise((resolve) => {
    const img = new Image()
    img.onload = () => {
      try {
        const S = 16
        const c = document.createElement('canvas')
        c.width = S
        c.height = S
        const ctx = c.getContext('2d', { willReadFrequently: true })
        const iw = img.naturalWidth || img.width
        const ih = img.naturalHeight || img.height
        const crop = 0.6
        ctx.drawImage(
          img,
          iw * (1 - crop) / 2, ih * (1 - crop) / 2,
          iw * crop, ih * crop,
          0, 0, S, S
        )
        const { data } = ctx.getImageData(0, 0, S, S)
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

// Relative luminance (0..255) of a #hex.
function luminanceOfHex(hex) {
  let h = String(hex).replace('#', '')
  if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2]
  const n = parseInt(h, 16)
  if (Number.isNaN(n)) return null
  return 0.299 * ((n >> 16) & 255) + 0.587 * ((n >> 8) & 255) + 0.114 * (n & 255)
}

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

function paintSolid(hex) {
  const el = shell()
  if (!el) return false
  el.classList.add('has-wallpaper')
  el.style.backgroundColor = hex
  el.style.backgroundImage = solidBackgroundCss(hex)
  el.style.backgroundSize = '180px 180px, cover, cover'
  el.style.backgroundRepeat = 'repeat, no-repeat, no-repeat'
  el.style.backgroundPosition = 'center, center, center'
  el.style.backgroundBlendMode = 'soft-light, soft-light, normal'
  return true
}

function clearShell() {
  const el = shell()
  if (!el) return false
  el.classList.remove('has-wallpaper')
  el.style.backgroundImage = ''
  el.style.backgroundSize = ''
  el.style.backgroundPosition = ''
  el.style.backgroundRepeat = ''
  el.style.backgroundBlendMode = ''
  el.style.backgroundColor = ''
  return true
}

function applyThemeToDom(mode, threshold, currentLum) {
  if (mode !== 'auto') {
    document.documentElement.dataset.theme = mode
    return mode
  }
  if (currentLum == null) {
    // auto + no wallpaper/solid (aurora): the aurora gradient is dark.
    delete document.documentElement.dataset.theme
    return 'dark'
  }
  const theme = currentLum >= threshold ? 'light' : 'dark'
  document.documentElement.dataset.theme = theme
  return theme
}

// ---- background library bridge calls -----------------------------------------

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

export async function bgLoad(name) {
  try {
    const res = await call('bg_load', name)
    return res?.url || ''
  } catch (e) {
    console.error('bgLoad failed', e)
    return ''
  }
}

export async function bgLoadThumb(name) {
  try {
    const res = await call('bg_loadThumb', name)
    return res?.url || ''
  } catch (e) {
    console.error('bgLoadThumb failed', e)
    return ''
  }
}

// ---- Context & Provider ------------------------------------------------------

const AppearanceContext = createContext(null)

export function AppearanceProvider({ children }) {
  const [themeMode, setThemeModeState] = useState(DEFAULT_PREFS.mode)
  const [themeThreshold, setThemeThresholdState] = useState(DEFAULT_PREFS.threshold)
  const [design, setDesignState] = useState(() => designFrom({}))
  const [backgroundName, setBackgroundName] = useState('')
  const [solidColor, setSolidColor] = useState('')
  const [currentLuminance, setCurrentLuminance] = useState(null)
  const [loading, setLoading] = useState(true)

  // Keep latest values in refs so callbacks never use stale closures
  const themeModeRef = useRef(themeMode)
  themeModeRef.current = themeMode
  const themeThresholdRef = useRef(themeThreshold)
  themeThresholdRef.current = themeThreshold
  const currentLuminanceRef = useRef(currentLuminance)
  currentLuminanceRef.current = currentLuminance

  // Lifecycle & Initial hydration:
  // Loads persisted appearance, applies CSS design variables, paints background,
  // computes initial currentLuminance, and resolves the initial theme.
  useEffect(() => {
    let isMounted = true

    async function init() {
      try {
        const s = await call('settings_get')
        if (!isMounted) return

        const mode = ['auto', 'dark', 'light'].includes(s?.themeMode) ? s.themeMode : DEFAULT_PREFS.mode
        const threshold = Number.isFinite(s?.themeThreshold) ? s.themeThreshold : DEFAULT_PREFS.threshold
        const d = designFrom(s)
        const bgName = typeof s?.backgroundName === 'string' ? s.backgroundName : ''
        const solid = typeof s?.solidColor === 'string' ? s.solidColor : ''

        applyDesign(d)
        setDesignState(d)
        setThemeModeState(mode)
        setThemeThresholdState(threshold)
        setBackgroundName(bgName)
        setSolidColor(solid)

        themeModeRef.current = mode
        themeThresholdRef.current = threshold

        if (solid) {
          paintSolid(solid)
          const lum = luminanceOfHex(solid)
          setCurrentLuminance(lum)
          currentLuminanceRef.current = lum
          applyThemeToDom(mode, threshold, lum)
        } else if (bgName) {
          const url = await bgLoad(bgName)
          if (!isMounted) return
          if (url) {
            paintPhoto(url)
            const lum = await averageLuminance(url)
            if (!isMounted) return
            setCurrentLuminance(lum)
            currentLuminanceRef.current = lum
            applyThemeToDom(mode, threshold, lum)
          } else {
            clearShell()
            setCurrentLuminance(null)
            currentLuminanceRef.current = null
            applyThemeToDom(mode, threshold, null)
          }
        } else {
          clearShell()
          setCurrentLuminance(null)
          currentLuminanceRef.current = null
          applyThemeToDom(mode, threshold, null)
        }
      } catch (e) {
        console.error('AppearanceProvider: init failed', e)
      } finally {
        if (isMounted) setLoading(false)
      }
    }

    init()

    const onBeforeUnload = () => { flushSettings() }
    window.addEventListener('beforeunload', onBeforeUnload)

    return () => {
      isMounted = false
      window.removeEventListener('beforeunload', onBeforeUnload)
      flushSettings()
    }
  }, [])

  const setThemeMode = useCallback((mode) => {
    setThemeModeState(mode)
    themeModeRef.current = mode
    applyThemeToDom(mode, themeThresholdRef.current, currentLuminanceRef.current)
    schedulePersist({ themeMode: mode })
  }, [])

  const setThemeThreshold = useCallback((threshold) => {
    const num = Number(threshold)
    setThemeThresholdState(num)
    themeThresholdRef.current = num
    applyThemeToDom(themeModeRef.current, num, currentLuminanceRef.current)
    schedulePersist({ themeThreshold: num })
  }, [])

  const setThemePrefs = useCallback((patch) => {
    const nextMode = patch?.mode ?? themeModeRef.current
    const nextThreshold = patch?.threshold != null ? Number(patch.threshold) : themeThresholdRef.current
    setThemeModeState(nextMode)
    setThemeThresholdState(nextThreshold)
    themeModeRef.current = nextMode
    themeThresholdRef.current = nextThreshold
    applyThemeToDom(nextMode, nextThreshold, currentLuminanceRef.current)
    schedulePersist({ themeMode: nextMode, themeThreshold: nextThreshold })
  }, [])

  const updateDesign = useCallback((patch) => {
    setDesignState((prev) => {
      const next = { ...prev, ...patch }
      applyDesign(next)
      schedulePersist({ design: next })
      return next
    })
  }, [])

  const applyBackground = useCallback(async (name) => {
    const url = await bgLoad(name)
    if (!url) return ''
    setBackgroundName(name)
    setSolidColor('')
    paintPhoto(url)
    const lum = await averageLuminance(url)
    setCurrentLuminance(lum)
    currentLuminanceRef.current = lum
    applyThemeToDom(themeModeRef.current, themeThresholdRef.current, lum)
    schedulePersist({ backgroundName: name, solidColor: '' })
    flushSettings()
    return url
  }, [])

  const applySolidBg = useCallback(async (hex) => {
    setBackgroundName('')
    setSolidColor(hex)
    paintSolid(hex)
    const lum = luminanceOfHex(hex)
    setCurrentLuminance(lum)
    currentLuminanceRef.current = lum
    applyThemeToDom(themeModeRef.current, themeThresholdRef.current, lum)
    schedulePersist({ backgroundName: '', solidColor: hex })
    return 'solid'
  }, [])

  const clearBackground = useCallback(async () => {
    setBackgroundName('')
    setSolidColor('')
    clearShell()
    setCurrentLuminance(null)
    currentLuminanceRef.current = null
    applyThemeToDom(themeModeRef.current, themeThresholdRef.current, null)
    schedulePersist({ backgroundName: '', solidColor: '' })
    flushSettings()
    return true
  }, [])

  const currentBg = useMemo(() => {
    if (solidColor) return `${SOLID_KEY}${solidColor}`
    return backgroundName || ''
  }, [solidColor, backgroundName])

  const currentTheme = useMemo(() => {
    if (themeMode !== 'auto') return themeMode
    if (currentLuminance == null) return 'dark'
    return currentLuminance >= themeThreshold ? 'light' : 'dark'
  }, [themeMode, currentLuminance, themeThreshold])

  const value = useMemo(
    () => ({
      themeMode,
      themeThreshold,
      currentTheme,
      currentLuminance,
      currentBg,
      solidColor,
      backgroundName,
      design,
      loading,
      setThemeMode,
      setThemeThreshold,
      setThemePrefs,
      updateDesign,
      applyBackground,
      applySolidBg,
      clearBackground,
    }),
    [
      themeMode,
      themeThreshold,
      currentTheme,
      currentLuminance,
      currentBg,
      solidColor,
      backgroundName,
      design,
      loading,
      setThemeMode,
      setThemeThreshold,
      setThemePrefs,
      updateDesign,
      applyBackground,
      applySolidBg,
      clearBackground,
    ],
  )

  return <AppearanceContext.Provider value={value}>{children}</AppearanceContext.Provider>
}

export function useAppearance() {
  const ctx = useContext(AppearanceContext)
  if (!ctx) throw new Error('useAppearance 必须在 AppearanceProvider 内部使用')
  return ctx
}
