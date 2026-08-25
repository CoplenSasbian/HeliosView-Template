import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// Dev:  `npm run dev`  → http://localhost:5173 (HMR); the C++ app loads this
//       URL in dev builds (keep the port in sync with scripts/dev.cmd / dev.sh
//       and the HELIOSVIEW_TEMPLATE_DEV_URL CMake option).
// Prod: `npm run build` → frontend/dist with RELATIVE asset paths (base: './'),
//       so the page works no matter which folder the app serves it from (the
//       app root's assets\, i.e. ../assets relative to the exe in bin\).
//       assetsDir: '.' flattens the bundles next to index.html — no
//       assets\assets nesting in the deployed folder.
export default defineConfig({
  plugins: [react()],
  base: './',
  build: {
    assetsDir: '.',
  },
  server: {
    port: 5173,
    strictPort: true,
  },
})
