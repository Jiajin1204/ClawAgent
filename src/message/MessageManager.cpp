#include "message/MessageManager.hpp"
#include "utils/Logger.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>

using namespace ClawAgent;

MessageManager::MessageManager(int max_history,
                                const std::string& persist_path,
                                bool enable_compression)
    : max_history_(max_history)
    , persist_path_(persist_path)
    , enable_compression_(enable_compression)
    , session_id_(generateSessionId()) {
}

MessageManager::~MessageManager() = default;

void MessageManager::addMessage(const ChatMessage& message) {
    std::unique_lock lock(mutex_);
    messages_.push_back(message);
    trimHistory();
}

void MessageManager::addMessage(const std::string& role, const std::string& content) {
    ChatMessage msg(role, content);
    msg.timestamp = getCurrentTimestamp();
    addMessage(msg);
}

std::string MessageManager::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::vector<ChatMessage> MessageManager::getHistory(int max_count) const {
    std::shared_lock lock(mutex_);
    std::vector<ChatMessage> result;

    if (max_count < 0 || max_count >= static_cast<int>(messages_.size())) {
        result.assign(messages_.begin(), messages_.end());
    } else {
        auto it = messages_.end() - max_count;
        result.assign(it, messages_.end());
    }

    return result;
}

std::vector<Message> MessageManager::getMessagesForLLM() const {
    std::shared_lock lock(mutex_);
    std::vector<Message> result;

    for (const auto& msg : messages_) {
        Message m;
        m.role = msg.role;
        m.content = msg.content;
        if (!msg.tool_call_id.empty()) {
            m.tool_call_id = msg.tool_call_id;
        }
        if (!msg.content_blocks.empty()) {
            m.content_blocks = msg.content_blocks;
        }
        result.push_back(m);
    }

    return result;
}

void MessageManager::clearHistory() {
    std::unique_lock lock(mutex_);
    messages_.clear();
    Logger::instance().info("消息历史已清除");
}

void MessageManager::newSession() {
    std::unique_lock lock(mutex_);
    messages_.clear();
    session_id_ = generateSessionId();
    Logger::instance().info("新会话创建: " + session_id_);
}

bool MessageManager::saveToFile(const std::string& filepath) const {
    std::shared_lock lock(mutex_);

    std::string path = filepath.empty() ? getDefaultFilepath() : filepath;

    // 确保目录存在
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        std::string dir = path.substr(0, pos);
        mkdir(dir.c_str(), 0755);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::instance().error("无法打开文件写入: " + path);
        return false;
    }

    for (const auto& msg : messages_) {
        json j = msg.toJson();
        file << j.dump() << "\n";
    }

    file.close();
    Logger::instance().info("消息已保存到: " + path);
    return true;
}

bool MessageManager::loadFromFile(const std::string& filepath) {
    std::unique_lock lock(mutex_);

    std::string path = filepath.empty() ? getDefaultFilepath() : filepath;

    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::instance().error("无法打开文件读取: " + path);
        return false;
    }

    messages_.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            json j = json::parse(line);
            messages_.push_back(ChatMessage::fromJson(j));
        } catch (const json::parse_error& e) {
            Logger::instance().warning("跳过无效JSON行: " + std::string(e.what()));
        }
    }

    file.close();
    Logger::instance().info("消息已从文件加载: " + path + ", 共 " + std::to_string(messages_.size()) + " 条");
    return true;
}

void MessageManager::trimHistory() {
    while (static_cast<int>(messages_.size()) > max_history_) {
        messages_.pop_front();
    }
}

std::string MessageManager::generateSessionId() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "session_";
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");

    // 添加随机部分
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    ss << "_" << std::rand() % 10000;

    return ss.str();
}

std::string MessageManager::getDefaultFilepath() const {
    std::stringstream ss;
    ss << persist_path_ << "/" << session_id_ << ".jsonl";
    return ss.str();
}

std::string MessageManager::compressMessages() const {
    // 简单的消息压缩：将多条消息合并为一条摘要
    std::shared_lock lock(mutex_);

    if (messages_.empty()) {
        return "";
    }

    std::stringstream ss;
    ss << "[压缩消息，共 " << messages_.size() << " 条]\n";

    for (const auto& msg : messages_) {
        ss << msg.role << ": " << msg.content.substr(0, 100);
        if (msg.content.length() > 100) {
            ss << "...";
        }
        ss << "\n";
    }

    return ss.str();
}

void MessageManager::decompressMessages(const std::string& /*compressed*/) {
    // 压缩的消息无法完全还原，这里只是演示
    Logger::instance().warning("消息解压功能仅作演示，无法完全恢复原始数据");
}

// ============ MessagePersister Implementation ============

bool MessagePersister::saveMessages(const std::vector<ChatMessage>& messages,
                                    const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& msg : messages) {
        json j = msg.toJson();
        file << j.dump() << "\n";
    }

    return true;
}

bool MessagePersister::loadMessages(const std::string& filepath,
                                    std::vector<ChatMessage>& messages) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    messages.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            json j = json::parse(line);
            messages.push_back(ChatMessage::fromJson(j));
        } catch (...) {
            continue;
        }
    }

    return true;
}

bool MessagePersister::appendMessage(const ChatMessage& message,
                                     const std::string& filepath) {
    std::ofstream file(filepath, std::ios::app);
    if (!file.is_open()) {
        return false;
    }

    json j = message.toJson();
    file << j.dump() << "\n";

    return true;
}
