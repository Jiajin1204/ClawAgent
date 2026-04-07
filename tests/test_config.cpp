#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "config/ConfigManager.hpp"

namespace fs = std::filesystem;
using namespace ClawAgent;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建临时配置文件用于测试
        test_config_path_ = "/tmp/test_config.json";
        std::ofstream(test_config_path_) << R"({
            "model": {
                "provider": "openai",
                "name": "test-model",
                "api_key": "test-key",
                "base_url": "https://api.test.com",
                "stream": true,
                "timeout_ms": 30000
            },
            "message": {
                "max_history": 10,
                "persist_path": "/tmp/messages",
                "enable_compression": false
            },
            "agent": {
                "system_prompt": "You are a test assistant.",
                "max_iterations": 5,
                "stop_on_error": true
            },
            "tools": {
                "enable_read": true,
                "enable_write": false,
                "enable_exec": true,
                "exec_timeout_ms": 5000
            },
            "output": {
                "show_tools": true,
                "show_thinking": false,
                "color_output": false
            },
            "logging": {
                "level": "debug",
                "file": "/tmp/test.log"
            }
        })";
    }

    void TearDown() override {
        fs::remove(test_config_path_);
    }

    std::string test_config_path_;
};

TEST_F(ConfigTest, LoadConfig) {
    ConfigManager config(test_config_path_);
    bool loaded = config.load(test_config_path_);
    EXPECT_TRUE(loaded);
    EXPECT_TRUE(config.isLoaded());
}

TEST_F(ConfigTest, GetModelConfig) {
    ConfigManager config(test_config_path_);
    config.load(test_config_path_);
    auto model_config = config.getModelConfig();

    EXPECT_EQ(model_config.provider, "openai");
    EXPECT_EQ(model_config.name, "test-model");
    EXPECT_EQ(model_config.api_key, "test-key");
    EXPECT_EQ(model_config.base_url, "https://api.test.com");
    EXPECT_TRUE(model_config.stream);
    EXPECT_EQ(model_config.timeout_ms, 30000);
}

TEST_F(ConfigTest, GetMessageConfig) {
    ConfigManager config(test_config_path_);
    config.load(test_config_path_);
    auto msg_config = config.getMessageConfig();

    EXPECT_EQ(msg_config.max_history, 10);
    EXPECT_EQ(msg_config.persist_path, "/tmp/messages");
    EXPECT_FALSE(msg_config.enable_compression);
}

TEST_F(ConfigTest, GetAgentConfig) {
    ConfigManager config(test_config_path_);
    config.load(test_config_path_);
    auto agent_config = config.getAgentConfig();

    EXPECT_EQ(agent_config.system_prompt, "You are a test assistant.");
    EXPECT_EQ(agent_config.max_iterations, 5);
    EXPECT_TRUE(agent_config.stop_on_error);
}

TEST_F(ConfigTest, GetToolConfig) {
    ConfigManager config(test_config_path_);
    config.load(test_config_path_);
    auto tool_config = config.getToolsConfig();

    EXPECT_TRUE(tool_config.enable_read);
    EXPECT_FALSE(tool_config.enable_write);
    EXPECT_TRUE(tool_config.enable_exec);
    EXPECT_EQ(tool_config.exec_timeout_ms, 5000);
}

TEST_F(ConfigTest, GetOutputConfig) {
    ConfigManager config(test_config_path_);
    config.load(test_config_path_);
    auto output_config = config.getOutputConfig();

    EXPECT_TRUE(output_config.show_tools);
    EXPECT_FALSE(output_config.show_thinking);
    EXPECT_FALSE(output_config.color_output);
}

TEST_F(ConfigTest, GetLoggingConfig) {
    ConfigManager config(test_config_path_);
    config.load(test_config_path_);
    auto logging_config = config.getLoggingConfig();

    EXPECT_EQ(logging_config.level, "debug");
    EXPECT_EQ(logging_config.file, "/tmp/test.log");
}

TEST_F(ConfigTest, InvalidPath) {
    ConfigManager config("/nonexistent/path.json");
    EXPECT_FALSE(config.load("/nonexistent/path.json"));
    EXPECT_FALSE(config.isLoaded());
}

TEST_F(ConfigTest, GetterMethodsReturnDefaultsWhenNotLoaded) {
    ConfigManager config("/nonexistent/path.json");
    config.load("/nonexistent/path.json");

    // 应该返回默认配置而不是崩溃
    auto model_config = config.getModelConfig();
    EXPECT_EQ(model_config.provider, "");

    auto agent_config = config.getAgentConfig();
    EXPECT_EQ(agent_config.max_iterations, 0); // 默认值（未加载时为0）
}
