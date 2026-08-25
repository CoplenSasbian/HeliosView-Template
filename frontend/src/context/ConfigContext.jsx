// ConfigContext — config business module (like LoggerContext): owns the
// global config state (active config + config list), auto-refreshes from the
// native "configActivated"/"configsChanged" broadcasts, and exposes the
// operations pages need. Plugin list / parameter metadata live in
// PluginContext (separate business module). Pages consume it via useConfig()
// instead of subscribing to broadcasts or calling the bridge themselves.
import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react'
import { call } from '../bridge'
import { useChannel } from '../hooks/useChannel.js'

const ConfigContext = createContext(null)

export function ConfigProvider({ children }) {
  const [activeConfig, setActiveConfig] = useState('')
  const [configs, setConfigs] = useState([])
  const [loading, setLoading] = useState(true)

  const refresh = useCallback(async () => {
    try {
      const state = await call('config_get')
      setActiveConfig(state?.activeConfig ?? '')
      setConfigs(Array.isArray(state?.configs) ? state.configs : [])
    } catch (e) {
      console.error('ConfigProvider: refresh failed', e)
    } finally {
      setLoading(false)
    }
  }, [])

  // Initial load, then keep in sync with native-side mutations (manual/auto
  // activate, config create/delete).
  useEffect(() => { refresh() }, [refresh])
  useChannel('configActivated', refresh)
  useChannel('configsChanged', refresh)

  const activate = useCallback(async (config) => {
    if (config === activeConfig) return // already active, skip
    await call('plugins_activate', config)
    await refresh() // also arrives via broadcast; await here so callers see it done
  }, [refresh, activeConfig])

  // Create/delete: rethrow on failure (pages own the toast); refresh happens
  // via the native broadcasts too, but awaiting refresh keeps callers in sync.
  const createConfig = useCallback(async (name) => {
    await call('plugins_createConfig', name)
    await refresh()
  }, [refresh])

  const deleteConfig = useCallback(async (name) => {
    await call('plugins_deleteConfig', name)
    await refresh()
  }, [refresh])

  const value = useMemo(
    () => ({ activeConfig, configs, loading, refresh, activate, createConfig, deleteConfig }),
    [activeConfig, configs, loading, refresh, activate, createConfig, deleteConfig],
  )

  return <ConfigContext.Provider value={value}>{children}</ConfigContext.Provider>
}

export function useConfig() {
  const ctx = useContext(ConfigContext)
  if (!ctx) throw new Error('useConfig 必须在 ConfigProvider 内部使用')
  return ctx
}
