// HomePage — dashboard with live config switching and log console.

import { useState } from 'react'
import { Card, Segmented, Button } from '../components/ui'
import { useLogger } from '../context/LoggerContext.jsx'
import { useConfig } from '../context/ConfigContext.jsx'
import { usePlugins } from '../context/PluginContext.jsx'
import LogConsole from '../components/LogConsole'
import LogFilters from '../components/LogFilters'
import { call } from '../bridge'
import { toast } from '../components/toast'

export default function HomePage() {
  const { logList, clearLogs } = useLogger()
  const [logFilters, setLogFilters] = useState({ timeRange: 0, tag: '', level: '' })
  const { activeConfig, configs, activate: activateConfig } = useConfig()
  const { plugins } = usePlugins()

  const activate = async (config) => {
    try {
      await activateConfig(config)
      toast.success(`已切换到配置「${config}」`)
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '激活失败'))
    }
  }

  const pluginCount = plugins?.length ?? 0
  const isOff = !activeConfig || activeConfig === 'close'

  return (
    <div>
      <div className="page-title">概览</div>
      <p className="page-sub">当前运行状态与配置切换</p>

      {/* status hero + config switcher — side by side */}
      <div className="hero-row">
        <Card className="hero">
          <div className={`hero__ring${isOff ? ' is-off' : ''}`}>
            <span className="status-dot" />
          </div>
          <div className="hero__text">
            <div className="hero__label">{isOff ? '当前已关闭' : '当前配置'}</div>
            <div className="hero__name">{activeConfig || '—'}</div>
            <div className="hero__meta">
              <span>{configs.length} 个配置</span>
              <span>{pluginCount} 个插件</span>
            </div>
          </div>
        </Card>

        <Card title="切换配置" hint="切换后立即生效">
          {configs.length ? (
            <Segmented
              options={configs.map((c) => ({ value: c, label: c }))}
              value={activeConfig}
              onChange={activate}
            />
          ) : (
            <p className="field__desc">暂无配置</p>
          )}
        </Card>
      </div>

      <div className="section-gap" />

      {/* log console */}
      <Card
        title="日志"
        hint="最近 300 条"
        accent="var(--c-success)"
        toolbar={<LogFilters value={logFilters} onChange={setLogFilters} />}
        actions={
          <>
            <Button
              variant="link"
              size="sm"
              onClick={async () => {
                try {
                  await call('shell_openDir', 'logs')
                } catch (e) {
                  toast.error(String(e?.error ?? e ?? '打开失败'))
                }
              }}
            >
              打开日志目录
            </Button>
            <Button variant="link" size="sm" onClick={clearLogs} disabled={!logList.length}>
              清屏
            </Button>
          </>
        }
      >
        <LogConsole filters={logFilters} />
      </Card>
    </div>
  )
}
