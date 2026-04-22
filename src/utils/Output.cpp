#include "utils/Output.hpp"
#include "utils/Logger.hpp"

#include <iostream>
#include <mutex>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

using namespace ClawAgent;

Output& Output::instance() {
    static Output instance;
    return instance;
}

void Output::init(bool color_output, bool show_tools) {
    std::lock_guard<std::mutex> lock(mutex_);
    color_output_ = color_output;
    show_tools_ = show_tools;
}

void Output::printPrompt(const std::string& prompt) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << prompt << " ";
    std::cout.flush();
}

void Output::printAssistant(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "\n[ClawAgent] " << message << std::endl;
}

void Output::printError(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (color_output_) {
        std::cerr << "\n[" << "\033[31m" << "错误" << "\033[0m" << "] " << error << std::endl;
    } else {
        std::cerr << "\n[错误] " << error << std::endl;
    }
}

void Output::printSystem(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << message << std::endl;
}

void Output::printCallingModel() {
    if (!show_tools_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "\n[调用模型...]" << std::endl;
}

void Output::printToolCalls(size_t count) {
    if (!show_tools_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "\n[检测到 " << count << " 个工具调用]" << std::endl;
}

void Output::printExecutingTool(const std::string& name) {
    if (!show_tools_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "\n[执行工具: " << name << "]" << std::endl;
}

void Output::printToolParams(const std::string& params) {
    if (!show_tools_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "  参数: " << params << std::endl;
}

void Output::printToolResult(const std::string& result, bool truncated) {
    if (!show_tools_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "  结果: " << result;
    if (truncated) {
        std::cout << "...";
    }
    std::cout << std::endl;
}

void Output::printToolError(const std::string& error) {
    if (!show_tools_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (color_output_) {
        std::cout << "  " << "\033[31m" << "错误" << "\033[0m" << ": " << error << std::endl;
    } else {
        std::cout << "  错误: " << error << std::endl;
    }
}

void Output::printToolTime(long ms) {
    if (!show_tools_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "  耗时: " << ms << "ms" << std::endl;
}

void Output::printProcessing() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "\n[处理中..." << std::endl;
}

void Output::printProcessingEnd() {
    // 不输出任何内容，由后续操作完成输出
    (void)this;  // suppress unused warning
}

void Output::setColor(Color color) {
    if (!color_output_) return;

    const char* color_code;
    switch (color) {
        case Color::Red:     color_code = "\033[31m"; break;
        case Color::Green:   color_code = "\033[32m"; break;
        case Color::Yellow:  color_code = "\033[33m"; break;
        case Color::Blue:    color_code = "\033[34m"; break;
        case Color::Cyan:    color_code = "\033[36m"; break;
        case Color::White:   color_code = "\033[37m"; break;
        default:             color_code = "\033[0m";  break;
    }
    std::cout << color_code;
}

void Output::resetColor() {
    if (color_output_) {
        std::cout << "\033[0m";
    }
}
