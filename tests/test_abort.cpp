#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include "ClawAgent.hpp"

using namespace ClawAgent;

std::unique_ptr<ClawAgentCore> g_agent;
volatile bool g_cancelled = false;

void signalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[TEST] 收到 Ctrl+C，调用 cancel()..." << std::endl;
        if (g_agent) {
            g_agent->cancel();
        }
        g_cancelled = true;
    }
}

int main() {
    std::signal(SIGINT, signalHandler);

    try {
        // 初始化
        g_agent = std::make_unique<ClawAgentCore>("config.json");

        std::cout << "\n========== 测试 1: 正常请求 ==========" << std::endl;
        std::string response;
        bool success = g_agent->process("1+1等于几？", response);
        std::cout << "结果: " << (success ? "成功" : "失败") << std::endl;
        std::cout << "响应: " << response << std::endl;

        std::cout << "\n========== 测试 2: 发送取消请求 ==========" << std::endl;
        // 启动一个请求，在另一个线程中等待后取消
        std::thread cancel_thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "[TEST] 2秒后发送取消信号..." << std::endl;
            if (g_agent) {
                g_agent->cancel();
            }
        });

        // 这个请求会触发 LLM 调用，如果取消信号在调用期间到达，会被中止
        response.clear();
        success = g_agent->process("写一个很长的自我介绍，至少500字，包含详细的兴趣爱好描述", response);
        std::cout << "结果: " << (success ? "成功" : "失败") << std::endl;
        if (success) {
            std::cout << "响应: " << response.substr(0, 200) << "..." << std::endl;
        } else {
            std::cout << "响应: " << response << std::endl;
        }

        cancel_thread.join();

        std::cout << "\n========== 测试 3: 取消后发送新任务 ==========" << std::endl;
        // 验证取消后系统仍然可用
        response.clear();
        success = g_agent->process("2+2等于几？", response);
        std::cout << "结果: " << (success ? "成功" : "失败") << std::endl;
        std::cout << "响应: " << response << std::endl;

        if (!success) {
            std::cout << "\n[FAIL] 取消后新任务失败!" << std::endl;
            return 1;
        }

        std::cout << "\n========== 测试 4: 统计信息 ==========" << std::endl;
        auto stats = g_agent->getStats();
        std::cout << "迭代次数: " << stats.iterations << std::endl;
        std::cout << "工具调用: " << stats.total_tool_calls << std::endl;
        std::cout << "总耗时: " << stats.total_time_ms << "ms" << std::endl;

        std::cout << "\n[PASS] 所有测试通过!" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
}
