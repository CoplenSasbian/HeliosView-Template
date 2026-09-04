// HdrPlugin — 切换 HDR (从 gameMode 移植到 GameTrigger 插件接口)
//
// 参数:
//   action (Int, 0..2, 默认 0):
//     0 = 不操作, 1 = 开启 HDR, 2 = 关闭 HDR
//
// 实现顺序:
//   1. Win11 24H2+ 的 SET_HDR_STATE / GET_ADVANCED_COLOR_INFO_2
//   2. Win10 2004+ 的 SET_ADVANCED_COLOR_STATE / GET_ADVANCED_COLOR_INFO
//   3. 兜底: 模拟 Win+Alt+B (Win11 原生快捷键), 并通过 DXGI 校验结果

#include <IPlugin.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

// ======================================================================
// Structs for advanced color / HDR display config APIs.
// The DISPLAYCONFIG_DEVICE_INFO_TYPE values are:
//   -3 = GET_ADVANCED_COLOR_INFO       (Win10 2004+)
//   -4 = SET_ADVANCED_COLOR_STATE      (Win10 2004+)
//   -5 = GET_ADVANCED_COLOR_INFO_2     (Win11 24H2+)
//   -6 = SET_HDR_STATE                 (Win11 24H2+)
// We use these constants to avoid depending on a specific SDK version.
// ======================================================================

#pragma pack(push, 4)

struct HdrAdvancedColorInfo {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        UINT32 value;
        struct {
            UINT32 advancedColorSupported : 1;
            UINT32 advancedColorEnabled : 1;
            UINT32 wideColorEnforced : 1;
            UINT32 advancedColorMode : 2;
            UINT32 reserved : 27;
        };
    };
};

struct HdrSetAdvancedColorState {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        UINT32 value;
        struct {
            UINT32 enableAdvancedColor : 1;
            UINT32 reserved : 31;
        };
    };
};

struct HdrAdvancedColorInfo2 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        UINT32 value;
        struct {
            UINT32 highDynamicRangeSupported : 1;
            UINT32 advancedColorSupported : 1;
            UINT32 activeColorMode : 2;
            UINT32 reserved : 28;
        };
    };
};

struct HdrSetHdrState {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        UINT32 value;
        struct {
            UINT32 enableHdr : 1;
            UINT32 reserved : 31;
        };
    };
};

#pragma pack(pop)

// activeColorMode == 2 means HDR
static constexpr UINT32 HDR_MODE_HDR = 2;

// ======================================================================
// HDR helper functions
// ======================================================================

namespace {

enum class HdrStatus { Unsupported, Off, On };

static HdrStatus GetDisplayHDRStatus(LUID adapterId, UINT32 targetId) {
    // Try the Win11 24H2+ API first — it correctly reports HDR mode under ACM.
    HdrAdvancedColorInfo2 info2 = {};
    info2.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-5);
    info2.header.size = sizeof(info2);
    info2.header.adapterId = adapterId;
    info2.header.id = targetId;

    if (DisplayConfigGetDeviceInfo(&info2.header) == ERROR_SUCCESS) {
        if (!info2.highDynamicRangeSupported)
            return HdrStatus::Unsupported;
        return info2.activeColorMode == HDR_MODE_HDR ? HdrStatus::On : HdrStatus::Off;
    }

    // Fall back to the Win10 2004+ API.
    HdrAdvancedColorInfo info = {};
    info.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-3);
    info.header.size = sizeof(info);
    info.header.adapterId = adapterId;
    info.header.id = targetId;

    if (DisplayConfigGetDeviceInfo(&info.header) != ERROR_SUCCESS)
        return HdrStatus::Unsupported;

    if (!info.advancedColorSupported)
        return HdrStatus::Unsupported;

    return info.advancedColorEnabled ? HdrStatus::On : HdrStatus::Off;
}

template<typename F>
static void ForEachDisplay(F func) {
    UINT32 pathCount = 0, modeCount = 0;

    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        return;

    for (const auto& path : paths)
        func(path, modes.at(path.targetInfo.modeInfoIdx));
}

// DXGI-based HDR detection (works even when DisplayConfig HDR APIs fail).
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi")

static std::optional<HdrStatus> GetHdrStatusViaDXGI() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return std::nullopt;

    std::optional<HdrStatus> result;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT ai = 0; factory->EnumAdapters1(ai, &adapter) != DXGI_ERROR_NOT_FOUND; ++ai) {
        IDXGIOutput* output = nullptr;
        for (UINT oi = 0; adapter->EnumOutputs(oi, &output) != DXGI_ERROR_NOT_FOUND; ++oi) {
            IDXGIOutput6* out6 = nullptr;
            if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&out6)))) {
                DXGI_OUTPUT_DESC1 desc;
                out6->GetDesc1(&desc);

                // ColorSpace == ST2084 (PQ) means HDR is ON
                bool hdrOn = (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                if (hdrOn) {
                    result = HdrStatus::On;
                    out6->Release();
                    output->Release();
                    adapter->Release();
                    factory->Release();
                    return result;
                }
                if (!result.has_value())
                    result = HdrStatus::Off;
                out6->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return result;
}

// Toggle HDR by simulating Win+Alt+B (Windows 11 native shortcut).
static void SimulateWinAltB() {
    INPUT inputs[6] = {};
    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_LWIN;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = VK_MENU;
    inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'B';

    inputs[3] = inputs[2]; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[4] = inputs[1]; inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[5] = inputs[0]; inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(6, inputs, sizeof(INPUT));
}

// Returns nullopt if the operation failed.
static std::optional<HdrStatus> SetDisplayHDRStatus(LUID adapterId, UINT32 targetId, bool enable) {
    // Try the Win11 24H2+ SET_HDR_STATE first.
    HdrSetHdrState setHdr = {};
    setHdr.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-6);
    setHdr.header.size = sizeof(setHdr);
    setHdr.header.adapterId = adapterId;
    setHdr.header.id = targetId;
    setHdr.enableHdr = enable;

    if (DisplayConfigSetDeviceInfo(&setHdr.header) == ERROR_SUCCESS) {
        auto s = GetDisplayHDRStatus(adapterId, targetId);
        if (s != HdrStatus::Unsupported)
            return s;
        return enable ? HdrStatus::On : HdrStatus::Off;
    }

    // Fall back to SET_ADVANCED_COLOR_STATE.
    HdrSetAdvancedColorState setColor = {};
    setColor.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-4);
    setColor.header.size = sizeof(setColor);
    setColor.header.adapterId = adapterId;
    setColor.header.id = targetId;
    setColor.enableAdvancedColor = enable;

    if (DisplayConfigSetDeviceInfo(&setColor.header) != ERROR_SUCCESS)
        return std::nullopt;

    auto s = GetDisplayHDRStatus(adapterId, targetId);
    if (s != HdrStatus::Unsupported)
        return s;
    return enable ? HdrStatus::On : HdrStatus::Off;
}

} // anonymous namespace

// ======================================================================
// HdrPlugin
// ======================================================================

namespace {

const char* const s_hdrOptions[] = {
    "不操作",
    "开启 HDR",
    "关闭 HDR",
};

class HdrPlugin : public IPlugin {
public:
    ~HdrPlugin() override = default;

    int version() noexcept override { return 1; }

    const char* name() noexcept override { return "HdrPlugin"; }

    const char* description() noexcept override {
        return "切换 HDR (原生 DisplayConfig / Win+Alt+B)";
    }

    // 宿主 UI 在"插件详情"里展示的自定义信息: 一句话说明 + 官方文档链接
    const char* customInfo() noexcept override {
        uiBuffer_ =
            "<div style=\"font-family:system-ui,'Segoe UI',sans-serif;color:#dbe2ea;"
            "padding:4px 2px;font-size:13px;line-height:1.7\">"
            "<div style=\"font-weight:600;margin-bottom:6px\">HDR 切换</div>"
            "<div style=\"margin-bottom:8px\">激活配置时开启/关闭 HDR：Win11 24H2+ 走系统 "
            "HDR API，Win10 2004+ 走高级颜色状态 API，两者都不可用时模拟 Win+Alt+B "
            "并读取 DXGI 状态校验结果。HDR 需要显示器与连接链路支持。</div>"
            "<a href=\"https://support.microsoft.com/en-us/windows/what-is-hdr-in-windows-f5fbf5cb-149d-4a0d-8be1-9ed78c68d3b4\" "
            "style=\"color:#5ea6ff\">Microsoft：什么是 Windows 中的 HDR</a>"
            "</div>";
        return uiBuffer_.c_str();
    }

    void initialize(ILogger* logger, IPluginContext* /*context*/) noexcept override {
        this->logger = logger;
        parameters.reserve(1);

        auto& param = parameters.emplace_back();
        param.name = "action";
        param.label = "HDR 动作";
        param.description = "HDR 动作";
        param.type = PluginParameterType::Select;
        param.selectValue.options = s_hdrOptions;
        param.selectValue.optionCount = 3;
        param.selectValue.defaultValue = 0;

        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
            logError("DisplayConfig API 不可用");
            m_available = false;
            return;
        }

        m_available = true;
        logInfo("已初始化 (原生 DisplayConfig)");
    }

    void uninitialize() noexcept override {}

    void pluginParameters(PluginParameterInfo** out, int* count) noexcept override {
        *out = parameters.data();
        *count = static_cast<int>(parameters.size());
    }

    bool execute(PluginParameterValue* values) noexcept override {
        int action = 0;
        try {
            action = static_cast<int>(values->getInt64Value("action"));
        } catch (...) {
            logError("execute: 缺少 'action' 参数");
            return false;
        }

        // 下标越界保护
        if (action < 0) action = 0;
        if (action > 2) action = 2;

        if (!m_available) {
            logWarn("HDR 不可用, 忽略动作");
            return false;
        }

        switch (action) {
        case 1:
            return enableHdr(true);
        case 2:
            return enableHdr(false);
        default:
            return true; // 0 = 不操作
        }
    }

private:
    void logInfo(const std::string& msg) const {
        if (logger) logger->log(ILogger::Info, "Hdr", msg.c_str());
    }
    void logWarn(const std::string& msg) const {
        if (logger) logger->log(ILogger::Warning, "Hdr", msg.c_str());
    }
    void logError(const std::string& msg) const {
        if (logger) logger->log(ILogger::Error, "Hdr", msg.c_str());
    }

    bool enableHdr(bool enable) {
        // 1. Detect current HDR state
        auto current = GetHdrStatusViaDXGI();
        if (current.has_value()) {
            bool on = (*current == HdrStatus::On);
            logInfo(std::string("当前 HDR 状态: ") + (on ? "ON" : "OFF") +
                    (enable ? ", 目标: ON" : ", 目标: OFF"));
            if (on == enable) {
                logInfo("已处于目标状态, 跳过");
                return true;
            }
        } else {
            logWarn("无法检测 HDR 状态, 将盲切换");
        }

        // 2. Try DisplayConfig SET API first (works on WDDM 2.7+ drivers)
        bool setOk = false;
        ForEachDisplay([&](const DISPLAYCONFIG_PATH_INFO& path, const DISPLAYCONFIG_MODE_INFO&) {
            if (!setOk) {
                auto r = SetDisplayHDRStatus(path.targetInfo.adapterId, path.targetInfo.id, enable);
                if (r.has_value()) setOk = true;
            }
        });

        if (setOk) {
            logInfo(enable ? "HDR 已开启 (DisplayConfig)" : "HDR 已关闭 (DisplayConfig)");
            return true;
        }

        // 3. Fall back to Win+Alt+B (Windows 11 native shortcut)
        logInfo("DisplayConfig 失败, 回退到模拟 Win+Alt+B");
        SimulateWinAltB();

        // 4. Wait and verify
        Sleep(1000);
        auto after = GetHdrStatusViaDXGI();
        if (after.has_value()) {
            bool success = ((*after == HdrStatus::On) == enable);
            if (success)
                logInfo(enable ? "HDR 已开启 (Win+Alt+B)" : "HDR 已关闭 (Win+Alt+B)");
            else
                logWarn("HDR 切换失败 (状态未变化)");
            return success;
        }

        logWarn("已尝试切换 HDR 但无法校验结果");
        return false;
    }

    ILogger* logger = nullptr;
    std::vector<PluginParameterInfo> parameters;
    std::string uiBuffer_;   // customInfo() 返回缓冲
    bool m_available = false;
};

} // namespace

REGISTER_PLUGIN(HdrPlugin)
