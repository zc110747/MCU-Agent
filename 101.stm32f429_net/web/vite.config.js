import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// base: './' keeps all asset URLs relative so the dist/ folder can be
// served from any path (SD-card web server on the device) or opened
// directly via file://.
export default defineConfig({
  plugins: [vue()],
  base: './',
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    target: 'es2018'
  }
})
