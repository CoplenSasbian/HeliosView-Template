// bridge.js — shared wrapper for window.helios.call() with request/response
// logging. Every page goes through this so bridge traffic is visible in the
// DevTools console (F12) without per-page duplicates.

export async function call(name, ...args) {
  console.log('[bridge] →', name, args)
  try {
    const result = await window.helios.call(name, ...args)
    console.log('[bridge] ←', name, result)
    return result
  } catch (e) {
    console.error('[bridge] ✗', name, e?.error ?? e)
    throw e
  }
}
