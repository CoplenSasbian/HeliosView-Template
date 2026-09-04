// ParamConfigModal.jsx — modal for editing one plugin's parameters. Pure
// presentation: values come from the parent (current values + local draft),
// changes are reported via onChange(name, value), saving via onSave().

import { Modal, Button, Toggle, SettingRow } from '../ui'
import ParamControl from './ParamControl'

export default function ParamConfigModal({
  open,
  plugin,
  infos,
  values,
  draft,
  targetConfig,
  saving,
  canSave,
  onClose,
  onChange,
  onSave,
}) {
  // 把本插件所有参数恢复为声明时的默认值 (defaultValue 来自插件元数据)
  const restoreDefaults = () => {
    for (const p of infos) {
      if (p.defaultValue !== undefined) onChange(p.name, p.defaultValue)
    }
  }

  return (
    <Modal
      open={open}
      title={`${plugin} — 参数配置`}
      onClose={onClose}
      actions={
        <>
          <Button variant="ghost" onClick={restoreDefaults} disabled={!infos.length}>
            恢复默认值
          </Button>
          <Button variant="ghost" onClick={onClose}>关闭</Button>
          <Button
            variant="primary"
            disabled={saving || !canSave || !targetConfig}
            onClick={onSave}
          >
            {saving ? '保存中…' : '保存'}
          </Button>
        </>
      }
    >
      <SettingRow
        label="启用该插件"
        desc="关闭后, 切换到当前配置时不会执行此插件"
        control={
          <Toggle
            on={draft._enabled ?? values._enabled ?? true}
            onChange={(v) => onChange('_enabled', v)}
          />
        }
      />

      {infos.map((param) => (
        <ParamControl
          key={param.name}
          param={param}
          value={draft[param.name] ?? values[param.name] ?? param.defaultValue}
          onChange={(v) => onChange(param.name, v)}
        />
      ))}
      {!infos.length && <p className="field__desc">该插件没有可配置参数。</p>}
    </Modal>
  )
}
