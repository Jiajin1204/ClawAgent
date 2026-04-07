#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <csignal>

#include "ClawAgent.hpp"
#include "utils/Logger.hpp"

using namespace ClawAgent;

namespace {
    std::unique_ptr<ClawAgentCore> g_agent;
    volatile std::sig_atomic_t g_signal_count = 0;
}

void signalHandler(int signal) {
    if (signal == SIGINT) {
        g_signal_count++;
        if (g_signal_count >= 2) {
            std::cout << "\n强制退出..." << std::endl;
            exit(1);
        }
        std::cout << "\n按 Ctrl+C 再次退出，或输入 /quit 退出" << std::endl;
    }
}

void printBanner() {
    std::cout << "\n";
    std::cout << "  ClawAgent - 智能体开发框架\n";
    std::cout << "  版本: 1.0.0\n";
    std::cout << "\n";
    std::cout << "  输入 /help 查看帮助\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    std::signal(SIGINT, signalHandler);

    // 解析命令行参数
    std::string config_path = "config.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "用法: " << argv[0] << " [-c config_path]\n";
            std::cout << "  -c: 指定配置文件路径 (默认: config.json)\n";
            return 0;
        }
    }

    try {
        // 初始化日志
        Logger::instance().init(LogLevel::Info, "", true);

        // 创建Agent
        g_agent = std::make_unique<ClawAgentCore>(config_path);

        printBanner();

        // 运行Agent
        g_agent->run();

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
