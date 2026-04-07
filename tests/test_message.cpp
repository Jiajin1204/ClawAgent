#include <gtest/gtest.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include "message/MessageManager.hpp"
#include "message/Message.hpp"

namespace fs = std::filesystem;
using namespace ClawAgent;

class MessageTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/clawagent_test_messages";
        fs::create_directories(test_dir_);
        manager_ = std::make_unique<MessageManager>(10, test_dir_, false);
    }

    void TearDown() override {
        manager_.reset();
        fs::remove_all(test_dir_);
    }

    std::string test_dir_;
    std::unique_ptr<MessageManager> manager_;
};

TEST_F(MessageTest, AddMessage) {
    manager_->addMessage("user", "Hello");
    auto history = manager_->getHistory();

    EXPECT_EQ(history.size(), 1);
    EXPECT_EQ(history[0].role, "user");
    EXPECT_EQ(history[0].content, "Hello");
}

TEST_F(MessageTest, AddMultipleMessages) {
    manager_->addMessage("user", "Hello");
    manager_->addMessage("assistant", "Hi there!");
    manager_->addMessage("user", "How are you?");

    auto history = manager_->getHistory();
    EXPECT_EQ(history.size(), 3);
}

TEST_F(MessageTest, GetHistoryLimit) {
    for (int i = 0; i < 15; ++i) {
        manager_->addMessage("user", "Message " + std::to_string(i));
    }

    // 应该只保留最后10条
    auto history = manager_->getHistory();
    EXPECT_EQ(history.size(), 10);
}

TEST_F(MessageTest, ClearHistory) {
    manager_->addMessage("user", "Hello");
    manager_->addMessage("assistant", "Hi");

    manager_->clearHistory();
    auto history = manager_->getHistory();

    EXPECT_TRUE(history.empty());
}

TEST_F(MessageTest, NewSession) {
    manager_->addMessage("user", "Hello");
    auto old_session = manager_->getSessionId();

    // 等待较长时间确保 session ID 会变化
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    manager_->newSession();
    auto new_session = manager_->getSessionId();

    EXPECT_NE(old_session, new_session);
    EXPECT_TRUE(manager_->getHistory().empty());
}

TEST_F(MessageTest, SaveAndLoad) {
    manager_->addMessage("user", "Hello");
    manager_->addMessage("assistant", "Hi there!");

    std::string save_path = "/tmp/test_messages.jsonl";
    bool save_result = manager_->saveToFile(save_path);
    EXPECT_TRUE(save_result);

    // 创建新的 manager 并加载
    MessageManager new_manager(10, "/tmp", false);
    bool load_result = new_manager.loadFromFile(save_path);
    EXPECT_TRUE(load_result);

    auto history = new_manager.getHistory();
    EXPECT_EQ(history.size(), 2);

    fs::remove(save_path);
}

TEST_F(MessageTest, MessageToJson) {
    Message msg("user", "Hello");
    json j = msg.toJson();

    EXPECT_EQ(j["role"], "user");
    EXPECT_EQ(j["content"], "Hello");
}

TEST_F(MessageTest, MessageFromJson) {
    json j = {
        {"role", "assistant"},
        {"content", "Hello!"}
    };
    Message msg = Message::fromJson(j);

    EXPECT_EQ(msg.role, "assistant");
    EXPECT_EQ(msg.content, "Hello!");
}

TEST_F(MessageTest, MessageWithToolCallId) {
    json j = {
        {"role", "tool"},
        {"content", "tool result"},
        {"tool_call_id", "call_123"}
    };
    Message msg = Message::fromJson(j);

    EXPECT_EQ(msg.role, "tool");
    EXPECT_EQ(msg.tool_call_id, "call_123");
}

TEST_F(MessageTest, GetMessagesForLLM) {
    manager_->addMessage("user", "Hello");
    manager_->addMessage("assistant", "Hi");

    auto llm_messages = manager_->getMessagesForLLM();
    EXPECT_EQ(llm_messages.size(), 2);
    EXPECT_EQ(llm_messages[0].role, "user");
    EXPECT_EQ(llm_messages[1].role, "assistant");
}

TEST_F(MessageTest, ChatMessageToJson) {
    ChatMessage msg;
    msg.role = "assistant";
    msg.content = "Hello!";
    msg.timestamp = "2024-01-01 12:00:00.000";
    msg.tool_name = "exec";

    json j = msg.toJson();
    EXPECT_EQ(j["role"], "assistant");
    EXPECT_EQ(j["content"], "Hello!");
    EXPECT_EQ(j["tool_name"], "exec");
}

TEST_F(MessageTest, ChatMessageFromJson) {
    json j = {
        {"role", "tool"},
        {"content", "result"},
        {"timestamp", "2024-01-01 12:00:00.000"},
        {"tool_call_id", "call_456"},
        {"tool_name", "read"}
    };
    ChatMessage msg = ChatMessage::fromJson(j);

    EXPECT_EQ(msg.role, "tool");
    EXPECT_EQ(msg.content, "result");
    EXPECT_EQ(msg.tool_call_id, "call_456");
    EXPECT_EQ(msg.tool_name, "read");
}
