// toast.jsx — antd-style global message toasts. Imperative API: call
// toast.success('...') / toast.error('...') from anywhere (pages, components);
// the singleton <ToastContainer/> (mounted once in main.jsx) renders the stack
// at the top of the app. Auto-dismiss after `duration` ms (default 3000;
// pass 0 to keep it until clicked). Clicking a toast dismisses it early.
//
// Three-phase lifecycle per toast:
//   1. entering  — slide-in keyframe (CSS), FLIP skipped
//   2. settled   — no animation, FLIP runs on layout changes
//   3. leaving   — slide-out keyframe (CSS), FLIP skipped, then removed
//
// Remaining toasts slide up smoothly via a FLIP layout animation.

import { useRef, useLayoutEffect, useSyncExternalStore } from 'react'

let toasts = []
let listeners = []
let nextId = 1

function emit() {
  for (const l of listeners) l()
}

function subscribe(listener) {
  listeners.push(listener)
  return () => {
    listeners = listeners.filter((l) => l !== listener)
  }
}

function getSnapshot() {
  return toasts
}

// Phase 1 → 3: mark leaving so the CSS exit animation plays.
// A fallback timeout ensures removal even if onAnimationEnd doesn't fire
// (e.g. prefers-reduced-motion disables the animation).
function dismiss(id) {
  toasts = toasts.map((t) => (t.id === id ? { ...t, leaving: true } : t))
  emit()
  setTimeout(() => remove(id), 250)
}

// Actually remove from the list (called after the exit animation).
function remove(id) {
  toasts = toasts.filter((t) => t.id !== id)
  emit()
}

function push(type, content, duration) {
  const id = nextId++
  toasts = [...toasts, { id, type, content, entering: true }]
  emit()
  if (duration !== 0) {
    setTimeout(() => dismiss(id), duration > 0 ? duration : 3000)
  }
}

export const toast = {
  success: (content, duration) => push('success', content, duration),
  error: (content, duration) => push('error', content, duration),
  warning: (content, duration) => push('warning', content, duration),
  info: (content, duration) => push('info', content, duration),
}

export default function ToastContainer() {
  const items = useSyncExternalStore(subscribe, getSnapshot)
  // Positions captured at the end of the previous useLayoutEffect.
  const oldPos = useRef(new Map())

  useLayoutEffect(() => {
    // --- FLIP: only for settled toasts (not entering, not leaving) ----------
    items.forEach((t) => {
      if (t.entering || t.leaving) return
      const el = document.querySelector(`[data-toast-id="${t.id}"]`)
      if (!el) return
      const prev = oldPos.current.get(t.id)
      if (prev == null) return
      const cur = el.getBoundingClientRect().top
      const dy  = prev - cur
      if (Math.abs(dy) < 0.5) return
      // Instantly move to old position, then animate to current.
      el.style.transition = 'none'
      el.style.transform  = `translateY(${dy}px)`
      requestAnimationFrame(() => {
        el.style.transition = 'transform 200ms cubic-bezier(0.22, 1, 0.36, 1)'
        el.style.transform  = ''
        const done = () => {
          el.style.transition = ''
          el.removeEventListener('transitionend', done)
        }
        el.addEventListener('transitionend', done, { once: true })
      })
    })

    // Snapshot current positions for the next render cycle.
    const snap = new Map()
    items.forEach((t) => {
      const el = document.querySelector(`[data-toast-id="${t.id}"]`)
      if (el) snap.set(t.id, el.getBoundingClientRect().top)
    })
    oldPos.current = snap
  })

  // --- event handlers (stable refs, no re-render) --------------------------
  const onEnterEnd = (id) => {
    toasts = toasts.map((t) => (t.id === id ? { ...t, entering: false } : t))
    emit()
  }
  const onLeaveEnd = (id) => remove(id)

  return (
    <div className="toast-container" aria-live="polite">
      {items.map((t) => {
        let cls = `toast toast--${t.type}`
        if (t.entering) cls += ' toast--entering'
        if (t.leaving)  cls += ' toast--leaving'
        return (
          <div
            key={t.id}
            data-toast-id={t.id}
            className={cls}
            onClick={() => dismiss(t.id)}
            role="status"
            onAnimationEnd={() => {
              if (t.entering) onEnterEnd(t.id)
              else if (t.leaving) onLeaveEnd(t.id)
            }}
          >
            <span className="toast__dot" />
            <span>{t.content}</span>
          </div>
        )
      })}
    </div>
  )
}
