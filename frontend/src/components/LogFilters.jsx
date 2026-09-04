// LogFilters.jsx — compact log filter bar (time range / tag / level), sized
// for the card title row. Controlled: value/onChange live in the parent so
// LogConsole can share the same filter state.

import { useMemo } from 'react'
import { useLogger } from '../context/LoggerContext.jsx'
import { Select } from '../ui'

// Preset time windows (seconds); 0 = show everything.
const TIME_RANGES = [
  { value: 0, label: '全部' },
  { value: 60, label: '1 分钟' },
  { value: 300, label: '5 分钟' },
  { value: 1800, label: '30 分钟' },
  { value: 3600, label: '1 小时' },
]

const LEVELS = ['Info', 'Warning', 'Error']

export default function LogFilters({ value, onChange }) {
  const { logList } = useLogger()

  // All distinct tags seen so far, for the tag dropdown.
  const tags = useMemo(
    () => [...new Set(logList.map((l) => l.tag).filter(Boolean))],
    [logList]
  )

  const tagOptions = [
    { value: '', label: '全部 tag' },
    ...tags.map((t) => ({ value: t, label: t })),
  ]

  const levelOptions = [
    { value: '', label: '全部级别' },
    ...LEVELS.map((l) => ({ value: l, label: l })),
  ]

  return (
    <div className="log-filters">
      <Select
        className="sm"
        title="时间范围"
        options={TIME_RANGES}
        value={value.timeRange}
        onChange={(v) => onChange({ ...value, timeRange: Number(v) })}
      />
      <Select
        className="sm"
        title="按 tag 筛选"
        options={tagOptions}
        value={value.tag}
        onChange={(v) => onChange({ ...value, tag: v })}
      />
      <Select
        className="sm"
        title="按级别筛选"
        options={levelOptions}
        value={value.level}
        onChange={(v) => onChange({ ...value, level: v })}
      />
    </div>
  )
}
