// ParamControl.jsx — renders one plugin parameter according to its declared
// type (bool / int / double / string / file / folder / datetime). Used inside
// the parameter-config modal; also exports normalizeValue(), the save-time
// counterpart that maps a UI value back to the JSON type the C++ side expects.

import { Toggle, Button, Field, Input, Range, Select } from './ui'
import { call } from '../bridge'

// Normalize a value to the JSON type expected by the C++ side, so that
// as_int64()/as_double()/as_bool() never throw on the wrong kind.
export function normalizeValue(value, type) {
  switch (type) {
    case 'int':
    case 'datetime':
    case 'select':
      return Math.trunc(Number(value))
    case 'double':
      return Number(value)
    case 'bool':
      return Boolean(value)
    case 'string':
    case 'file':
    case 'folder':
      return String(value)
    default:
      return value
  }
}

export default function ParamControl({ param, value, onChange }) {
  switch (param.type) {
    case 'bool':
      return (
        <div className="setting-row">
          <div>
            <div className="setting-row__label">{param.label || param.desc || param.name}</div>
          </div>
          <Toggle on={!!value} onChange={onChange} />
        </div>
      )
    case 'int':
    case 'double': {
      // A slider only makes sense when the plugin declared a real range
      // (min < max). Undeclared ranges come through as 0/0 (zero-init struct),
      // which would clamp the value into a dead 0..0 track — fall back to a
      // plain number input so any value can be typed.
      const hasRange =
        param.min != null && param.max != null &&
        Number.isFinite(Number(param.min)) && Number.isFinite(Number(param.max)) &&
        Number(param.min) < Number(param.max)
      const label = param.label || param.desc || param.name
      const commit = (raw) =>
        onChange(param.type === 'double' ? Number(raw) : parseInt(raw, 10))
      return (
        <Field
          label={
            hasRange ? (
              <span style={{ display: 'flex', justifyContent: 'space-between' }}>
                <span>{label}</span>
                <span className="chip">{value}</span>
              </span>
            ) : (
              label
            )
          }
        >
          {hasRange ? (
            <Range
              min={Number(param.min)}
              max={Number(param.max)}
              step={param.type === 'double' ? param.step || 0.1 : 1}
              value={value}
              onChange={(e) => commit(e.target.value)}
            />
          ) : (
            <Input
              type="number"
              step={param.type === 'double' ? param.step || 0.1 : 1}
              value={value ?? 0}
              onChange={(e) => commit(e.target.value)}
            />
          )}
        </Field>
      )
    }
    case 'file':
    case 'folder':
      return (
        <Field label={param.label || param.desc || param.name}>
          <div style={{ display: 'flex', gap: 8 }}>
            <Input
              style={{ flex: 1 }}
              value={value ?? ''}
              onChange={(e) => onChange(e.target.value)}
            />
            <Button
              size="sm"
              onClick={async () => {
                try {
                  const res = await call(
                    'plugins_pickPath',
                    param.type,
                    `选择${param.type === 'file' ? '文件' : '文件夹'}`,
                    param.filter ?? ''
                  )
                  if (res?.ok && res.path != null) onChange(res.path)
                } catch (e) {
                  console.error('pickPath failed', e)
                }
              }}
            >
              浏览
            </Button>
          </div>
        </Field>
      )
    case 'select':
      return (
        <Field label={param.label || param.desc || param.name}>
          <Select
            options={(param.options ?? []).map((opt, i) => ({ value: i, label: opt }))}
            value={value ?? 0}
            onChange={(v) => onChange(Number(v))}
          />
        </Field>
      )
    case 'datetime':
      // datetime params are unix timestamps: keep the input numeric so the
      // value stays a number on save (the native side accepts numeric strings
      // too, but a number avoids surprises).
      return (
        <Field label={param.label || param.desc || param.name}>
          <Input
            type="number"
            value={value ?? 0}
            onChange={(e) => onChange(Number(e.target.value))}
          />
        </Field>
      )
    default:
      // string
      return (
        <Field label={param.label || param.desc || param.name}>
          <Input
            value={value ?? ''}
            onChange={(e) => onChange(e.target.value)}
          />
        </Field>
      )
  }
}
