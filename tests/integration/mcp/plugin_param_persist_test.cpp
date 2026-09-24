// Plugin-slot host-param persistence (docs/plans/2026-09-21-plugin-param-persistence.md).
//
// Root cause this protects: a plugin slot's host-param write reaches the LIVE
// isolated child only (PluginParamService::setParam -> proxy staging -> shm
// ring -> child). Every render the audit harness uses (audition_plugin /
// verify_part / export_audio) is an OFFLINE EXPORT of a tree copy into a FRESH
// child, so a live-only write is invisible to it and to save/load.
// docs/plans/2026-09-21-vavra-live-param-delivery.md
//
// The fix: AudioEngineCommands::setPluginParam writes live AND merges the
// slot's offline-replay ledger (IDs::appliedParamOverrides), which
// ExportManager::replayAppliedParamOverrides already replays into every fresh
// export child. MCP set_fx_param and RPC pluginParam.setParam both route
// through it (parity by construction).
//
// The fixture plugin id resolves to NO live instance, which is exactly the
// "deviceless / not yet settled" case: the ledger must still be written (the
// live write is a null-instance no-op) so a render sees it.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/ExportManager.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpJsonRpc.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "model/ProjectModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <memory>

namespace {

QJsonObject parseOne(const QByteArray& buf)
{
    const int nl = buf.indexOf('\n');
    const QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

class PluginParamPersistTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
        call("add_track", { { "name", "Track" } });
        addPluginSlot();
    }

    void TearDown() override {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
    }

    void addPluginSlot() {
        // Built via the COMMAND layer since the 2026-09-23 add_fx gate: the
        // MCP surface now rejects unresolvable pluginIds (fixture.test is in
        // no scan cache — that rejection itself is asserted by
        // unit/frontend/add_fx_parity_test), and this fixture wants the inert
        // slot, not the surface gate. This is the exact command the MCP
        // handler ran before (same args: type "plugin" derived from the
        // pluginId, position -1 append), so the slot is identical:
        // fxType "plugin", pluginID "fixture.test", pluginFormat "" —
        // matrix_presets_rpc_test.cpp:58 is the same precedent.
        engine->getProjectCommands().addFxSlot(0, "plugin", -1, "fixture.test");
        ASSERT_TRUE(slotTree().isValid()) << "the fixture slot must exist";
    }

    QJsonObject call(const char* method, const QJsonObject& args = {}) {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = nextId_++;
        req["method"] = "tools/call";
        req["params"] = QJsonObject{ { "name", method }, { "arguments", args } };
        loopback->drainOutgoing();
        loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!loopback->waitForOutgoing(500, &out)) return {};
        return parseOne(out).value("result").toObject();
    }

    QString callText(const char* method, const QJsonObject& args = {}) {
        const auto r = call(method, args);
        const auto content = r.value("content").toArray();
        if (content.isEmpty()) return {};
        return content[0].toObject().value("text").toString();
    }

    bool isError(const QJsonObject& r) { return r.value("isError").toBool(false); }

    // frontend::dispatch entry (the RPC surface) — no FrontendServer needed.
    frontend::DispatchResult rpc(const char* method, const QJsonObject& args = {}) {
        return frontend::dispatch(*engine, method, QJsonValue(args));
    }

    juce::ValueTree slotTree() {
        return engine->getProjectModel().getTrackListTree().getChild(0)
            .getChildWithName(IDs::FX_CHAIN).getChild(0);
    }

    juce::String ledger() {
        return slotTree().getProperty(IDs::appliedParamOverrides, "").toString();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    int nextId_ = 1;
};

// G3: the MCP write persists the ledger and reports its size.
TEST_F(PluginParamPersistTest, McpSetFxParamPersistsLedger)
{
    const QString text = callText("set_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 59 }, { "value", 0.25 } });
    EXPECT_EQ(text, QString("ok overrides=1")) << text.toStdString();

    ASSERT_TRUE(slotTree().hasProperty(IDs::appliedParamOverrides));
    const auto pairs = HDAW::ExportManager::parseAppliedParamOverrides(slotTree());
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0].first, 59);
    EXPECT_NEAR(pairs[0].second, 0.25f, 1e-6f);
}

// G4: MCP and RPC produce a BYTE-IDENTICAL ledger for the same write
// (parity by construction: both call the shared command).
TEST_F(PluginParamPersistTest, McpAndRpcProduceIdenticalLedger)
{
    ASSERT_EQ(callText("set_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 59 }, { "value", 0.25 } }),
        QString("ok overrides=1"));
    const juce::String viaMcp = ledger();
    ASSERT_FALSE(viaMcp.isEmpty());

    ASSERT_EQ(callText("clear_fx_param_overrides",
        { { "trackId", 0 }, { "slotIndex", 0 } }),
        QString("{\"removed\":1}"));
    EXPECT_FALSE(slotTree().hasProperty(IDs::appliedParamOverrides));

    const auto r = rpc("pluginParam.setParam",
        { { "trackIndex", 0 }, { "pluginID", "fixture.test" },
          { "paramIndex", 59 }, { "normalizedValue", 0.25 } });
    ASSERT_FALSE(r.isError);
    EXPECT_EQ(r.payload.toObject().value("overrides").toInt(), 1);

    EXPECT_EQ(ledger(), viaMcp); // identical grammar + value, from either surface
}

// Merge (not replace) semantics: one entry per index; a new index appends.
TEST_F(PluginParamPersistTest, MergeKeepsOneEntryPerIndexAndAppends)
{
    callText("set_fx_param", { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 59 }, { "value", 0.2 } });
    callText("set_fx_param", { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 59 }, { "value", 0.7 } });
    auto pairs = HDAW::ExportManager::parseAppliedParamOverrides(slotTree());
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0].first, 59);
    EXPECT_NEAR(pairs[0].second, 0.7f, 1e-6f);

    callText("set_fx_param", { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 60 }, { "value", 0.4 } });
    pairs = HDAW::ExportManager::parseAppliedParamOverrides(slotTree());
    ASSERT_EQ(pairs.size(), 2u);
    EXPECT_EQ(pairs[0].first, 59);
    EXPECT_EQ(pairs[1].first, 60);
    EXPECT_NEAR(pairs[1].second, 0.4f, 1e-6f);
}

// G5: the RPC read surface reports the persisted overrides, and clear reports
// what it removed + actually drops the property.
TEST_F(PluginParamPersistTest, RpcGetAndClearOverrides)
{
    rpc("pluginParam.setParam", { { "trackIndex", 0 }, { "pluginID", "fixture.test" },
                                  { "paramIndex", 59 }, { "normalizedValue", 0.3 } });
    rpc("pluginParam.setParam", { { "trackIndex", 0 }, { "pluginID", "fixture.test" },
                                  { "paramIndex", 60 }, { "normalizedValue", 0.6 } });

    const auto got = rpc("project.getPluginParamOverrides",
                         { { "trackIndex", 0 }, { "slotIndex", 0 } });
    ASSERT_FALSE(got.isError);
    const auto arr = got.payload.toObject().value("overrides").toArray();
    ASSERT_EQ(arr.size(), 2);
    EXPECT_EQ(arr[0].toObject().value("paramIndex").toInt(), 59);
    EXPECT_NEAR(arr[0].toObject().value("value").toDouble(), 0.3, 1e-6);
    EXPECT_EQ(arr[1].toObject().value("paramIndex").toInt(), 60);

    const auto cleared = rpc("project.clearPluginParamOverrides",
                             { { "trackIndex", 0 }, { "slotIndex", 0 } });
    ASSERT_FALSE(cleared.isError);
    EXPECT_EQ(cleared.payload.toObject().value("removed").toInt(), 2);
    EXPECT_FALSE(slotTree().hasProperty(IDs::appliedParamOverrides));
}

// The plugin-param channel must refuse non-plugin slots (internal FX keep
// their param_N ValueTree source of truth).
TEST_F(PluginParamPersistTest, NonPluginSlotIsRejected)
{
    auto& cmds = engine->getProjectCommands();
    cmds.addFxSlot(0, std::string("eq"), -1, std::string());
    EXPECT_EQ(cmds.setPluginParam(0, 1, 0, 0.5f), -1);
    EXPECT_EQ(cmds.getPluginParamOverrides(0, 1).size(), 0u);

    // The plugin slot at index 0 is fine.
    EXPECT_EQ(cmds.setPluginParam(0, 0, 59, 0.5f), 1);
    EXPECT_EQ(cmds.getPluginParamOverrides(0, 0).size(), 1u);
    EXPECT_EQ(cmds.clearPluginParamOverrides(0, 0), 1);
    // Out-of-range slot -> -1 from both.
    EXPECT_EQ(cmds.setPluginParam(0, 99, 0, 0.5f), -1);
    EXPECT_EQ(cmds.clearPluginParamOverrides(0, 99), -1);
}

// Values are clamped to the normalized 0..1 domain at the command boundary.
TEST_F(PluginParamPersistTest, ValuesAreClamped)
{
    auto& cmds = engine->getProjectCommands();
    EXPECT_EQ(cmds.setPluginParam(0, 0, 59, 4.0f), 1);
    const auto pairs = cmds.getPluginParamOverrides(0, 0);
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_NEAR(pairs[0].second, 1.0f, 1e-6f);
}

} // namespace
