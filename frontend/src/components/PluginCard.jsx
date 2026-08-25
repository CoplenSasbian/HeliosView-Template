// PluginCard.jsx — one plugin tile in the grid: name / description / version
// in the header row, plus a compact overview of the CURRENT parameter values
// (the full editor lives in the parameter-config modal). Clicking the card
// opens that modal.
//
// The clickable .card wraps the plain .plugin content block; the .card carries
// the glass styling + entrance animation (staggered via `index`), while .plugin
// only holds the inner layout.
//
// Note: the overview intentionally excludes the per-config `_enabled` flag —
// it is not a declared parameter and was dropped from the tile.

import { IconFolder } from './icons'

// How many declared params to preview on the tile ("overview only").
const PREVIEW_MAX = 4

// One-line formatting for a parameter value in the overview.
function formatValue(info, value) {
  if (value === undefined || value === null) return '—'
  switch (info.type) {
    case 'bool':
      return value ? '开' : '关'
    case 'select': {
      const label = info.options?.[value]
      return label != null ? String(label) : String(value)
    }
    case 'datetime': {
      const d = new Date(Number(value) * 1000)
      return Number.isNaN(d.getTime())
        ? String(value)
        : d.toLocaleString('zh-CN', {
            month: '2-digit',
            day: '2-digit',
            hour: '2-digit',
            minute: '2-digit',
          })
    }
    default:
      return String(value)
  }
}

export default function PluginCard({ plugin, index = 0, values = {}, infos = [], onClick, onReveal }) {
  // _enabled is the per-config plugin on/off flag, not a declared parameter —
  // shown as a status dot next to the name, kept out of the param overview.
  const enabled = values._enabled
  const declared = (infos ?? []).filter((i) => i.name !== '_enabled')
  const shown = declared.slice(0, PREVIEW_MAX)
  const extra = declared.length - shown.length

  return (
    <div
      className="card card--plugin"
      style={{ '--card-i': index }}
      onClick={onClick}
      // Focusable + Enter/Space activation so keyboard users get the global
      // :focus-visible ring and can open the modal without a mouse.
      role="button"
      tabIndex={0}
      onKeyDown={(e) => {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault()
          onClick?.()
        }
      }}
    >
      <div className="plugin">
        <div className="plugin__head">
          <div className="plugin__name">
            <span
              className={`plugin__dot${enabled ? '' : ' is-off'}`}
              title={enabled ? '已启用' : '已停用'}
            />
            <span className="plugin__name-text">{plugin.name}</span>
          </div>
          {plugin.description && (
            <div className="plugin__desc" title={plugin.description}>
              {plugin.description}
            </div>
          )}
          <span className="plugin__head-actions">
            <span className="plugin__version">v{plugin.version}</span>
            <button
              type="button"
              className="plugin__reveal"
              title="在资源管理器中显示"
              onClick={(e) => {
                e.stopPropagation() // don't open the param modal
                onReveal?.()
              }}
            >
              <IconFolder width={13} height={13} />
            </button>
          </span>
        </div>

        {shown.length > 0 && (
          <div className="plugin__params">
            {shown.map((info) => {
              const text = formatValue(info, values[info.name])
              return (
                <div className="plugin__param" key={info.name}>
                  <span className="plugin__param-label">
                    <span className="plugin__param-name" title={info.label || info.name}>
                      {info.label || info.name}
                    </span>
                    {info.desc && (
                      <span className="plugin__param-desc" title={info.desc}>
                        {info.desc}
                      </span>
                    )}
                  </span>
                  <span className="plugin__param-value" title={text}>
                    {text}
                  </span>
                </div>
              )
            })}
            {extra > 0 && <div className="plugin__param-more">… 还有 {extra} 项</div>}
          </div>
        )}
      </div>
    </div>
  )
}
