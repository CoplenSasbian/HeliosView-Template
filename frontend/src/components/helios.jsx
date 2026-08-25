// helios.jsx — React wrappers for the window chrome that HeliosView injects.
//
// <helios-window-title-bar> and <helios-window-controls> are NOT part of this
// bundle: they are defined at runtime by the bridge script HeliosView injects
// into every document (HeliosView/src/win32/webview_bridge.js — embedded into
// HeliosView.dll and installed via AddScriptToExecuteOnDocumentCreated).
//
// Why wrappers? React 18 renders custom elements by copying each prop to the
// DOM as an attribute VERBATIM: the className→class mapping that built-in tags
// get is skipped for hyphenated tags (see setValueForProperty in react-dom —
// `isCustomComponentTag || propertyInfo === null` → setAttribute(name, value)
// with the raw prop name). So writing
//
//     <helios-window-title-bar className="header">
//
// would emit a literal `className="header"` attribute and CSS `.header` would
// never match. These wrappers translate className → class and forward every
// other prop (style, events, children, …) unchanged, so callers can use
// ordinary React props.

import { forwardRef } from 'react'

// Custom elements receive props as attributes with the prop name kept as-is;
// translate the one React-specific spelling React itself won't.
function toElementProps({ className, ...rest }) {
  if (className == null) return rest
  return { ...rest, class: className }
}

export const HeliosTitleBar = forwardRef(function HeliosTitleBar(props, ref) {
  return <helios-window-title-bar ref={ref} {...toElementProps(props)} />
})

export const HeliosWindowControls = forwardRef(function HeliosWindowControls(props, ref) {
  return <helios-window-controls ref={ref} {...toElementProps(props)} />
})
