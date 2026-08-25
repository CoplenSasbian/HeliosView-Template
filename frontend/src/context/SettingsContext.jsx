// SettingsContext — app-settings business module: auto-start and plugin
// popup position, loaded once and updated by the settings page. The native
// side broadcasts "settingsChanged" from EVERY write point (the settings page
// AND the tray menu), so the UI stays in sync even when a toggle is flipped
// from the system-tray context menu. Pages consume it via useSettings().
import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react'
import { call } from '../bridge'
import { useChannel } from '../hooks/useChannel'

const SettingsContext = createContext(null)

export function SettingsProvider({ children }) {
  const [autoStart, setAutoStart] = useState(false)
  const [popupPosition, setPopupPosition] = useState(3)
  const [loading, setLoading] = useState(true)

  const refresh = useCallback(async () => {
    try {
      const settings = await call('settings_get')
      setAutoStart(settings?.autoStart ?? false)
      setPopupPosition(settings?.popupPosition ?? 3)
    } catch (e) {
      console.error('SettingsProvider: refresh failed', e)
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => { refresh() }, [refresh])

  // Native broadcasts "settingsChanged" carrying the FULL state from every
  // write point (settings page AND tray menu). Update the local snapshot
  // straight from the payload — no extra bridge round-trip — and the UI cells
  // that read this context re-render automatically.
  useChannel('settingsChanged', (state) => {
    if (typeof state?.autoStart === 'boolean') setAutoStart(state.autoStart)
    if (typeof state?.popupPosition === 'number') setPopupPosition(state.popupPosition)
  })

  // Operations mutate native state via the whole-object settings_set (partial
  // patch), update the local snapshot and rethrow on failure (pages own toasts).
  const setAutoStartSetting = useCallback(async (on) => {
    const res = await call('settings_set', { autoStart: on })
    setAutoStart(res?.autoStart ?? on)
    return res?.autoStart === on ? { ok: true } : { ok: false }
  }, [])

  const setPopupPositionSetting = useCallback(async (pos) => {
    const res = await call('settings_set', { popupPosition: Number(pos) })
    setPopupPosition(res?.popupPosition ?? Number(pos))
    return res
  }, [])

  const value = useMemo(
    () => ({
      autoStart,
      popupPosition,
      loading,
      refresh,
      setAutoStart: setAutoStartSetting,
      setPopupPosition: setPopupPositionSetting,
    }),
    [autoStart, popupPosition, loading, refresh, setAutoStartSetting, setPopupPositionSetting],
  )

  return <SettingsContext.Provider value={value}>{children}</SettingsContext.Provider>
}

export function useSettings() {
  const ctx = useContext(SettingsContext)
  if (!ctx) throw new Error('useSettings 必须在 SettingsProvider 内部使用')
  return ctx
}
