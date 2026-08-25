#include "Logger.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <print>

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
    moodycamel::ConcurrentQueue<LogEntry> logQueue;
    std::jthread logThread;
};

Logger::Logger()
    :m_(std::make_unique<Impl>()), m_sinks(std::make_shared<const std::vector<LogSink>>())
{
    std::filesystem::create_directory(LogDir());

    auto now = std::chrono::system_clock::now();
    auto date = std::format("{:%Y-%m-%d}", now);
    auto logFilePath = LogDir() / std::format("log-{}.txt", date);
    m_->logFile.open(logFilePath, std::ios::app);
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


