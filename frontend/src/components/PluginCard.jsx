// PluginCard.jsx — one plugin ROW in the plugins-page list: a status dot + name
// + short description in the head, the CURRENT parameter values as compact
// chips underneath, and version + action buttons pinned to the right.
//
// The row lives inside a single Card on the plugins page — no nested card-in-
// card — so the page reads as one continuous surface instead of a grid of
// boxes. Clicking the row opens the plugin DETAIL dialog; the gear opens the
// parameter-config modal; the head action area stops propagation so those
// clicks never open the detail dialog.
//
// Note: the overview intentionally excludes the per-config `_enabled` flag —
// it is not a declared parameter and is shown only as the status dot.

import { IconFolder, IconSettings } from './icons'
import { Pill, Toggle } from './ui'

// How many declared params to show as chips in the overview ("+n" for the rest).
const PARAMS_SHOWN = 4

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

export default function PluginCard({
  plugin,
  index = 0,
  mode = 'grid',
  values = {},
  infos = [],
  onClick,
  onReveal,
  onConfig,
  onToggle,
}) {
  const enabled = values._enabled ?? true
  const declared = (infos ?? []).filter((i) => i.name !== '_enabled')
  const shown = declared.slice(0, mode === 'grid' ? 6 : PARAMS_SHOWN)
  const extra = declared.length - shown.length

  const handleKeyDown = (e) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault()
      onClick?.()
    }
  }

  if (mode === 'grid') {
    return (
      <div
        className={`plugin-card-item${enabled ? '' : ' is-disabled'}`}
        role="button"
        tabIndex={0}
        style={{ '--item-i': index }}
        onClick={onClick}
        onKeyDown={handleKeyDown}
      >
        <div className="plugin-card-item__header">
          <div className="plugin-card-item__titles">
            <div className="plugin-card-item__title-row">
              <span className="plugin-card-item__name" title={plugin.name}>
                {plugin.name}
              </span>
              <Pill tone="muted">v{plugin.version}</Pill>
            </div>
            {plugin.author && (
              <span className="plugin-card-item__author" title={`作者: ${plugin.author}`}>
                {plugin.author}
              </span>
            )}
          </div>

          <div
            className="plugin-card-item__toggle"
            onClick={(e) => e.stopPropagation()}
            onKeyDown={(e) => e.stopPropagation()}
          >
            <Toggle
              on={enabled}
              title={enabled ? '点击停用插件' : '点击启用插件'}
              onChange={(val) => onToggle?.(val)}
            />
          </div>
        </div>

        <div className="plugin-card-item__body">
          <p className="plugin-card-item__desc" title={plugin.description || '暂无插件描述'}>
            {plugin.description || '暂无描述信息'}
          </p>
        </div>

        {declared.length > 0 && (
          <div className="plugin-card-item__params">
            {shown.map((info) => {
              const label = info.label || info.name
              const text = formatValue(info, values[info.name])
              const tip = `${label}: ${text}${info.desc ? ` — ${info.desc}` : ''}`
              return (
                <Pill key={info.name} tone="muted" title={tip}>
                  <span className="pill__key">{label}</span>
                  <span className="pill__val">{text}</span>
                </Pill>
              )
            })}
            {extra > 0 && <span className="plugin-row__param-more">+{extra}</span>}
          </div>
        )}

        <div
          className="plugin-card-item__footer"
          onClick={(e) => e.stopPropagation()}
          onKeyDown={(e) => e.stopPropagation()}
        >
          <span className={`plugin-card-item__status-tag${enabled ? ' is-on' : ' is-off'}`}>
            {enabled ? '已启用' : '已停用'}
          </span>
          <div className="plugin-card-item__actions">
            <button
              type="button"
              className="icon-btn"
              title="参数配置"
              onClick={(e) => {
                e.stopPropagation()
                onConfig?.()
              }}
            >
              <IconSettings width={14} height={14} />
            </button>
            <button
              type="button"
              className="icon-btn"
              title="在资源管理器中显示"
              onClick={(e) => {
                e.stopPropagation()
                onReveal?.()
              }}
            >
              <IconFolder width={14} height={14} />
            </button>
          </div>
        </div>
      </div>
    )
  }

  // mode === 'list' (Dense Table / Grid Row)
  return (
    <div
      className={`plugin-row${enabled ? '' : ' is-disabled'}`}
      role="button"
      tabIndex={0}
      style={{ '--row-i': index }}
      onClick={onClick}
      onKeyDown={handleKeyDown}
    >
      {/* Col 1: Identity */}
      <div className="plugin-row__identity">
        <div className="plugin-row__name-wrap">
          <span className="plugin-row__name" title={plugin.name}>{plugin.name}</span>
          <span className="plugin-row__ver">v{plugin.version}</span>
        </div>
      </div>

      {/* Col 2: Description */}
      <span className="plugin-row__desc" title={plugin.description || '无详细描述'}>
        {plugin.description || '—'}
      </span>

      {/* Col 3: Parameters Preview */}
      <div className="plugin-row__params">
        {shown.length > 0 ? (
          <>
            {shown.map((info) => {
              const label = info.label || info.name
              const text = formatValue(info, values[info.name])
              const tip = `${label}: ${text}${info.desc ? ` — ${info.desc}` : ''}`
              return (
                <Pill key={info.name} tone="muted" title={tip}>
                  <span className="pill__key">{label}</span>
                  <span className="pill__val">{text}</span>
                </Pill>
              )
            })}
            {extra > 0 && <span className="plugin-row__param-more">+{extra}</span>}
          </>
        ) : (
          <span className="plugin-row__params-empty">无参数</span>
        )}
      </div>

      {/* Col 4: Status Switch Toggle */}
      <div
        className="plugin-row__status"
        onClick={(e) => e.stopPropagation()}
        onKeyDown={(e) => e.stopPropagation()}
      >
        <Toggle
          on={enabled}
          title={enabled ? '点击停用插件' : '点击启用插件'}
          onChange={(val) => onToggle?.(val)}
        />
      </div>

      {/* Col 5: Actions */}
      <div
        className="plugin-row__actions"
        onClick={(e) => e.stopPropagation()}
        onKeyDown={(e) => e.stopPropagation()}
      >
        <button
          type="button"
          className="icon-btn"
          title="参数配置"
          onClick={(e) => {
            e.stopPropagation()
            onConfig?.()
          }}
        >
          <IconSettings width={14} height={14} />
        </button>
        <button
          type="button"
          className="icon-btn"
          title="在资源管理器中显示"
          onClick={(e) => {
            e.stopPropagation()
            onReveal?.()
          }}
        >
          <IconFolder width={14} height={14} />
        </button>
      </div>
    </div>
  )
}