#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {

QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

QJsonObject callTool(mcp::TransportLoopback& tp, const QString& name,
                     const QJsonObject& args = {})
{
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = 1;
    req["method"] = "tools/call";
    QJsonObject params;
    params["name"] = name;
    params["arguments"] = args;
    req["params"] = params;

    tp.drainOutgoing();
    tp.pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
    QByteArray out;
    if (!tp.waitForOutgoing(500, &out)) return {};
    auto resp = parseOne(out);
    auto result = resp.value("result").toObject();
    auto content = result.value("content").toArray();
    if (content.isEmpty()) return {};
    auto text = content[0].toObject()["text"].toString();
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

QJsonArray callToolArray(mcp::TransportLoopback& tp, const QString& name,
                         const QJsonObject& args = {})
{
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = 1;
    req["method"] = "tools/call";
    QJsonObject params;
    params["name"] = name;
    params["arguments"] = args;
    req["params"] = params;

    tp.drainOutgoing();
    tp.pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
    QByteArray out;
    if (!tp.waitForOutgoing(500, &out)) return {};
    auto resp = parseOne(out);
    auto result = resp.value("result").toObject();
    auto content = result.value("content").toArray();
    if (content.isEmpty()) return {};
    auto text = content[0].toObject()["text"].toString();
    return QJsonDocument::fromJson(text.toUtf8()).array();
}

} // namespace

TEST(GuiInspect, ToolsRegistered) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);

    const auto& tools = s.tools();
    EXPECT_TRUE(tools.contains("gui.snapshot"));
    EXPECT_TRUE(tools.contains("gui.get_clip_geometry"));
    EXPECT_TRUE(tools.contains("gui.get_track_layout"));
    EXPECT_TRUE(tools.contains("gui.get_selection"));
    EXPECT_TRUE(tools.contains("gui.get_scroll"));
    EXPECT_TRUE(tools.contains("gui.get_panel_state"));
    EXPECT_TRUE(tools.contains("gui.get_piano_roll"));
    EXPECT_TRUE(tools.contains("gui.hit_test"));
}

TEST(GuiInspect, HeadlessSnapshotReturnsUnavailable) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto snap = callTool(tp, "gui.snapshot");
    EXPECT_FALSE(snap["available"].toBool());
    EXPECT_EQ(snap["reason"].toString().toStdString(), "no_gui");

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessClipGeometryReturnsEmpty) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto clips = callToolArray(tp, "gui.get_clip_geometry");
    EXPECT_TRUE(clips.isEmpty());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessTrackLayoutReturnsEmpty) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto tracks = callToolArray(tp, "gui.get_track_layout");
    EXPECT_TRUE(tracks.isEmpty());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessHitTestReturnsNoHit) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto hit = callTool(tp, "gui.hit_test", {{"x", 100.0}, {"y", 100.0}});
    EXPECT_FALSE(hit["hit"].toBool());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessPanelStateReturnsUnavailable) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto panel = callTool(tp, "gui.get_panel_state");
    EXPECT_TRUE(panel.empty());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessPianoRollReturnsUnavailable) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto pr = callTool(tp, "gui.get_piano_roll");
    EXPECT_FALSE(pr["loaded"].toBool());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessScrollStateReturnsEmpty) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto scroll = callTool(tp, "gui.get_scroll");
    EXPECT_TRUE(scroll.empty());

    s.stop();
    s.setTransport(nullptr);
}

TEST(GuiInspect, HeadlessSelectionReturnsEmpty) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    auto sel = callTool(tp, "gui.get_selection");
    EXPECT_TRUE(sel.empty());

    s.stop();
    s.setTransport(nullptr);
}
