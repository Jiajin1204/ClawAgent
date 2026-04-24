#include "llm/LlmClientFactory.hpp"
#include "llm/OpenAIClient.hpp"
#include "llm/AnthropicClient.hpp"

namespace ClawAgent {

std::unique_ptr<ILlmClient> LlmClientFactory::create(
    const std::string& provider,
    const std::string& model,
    const std::string& api_key,
    const std::string& base_url,
    bool stream,
    int timeout_ms) {

    if (provider == "openai") {
        return std::make_unique<OpenAIClient>(model, api_key, base_url, stream, timeout_ms);
    } else if (provider == "anthropic") {
        return std::make_unique<AnthropicClient>(model, api_key, base_url, stream, timeout_ms);
    }

    throw std::runtime_error("Unknown LLM provider: " + provider);
}

} // namespace ClawAgent
