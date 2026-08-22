<template>
  <span class="status-dot">
    <span class="dot" :class="state"></span>
    <span>{{ label }}</span>
  </span>
</template>

<script setup>
defineProps({
  state: { type: String, default: 'off' }, // on | off | online | offline | warn
  label: { type: String, default: '' }
})
</script>
