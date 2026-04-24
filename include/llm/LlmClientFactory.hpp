#pragma once

#include <memory>
#include <string>
#include "ILlmClient.hpp"

namespace ClawAgent {

// 前向声明
class OpenAIClient;
class AnthropicClient;

/**
 * @brief LLM客户端工厂
 * 根据配置创建对应的LLM客户端实例
 */
class LlmClientFactory {
public:
    /**
     * @brief 创建LLM客户端
     * @param provider 提供商名称 ("openai" 或 "anthropic")
     * @param model 模型名称
     * @param api_key API密钥
     * @param base_url API基础URL
     * @param stream 是否启用流式
     * @param timeout_ms 超时时间(毫秒)
     * @return 对应的客户端智能指针
     */
    static std::unique_ptr<ILlmClient> create(
        const std::string& provider,
        const std::string& model,
        const std::string& api_key,
        const std::string& base_url,
        bool stream = true,
        int timeout_ms = 120000);
};

} // namespace ClawAgent
