import './style.css'

// The HeliosView bridge, injected into every page:
//   const r = await window.helios.call('appInfo', {})  → Promise<...>
//   new BroadcastChannel('ping')                       → native ⇄ JS broadcasts

const app = document.querySelector('#app')

app.innerHTML = `
  <main>
    <h1>HeliosView + Vanilla JS</h1>
    <p>The native bridge <code>window.helios</code> is injected into every page — try it:</p>

    <div class="row">
      <input id="num1" type="number" value="0"/>
      <input id="num2" type="number" value="0"/>
      = <span id="result">0</span>
      <button id="add">Add</button>
    </div>

    <div class="row">
      <button id="appInfo">appInfo</button>
      <button id="ping">ping (worker round trip)</button>
    </div>

    <pre id="bridgeError" class="error" hidden></pre>
    <pre id="appInfoOut" hidden></pre>
    <pre id="pongOut" hidden></pre>

    <h2>BroadcastChannel 'ping' (native → page)</h2>
    <ul id="uiEvents"></ul>
  </main>
`

const $ = (sel) => app.querySelector(sel)

const appInfoOut = $('#appInfoOut')
const pongOut = $('#pongOut')
const bridgeError = $('#bridgeError')
const uiEvents = $('#uiEvents')
const num1 = $('#num1')
const num2 = $('#num2')
const result = $('#result')

function appendError(msg) {
  bridgeError.hidden = false
  bridgeError.textContent += String(msg)
}

$('#appInfo').addEventListener('click', async () => {
  bridgeError.hidden = true
  bridgeError.textContent = ''
  try {
    appInfoOut.hidden = false
    appInfoOut.textContent = JSON.stringify(await window.helios.call('appInfo', {}), null, 2)
  } catch (e) {
    appendError(e)
  }
})

$('#ping').addEventListener('click', async () => {
  // Native side: the handler hops onto the background pool (helios::Async,
  // asio-backed, v1.0.1) and broadcasts the result from the pool via the
  // thread-safe broadcast(); the same payload also arrives on the 'ping'
  // BroadcastChannel (see MainWindow::setupBridge in src/MainWindow.cpp).
  bridgeError.hidden = true
  bridgeError.textContent = ''
  try {
    pongOut.hidden = false
    pongOut.textContent = JSON.stringify(
      await window.helios.call('ping', { msg: 'hello from Vanilla JS' }),
      null,
      2,
    )
  } catch (e) {
    appendError(e)
  }
})

$('#add').addEventListener('click', async () => {
  try {
    result.textContent = await window.helios.call('add', Number(num1.value), Number(num2.value))
  } catch (e) {
    appendError(e)
  }
})

const channel = new BroadcastChannel('ping')
channel.addEventListener('message', (e) => {
  const li = document.createElement('li')
  li.textContent = JSON.stringify(e.data)
  uiEvents.appendChild(li)
})
