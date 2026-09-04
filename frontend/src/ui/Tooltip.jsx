// Tooltip.jsx — a reusable label-on-hover bubble.
//
// Usage (text empty/omitted → no bubble, no extra DOM):
//   <Tooltip text="应用设置">
//     <Icon/>  ‹ cursor it (any element) ›
//   </Tooltip>
//
// You can also pass a `side` (default "right": "right" | "left" | "top" | "bottom")
// to flip where the bubble sits. Styling lives in style.css (.tooltip) and uses the
// theme variables, so it works anywhere — not just the collapsed sidebar.
import { useRef, useState } from 'react'

export default function Tooltip({ text, side = 'right', children }) {
  const [open, setOpen] = useState(false)
  const triggerRef = useRef(null)

  const show = () => setOpen(true)
  const hide = () => setOpen(false)

  if (!text) return children // nothing to say: render the child bare

  return (
    <span
      ref={triggerRef}
      className="tooltip"
      data-side={side}
      onMouseEnter={show}
      onMouseLeave={hide}
      onFocus={show}
      onBlur={hide}
    >
      {children}
      <span
        className={`tooltip__bubble${open ? ' is-open' : ''}`}
        role="tooltip"
      >
        {text}
      </span>
    </span>
  )
}
