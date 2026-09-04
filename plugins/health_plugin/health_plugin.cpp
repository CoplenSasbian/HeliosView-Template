// HealthPlugin — 健康提醒 + 游戏时长统计。
//
// 宿主 (PluginManager) 在每次配置激活时调用插件的 execute(); 通过注入的
// IPluginContext 读取当前激活配置名:
//   - 激活某个游戏配置 (activeConfigName != "close") → 记为会话开始, 并读取
//     该配置里本插件的阈值/文案参数;
//   - 激活 "close" → 会话结束: 若时长超过阈值, 通过 context->notifyUser()
//     发出 OS toast 通知提醒休息; 无论是否超时, 会话记录都写入宿主 KV 存储。
//
// 统计保存在宿主提供的 per-plugin KV 存储里 (键 "sessions", 值为多行文本,
// 每行一条记录: start<TAB>end<TAB>minutes<TAB>config, 配置名经过转义)。
// customInfo() 读出全部记录, 生成一段自包含的 HTML 统计页, 由宿主 UI 在插件
// 详情弹窗的"自定义信息"栏展示。

#include <IPlugin.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace
{
// ---------------------------------------------------------------------------
// 记录行编解码 (TSV):  start\tend\tminutes\tconfig
// ---------------------------------------------------------------------------
std::string EscapeField(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == '\\')      out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

std::string UnescapeField(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            const char n = s[++i];
            if (n == 't')       out += '\t';
            else if (n == 'n')  out += '\n';
            else                out += n;   // "\\" -> "\"
        }
        else
            out += s[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// 最小 HTML 转义 (配置名/时间文本进入 HTML 之前)
// ---------------------------------------------------------------------------
std::string EscapeHtml(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;        break;
        }
    }
    return out;
}

struct SessionRecord
{
    int64_t start = 0;    // unix epoch 秒
    int64_t end = 0;      // unix epoch 秒
    int64_t seconds = 0;  // 会话时长（秒，避免不足 1 分钟被截断成 0）
    std::string config;
};

std::string FormatTime(int64_t epoch)
{
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%m-%d %H:%M", &tm);
    return buf;
}

// 格式化会话时长（输入为秒）：不足 1 分钟显示"不到 1 分钟"，其余按分钟显示
// （四舍五入），超过 1 小时显示"X 小时 Y 分钟"。
std::string FormatDuration(int64_t seconds)
{
    if (seconds < 60)
        return "不到 1 分钟";
    const int64_t mins = (seconds + 30) / 60;
    if (mins < 60)
        return std::format("{} 分钟", mins);
    return std::format("{} 小时 {} 分钟", mins / 60, mins % 60);
}

class HealthPlugin : public IPlugin
{
public:
    ~HealthPlugin() override = default;

    int version() noexcept override { return 1; }

    const char* name() noexcept override { return "health"; }

    const char* description() noexcept override
    {
        return "健康提醒 + 游戏时长统计：超时提醒休息，并记录每次游戏会话";
    }

    void initialize(ILogger* logger, IPluginContext* context) noexcept override
    {
        this->logger = logger;
        this->context = context;

        parameters.reserve(2);

        // 提醒阈值（分钟）
        auto& threshold = parameters.emplace_back();
        threshold.name = "remindMinutes";
        threshold.label = "提醒阈值（分钟）";
        threshold.description = "单次游戏超过该时长后，切换到“关闭”时提醒休息";
        threshold.type = PluginParameterType::Int;
        threshold.intValue.defaultValue = 60;
        threshold.intValue.minValue = 10;
        threshold.intValue.maxValue = 600;

        // 自定义提醒文案（支持 {minutes} 占位符；留空使用默认文案）
        auto& message = parameters.emplace_back();
        message.name = "restMessage";
        message.label = "提醒文案";
        message.description = "自定义提醒内容，{minutes} 会替换为本次时长（分钟）；留空使用默认文案";
        message.type = PluginParameterType::String;
        message.stringValue.defaultValue = "";
    }

    void uninitialize() noexcept override
    {
        // 退出时把仍在进行中的会话收尾, 统计不丢
        if (inSession)
            flushSession(std::chrono::system_clock::now());
    }

    void pluginParameters(PluginParameterInfo** out, int* count) noexcept override
    {
        *out = parameters.data();
        *count = static_cast<int>(parameters.size());
    }

    bool execute(PluginParameterValue* params) noexcept override
    {
        if (!context)
            return false;

        const char* active = context->activeConfigName();
        const bool isClose = active && std::strcmp(active, "close") == 0;

        if (isClose)
        {
            endSession();
        }
        else if (active && *active)
        {
            // 某个游戏配置被激活 → 会话开始。同一个配置被重复激活（如进程
            // 监控再次命中同一配置）时保持当前会话, 不重置计时器。
            if (!inSession || active != sessionConfig)
                startSession(active, params);
        }
        return true;
    }

    // 宿主 UI 在"插件详情"弹窗里展示的自定义信息: 从 KV 读全部会话记录,
    // 汇总后返回一段自包含的 HTML 统计页。
    const char* customInfo() noexcept override
    {
        uiBuffer_ = BuildStatsHtml();
        return uiBuffer_.c_str();
    }

private:
    using Clock = std::chrono::steady_clock;

    void startSession(const char* config, PluginParameterValue* params) noexcept
    {
        sessionStart = Clock::now();
        wallStart = std::chrono::system_clock::now();
        sessionConfig = config ? config : "";
        inSession = true;

        // 阈值/文案来自“当前游戏配置”里本插件的参数（每个配置各自保存），
        // 在会话开始时快照下来，结束判定用这次的值。
        thresholdMinutes = 60;
        customMessage.clear();
        if (params)
        {
            thresholdMinutes = params->getInt64Value("remindMinutes");
            customMessage = params->getStringValue("restMessage");
        }
        if (logger)
        {
            const std::string msg = std::format("会话开始: {}, 阈值 {} 分钟", sessionConfig, thresholdMinutes);
            logger->log(ILogger::Info, "HealthPlugin", msg.c_str());
        }
    }

    void endSession() noexcept
    {
        if (!inSession)
            return;
        inSession = false;
        flushSession(std::chrono::system_clock::now());
    }

    // 写入记录 (KV "sessions"), 超时则顺便提醒休息。时长按秒记录, 避免
    // 不足 1 分钟被截断成 0。
    void flushSession(std::chrono::system_clock::time_point end) noexcept
    {
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - sessionStart).count();
        const int64_t startEpoch = std::chrono::duration_cast<std::chrono::seconds>(
            wallStart.time_since_epoch()).count();
        const int64_t endEpoch = std::chrono::duration_cast<std::chrono::seconds>(
            end.time_since_epoch()).count();

        // 记录: start<TAB>end<TAB>seconds<TAB>config
        std::string line = std::format("{}\t{}\t{}\t{}", startEpoch, endEpoch, seconds,
                                       EscapeField(sessionConfig));

        std::string all;
        if (const char* cur = context->kvGet("sessions"))
            all = cur;
        if (!all.empty() && all.back() != '\n')
            all += '\n';
        all += line + '\n';
        context->kvSet("sessions", all.c_str());

        if (logger)
        {
            const std::string msg = std::format("会话结束: {}, 时长 {} 秒", sessionConfig, seconds);
            logger->log(ILogger::Info, "HealthPlugin", msg.c_str());
        }

        // 超过阈值才提醒休息 (阈值以分钟为单位)
        if (seconds < thresholdMinutes * 60)
            return;
        const int64_t mins = (seconds + 30) / 60;
        const std::string body = makeMessage(mins);
        if (!context->notifyUser("健康提醒", body.c_str(), NotifyLevel::Warning) && logger)
            logger->log(ILogger::Warning, "HealthPlugin", "休息提醒发送失败 (toast 不可用)");
    }

    // 默认: "你已经连续游戏 X 分钟了……"; 自定义文案里把 {minutes} 换成时长。
    std::string makeMessage(int64_t minutes) const
    {
        if (!customMessage.empty())
        {
            constexpr std::string_view needle = "{minutes}";
            std::string out;
            out.reserve(customMessage.size() + 8);
            size_t pos = 0;
            while (true)
            {
                const size_t found = customMessage.find(needle, pos);
                if (found == std::string::npos)
                {
                    out.append(customMessage, pos, customMessage.size() - pos);
                    break;
                }
                out.append(customMessage, pos, found - pos);
                out += std::to_string(minutes);
                pos = found + needle.size();
            }
            return out;
        }
        return std::format("你已经连续游戏 {} 分钟了，起来活动一下，休息几分钟再继续吧。", minutes);
    }

    std::vector<SessionRecord> loadSessions() noexcept
    {
        std::vector<SessionRecord> sessions;
        if (!context)
            return sessions;
        std::string all;
        if (const char* cur = context->kvGet("sessions"))
            all = cur;

        size_t pos = 0;
        while (pos < all.size())
        {
            const size_t nl = all.find('\n', pos);
            const std::string_view line = (nl == std::string::npos)
                ? std::string_view(all).substr(pos)
                : std::string_view(all).substr(pos, nl - pos);
            pos = (nl == std::string::npos) ? all.size() : nl + 1;
            if (line.empty())
                continue;

            // 拆分 4 个字段
            SessionRecord rec;
            size_t f = 0, cur2 = 0;
            std::array<std::string_view, 4> fields;
            for (; f < 4 && cur2 <= line.size(); ++f)
            {
                const size_t tab = line.find('\t', cur2);
                fields[f] = (tab == std::string_view::npos)
                    ? line.substr(cur2)
                    : line.substr(cur2, tab - cur2);
                cur2 = (tab == std::string_view::npos) ? line.size() + 1 : tab + 1;
            }
            if (f < 4)
                continue;
            try
            {
                rec.start = std::stoll(std::string(fields[0]));
                rec.end = std::stoll(std::string(fields[1]));
                rec.seconds = std::stoll(std::string(fields[2]));
            }
            catch (...)
            {
                continue;
            }
            rec.config = UnescapeField(fields[3]);
            sessions.push_back(std::move(rec));
        }
        return sessions;
    }

    // 某窗口内 (会话 end 落在窗口内) 各配置的秒数, 按时长降序
    using ConfigSeconds = std::vector<std::pair<std::string, int64_t>>;

    static ConfigSeconds AggregateWindow(const std::vector<SessionRecord>& sessions, int64_t windowStart)
    {
        std::map<std::string, int64_t> acc;
        for (const auto& s : sessions)
            if (s.end >= windowStart)
                acc[s.config] += s.seconds;
        ConfigSeconds out(acc.begin(), acc.end());
        std::sort(out.begin(), out.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        return out;
    }

    static std::string RenderConfigStats(const ConfigSeconds& list)
    {
        std::string out;
        for (const auto& [cfg, secs] : list)
        {
            if (!out.empty())
                out += "&nbsp;&nbsp;·&nbsp;&nbsp;";
            out += EscapeHtml(cfg) + " " + FormatDuration(secs);
        }
        return out.empty() ? "<span style=\"color:#8a94a3\">无记录</span>" : out;
    }

    std::string BuildStatsHtml() noexcept
    {
        const auto sessions = loadSessions();

        int64_t totalSeconds = 0;
        int64_t todaySeconds = 0;
        const auto now = std::chrono::system_clock::now();
        const int64_t nowEpoch = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
        const std::time_t nowT = std::chrono::system_clock::to_time_t(now);
        std::tm todayTm{};
#if defined(_WIN32)
        localtime_s(&todayTm, &nowT);
#else
        localtime_r(&nowT, &todayTm);
#endif
        todayTm.tm_hour = todayTm.tm_min = todayTm.tm_sec = 0;
        const int64_t todayStart = static_cast<int64_t>(std::mktime(&todayTm));

        for (const auto& s : sessions)
        {
            totalSeconds += s.seconds;
            if (s.end >= todayStart)
                todaySeconds += s.seconds;
        }
        const int64_t count = static_cast<int64_t>(sessions.size());
        const int64_t avg = count ? totalSeconds / count : 0;

        std::string html;
        html.reserve(4096);
        html += R"(<div style="font-family:system-ui,'Segoe UI',sans-serif;color:#dbe2ea;padding:4px 2px;font-size:13px;line-height:1.6">)";
        html += "<div style=\"font-size:15px;font-weight:600;margin-bottom:10px\">游戏时长统计</div>";

        html += "<div style=\"display:flex;gap:18px;flex-wrap:wrap;margin-bottom:14px\">";
        html += statCard("总次数", std::to_string(count));
        html += statCard("累计时长", FormatDuration(totalSeconds));
        html += statCard("今日", FormatDuration(todaySeconds));
        html += statCard("平均每次", FormatDuration(avg));
        html += "</div>";

        // 时间维度: 最近 5 小时 / 12 小时 / 24 小时 / 7 天 — 每个窗口内各配置玩了多久
        struct WindowDef { const char* label; int64_t seconds; };
        constexpr WindowDef kWindows[] = {
            {"最近 5 小时", 5 * 3600},
            {"最近 12 小时", 12 * 3600},
            {"最近 24 小时", 24 * 3600},
            {"最近 7 天", 7 * 86400},
        };
        html += "<div style=\"margin-bottom:12px\">";
        html += "<div style=\"font-weight:600;margin-bottom:6px\">时间维度</div>";
        for (const auto& w : kWindows)
        {
            const auto list = AggregateWindow(sessions, nowEpoch - w.seconds);
            html += "<div style=\"margin-bottom:4px\"><span style=\"color:#8a94a3\">" +
                    std::string(w.label) + "：</span>" + RenderConfigStats(list) + "</div>";
        }
        html += "</div>";

        // 超过一周的旧记录: 只按游戏累计 (次数 + 总时长)
        const int64_t weekCutoff = nowEpoch - 7 * 86400;
        std::map<std::string, std::pair<int64_t, int64_t>> oldAcc;   // config -> {seconds, count}
        for (const auto& s : sessions)
            if (s.end < weekCutoff)
            {
                auto& [secs, cnt] = oldAcc[s.config];
                secs += s.seconds;
                cnt += 1;
            }
        html += "<div style=\"margin-bottom:14px\">";
        html += "<div style=\"font-weight:600;margin-bottom:6px\">超过一周（累计）</div>";
        if (oldAcc.empty())
        {
            html += "<div style=\"color:#8a94a3\">无记录</div>";
        }
        else
        {
            html += R"(<table style="width:100%;border-collapse:collapse;font-size:12px">
  <tr style="color:#8a94a3;text-align:left">
    <th style="padding:4px 8px;border-bottom:1px solid #2a3340">配置</th>
    <th style="padding:4px 8px;border-bottom:1px solid #2a3340">次数</th>
    <th style="padding:4px 8px;border-bottom:1px solid #2a3340">总时长</th>
  </tr>)";
            std::vector<std::pair<std::string, std::pair<int64_t, int64_t>>> rows(oldAcc.begin(), oldAcc.end());
            std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
                return a.second.first > b.second.first;
            });
            for (const auto& [cfg, v] : rows)
                html += std::format(
                    "<tr><td style=\"padding:4px 8px;border-bottom:1px solid #20262e\">{}</td>"
                    "<td style=\"padding:4px 8px;border-bottom:1px solid #20262e\">{}</td>"
                    "<td style=\"padding:4px 8px;border-bottom:1px solid #20262e\">{}</td></tr>",
                    EscapeHtml(cfg), v.second, FormatDuration(v.first));
            html += "</table>";
        }
        html += "</div>";

        // 最近 10 次会话明细
        if (!sessions.empty())
        {
            html += "<div style=\"font-weight:600;margin-bottom:6px\">最近会话</div>";
            html += R"(<table style="width:100%;border-collapse:collapse;font-size:12px">
  <tr style="color:#8a94a3;text-align:left">
    <th style="padding:4px 8px;border-bottom:1px solid #2a3340">开始</th>
    <th style="padding:4px 8px;border-bottom:1px solid #2a3340">结束</th>
    <th style="padding:4px 8px;border-bottom:1px solid #2a3340">时长</th>
    <th style="padding:4px 8px;border-bottom:1px solid #2a3340">配置</th>
  </tr>)";
            // 记录按时间顺序追加, 取最后最多 10 条 (会话少时全部显示)
            const size_t total = sessions.size();
            const size_t startRow = total > 10 ? total - 10 : 0;
            for (size_t i = startRow; i < total; ++i)
            {
                const auto& s = sessions[i];
                html += std::format(
                    "<tr><td style=\"padding:4px 8px;border-bottom:1px solid #20262e\">{}</td>"
                    "<td style=\"padding:4px 8px;border-bottom:1px solid #20262e\">{}</td>"
                    "<td style=\"padding:4px 8px;border-bottom:1px solid #20262e\">{}</td>"
                    "<td style=\"padding:4px 8px;border-bottom:1px solid #20262e\">{}</td></tr>",
                    EscapeHtml(FormatTime(s.start)), EscapeHtml(FormatTime(s.end)),
                    FormatDuration(s.seconds), EscapeHtml(s.config));
            }
            html += "</table>";
        }
        else
        {
            html += "<div style=\"color:#8a94a3\">还没有会话记录 —— 激活一个游戏配置开始计时，切换到“关闭”结束并记录。</div>";
        }
        html += "</div>";
        return html;
    }

    static std::string statCard(const std::string& label, const std::string& value)
    {
        return "<div style=\"background:#171d26;border:1px solid #262f3b;border-radius:8px;"
               "padding:8px 12px;min-width:86px\">"
               "<div style=\"color:#8a94a3;font-size:11px\">" + label + "</div>"
               "<div style=\"font-size:14px;font-weight:600;margin-top:2px\">" + value + "</div></div>";
    }

    ILogger* logger = nullptr;
    IPluginContext* context = nullptr;
    std::vector<PluginParameterInfo> parameters;

    Clock::time_point sessionStart{};                    // steady: 时长计算
    std::chrono::system_clock::time_point wallStart{};   // wall: 记录/展示时间
    std::string sessionConfig;
    bool inSession = false;
    int64_t thresholdMinutes = 60;
    std::string customMessage;

    std::string uiBuffer_;   // customInfo() 返回缓冲 (稳定到下一次调用)
};
} // namespace

REGISTER_PLUGIN(HealthPlugin)