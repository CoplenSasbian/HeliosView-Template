// PluginDetailModal.jsx — plugin detail dialog: common metadata (name,
// version, description, DLL file, enabled state in the current config,
// declared parameters) plus a "自定义信息" section filled with the HTML the
// plugin itself returns (plugins_customInfo, empty for plugins without one).

import { useEffect, useState } from 'react'
import { Modal, Button } from './ui'
import { call } from '../bridge'

const PARAM_TYPE_LABEL = {
  int: '整数',
  double: '浮点',
  string: '文本',
  bool: '开关',
  datetime: '时间',
  file: '文件',
  folder: '文件夹',
  select: '选择',
}

// 把插件返回的 HTML 片段包成完整文档, 并在 <head> 注入两段脚本:
// 1) 链接拦截: sandbox 的 iframe 里直接导航到外部站点会被对方的
//    X-Frame-Options 拒绝 (显示"拒绝连接"), 所以拦截所有 <a> 点击,
//    postMessage 给父页面, 由 shell_openUrl 交给系统默认浏览器打开。
// 2) 高度上报: iframe 高度固定会留大片空白 (内容短) 或出现内部滚动条
//    (内容长), 所以插件文档自己量高度 postMessage 给父页面自适应。
const wrapPluginHtml = (html) =>
  '<!doctype html><html><head><meta charset="utf-8">' +
  "<script>" +
  "var report=function(){" +
  // 只量 body: documentElement.scrollHeight 会把 iframe 自身视口也算进去,
  // 量出来的永远是当前高度, 永远缩不回去。
  "var h=document.body?document.body.scrollHeight:0;" +
  "if(h>0)window.parent.postMessage({type:'pluginSize',height:h},'*')" +
  "};" +
  // body 内容变化 (图片/字体加载) 时重报; body 要等 DOM 建好才能观察。
  "var watch=function(){" +
  "if(!window.ResizeObserver||document.__pluginRo)return;" +
  "document.__pluginRo=new ResizeObserver(report);" +
  "if(document.body)document.__pluginRo.observe(document.body);" +
  "report();" +
  "};" +
  "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',watch);}" +
  "else{watch();}" +
  "window.addEventListener('load',report);" +
  "document.addEventListener('click',function(e){" +
  "var a=e.target&&e.target.closest?e.target.closest('a'):null;" +
  "if(!a)return;var href=a.getAttribute('href')||'';" +
  "if(/^https?:\\/\\//i.test(href)){" +
  "e.preventDefault();window.parent.postMessage({type:'pluginLink',href:href},'*');" +
  "}});" +
  "</script></head><body style='margin:0'>" + html + "</body></html>"

// iframe 高度自适应: 插件文档量出的高度夹在这个区间里, 太长的仍然内部滚动。
// PAD 让 iframe 略高于量出的内容高度, 避免差一两像素时出现内部滚动条。
const FRAME_H_DEFAULT = 340
const FRAME_H_MIN = 80
const FRAME_H_MAX = 520
const FRAME_H_PAD = 6

export default function PluginDetailModal({ plugin, infos = [], enabled, onClose }) {
  const [html, setHtml] = useState('')
  const [frameH, setFrameH] = useState(FRAME_H_DEFAULT)
  const [loading, setLoading] = useState(false)
  const [failed, setFailed] = useState(false)

  // 每次打开都重新向插件索取自定义 HTML（统计页等按需生成）。
  useEffect(() => {
    const name = plugin?.name
    if (!name) return
    let cancelled = false
    setHtml('')
    setFrameH(FRAME_H_DEFAULT)
    setFailed(false)
    setLoading(true)
    call('plugins_customInfo', name)
      .then((result) => { if (!cancelled) setHtml(typeof result === 'string' ? result : '') })
      .catch(() => { if (!cancelled) setFailed(true) })
      .finally(() => { if (!cancelled) setLoading(false) })
    return () => { cancelled = true }
  }, [plugin?.name])

  // 监听 iframe 注入脚本的 postMessage:
  // - pluginLink: 插件里的 http(s) 链接转交系统默认浏览器打开;
  // - pluginSize: 插件文档高度, 用来让 iframe 高度贴合内容。
  useEffect(() => {
    const onMessage = (e) => {
      const d = e?.data
      if (d?.type === 'pluginLink' && typeof d.href === 'string') {
        call('shell_openUrl', d.href).catch(() => {})
      } else if (d?.type === 'pluginSize' && Number.isFinite(d.height)) {
        const h = Math.round(d.height)
        if (h > 0) {
          setFrameH(Math.min(Math.max(h + FRAME_H_PAD, FRAME_H_MIN), FRAME_H_MAX))
        }
      }
    }
    window.addEventListener('message', onMessage)
    return () => window.removeEventListener('message', onMessage)
  }, [])

  if (!plugin) return null

  return (
    <Modal
      open={!!plugin}
      title={`${plugin.name} — 插件详情`}
      onClose={onClose}
      className="modal--wide"
      actions={<Button variant="ghost" onClick={onClose}>关闭</Button>}
    >
      <div className="plugin-detail">
        <div className="plugin-detail__grid">
          <div className="plugin-detail__cell">
            <div className="plugin-detail__label">插件名</div>
            <div className="plugin-detail__value">{plugin.name}</div>
          </div>
          <div className="plugin-detail__cell">
            <div className="plugin-detail__label">版本</div>
            <div className="plugin-detail__value">v{plugin.version ?? '—'}</div>
          </div>
          <div className="plugin-detail__cell">
            <div className="plugin-detail__label">是否启用（当前配置）</div>
            <div className="plugin-detail__value">{enabled ? '已启用' : '已停用'}</div>
          </div>
          <div className="plugin-detail__cell plugin-detail__cell--wide">
            <div className="plugin-detail__label">描述</div>
            <div className="plugin-detail__value">{plugin.description || '—'}</div>
          </div>
          <div className="plugin-detail__cell plugin-detail__cell--wide">
            <div className="plugin-detail__label">DLL 文件</div>
            <div className="plugin-detail__value plugin-detail__value--mono" title={plugin.dll}>
              {plugin.dll || '—'}
            </div>
          </div>
        </div>

        <div className="plugin-detail__section-title">参数（{infos.length}）</div>
        {infos.length ? (
          <div className="plugin-detail__params">
            {infos.map((p) => (
              <div className="plugin-detail__param" key={p.name}>
                <div className="plugin-detail__param-head">
                  <span className="plugin-detail__param-name">{p.label || p.name}</span>
                  <span className="plugin-detail__param-type">{PARAM_TYPE_LABEL[p.type] ?? p.type}</span>
                  <code className="plugin-detail__param-key">{p.name}</code>
                </div>
                {p.desc && <div className="plugin-detail__param-desc">{p.desc}</div>}
                {p.defaultValue !== undefined && (
                  <div className="plugin-detail__param-default">默认：{String(p.defaultValue)}</div>
                )}
              </div>
            ))}
          </div>
        ) : (
          <div className="plugin-detail__empty">该插件没有可配置参数。</div>
        )}

        <div className="plugin-detail__section-title">自定义信息</div>
        {loading ? (
          <div className="plugin-detail__empty">加载中…</div>
        ) : failed ? (
          <div className="plugin-detail__empty">自定义信息加载失败</div>
        ) : html ? (
          <iframe
            className="plugin-detail__frame"
            style={{ height: frameH }}
            sandbox="allow-scripts"
            srcDoc={wrapPluginHtml(html)}
            title={`${plugin.name} 自定义信息`}
          />
        ) : (
          <div className="plugin-detail__empty">该插件未提供自定义信息。</div>
        )}
      </div>
    </Modal>
  )
}