// LogConsole.jsx — scrollable log viewer. Pure rendering: the filter state
// (time range / tag / level) comes in via the `filters` prop, shared with the
// LogFilters bar in the card title row.
//
// Each line collapses to a single ellipsized row; clicking the "›" chevron
// expands it into a labeled meta row (level / time / tag) with the message
// wrapping underneath.

import { useEffect, useMemo, useRef, useState } from 'react'
import { useLogger } from '../context/LoggerContext.jsx'
import { Pill } from '../ui'

function getLevelClass(level) {
  const s = String(level).toLowerCase()
  if (s.includes('warn')) return 'is-warn'
  if (s.includes('err')) return 'is-error'
  return 'is-info'
}

// Level → Pill tone (info logs read as success-tinted, warn→warning, error→danger).
function getLevelTone(level) {
  const s = String(level).toLowerCase()
  if (s.includes('warn')) return 'warning'
  if (s.includes('err')) return 'danger'
  return 'success'
}

// Display formatting only — the stored log keeps the full timestamp from the
// native logger. We always render from the numeric epoch timestamp so the
// display stays readable (date + time) no matter how the native display string
// looks; milliseconds are carried over from it when present.
function formatLogTime(log) {
  const d = log.timestamp ? new Date(log.timestamp * 1000) : null
  if (d && !Number.isNaN(d.getTime())) {
    const pad = (n) => String(n).padStart(2, '0')
    let ms = '000'
    const m = /\.(\d{1,3})/.exec(log.time ?? '')
    if (m) ms = m[1].padEnd(3, '0')
    return (
      `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ` +
      `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${ms}`
    )
  }
  return log.time ?? '--'
}

// Relative age label shown next to the absolute time: "刚刚 / N 分钟前 /
// N 小时前 / 昨天". Entries without a numeric timestamp (e.g. posted from JS)
// or older than a day return null — the caller then shows only the absolute
// time. `now` is passed in so the label stays consistent within one render.
function formatRelativeTime(log, now) {
  const t = log.timestamp ? log.timestamp * 1000 : NaN
  if (Number.isNaN(t)) return null
  const diff = now - t
  if (diff < 60_000) return '刚刚'
  if (diff < 3_600_000) return `${Math.floor(diff / 60_000)} 分钟前`
  if (diff < 86_400_000) return `${Math.floor(diff / 3_600_000)} 小时前`
  if (diff < 172_800_000) return '昨天'
  return null
}

export default function LogConsole({ filters }) {
  const { logList } = useLogger()
  const scrollRef = useRef(null)
  // Keys of the expanded lines (timestamp + index; index keeps same-second
  // entries distinct, the timestamp survives the 300-entry ring buffer).
  const [openKeys, setOpenKeys] = useState(() => new Set())
  // Re-render every 30s so relative-time labels ("3 分钟前") stay fresh while
  // a line sits open.
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), 30_000)
    return () => clearInterval(id)
  }, [])

  const toggle = (key) => {
    setOpenKeys((prev) => {
      const next = new Set(prev)
      if (next.has(key)) next.delete(key)
      else next.add(key)
      return next
    })
  }

  const filtered = useMemo(() => {
    const now = Date.now()

    return logList.filter((log) => {
      if (filters.timeRange > 0) {
        // Compare against the numeric epoch timestamp sent by the native
        // logger. Logs without one (e.g. posted from JS) are kept — there is
        // no timestamp to compare, so the range cannot filter them.
        const t = log.timestamp ? log.timestamp * 1000 : NaN
        if (!Number.isNaN(t) && now - t > filters.timeRange * 1000) return false
      }
      if (filters.tag && log.tag !== filters.tag) return false
      if (filters.level && log.level?.toLowerCase() !== filters.level.toLowerCase())
        return false
      return true
    })

  }, [logList, filters])

  // Follow the console tail: scroll to the bottom on mount, when the log
  // history seeds in (list grows from 0 → N), and for every new batch — as
  // long as the user hasn't scrolled up (further than 80px from the bottom).
  // Scrolling up manually stops the following until the user returns near the
  // bottom; a cleared/empty console resumes following.
  const stickToBottom = useRef(true)
  useEffect(() => {
    const el = scrollRef.current
    if (!el) return
    if (stickToBottom.current || filtered.length === 0) {
      if (filtered.length === 0) stickToBottom.current = true
      el.scrollTop = el.scrollHeight
    }
  }, [filtered])

  const handleScroll = () => {
    const el = scrollRef.current
    if (!el) return
    stickToBottom.current = el.scrollHeight - el.scrollTop - el.clientHeight < 80
  }

  return (
    <div className="log-console" ref={scrollRef} onScroll={handleScroll}>
      {filtered.map((log, i) => {
        const key = `${log.timestamp ?? 'js'}-${i}`
        const isOpen = openKeys.has(key)
        const timeLabel = formatLogTime(log)
        const relLabel = formatRelativeTime(log, now)
        return (
          <div
            key={key}
            className={`log-line ${getLevelClass(log.level)}${isOpen ? ' is-open' : ''}`}
          >
            <div className="log-line__row">
              <span
                className="log-line__chevron"
                role="button"
                title={isOpen ? '收起详情' : '展开详情'}
                onClick={() => toggle(key)}
              >
                ›
              </span>
              <span
                className="log-line__text"
                title={isOpen ? undefined : log.message ?? undefined}
                onClick={() => toggle(key)}
                style={{ cursor: 'pointer' }}
              >
                {log.message ?? ''}
              </span>
            </div>

            <div className="log-line__drawer">
              <div className="log-line__drawer-inner">
                <div className="log-line__detail">
                  <div className="log-line__meta">
                    <Pill tone={getLevelTone(log.level)}>{log.level ?? '--'}</Pill>
                    <span className="log-line__tag-badge">#{log.tag ?? '--'}</span>
                    <span className="log-line__time" title={timeLabel}>
                      {relLabel && <span className="log-line__time-rel">{relLabel} · </span>}
                      {timeLabel}
                    </span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        )
      })}
      {!filtered.length && <div className="log-line">暂无日志</div>}
    </div>
  )
}
