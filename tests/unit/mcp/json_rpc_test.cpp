#include <gtest/gtest.h>
#include "mcp/McpJsonRpc.h"
using namespace mcp;

TEST(JsonRpc, SerializeSuccess) {
    auto s = serializeResponse(McpResponse::success(42, QJsonObject{{"ok", true}}));
    EXPECT_TRUE(s.contains("\"jsonrpc\":\"2.0\"")) << s.toStdString();
    EXPECT_TRUE(s.contains("\"id\":42"));
    EXPECT_TRUE(s.contains("\"result\""));
    EXPECT_FALSE(s.contains("error"));
}
TEST(JsonRpc, SerializeError) {
    auto s = serializeResponse(McpResponse::failure(7, err::MethodNotFound, "nope"));
    EXPECT_TRUE(s.contains("\"code\":-32601"));
    EXPECT_TRUE(s.contains("nope"));
}
TEST(JsonRpc, ParseLineValid) {
    bool ok = false;
    auto v = parseLine(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"ping"})"), &ok);
    EXPECT_TRUE(ok);
    auto r = validateRequest(v);
    EXPECT_TRUE(std::holds_alternative<McpRequest>(r));
    EXPECT_EQ(std::get<McpRequest>(r).method, "ping");
    EXPECT_FALSE(std::get<McpRequest>(r).isNotification());
}
TEST(JsonRpc, ValidateNotificationHasNullId) {
    auto v = QJsonObject{{"jsonrpc","2.0"},{"method","notifications/cancelled"},
                         {"params", QJsonObject{{"requestId", 1}}}};
    auto r = validateRequest(v);
    ASSERT_TRUE(std::holds_alternative<McpRequest>(r));
    EXPECT_TRUE(std::get<McpRequest>(r).isNotification());
}
TEST(JsonRpc, InvalidJsonReturnsParseError) {
    bool ok = true;
    parseLine(QByteArray("not json"), &ok);
    EXPECT_FALSE(ok);
}
TEST(JsonRpc, MissingMethodIsInvalidRequest) {
    auto r = validateRequest(QJsonObject{{"jsonrpc","2.0"},{"id",1}});
    ASSERT_TRUE(std::holds_alternative<McpResponse>(r));
    EXPECT_EQ(std::get<McpResponse>(r).error.toObject().value("code").toInt(),
              err::InvalidRequest);
}
