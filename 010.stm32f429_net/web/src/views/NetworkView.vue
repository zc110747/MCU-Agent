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
      <h2 class="card-title"><SvgIcon name="network" :size="18" /> 网络参数设置</h2>
      <p class="card-desc">
        修改后写入 EEPROM，<b>重启设备后生效</b>。提交前需通过格式校验，不合规无法写入。
      </p>

      <div class="form">
        <div class="field">
          <label>IP 地址</label>
          <input v-model="form.ip" placeholder="192.168.10.99" />
          <span class="err" v-if="errors.ip">{{ errors.ip }}</span>
        </div>
        <div class="field">
          <label>子网掩码</label>
          <input v-model="form.mask" placeholder="255.255.255.0" />
          <span class="err" v-if="errors.mask">{{ errors.mask }}</span>
        </div>
        <div class="field">
          <label>网关</label>
          <input v-model="form.gw" placeholder="192.168.10.1" />
          <span class="err" v-if="errors.gw">{{ errors.gw }}</span>
        </div>
        <div class="field">
          <label>MAC 地址</label>
          <input v-model="form.mac" placeholder="00:80:E1:00:00:00" />
          <span class="err" v-if="errors.mac">{{ errors.mac }}</span>
        </div>
      </div>

      <div class="actions">
        <button class="btn primary" :disabled="!canSubmit" @click="onSubmit">保存并写入 EEPROM</button>
        <button class="btn" @click="onReset">恢复当前值</button>
      </div>

      <div class="msg ok" v-if="msg.ok">已写入 EEPROM，请重启设备使其生效。</div>
      <div class="msg err" v-if="msg.err">{{ msg.err }}</div>
    </div>

    <div v-if="!online" class="msg err">设备离线，等待重连…</div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, onUnmounted, computed } from 'vue'
import { getNetwork, setNetwork } from '../api.js'
import StatCard from '../components/StatCard.vue'
import StatusDot from '../components/StatusDot.vue'
import SvgIcon from '../components/SvgIcon.vue'

const net = ref({})
const online = ref(false)

const form = reactive({ ip: '', mask: '', gw: '', mac: '' })
const errors = reactive({ ip: '', mask: '', gw: '', mac: '' })
const msg = reactive({ ok: false, err: '' })

let timer = null

/* ---- validators (must mirror firmware-side checks) ---- */
const RE_IP = /^((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$/
const RE_MAC = /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/

function isContiguousMask(v) {
  const m = v.split('.').map(Number)
  const bin = m.map((x) => x.toString(2).padStart(8, '0')).join('')
  return /^1*0*$/.test(bin)
}

function validate() {
  errors.ip = RE_IP.test(form.ip.trim())
    ? '' : 'IP 格式不合法 (0-255.0-255.0-255.0-255)'
  errors.mask = (RE_IP.test(form.mask.trim()) && isContiguousMask(form.mask.trim()))
    ? '' : '子网掩码不合法 (需连续 1 后接连续 0)'
  errors.gw = RE_IP.test(form.gw.trim())
    ? '' : '网关格式不合法'
  errors.mac = RE_MAC.test(form.mac.trim())
    ? '' : 'MAC 格式不合法 (XX:XX:XX:XX:XX:XX)'
  return !errors.ip && !errors.mask && !errors.gw && !errors.mac
}

const canSubmit = computed(() => validate())

async function refresh() {
  try {
    const n = await getNetwork()
    net.value = n
    /* only fill the form on first load / when user hasn't edited */
    if (!form.ip) {
      form.ip = n.ip || ''
      form.mask = n.mask || ''
      form.gw = n.gw || ''
      form.mac = n.mac || ''
    }
    online.value = true
  } catch (_) {
    online.value = false
  }
}

function onReset() {
  form.ip = net.value.ip || ''
  form.mask = net.value.mask || ''
  form.gw = net.value.gw || ''
  form.mac = net.value.mac || ''
  msg.ok = false
  msg.err = ''
}

async function onSubmit() {
  msg.ok = false
  msg.err = ''
  if (!validate()) {
    msg.err = '存在不合法的字段，请修正后再提交。'
    return
  }
  try {
    await setNetwork({
      ip: form.ip.trim(),
      mask: form.mask.trim(),
      gw: form.gw.trim(),
      mac: form.mac.trim().toUpperCase(),
    })
    msg.ok = true
  } catch (e) {
    msg.err = '写入失败: ' + (e.message || e)
  }
}

onMounted(() => {
  refresh()
  timer = setInterval(refresh, 2000)
})
onUnmounted(() => clearInterval(timer))
</script>

<style scoped>
.form { display: flex; flex-direction: column; gap: 14px; margin: 16px 0; }
.field { display: flex; flex-direction: column; gap: 6px; }
.field label { font-size: 13px; color: #9aa0a6; }
.field input {
  background: #1e1e1e; color: #e8eaed; border: 1px solid #333;
  border-radius: 6px; padding: 8px 10px; font-size: 14px; outline: none;
}
.field input:focus { border-color: #1677ff; }
.err { color: #ff7875; font-size: 12px; }
.actions { display: flex; gap: 12px; margin-top: 8px; }
.btn {
  background: #2a2a2a; color: #e8eaed; border: 1px solid #3a3a3a;
  border-radius: 6px; padding: 8px 16px; font-size: 14px; cursor: pointer;
}
.btn.primary { background: #1677ff; border-color: #1677ff; color: #fff; }
.btn:disabled { opacity: .45; cursor: not-allowed; }
.msg { margin-top: 12px; padding: 8px 12px; border-radius: 6px; font-size: 13px; }
.msg.ok { background: rgba(82,196,26,.12); color: #52c41a; }
.msg.err { background: rgba(255,77,79,.12); color: #ff7875; }
</style>
