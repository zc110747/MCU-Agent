<template>
  <div class="card">
    <h2>设备硬件数据</h2>

    <div class="row">
      <span class="k">MCU 型号</span>
      <span class="v">{{ hw.mcu || '-' }}</span>
    </div>
    <div class="row">
      <span class="k">时钟频率</span>
      <span class="v">{{ hw.clock || '-' }}</span>
    </div>

    <h3>AP3216C 环境光传感器</h3>
    <div class="row">
      <span class="k">光照强度 (lux)</span>
      <span class="v">{{ fmt(hw.ap3216c?.lux) }}</span>
    </div>
    <div class="row">
      <span class="k">接近距离 (ps)</span>
      <span class="v">{{ fmt(hw.ap3216c?.ps) }}</span>
    </div>
    <div class="row">
      <span class="k">红外 (ir)</span>
      <span class="v">{{ fmt(hw.ap3216c?.ir) }}</span>
    </div>

    <h3>MPU9250 九轴传感器</h3>
    <div class="row">
      <span class="k">加速度 (g)</span>
      <span class="v">{{ vec(hw.mpu9250, 'a') }}</span>
    </div>
    <div class="row">
      <span class="k">陀螺仪 (°/s)</span>
      <span class="v">{{ vec(hw.mpu9250, 'g') }}</span>
    </div>
    <div class="row">
      <span class="k">磁力计 (µT)</span>
      <span class="v">{{ vec(hw.mpu9250, 'm') }}</span>
    </div>

    <h3>输出控制</h3>
    <div class="row">
      <span class="k">LED</span>
      <span class="v">{{ hw.led ? 'ON' : 'OFF' }}</span>
    </div>
    <div class="row">
      <span class="k">BEEP</span>
      <span class="v">{{ hw.beep ? 'ON' : 'OFF' }}</span>
    </div>
    <div class="row">
      <span class="k">操作</span>
      <span class="btn-group">
        <button class="btn" :disabled="busy" @click="toggle('led')">
          {{ hw.led ? '关闭 LED' : '打开 LED' }}
        </button>
        <button class="btn" :disabled="busy" @click="toggle('beep')">
          {{ hw.beep ? '关闭 BEEP' : '打开 BEEP' }}
        </button>
      </span>
    </div>

    <div v-if="msg" class="msg" :class="{ ok: msgOk, err: !msgOk }">{{ msg }}</div>
    <div v-else class="msg">{{ offline ? '设备离线，等待重连…' : '数据每 1s 自动刷新' }}</div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { getHardware, controlDevice } from '../api.js'

const hw = ref({})
const msg = ref('')
const msgOk = ref(true)
const busy = ref(false)
const offline = ref(false)

let timer = null

function fmt(v) {
  return v === undefined || v === null ? '-' : String(v)
}

function vec(s, p) {
  if (!s) return '-'
  const x = s[p + 'x'], y = s[p + 'y'], z = s[p + 'z']
  if (x === undefined) return '-'
  return `${x} / ${y} / ${z}`
}

async function refresh() {
  try {
    const data = await getHardware()
    hw.value = data
    offline.value = false
  } catch (e) {
    offline.value = true
    msg.value = ''
  }
}

async function toggle(which) {
  busy.value = true
  msg.value = ''
  try {
    const next = which === 'led' ? (hw.value.led ? 0 : 1) : (hw.value.beep ? 0 : 1)
    await controlDevice({ [which]: next })
    await refresh()
    msg.value = `${which.toUpperCase()} 已${next ? '打开' : '关闭'}`
    msgOk.value = true
  } catch (e) {
    msg.value = `操作失败：${e.message}`
    msgOk.value = false
  } finally {
    busy.value = false
  }
}

onMounted(() => {
  refresh()
  timer = setInterval(refresh, 1000)
})
onUnmounted(() => clearInterval(timer))
</script>
