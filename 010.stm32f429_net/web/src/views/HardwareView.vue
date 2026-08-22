<template>
  <div>
    <!-- 传感器统计卡 -->
    <div class="stat-grid">
      <StatCard title="光照强度" :value="fmt(hw.ap3216c?.lux)" unit="lux" icon="sun" color="#faad14" />
      <StatCard title="接近距离" :value="fmt(hw.ap3216c?.ps)" unit="ps" icon="led" color="#13c2c2" />
      <StatCard title="红外值" :value="fmt(hw.ap3216c?.ir)" unit="ir" icon="chip" color="#722ed1" />
      <StatCard title="加速度 Z" :value="fmt(hw.mpu9250?.az)" unit="g" icon="gyro" color="#1677ff" />
    </div>

    <!-- AP3216C -->
    <div class="card">
      <h2 class="card-title"><SvgIcon name="sun" :size="18" /> AP3216C 环境传感器</h2>
      <p class="card-desc">环境光 / 接近 / 红外，每 1 秒自动刷新。</p>
      <div class="row"><span class="k">光照强度 (lux)</span><span class="v">{{ fmt(hw.ap3216c?.lux) }}</span></div>
      <div class="row"><span class="k">接近距离 (ps)</span><span class="v">{{ fmt(hw.ap3216c?.ps) }}</span></div>
      <div class="row"><span class="k">红外 (ir)</span><span class="v">{{ fmt(hw.ap3216c?.ir) }}</span></div>
    </div>

    <!-- MPU9250 -->
    <div class="card">
      <h2 class="card-title"><SvgIcon name="gyro" :size="18" /> MPU9250 九轴传感器</h2>
      <p class="card-desc">加速度 / 陀螺仪 / 磁力计（单位：g, °/s, µT）。</p>
      <div class="row"><span class="k">加速度 (g) ax/ay/az</span><span class="v">{{ vec(hw.mpu9250, 'a') }}</span></div>
      <div class="row"><span class="k">陀螺仪 (°/s) gx/gy/gz</span><span class="v">{{ vec(hw.mpu9250, 'g') }}</span></div>
      <div class="row"><span class="k">磁力计 (µT) mx/my/mz</span><span class="v">{{ vec(hw.mpu9250, 'm') }}</span></div>
    </div>

    <!-- 输出控制 -->
    <div class="card">
      <h2 class="card-title"><SvgIcon name="setting" :size="18" /> 输出控制</h2>
      <p class="card-desc">点击开关切换 LED / 蜂鸣器状态。</p>

      <div class="row">
        <span class="k">LED 指示灯</span>
        <span class="btn-group">
          <StatusDot :state="hw.led ? 'on' : 'off'" :label="hw.led ? 'ON' : 'OFF'" />
          <div class="switch" :class="{ on: hw.led }" @click="toggle('led')">
            <span class="knob"></span>
          </div>
        </span>
      </div>
      <div class="row">
        <span class="k">蜂鸣器 BEEP</span>
        <span class="btn-group">
          <StatusDot :state="hw.beep ? 'on' : 'off'" :label="hw.beep ? 'ON' : 'OFF'" />
          <div class="switch" :class="{ on: hw.beep }" @click="toggle('beep')">
            <span class="knob"></span>
          </div>
        </span>
      </div>

      <div v-if="msg" class="msg" :class="{ ok: msgOk, err: !msgOk }">{{ msg }}</div>
      <div v-else class="msg">{{ offline ? '设备离线，等待重连…' : '数据每 1s 自动刷新' }}</div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { getHardware, controlDevice } from '../api.js'
import StatCard from '../components/StatCard.vue'
import StatusDot from '../components/StatusDot.vue'
import SvgIcon from '../components/SvgIcon.vue'

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
    hw.value = await getHardware()
    offline.value = false
  } catch (_) {
    offline.value = true
    msg.value = ''
  }
}

async function toggle(which) {
  if (busy.value) return
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
