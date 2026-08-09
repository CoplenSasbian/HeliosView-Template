<script setup>
import { onMounted, ref } from 'vue'

// The HeliosView bridge, injected into every page:
//   const r = await window.helios.call('appInfo', {})  → Promise<...>
//   new BroadcastChannel('ping')                       → native ⇄ JS broadcasts
// (TS templates scaffolded with scripts/setup.ps1 also get src/helios.d.ts)

const appInfo = ref(null)
const pong = ref(null)
const uiEvents = ref([])
const bridgeError = ref(null)
const num1 = ref(0)
const num2 = ref(0)
const result = ref(0)
async function fetchAppInfo() {
  bridgeError.value = null
  try {
    appInfo.value = await window.helios.call('appInfo', {})
  } catch (e) {
    bridgeError.value = String(e)
  }
}

function appendError(msg){
  bridgeError.value =  bridgeError.value + String(msg)
}

async function ping() {
  // Native side: hops to the thread pool, then back to the UI loop; the same
  // payload also arrives on the 'ping' BroadcastChannel, posted from the UI
  // thread (see MainWindow::setupBridge in src/MainWindow.cpp).
  bridgeError.value = null
  try {
    pong.value = await window.helios.call('ping', { msg: 'hello from Vue' })
  } catch (e) {
    appendError(e)
  }
}

async function add(){
  try{
    result.value = await  window.helios.call('add',num1.value,num2.value)
  }catch (e){
    appendError(e)
  }
}

onMounted(() => {
  const channel = new BroadcastChannel('ping')
  channel.addEventListener('message', (e) => uiEvents.value.push(e.data))
})
</script>

<template>
  <main>
    <h1>HeliosView + Vue</h1>
    <p>The native bridge <code>window.helios</code> is injected into every page — try it:</p>

    <div class="row">
      <input type="number" v-model="num1"/>
      <input type="number" v-model="num2" />
      = <span>{{result}}</span>
      <button @click="add">Add</button>
    </div>


    <div class="row">
      <button @click="fetchAppInfo">appInfo</button>
      <button @click="ping">ping (thread-pool round trip)</button>
    </div>



    <pre v-if="bridgeError" class="error">{{ bridgeError }}</pre>
    <pre v-if="appInfo">{{ JSON.stringify(appInfo, null, 2) }}</pre>
    <pre v-if="pong">{{ JSON.stringify(pong, null, 2) }}</pre>

    <h2>BroadcastChannel 'ping' (native → page)</h2>
    <ul>
      <li v-for="(e, i) in uiEvents" :key="i">{{ JSON.stringify(e) }}</li>
    </ul>
  </main>
</template>
