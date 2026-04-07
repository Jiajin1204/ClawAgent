#pragma once

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <nlohmann/json.hpp>
#include "Message.hpp"

using json = nlohmann::json;

namespace ClawAgent {

/**
 * @brief 消息管理器 - 管理短期记忆和上下文窗口
 *
 * 功能：
 * 1. 维护历史消息列表
 * 2. 支持按条数限制（max_history）
 * 3. 支持JSONL文件持久化
 * 4. 简单的消息压缩（可选）
 * 5. 线程安全，支持多实例
 */
class MessageManager {
public:
    MessageManager(int max_history = 20,
                   const std::string& persist_path = "./messages",
                   bool enable_compression = false);
    ~MessageManager();

    // 禁用拷贝，使用指针
    MessageManager(const MessageManager&) = delete;
    MessageManager& operator=(const MessageManager&) = delete;

    // 添加消息
    void addMessage(const ChatMessage& message);
    void addMessage(const std::string& role, const std::string& content);

    // 获取消息历史
    std::vector<ChatMessage> getHistory(int max_count = -1) const;
    std::vector<Message> getMessagesForLLM() const;

    // 管理功能
    void clearHistory();
    void newSession();
    bool saveToFile(const std::string& filepath = "") const;
    bool loadFromFile(const std::string& filepath = "");

    // 压缩消息（可选功能）
    std::string compressMessages() const;
    void decompressMessages(const std::string& compressed);

    // 获取统计信息
    size_t getMessageCount() const { std::shared_lock lock(mutex_); return messages_.size(); }
    int getMaxHistory() const { return max_history_; }

    // 会话ID
    std::string getSessionId() const { std::shared_lock lock(mutex_); return session_id_; }
    void setSessionId(const std::string& id) { std::unique_lock lock(mutex_); session_id_ = id; }

private:
    void trimHistory();
    std::string generateSessionId();
    std::string getDefaultFilepath() const;
    std::string getCurrentTimestamp() const;

    mutable std::shared_mutex mutex_;
    std::deque<ChatMessage> messages_;
    int max_history_;
    std::string persist_path_;
    bool enable_compression_;
    std::string session_id_;
};

/**
 * @brief 消息持久化器 - 处理JSONL文件的读写
 */
class MessagePersister {
public:
    static bool saveMessages(const std::vector<ChatMessage>& messages,
                             const std::string& filepath);
    static bool loadMessages(const std::string& filepath,
                             std::vector<ChatMessage>& messages);
    static bool appendMessage(const ChatMessage& message,
                              const std::string& filepath);
};

} // namespace ClawAgent
