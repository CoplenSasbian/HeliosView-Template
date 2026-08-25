// PluginContext — plugin business module: the loaded plugin list and each
// plugin's parameter metadata (infos). Plugins are loaded once at startup and
// don't change at runtime, so no broadcast subscription is needed here — load
// once, expose refresh() for completeness. Pages consume it via usePlugins().
import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react'
import { call } from '../bridge'

const PluginContext = createContext(null)

export function PluginProvider({ children }) {
  const [plugins, setPlugins] = useState([])
  const [infos, setInfos] = useState({})
  const [loading, setLoading] = useState(true)

  // Temporarily empty the plugin list so the grid unmounts all cards; the
  // caller should follow up with refresh() which re-populates them (triggering
  // the card entrance animation).
  const clear = useCallback(() => {
    setPlugins([])
    setInfos({})
  }, [])

  const refresh = useCallback(async () => {
    try {
      const state = await call('config_get')
      setPlugins(Array.isArray(state?.plugins) ? state.plugins : [])
      setInfos(state?.paramInfos ?? {})
    } catch (e) {
      console.error('PluginProvider: refresh failed', e)
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => { refresh() }, [refresh])

  const value = useMemo(
    () => ({ plugins, infos, loading, refresh, clear }),
    [plugins, infos, loading, refresh, clear],
  )

  return <PluginContext.Provider value={value}>{children}</PluginContext.Provider>
}

export function usePlugins() {
  const ctx = useContext(PluginContext)
  if (!ctx) throw new Error('usePlugins 必须在 PluginProvider 内部使用')
  return ctx
}
