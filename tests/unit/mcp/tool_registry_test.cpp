#include <gtest/gtest.h>
#include "mcp/McpServer.h"
using namespace mcp;

TEST(ToolRegistry, RegisterAndList) {
    McpServer s;
    s.registerTool({"foo","does foo",QJsonObject{{"type","object"}},"test",
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    s.registerTool({"bar","does bar",QJsonObject{{"type","object"}},"test",
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    EXPECT_EQ(s.tools().size(), 2u);
    EXPECT_TRUE(s.tools().contains("foo"));
}
TEST(ToolRegistry, UnknownToolReturnsToolError) {
    McpServer s;
    s.registerTool({"foo","x",QJsonObject{{"type","object"}},"test",
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","baz"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
}
TEST(ToolRegistry, InvalidParamsReturnsToolError) {
    McpServer s;
    QJsonObject schema{{"type","object"},{"required", QJsonArray{"x"}},
                       {"properties", QJsonObject{{"x", QJsonObject{{"type","integer"}}}}}};
    s.registerTool({"t","x",schema,"test",[](const QJsonObject&){ return McpToolResult::text("ok"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","t"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
    EXPECT_TRUE(r.toObject().value("content").toArray().at(0).toObject()
                .value("text").toString().contains("invalid params"));
}
TEST(ToolRegistry, HandlerExceptionBecomesToolError) {
    McpServer s;
    s.registerTool({"boom","x",QJsonObject{{"type","object"}},"test",
                   [](const QJsonObject&) -> McpToolResult { throw std::runtime_error("nope"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","boom"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
}

#include "mcp/McpTools_Private.h"

TEST(ToolRegistry, FxPresetFileToolMentionsSerumPreset) {
    McpServer s;
    registerFxPresetTools(s, nullptr);
    ASSERT_TRUE(s.tools().contains("load_plugin_preset_file"));
    const auto def = s.tools().value("load_plugin_preset_file");
    EXPECT_TRUE(def.description.contains(".SerumPreset"));
    EXPECT_TRUE(def.inputSchema.value("properties").toObject().contains("filePath"));
}
TEST(ToolRegistry, TogglePluginEditorToolMentionsEditor) {
    McpServer s;
    registerFxSlotTools(s, nullptr);
    ASSERT_TRUE(s.tools().contains("toggle_plugin_editor"));
    const auto def = s.tools().value("toggle_plugin_editor");
    EXPECT_TRUE(def.description.contains("editor"));
}
