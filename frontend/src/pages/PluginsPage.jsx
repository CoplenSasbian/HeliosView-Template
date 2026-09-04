// PluginsPage — live plugin manager UI. Owns the local data flow (loading
// state, drafts, param values/infos, bridge calls for editing); the global
// config list / active config come from ConfigContext.
// ConfigSelector / PluginCard / CreateConfigModal / ParamConfigModal.

import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { Button, Card, Input, Segmented, DataTable, Toggle, Pill } from '../ui'
import { IconGrid, IconList, IconSearch, IconRefresh, IconSave, IconUndo, IconSettings, IconFolder } from '../ui'
import { formatValue } from '../components/PluginCard'
import { useChannel } from '../hooks/useChannel.js'
import { usePlugins,useConfig } from '../context'
import { call } from '../bridge'
import ConfigSelector from '../components/ConfigSelector'
import PluginCard from '../components/PluginCard'
import PluginDetailModal from '../components/PluginDetailModal'
import CreateConfigModal from '../components/CreateConfigModal'
import ParamConfigModal from '../components/ParamConfigModal'
import ConfirmModal from '../components/ConfirmModal'
import { normalizeValue } from '../components/ParamControl'
import { toast } from '../ui'

export default function PluginsPage() {
  const [data, setData] = useState(null)
  const [loading, setLoading] = useState(true)
  const [draft, setDraft] = useState({})
  const [saving, setSaving] = useState(false)
  const [configPlugin, setConfigPlugin] = useState(null)
  // 详情弹窗中查看的插件对象（null = 关闭）
  const [detailPlugin, setDetailPlugin] = useState(null)
  const [creating, setCreating] = useState(false)
  const [showCreateConfig, setShowCreateConfig] = useState(false)
  // 待删除的配置名（null = 未在确认弹窗中）
  const [deleteTarget, setDeleteTarget] = useState(null)
  const [deleting, setDeleting] = useState(false)
  // 当前编辑的配置（null = 跟随激活配置）。选择它只是决定编辑哪一份，
  // 不会激活；激活只能通过主页的“切换配置”完成。
  const [editConfig, setEditConfig] = useState(null)
  // 视图模式：'grid' 网格卡片流 vs 'list' 紧凑列表（持久化到 localStorage）
  const [viewMode, setViewMode] = useState(() => {
    try {
      return localStorage.getItem('gt_plugins_view') || 'grid'
    } catch {
      return 'grid'
    }
  })
  // 搜索关键词
  const [searchQuery, setSearchQuery] = useState('')
  // 状态筛选：'all' | 'enabled' | 'disabled'
  const [statusFilter, setStatusFilter] = useState('all')

  // 配置列表 / 当前配置由 ConfigContext 维护；插件列表 + 参数元数据由
  // PluginContext 维护；本页只自管 params 值（跟随编辑目标）。
  const { configs, activeConfig, createConfig: createConfigCtx, deleteConfig: deleteConfigCtx } = useConfig()
  const { plugins, infos, clear: clearPlugins, refresh: refreshPlugins } = usePlugins()

  const handleViewModeChange = (mode) => {
    setViewMode(mode)
    try {
      localStorage.setItem('gt_plugins_view', mode)
    } catch {
      // ignore
    }
  }

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

  // 合并草稿与服务器当前值，实时判定插件是否启用
  const isPluginEnabled = useCallback(
    (pluginName) => {
      if (draft[pluginName]?._enabled !== undefined) {
        return Boolean(draft[pluginName]._enabled)
      }
      return Boolean(params[pluginName]?._enabled ?? true)
    },
    [draft, params]
  )

  // 快捷启闭单个插件（加入草稿）
  const handleTogglePlugin = (pluginName, nextEnabled) => {
    setParam(pluginName, '_enabled', nextEnabled)
  }

  // 声明式 DataTable 列定义（List 模式专用）
  const listColumns = useMemo(
    () => [
      {
        key: 'name',
        title: '插件名称',
        width: 170,
        render: (p) => (
          <div className="plugin-row__identity">
            <span className="plugin-row__name" title={p.name}>{p.name}</span>
            <span className="plugin-row__ver">v{p.version}</span>
          </div>
        ),
      },
      {
        key: 'desc',
        title: '功能描述',
        flex: '1 1 200px',
        render: (p) => (
          <span className="plugin-row__desc" title={p.description || '无详细描述'}>
            {p.description || '—'}
          </span>
        ),
      },
      {
        key: 'params',
        title: '核心参数预览',
        flex: '1 1 240px',
        render: (p) => {
          const pluginValues = { ...(params[p.name] ?? {}), ...(draft[p.name] ?? {}) }
          const declared = infos[p.name] ?? []
          const shown = declared.slice(0, 4)
          const extra = declared.length - shown.length
          if (!shown.length) return <span className="plugin-row__params-empty">无参数</span>
          return (
            <div className="plugin-row__params">
              {shown.map((info) => {
                const label = info.label || info.name
                const text = formatValue(info, pluginValues[info.name])
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
          )
        },
      },
      {
        key: 'status',
        title: '启用状态',
        width: 68,
        align: 'center',
        render: (p) => {
          const enabled = isPluginEnabled(p.name)
          return (
            <div
              onClick={(e) => e.stopPropagation()}
              onKeyDown={(e) => e.stopPropagation()}
            >
              <Toggle
                size="sm"
                on={enabled}
                title={enabled ? '点击停用插件' : '点击启用插件'}
                onChange={(val) => handleTogglePlugin(p.name, val)}
              />
            </div>
          )
        },
      },
      {
        key: 'actions',
        title: '操作',
        width: 64,
        align: 'right',
        render: (p) => (
          <div
            className="plugin-row__actions"
            onClick={(e) => e.stopPropagation()}
            onKeyDown={(e) => e.stopPropagation()}
          >
            <button
              type="button"
              className="plugin-row__btn"
              title="参数配置"
              onClick={(e) => {
                e.stopPropagation()
                setConfigPlugin(p.name)
              }}
            >
              <IconSettings width={14} height={14} />
            </button>
            <button
              type="button"
              className="plugin-row__btn"
              title="在资源管理器中显示"
              onClick={(e) => {
                e.stopPropagation()
                call('shell_reveal', 'plugin', p.name)
              }}
            >
              <IconFolder width={14} height={14} />
            </button>
          </div>
        ),
      },
    ],
    [params, draft, infos, isPluginEnabled]
  )
  const totalCount = pluginsList.length
  const enabledCount = useMemo(
    () => pluginsList.filter((p) => isPluginEnabled(p.name)).length,
    [pluginsList, isPluginEnabled]
  )
  const disabledCount = totalCount - enabledCount

  const filterOptions = useMemo(() => [
    { value: 'all', label: '全部' },
    { value: 'enabled', label: '已启用' },
    { value: 'disabled', label: '已停用' },
  ], [])

  const viewOptions = useMemo(() => [
    { value: 'grid', label: '', icon: <IconGrid width={18} height={18} /> },
    { value: 'list', label: '', icon: <IconList width={18} height={18} /> },
  ], [])

  // 搜索和状态过滤
  const filteredPlugins = useMemo(() => {
    const q = searchQuery.trim().toLowerCase()
    return pluginsList.filter((p) => {
      const enabled = isPluginEnabled(p.name)
      if (statusFilter === 'enabled' && !enabled) return false
      if (statusFilter === 'disabled' && enabled) return false
      if (q) {
        const matchName = p.name?.toLowerCase().includes(q)
        const matchDesc = p.description?.toLowerCase().includes(q)
        const matchAuthor = p.author?.toLowerCase().includes(q)
        if (!matchName && !matchDesc && !matchAuthor) return false
      }
      return true
    })
  }, [pluginsList, searchQuery, statusFilter, isPluginEnabled])

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

      <Card
        title="插件"
        hint={
          pluginsList.length
            ? `${enabledCount} 个已启用 / 共 ${pluginsList.length} 个`
            : undefined
        }
        index={1}
        actions={pluginsList.length ? (
          <>
            <Button
              variant="ghost"
              icon={<IconRefresh width={14} height={14} />}
              disabled={loading}
              title={loading ? '刷新中…' : '刷新插件与参数'}
              onClick={async () => {
                if (!guardDirty()) return
                clearPlugins()                 // unmount rows
                await refreshPlugins()         // re-fetch plugin list → rows re-mount
                refresh(targetConfig)          // re-fetch param values (fire-and-forget)
              }}
            >
              刷新
            </Button>
            {dirtyCount > 0 && (
              <Button
                variant="ghost"
                icon={<IconUndo width={14} height={14} />}
                title="放弃所有未保存修改"
                onClick={() => setDraft({})}
              >
                放弃修改
              </Button>
            )}
            <Button
              variant="primary"
              icon={<IconSave width={14} height={14} />}
              onClick={saveParams}
              disabled={saving || !dirtyCount || !targetConfig}
              title={
                !targetConfig
                  ? '请先选择配置'
                  : dirtyCount
                    ? `保存 ${dirtyCount} 项修改`
                    : '无未保存的修改'
              }
            >
              {saving
                ? '保存中…'
                : dirtyCount
                  ? `保存 (${dirtyCount})`
                  : '保存'}
            </Button>
          </>
        ) : undefined}
      >
        {pluginsList.length > 0 && (
          <div className="plugin-toolbar">
            <div className="plugin-toolbar__left">
              <Input
                size="sm"
                className="plugin-search-wrap"
                prefix={<IconSearch width={14} height={14} />}
                allowClear
                placeholder="搜索插件名称或描述..."
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
              />

              <Segmented
                size="sm"
                options={filterOptions}
                value={statusFilter}
                onChange={setStatusFilter}
                renderAction={(val) => {
                  let cnt = totalCount
                  if (val === 'enabled') cnt = enabledCount
                  else if (val === 'disabled') cnt = disabledCount
                  return <span className="segmented-count-badge">{cnt}</span>
                }}
              />
            </div>

            <div className="plugin-toolbar__right">
              <Segmented
                size="sm"
                options={viewOptions}
                value={viewMode}
                onChange={handleViewModeChange}
              />
            </div>
          </div>
        )}

        {pluginsList.length ? (
          filteredPlugins.length ? (
            viewMode === 'grid' ? (
              <div className="plugin-grid-layout">
                {filteredPlugins.map((p, i) => {
                  const pluginValues = {
                    ...(params[p.name] ?? {}),
                    ...(draft[p.name] ?? {}),
                  }
                  return (
                    <PluginCard
                      key={p.name}
                      index={i}
                      mode="grid"
                      plugin={p}
                      values={pluginValues}
                      infos={infos[p.name] ?? []}
                      onClick={() => setDetailPlugin(p)}
                      onConfig={() => setConfigPlugin(p.name)}
                      onReveal={() => call('shell_reveal', 'plugin', p.name)}
                      onToggle={(nextVal) => handleTogglePlugin(p.name, nextVal)}
                    />
                  )
                })}
              </div>
            ) : (
              <DataTable
                columns={listColumns}
                data={filteredPlugins}
                rowKey="name"
                onRowClick={(p) => setDetailPlugin(p)}
                rowClassName={(p) => (isPluginEnabled(p.name) ? '' : 'is-disabled')}
              />
            )
          ) : (
            <div className="plugin-empty-box">
              <span className="plugin-empty-box__title">未找到匹配的插件</span>
              <p className="field__desc">请尝试调整搜索关键词或重置筛选条件</p>
              <Button
                variant="ghost"
                size="sm"
                onClick={() => {
                  setSearchQuery('')
                  setStatusFilter('all')
                }}
              >
                重置筛选
              </Button>
            </div>
          )
        ) : (
          <p className="field__desc">
            {loading ? '正在加载插件…' : '没有加载到插件：检查 plugins 目录下的文件'}
          </p>
        )}
      </Card>


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

      <PluginDetailModal
        plugin={detailPlugin}
        infos={detailPlugin ? (infos[detailPlugin.name] ?? []) : []}
        enabled={detailPlugin ? (params[detailPlugin.name]?._enabled ?? true) : true}
        onClose={() => setDetailPlugin(null)}
      />
    </div>
  )
}
