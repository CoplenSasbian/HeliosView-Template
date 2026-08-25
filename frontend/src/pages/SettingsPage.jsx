// SettingsPage — app settings: general behaviors (auto-start, popup position)
// and config management (active config / delete / create). Settings come from
// SettingsContext, config state/ops from ConfigContext; only the modal/local
// state (create/delete dialogs) lives in this page.

import { useState, useEffect, useRef } from 'react'
import { Card, Toggle, Chip, Button, Segmented, Range, Notice, Select, ColorPicker } from '../components/ui'
import { IconFolder } from '../components/icons'
import { call } from '../bridge'
import { useSettings } from '../context/SettingsContext.jsx'
import { useConfig } from '../context/ConfigContext.jsx'
import CreateConfigModal from '../components/CreateConfigModal'
import ConfirmModal from '../components/ConfirmModal'
import { toast } from '../components/toast'
import { getThemePrefs, setAppearancePrefs, reapplyTheme,
         getDesign, setDesign, applyDesign,
         bgList, bgLoadThumb, getCurrentBg, getSolidColor, applyBackground,
         applySolidBg, clearBackground, solidBackgroundCss,
         DEFAULT_ACCENT } from '../wallpaper'

// 0=左上角 1=右上角 2=左下角 3=右下角 — must match the native popupPosition.
const POPUP_OPTIONS = [
  { value: 0, label: '左上角' },
  { value: 1, label: '右上角' },
  { value: 2, label: '左下角' },
  { value: 3, label: '右下角' },
]

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
    <button
      ref={ref}
      type="button"
      className={`bg-tile${current ? ' is-current' : ''}${applying ? ' is-applying' : ''}`}
      title={name}
      onClick={onPick}
      disabled={applying}
    >
      {thumb ? (
        <img src={thumb} alt={name} loading="lazy" />
      ) : (
        <span className="bg-tile__ph">…</span>
      )}
      <span className="bg-tile__name">{name}</span>
      {current && <span className="bg-tile__check">✓ 当前</span>}
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
    </button>
  )
}

export default function SettingsPage() {
  const { autoStart, popupPosition, loading, setAutoStart, setPopupPosition } = useSettings()
  const { activeConfig, createConfig: createConfigCtx, deleteConfig: deleteConfigCtx } = useConfig()

  const [showCreateConfig, setShowCreateConfig] = useState(false)
  const [creating, setCreating] = useState(false)
  const [deleteTarget, setDeleteTarget] = useState(null)
  const [deleting, setDeleting] = useState(false)

  // ---- appearance: background library + luminance→theme ----
  const [themePrefs, setThemePrefsState] = useState(() => getThemePrefs())
  const [design, setDesignState] = useState(() => getDesign())
  const [bgImages, setBgImages] = useState([])      // names in the bg dir
  const [bgThumbs, setBgThumbs] = useState({})      // name -> data URL (lazy)

  // JS-internal UI key only — never written to JSON. The persisted shape uses
  // two clean fields (backgroundName for photos, solidColor for fills); we just
  // prefix the color with "solid:" here to compare it against bgCurrent.
  const SOLID_KEY = 'solid:'
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

  const [bgCurrent, setBgCurrent] = useState(() => {
    const solid = getSolidColor()
    return solid ? `${SOLID_KEY}${solid}` : getCurrentBg()
  })
  const [bgLoading, setBgLoading] = useState(true)
  // Which background source tab is open. If a solid color is the current choice
  // (or nothing → the generated aurora), land on the 纯色 tab by default.
  const [bgTab, setBgTab] = useState(() =>
    bgCurrent.startsWith(SOLID_KEY) || bgCurrent === '' ? 'solid' : 'image'
  )
  // A solid color chosen via the picker that isn't one of the presets → the
  // "自定义" tile is current and shows that color as its preview.
  const customInputRef = useRef(null)
  const isCustomCurrent =
    bgCurrent.startsWith(SOLID_KEY) && !SOLID_PRESETS.some((p) => p.hex === bgCurrent.slice(SOLID_KEY.length))
  const customHex = isCustomCurrent ? bgCurrent.slice(SOLID_KEY.length) : '#5ea6ff'
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

  const changeDesign = (patch) => {
    // Update local React state, apply CSS instantly; the persist happens inside
    // setDesign via the unified debounced writer — no per-slider debounce here.
    const next = { ...getDesign(), ...patch }
    setDesignState(next)
    applyDesign(next)
    setDesign(next)
  }

  // Load the available images and mirror the current background selection.
  // NOTE: deliberately does NOT call loadAppearance() here — that re-reads
  // native and overwrites the in-memory appearance cache, so any design
  // choice (font size / density / accent …) made this session would be
  // clobbered by whatever stale value is persisted (old app.json files may
  // even lack density entirely), resetting the controls to defaults on every
  // page entry. main.jsx already hydrated the cache from native before React
  // mounted; everything below reads that same cache.
  const [bgError, setBgError] = useState('')
  const loadBg = async () => {
    setBgLoading(true)
    setBgError('')
    try {
      setThemePrefsState(getThemePrefs())
      setDesignState(getDesign())
      const res = await bgList()
      setBgImages(res.ok ? res.names : [])
      setBgError(res.ok ? '' : res.error)
      // Mirror the initial-state derivation: a solid color wins over the photo
      // name. Using getCurrentBg() alone would blank it to "" for solids and
      // wrongly mark "默认极光" as current (the bug seen after page switches).
      const solid = getSolidColor()
      setBgCurrent(solid ? `${SOLID_KEY}${solid}` : getCurrentBg())
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
      setBgCurrent(name)
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
      setBgCurrent(key)
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
    clearBackground()
    setBgCurrent('')
    toast.success('已恢复默认极光背景')
  }

  const updateTheme = (next) => {
    // Apply locally immediately and re-evaluate the theme on every change;
    // native persist is debounced inside setAppearancePrefs (unified writer).
    // ORDER MATTERS: setAppearancePrefs updates the wallpaper.js module cache
    // synchronously, and reapplyTheme resolves the theme from it — calling
    // them the other way round applies the PREVIOUS mode (each click lagged
    // one step behind: dead first click, then wrong theme).
    const merged = { ...themePrefs, ...next }
    setThemePrefsState(merged)
    setAppearancePrefs({ mode: merged.mode, threshold: merged.threshold })
    reapplyTheme(merged)
  }

  const changeMode = (mode) => updateTheme({ ...themePrefs, mode })
  const changeThreshold = (v) => updateTheme({ ...themePrefs, threshold: Number(v) })

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

  const createConfig = async (name) => {
    setCreating(true)
    try {
      await createConfigCtx(name)
      setShowCreateConfig(false)
      toast.success(`配置「${name}」已创建并激活`)
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '创建失败'))
    } finally {
      setCreating(false)
    }
  }

  const deleteConfig = async () => {
    if (!deleteTarget) return
    setDeleting(true)
    try {
      await deleteConfigCtx(deleteTarget)
      setDeleteTarget(null)
      toast.success(`配置「${deleteTarget}」已删除`)
    } catch (e) {
      toast.error(String(e?.error ?? e ?? '删除失败'))
    } finally {
      setDeleting(false)
    }
  }

  return (
    <div>
      <div className="page-title">应用设置</div>
      <p className="page-sub">常规行为、启动与配置管理</p>

      <Card title="外观">
        <div className="setting-row">
          <div>
            <div className="setting-row__label">界面主题</div>
            <div className="setting-row__desc">跟随背景时，亮背景用浅色主题，暗背景用深色主题</div>
          </div>
        </div>
        <div className="bg-grid theme-mode-grid">
          {MODE_TILES.map((m) => (
            <button
              key={m.value}
              type="button"
              className={`bg-tile${themePrefs.mode === m.value ? ' is-current' : ''}`}
              onClick={() => changeMode(m.value)}
            >
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
              <span className="bg-tile__name">{m.label}</span>
              {themePrefs.mode === m.value && <span className="bg-tile__check">✓ 当前</span>}
            </button>
          ))}
        </div>

        <div className="section-gap" />

        <div className="setting-row">
          <div>
            <div className="setting-row__label">亮度判定阈值</div>
            <div className="setting-row__desc">背景多亮才切浅色主题</div>
          </div>
          <div className="setting-row__control">
            <span className="value-badge">{themePrefs.threshold}</span>
            <Range
              className="slider"
              min={40} max={200}
              value={themePrefs.threshold}
              disabled={themePrefs.mode !== 'auto'}
              onChange={(e) => changeThreshold(e.target.value)}
            />
          </div>
        </div>

        <div className="setting-row">
          <div>
            <div className="setting-row__label">玻璃模糊</div>
            <div className="setting-row__desc">毛玻璃表面的模糊强度</div>
          </div>
          <div className="setting-row__control">
            <span className="value-badge">{design.glassBlur}px</span>
            <Range
              className="slider"
              min={4} max={48}
              value={design.glassBlur}
              onChange={(e) => changeDesign({ glassBlur: Number(e.target.value) })}
            />
          </div>
        </div>
        <div className="setting-row">
          <div>
            <div className="setting-row__label">字体大小</div>
            <div className="setting-row__desc">整体界面文字的基础尺寸</div>
          </div>
          <div className="setting-row__control">
            <Segmented
              options={FONT_SIZE_OPTIONS}
              value={design.fontSize ?? 'md'}
              onChange={(v) => changeDesign({ fontSize: v })}
            />
          </div>
        </div>
        <div className="setting-row">
          <div>
            <div className="setting-row__label">界面密度</div>
            <div className="setting-row__desc">间距与控件尺寸的紧凑程度</div>
          </div>
          <div className="setting-row__control">
            <Segmented
              options={DENSITY_OPTIONS}
              value={design.density ?? 'md'}
              onChange={(v) => changeDesign({ density: v })}
            />
          </div>
        </div>

        <div className="setting-row">
          <div>
            <div className="setting-row__label">圆角</div>
            <div className="setting-row__desc">卡片与控件的圆润程度</div>
          </div>
          <div className="setting-row__control">
            <span className="value-badge">{design.cornerRadius}px</span>
            <Range
              className="slider"
              min={4} max={32}
              value={design.cornerRadius}
              onChange={(e) => changeDesign({ cornerRadius: Number(e.target.value) })}
            />
          </div>
        </div>
        <div className="setting-row">
          <div>
            <div className="setting-row__label">主题色</div>
            <div className="setting-row__desc">界面主色；强调色、柔光等衍生色调由主色自动计算</div>
          </div>
          <div className="setting-row__control">
            <ColorPicker
              value={design.accentColor || DEFAULT_ACCENT}
              onChange={(v) => changeDesign({ accentColor: v })}
            />
          </div>
        </div>

        <div className="section-gap" />

        {/* 背景：纯色（CSS 直接上色）/ 图片（bg 目录）两类 */}
        <div className="setting-row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 12 }}>
          <div>
            <div className="setting-row__label">背景</div>
            <div className="setting-row__desc">纯色用 CSS 色块（无需图片）；图片沿用软件目录下的 bg/</div>
          </div>
          <Segmented options={BG_TABS} value={bgTab} onChange={setBgTab} />
        </div>

        {bgTab === 'solid' ? (
          <>
            <div className="bg-grid">
              {/* 默认极光（= 清除选择）：预览用渐变变量，选中则恢复生成式背景 */}
              <button
                type="button"
                className={`bg-tile${bgCurrent === '' ? ' is-current' : ''}`}
                onClick={clearBg}
              >
                <span className="bg-tile__ph" style={{ background: 'var(--bg-base)' }}>极光</span>
                <span className="bg-tile__name">默认极光</span>
                {bgCurrent === '' && <span className="bg-tile__check">✓ 当前</span>}
              </button>
              {SOLID_PRESETS.map(({ name, hex }) => {
                const key = `${SOLID_KEY}${hex}`
                const cur = bgCurrent === key
                return (
                  <button
                    key={hex}
                    type="button"
                    className={`bg-tile${cur ? ' is-current' : ''}`}
                    onClick={() => pickSolid(hex, { name })}
                    disabled={applying === key}
                  >
                    <span className="bg-tile__ph" style={{ background: solidBackgroundCss(hex), backgroundBlendMode: 'soft-light, normal' }} />
                    <span className="bg-tile__name">{name}</span>
                    {cur && <span className="bg-tile__check">✓ 当前</span>}
                  </button>
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
          <>
            <div className="bg-grid">
              {bgImages.map((name) => (
                <BgThumb
                  key={name}
                  name={name}
                  thumb={bgThumbs[name]}
                  current={name === bgCurrent}
                  applying={applying === name}
                  onLoadThumb={() => thumbFor(name)}
                  onPick={() => pickBackground(name)}
                  onReveal={() => call('shell_reveal', 'bg', name)}
                />
              ))}
            </div>
          </>
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

        <div className="setting-row" style={{ marginTop: '12px' }}>
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
        <div className="setting-row">
          <div>
            <div className="setting-row__label">开机启动</div>
            <div className="setting-row__desc">登录 Windows 时自动在后台启动 GameTrigger</div>
          </div>
          <div className="setting-row__control">
            <Toggle
              on={autoStart}
              onChange={toggleAutoStart}
              title={loading ? '加载中…' : autoStart ? '已开启' : '已关闭'}
            />
          </div>
        </div>

        <div className="setting-row">
          <div>
            <div className="setting-row__label">插件输出弹窗位置</div>
            <div className="setting-row__desc">插件通知在屏幕的哪个角落弹出</div>
          </div>
          <div className="setting-row__control">
            <Select
              options={POPUP_OPTIONS}
              value={popupPosition}
              disabled={loading}
              onChange={changePopupPosition}
            />
          </div>
        </div>
      </Card>

      <div className="section-gap" />

      <Card title="配置管理">
        <div className="setting-row">
          <div>
            <div className="setting-row__label">当前配置</div>
            <div className="setting-row__desc">当前激活的配置文件</div>
          </div>
          <div className="setting-row__control">
            <Chip>{activeConfig || '—'}</Chip>
            <Button
              variant="danger"
              size="sm"
              disabled={!activeConfig || deleting}
              onClick={() => setDeleteTarget(activeConfig)}
            >
              删除
            </Button>
          </div>
        </div>

        <div className="section-gap" />

        <Button variant="primary" onClick={() => setShowCreateConfig(true)}>
          + 新建配置
        </Button>
      </Card>

      <CreateConfigModal
        open={showCreateConfig}
        busy={creating}
        onClose={() => setShowCreateConfig(false)}
        onSubmit={createConfig}
      />

      <ConfirmModal
        open={!!deleteTarget}
        title="删除配置"
        message={`确定删除配置「${deleteTarget}」？该操作会删除对应的配置文件，不可恢复。`}
        confirmText="删除"
        busy={deleting}
        danger
        onClose={() => { if (!deleting) setDeleteTarget(null) }}
        onConfirm={deleteConfig}
      />
    </div>
  )
}
