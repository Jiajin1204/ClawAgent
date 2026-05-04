#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "tools/ToolManager.hpp"

namespace fs = std::filesystem;
using namespace ClawAgent;

class ToolTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_unique<ToolManager>(true, true, true, 5000);
    }

    void TearDown() override {
        manager_.reset();
    }

    std::unique_ptr<ToolManager> manager_;
};

TEST_F(ToolTest, HasTools) {
    EXPECT_TRUE(manager_->hasTool("read"));
    EXPECT_TRUE(manager_->hasTool("write"));
    EXPECT_TRUE(manager_->hasTool("exec"));
}

TEST_F(ToolTest, GetToolDefinitions) {
    auto defs = manager_->getToolDefinitions();
    EXPECT_EQ(defs.size(), 3);
}

TEST_F(ToolTest, GetToolDescription) {
    std::string desc = manager_->getToolDescription("read");
    EXPECT_FALSE(desc.empty());
    // description 中包含 "Read" 或 "file"
    EXPECT_TRUE(desc.find("Read") != std::string::npos ||
                desc.find("file") != std::string::npos);
}

TEST_F(ToolTest, GetToolDescriptionUnknown) {
    std::string desc = manager_->getToolDescription("unknown_tool");
    EXPECT_TRUE(desc.empty());
}

TEST_F(ToolTest, ExecuteReadTool) {
    // 创建临时文件
    std::string test_file = "/tmp/test_read.txt";
    std::ofstream(test_file) << "Hello, World!";

    json args = {{"path", test_file}};
    auto result = manager_->executeTool("read", args, "call_1");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.result, "Hello, World!");
    EXPECT_EQ(result.tool_call_id, "call_1");

    fs::remove(test_file);
}

TEST_F(ToolTest, ExecuteReadToolFileNotFound) {
    json args = {{"filepath", "/nonexistent/file.txt"}};
    auto result = manager_->executeTool("read", args, "call_1");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("无法打开文件"), std::string::npos);
}

TEST_F(ToolTest, ExecuteWriteTool) {
    std::string test_file = "/tmp/test_write.txt";
    json args = {
        {"filepath", test_file},
        {"content", "Test content"}
    };
    auto result = manager_->executeTool("write", args, "call_2");

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(fs::exists(test_file));

    // 验证内容
    std::ifstream ifs(test_file);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "Test content");

    fs::remove(test_file);
}

TEST_F(ToolTest, ExecuteWriteToolMissingParam) {
    json args = {{"filepath", "/tmp/test.txt"}}; // 缺少 content
    auto result = manager_->executeTool("write", args, "call_2");

    EXPECT_FALSE(result.success);
}

TEST_F(ToolTest, ExecuteExecTool) {
    json args = {{"command", "echo 'Hello from exec'"}};
    auto result = manager_->executeTool("exec", args, "call_3");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.result, "Hello from exec\n");
}

TEST_F(ToolTest, ExecuteExecToolWithArgs) {
    json args = {{"command", "echo 'test_output'"}};
    auto result = manager_->executeTool("exec", args, "call_4");

    EXPECT_TRUE(result.success);
    EXPECT_NE(result.result.find("test_output"), std::string::npos);
}

TEST_F(ToolTest, ExecuteUnknownTool) {
    json args = {{"param", "value"}};
    auto result = manager_->executeTool("unknown_tool", args, "call_5");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("未知工具"), std::string::npos);
}

TEST_F(ToolTest, ExecuteToolWithEmptyArgs) {
    json args = json::object();
    auto result = manager_->executeTool("read", args, "call_6");

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_message.find("参数缺失"), std::string::npos);
}

TEST_F(ToolTest, SystemToolsPathExists) {
    EXPECT_TRUE(SystemTools::pathExists("/tmp"));
    EXPECT_TRUE(SystemTools::pathExists("/"));
    EXPECT_FALSE(SystemTools::pathExists("/nonexistent/path/123"));
}

TEST_F(ToolTest, SystemToolsGetOSInfo) {
    std::string os_info = SystemTools::getOSInfo();
    EXPECT_FALSE(os_info.empty());
}

TEST_F(ToolTest, SystemToolsGetHostname) {
    std::string hostname = SystemTools::getHostname();
    EXPECT_FALSE(hostname.empty());
    EXPECT_NE(hostname, "unknown");
}

TEST_F(ToolTest, RegisterCustomTool) {
    bool called = false;
    manager_->registerTool(
        "custom_tool",
        "A custom test tool",
        json{
            {"type", "object"},
            {"properties", json{
                {"input", json{
                    {"type", "string"},
                    {"description", "Input string"}
                }}
            }},
            {"required", {"input"}}
        },
        [&called](const json& args) -> json {
            called = true;
            return json{{"result", "custom result"}, {"success", true}};
        }
    );

    EXPECT_TRUE(manager_->hasTool("custom_tool"));

    json args = {{"input", "test"}};
    auto result = manager_->executeTool("custom_tool", args, "call_custom");
    EXPECT_TRUE(called);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.result, "custom result");
}

TEST_F(ToolTest, ExecuteMultipleTools) {
    std::vector<std::pair<std::string, json>> tools = {
        {"exec", json{{"command", "echo 'first'"}}},
        {"exec", json{{"command", "echo 'second'"}}}
    };
    std::vector<std::string> ids = {"call_1", "call_2"};

    auto results = manager_->executeTools(tools, ids);

    EXPECT_EQ(results.size(), 2);
    EXPECT_TRUE(results[0].success);
    EXPECT_TRUE(results[1].success);
}

TEST_F(ToolTest, ToolExecutionTiming) {
    json args = {{"command", "sleep 0.1 && echo 'done'"}};
    auto result = manager_->executeTool("exec", args, "call_timing");

    EXPECT_TRUE(result.success);
    EXPECT_GE(result.execution_time_ms, 100);
}
