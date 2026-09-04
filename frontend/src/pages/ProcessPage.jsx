// ProcessPage — process monitor + auto-activate config. Watch rules map an
// exe path to a config; when a watched process starts the native side switches
// to that config, and back to "close" when the last watched process exits.
// Process state/rules come from ProcessContext; config list / active config
// from ConfigContext; only the new-rule form lives in this page.

import { useState, useRef, useLayoutEffect, useMemo } from 'react'
import { Card, Toggle, Button, Select, SettingRow, Pill, Input, DataTable } from '../ui'
import { IconTrash, IconFolder } from '../ui'
import { useProcess,useConfig} from '../context'
import { call } from '../bridge'
import { toast } from '../ui'

// Middle-ellipsis for long exe paths: keep the front of the path and the
// WHOLE file name, collapse only the middle directories — C:\Games\…\GameA.exe.
// Measures against the real rendered column width (the table uses fixed
// layout, so the path cell has a stable width): truncation only kicks in when
// the path genuinely doesn't fit, and the tooltip always shows the full path.
function ProcessPath({ path }) {
  const ref = useRef(null)
  const [disp, setDisp] = useState(path)

  useLayoutEffect(() => {
    const el = ref.current
    if (!el) return
    let raf = 0
    let cancelled = false
    let ctx = null

    // Measure text with a Canvas context using the span's ACTUAL computed
    // font. Critically, this never mutates the DOM: the old approach (write
    // the candidate into the span, read scrollWidth/clientWidth) made the
    // measurement itself trigger a re-layout, so clientWidth could grow to
    // fit whatever text we just set ("always fits" → no truncation, or the
    // wrong truncation after a resize). Canvas measurement is side-effect
    // free, so the result is stable and matches the real column width.
    const textWidth = (s) => {
      if (!ctx) {
        ctx = document.createElement('canvas').getContext('2d')
        ctx.font = getComputedStyle(el).font
      }
      return ctx.measureText(s).width
    }

    const compute = () => {
      if (cancelled) return
      // The span is width:100% of the fixed-layout column, so its laid-out
      // width IS the available width — read it before any state change.
      const avail = el.getBoundingClientRect().width
      if (avail <= 0) return // not laid out yet; RO/resize will re-trigger

      if (textWidth(path) <= avail + 1) return setDisp(path)

      // Split at the last separator: keep the file name whole and collapse the
      // directory part. Cut only right AFTER a separator so the result reads
      // "C:\Games\…\GameA.exe", never "C:\Games\G…ameA.exe".
      const sep = Math.max(path.lastIndexOf('\\'), path.lastIndexOf('/'))
      if (sep > 0 && textWidth('…' + path.slice(sep + 1)) <= avail + 1) {
        const name = path.slice(sep + 1)
        const front = path.slice(0, sep + 1)
        let best = '…' + name // even the whole directory part may have to go
        for (let i = 0; i < front.length; ++i) {
          if (front[i] !== '\\' && front[i] !== '/') continue
          const cand = front.slice(0, i + 1) + '…' + name
          if (textWidth(cand) > avail + 1) break // longer fronts only get wider — stop early
          best = cand
        }
        return setDisp(best)
      }

      // No directory part, or the file name alone is too wide: keep an equal
      // head + tail and collapse the middle. (CSS text-overflow is the
      // last-resort clip for absurdly long single tokens.)
      let lo = 0, hi = path.length >> 1, best = path
      while (lo <= hi) {
        const mid = (lo + hi) >> 1
        const cand = mid > 0 ? path.slice(0, mid) + '…' + path.slice(-mid) : path
        if (textWidth(cand) <= avail + 1) { best = cand; lo = mid + 1 } else hi = mid - 1
      }
      setDisp(best)
    }

    // Re-measure when the column width changes: the span's ResizeObserver
    // covers card/column resizes; the window listener is a cheap safety net.
    // rAF batches resize storms into one pass; fonts.ready covers late
    // webfont loads that would otherwise skew measureText.
    const schedule = () => {
      cancelAnimationFrame(raf)
      raf = requestAnimationFrame(compute)
    }

    compute()
    const ro = new ResizeObserver(schedule)
    ro.observe(el)
    window.addEventListener('resize', schedule)
    document.fonts?.ready.then(schedule)
    return () => {
      cancelled = true
      ro.disconnect()
      window.removeEventListener('resize', schedule)
      cancelAnimationFrame(raf)
    }
  }, [path])

  return <span className="process-path" ref={ref} title={path}>{disp}</span>
}

export default function ProcessPage() {
  const { enabled, running, rules, loading, setEnabled, setRules } = useProcess()
  // 新增规则的表单
  const [newExe, setNewExe] = useState('')
  const [newConfig, setNewConfig] = useState('')
  // 配置列表 / 当前配置由 ConfigContext 统一维护（含自动切换后的刷新）
  const { configs, activeConfig } = useConfig()

  const saveRules = async (next) => {
    try {
      await setRules(next)
      toast.success('规则已保存')
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '保存失败'))
    }
  }

  const toggleEnabled = async (on) => {
    try {
      await setEnabled(on)
      toast.success(on ? '已启用自动切换' : '已停用自动切换')
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '设置失败'))
    }
  }

  const pickExe = async () => {
    try {
      // type "exe" — the native dialog filters to *.exe (process monitor only
      // watches executables; plugin `file` params keep the unfiltered dialog).
      const res = await call('plugins_pickPath', 'exe', '选择要监控的程序', '')
      if (res?.ok && res.path) setNewExe(res.path)
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '选择失败'))
    }
  }

  const addRule = async () => {
    const exe = newExe.trim()
    const config = newConfig || configs[0]
    if (!exe || !config) {
      toast.error('请选择程序和目标配置')
      return
    }
    if (rules.some((r) => r.exe.toLowerCase() === exe.toLowerCase())) {
      toast.error('该程序已有规则，可直接修改目标配置')
      return
    }
    await saveRules([...rules, { exe, config }])
    setNewExe('')
  }

  const changeRuleConfig = (exe, config) => {
    saveRules(rules.map((r) => (r.exe === exe ? { ...r, config } : r)))
  }

  const removeRule = (exe) => {
    saveRules(rules.filter((r) => r.exe !== exe))
  }

  const ruleColumns = useMemo(
    () => [
      {
        key: 'exe',
        title: '程序路径',
        flex: '1 1 0',
        render: (r) => <ProcessPath path={r.exe} />,
      },
      {
        key: 'config',
        title: '切换到配置',
        width: 180,
        render: (r) => (
          <Select
            size="sm"
            options={configs.map((c) => ({ value: c, label: c }))}
            value={r.config}
            disabled={loading}
            onChange={(val) => changeRuleConfig(r.exe, val)}
          />
        ),
      },
      {
        key: 'actions',
        title: '',
        width: 44,
        align: 'right',
        render: (r) => (
          <button
            type="button"
            className="plugin-row__btn"
            title={`删除规则 ${r.exe}`}
            onClick={() => removeRule(r.exe)}
          >
            <IconTrash width={14} height={14} />
          </button>
        ),
      },
    ],
    [configs, loading]
  )

  return (
    <div>
      <div className="page-title">进程监控</div>
      <p className="page-sub">当以下程序启动时，自动切换到对应配置；全部退出后回到「关闭」</p>

      <Card title="自动切换">
        <SettingRow
          label="启用进程监控"
          desc={
            running
              ? '监控运行中（WMI 进程事件）'
              : enabled
                ? '已启用，但还没有匹配规则'
                : '停用'
          }
          control={
            <Toggle
              on={enabled}
              onChange={toggleEnabled}
              title={loading ? '加载中…' : enabled ? '已启用' : '已停用'}
            />
          }
        />
        <SettingRow
          label="当前配置"
          desc="被监控程序触发后自动切换到这里"
          control={<Pill tone="muted">{activeConfig || '—'}</Pill>}
        />
      </Card>

      <div className="section-gap" />

      <Card title="匹配规则" hint={`${rules.length} 条规则`}>
        {rules.length ? (
          <DataTable
            columns={ruleColumns}
            data={rules}
            rowKey="exe"
          />
        ) : (
          <p className="field__desc">暂无规则：添加要监控的程序及其目标配置</p>
        )}

        <div className="section-gap" />

        <div style={{ display: 'flex', gap: 8, alignItems: 'center', flexWrap: 'wrap' }}>
          <Input
            style={{ flex: 1, minWidth: 260 }}
            allowClear
            placeholder="程序路径，例如 C:\Games\GameA\GameA.exe"
            value={newExe}
            onChange={(e) => setNewExe(e.target.value)}
            onKeyDown={(e) => { if (e.key === 'Enter') addRule() }}
            suffix={
              <button
                type="button"
                className="input__action-btn"
                title="浏览选择可执行文件"
                onClick={pickExe}
              >
                <IconFolder width={14} height={14} />
              </button>
            }
          />
          <Select
            options={configs.map((c) => ({ value: c, label: c }))}
            value={newConfig}
            placeholder="目标配置"
            onChange={setNewConfig}
          />
          <Button variant="primary" onClick={addRule}>
            + 添加规则
          </Button>
        </div>
      </Card>
    </div>
  )
}
