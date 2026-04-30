#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "message/Message.hpp"
#include "llm/LLMClient.hpp"

using json = nlohmann::json;

namespace ClawAgent {

/**
 * @brief LLM客户端抽象接口
 * 所有LLM客户端实现必须继承此接口
 */
class ILlmClient {
public:
    virtual ~ILlmClient() = default;

    /**
     * @brief 发送聊天请求
     * @param messages 消息列表
     * @param tools 工具定义列表
     * @param response 响应输出
     * @return 是否成功
     */
    virtual bool chat(const std::vector<Message>& messages,
                     const std::vector<json>& tools,
                     LLMResponse& response) = 0;

    /**
     * @brief 获取提供商名称
     * @return provider名称，如"openai"、"anthropic"
     */
    virtual std::string getProvider() const = 0;

    /**
     * @brief 获取模型名称
     * @return 模型名称
     */
    virtual std::string getModelName() const = 0;

    /**
     * @brief 健康检查
     * @return 是否可用
     */
    virtual bool healthCheck() = 0;

    /**
     * @brief 中止当前请求
     * 可用于终止正在进行的 LLM 调用，从其他线程安全调用
     */
    virtual void abort() = 0;
};

} // namespace ClawAgent