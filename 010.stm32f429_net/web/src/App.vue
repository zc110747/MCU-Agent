<template>
  <div class="shell">
    <header>
      <h1>STM32F429 设备控制台</h1>
      <nav>
        <button
          v-for="t in tabs"
          :key="t.id"
          class="tab"
          :class="{ active: tab === t.id }"
          @click="tab = t.id"
        >{{ t.name }}</button>
      </nav>
    </header>

    <HardwareView v-if="tab === 'hw'" />
    <NetworkView v-else-if="tab === 'net'" />
    <SettingsView v-else />
  </div>
</template>

<script setup>
import { ref } from 'vue'
import HardwareView from './views/HardwareView.vue'
import NetworkView from './views/NetworkView.vue'
import SettingsView from './views/SettingsView.vue'

const tab = ref('hw')

const tabs = [
  { id: 'hw', name: '硬件数据' },
  { id: 'net', name: '网络数据' },
  { id: 'cfg', name: '参数修改' }
]
</script>

<style scoped>
.shell {
  max-width: 640px;
  margin: 0 auto;
  padding: 32px 16px 48px;
}

header h1 {
  font-size: 22px;
  margin: 0 0 16px;
}

nav {
  display: flex;
  gap: 8px;
  margin-bottom: 20px;
}

.tab {
  background: var(--card);
  color: var(--text-dim);
  border: none;
  border-radius: 6px;
  padding: 8px 20px;
  font-size: 14px;
  cursor: pointer;
}

.tab.active {
  background: var(--card-2);
  color: var(--text);
}

h3 {
  margin: 18px 0 4px;
  font-size: 15px;
  color: var(--text-dim);
}
</style>
