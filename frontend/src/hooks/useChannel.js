// useChannel — subscribe to a native broadcast channel.
// The native side broadcasts per-topic channels after mutations:
//   "configActivated"  — a config was activated/switched (manual, auto, create)
//   "paramsSaved"      — plugin parameters were saved
//   "configsChanged"   — configs were created/deleted
//   "settingsChanged"  — app settings / process state changed (page OR tray menu)
// Pages subscribe only to the channels they care about and refresh on message.
import { useEffect, useRef } from 'react'

export function useChannel(channel, onChange) {
  const cbRef = useRef(onChange)
  cbRef.current = onChange

  useEffect(() => {
    const ch = new BroadcastChannel(channel)
    const handle = (e) => cbRef.current?.(e.data)
    ch.addEventListener('message', handle)
    return () => {
      ch.removeEventListener('message', handle)
      ch.close()
    }
  }, [channel])
}
