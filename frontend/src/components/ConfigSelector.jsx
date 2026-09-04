// ConfigSelector.jsx — the config card on the plugins page. Picking a config
// only selects which one to EDIT (it does not activate it — activation lives
// on the home page); "+ 添加配置" opens the create-config modal and
// "删除配置" deletes the currently selected one (with a confirmation in the
// page). Deletion is one explicit button, not per-chip hover badges — those
// overlapped the options and were too easy to trigger by accident.

import { Segmented, Button, Card, IconTrash } from '../ui'

export default function ConfigSelector({
  configs,
  value,
  activeConfig,
  loading,
  onChange,
  onAddConfig,
  onDeleteConfig,
  index,
}) {
  // Deleting the last remaining config would leave the app with none — keep
  // at least one (the create modal can add more). The built-in "close" config
  // (the off state) is not deletable either.
  const isClose = value === 'close'
  const canDelete = configs.length > 1 && !!value && !isClose

  return (
    <Card
      title="配置"
      index={index}
      actions={
        <>
          <Button
            variant="link"
            size="sm"
            className="btn--link-danger"
            disabled={!canDelete}
            title={
              isClose
                ? '内置「关闭」配置不能删除'
                : configs.length <= 1
                  ? '至少保留一个配置'
                  : `删除配置「${value}」`
            }
            onClick={() => canDelete && onDeleteConfig(value)}
          >
            <IconTrash width={12} height={12} />
            删除配置
          </Button>
          <Button variant="link" size="sm" onClick={onAddConfig}>
            + 添加配置
          </Button>
        </>
      }
    >
      {configs.length ? (
        <Segmented
          options={configs.map((c) => ({ value: c, label: c }))}
          value={value}
          onChange={onChange}
        />
      ) : (
        <p className="field__desc">
          {loading ? '插件加载中…' : '暂无配置：先新建一份'}
        </p>
      )}

        <p className="field__desc" style={{ color: 'var(--c-warning)' }}>
          正在编辑配置「{value}」（未激活），保存不会切换激活配置
        </p>
      {!value && (
        <p className="field__desc">未选择配置，先在上方选择或新建一份</p>
      )}
    </Card>
  )
}
