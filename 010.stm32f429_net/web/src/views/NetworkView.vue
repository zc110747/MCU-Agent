<template>
  <div class="card">
    <h2>设备网络数据</h2>

    <div class="row">
      <span class="k">IP 地址</span>
      <span class="v">{{ net.ip || '-' }}</span>
    </div>
    <div class="row">
      <span class="k">子网掩码</span>
      <span class="v">{{ net.mask || '-' }}</span>
    </div>
    <div class="row">
      <span class="k">网关</span>
      <span class="v">{{ net.gw || '-' }}</span>
    </div>
    <div class="row">
      <span class="k">MAC 地址</span>
      <span class="v">{{ net.mac || '-' }}</span>
    </div>

    <div class="msg" :class="{ err: offline }">
      {{ offline ? '设备离线，等待重连…' : '数据每 2s 自动刷新' }}
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { getNetwork } from '../api.js'

const net = ref({})
const offline = ref(false)

let timer = null

async function refresh() {
  try {
    net.value = await getNetwork()
    offline.value = false
  } catch (_) {
    offline.value = true
  }
}

onMounted(() => {
  refresh()
  timer = setInterval(refresh, 2000)
})
onUnmounted(() => clearInterval(timer))
</script>
