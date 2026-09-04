// ui.jsx — small shared UI primitives used across pages. All styling is
// variables-driven (see style.css / theme.css); no hardcoded values here.
import { useRef, useState, useEffect, useLayoutEffect, useCallback, useId, forwardRef } from 'react'
import { createPortal } from 'react-dom'

// Button — wraps the .btn styles; variant maps to btn--{variant}
// (primary / ghost / subtle / danger / link), size to btn--{size} (xs / sm / lg / icon).
export const Button = forwardRef(function Button(
  {
    children,
    variant,
    size,
    className,
    loading = false,
    disabled = false,
    icon,
    iconRight,
    type = 'button',
    ...rest
  },
  ref,
) {
  const cls = ['btn']
  if (variant) cls.push(`btn--${variant}`)
  if (size) cls.push(`btn--${size}`)
  if (loading) cls.push('is-loading')
  if (className) cls.push(className)

  return (
    <button
      ref={ref}
      type={type}
      className={cls.join(' ')}
      disabled={disabled || loading}
      aria-busy={loading}
      {...rest}
    >
      {loading && <span className="btn__spinner" aria-hidden="true" />}
      {!loading && icon && <span className="btn__icon btn__icon--left">{icon}</span>}
      {children != null && <span className="btn__text">{children}</span>}
      {!loading && iconRight && <span className="btn__icon btn__icon--right">{iconRight}</span>}
    </button>
  )
})

export function Toggle({ on, onChange, title }) {
  return (
    <button
      type="button"
      className={`toggle${on ? ' is-on' : ''}`}
      role="switch"
      aria-checked={on}
      title={title}
      onClick={() => onChange(!on)}
    >
      <span className="toggle__knob" />
    </button>
  )
}

// Segmented — option bar with a translucent bubble thumb that SLIDES between
// options (measures the active button and moves the thumb to it with a spring).
// The thumb is pure presentation (aria-hidden); the real state stays on the
// buttons. renderAction(value) may append a small element to each option (e.g.
// a delete badge); option.icon (a component) renders before the label.
//
// One travelling layer: .segmented__thumb — a tinted bubble OVER the labels;
// always visible so the settled control keeps the raised look, and the spring
// travel reads as the bubble carrying the highlight with it.
// Segmented — option bar with a solid/tinted bubble thumb that SLIDES between
// options.
// Props:
//  - size: 'xs' | 'sm' | 'md' | 'lg' (defaults to 'md')
//  - options: [{ value, label, icon }]
export function Segmented({ options, value, onChange, renderAction, size, className }) {
  const wrapRef = useRef(null)
  const glassRef = useRef(null)
  const activeIdx = options?.findIndex((o) => o.value === value) ?? -1

  const cls = ['segmented']
  if (size) cls.push(`segmented--${size}`)
  if (className) cls.push(className)

  useLayoutEffect(() => {
    const wrap = wrapRef.current
    const btn = wrap?.querySelectorAll('button')[activeIdx]

    const el = glassRef.current
    if (!el) return
    if (!btn) { el.style.opacity = '0'; return }
    el.style.opacity = '1'
    el.style.width = `${btn.offsetWidth}px`
    el.style.height = `${btn.offsetHeight}px`
    el.style.transform = `translate(${btn.offsetLeft}px, ${btn.offsetTop}px)`
  }, [activeIdx, options, value, size])

  return (
    <div
      className={cls.join(' ')}
      ref={wrapRef}
    >
      <span className="segmented__thumb" ref={glassRef} aria-hidden="true" />
      {options.map((o) => {
        const isIconOnly = !!o.icon && !o.label
        return (
          <button
            key={o.value}
            type="button"
            className={`${o.value === value ? 'is-active' : ''}${isIconOnly ? ' is-icon-only' : ''}`}
            onClick={() => onChange(o.value)}
          >
            {o.icon}
            {o.label}
            {renderAction?.(o.value)}
          </button>
        )
      })}
    </div>
  )
}

export function Card({ title, hint, accent, actions, toolbar, children, index, style, className }) {
  return (
    <div
      className={`card has-noise${className ? ` ${className}` : ''}`}
      style={index !== undefined ? { ...style, '--card-i': index } : style}
    >
      {(title !== undefined || hint || actions || toolbar) && (
        <div className="card__title">
          {title !== undefined && (
            <>
              <span
                className="card__stripe"
                style={accent ? { background: accent } : undefined}
              />
              {title}
            </>
          )}
          {hint && <span className="card__hint">{hint}</span>}
          {toolbar && <span className="card__toolbar">{toolbar}</span>}
          {actions && <span className="card__actions">{actions}</span>}
        </div>
      )}
      {children}
    </div>
  )
}

// Field — labeled form row (label above the control).
export function Field({ label, children }) {
  return (
    <div className="set-field">
      {label && <div className="field__label">{label}</div>}
      {children}
    </div>
  )
}

// Input — styled text/number input (.input).
export function Input({ className, size, type = 'text', ...rest }) {
  const cls = ['input']
  if (size) cls.push(`input--${size}`)
  if (className) cls.push(className)
  return (
    <input
      className={cls.join(' ')}
      type={type}
      {...rest}
    />
  )
}

// ColorPicker — wraps native <input type="color"> with the .swatch style
// (rounded color well + optional label).  value / onChange are controlled;
// label is optional text shown beside the well.
export function ColorPicker({ value, onChange, label, className, ...rest }) {
  return (
    <label className={`swatch${className ? ` ${className}` : ''}`}>
      <input
        type="color"
        value={value}
        onChange={(e) => onChange(e.target.value)}
        {...rest}
      />
      {label && <span>{label}</span>}
    </label>
  )
}

// Select — custom JS dropdown. A native <select> popup can't be styled
// reliably (::picker(select) is Chrome-only and theme vars don't reach it),
// so the menu is portal-rendered plain DOM: every style.css rule applies,
// long lists scroll, and it floats above cards/modals.
// options: [{value, label}]; value/onChange controlled; extra props (title,
// …) land on the trigger button.
export function Select({ options, value, onChange, className, size, disabled, placeholder, ...rest }) {
  const [open, setOpen] = useState(false)
  const [highlight, setHighlight] = useState(-1)
  // Viewport-fixed position of the open menu: {left, minWidth, top|bottom, up}.
  const [pos, setPos] = useState(null)
  const rootRef = useRef(null)
  const menuRef = useRef(null)
  const itemRefs = useRef([])
  const uid = useId()

  const isSm = size === 'sm' || (className?.split(/\s+/).includes('sm') ?? false)
  const isXs = size === 'xs' || (className?.split(/\s+/).includes('xs') ?? false)
  const isLg = size === 'lg' || (className?.split(/\s+/).includes('lg') ?? false)

  const strValue = value != null ? String(value) : ''
  const selectedIdx = options?.findIndex((o) => String(o.value) === strValue) ?? -1
  const selected = selectedIdx >= 0 ? options[selectedIdx] : null
  const hasOptions = !!options && options.length > 0

  const close = useCallback(() => {
    setOpen(false)
    setHighlight(-1)
  }, [])

  const openMenu = useCallback(() => {
    if (disabled) return
    const rect = rootRef.current?.getBoundingClientRect()
    if (!rect) return
    const GAP = 6
    setPos({
      // Anchor the menu to the trigger's left edge; the layout effect below
      // right-aligns it only if the right edge would run off the viewport.
      left: rect.left,
      minWidth: rect.width,
      top: rect.bottom + GAP, // start below; the layout effect flips up if needed
      bottom: undefined,
      up: false,
    })
    setHighlight(selectedIdx >= 0 ? selectedIdx : 0)
    setOpen(true)
  }, [disabled, isSm, selectedIdx])

  // The menu is positioned below the trigger first; once it renders, measure
  // its REAL size and correct the position before paint:
  //  - Y: a short list is far shorter than the max-height, so flipping on a
  //    fixed max-height made menus jump upward while still far from the edge —
  //    flip only when the real height wouldn't fit below.
  //  - X: keep the menu anchored to the trigger; only pull it left (right-
  //    aligned with the trigger) when its right edge would leave the viewport.
  useLayoutEffect(() => {
    if (!open) return
    const menu = menuRef.current
    const trigger = rootRef.current
    if (!menu || !trigger) return
    const GAP = 6
    const t = trigger.getBoundingClientRect()
    const m = menu.getBoundingClientRect()
    const menuH = m.height
    const menuW = m.width

    const next = {}
    if (m.right > window.innerWidth - GAP) {
      next.left = Math.max(GAP, t.right - menuW) // right-align with the trigger
    }
    const spaceBelow = window.innerHeight - t.bottom - GAP
    const spaceAbove = t.top - GAP
    if (menuH > spaceBelow && spaceAbove >= menuH) {
      next.top = undefined
      next.bottom = window.innerHeight - t.top + GAP
      next.up = true
    }
    if (Object.keys(next).length > 0) {
      setPos((p) => ({ ...p, ...next }))
    }
  }, [open])

  // Close on outside pointer-down or Escape while open.
  useEffect(() => {
    if (!open) return
    const onPointerDown = (e) => {
      if (menuRef.current?.contains(e.target)) return
      if (rootRef.current?.contains(e.target)) return
      close()
    }
    const onKeyDown = (e) => { if (e.key === 'Escape') close() }
    document.addEventListener('mousedown', onPointerDown)
    document.addEventListener('keydown', onKeyDown)
    return () => {
      document.removeEventListener('mousedown', onPointerDown)
      document.removeEventListener('keydown', onKeyDown)
    }
  }, [open, close])

  // The popup is viewport-fixed: close when the page scrolls/resizes (native
  // selects do the same). Ignore scroll events from inside the menu itself.
  useEffect(() => {
    if (!open) return
    const onScroll = (e) => {
      if (menuRef.current?.contains(e.target)) return
      close()
    }
    document.addEventListener('scroll', onScroll, true)
    window.addEventListener('resize', onScroll)
    return () => {
      document.removeEventListener('scroll', onScroll, true)
      window.removeEventListener('resize', onScroll)
    }
  }, [open, close])

  // Keep the highlighted option visible while keyboard-navigating a long list.
  useEffect(() => {
    if (!open || highlight < 0) return
    const list = menuRef.current
    const item = itemRefs.current[highlight]
    if (list && item) {
      const top = item.offsetTop
      const bottom = top + item.offsetHeight
      if (top < list.scrollTop) list.scrollTop = top
      else if (bottom > list.scrollTop + list.clientHeight) {
        list.scrollTop = bottom - list.clientHeight
      }
    }
  }, [open, highlight])

  const onTriggerKeyDown = (e) => {
    if (disabled || !hasOptions) return
    switch (e.key) {
      case 'ArrowDown':
      case 'ArrowUp': {
        e.preventDefault()
        if (!open) { openMenu(); return }
        const step = e.key === 'ArrowDown' ? 1 : -1
        setHighlight((h) => (h + step + options.length) % options.length)
        break
      }
      case 'Enter':
      case ' ': {
        e.preventDefault()
        if (!open) { openMenu(); return }
        const opt = options[highlight]
        if (opt != null) { onChange?.(opt.value); close() }
        break
      }
      case 'Escape':
        if (open) { e.preventDefault(); close() }
        break
      case 'Home':
        if (open) { e.preventDefault(); setHighlight(0) }
        break
      case 'End':
        if (open) { e.preventDefault(); setHighlight(options.length - 1) }
        break
      case 'Tab':
        close()
        break
    }
  }

  const dropdownCls = ['dropdown']
  if (size) dropdownCls.push(`dropdown--${size}`)
  if (className) dropdownCls.push(className)
  if (open) dropdownCls.push('is-open')

  return (
    <div className="dropdown-wrap" ref={rootRef}>
      <button
        type="button"
        {...rest}
        className={dropdownCls.join(' ')}
        disabled={disabled}
        aria-haspopup="listbox"
        aria-expanded={open}
        onClick={open ? close : openMenu}
        onKeyDown={onTriggerKeyDown}
      >
        <span className="dropdown__label">{selected ? selected.label : placeholder ?? ''}</span>
      </button>
      {open && createPortal(
        <div
          className={`dropdown-menu${pos?.up ? ' dropdown-menu--up' : ''}${isSm ? ' dropdown-menu--sm' : ''}`}
          role="listbox"
          aria-activedescendant={highlight >= 0 ? `${uid}-opt-${highlight}` : undefined}
          ref={menuRef}
          style={{ left: pos?.left, minWidth: pos?.minWidth, top: pos?.top, bottom: pos?.bottom }}
        >
          {hasOptions ? options.map((o, i) => (
            <div
              key={o.value}
              id={`${uid}-opt-${i}`}
              ref={(el) => { itemRefs.current[i] = el }}
              role="option"
              aria-selected={i === selectedIdx}
              className={`dropdown-menu__item${i === selectedIdx ? ' is-selected' : ''}${i === highlight ? ' is-highlight' : ''}`}
              onMouseDown={(e) => e.preventDefault()}
              onMouseEnter={() => setHighlight(i)}
              onClick={() => { onChange?.(o.value); close() }}
            >
              {o.label}
            </div>
          )) : (
            <div className="dropdown-menu__empty">无选项</div>
          )}
        </div>,
        document.body
      )}
    </div>
  )
}

// Range — three-layer slider:
//   1. .range-track — the visual rail (z 0, pointer-events none)
//   2. .range-mark  — the marker dot (z 1, pointer-events none, gray→accent)
//   3. <input>     — transparent input that handles drag + thumb (z 2)
// Also paints the elapsed-track fill (--fill) on the wrapper so the track
// gradient picks it up. Extras:
//   mark — a POINT on the track: number | { value, text }. The mark is a
//          small dot; its hint rides the native `title` on the input.
export function Range({ className, min = 0, max = 100, value, onChange, mark, ...rest }) {
  const ref = useRef(null)
  const wrapRef = useRef(null)
  // Paint --fill onto the wrapper (track gradient reads it).
  const paintFill = (v) => {
    const lo = Number(min), hi = Number(max)
    const pct = hi > lo ? ((Number(v) - lo) / (hi - lo)) * 100 : 0
    wrapRef.current?.style.setProperty('--fill', `${Math.max(0, Math.min(100, pct))}%`)
  }
  useEffect(() => { paintFill(value ?? min) }, [value, min, max])

  const pct = (v) => {
    const lo = Number(min), hi = Number(max)
    if (!(hi > lo)) return 0
    return Math.max(0, Math.min(100, ((Number(v) - lo) / (hi - lo)) * 100))
  }
  const current = Number(value ?? min)

  const markNum = mark != null ? (typeof mark === 'object' ? mark.value : mark) : null
  const markText = mark != null
    ? (typeof mark === 'object' && mark.text) || (markNum != null ? `当前值：${Math.round(markNum)}` : '')
    : null
  const markPct = markNum != null ? pct(markNum) : null

  // Position the mark at the thumb's linear offset along the track:
  //   left = f·100% + (6 − 12·f)px,  f = pct/100,  thumbW = 12px
  const onTrack = (p) => `calc(${p.toFixed(3)}% + ${(6 - 12 * p / 100).toFixed(3)}px)`

  return (
    <div ref={wrapRef} className={`range-wrap${className ? ` ${className}` : ''}`}>
      {/* Layer 1: visual track rail (behind everything) */}
      <div className="range-track" aria-hidden="true" />
      {/* Layer 2: marker dot */}
      {markPct != null && (
        <span
          className={`range-mark${current >= markNum ? ' is-crossed' : ''}`}
          style={{ left: onTrack(markPct) }}
          title={markText}
        />
      )}
      {/* Layer 3: custom thumb (purely visual, no interaction) */}
      <span className="range-thumb" style={{ left: onTrack(pct(current)) }} aria-hidden="true" />
      {/* Layer 4: transparent input (handles drag, native thumb hidden) */}
      <input
        ref={ref}
        className="range"
        type="range"
        min={min}
        max={max}
        value={value}
        title={`当前值：${Math.round(current)}`}
        onChange={(e) => { paintFill(e.currentTarget.value); onChange?.(e) }}
        {...rest}
      />
    </div>
  )
}

// Notice — one-line status message (error / success / warning).
export function Notice({ tone, children }) {
  return <p className={`notice${tone ? ` notice--${tone}` : ''}`}>{children}</p>
}

export function Modal({ open, title, onClose, children, actions, className }) {
  const [visible, setVisible] = useState(open)
  const [closing, setClosing] = useState(false)
  const prevOpen = useRef(open)
  const timerRef = useRef(null)
  // Whether the current press began on the overlay itself (a pure overlay
  // click) — used to avoid closing when a drag that started INSIDE the modal
  // (e.g. selecting text in an input) ends outside and the click lands on
  // the overlay.
  const overlayDownRef = useRef(false)

  useEffect(() => {
    // Cancel any pending close timer first (handles rapid open→close→open).
    clearTimeout(timerRef.current)
    timerRef.current = null

    if (open && !prevOpen.current) {
      // Opening: mount immediately, clear closing.
      setVisible(true)
      setClosing(false)
    } else if (!open && prevOpen.current) {
      // Closing: play exit animation, then unmount.
      setClosing(true)
      timerRef.current = setTimeout(() => {
        setVisible(false)
        setClosing(false)
        timerRef.current = null
      }, 200)
    }
    prevOpen.current = open

    return () => { clearTimeout(timerRef.current) }
  }, [open])

  // Suppress initial close animation: only animate after opened at least once.
  const wasOpened = useRef(false)
  if (open) wasOpened.current = true
  const showClosing = closing && wasOpened.current

  if (!visible) return null

  const overlayCls = `modal-overlay${showClosing ? ' modal-overlay--closing' : ''}`
  const modalCls   = `modal has-noise${showClosing ? ' modal--closing' : ''}${className ? ` ${className}` : ''}`

  return (
    <div
      className={overlayCls}
      onMouseDown={(e) => {
        overlayDownRef.current = e.target === e.currentTarget
      }}
      onClick={(e) => {
        if (overlayDownRef.current) onClose()
      }}
    >
      <div className={modalCls} onClick={(e) => e.stopPropagation()}>
        {title && <div className="modal__title">{title}</div>}
        <div className="modal__body">{children}</div>
        {actions && <div className="modal__actions">{actions}</div>}
      </div>
    </div>
  )
}

export function Chip({ tone, children }) {
  return <span className={`chip${tone ? ` chip--${tone}` : ''}`}>{children}</span>
}

// Pill — the shared small rounded badge, ONE component with BUTTON-style
// variants: `tone` (default = theme accent, "muted | success | danger |
// warning | info"). Same shell everywhere (see style.css .pill) so version
// chips / log-level chips / param-value chips all read identically, and the
// colour follows the tone. Renders a plain span so it can sit inline.
export function Pill({ tone, className, children, ...rest }) {
  const cls = ['pill']
  if (tone) cls.push(`pill--${tone}`)
  if (className) cls.push(className)
  return <span className={cls.join(' ')} {...rest}>{children}</span>
}

// SettingRow — the recurring "settings line" pattern (label + optional desc on
// the left, a control on the right). Used by every page's settings card and the
// plugin modals; keeps raw .setting-row markup out of callers.
export function SettingRow({ label, desc, control, className, children, style }) {
  return (
    <div className={`setting-row${className ? ` ${className}` : ''}`} style={style}>
      <div>
        {label && <div className="setting-row__label">{label}</div>}
        {desc && <div className="setting-row__desc">{desc}</div>}
      </div>
      {control && <div className="setting-row__control">{control}</div>}
      {children}
    </div>
  )
}

// ValueBadge — the little numeric readout beside sliders (threshold / blur / …).
export function ValueBadge({ children }) {
  return <span className="value-badge">{children}</span>
}
