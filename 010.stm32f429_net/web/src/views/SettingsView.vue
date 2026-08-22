<template>
  <div class="card">
    <h2>设备参数修改</h2>

    <div class="row">
      <span class="k">IP 地址</span>
      <span class="v">
        <input v-model.trim="form.ip" placeholder="192.168.10.99">
      </span>
    </div>
    <div class="row">
      <span class="k">子网掩码</span>
      <span class="v">
        <input v-model.trim="form.mask" placeholder="255.255.255.0">
      </span>
    </div>
    <div class="row">
      <span class="k">网关</span>
      <span class="v">
        <input v-model.trim="form.gw" placeholder="192.168.10.1">
      </span>
    </div>
    <div class="row">
      <span class="k">MAC 地址</span>
      <span class="v">
        <input v-model.trim="form.mac" placeholder="00:80:E1:xx:xx:xx">
      </span>
    </div>

    <div style="margin-top:16px" class="btn-group">
      <button class="btn" :disabled="busy" @click="apply">应用修改</button>
      <button class="btn danger" :disabled="busy" @click="doReset">复位设备</button>
    </div>

    <div v-if="msg" class="msg" :class="{ ok: msgOk, err: !msgOk }">{{ msg }}</div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { getNetwork, setNetwork, resetDevice } from '../api.js'

const form = ref({ ip: '', mask: '', gw: '', mac: '' })
const msg = ref('')
const msgOk = ref(true)
const busy = ref(false)

const IP_RE = /^(\d{1,3}\.){3}\d{1,3}$/
const MAC_RE = /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/

function show(m, ok) {
  msg.value = m
  msgOk.value = ok
}

async function load() {
  try {
    const net = await getNetwork()
    form.value = {
      ip: net.ip || '',
      mask: net.mask || '',
      gw: net.gw || '',
      mac: net.mac || ''
    }
  } catch (e) {
    show(`读取当前参数失败：${e.message}`, false)
  }
}

async function apply() {
  if (!IP_RE.test(form.value.ip)) return show('IP 地址格式无效', false)
  if (!IP_RE.test(form.value.mask)) return show('子网掩码格式无效', false)
  if (!IP_RE.test(form.value.gw)) return show('网关格式无效', false)
  if (!MAC_RE.test(form.value.mac)) return show('MAC 地址格式无效（需 00:11:22:33:44:55）', false)

  busy.value = true
  msg.value = ''
  try {
    await setNetwork({
      ip: form.value.ip,
      mask: form.value.mask,
      gw: form.value.gw,
      mac: form.value.mac
    })
    show('参数已保存，设备将在重启后生效（或立即生效）', true)
  } catch (e) {
    show(`保存失败：${e.message}`, false)
  } finally {
    busy.value = false
  }
}

async function doReset() {
  if (!confirm('确认复位设备？修改的网络参数将生效。')) return
  busy.value = true
  msg.value = ''
  try {
    await resetDevice()
    show('复位指令已发送，设备重启中…', true)
    /* the device reboots; poll until it comes back */
    const wait = setInterval(async () => {
      try {
        await getNetwork()
        clearInterval(wait)
        show('设备已重启并恢复在线', true)
        await load()
      } catch (_) { /* still offline */ }
    }, 2000)
  } catch (e) {
    show(`复位失败：${e.message}`, false)
    busy.value = false
  }
}

onMounted(load)
</script>
