// ProcessContext — process-monitor business module: the watch rules and the
// monitor's enabled/running state. Everything is read/written through the single
// whole-object settings bridge (settings_get / settings_set) — process fields
// live inside AppSettings alongside every other setting — and the native
// "settingsChanged" broadcast keeps both the page and the tray in sync.
import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react'
import { call } from '../bridge'
import { useChannel } from '../hooks/useChannel'

const ProcessContext = createContext(null)

export function ProcessProvider({ children }) {
  const [enabled, setEnabled] = useState(false)
  const [running, setRunning] = useState(false)
  const [rules, setRules] = useState([])
  const [loading, setLoading] = useState(true)

  const refresh = useCallback(async () => {
    try {
      const state = await call('settings_get')
      setEnabled(!!state?.processAutoSwitch)
      setRunning(!!state?.processRunning)
      setRules(Array.isArray(state?.processRules) ? state.processRules : [])
    } catch (e) {
      console.error('ProcessProvider: refresh failed', e)
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => { refresh() }, [refresh])

  // Native broadcasts the whole settings object (with processRunning) from
  // every write point; apply the triage directly.
  useChannel('settingsChanged', (state) => {
    if (typeof state?.processAutoSwitch === 'boolean') setEnabled(state.processAutoSwitch)
    if (typeof state?.processRunning === 'boolean') setRunning(state.processRunning)
  })

  // Mutations via the whole-object settings_set patch; then re-pull the
  // authoritative snapshot. Rethrow so pages own toasts.
  const setEnabledSetting = useCallback(async (on) => {
    await call('settings_set', { processAutoSwitch: on })
    await refresh()
  }, [refresh])

  const setRulesSetting = useCallback(async (next) => {
    await call('settings_set', { processRules: next })
    // settings_set 内部会调 ApplyProcessMonitor()，可能启动/停止监控；
    // refresh() 从 settings_get 拉取 processRunning 等全量状态。
    await refresh()
  }, [refresh])

  const value = useMemo(
    () => ({
      enabled,
      running,
      rules,
      loading,
      refresh,
      setEnabled: setEnabledSetting,
      setRules: setRulesSetting,
    }),
    [enabled, running, rules, loading, refresh, setEnabledSetting, setRulesSetting],
  )

  return <ProcessContext.Provider value={value}>{children}</ProcessContext.Provider>
}

export function useProcess() {
  const ctx = useContext(ProcessContext)
  if (!ctx) throw new Error('useProcess 必须在 ProcessProvider 内部使用')
  return ctx
}
