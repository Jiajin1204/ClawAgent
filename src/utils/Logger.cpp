#include "utils/Logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

using namespace ClawAgent;

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::init(LogLevel level, const std::string& filepath, bool console_output) {
    std::lock_guard<std::mutex> lock(mutex_);

    min_level_ = level;
    console_output_ = console_output;

    if (!filepath.empty()) {
        file_.open(filepath, std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "无法打开日志文件: " << filepath << std::endl;
        }
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < min_level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::stringstream ss;
    ss << "[" << getCurrentTime() << "] ";
    ss << "[" << levelToString(level) << "] ";
    ss << message;

    std::string log_line = ss.str();

    // 输出到控制台
    if (console_output_) {
        if (level == LogLevel::Error) {
            std::cerr << log_line << std::endl;
        } else {
            std::cout << log_line << std::endl;
        }
    }

    // 输出到文件
    if (file_.is_open()) {
        file_ << log_line << std::endl;
        file_.flush();
    }
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

void Logger::setLevel(const std::string& level) {
    if (level == "debug") {
        setLevel(LogLevel::Debug);
    } else if (level == "info") {
        setLevel(LogLevel::Info);
    } else if (level == "warning") {
        setLevel(LogLevel::Warning);
    } else if (level == "error") {
        setLevel(LogLevel::Error);
    }
}

void Logger::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

std::string Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

std::string Logger::getCurrentTime() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void Logger::write(const std::string& message) {
    if (file_.is_open()) {
        file_ << message << std::endl;
    }
}
