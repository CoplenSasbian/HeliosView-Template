// SettingsPage — app settings: general behaviors (auto-start)
// and appearance (theme, background, design). Settings come from
// SettingsContext; appearance state from AppearanceContext.
// Note: config management (create/delete/edit/activate) deliberately lives
// elsewhere — HomePage activates, PluginsPage creates/deletes/edits.

import { useState, useEffect, useRef, forwardRef } from 'react'
import { Card, Toggle, Button, Segmented, Range, Notice, Select, ColorPicker, SettingRow, ValueBadge } from '../components/ui'
import { IconFolder } from '../components/icons'
import { call } from '../bridge'
import { useSettings } from '../context/SettingsContext.jsx'
import { toast } from '../components/toast'
import { useAppearance, bgList, bgLoadThumb, solidBackgroundCss, DEFAULT_ACCENT, SOLID_KEY } from '../context/AppearanceContext.jsx'

// Mini wireframe of the app shell (a sidebar strip + a few content lines and
// a card) — the theme-mode tiles show this rough sketch in light/dark colors
// so the picker previews what the UI will actually look like.
function MockUi({ theme }) {
  return (
    <span className={`theme-mode-mock theme-mode-mock--${theme}`}>
      <span className="theme-mode-mock__sidebar" />
      <span className="theme-mode-mock__content">
        <span className="theme-mode-mock__bar" />
        <span className="theme-mode-mock__line" />
        <span className="theme-mode-mock__line theme-mode-mock__line--short" />
        <span className="theme-mode-mock__card" />
      </span>
    </span>
  )
}

// BgTile — the shared clickable tile used by all pickers here (theme mode,
// solid color, background image, default aurora): a preview slot, the name
// line, the "当前" check, and a theme-accent bar that fades in on hover/focus.
// The accent needs no prop: applyDesign() keeps --c-accent in sync with the
// user's 主题色, so the hover bar follows it automatically. `children` rides
// inside the button for extras (e.g. the image reveal icon).
const BgTile = forwardRef(function BgTile(
  { name, preview, current = false, applying = false, disabled = false, title, onClick, children },
  ref,
) {
  return (
    <button
      ref={ref}
      type="button"
      className={`bg-tile${current ? ' is-current' : ''}${applying ? ' is-applying' : ''}`}
      title={title}
      onClick={onClick}
      disabled={disabled || applying}
    >
      {preview}
      <span className="bg-tile__name">{name}</span>
      {current && <span className="bg-tile__check">✓ 当前</span>}
      <span className="bg-tile__accent" aria-hidden="true" />
      {children}
    </button>
  )
})

// One clickable tile in the background picker. The native thumbnail (few KB) is
// fetched only once the tile scrolls into view (IntersectionObserver), so a
// large bg dir never decodes all previews at once.
function BgThumb({ name, thumb, current, applying, onLoadThumb, onPick, onReveal }) {
  const ref = useRef(null)
  const loaded = useRef(false)

  useEffect(() => {
    if (thumb) { loaded.current = true; return }
    if (!ref.current) return
    const io = new IntersectionObserver(
      (entries) => {
        if (entries.some((e) => e.isIntersecting) && !loaded.current) {
          loaded.current = true
          onLoadThumb()
          io.disconnect()
        }
      },
      { rootMargin: '200px' },
    )
    io.observe(ref.current)
    return () => io.disconnect()
  }, [thumb, onLoadThumb])

  return (
    <BgTile
      ref={ref}
      name={name}
      current={current}
      applying={applying}
      title={name}
      onClick={onPick}
      preview={thumb ? (
        <img src={thumb} alt={name} loading="lazy" />
      ) : (
        <span className="bg-tile__ph">…</span>
      )}
    >
      {/* reveal the image file in Explorer; a span because the tile is a <button> */}
      <span
        className="bg-tile__reveal"
        role="button"
        title="在资源管理器中显示"
        onClick={(e) => {
          e.stopPropagation()
          onReveal?.()
        }}
      >
        <IconFolder width={12} height={12} />
      </span>
    </BgTile>
  )
}

export default function SettingsPage() {
  const { autoStart, loading, setAutoStart } = useSettings()
  const {
    themeMode,
    themeThreshold,
    currentLuminance,
    currentBg,
    design,
    setThemeMode,
    setThemeThreshold,
    updateDesign,
    applyBackground,
    applySolidBg,
    clearBackground,
  } = useAppearance()

  const [bgImages, setBgImages] = useState([])      // names in the bg dir
  const [bgThumbs, setBgThumbs] = useState({})      // name -> data URL (lazy)
  const [bgLoading, setBgLoading] = useState(true)
  const [bgError, setBgError] = useState('')

  // Curated solid fills for the "纯色" tab (plain CSS colors, no loading). Each
  // has a friendly name for the UI; only the hex is persisted (business field
  // stays a clean color string). Colors that had no natural name were swapped
  // for more distinguishable, nameable ones.
  const SOLID_PRESETS = [
    { name: '午夜黑', hex: '#0d0f14' },
    { name: '深空蓝', hex: '#111a2e' },
    { name: '雾霭蓝', hex: '#15212e' },
    { name: '松墨绿', hex: '#0f1a14' },
    { name: '黛青', hex: '#10211f' },
    { name: '暗夜紫', hex: '#1d1430' },
    { name: '咖啡棕', hex: '#241a14' },
    { name: '玄黑', hex: '#0a0a0a' },
  ]

  // Which background source tab is open. If a solid color is the current choice
  // (or nothing → the generated aurora), land on the 纯色 tab by default.
  const [bgTab, setBgTab] = useState(() =>
    currentBg.startsWith(SOLID_KEY) || currentBg === '' ? 'solid' : 'image'
  )
  // A solid color chosen via the picker that isn't one of the presets → the
  // "自定义" tile is current and shows that color as its preview.
  const customInputRef = useRef(null)
  const isCustomCurrent =
    currentBg.startsWith(SOLID_KEY) && !SOLID_PRESETS.some((p) => p.hex === currentBg.slice(SOLID_KEY.length))
  const customHex = isCustomCurrent ? currentBg.slice(SOLID_KEY.length) : '#5ea6ff'
  const [applying, setApplying] = useState('')

  const MODE_TILES = [
    { value: 'auto', label: '跟随背景', preview: 'auto' },
    { value: 'dark', label: '深色', preview: 'dark' },
    { value: 'light', label: '浅色', preview: 'light' },
  ]
  const FONT_SIZE_OPTIONS = [
    { value: 'sm', label: '小' },
    { value: 'md', label: '默认' },
    { value: 'lg', label: '大' },
  ]
  const DENSITY_OPTIONS = [
    { value: 'sm', label: '紧凑' },
    { value: 'md', label: '标准' },
    { value: 'lg', label: '宽松' },
  ]

  // Two background sources: "纯色" picks a CSS fill (no image on disk),
  // "图片" keeps the old behaviour of listing the bg/ dir.
  const BG_TABS = [
    { value: 'solid', label: '纯色' },
    { value: 'image', label: '图片' },
  ]

  const changeDesign = (patch) => updateDesign(patch)

  const loadBg = async () => {
    setBgLoading(true)
    setBgError('')
    try {
      const res = await bgList()
      setBgImages(res.ok ? res.names : [])
      setBgError(res.ok ? '' : res.error)
    } finally {
      setBgLoading(false)
    }
  }
  useEffect(() => { loadBg() }, [])

  // Load a lightweight thumbnail on demand — and only when the tile is actually
  // about to be visible (see BgThumb's IntersectionObserver), so the 4K sources
  // are never decoded into the grid at once.
  const thumbFor = async (name) => {
    if (bgThumbs[name]) return bgThumbs[name]
    const url = await bgLoadThumb(name)
    setBgThumbs((prev) => ({ ...prev, [name]: url }))
    return url
  }

  const pickBackground = async (name) => {
    setApplying(name)
    try {
      const url = await applyBackground(name)
      if (!url) {
        toast.error(`读取背景图失败：${name}`)
        return
      }
      toast.success(`已应用背景：${name}`)
    } finally {
      setApplying('')
    }
  }

  // Pick a CSS-only solid color (persists to the separate solidColor field).
  // `silent` skips the confirmation toast so the live drag frames don't each
  // pop one — the commit toast is fired by the native `change` listener below.
  const pickSolid = async (hex, opts = {}) => {
    const key = `${SOLID_KEY}${hex}`
    setApplying(key)
    try {
      const res = await applySolidBg(hex)
      if (!res) {
        toast.error(`应用纯色背景失败：${hex}`)
        return
      }
      if (!opts.silent) {
        toast.success(opts.name ? `已应用纯色背景：${opts.name}` : '已应用纯色背景')
      }
    } finally {
      setApplying('')
    }
  }

  // `<input type="color">` fires React's onChange (the native "input" event)
  // on EVERY frame while dragging — used for live repaint only. The native
  // "change" event fires ONCE when the picker is committed/closed, so that's
  // the only place we show the "已应用纯色背景" toast. Attached via ref (React
  // has no onCommit) and re-bound whenever the solid tab (and thus the input)
  // mounts/unmounts.
  useEffect(() => {
    const el = customInputRef.current
    if (!el) return
    const onCommit = () => toast.success('已应用纯色背景')
    el.addEventListener('change', onCommit)
    return () => el.removeEventListener('change', onCommit)
  }, [bgTab])

  const clearBg = async () => {
    await clearBackground()
    toast.success('已恢复默认极光背景')
  }

  const changeMode = (mode) => setThemeMode(mode)
  const changeThreshold = (v) => setThemeThreshold(Number(v))

  const toggleAutoStart = async (on) => {
    try {
      const res = await setAutoStart(on)
      if (res?.ok === false) {
        toast.error('开启开机启动失败（任务计划程序注册被拒绝），请检查系统策略后重试')
      } else {
        toast.success(on ? '已开启开机启动' : '已关闭开机启动')
      }
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '设置失败'))
    }
  }

  const changePopupPosition = async (pos) => {
    try {
      await setPopupPosition(pos)
      toast.success('插件弹窗位置已更新')
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '设置失败'))
    }
  }

  return (
    <div>
      <div className="page-title">应用设置</div>
      <p className="page-sub">常规行为、外观与设计</p>

      <Card title="主题">
        <SettingRow
          label="界面主题"
          desc="跟随背景时，亮背景用浅色主题，暗背景用深色主题"
        />
        <div className="bg-grid theme-mode-grid">
          {MODE_TILES.map((m) => (
            <BgTile
              key={m.value}
              name={m.label}
              current={themeMode === m.value}
              onClick={() => changeMode(m.value)}
              preview={
                <span className="bg-tile__ph theme-mode__ph">
                  {m.preview === 'auto' ? (
                    <>
                      <MockUi theme="light" />
                      <MockUi theme="dark" />
                    </>
                  ) : (
                    <MockUi theme={m.preview} />
                  )}
                </span>
              }
            />
          ))}
        </div>

        <div className="section-gap" />

        <SettingRow
          label="亮度判定阈值"
          desc={
            <>
              背景多亮才切浅色主题。探测取背景中央约 60% 区域（内容所在处）的平均亮度，而非整图
              <span className="threshold-readout">
                {currentLuminance != null && <span className="threshold-readout__hint"> / 255</span>}
              </span>
            </>
          }
          control={
            <>
              <ValueBadge>{themeThreshold}</ValueBadge>
              <Range
                className="slider"
                min={40} max={200}
                value={themeThreshold}
                mark={currentLuminance != null
                  ? { value: currentLuminance, text: `当前背景亮度：${Math.round(currentLuminance)}` }
                  : undefined}
                disabled={themeMode !== 'auto'}
                onChange={(e) => changeThreshold(e.target.value)}
              />
            </>
          }
        />
      </Card>

      <div className="section-gap" />

      <Card title="字体与密度">
        <SettingRow
          label="字体大小"
          desc="整体界面文字的基础尺寸"
          control={
            <Segmented
              options={FONT_SIZE_OPTIONS}
              value={design.fontSize ?? 'md'}
              onChange={(v) => changeDesign({ fontSize: v })}
            />
          }
        />
        <SettingRow
          label="界面密度"
          desc="间距与控件尺寸的紧凑程度"
          control={
            <Segmented
              options={DENSITY_OPTIONS}
              value={design.density ?? 'md'}
              onChange={(v) => changeDesign({ density: v })}
            />
          }
        />
      </Card>

      <div className="section-gap" />

      <Card title="设计">
        <SettingRow
          label="玻璃模糊"
          desc="毛玻璃表面的模糊强度"
          control={
            <>
              <ValueBadge>{design.glassBlur}px</ValueBadge>
              <Range
                className="slider"
                min={4} max={48}
                value={design.glassBlur}
                onChange={(e) => changeDesign({ glassBlur: Number(e.target.value) })}
              />
            </>
          }
        />
        <SettingRow
          label="圆角"
          desc="卡片与控件的圆润程度"
          control={
            <>
              <ValueBadge>{design.cornerRadius}px</ValueBadge>
              <Range
                className="slider"
                min={4} max={32}
                value={design.cornerRadius}
                onChange={(e) => changeDesign({ cornerRadius: Number(e.target.value) })}
              />
            </>
          }
        />
        <SettingRow
          label="主题色"
          desc="界面主色；强调色、柔光等衍生色调由主色自动计算"
          control={
            <ColorPicker
              value={design.accentColor || DEFAULT_ACCENT}
              onChange={(v) => changeDesign({ accentColor: v })}
            />
          }
        />
      </Card>

      <div className="section-gap" />

      <Card title="背景">
        <SettingRow
          className="setting-row--column"
          label="背景来源"
          desc="纯色用 CSS 色块（无需图片）；图片沿用软件目录下的 bg/"
        >
          <Segmented options={BG_TABS} value={bgTab} onChange={setBgTab} />
        </SettingRow>

        {bgTab === 'solid' ? (
          <>
            <div className="bg-grid">
              {/* 默认极光（= 清除选择）：预览用渐变变量，选中则恢复生成式背景 */}
              <BgTile
                name="默认极光"
                current={currentBg === ''}
                onClick={clearBg}
                preview={<span className="bg-tile__ph" style={{ background: 'var(--bg-base)' }}>极光</span>}
              />
              {SOLID_PRESETS.map(({ name, hex }) => {
                const key = `${SOLID_KEY}${hex}`
                return (
                  <BgTile
                    key={hex}
                    name={name}
                    current={currentBg === key}
                    applying={applying === key}
                    onClick={() => pickSolid(hex, { name })}
                    preview={
                      <span
                        className="bg-tile__ph"
                        style={{ background: solidBackgroundCss(hex), backgroundBlendMode: 'soft-light, normal' }}
                      />
                    }
                  />
                )
              })}
              {/* 自定义任意纯色：调起系统取色器。包一层 relative 容器，色块是普通
                  div、取色器 input 作为兄弟节点绝对覆盖在上面——避免把 input 嵌进
                  button（非法且易出怪问题）。input 视觉隐藏但占据正常布局位置，所以
                  系统取色弹窗锚定在色块附近（而不是视口原点、 pop 到侧边 nav）。 */}
              <div style={{ position: 'relative' }}>
                <div className={`bg-tile${isCustomCurrent ? ' is-current' : ''}`}>
                  <span
                    className="bg-tile__ph"
                    style={{ background: isCustomCurrent ? solidBackgroundCss(customHex) : 'conic-gradient(from 0deg, #ff639c, #ffd24a, #34d399, #5ea6ff, #6348ff)', backgroundBlendMode: isCustomCurrent ? 'soft-light, normal' : undefined }}
                  >＋</span>
                  <span className="bg-tile__name">自定义</span>
                  {isCustomCurrent && <span className="bg-tile__check">✓ 当前</span>}
                  <span className="bg-tile__accent" aria-hidden="true" />
                </div>
                <input
                  ref={customInputRef}
                  type="color"
                  value={customHex}
                  onChange={(e) => pickSolid(e.target.value, { silent: true })}
                  aria-label="自定义纯色"
                  style={{
                    position: 'absolute',
                    inset: 0,
                    width: '100%',
                    height: '100%',
                    opacity: 0,
                    border: 'none',
                    padding: 0,
                    cursor: 'pointer',
                  }}
                />
              </div>
            </div>
          </>
        ) : bgLoading ? (
          <p className="field__desc">读取中…</p>
        ) : bgImages.length ? (
          <div className="bg-grid">
            {bgImages.map((name) => (
              <BgThumb
                key={name}
                name={name}
                thumb={bgThumbs[name]}
                current={name === currentBg}
                applying={applying === name}
                onLoadThumb={() => thumbFor(name)}
                onPick={() => pickBackground(name)}
                onReveal={() => call('shell_reveal', 'bg', name)}
              />
            ))}
          </div>
        ) : bgError === 'bridge' ? (
          <Notice tone="error">
            无法获取背景图列表：native 端未包含 bg 列表功能。请重新构建 C++（本机 exe
            比源码旧，bg_list 桥接方法尚未编译进去）后再试。
          </Notice>
        ) : (
          <Notice tone="warning">
            未找到背景图。请在软件目录下创建 <code>bg/</code> 文件夹并放入图片，然后点“刷新”。
          </Notice>
        )}

        <div style={{ marginTop: '12px' }}>
          {/* 仅图片模式需要“刷新目录” */}
          {bgTab === 'image' && (
            <Button variant="ghost" size="sm" onClick={loadBg} disabled={bgLoading}>
              {bgLoading ? '刷新中…' : '刷新背景图列表'}
            </Button>
          )}
        </div>
      </Card>

      <div className="section-gap" />

      <Card title="常规">
        <SettingRow
          label="开机启动"
          desc="登录 Windows 时自动在后台启动 GameTrigger"
          control={
            <Toggle
              on={autoStart}
              onChange={toggleAutoStart}
              title={loading ? '加载中…' : autoStart ? '已开启' : '已关闭'}
            />
          }
        />
      </Card>
    </div>
  )
}
