import React from 'react'
import { createRoot } from 'react-dom/client'
import { createHashRouter, RouterProvider } from 'react-router-dom'
import './style.css'

// Pre-hydrate the appearance cache BEFORE React mounts so the first render
// already has the correct accent color, blur, radius, etc.  (loadAppearance
// fetches settings_get from native and calls applyDesign.)
import { loadAppearance, flushSettings } from './wallpaper'

// Settings writes are debounced (wallpaper.js schedulePersist, 400ms). If the
// user changes a design token (e.g. 主题色) and the app exits before the timer
// fires, the change would be lost on the next launch — flush any pending write
// when the page is being torn down. Best-effort: the bridge call is dispatched
// even though the response may never arrive.
window.addEventListener('beforeunload', () => { flushSettings() })

// Layout (sidebar + header + content) and route pages.
import AppLayout from './App.jsx'
import HomePage from './pages/HomePage.jsx'
import SettingsPage from './pages/SettingsPage.jsx'
import PluginsPage from './pages/PluginsPage.jsx'
import ProcessPage from './pages/ProcessPage.jsx'
import ToastContainer from './components/toast.jsx'

// Hash-based router: the app runs over both file:// (prod) and localhost
// (dev), where BrowserRouter would break on refresh / deep links.
const router = createHashRouter([
  {
    path: '/',
    element: <AppLayout />,
    children: [
      { index: true, element: <HomePage /> },
      { path: 'settings', element: <SettingsPage /> },
      { path: 'plugins', element: <PluginsPage /> },
      { path: 'process', element: <ProcessPage /> },
    ],
  },
])

// Hydrate design tokens (accent, blur, radius …) from native settings before
// the first paint, so the user never sees a flash of default blue.
loadAppearance().then(() => {
  createRoot(document.getElementById('root')).render(
    <React.StrictMode>
      <RouterProvider router={router} />
      <ToastContainer />
    </React.StrictMode>,
  )
})
