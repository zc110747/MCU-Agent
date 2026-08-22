<template>
  <div class="layout">
    <!-- Sidebar -->
    <aside class="sider">
      <div class="sider-logo">
        <svg class="logo-mark" viewBox="0 0 1024 1024" fill="#1677ff">
          <path
            d="M512 64l384 224v448L512 960 128 736V288L512 64zm0 96L224 288v352l288 166 288-166V288L512 160zM352 416h320v192H352V416zm64 64v64h192v-64H416z"
          />
        </svg>
        <span>嵌入式Web管理系统</span>
      </div>

      <nav class="menu">
        <div
          v-for="m in menus"
          :key="m.id"
          class="menu-item"
          :class="{ active: active === m.id }"
          @click="active = m.id"
        >
          <SvgIcon :name="m.icon" :size="16" />
          <span>{{ m.name }}</span>
        </div>
      </nav>
    </aside>

    <!-- Main -->
    <div class="main">
      <header class="header">
        <span>{{ currentTitle }}</span>
        <span class="sub">{{ currentSub }}</span>
      </header>

      <main class="content">
        <DashboardView v-if="active === 'dashboard'" />
        <HardwareView v-else-if="active === 'hardware'" />
        <NetworkView v-else-if="active === 'network'" />
        <SettingsView v-else />
      </main>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import SvgIcon from './components/SvgIcon.vue'
import DashboardView from './views/DashboardView.vue'
import HardwareView from './views/HardwareView.vue'
import NetworkView from './views/NetworkView.vue'
import SettingsView from './views/SettingsView.vue'

const active = ref('dashboard')

const menus = [
  { id: 'dashboard', name: '系统概览', icon: 'dashboard', title: '系统概览', sub: 'System Overview' },
  { id: 'hardware', name: '硬件监控', icon: 'chip', title: '硬件监控', sub: 'Sensors & I/O' },
  { id: 'network', name: '网络状态', icon: 'network', title: '网络状态', sub: 'Network Status' },
  { id: 'cfg', name: '参数设置', icon: 'setting', title: '参数设置', sub: 'Configuration' }
]

const current = computed(() => menus.find((m) => m.id === active.value) || menus[0])
const currentTitle = computed(() => current.value.title)
const currentSub = computed(() => current.value.sub)
</script>
