// NvDvcPlugin — NVIDIA Digital Vibrance (数字振动) (从 gameMode 移植)
//
// 参数:
//   level (Int, -1..100, 默认 -1):
//     -1 = 不改变 (推荐在"默认/关闭"配置中使用)
//     0-100 = 激活配置时设置的数字振动级别
//
// 通过动态加载 nvapi64.dll / nvapi.dll 调用 NvAPI_SetDVCLevelEx,
// 无需链接 NVIDIA 官方 SDK。

#include <IPlugin.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <format>
#include <string>
#include <vector>

// NvAPI function IDs
constexpr unsigned int NVAPI_INITIALIZE          = 0x0150E828;
constexpr unsigned int NVAPI_ENUM_NVIDIA_DISPLAY = 0x9ABDD40D;
constexpr unsigned int NVAPI_GET_DVC_INFO_EX     = 0x0E45002D;
constexpr unsigned int NVAPI_SET_DVC_LEVEL_EX    = 0x4A82C2B1;

// NV_DISPLAY_DVC_INFO_EX structure
struct NvDvcInfo {
    unsigned int version = (1 << 16) | sizeof(NvDvcInfo);
    int currentLevel = 0;
    int minLevel = 0;
    int maxLevel = 100;
    int defaultLevel = 50;
};

using QueryInterface_t = int* (*)(unsigned int offset);

namespace {

class NvDvcPlugin : public IPlugin {
public:
    ~NvDvcPlugin() override = default;

    int version() noexcept override { return 1; }

    const char* name() noexcept override { return "NvDvcPlugin"; }

    const char* description() noexcept override {
        return "NVIDIA 数字振动 (Digital Vibrance) 自动调节";
    }

    // 宿主 UI 在"插件详情"里展示的自定义信息: 一句话说明 + NVIDIA 官方文档链接
    const char* customInfo() noexcept override {
        uiBuffer_ =
            "<div style=\"font-family:system-ui,'Segoe UI',sans-serif;color:#dbe2ea;"
            "padding:4px 2px;font-size:13px;line-height:1.7\">"
            "<div style=\"font-weight:600;margin-bottom:6px\">数字振动 (Digital Vibrance)</div>"
            "<div style=\"margin-bottom:8px\">激活配置时自动调整 NVIDIA 数字振动颜色增强级别 "
            "(0-100)。数字振动会提升屏幕色彩饱和度, 让游戏画面更鲜艳; "
            "级别用各配置下本插件的 <code>level</code> 参数设置。</div>"
            "<a href=\"https://www.nvidia.com/content/Control-Panel-Help/vLatest/en-us/mergedProjects/nvdsp/CS_Adjust_Color_Settings_Advanced.htm\" "
            "style=\"color:#5ea6ff\">NVIDIA 官方文档：控制面板 - 调整桌面颜色设置（数字振动）</a>"
            "</div>";
        return uiBuffer_.c_str();
    }

    void initialize(ILogger* logger, IPluginContext* /*context*/) noexcept override {
        this->logger = logger;
        parameters.reserve(1);

        auto& param = parameters.emplace_back();
        param.name = "level";
        param.label = "数字振动级别";
        param.description = "数字振动级别:0-100=设置值";
        param.type = PluginParameterType::Int;
        param.intValue.defaultValue = 50;
        param.intValue.minValue = 0;
        param.intValue.maxValue = 100;

        wchar_t sysDir[MAX_PATH];
        GetSystemDirectoryW(sysDir, MAX_PATH);

        std::wstring dllPath;
#ifdef _WIN64
        dllPath = std::format(L"{}\\nvapi64.dll", sysDir);
#else
        dllPath = std::format(L"{}\\nvapi.dll", sysDir);
#endif

        m_nvapi = LoadLibraryW(dllPath.c_str());
        if (!m_nvapi) {
#ifdef _WIN64
            m_nvapi = LoadLibraryW(L"nvapi64.dll");
#else
            m_nvapi = LoadLibraryW(L"nvapi.dll");
#endif
        }
        if (!m_nvapi) {
            logError(std::format("nvapi DLL 未找到 (error {})", GetLastError()));
            return;
        }

        auto query = reinterpret_cast<QueryInterface_t>(
            GetProcAddress(m_nvapi, "nvapi_QueryInterface"));
        if (!query) {
            logError("nvapi_QueryInterface 未找到");
            FreeLibrary(m_nvapi);
            m_nvapi = nullptr;
            return;
        }

        m_NvAPI_Initialize = reinterpret_cast<decltype(m_NvAPI_Initialize)>(
            (*query)(NVAPI_INITIALIZE));
        m_NvAPI_EnumNvidiaDisplayHandle = reinterpret_cast<decltype(m_NvAPI_EnumNvidiaDisplayHandle)>(
            (*query)(NVAPI_ENUM_NVIDIA_DISPLAY));
        m_NvAPI_GetDVCInfoEx = reinterpret_cast<decltype(m_NvAPI_GetDVCInfoEx)>(
            (*query)(NVAPI_GET_DVC_INFO_EX));
        m_NvAPI_SetDVCLevelEx = reinterpret_cast<decltype(m_NvAPI_SetDVCLevelEx)>(
            (*query)(NVAPI_SET_DVC_LEVEL_EX));

        if (!m_NvAPI_Initialize || !m_NvAPI_EnumNvidiaDisplayHandle ||
            !m_NvAPI_GetDVCInfoEx || !m_NvAPI_SetDVCLevelEx) {
            logWarn("部分 NvAPI 函数缺失, 插件不可用");
            FreeLibrary(m_nvapi);
            m_nvapi = nullptr;
            return;
        }

        if (m_NvAPI_Initialize() != 0) {
            logError("NvAPI_Initialize 失败");
            FreeLibrary(m_nvapi);
            m_nvapi = nullptr;
            return;
        }

        if (m_NvAPI_EnumNvidiaDisplayHandle(0, &m_displayHandle) != 0) {
            logWarn("未找到 NVIDIA 显示器");
            FreeLibrary(m_nvapi);
            m_nvapi = nullptr;
            return;
        }

        NvDvcInfo info;
        int ret = m_NvAPI_GetDVCInfoEx(m_displayHandle, 0, &info);
        if (ret == 0) {
            logInfo(std::format("当前数字振动: {} (范围 {}-{})",
                                info.currentLevel, info.minLevel, info.maxLevel));
        } else {
            logError(std::format("GetDVCInfoEx 失败: return {}", ret));
        }

        m_available = true;
        logInfo("已初始化");
    }

    void uninitialize() noexcept override {
        if (m_nvapi) {
            FreeLibrary(m_nvapi);
            m_nvapi = nullptr;
        }
        m_available = false;
    }

    void pluginParameters(PluginParameterInfo** out, int* count) noexcept override {
        *out = parameters.data();
        *count = static_cast<int>(parameters.size());
    }

    bool execute(PluginParameterValue* values) noexcept override {
        int level = -1;
        try {
            level = static_cast<int>(values->getInt64Value("level"));
        } catch (...) {
            logError("execute: 缺少 'level' 参数");
            return false;
        }

        if (level < 0) {
            logInfo("level=-1, 不改变数字振动");
            return true;
        }

        if (!m_available) {
            logWarn("NvDvc 不可用, 忽略");
            return false;
        }

        if (setVibrance(level)) {
            logInfo(std::format("数字振动已设置为 {} (配置值)", level));
            return true;
        }
        logInfo("数字振动设置失败");
        return false;
    }

private:
    void logInfo(const std::string& msg) const {
        if (logger) logger->log(ILogger::Info, "NvDvc", msg.c_str());
    }
    void logWarn(const std::string& msg) const {
        if (logger) logger->log(ILogger::Warning, "NvDvc", msg.c_str());
    }
    void logError(const std::string& msg) const {
        if (logger) logger->log(ILogger::Error, "NvDvc", msg.c_str());
    }

    bool setVibrance(int level) {
        NvDvcInfo info{};
        info.currentLevel = level;
        int ret = m_NvAPI_SetDVCLevelEx(m_displayHandle, 0, &info);
        if (ret != 0) {
            logError(std::format("SetDVCLevel({}) 失败: return {}", level, ret));
            return false;
        }
        return true;
    }

    ILogger* logger = nullptr;
    std::vector<PluginParameterInfo> parameters;
    std::string uiBuffer_;   // customInfo() 返回缓冲

    HMODULE m_nvapi = nullptr;
    bool m_available = false;

    int (*m_NvAPI_Initialize)() = nullptr;
    int (*m_NvAPI_EnumNvidiaDisplayHandle)(int thisEnum, void** pHandle) = nullptr;
    int (*m_NvAPI_GetDVCInfoEx)(void* hDisplay, int outputId, void* pDVCInfo) = nullptr;
    int (*m_NvAPI_SetDVCLevelEx)(void* hDisplay, int outputId, void* pDVCInfo) = nullptr;

    void* m_displayHandle = nullptr;
};

} // namespace

REGISTER_PLUGIN(NvDvcPlugin)
