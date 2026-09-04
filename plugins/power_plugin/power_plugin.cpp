// PowerPlugin — 切换 Windows 电源方案 (从 gameMode 移植到 GameTrigger 插件接口)
//
// 参数:
//   scheme (Select, 动态选项) — 下拉选择要切换到的电源方案。
//     选项列表 = 本机所有电源方案 (初始化时枚举), 值为选项下标。
//     默认选中"平衡"方案 (找不到则选第一个)。
//
// 说明: gameMode 版参数是 "Game ON 时切换到的方案", 在 GameTrigger 的
// 配置模型里, 每个配置各自定义 scheme —— "游戏"配置选高性能, "默认/关闭"
// 配置选平衡, 即可实现进入/退出游戏时自动切换电源方案。

#include <IPlugin.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <powrprof.h>
#pragma comment(lib, "powrprof")

#include <format>
#include <string>
#include <utility>
#include <vector>

namespace {

static std::string WidenToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string r(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), r.data(), len, nullptr, nullptr);
    return r;
}

static std::wstring SchemeFriendlyName(const GUID& guid) {
    DWORD size = 0;
    PowerReadFriendlyName(nullptr, &guid, nullptr, nullptr, nullptr, &size);
    if (size == 0) return {};
    std::wstring name(size / sizeof(wchar_t), L'\0');
    DWORD written = size;
    PowerReadFriendlyName(nullptr, &guid, nullptr, nullptr,
                          reinterpret_cast<UCHAR*>(name.data()), &written);
    name.resize(written / sizeof(wchar_t));
    // 去掉结尾的 \0 (PowerReadFriendlyName 可能带终止符)
    while (!name.empty() && name.back() == L'\0') name.pop_back();
    return name;
}

static std::string GuidToString(const GUID& g) {
    return std::format("{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
                       g.Data1, g.Data2, g.Data3,
                       g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
                       g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

class PowerPlugin : public IPlugin {
public:
    ~PowerPlugin() override = default;

    int version() noexcept override { return 1; }

    const char* name() noexcept override { return "PowerPlugin"; }

    const char* description() noexcept override {
        return "切换 Windows 电源方案";
    }

    // 宿主 UI 在"插件详情"里展示的自定义信息: 一句话说明 + 官方文档链接
    const char* customInfo() noexcept override {
        uiBuffer_ =
            "<div style=\"font-family:system-ui,'Segoe UI',sans-serif;color:#dbe2ea;"
            "padding:4px 2px;font-size:13px;line-height:1.7\">"
            "<div style=\"font-weight:600;margin-bottom:6px\">电源方案切换</div>"
            "<div style=\"margin-bottom:8px\">激活配置时把 Windows 电源方案切换到指定项 "
            "(下拉列出本机全部方案，含 GUID 与名称)。适合打游戏时切\"高性能\"、"
            "日常用\"平衡\"来省电。可用 <code>powercfg /list</code> 查看本机方案。</div>"
            "<a href=\"https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/powercfg-command-line-options\" "
            "style=\"color:#5ea6ff\">Microsoft：powercfg 命令行选项</a>"
            "</div>";
        return uiBuffer_.c_str();
    }

    void initialize(ILogger* logger, IPluginContext* context) noexcept override {
        this->logger = logger;
        this->context = context;
        parameters.reserve(1);

        auto& param = parameters.emplace_back();
        param.name = "scheme";
        param.label = "电源方案";
        param.description = "电源方案 (下拉选择)";
        param.type = PluginParameterType::Select;
        param.selectValue.options = nullptr;
        param.selectValue.optionCount = 0;
        param.selectValue.defaultValue = 0;

        // 枚举本机所有电源方案
        GUID guid;
        DWORD size = sizeof(guid);
        DWORD index = 0;
        while (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, index,
                              reinterpret_cast<UCHAR*>(&guid), &size) == ERROR_SUCCESS) {
            m_schemeGuids.push_back(guid);
            m_schemeNames.push_back(WidenToUtf8(SchemeFriendlyName(guid)));
            index++;
            size = sizeof(guid);
        }

        if (m_schemeGuids.empty()) {
            logError("未找到任何电源方案!");
            return;
        }

        // 重名方案用 GUID 片段消歧
        for (size_t i = 0; i < m_schemeNames.size(); i++) {
            for (size_t j = 0; j < i; j++) {
                if (m_schemeNames[j] == m_schemeNames[i]) {
                    m_schemeNames[i] += std::format("({:04X})", m_schemeGuids[i].Data3);
                    break;
                }
            }
        }

        // 构建选项数组 (指针指向 m_schemeNames, 之后不得再修改 m_schemeNames)
        m_schemeOpts.clear();
        m_schemeOpts.reserve(m_schemeNames.size());
        for (const auto& n : m_schemeNames)
            m_schemeOpts.push_back(n.c_str());

        param.selectValue.options = m_schemeOpts.data();
        param.selectValue.optionCount = static_cast<int>(m_schemeOpts.size());
        param.selectValue.defaultValue = findDefaultScheme();

        logInfo(std::format("已初始化, 共 {} 个电源方案:", m_schemeGuids.size()));
        for (size_t i = 0; i < m_schemeGuids.size(); i++) {
            logInfo(std::format("  [{}] {} = {}", i, m_schemeNames[i], GuidToString(m_schemeGuids[i])));
        }
    }

    void uninitialize() noexcept override {}

    void pluginParameters(PluginParameterInfo** out, int* count) noexcept override {
        *out = parameters.data();
        *count = static_cast<int>(parameters.size());
    }

    bool execute(PluginParameterValue* values) noexcept override {
        int index = 0;
        try {
            index = static_cast<int>(values->getInt64Value("scheme"));
        } catch (...) {
            logError("execute: 缺少 'scheme' 参数");
            if (context) context->reportStatus(NotifyLevel::Error, "缺少 scheme 参数");
            return false;
        }

        if (m_schemeGuids.empty()) {
            logError("没有可用的电源方案");
            if (context) context->reportStatus(NotifyLevel::Warning, "无可用电源方案");
            return false;
        }

        // 下标越界保护 (方案列表可能因系统设置变化而改变)
        if (index < 0 || index >= static_cast<int>(m_schemeGuids.size())) {
            logError(std::format("scheme 下标越界: {} (有效范围 0-{})",
                                 index, static_cast<int>(m_schemeGuids.size()) - 1));
            if (context) context->reportStatus(NotifyLevel::Error, "电源方案下标越界");
            return false;
        }

        DWORD err = PowerSetActiveScheme(nullptr, &m_schemeGuids[index]);
        if (err != ERROR_SUCCESS) {
            logError(std::format("PowerSetActiveScheme 失败: {} (0x{:08X})", err, err));
            if (err == ERROR_ACCESS_DENIED)
                logInfo("需要管理员权限来切换电源方案");
            else if (err == ERROR_NOT_SUPPORTED)
                logInfo("此电源方案不可用, 请选择其他方案");
            if (context) context->reportStatus(NotifyLevel::Error, "电源方案切换失败(需权限)");
            return false;
        }

        logInfo(std::format("电源方案已切换: {}", m_schemeNames[index]));
        if (context) context->reportStatus(NotifyLevel::Success, std::format("电源方案: {}", m_schemeNames[index]).c_str());
        return true;
    }

private:
    int findDefaultScheme() const {
        for (size_t i = 0; i < m_schemeGuids.size(); i++) {
            if (IsEqualGUID(m_schemeGuids[i], GUID_TYPICAL_POWER_SAVINGS))
                return static_cast<int>(i);
        }
        return 0;
    }

    void logInfo(const std::string& msg) const {
        if (logger) logger->log(ILogger::Info, "Power", msg.c_str());
    }
    void logWarn(const std::string& msg) const {
        if (logger) logger->log(ILogger::Warning, "Power", msg.c_str());
    }
    void logError(const std::string& msg) const {
        if (logger) logger->log(ILogger::Error, "Power", msg.c_str());
    }

    ILogger* logger = nullptr;
    IPluginContext* context = nullptr;
    std::vector<PluginParameterInfo> parameters;
    std::string uiBuffer_;   // customInfo() 返回缓冲

    std::vector<GUID> m_schemeGuids;
    std::vector<std::string> m_schemeNames;
    std::vector<const char*> m_schemeOpts;
};

} // namespace

REGISTER_PLUGIN(PowerPlugin)
