// ui.jsx — small shared UI primitives used across pages. All styling is
// variables-driven (see style.css / theme.css); no hardcoded values here.
import { useRef, useState, useEffect, useLayoutEffect, useCallback, useId } from 'react'
import { createPortal } from 'react-dom'

// Button — wraps the .btn styles; variant maps to btn--{variant}
// (primary / ghost / danger / link), size to btn--{size} (sm).
export function Button({ variant, size, className, ...rest }) {
  const cls = ['btn']
  if (variant) cls.push(`btn--${variant}`)
  if (size) cls.push(`btn--${size}`)
  if (className) cls.push(className)
  return <button type="button" className={cls.join(' ')} {...rest} />
}

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

// Segmented — option bar; renderAction(value) may append a small element to
// each option (e.g. a delete badge); option.icon (a component) renders before
// the label. Keep it a span: a <button> cannot nest.
export function Segmented({ options, value, onChange, renderAction, className }) {
  return (
    <div className={`segmented ${className ? ` ${className}` : ''}`}>
      {options.map((o) => (
        <button
          key={o.value}
          type="button"
          className={o.value === value ? 'is-active' : ''}
          onClick={() => onChange(o.value)}
        >
          {o.icon}
          {o.label}
          {renderAction?.(o.value)}
        </button>
      ))}
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
export function Input({ className, type = 'text', ...rest }) {
  return (
    <input
      className={`input${className ? ` ${className}` : ''}`}
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
export function Select({ options, value, onChange, className, disabled, placeholder, ...rest }) {
  const [open, setOpen] = useState(false)
  const [highlight, setHighlight] = useState(-1)
  // Viewport-fixed position of the open menu: {left, minWidth, top|bottom, up}.
  const [pos, setPos] = useState(null)
  const rootRef = useRef(null)
  const menuRef = useRef(null)
  const itemRefs = useRef([])
  const uid = useId()

  const isSm = className?.split(/\s+/).includes('sm') ?? false

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
      // The menu is at least as wide as the trigger (never smaller), and only
      // grows beyond it when an option label is longer. A fixed floor (e.g.
      // 140 for the sm variant) made menus wider than their trigger whenever
      // the labels were short — the width should track the trigger instead.
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

  return (
    <div className="dropdown-wrap" ref={rootRef}>
      <button
        type="button"
        {...rest}
        className={`dropdown${className ? ` ${className}` : ''}${open ? ' is-open' : ''}`}
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

// Range — styled slider (.range). Also paints the elapsed-track fill: it
// writes the --fill CSS variable (0–100%) from value/min/max so the track
// gradient in style.css shows the accent up to the thumb. Controlled updates
// arrive via the effect; during a drag the change handler writes it straight
// onto the element so the fill never lags a frame behind the thumb.
export function Range({ className, min = 0, max = 100, value, onChange, ...rest }) {
  const ref = useRef(null)
  const paintFill = (el, v) => {
    const lo = Number(min), hi = Number(max)
    const pct = hi > lo ? ((Number(v) - lo) / (hi - lo)) * 100 : 0
    el.style.setProperty('--fill', `${Math.max(0, Math.min(100, pct))}%`)
  }
  useEffect(() => {
    if (ref.current) paintFill(ref.current, value ?? min)
  }, [value, min, max])
  return (
    <input
      ref={ref}
      className={`range${className ? ` ${className}` : ''}`}
      type="range"
      min={min}
      max={max}
      value={value}
      onChange={(e) => {
        paintFill(e.currentTarget, e.currentTarget.value)
        onChange?.(e)
      }}
      {...rest}
    />
  )
}

// Notice — one-line status message (error / success / warning).
export function Notice({ tone, children }) {
  return <p className={`notice${tone ? ` notice--${tone}` : ''}`}>{children}</p>
}

export function Modal({ open, title, onClose, children, actions }) {
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
  const modalCls   = `modal has-noise${showClosing ? ' modal--closing' : ''}`

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
