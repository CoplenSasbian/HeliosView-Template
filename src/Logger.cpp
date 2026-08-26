#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <print>
#include <string_view>
#include <vector>

#include <boost/json.hpp>

#include "utils/AppFilePath.h"
#include "utils/concurrentqueue.h"
static const char* LevelToString(ILogger::Level level)
{
   switch (level)
   {
   case ILogger::Info:
       return "Info";
   case ILogger::Warning:
       return "Warning";
   case ILogger::Error:
       return "Error";
   default:
       return "Unknown";
   }
}

// ILogger::Level is a plain enum (no BOOST_DESCRIBE_ENUM), so give
// boost::json a conversion for it: LogEntry gets serialized via
// BOOST_DESCRIBE_STRUCT, which needs a value_from for every member.


struct Logger::Impl
{
    std::ofstream logFile;
    std::filesystem::path logFilePath; /* today's log-YYYY-MM-DD.txt (also used by recentFromLogFile) */
    moodycamel::ConcurrentQueue<LogEntry> logQueue;
    std::jthread logThread;
};

Logger::Logger()
    :m_(std::make_unique<Impl>()), m_sinks(std::make_shared<const std::vector<LogSink>>())
{
    std::filesystem::create_directory(LogDir());

    auto now = std::chrono::system_clock::now();
    auto date = std::format("{:%Y-%m-%d}", now);
    m_->logFilePath = LogDir() / std::format("log-{}.txt", date);
    m_->logFile.open(m_->logFilePath, std::ios::app);
    if (!m_->logFile.is_open())
    {
        throw std::runtime_error("Failed to open log file");
    }
    m_->logThread = std::jthread([this](std::stop_token token) {
        moodycamel::ConsumerToken consumerToken(m_->logQueue);
        std::vector<LogEntry> batch;
        batch.resize(256);
        while (!token.stop_requested())
        {

            size_t count = m_->logQueue.try_dequeue_bulk(consumerToken, batch.data(), batch.capacity());
            if (count == 0) {
               std::this_thread::sleep_for(std::chrono::milliseconds(5));
               continue;
            }
            for (size_t i = 0; i < count; ++i)
            {
                auto entry = std::move(batch[i]);
                std::string logMessage = std::format("[{}][{}][{}] {}\n", entry.time, LevelToString(entry.level), entry.tag, entry.message);
                std::print("{}",logMessage);
                m_->logFile << logMessage;
                boost::json::value jv = boost::json::value_from(entry);
                auto json = boost::json::serialize(jv);

                auto sinks = m_sinks.load();
                for (auto& sink : *sinks)
                    sink.callback(json);
            }
            m_->logFile.flush();
        }
    });
}

Logger::~Logger()
{
    m_->logThread.request_stop();
    m_->logThread.join();
    m_->logFile.close();
}

size_t Logger::addLogListener(std::function<void(const std::string&)> listener)
{
    std::lock_guard lock(m_writeMutex);
    auto copy = std::make_shared<std::vector<LogSink>>(*m_sinks.load());
    size_t id = ++m_nextSinkId;
    copy->push_back({id, std::move(listener)});
    m_sinks.store(std::move(copy));
    return id;
}

void Logger::removeLogListener(size_t id)
{
    std::lock_guard lock(m_writeMutex);
    auto copy = std::make_shared<std::vector<LogSink>>(*m_sinks.load());
    std::erase_if(*copy, [id](const LogSink& s) { return s.id == id; });
    m_sinks.store(std::move(copy));
}


void Logger::log(Level level, const char* tag, const char* message)
{
    static thread_local moodycamel::ProducerToken token(m_->logQueue);
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    auto msg = LogEntry{
        .time = std::format("{:%Y-%m-%d %H:%M:%S}.{:03}", now, ms),
        .timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count(),
        .level = level, .tag = tag, .message = message};
    m_->logQueue.enqueue(token,std::move(msg));
}

namespace {

// [time][Level][tag] message — level token is one of the enum names.
ILogger::Level ParseLevel(std::string_view s)
{
    if (s == "Warning") return ILogger::Warning;
    if (s == "Error")   return ILogger::Error;
    return ILogger::Info;
}

// "YYYY-MM-DD HH:MM:SS.mmm" (the display time, local) -> epoch seconds.
// 0 on failure (the frontend then treats the entry as untimed, which only
// disables its time-range filter / relative label — it still shows).
int64_t ParseDisplayTimeEpoch(std::string_view t)
{
    std::string s(t); /* sscanf needs a NUL-terminated buffer */
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6)
        return 0;
    struct tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = sec;
    tm.tm_isdst = -1; // let mktime resolve DST (the written time is local)
    return static_cast<int64_t>(std::mktime(&tm));
}

} // namespace

std::vector<LogEntry> Logger::recentFromLogFile(size_t max_entries) const
{
    std::vector<LogEntry> out;
    if (!m_ || m_->logFilePath.empty())
        return out;

    // Open the file that the log thread is appending to. The MSVC CRT opens
    // streams without an exclusive lock, so a concurrent read sees the data
    // the writer flushed; lines still being written are skipped by the parser.
    std::ifstream file(m_->logFilePath, std::ios::binary);
    if (!file.is_open())
        return out;

    // Read only the tail (a long-lived log file must not be slurped whole).
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    constexpr std::streamoff kTailBytes = 512 * 1024;
    const std::streamoff start = fileSize > kTailBytes ? fileSize - kTailBytes : 0;
    file.seekg(start);
    std::string data(static_cast<std::size_t>(fileSize - start), '\0');
    file.read(data.data(), static_cast<std::streamsize>(data.size()));
    file.close();

    // Split into lines; drop the (cut-off) first line when we started mid-file.
    std::vector<std::string_view> lines;
    std::string_view rest(data);
    for (;;) {
        const auto nl = rest.find('\n');
        if (nl == std::string_view::npos) {
            lines.push_back(rest);
            break;
        }
        lines.push_back(rest.substr(0, nl));
        rest.remove_prefix(nl + 1);
    }
    if (start != 0 && !lines.empty())
        lines.erase(lines.begin());

    // Parse into LogEntry (chronological, oldest first).
    std::vector<LogEntry> entries;
    entries.reserve(std::min(lines.size(), max_entries));
    for (const std::string_view line : lines) {
        if (line.empty())
            continue;
        // [time][Level][tag] message  — time/level/tag never contain ']' (they
        // are ours); the message may, and is taken verbatim after the 3rd ']'.
        const auto c1 = line.find(']');
        if (c1 == std::string_view::npos || c1 < 2 || line[0] != '[')
            continue;
        LogEntry e;
        e.time = std::string(line.substr(1, c1 - 1));
        const auto b2 = c1 + 1;
        if (b2 >= line.size() || line[b2] != '[')
            continue;
        const auto c2 = line.find(']', b2 + 1);
        if (c2 == std::string_view::npos)
            continue;
        e.level = ParseLevel(line.substr(b2 + 1, c2 - b2 - 1));
        const auto b3 = c2 + 1;
        if (b3 >= line.size() || line[b3] != '[')
            continue;
        const auto c3 = line.find(']', b3 + 1);
        if (c3 == std::string_view::npos)
            continue;
        e.tag = std::string(line.substr(b3 + 1, c3 - b3 - 1));
        std::string_view msg = line.substr(c3 + 1);
        if (!msg.empty() && msg.front() == ' ')
            msg.remove_prefix(1);
        e.message = std::string(msg);
        e.timestamp = ParseDisplayTimeEpoch(e.time);
        entries.push_back(std::move(e));
    }

    const size_t keep = std::min(entries.size(), max_entries);
    if (keep < entries.size())
        entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(keep));
    return entries;
}


