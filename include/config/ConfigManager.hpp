#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ClawAgent {

/**
 * @brief 配置管理器 - 统一管理系统所有配置
 */
class ConfigManager {
public:
    ConfigManager(const std::string& config_path = "config.json");
    ~ConfigManager();

    // 加载配置
    bool load(const std::string& path);
    bool save(const std::string& path = "");

    // 获取配置值
    template<typename T>
    T get(const std::string& key, const T& default_value = T()) const;

    // 模型配置
    struct ModelConfig {
        std::string provider;       // "openai" or "anthropic"
        std::string name;
        std::string api_key;
        std::string base_url;
        bool stream;
        int timeout_ms;
    };
    ModelConfig getModelConfig() const;

    // 消息配置
    struct MessageConfig {
        int max_history;
        std::string persist_path;
        bool enable_compression;
    };
    MessageConfig getMessageConfig() const;

    // Agent配置
    struct AgentConfig {
        std::string system_prompt;
        int max_iterations;
        bool stop_on_error;
    };
    AgentConfig getAgentConfig() const;

    // 工具配置
    struct ToolsConfig {
        bool enable_read;
        bool enable_write;
        bool enable_exec;
        int exec_timeout_ms;
    };
    ToolsConfig getToolsConfig() const;

    // 输出配置
    struct OutputConfig {
        bool show_tools;
        bool show_thinking;
        bool color_output;
    };
    OutputConfig getOutputConfig() const;

    // 日志配置
    struct LoggingConfig {
        std::string level;
        std::string file;
    };
    LoggingConfig getLoggingConfig() const;

    const json& getRawConfig() const { return config_; }
    bool isLoaded() const { return !config_.empty(); }

private:
    json config_;
    std::string config_path_;
};

template<typename T>
T ConfigManager::get(const std::string& key, const T& default_value) const {
    try {
        size_t pos = key.find('.');
        if (pos == std::string::npos) {
            return config_.value(key, default_value);
        }
        std::string first = key.substr(0, pos);
        std::string rest = key.substr(pos + 1);
        return config_[first].value(rest, default_value);
    } catch (...) {
        return default_value;
    }
}

} // namespace ClawAgent
