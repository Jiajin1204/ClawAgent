#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <sstream>
#include <chrono>

namespace ClawAgent {

/**
 * @brief 日志级别
 */
enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

/**
 * @brief 日志器 - 统一的日志输出
 */
class Logger {
public:
    static Logger& instance();

    // 初始化
    void init(LogLevel level = LogLevel::Info,
              const std::string& filepath = "",
              bool console_output = true);

    // 日志输出
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);

    // 格式化日志
    void log(LogLevel level, const std::string& message);

    // 设置级别
    void setLevel(LogLevel level);
    void setLevel(const std::string& level);

    // 关闭日志文件
    void close();

private:
    Logger() = default;
    ~Logger() { close(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string levelToString(LogLevel level) const;
    std::string getCurrentTime() const;
    void write(const std::string& message);

    LogLevel min_level_ = LogLevel::Info;
    std::ofstream file_;
    bool console_output_ = true;
    mutable std::mutex mutex_;
};

// 日志宏
#define LOG_DEBUG(msg) ClawAgent::Logger::instance().debug(msg)
#define LOG_INFO(msg) ClawAgent::Logger::instance().info(msg)
#define LOG_WARNING(msg) ClawAgent::Logger::instance().warning(msg)
#define LOG_ERROR(msg) ClawAgent::Logger::instance().error(msg)

} // namespace ClawAgent
