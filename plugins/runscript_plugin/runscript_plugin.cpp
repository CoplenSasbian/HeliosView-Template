// RunScriptPlugin — 激活配置时执行自定义脚本/程序 (从 gameMode 移植)
//
// gameMode 版有 "Game ON 脚本" / "Game OFF 脚本" 两个 FILE 参数;
// GameTrigger 的配置模型是每个配置各自定义参数, 因此这里只保留一个
// "command" 参数 —— 在"游戏"配置里填开启时执行的脚本, 在"默认/关闭"
// 配置里填关闭时执行的脚本, 效果与旧版一致。
//
// 支持: .ps1 (powershell), .vbs (wscript), 其他 (bat/exe/文件关联直接执行)

#include <IPlugin.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

static std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), len);
    return r;
}

class RunScriptPlugin : public IPlugin {
public:
    ~RunScriptPlugin() override = default;

    int version() noexcept override { return 1; }

    const char* name() noexcept override { return "RunScriptPlugin"; }

    const char* description() noexcept override {
        return "激活配置时执行脚本/程序 (bat, ps1, exe…)";
    }

    // 宿主 UI 在"插件详情"里展示的自定义信息: 一句话说明 + 官方文档链接
    const char* customInfo() noexcept override {
        uiBuffer_ =
            "<div style=\"font-family:system-ui,'Segoe UI',sans-serif;color:#dbe2ea;"
            "padding:4px 2px;font-size:13px;line-height:1.7\">"
            "<div style=\"font-weight:600;margin-bottom:6px\">脚本/程序执行</div>"
            "<div style=\"margin-bottom:8px\">激活配置时启动指定脚本或程序 "
            "(bat/cmd/ps1/vbs/exe…)，可用于拉起游戏工具、切换音效、同步外设等。"
            "脚本路径保存在各配置的 <code>command</code> 参数里，每个配置可以不同。</div>"
            "<a href=\"https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/cmd\" "
            "style=\"color:#5ea6ff\">Microsoft：cmd 命令参考</a>"
            "</div>";
        return uiBuffer_.c_str();
    }

    void initialize(ILogger* logger, IPluginContext* /*context*/) noexcept override {
        this->logger = logger;
        parameters.reserve(1);

        auto& param = parameters.emplace_back();
        param.name = "command";
        param.label = "启动脚本";
        param.description = "激活此配置时执行的脚本/程序 (bat/ps1/vbs/exe…)";
        param.type = PluginParameterType::File;
        param.fileValue.defaultValue = "";
        param.fileValue.filter = "脚本/程序 (*.bat;*.cmd;*.ps1;*.vbs;*.exe)|*.bat;*.cmd;*.ps1;*.vbs;*.exe";

        logInfo("已初始化");
    }

    void uninitialize() noexcept override {}

    void pluginParameters(PluginParameterInfo** out, int* count) noexcept override {
        *out = parameters.data();
        *count = static_cast<int>(parameters.size());
    }

    bool execute(PluginParameterValue* values) noexcept override {
        std::string command;
        try {
            const char* v = values->getFileValue("command");
            if (v) command = v;
        } catch (...) {
            logError("execute: 缺少 'command' 参数");
            return false;
        }

        if (command.empty()) {
            logInfo("command 为空, 不执行");
            return true;
        }

        logInfo(std::format("执行脚本: {}", command));
        return executeCommand(command);
    }

private:
    void logInfo(const std::string& msg) const {
        if (logger) logger->log(ILogger::Info, "RunScript", msg.c_str());
    }
    void logWarn(const std::string& msg) const {
        if (logger) logger->log(ILogger::Warning, "RunScript", msg.c_str());
    }
    void logError(const std::string& msg) const {
        if (logger) logger->log(ILogger::Error, "RunScript", msg.c_str());
    }

    bool executeCommand(const std::string& cmd) {
        std::wstring wcmd = widen(cmd);

        // Detect file extension
        std::wstring ext;
        auto dot = wcmd.rfind(L'.');
        if (dot != std::wstring::npos)
            for (auto c : wcmd.substr(dot))
                ext.push_back(towlower(c));

        std::wstring params; // must outlive ShellExecuteExW
        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        sei.nShow = SW_HIDE;

        if (ext == L".ps1") {
            sei.lpFile = L"powershell.exe";
            params = std::format(L"-ExecutionPolicy Bypass -NoProfile -File \"{}\"", wcmd);
            sei.lpParameters = params.c_str();
        } else if (ext == L".vbs") {
            sei.lpFile = L"wscript.exe";
            params = std::format(L"\"{}\"", wcmd);
            sei.lpParameters = params.c_str();
        } else {
            sei.lpFile = wcmd.c_str();
        }

        if (!ShellExecuteExW(&sei)) {
            DWORD err = GetLastError();
            logError(std::format("ShellExecuteEx 失败: {} (0x{:08X})", err, err));
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
                logWarn("脚本执行失败");
            return false;
        }
        return true;
    }

    ILogger* logger = nullptr;
    std::vector<PluginParameterInfo> parameters;
    std::string uiBuffer_;   // customInfo() 返回缓冲
};

} // namespace

REGISTER_PLUGIN(RunScriptPlugin)
