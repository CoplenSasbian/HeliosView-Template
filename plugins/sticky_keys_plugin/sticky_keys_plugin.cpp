// StickyKeysPlugin — 屏蔽 / 恢复 粘滞键 (Sticky Keys)
//
// 与 gameMode 旧版不同: 旧版直接把粘滞键"开启/关闭" (SPI_SETSTICKYKEYS
// 写死 SKF_STICKYKEYSON), 会永久改动用户的辅助功能设置。
// 本插件采用"屏蔽 + 恢复"语义:
//   - 配置中 block = true  : 保存当前粘滞键状态, 然后屏蔽
//                            (关闭粘滞键功能 + 关闭 5 次 Shift 热键,
//                             不再弹确认对话框, 也不会被误触发)。
//                            仅改内存状态, 不写入注册表 —— 崩溃/重启后
//                            Windows 自动回到用户原有设置。
//   - 配置中 block = false : 恢复之前保存的粘滞键状态
//                            (连同注册表一起写回, 完整还原用户设置)。
//   - 插件卸载时若仍处于屏蔽状态, 自动恢复。

#include <IPlugin.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <format>
#include <string>
#include <vector>

namespace {

class StickyKeysPlugin : public IPlugin {
public:
    ~StickyKeysPlugin() override = default;

    int version() noexcept override { return 1; }

    const char* name() noexcept override { return "StickyKeysPlugin"; }

    const char* description() noexcept override {
        return "屏蔽/恢复粘滞键 (Sticky Keys block & restore)";
    }

    // 宿主 UI 在"插件详情"里展示的自定义信息: 一句话说明 + 官方文档链接
    const char* customInfo() noexcept override {
        uiBuffer_ =
            "<div style=\"font-family:system-ui,'Segoe UI',sans-serif;color:#dbe2ea;"
            "padding:4px 2px;font-size:13px;line-height:1.7\">"
            "<div style=\"font-weight:600;margin-bottom:6px\">粘滞键屏蔽</div>"
            "<div style=\"margin-bottom:8px\">激活配置时保存当前粘滞键状态并屏蔽 "
            "(含连续按 5 次 Shift 的确认弹窗)，切走配置或退出时恢复原状——"
            "避免游戏中误触发粘滞键弹窗。粘滞键是辅助功能，正常打字场景建议保持开启。</div>"
            "<a href=\"https://www.microsoft.com/en-us/windows/accessibility-features\" "
            "style=\"color:#5ea6ff\">Microsoft：Windows 辅助功能（键盘 / 粘滞键）</a>"
            "</div>";
        return uiBuffer_.c_str();
    }

    void initialize(ILogger* logger, IPluginContext* context) noexcept override {
        this->logger = logger;
        this->context = context;
        parameters.reserve(1);

        auto& param = parameters.emplace_back();
        param.name = "block";
        param.label = "屏蔽粘滞键";
        param.description = "屏蔽粘滞键: true=保存当前状态并禁用(含5次Shift热键), false=恢复之前的状态";
        param.type = PluginParameterType::Boolean;
        param.boolValue.defaultValue = false;
    }

    void uninitialize() noexcept override {
        // 应用退出/插件重载时, 若仍处于屏蔽状态则恢复用户原有设置
        restore();
    }

    void pluginParameters(PluginParameterInfo** out, int* count) noexcept override {
        *out = parameters.data();
        *count = static_cast<int>(parameters.size());
    }

    bool execute(PluginParameterValue* values) noexcept override {
        bool block = false;
        try {
            block = values->getBoolValue("block");
        } catch (...) {
            logError("execute: 缺少 'block' 参数");
            if (context) context->reportStatus(NotifyLevel::Error, "缺少 block 参数");
            return false;
        }

        if (block) {
            blockStickyKeys();
            if (context) context->reportStatus(NotifyLevel::Success, "粘滞键已屏蔽");
        } else {
            restore();
        }
        return true;
    }

private:
    // ---------- 日志辅助 ----------
    void logInfo(const std::string& msg) const {
        if (logger) logger->log(ILogger::Info, "StickyKeys", msg.c_str());
    }
    void logWarn(const std::string& msg) const {
        if (logger) logger->log(ILogger::Warning, "StickyKeys", msg.c_str());
    }
    void logError(const std::string& msg) const {
        if (logger) logger->log(ILogger::Error, "StickyKeys", msg.c_str());
    }

    // ---------- SPI 封装 ----------
    static bool queryStickyKeys(STICKYKEYS& sk) {
        sk.cbSize = sizeof(sk);
        return SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(sk), &sk, 0) != FALSE;
    }

    static bool applyStickyKeys(const STICKYKEYS& sk, bool persist) {
        return SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(sk),
                                     const_cast<STICKYKEYS*>(&sk),
                                     SPIF_SENDCHANGE | (persist ? SPIF_UPDATEINIFILE : 0)) != FALSE;
    }

    // ---------- 屏蔽 ----------
    void blockStickyKeys() {
        if (m_blocked) return;  // 已屏蔽, 不再重复保存原状态

        STICKYKEYS sk{};
        if (!queryStickyKeys(sk)) {
            logError("SPI_GETSTICKYKEYS 失败, 无法屏蔽粘滞键");
            return;
        }

        // 保存用户当前状态, 供恢复使用
        m_originalFlags = sk.dwFlags;

        // 屏蔽: 关闭粘滞键状态 + 5 次 Shift 热键 + 确认/提示/声音,
        // 保留 SKF_AVAILABLE 使功能在设置中仍然可用。
        sk.dwFlags &= ~(SKF_STICKYKEYSON | SKF_HOTKEYACTIVE | SKF_CONFIRMHOTKEY |
                        SKF_HOTKEYSOUND);
        sk.dwFlags |= SKF_AVAILABLE;

        // 仅修改内存状态, 不写入注册表(临时屏蔽, 重启自动还原)
        if (!applyStickyKeys(sk, /*persist=*/false)) {
            logError("SPI_SETSTICKYKEYS 失败, 无法屏蔽粘滞键");
            m_originalFlags = 0;
            return;
        }

        m_blocked = true;
        logInfo(std::format("粘滞键已屏蔽 (原 flags=0x{:08X})", m_originalFlags));
    }

    // ---------- 恢复 ----------
    void restore() {
        if (!m_blocked) return;

        STICKYKEYS sk{};
        sk.cbSize = sizeof(sk);
        sk.dwFlags = m_originalFlags;

        // 完整还原: 内存 + 注册表, 恢复用户原有设置
        if (!applyStickyKeys(sk, /*persist=*/true)) {
            logError("SPI_SETSTICKYKEYS 失败, 无法恢复粘滞键");
            return;
        }

        logInfo(std::format("粘滞键已恢复 (flags=0x{:08X})", m_originalFlags));
        m_blocked = false;
        m_originalFlags = 0;
    }

    ILogger* logger = nullptr;
    IPluginContext* context = nullptr;
    std::vector<PluginParameterInfo> parameters;
    std::string uiBuffer_;   // customInfo() 返回缓冲

    bool m_blocked = false;
    DWORD m_originalFlags = 0;
};

} // namespace

REGISTER_PLUGIN(StickyKeysPlugin)
