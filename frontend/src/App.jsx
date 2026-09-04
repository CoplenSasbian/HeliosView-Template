// AppLayout — the app shell: sidebar navigation + header + routed content.
// Uses react-router (<Outlet/> + <NavLink/>) so navigation is URL-driven
// (HashRouter) instead of a hand-rolled NAV state machine.

import {NavLink, Outlet, useLocation} from 'react-router-dom'
import {useEffect, useState, useRef, useCallback} from 'react'
import {IconHome, IconSettings, IconPlugin, IconMonitor, IconLogo} from './ui'
import {HeliosTitleBar, HeliosWindowControls} from './ui'
import Tooltip from './ui/Tooltip.jsx'
import { AppProviders, useConfig } from './context'


// Route metadata (label + icon). URL paths are defined in main.jsx; keep the
// two in sync.
const NAV_ITEMS = [
    {to: '/', label: '主页', icon: IconHome},
    {to: '/settings', label: '应用设置', icon: IconSettings},
    {to: '/plugins', label: '插件', icon: IconPlugin},
    {to: '/process', label: '进程监控', icon: IconMonitor},
]

// Live sidebar status: shows the currently active config.
function SidebarStatus() {
    const {activeConfig} = useConfig()
    const isOff = !activeConfig || activeConfig === 'close'
    return (
        <div className="sidebar-foot__status">
            <span className={`status-dot${isOff ? ' is-off' : ''}`}/>
            配置：{isOff ? '关闭' : activeConfig}
        </div>
    )
}

// Sidebar nav entry — a NavLink with an exit choreography. When an item stops
// being the active page it keeps a short-lived is-leaving class so the fill
// block can animate "pill → hover bar → gone" (see .is-leaving in style.css)
// instead of the pill vanishing abruptly the instant the route changes.
function SidebarNavItem({to, label, icon: Icon}) {
    const {pathname} = useLocation()
    // '/' owns only the exact index path; the others use prefix match
    const isActive = to === '/' ? pathname === '/' : pathname.startsWith(to)
    const [leaving, setLeaving] = useState(false)
    const wasActive = useRef(isActive)
    const timer = useRef(null)

    useEffect(() => {
        clearTimeout(timer.current)
        if (wasActive.current && !isActive) {
            setLeaving(true)
            timer.current = setTimeout(() => setLeaving(false), 620)
        } else {
            setLeaving(false)
        }
        wasActive.current = isActive
        return () => clearTimeout(timer.current)
    }, [isActive])

    return (
        <NavLink
            to={to}
            end={to === '/'}
            className={`sidebar-nav__item${isActive ? ' is-active' : ''}${leaving ? ' is-leaving' : ''}`}
        >
            <Tooltip text={label}>
                <span className="sidebar-nav__icon">
                    <Icon width={18} height={18}/>
                </span>
            </Tooltip>
            <span className="sidebar-nav__label">{label}</span>
        </NavLink>
    )
}

export default function AppLayout() {
    const {pathname} = useLocation()
    // The "/" route only owns the exact index path; the others use prefix match.
    const current =
        NAV_ITEMS.find((n) => (n.to === '/' ? pathname === '/' : pathname.startsWith(n.to))) ??
        NAV_ITEMS[0]

    // Zoom tracking: Ctrl+scroll changes zoom; show a reset button when ≠ 100%.
    const [zoom, setZoom] = useState(1)
    const handleWheel = useCallback((e) => {
        if (!e.ctrlKey) return
        e.preventDefault()
        setZoom((prev) => {
            const next = Math.min(3, Math.max(0.5, prev + (e.deltaY < 0 ? 0.1 : -0.1)))
            document.documentElement.style.zoom = String(next)
            return next
        })
    }, [])
    const resetZoom = useCallback(() => {
        document.documentElement.style.zoom = '1'
        setZoom(1)
    }, [])

    useEffect(() => {
        window.addEventListener('wheel', handleWheel, { passive: false })
        return () => window.removeEventListener('wheel', handleWheel)
    }, [handleWheel])

    return (
        <AppProviders>
            <div className="app">
                <aside className="sidebar has-noise">
                    <div className="sidebar-brand">
                        <div className="sidebar-brand__dot">
                            <IconLogo width={20} height={20}/>
                        </div>
                        <div className="sidebar-brand__text">
                            <div className="sidebar-brand__name">GameTrigger</div>
                            <div className="sidebar-brand__sub">进程触发与监控</div>
                        </div>
                    </div>

                    <nav className="sidebar-nav">
                        {NAV_ITEMS.map((item) => (
                            <SidebarNavItem key={item.to} {...item}/>
                        ))}
                    </nav>

                    <div className="sidebar-foot">
                        <SidebarStatus/>
                        {Math.round(zoom * 100) !== 100 && (
                            <button
                                type="button"
                                className="zoom-reset"
                                onClick={resetZoom}
                                title="重置缩放到 100%"
                            >
                                {Math.round(zoom * 100)}% · 点击恢复
                            </button>
                        )}
                    </div>
                </aside>

                <div className="app-main">
                    <HeliosTitleBar className="header">
                        <div className="header__title" key={current?.label ?? 'home'}>
                            {current?.label ?? '主页'}
                        </div>
                        <div className="header__actions"/>
                        <HeliosWindowControls/>
                    </HeliosTitleBar>

                    <div className="app-scroll">
                        <main className="app-content">
                            <div className="route-view" key={pathname}>
                                <Outlet/>
                            </div>
                        </main>
                    </div>
                </div>
            </div>
        </AppProviders>
    )
}
