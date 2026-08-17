import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// Dev:  `npm run dev`  → http://localhost:5173 (HMR); the C++ app loads this
//       URL in dev builds (keep the port in sync with scripts/dev.cmd / dev.sh
//       and the HELIOSVIEW_TEMPLATE_DEV_URL CMake option).
// Prod: `npm run build` → frontend/dist with RELATIVE asset paths (base: './'),
//       so the built page works over file:// from exe-dir/assets.
export default defineConfig({
  plugins: [vue()],
  base: './',
  server: {
    port: 5173,
    strictPort: true,
  },
})
