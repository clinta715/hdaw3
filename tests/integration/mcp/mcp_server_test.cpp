#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <thread>

namespace {
QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}
}

TEST(McpServer, InitializeAndList) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_EQ(r.value("id").toInt(), 1);
    EXPECT_FALSE(r.value("result").toObject().value("capabilities").toObject().value("tools").isUndefined());

    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto listed = parseOne(out).value("result").toObject().value("tools").toArray();
    EXPECT_GE(listed.size(), 12); // 8 read + 2 transport + 2 undo (Phase 1 v1)
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, GetProjectSummary) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"get_project_summary","arguments":{}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_TRUE(r.value("result").toObject().value("content").toArray()
                .at(0).toObject().value("text").toString().contains("tracks="));
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, UndoAddThenUndoRemoves) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"undo","arguments":{"count":1}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out).value("result").toObject();
    EXPECT_FALSE(r.value("isError").toBool(true));
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, HandlerRunsOnMainThread) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();
    std::thread::id mainTid = std::this_thread::get_id();
    s.registerTool({"whereami","test", QJsonObject{{"type","object"}},
        [mainTid](const QJsonObject&) {
            return mcp::McpToolResult::text(
                (std::this_thread::get_id() == mainTid) ? "main" : "other");
        }});
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"whereami","arguments":{}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto txt = parseOne(out).value("result").toObject()
                  .value("content").toArray().at(0).toObject().value("text").toString();
    EXPECT_EQ(txt, QString("main"));
    s.stop();
    s.setTransport(nullptr);
}
