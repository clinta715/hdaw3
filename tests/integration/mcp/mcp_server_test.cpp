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

TEST(McpServer, FxAddRemoveBypass) {
    AudioEngine engine;
    // initialize() builds the routing manager and instantiates Tracks with
    // back-pointers (setProjectContext) into the project model. The FX
    // mutation methods require those back-pointers to be live.
    engine.initialize();

    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    auto* tr0 = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(tr0, nullptr);
    EXPECT_EQ(tr0->getNumFXSlots(), 0);

    auto callTool = [&](int id, const char* name, const char* args) {
        tp.drainOutgoing();
        QString req = QString(R"({"jsonrpc":"2.0","id":%1,"method":"tools/call",)"
                              R"("params":{"name":"%2","arguments":%3}})")
                          .arg(id).arg(name).arg(args);
        tp.pumpIncoming(req.toUtf8());
        QByteArray out; EXPECT_TRUE(tp.waitForOutgoing(500, &out));
        return parseOne(out);
    };

    auto text = [](const QJsonObject& r) -> QString {
        return r.value("result").toObject()
                .value("content").toArray().at(0).toObject()
                .value("text").toString();
    };

    // add_fx: eq at position 0
    auto r = callTool(1, "add_fx", R"({"trackId":0,"fxType":"eq","position":0})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_EQ(text(r).toStdString(), std::string("slot=0"));
    ASSERT_EQ(tr0->getNumFXSlots(), 1);
    EXPECT_EQ(tr0->getFXChain().at(0)->getType().toStdString(), std::string("eq"));

    // add_fx: compressor (default position = append)
    r = callTool(2, "add_fx", R"({"trackId":0,"fxType":"compressor"})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_EQ(text(r).toStdString(), std::string("slot=1"));
    ASSERT_EQ(tr0->getNumFXSlots(), 2);
    EXPECT_EQ(tr0->getFXChain().at(1)->getType().toStdString(), std::string("compressor"));

    // add_fx: reverb
    r = callTool(3, "add_fx", R"({"trackId":0,"fxType":"reverb"})");
    EXPECT_FALSE(r.value("error").isObject());
    ASSERT_EQ(tr0->getNumFXSlots(), 3);
    EXPECT_EQ(tr0->getFXChain().at(2)->getType().toStdString(), std::string("reverb"));

    // add_fx: delay
    r = callTool(4, "add_fx", R"({"trackId":0,"fxType":"delay"})");
    EXPECT_FALSE(r.value("error").isObject());
    ASSERT_EQ(tr0->getNumFXSlots(), 4);
    EXPECT_EQ(tr0->getFXChain().at(3)->getType().toStdString(), std::string("delay"));

    // Model state should be in sync: FX_CHAIN subtree has 4 children
    {
        auto fxChainTree = engine.getProjectModel().getTrackListTree()
                              .getChild(0).getChildWithName(IDs::FX_CHAIN);
        EXPECT_EQ(fxChainTree.getNumChildren(), 4);
    }

    // set_fx_bypass: slot 1 (compressor) -> true
    r = callTool(5, "set_fx_bypass", R"({"trackId":0,"slotIndex":1,"bypassed":true})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(tr0->getFXChain().at(1)->isBypassed());
    EXPECT_FALSE(tr0->getFXChain().at(0)->isBypassed());
    EXPECT_FALSE(tr0->getFXChain().at(2)->isBypassed());
    EXPECT_FALSE(tr0->getFXChain().at(3)->isBypassed());
    // model side: bypassed property persisted
    {
        auto fxChainTree = engine.getProjectModel().getTrackListTree()
                              .getChild(0).getChildWithName(IDs::FX_CHAIN);
        EXPECT_TRUE(static_cast<bool>(fxChainTree.getChild(1).getProperty(IDs::bypassed)));
    }

    // set_fx_bypass: slot 1 -> false
    r = callTool(6, "set_fx_bypass", R"({"trackId":0,"slotIndex":1,"bypassed":false})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(tr0->getFXChain().at(1)->isBypassed());

    // remove_fx: dryRun=true (no mutation)
    r = callTool(7, "remove_fx", R"({"trackId":0,"slotIndex":2,"dryRun":true})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(text(r).contains("would remove"));
    EXPECT_EQ(tr0->getNumFXSlots(), 4);  // unchanged

    // remove_fx: actually remove slot 2 (reverb)
    r = callTool(8, "remove_fx", R"({"trackId":0,"slotIndex":2})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    ASSERT_EQ(tr0->getNumFXSlots(), 3);
    EXPECT_EQ(tr0->getFXChain().at(2)->getType().toStdString(), std::string("delay"));

    // Model state in sync: FX_CHAIN subtree has 3 children
    {
        auto fxChainTree = engine.getProjectModel().getTrackListTree()
                              .getChild(0).getChildWithName(IDs::FX_CHAIN);
        EXPECT_EQ(fxChainTree.getNumChildren(), 3);
    }

    // add_fx for a non-existent track returns "track not found" with isError=true
    r = callTool(9, "add_fx", R"({"trackId":99,"fxType":"eq"})");
    EXPECT_FALSE(r.value("error").isObject());  // no JSON-RPC error
    EXPECT_TRUE(r.value("result").toObject().value("isError").toBool(false));
    EXPECT_TRUE(text(r).contains("track not found"));

    s.stop();
    s.setTransport(nullptr);
}
