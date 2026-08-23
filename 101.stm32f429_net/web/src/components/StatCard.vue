<template>
  <div class="stat-card">
    <div class="stat-head">
      <div class="stat-icon" :style="iconStyle">
        <SvgIcon :name="icon" :size="18" />
      </div>
      <div class="stat-title">{{ title }}</div>
    </div>

    <div class="stat-body">
      <div class="stat-value">
        {{ value }}<span v-if="unit" class="stat-unit">{{ unit }}</span>
      </div>
      <div v-if="$slots.action" class="stat-action">
        <slot name="action" />
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import SvgIcon from './SvgIcon.vue'

const props = defineProps({
  title: { type: String, required: true },
  value: { type: [String, Number], default: '-' },
  unit: { type: String, default: '' },
  icon: { type: String, default: 'chip' },
  color: { type: String, default: '#1677ff' }
})

const iconStyle = computed(() => ({
  background: props.color + '1a',
  color: props.color
}))
</script>
