import { useEffect, useState } from 'react'

// The HeliosView bridge, injected into every page:
//   const r = await window.helios.call('appInfo', {})  → Promise<...>
//   new BroadcastChannel('ping')                       → native ⇄ JS broadcasts

export default function App() {
  const [appInfo, setAppInfo] = useState(null)
  const [pong, setPong] = useState(null)
  const [uiEvents, setUiEvents] = useState([])
  const [bridgeError, setBridgeError] = useState(null)
  const [num1, setNum1] = useState(0)
  const [num2, setNum2] = useState(0)
  const [result, setResult] = useState(0)

  const appendError = (msg) => setBridgeError((prev) => (prev ?? '') + String(msg))

  async function fetchAppInfo() {
    setBridgeError(null)
    try {
      setAppInfo(await window.helios.call('appInfo', {}))
    } catch (e) {
      setBridgeError(String(e))
    }
  }

  async function ping() {
    // Native side: the handler hops off the UI thread onto the app's background
    // pool (AppContext::async(), helios::Async) and broadcasts the result from
    // the pool thread via the thread-safe broadcast(); the same payload also
    // arrives on the 'ping' BroadcastChannel (see MainWindow::setupBridge in
    // src/MainWindow.cpp).
    setBridgeError(null)
    try {
      setPong(await window.helios.call('ping', { msg: 'hello from React' }))
    } catch (e) {
      appendError(e)
    }
  }

  async function add() {
    try {
      setResult(await window.helios.call('add', Number(num1), Number(num2)))
    } catch (e) {
      appendError(e)
    }
  }

  useEffect(() => {
    const channel = new BroadcastChannel('ping')
    channel.addEventListener('message', (e) =>
      setUiEvents((prev) => [...prev, e.data]),
    )
    return () => channel.close()
  }, [])

  return (
    <main>
      <h1>HeliosView + React</h1>
      <p>
        The native bridge <code>window.helios</code> is injected into every
        page — try it:
      </p>

      <div className="row">
        <input
          type="number"
          value={num1}
          onChange={(e) => setNum1(e.target.value)}
        />
        <input
          type="number"
          value={num2}
          onChange={(e) => setNum2(e.target.value)}
        />
        = <span>{result}</span>
        <button onClick={add}>Add</button>
      </div>

      <div className="row">
        <button onClick={fetchAppInfo}>appInfo</button>
        <button onClick={ping}>ping (worker round trip)</button>
      </div>

      {bridgeError && <pre className="error">{bridgeError}</pre>}
      {appInfo && <pre>{JSON.stringify(appInfo, null, 2)}</pre>}
      {pong && <pre>{JSON.stringify(pong, null, 2)}</pre>}

      <h2>BroadcastChannel &apos;ping&apos; (native → page)</h2>
      <ul>
        {uiEvents.map((e, i) => (
          <li key={i}>{JSON.stringify(e)}</li>
        ))}
      </ul>

      <footer>
        <p>
          Built on{' '}
          <a href="https://github.com/CoplenSasbian/HeliosView" target="_blank" rel="noreferrer">
            HeliosView
          </a>{' '}
          — a C++ WebView windowing library (WebView2 + native ⇄ JS bridge).
          This page is the React template of{' '}
          <a href="https://github.com/CoplenSasbian/HeliosView-Template" target="_blank" rel="noreferrer">
            HeliosView-Template
          </a>
          .
        </p>
      </footer>
    </main>
  )
}
