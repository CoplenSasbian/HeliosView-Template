// ui.jsx — small shared UI primitives used across pages. All styling is
// variables-driven (see style.css / theme.css); no hardcoded values here.
import { useRef, useState, useEffect, useLayoutEffect, useCallback, useMemo, useId, forwardRef } from 'react'
import { createPortal } from 'react-dom'

// Button — wraps the .btn styles;
// variant: 'primary' | 'ghost' | 'outline' | 'subtle' | 'danger' | 'link'
// size: 'xs' | 'sm' | 'md' | 'lg'
// shape: 'default' | 'circle' | 'round'
// iconOnly: boolean (centers single icon)
export const Button = forwardRef(function Button(
  {
    children,
    variant,
    size,
    shape,
    iconOnly = false,
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
  const isIcon = iconOnly || (children == null && (icon != null || iconRight != null))
  const cls = ['btn']
  if (variant) cls.push(`btn--${variant}`)
  if (size) cls.push(`btn--${size}`)
  if (shape) cls.push(`btn--${shape}`)
  if (isIcon) cls.push('btn--icon')
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

// IconButton — specialized compact wrapper around Button with shape="circle" or "round"
export const IconButton = forwardRef(function IconButton(
  { icon, size = 'sm', variant = 'ghost', shape = 'circle', ...rest },
  ref
) {
  return (
    <Button
      ref={ref}
      variant={variant}
      size={size}
      shape={shape}
      iconOnly
      icon={icon}
      {...rest}
    />
  )
})

export function Toggle({
  on,
  onChange,
  size,
  className,
  title,
  disabled,
  checkedChildren,
  unCheckedChildren,
}) {
  const cls = ['toggle']
  if (size) cls.push(`toggle--${size}`)
  if (on) cls.push('is-on')
  if (disabled) cls.push('is-disabled')
  if (className) cls.push(className)

  return (
    <button
      type="button"
      className={cls.join(' ')}
      role="switch"
      aria-checked={on}
      disabled={disabled}
      title={title}
      onClick={() => {
        if (!disabled) onChange?.(!on)
      }}
    >
      <span className="toggle__knob" />
      {(checkedChildren != null || unCheckedChildren != null) && (
        <span className="toggle__inner">
          {on ? checkedChildren : unCheckedChildren}
        </span>
      )}
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

// Input — styled text/number input (.input) with optional prefix / suffix / clear button / status.
export function Input({
  className,
  size,
  type = 'text',
  prefix,
  suffix,
  status, // 'error' | 'warning'
  allowClear = false,
  value,
  onChange,
  onClear,
  style,
  disabled,
  ...rest
}) {
  const cls = ['input']
  if (size) cls.push(`input--${size}`)
  if (status === 'error') cls.push('is-error')
  if (className) cls.push(className)

  const showClear = allowClear && !disabled && value != null && value !== ''

  if (!prefix && !suffix && !allowClear) {
    return (
      <input
        className={cls.join(' ')}
        type={type}
        value={value}
        disabled={disabled}
        onChange={onChange}
        style={style}
        {...rest}
      />
    )
  }

  const wrapCls = ['input-affix-wrap']
  if (size) wrapCls.push(`input-affix-wrap--${size}`)
  if (prefix) wrapCls.push('has-prefix')
  if (suffix) wrapCls.push('has-suffix')
  if (showClear) wrapCls.push('has-clear')
  if (className) wrapCls.push(className)

  return (
    <div className={wrapCls.join(' ')} style={style}>
      {prefix && <span className="input__prefix">{prefix}</span>}
      <input
        className={cls.filter((c) => c !== className).join(' ')}
        type={type}
        value={value}
        disabled={disabled}
        onChange={onChange}
        {...rest}
      />
      {showClear && (
        <button
          type="button"
          className="input__clear"
          title="清空"
          onClick={(e) => {
            e.stopPropagation()
            if (onClear) onClear()
            else onChange?.({ target: { value: '' } })
          }}
        >
          ×
        </button>
      )}
      {suffix && <span className="input__suffix">{suffix}</span>}
    </div>
  )
}

// InputNumber — Number input with stepper buttons and bound min/max
export function InputNumber({
  value,
  onChange,
  min,
  max,
  step = 1,
  size,
  disabled,
  className,
  style,
  ...rest
}) {
  const numValue = value === '' || value == null ? '' : Number(value)

  const stepUp = () => {
    if (disabled) return
    const cur = typeof numValue === 'number' && !Number.isNaN(numValue) ? numValue : (min ?? 0)
    const next = cur + step
    if (max !== undefined && next > max) return
    onChange?.(next)
  }

  const stepDown = () => {
    if (disabled) return
    const cur = typeof numValue === 'number' && !Number.isNaN(numValue) ? numValue : (min ?? 0)
    const next = cur - step
    if (min !== undefined && next < min) return
    onChange?.(next)
  }

  return (
    <div className={`input-affix-wrap has-stepper${className ? ` ${className}` : ''}`} style={style}>
      <Input
        type="number"
        size={size}
        disabled={disabled}
        value={value ?? ''}
        min={min}
        max={max}
        step={step}
        onChange={(e) => {
          const val = e.target.value
          onChange?.(val === '' ? '' : Number(val))
        }}
        {...rest}
      />
      <div className="input-number-stepper">
        <button type="button" tabIndex={-1} disabled={disabled || (max !== undefined && numValue >= max)} onClick={stepUp}>▲</button>
        <button type="button" tabIndex={-1} disabled={disabled || (min !== undefined && numValue <= min)} onClick={stepDown}>▼</button>
      </div>
    </div>
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

// Select — custom JS dropdown with search & icons support
export function Select({
  options = [],
  value,
  onChange,
  className,
  size,
  disabled,
  placeholder,
  showSearch = false,
  status,
  ...rest
}) {
  const [open, setOpen] = useState(false)
  const [highlight, setHighlight] = useState(-1)
  const [query, setQuery] = useState('')
  const [pos, setPos] = useState(null)
  const rootRef = useRef(null)
  const menuRef = useRef(null)
  const searchInputRef = useRef(null)
  const itemRefs = useRef([])
  const uid = useId()

  const isSm = size === 'sm' || (className?.split(/\s+/).includes('sm') ?? false)
  const isXs = size === 'xs' || (className?.split(/\s+/).includes('xs') ?? false)
  const isLg = size === 'lg' || (className?.split(/\s+/).includes('lg') ?? false)

  const strValue = value != null ? String(value) : ''
  const selectedIdx = options?.findIndex((o) => String(o.value) === strValue) ?? -1
  const selected = selectedIdx >= 0 ? options[selectedIdx] : null

  const filteredOptions = useMemo(() => {
    if (!showSearch || !query.trim()) return options || []
    const q = query.trim().toLowerCase()
    return (options || []).filter((o) => String(o.label || o.value).toLowerCase().includes(q))
  }, [options, showSearch, query])

  const hasOptions = filteredOptions.length > 0

  const close = useCallback(() => {
    setOpen(false)
    setHighlight(-1)
    setQuery('')
  }, [])

  const openMenu = useCallback(() => {
    if (disabled) return
    const rect = rootRef.current?.getBoundingClientRect()
    if (!rect) return
    const GAP = 6
    setPos({
      left: rect.left,
      minWidth: rect.width,
      top: rect.bottom + GAP,
      bottom: undefined,
      up: false,
    })
    setHighlight(selectedIdx >= 0 ? selectedIdx : 0)
    setOpen(true)
    if (showSearch) {
      setTimeout(() => searchInputRef.current?.focus(), 50)
    }
  }, [disabled, selectedIdx, showSearch])

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
      next.left = Math.max(GAP, t.right - menuW)
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

  const onTriggerKeyDown = (e) => {
    if (disabled) return
    if (!open) {
      if (e.key === 'Enter' || e.key === ' ' || e.key === 'ArrowDown') {
        e.preventDefault()
        openMenu()
      }
      return
    }
    switch (e.key) {
      case 'ArrowDown':
        e.preventDefault()
        setHighlight((prev) => (prev < filteredOptions.length - 1 ? prev + 1 : 0))
        break
      case 'ArrowUp':
        e.preventDefault()
        setHighlight((prev) => (prev > 0 ? prev - 1 : filteredOptions.length - 1))
        break
      case 'Enter':
      case ' ':
        e.preventDefault()
        if (highlight >= 0 && highlight < filteredOptions.length) {
          onChange?.(filteredOptions[highlight].value)
          close()
        }
        break
      case 'Tab':
        close()
        break
    }
  }

  const dropdownCls = ['dropdown']
  if (size) dropdownCls.push(`dropdown--${size}`)
  if (status === 'error') dropdownCls.push('is-error')
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
        <span className="dropdown__label" style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
          {selected?.icon}
          {selected ? selected.label : placeholder ?? ''}
        </span>
      </button>
      {open && createPortal(
        <div
          className={`dropdown-menu${pos?.up ? ' dropdown-menu--up' : ''}${isSm ? ' dropdown-menu--sm' : ''}`}
          role="listbox"
          aria-activedescendant={highlight >= 0 ? `${uid}-opt-${highlight}` : undefined}
          ref={menuRef}
          style={{ left: pos?.left, minWidth: pos?.minWidth, top: pos?.top, bottom: pos?.bottom }}
        >
          {showSearch && (
            <div className="dropdown-search-wrap" onClick={(e) => e.stopPropagation()}>
              <Input
                ref={searchInputRef}
                size="xs"
                placeholder="搜索选项..."
                value={query}
                onChange={(e) => {
                  setQuery(e.target.value)
                  setHighlight(0)
                }}
              />
            </div>
          )}
          {hasOptions ? filteredOptions.map((o, i) => (
            <div
              key={o.value}
              id={`${uid}-opt-${i}`}
              ref={(el) => { itemRefs.current[i] = el }}
              role="option"
              aria-selected={String(o.value) === strValue}
              className={`dropdown-menu__item${String(o.value) === strValue ? ' is-selected' : ''}${i === highlight ? ' is-highlight' : ''}`}
              style={{ display: 'flex', alignItems: 'center', gap: 6 }}
              onMouseDown={(e) => e.preventDefault()}
              onMouseEnter={() => setHighlight(i)}
              onClick={() => { onChange?.(o.value); close() }}
            >
              {o.icon}
              <span style={{ flex: 1, overflow: 'hidden', textOverflow: 'ellipsis' }}>{o.label}</span>
            </div>
          )) : (
            <div className="dropdown-menu__empty">无匹配选项</div>
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

// Modal — glass modal dialog with sizes and Escape key support
export function Modal({
  open,
  title,
  onClose,
  children,
  actions,
  size = 'md', // 'sm' | 'md' | 'lg' | 'xl' | 'fullscreen'
  className,
}) {
  const [visible, setVisible] = useState(open)
  const [closing, setClosing] = useState(false)
  const prevOpen = useRef(open)
  const timerRef = useRef(null)
  const overlayDownRef = useRef(false)

  useEffect(() => {
    clearTimeout(timerRef.current)
    timerRef.current = null

    if (open && !prevOpen.current) {
      setVisible(true)
      setClosing(false)
    } else if (!open && prevOpen.current) {
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

  // Escape key to close modal
  useEffect(() => {
    if (!open) return
    const onKeyDown = (e) => {
      if (e.key === 'Escape') onClose?.()
    }
    document.addEventListener('keydown', onKeyDown)
    return () => document.removeEventListener('keydown', onKeyDown)
  }, [open, onClose])

  const wasOpened = useRef(false)
  if (open) wasOpened.current = true
  const showClosing = closing && wasOpened.current

  if (!visible) return null

  const overlayCls = `modal-overlay${showClosing ? ' modal-overlay--closing' : ''}`
  const modalCls = [
    'modal',
    'has-noise',
    size && size !== 'md' ? `modal--${size}` : '',
    showClosing ? 'modal--closing' : '',
    className || '',
  ].filter(Boolean).join(' ')

  return (
    <div
      className={overlayCls}
      onMouseDown={(e) => {
        overlayDownRef.current = e.target === e.currentTarget
      }}
      onClick={(e) => {
        if (overlayDownRef.current) onClose?.()
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

// Collapse / Accordion Component
// items: [{ key, title, extra, children, disabled }]
// activeKey: string | string[] (controlled)
// defaultActiveKey: string | string[]
// accordion: boolean (only one item expanded at a time)
export function Collapse({
  items = [],
  activeKey,
  defaultActiveKey,
  onChange,
  accordion = false,
  className,
}) {
  const [internalKeys, setInternalKeys] = useState(() => {
    if (activeKey !== undefined) return Array.isArray(activeKey) ? activeKey : [activeKey]
    if (defaultActiveKey !== undefined) return Array.isArray(defaultActiveKey) ? defaultActiveKey : [defaultActiveKey]
    return []
  })

  const currentKeys = activeKey !== undefined ? (Array.isArray(activeKey) ? activeKey : [activeKey]) : internalKeys

  const toggle = (key) => {
    let nextKeys = []
    if (accordion) {
      nextKeys = currentKeys.includes(key) ? [] : [key]
    } else {
      nextKeys = currentKeys.includes(key) ? currentKeys.filter((k) => k !== key) : [...currentKeys, key]
    }
    setInternalKeys(nextKeys)
    onChange?.(accordion ? (nextKeys[0] ?? null) : nextKeys)
  }

  return (
    <div className={`collapse${className ? ` ${className}` : ''}`}>
      {items.map((item, i) => {
        const key = item.key ?? String(i)
        const isOpen = currentKeys.includes(key)
        return (
          <div key={key} className={`collapse-item${isOpen ? ' is-open' : ''}${item.disabled ? ' is-disabled' : ''}`}>
            <div
              className="collapse-item__header"
              onClick={() => {
                if (!item.disabled) toggle(key)
              }}
            >
              <div className="collapse-item__title">
                <span className="collapse-item__arrow">›</span>
                {item.title}
              </div>
              {item.extra && <div className="collapse-item__extra">{item.extra}</div>}
            </div>
            <div className="collapse-item__content">
              <div className="collapse-item__inner">{item.children}</div>
            </div>
          </div>
        )
      })}
    </div>
  )
}

// DataTable — reusable declarative data table.
// columns: [{ key, title, width, flex, align, render: (row, i) => ReactNode }]
// data: array of row items
// rowKey: string | (row, i) => string/number
// onRowClick: (row, i) => void
// rowClassName: string | (row, i) => string
export function DataTable({
  columns = [],
  data = [],
  rowKey = 'id',
  onRowClick,
  rowClassName,
  emptyText = '暂无数据',
  className,
  showHeader = true,
}) {
  const getKey = (row, i) => {
    if (typeof rowKey === 'function') return rowKey(row, i)
    return row[rowKey] ?? i
  }

  const getRowCls = (row, i) => {
    const list = ['data-table-row']
    if (onRowClick) list.push('is-clickable')
    if (typeof rowClassName === 'function') {
      const extra = rowClassName(row, i)
      if (extra) list.push(extra)
    } else if (rowClassName) {
      list.push(rowClassName)
    }
    return list.join(' ')
  }

  return (
    <div className={`data-table-wrap${className ? ` ${className}` : ''}`}>
      {showHeader && columns.length > 0 && (
        <div className="data-table-header">
          {columns.map((col) => {
            const style = {
              flex: col.flex || (col.width ? `0 0 ${typeof col.width === 'number' ? `${col.width}px` : col.width}` : '1 1 0'),
              textAlign: col.align || 'left',
              justifyContent: col.align === 'center' ? 'center' : col.align === 'right' ? 'flex-end' : 'flex-start',
            }
            return (
              <div key={col.key || col.title} className="data-table-header__cell" style={style}>
                {col.title}
              </div>
            )
          })}
        </div>
      )}

      <div className="data-table-body">
        {data.length > 0 ? (
          data.map((row, i) => {
            const key = getKey(row, i)
            return (
              <div
                key={key}
                className={getRowCls(row, i)}
                style={{ '--row-i': i }}
                onClick={() => onRowClick?.(row, i)}
                role={onRowClick ? 'button' : undefined}
                tabIndex={onRowClick ? 0 : undefined}
                onKeyDown={(e) => {
                  if (onRowClick && (e.key === 'Enter' || e.key === ' ')) {
                    e.preventDefault()
                    onRowClick(row, i)
                  }
                }}
              >
                {columns.map((col) => {
                  const style = {
                    flex: col.flex || (col.width ? `0 0 ${typeof col.width === 'number' ? `${col.width}px` : col.width}` : '1 1 0'),
                    justifyContent: col.align === 'center' ? 'center' : col.align === 'right' ? 'flex-end' : 'flex-start',
                  }
                  return (
                    <div key={col.key || col.title} className="data-table-cell" style={style}>
                      {col.render ? col.render(row, i) : row[col.key]}
                    </div>
                  )
                })}
              </div>
            )
          })
        ) : (
          <div className="data-table-empty">{emptyText}</div>
        )}
      </div>
    </div>
  )
}
