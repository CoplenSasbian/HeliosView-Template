import React from 'react'
import { createRoot } from 'react-dom/client'
import { createHashRouter, RouterProvider } from 'react-router-dom'
import './style.css'

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

createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <RouterProvider router={router} />
    <ToastContainer />
  </React.StrictMode>,
)

