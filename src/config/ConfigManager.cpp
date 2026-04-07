#include "config/ConfigManager.hpp"
#include "utils/Logger.hpp"

#include <fstream>
#include <sstream>

using namespace ClawAgent;

ConfigManager::ConfigManager(const std::string& config_path)
    : config_path_(config_path) {
}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::load(const std::string& path) {
    std::string filepath = path.empty() ? config_path_ : path;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("无法打开配置文件: " + filepath);
        return false;
    }

    try {
        file >> config_;
        LOG_INFO("配置文件加载成功: " + filepath);
        return true;
    } catch (const json::parse_error& e) {
        LOG_ERROR("JSON解析错误: " + std::string(e.what()));
        return false;
    }
}

bool ConfigManager::save(const std::string& path) {
    std::string filepath = path.empty() ? config_path_ : path;

    std::ofstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR("无法保存配置文件: " + filepath);
        return false;
    }

    try {
        file << config_.dump(4);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("保存配置失败: " + std::string(e.what()));
        return false;
    }
}

ConfigManager::ModelConfig ConfigManager::getModelConfig() const {
    ModelConfig cfg;
    if (config_.empty()) return cfg;
    const json& model = config_.value("model", json::object());

    cfg.provider = model.value("provider", "openai");
    cfg.name = model.value("name", "gpt-4");
    cfg.api_key = model.value("api_key", "");
    cfg.base_url = model.value("base_url", "https://api.openai.com/v1");
    cfg.stream = model.value("stream", true);
    cfg.timeout_ms = model.value("timeout_ms", 120000);

    return cfg;
}

ConfigManager::MessageConfig ConfigManager::getMessageConfig() const {
    MessageConfig cfg;
    if (config_.empty()) return cfg;
    const json& msg = config_.value("message", json::object());

    cfg.max_history = msg.value("max_history", 20);
    cfg.persist_path = msg.value("persist_path", "./messages");
    cfg.enable_compression = msg.value("enable_compression", false);

    return cfg;
}

ConfigManager::AgentConfig ConfigManager::getAgentConfig() const {
    AgentConfig cfg;
    if (config_.empty()) return cfg;
    const json& agent = config_.value("agent", json::object());

    cfg.system_prompt = agent.value("system_prompt",
        "你是一个有帮助的AI助手。");
    cfg.max_iterations = agent.value("max_iterations", 50);
    cfg.stop_on_error = agent.value("stop_on_error", true);

    return cfg;
}

ConfigManager::ToolsConfig ConfigManager::getToolsConfig() const {
    ToolsConfig cfg;
    if (config_.empty()) return cfg;
    const json& tools = config_.value("tools", json::object());

    cfg.enable_read = tools.value("enable_read", true);
    cfg.enable_write = tools.value("enable_write", true);
    cfg.enable_exec = tools.value("enable_exec", true);
    cfg.exec_timeout_ms = tools.value("exec_timeout_ms", 300000);

    return cfg;
}

ConfigManager::OutputConfig ConfigManager::getOutputConfig() const {
    OutputConfig cfg;
    if (config_.empty()) return cfg;
    const json& output = config_.value("output", json::object());

    cfg.show_tools = output.value("show_tools", true);
    cfg.show_thinking = output.value("show_thinking", false);
    cfg.color_output = output.value("color_output", true);

    return cfg;
}

ConfigManager::LoggingConfig ConfigManager::getLoggingConfig() const {
    LoggingConfig cfg;
    if (config_.empty()) return cfg;
    const json& logging = config_.value("logging", json::object());

    cfg.level = logging.value("level", "info");
    cfg.file = logging.value("file", "./clawagent.log");

    return cfg;
}
