<template>
  <div>
    <!-- 状态卡 -->
    <div class="stat-grid">
      <StatCard title="连接状态" :value="online ? '已连接' : '离线'" icon="wifi"
        :color="online ? '#52c41a' : '#ff4d4f'" />
      <StatCard title="IP 地址" :value="net.ip || '-'" icon="network" color="#1677ff" />
      <StatCard title="网关" :value="net.gw || '-'" icon="mcu" color="#722ed1" />
      <StatCard title="MAC" :value="net.mac || '-'" icon="chip" color="#13c2c2" />
    </div>

    <div class="card">
      <h2 class="card-title"><SvgIcon name="network" :size="18" /> 网络参数</h2>
      <p class="card-desc">当前生效的网络配置，每 2 秒自动刷新。</p>

      <div class="row">
        <span class="k">连接状态</span>
        <span class="v">
          <StatusDot :state="online ? 'online' : 'offline'" :label="online ? '已连接' : '离线'" />
        </span>
      </div>
      <div class="row"><span class="k">IP 地址</span><span class="v">{{ net.ip || '-' }}</span></div>
      <div class="row"><span class="k">子网掩码</span><span class="v">{{ net.mask || '-' }}</span></div>
      <div class="row"><span class="k">网关</span><span class="v">{{ net.gw || '-' }}</span></div>
      <div class="row"><span class="k">MAC 地址</span><span class="v">{{ net.mac || '-' }}</span></div>
    </div>

    <div v-if="!online" class="msg err">设备离线，等待重连…</div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { getNetwork } from '../api.js'
import StatCard from '../components/StatCard.vue'
import StatusDot from '../components/StatusDot.vue'
import SvgIcon from '../components/SvgIcon.vue'

const net = ref({})
const online = ref(false)

let timer = null

async function refresh() {
  try {
    net.value = await getNetwork()
    online.value = true
  } catch (_) {
    online.value = false
  }
}

onMounted(() => {
  refresh()
  timer = setInterval(refresh, 2000)
})
onUnmounted(() => clearInterval(timer))
</script>
