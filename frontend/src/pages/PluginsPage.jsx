// PluginsPage — live plugin manager UI. Owns the local data flow (loading
// state, drafts, param values/infos, bridge calls for editing); the global
// config list / active config come from ConfigContext.
// ConfigSelector / PluginCard / CreateConfigModal / ParamConfigModal.

import { useCallback, useEffect, useRef, useState } from 'react'
import { Button } from '../components/ui'
import { useChannel } from '../hooks/useChannel.js'
import { useConfig } from '../context/ConfigContext.jsx'
import { usePlugins } from '../context/PluginContext.jsx'
import { call } from '../bridge'
import ConfigSelector from '../components/ConfigSelector'
import PluginCard from '../components/PluginCard'
import CreateConfigModal from '../components/CreateConfigModal'
import ParamConfigModal from '../components/ParamConfigModal'
import ConfirmModal from '../components/ConfirmModal'
import { normalizeValue } from '../components/ParamControl'
import { toast } from '../components/toast'

export default function PluginsPage() {
  const [data, setData] = useState(null)
  const [loading, setLoading] = useState(true)
  const [draft, setDraft] = useState({})
  const [saving, setSaving] = useState(false)
  const [configPlugin, setConfigPlugin] = useState(null)
  const [creating, setCreating] = useState(false)
  const [showCreateConfig, setShowCreateConfig] = useState(false)
  // 待删除的配置名（null = 未在确认弹窗中）
  const [deleteTarget, setDeleteTarget] = useState(null)
  const [deleting, setDeleting] = useState(false)
  // 当前编辑的配置（null = 跟随激活配置）。选择它只是决定编辑哪一份，
  // 不会激活；激活只能通过主页的“切换配置”完成。
  const [editConfig, setEditConfig] = useState(null)
  // 配置列表 / 当前配置由 ConfigContext 维护；插件列表 + 参数元数据由
  // PluginContext 维护；本页只自管 params 值（跟随编辑目标）。
  const { configs, activeConfig, createConfig: createConfigCtx, deleteConfig: deleteConfigCtx } = useConfig()
  const { plugins, infos, clear: clearPlugins, refresh: refreshPlugins } = usePlugins()

  // 拉当前参数值（config 空 = 激活配置）。
  const refresh = useCallback(async (config) => {
    setLoading(true)
    setData({ params: {} })
    try {
      const values = await call('plugins_getParamValues', config ?? '')
      setData({ params: values ?? {} })
      setDraft({})
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '调用失败'))
    } finally {
      setLoading(false)
    }
  }, [])

  // Initial load once the config list is ready (ConfigContext drives it).
  useEffect(() => {
    if (configs?.length) refresh()
  }, [configs, refresh])

  // 未保存草稿数量
  const dirtyCount = Object.values(draft).reduce(
    (sum, values) => sum + Object.keys(values).length,
    0
  )

  // 原生侧参数保存 / 配置切换后刷新本页 params（configs/activeConfig 由
  // ConfigContext 自动刷新）；有未保存草稿时跳过（避免清掉正在编辑的内容）。
  const editConfigRef = useRef(editConfig)
  editConfigRef.current = editConfig
  const dirtyRef = useRef(dirtyCount)
  dirtyRef.current = dirtyCount
  const refreshFromNative = useCallback((msg) => {
    if (dirtyRef.current > 0) {
      toast.info(`配置「${msg?.activeConfig ?? ''}」已变更，保存后可查看最新参数`)
      return
    }
    refresh(editConfigRef.current ?? '')
  }, [refresh])
  useChannel('configActivated', refreshFromNative)
  useChannel('paramsSaved', refreshFromNative)

  // 有未保存修改时拦截操作并提示（避免误丢草稿）
  const guardDirty = () => {
    if (!dirtyCount) return true
    toast.error(`有 ${dirtyCount} 项未保存的修改，请先保存或放弃修改`)
    return false
  }

  const createConfig = async (name) => {
    setCreating(true)
    try {
      await createConfigCtx(name)
      setShowCreateConfig(false)
      setEditConfig(name)   // 新配置已激活，直接作为编辑目标
      toast.success(`配置「${name}」已创建`)
      await refresh(name)
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '创建失败'))
    } finally {
      setCreating(false)
    }
  }

  const deleteConfig = async () => {
    if (!deleteTarget) return
    setDeleting(true)
    try {
      await deleteConfigCtx(deleteTarget)
      setDeleteTarget(null)
      setEditConfig(null)   // 跟随新的激活配置（被删的可能是激活配置）
      toast.success(`配置「${deleteTarget}」已删除`)
      await refresh()
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '删除失败'))
    } finally {
      setDeleting(false)
    }
  }

  const saveParams = async () => {
    const target = editConfig ?? activeConfig
    if (!target) {
      toast.error('请先选择要编辑的配置')
      return
    }
    const params = []
    for (const [plugin, values] of Object.entries(draft)) {
      const pluginInfos = infos[plugin] ?? []
      for (const [name, value] of Object.entries(values)) {
        // _enabled 不是插件声明的参数: 配置中禁用/启用该插件的开关
        if (name === '_enabled') {
          params.push({ plugin, name, value: Boolean(value) })
          continue
        }
        const info = pluginInfos.find((p) => p.name === name)
        if (!info) {
          toast.error(`找不到插件 ${plugin} 的参数 ${name}`)
          return
        }
        params.push({ plugin, name, value: normalizeValue(value, info.type) })
      }
    }
    if (!params.length) return
    setSaving(true)
    try {
      await call('plugins_setParams', target, params)
      toast.success(`已保存到配置「${target}」`)
      await refresh(target)
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '保存失败'))
    } finally {
      setSaving(false)
    }
  }

  const setParam = (plugin, name, value) =>
    setDraft((d) => ({ ...d, [plugin]: { ...d[plugin], [name]: value } }))

  const pluginsList = plugins ?? []
  const params = data?.params ?? {}   // 当前值：{plugin: {param: value}}
  const targetConfig = editConfig ?? activeConfig

  return (
    <div>
      <div className="page-title">插件</div>
      <p className="page-sub">插件加载与配置（实时）</p>

      <ConfigSelector
        configs={configs}
        value={targetConfig}
        activeConfig={activeConfig}
        loading={loading}
        index={0}
        onChange={(c) => {
          if (guardDirty()) {
            setEditConfig(c)
            refresh(c)
          }
        }}
        onAddConfig={() => setShowCreateConfig(true)}
        onDeleteConfig={(config) => {
          if (guardDirty()) setDeleteTarget(config)
        }}
      />

      <div className="section-gap" />

      {pluginsList.length ? (
        <div className="plugin-grid">
          {pluginsList.map((p, i) => (
            <PluginCard
              key={p.name}
              plugin={p}
              index={i + 1}
              values={params[p.name] ?? {}}
              infos={infos[p.name] ?? []}
              onClick={() => setConfigPlugin(p.name)}
              onReveal={() => call('shell_reveal', 'plugin', p.name)}
            />
          ))}
        </div>
      ) : (
        <div className="card" style={{ '--card-i': 1 }}>
          <p className="field__desc">
            {loading ? '正在加载插件…' : '没有加载到插件：检查 plugins 目录下的文件'}
          </p>
        </div>
      )}

      <CreateConfigModal
        open={showCreateConfig}
        busy={creating}
        onClose={() => setShowCreateConfig(false)}
        onSubmit={createConfig}
      />

      <ConfirmModal
        open={!!deleteTarget}
        title="删除配置"
        message={`确定删除配置「${deleteTarget}」？该操作会删除对应的配置文件，不可恢复。`}
        confirmText="删除"
        busy={deleting}
        danger
        onClose={() => { if (!deleting) setDeleteTarget(null) }}
        onConfirm={deleteConfig}
      />

      <ParamConfigModal
        open={!!configPlugin}
        plugin={configPlugin}
        infos={infos[configPlugin] ?? []}
        values={params[configPlugin] ?? {}}
        draft={draft[configPlugin] ?? {}}
        targetConfig={targetConfig}
        saving={saving}
        canSave={dirtyCount > 0}
        onClose={() => setConfigPlugin(null)}
        onChange={(name, value) => setParam(configPlugin, name, value)}
        onSave={saveParams}
      />

      <div className="section-gap" />

      <div style={{ display: 'flex', gap: 8 }}>
        <Button
          onClick={async () => {
            if (!guardDirty()) return
            clearPlugins()                       // unmount cards
            await refreshPlugins()               // re-fetch plugin list → cards re-mount
            refresh(targetConfig)                // re-fetch param values (fire-and-forget)
          }}
          disabled={loading}
        >
          {loading ? '加载中…' : '刷新'}
        </Button>
        {dirtyCount > 0 && (
          <Button
            onClick={() => {
              setDraft({})
            }}
          >
            放弃修改
          </Button>
        )}
        <Button
          variant="primary"
          onClick={saveParams}
          disabled={saving || !dirtyCount || !targetConfig}
        >
          {saving
            ? '保存中…'
            : !targetConfig
              ? '请先选择配置'
              : dirtyCount
                ? `保存参数 (${dirtyCount})`
                : '保存参数'}
        </Button>
      </div>
    </div>
  )
}
