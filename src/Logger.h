#pragma once
#include <IPlugin.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <format>
#include <vector>
#include <boost/describe.hpp>

struct LogEntry
{
    std::string  time;          // display form: "YYYY-MM-DD HH:MM:SS.mmm" (local)
    std::int64_t timestamp;     // epoch seconds — unambiguous for filtering
    ILogger::Level level;
    std::string tag;
    std::string message;
};
BOOST_DESCRIBE_STRUCT(LogEntry, (), (time,timestamp,level, tag, message))
BOOST_DESCRIBE_ENUM(ILogger::Level,  Info, Warning, Error)


class Logger : public ILogger {
public:
    void log(Level level, const char* tag, const char* message) override;
    Logger();
    ~Logger() override;

    // Subscribe to log entries. The callback receives serialized JSON for each
    // entry and is called from the log-writing thread — the listener is
    // responsible for any thread-safety / UI-thread dispatch.
    // Returns an id that can be passed to removeLogListener().
    size_t addLogListener(std::function<void(const std::string&)> listener);
    void removeLogListener(size_t id);

    // Re-read the tail of the currently-written log file (today's log-*.txt)
    // as LogEntry list — the durable history behind the live sink, so a page
    // that (re)loads later can show what happened before it loaded (e.g. while
    // the window was hidden in the tray and its WebView destroyed). Lines are
    // parsed back from the on-disk text format; best-effort: lines still being
    // written (or otherwise unparseable) are skipped, and the display time in
    // each line is converted back to an epoch timestamp. Safe from any thread.
    std::vector<LogEntry> recentFromLogFile(size_t max_entries = 500) const;

    template <typename... Args>
    void Info(const char* tag, std::format_string<Args...> fmt, Args&&... args) {
        std::string msg = std::format(fmt, std::forward<Args>(args)...);
        log(Level::Info, tag, msg.c_str());
    }

    template <typename... Args>
    void Warning(const char* tag, std::format_string<Args...> fmt, Args&&... args) {
        std::string msg = std::format(fmt, std::forward<Args>(args)...);
        log(Level::Warning, tag, msg.c_str());
    }

    template <typename... Args>
    void Error(const char* tag, std::format_string<Args...> fmt, Args&&... args) {
        std::string msg = std::format(fmt, std::forward<Args>(args)...);
        log(Level::Error, tag, msg.c_str());
    }

private:
    struct Impl;
    std::unique_ptr<Impl> m_;

    // ---- COW log sinks (write-rare, read-hot) ----
    // The log thread reads the snapshot via load() — lock-free.
    // add/remove copy the vector and store() a new snapshot atomically.
    struct LogSink {
        size_t id;
        std::function<void(const std::string&)> callback;
    };
    std::atomic<std::shared_ptr<const std::vector<LogSink>>> m_sinks;
    size_t m_nextSinkId = 0;
    std::mutex m_writeMutex;   // serialises writers only, never held on read path
};

class TagLogger {
public:
    TagLogger(Logger& logger, const char* tag) : logger_(logger), tag_(tag) {}

    template <typename... Args>
    void Info(std::format_string<Args...> fmt, Args&&... args) {
        logger_.Info(tag_, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Warning(std::format_string<Args...> fmt, Args&&... args) {
        logger_.Warning(tag_, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void Error(std::format_string<Args...> fmt, Args&&... args) {
        logger_.Error(tag_, fmt, std::forward<Args>(args)...);
    }

private:
    Logger& logger_;
    const char* tag_;
};

