<template>
  <div>
    <!-- 状态概览卡片 -->
    <div class="stat-grid">
      <StatCard
        title="系统状态"
        :value="online ? '在线' : '离线'"
        icon="wifi"
        :color="online ? '#52c41a' : '#ff4d4f'"
      />
      <StatCard title="MCU" :value="hw.mcu || '-'" icon="mcu" color="#1677ff" />
      <StatCard title="主频" :value="hw.clock || '-'" icon="chip" color="#722ed1" />
      <StatCard
        title="光照"
        :value="fmt(hw.ap3216c?.lux)"
        unit="lux"
        icon="sun"
        color="#faad14"
      />
      <StatCard
        title="LED 指示灯"
        :value="hw.led ? '开启' : '关闭'"
        icon="led"
        :color="hw.led ? '#52c41a' : '#bfbfbf'"
      >
        <template #action>
          <div
            class="switch"
            :class="{ on: hw.led, disabled: busy }"
            @click="toggle('led')"
          >
            <span class="knob"></span>
          </div>
        </template>
      </StatCard>
      <StatCard
        title="蜂鸣器 BEEP"
        :value="hw.beep ? '开启' : '关闭'"
        icon="sound"
        :color="hw.beep ? '#52c41a' : '#bfbfbf'"
      >
        <template #action>
          <div
            class="switch"
            :class="{ on: hw.beep, disabled: busy }"
            @click="toggle('beep')"
          >
            <span class="knob"></span>
          </div>
        </template>
      </StatCard>
    </div>

    <!-- 运行状态明细 -->
    <div class="card">
      <h2 class="card-title">
        <SvgIcon name="dashboard" :size="18" /> 运行状态
      </h2>
      <p class="card-desc">设备核心运行状态汇总，每 2 秒自动刷新。</p>

      <div class="row">
        <span class="k">连接状态</span>
        <span class="v">
          <StatusDot :state="online ? 'online' : 'offline'" :label="online ? '已连接' : '离线'" />
        </span>
      </div>
      <div class="row">
        <span class="k">IP 地址</span>
        <span class="v">{{ net.ip || '-' }}</span>
      </div>
      <div class="row">
        <span class="k">MAC 地址</span>
        <span class="v">{{ net.mac || '-' }}</span>
      </div>
      <div class="row">
        <span class="k">LED 输出</span>
        <span class="v">
          <StatusDot :state="hw.led ? 'on' : 'off'" :label="hw.led ? 'ON' : 'OFF'" />
          <span class="switch" :class="{ on: hw.led, disabled: busy }" @click="toggle('led')">
            <span class="knob"></span>
          </span>
        </span>
      </div>
      <div class="row">
        <span class="k">蜂鸣器</span>
        <span class="v">
          <StatusDot :state="hw.beep ? 'on' : 'off'" :label="hw.beep ? 'ON' : 'OFF'" />
          <span class="switch" :class="{ on: hw.beep, disabled: busy }" @click="toggle('beep')">
            <span class="knob"></span>
          </span>
        </span>
      </div>
    </div>

    <div v-if="!online" class="msg err">设备离线，请检查网络连接或刷新页面…</div>
    <div v-else-if="msg" class="msg" :class="{ ok: msgOk, err: !msgOk }">{{ msg }}</div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { getHardware, getNetwork, controlDevice } from '../api.js'
import StatCard from '../components/StatCard.vue'
import StatusDot from '../components/StatusDot.vue'
import SvgIcon from '../components/SvgIcon.vue'

const hw = ref({})
const net = ref({})
const online = ref(false)
const busy = ref(false)
const msg = ref('')
const msgOk = ref(true)

let timer = null

function fmt(v) {
  return v === undefined || v === null ? '-' : String(v)
}

async function refresh() {
  try {
    const [h, n] = await Promise.all([getHardware(), getNetwork()])
    hw.value = h
    net.value = n
    online.value = true
  } catch (_) {
    online.value = false
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
  timer = setInterval(refresh, 2000)
})
onUnmounted(() => clearInterval(timer))
</script>
