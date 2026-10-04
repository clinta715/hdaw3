// MCP set_fx_sidechain <-> RPC project.setFxSidechain twin tests — the surface
// half of TRACK-FX-SLOT COMPRESSOR SIDECHAIN v1 (the engine command
// AudioEngineCommands::setFxSidechain and its Sidechain.* suites are the other
// half and are FROZEN: this file must not change engine semantics).
//
// AGENTS.md feature-parity rule: the capability must be reachable on BOTH
// surfaces and each route gets a twin test asserting the SAME failure (and the
// SAME payload) on both. Both surfaces call ONE shared reader/body
// (src/common/FxSidechain.h), so the success payload and every refusal are
// byte-identical BY CONSTRUCTION — these tests prove both wirings.
//
// Harness idioms follow tests/integration/mcp/plugin_param_persist_test.cpp
// (this TU's neighbours: a fixture engine + McpServer for the tool surface and
// frontend::dispatch for the route surface, no live transport needed).
//
// Adversarial: every refusal asserts -32602 + byte-identical text on the route
// and the tool, so this file is RED on the pre-slice tree (the route answered
// "unknown project method: setFxSidechain").
//
// Determinism (lesson 9): createDefaultProject() ships ZERO tracks, so every
// track/slot a case needs is created here.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <memory>
#include <optional>
#include <string>

namespace {

class SidechainParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    void TearDown() override {
        server.reset();
        engine.reset();
    }

    // --- MCP surface --------------------------------------------------------
    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject{ { "name", tool }, { "arguments", args } })
            .toObject();
    }
    QString mcpText(const QString& tool, const QJsonObject& args) {
        const auto content = mcpResult(tool, args).value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }
    bool mcpIsError(const QString& tool, const QJsonObject& args) {
        return mcpResult(tool, args).value("isError").toBool();
    }
    QJsonObject mcpObj(const QString& tool, const QJsonObject& args) {
        return QJsonDocument::fromJson(mcpText(tool, args).toUtf8()).object();
    }

    // --- RPC surface --------------------------------------------------------
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }
    QJsonObject rpcObj(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_FALSE(r.isError)
            << "RPC " << method.toStdString() << " errored: "
            << r.payload.toObject().value("message").toString().toStdString();
        return r.payload.toObject();
    }

    // A failing pair: both surfaces must report the same text and the route
    // -32602 (the tool reports it as an isError result). Copied from
    // add_fx_parity_test.cpp so this TU stays self-contained.
    void expectSameFailure(const QJsonObject& args) {
        const auto r = rpc("project.setFxSidechain", args);
        ASSERT_TRUE(r.isError) << "expected project.setFxSidechain to fail";
        EXPECT_TRUE(mcpIsError("set_fx_sidechain", args))
            << "expected set_fx_sidechain to fail";
        const QString rpcMessage = r.payload.toObject().value("message").toString();
        const QString mcpMessage = mcpText("set_fx_sidechain", args);
        EXPECT_FALSE(mcpMessage.isEmpty()) << "a failure must carry a reason";
        EXPECT_EQ(rpcMessage, mcpMessage);
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
    }

    // --- scaffolding --------------------------------------------------------
    int addTrack(const char* name) {
        const int idx = engine->getProjectCommands().addTrack(name);
        engine->drainPendingRoutingRebuild();
        return idx;
    }
    int addSlot(int trackIndex, const char* type) {
        engine->getProjectCommands().addFxSlot(trackIndex, std::string(type), -1, "");
        engine->drainPendingRoutingRebuild();
        return 0;   // first (only) slot
    }
    int stableID(int trackIndex) {
        return engine->getProjectCommands().getTrackID(trackIndex);
    }
    juce::ValueTree slotNode(int trackIndex, int slotIndex) {
        return engine->getProjectModel().getTrackListTree()
            .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// ─── (a) success: identical payload + identical read-back on both surfaces ──

TEST_F(SidechainParityTest, SuccessPayloadAndReadbackMatchOnBothSurfaces)
{
    const int dest = addTrack("Dest");     // 0
    const int src  = addTrack("Source");   // 1
    addSlot(dest, "compressor");
    const int srcStable = stableID(src);
    const int destStable = stableID(dest);
    ASSERT_GT(srcStable, 0);
    ASSERT_GT(destStable, 0);

    // Positional spelling, first surface (MCP): set the edge + level/enabled.
    const QJsonObject positional{ { "trackId", dest },       { "slotIndex", 0 },
                                  { "sourceTrackId", src },  { "level", 0.6 },
                                  { "enabled", true } };
    const QJsonObject viaMcp = mcpObj("set_fx_sidechain", positional);
    EXPECT_FALSE(mcpIsError("set_fx_sidechain", positional))
        << mcpText("set_fx_sidechain", positional).toStdString();
    EXPECT_TRUE(viaMcp.value("ok").toBool());
    EXPECT_EQ(viaMcp.value("trackId").toInt(), dest);
    EXPECT_EQ(viaMcp.value("slotIndex").toInt(), 0);
    EXPECT_EQ(viaMcp.value("sourceTrackId").toInt(), src);

    // Read back the slot's three sidechain fields from the ReadModel: the tree
    // stores the SOURCE track's STABLE id.
    {
        const auto snap = engine->getReadModel().getFxSlots(dest);
        ASSERT_FALSE(snap.empty());
        EXPECT_EQ(snap[0].sidechainSource, srcStable);
        EXPECT_FLOAT_EQ(snap[0].sidechainLevel, 0.6f);
        EXPECT_TRUE(snap[0].sidechainEnabled);
    }

    // The route with the SAME args returns the SAME payload (byte-equal by
    // construction through the shared reader/body).
    EXPECT_EQ(rpcObj("project.setFxSidechain", positional), viaMcp);

    // Now the STABLE spelling on both surfaces: trackID wins over trackId, so
    // the id's own track (dest) takes the write even with a positional 0 that
    // names a different track.
    const QJsonObject stable{ { "trackId", 0 },            { "trackID", destStable },
                              { "slotIndex", 0 },          { "sourceTrackID", srcStable },
                              { "level", 0.25 },           { "enabled", false } };
    const QJsonObject viaMcpId = mcpObj("set_fx_sidechain", stable);
    EXPECT_FALSE(mcpIsError("set_fx_sidechain", stable))
        << mcpText("set_fx_sidechain", stable).toStdString();
    EXPECT_EQ(viaMcpId.value("trackId").toInt(), dest)
        << "the stable id must win over a disagreeing positional index";
    EXPECT_EQ(viaMcpId.value("sourceTrackId").toInt(), src);
    EXPECT_EQ(rpcObj("project.setFxSidechain", stable), viaMcpId);

    {
        const auto snap = engine->getReadModel().getFxSlots(dest);
        ASSERT_FALSE(snap.empty());
        EXPECT_EQ(snap[0].sidechainSource, srcStable);
        EXPECT_FLOAT_EQ(snap[0].sidechainLevel, 0.25f);
        EXPECT_FALSE(snap[0].sidechainEnabled);
    }

    // Clear with BOTH source spellings absent: the tree properties are removed
    // and the payload reports sourceTrackId 0.
    const QJsonObject clear{ { "trackId", dest }, { "slotIndex", 0 } };
    const QJsonObject viaMcpClear = mcpObj("set_fx_sidechain", clear);
    EXPECT_FALSE(mcpIsError("set_fx_sidechain", clear));
    EXPECT_EQ(viaMcpClear.value("sourceTrackId").toInt(), 0);
    EXPECT_FALSE(slotNode(dest, 0).hasProperty(IDs::sidechainSource));
    EXPECT_EQ(rpcObj("project.setFxSidechain", clear), viaMcpClear);
    EXPECT_FALSE(slotNode(dest, 0).hasProperty(IDs::sidechainSource))
        << "the route's clear must have removed the property too";
}

// ─── (b) refusals: byte-identical text + -32602 on BOTH surfaces ────────────

TEST_F(SidechainParityTest, UnknownDestTrackRefusedIdentically)
{
    addTrack("Only");
    expectSameFailure({ { "trackId", 99 }, { "slotIndex", 0 } });
}

TEST_F(SidechainParityTest, SlotIndexOutOfRangeRefusedIdentically)
{
    addTrack("A");   // track 0, has a compressor slot
    addSlot(0, "compressor");
    addTrack("B");   // track 1, NO FX chain at all
    expectSameFailure({ { "trackId", 1 }, { "slotIndex", 0 }, { "sourceTrackId", 0 } });
}

TEST_F(SidechainParityTest, NonCompressorSlotRefusedIdentically)
{
    addTrack("A");                 // 0
    addTrack("B");                 // 1
    addSlot(1, "reverb");          // dest slot 1 is NOT a compressor
    expectSameFailure({ { "trackId", 1 }, { "slotIndex", 0 }, { "sourceTrackId", 0 } });
}

TEST_F(SidechainParityTest, SelfSidechainRefusedIdentically)
{
    addTrack("A");
    addSlot(0, "compressor");
    expectSameFailure({ { "trackId", 0 }, { "slotIndex", 0 }, { "sourceTrackId", 0 } });
}

TEST_F(SidechainParityTest, UnknownSourceTrackRefusedIdentically)
{
    addTrack("A");
    addSlot(0, "compressor");
    expectSameFailure({ { "trackId", 0 }, { "slotIndex", 0 }, { "sourceTrackID", 12345 } });
}

TEST_F(SidechainParityTest, LevelOutOfRangeRefusedIdentically)
{
    addTrack("Dest");    // 0
    addTrack("Source");  // 1
    addSlot(0, "compressor");
    expectSameFailure({ { "trackId", 0 }, { "slotIndex", 0 },
                        { "sourceTrackId", 1 }, { "level", 1.5 } });
}

TEST_F(SidechainParityTest, CycleRefusedIdentically)
{
    addTrack("A");   // 0
    addTrack("B");   // 1
    addSlot(0, "compressor");
    addSlot(1, "compressor");

    // Legal edge B <- A, established through the ENGINE command (not a surface
    // under test), so the surfaces below only see the cycle-closing attempt.
    std::string err;
    const std::string ok = engine->getAudioEngineCommands().setFxSidechain(
        1, std::nullopt, 0, 0, std::nullopt, std::nullopt, std::nullopt, &err);
    ASSERT_NE(ok.find("\"ok\": true"), std::string::npos) << ok;
    engine->drainPendingRoutingRebuild();

    // Reciprocal edge A <- B would close A->B->A: refused, tree untouched.
    expectSameFailure({ { "trackId", 0 }, { "slotIndex", 0 }, { "sourceTrackId", 1 } });
    EXPECT_FALSE(slotNode(0, 0).hasProperty(IDs::sidechainSource));
}

// ─── (c) the mirrored SCHEMA gate: unknown key / missing required / mistyped ─
// The MCP tool declares its seven keys with additionalProperties:false +
// required:["slotIndex"], so mcp::validateSchema refuses these BEFORE the
// handler; the router has no validator, so the shared reader
// (src/common/FxSidechain.h) reproduces the validator's text byte for byte.
// Each case pins the EXACT string (first offender + wording), not just equality.

TEST_F(SidechainParityTest, UnknownKeyRefusedIdentically)
{
    // Every declared key present and valid: the ONLY offender is the typo, which
    // the schema answers "<key>: unknown property" (lesson 38: never silently
    // dropped) — and it is refused even though the request would otherwise be a
    // legal clear.
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0 }, { "bogus", 1 } };
    expectSameFailure(args);
    EXPECT_EQ(mcpText("set_fx_sidechain", args),
              QString("invalid params: bogus: unknown property"));
}

TEST_F(SidechainParityTest, MissingSlotIndexRefusedIdentically)
{
    // required:["slotIndex"] — checked AFTER property types, like validateInner.
    const QJsonObject args{ { "trackId", 0 } };
    expectSameFailure(args);
    EXPECT_EQ(mcpText("set_fx_sidechain", args),
              QString("invalid params: slotIndex: missing required property 'slotIndex'"));
}

TEST_F(SidechainParityTest, FractionalSlotIndexRefusedIdentically)
{
    // `{"type":"integer"}` answers a numeric-but-non-integral value with the
    // validator's "expected integer" — never a silent truncation onto index 0.
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0.5 } };
    expectSameFailure(args);
    EXPECT_EQ(mcpText("set_fx_sidechain", args),
              QString("invalid params: slotIndex: expected integer"));
}

TEST_F(SidechainParityTest, WrongTypedLevelRefusedIdentically)
{
    // `{"type":"number"}` vs a string: "expected number" (the declared-type text,
    // not the file's older ad-hoc wording).
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0 }, { "level", "loud" } };
    expectSameFailure(args);
    EXPECT_EQ(mcpText("set_fx_sidechain", args),
              QString("invalid params: level: expected number"));
}

// ─── (d) readback: list_fx / read.getFxSlots expose a configured sidechain ──
// A configured compressor sidechain must be VISIBLE on both read surfaces (the
// gap this slice closes: the snapshot already carried the fields, the one shared
// shaper dropped them). FxSlotSnapshot's sidechainSource/Level/Enabled are
// emitted — for the SAME reason the plugin IDENTITY fields are conditional — only
// on a "compressor" row, so every other type keeps the shape it always had.
TEST_F(SidechainParityTest, ListFxExposesSidechainOnBothSurfaces)
{
    addTrack("Dest");      // 0 — carries the COMPRESSOR
    addTrack("Source");    // 1 — carries a NON-compressor slot (its row is the control)
    addSlot(0, "compressor");
    addSlot(1, "reverb");

    const int srcStable = stableID(1);
    ASSERT_GT(srcStable, 0);

    // Configure the edge through the MCP surface (its own args, positional source).
    const QJsonObject set{ { "trackId", 0 },     { "slotIndex", 0 },
                           { "sourceTrackId", 1 }, { "level", 0.5 },
                           { "enabled", true } };
    EXPECT_FALSE(mcpIsError("set_fx_sidechain", set))
        << mcpText("set_fx_sidechain", set).toStdString();

    // MCP list_fx: the compressor row carries the three fields with the set
    // values, the source reported as the STABLE id the tree stores.
    const QJsonArray mcpRows = QJsonDocument::fromJson(
        mcpText("list_fx", QJsonObject{ { "trackId", 0 } }).toUtf8()).array();
    ASSERT_EQ(mcpRows.size(), 1);
    const QJsonObject row = mcpRows[0].toObject();
    EXPECT_EQ(row.value("fxType").toString().toStdString(), "compressor");
    ASSERT_TRUE(row.contains("sidechainSource")) << "a compressor row must expose its sidechain";
    EXPECT_EQ(row.value("sidechainSource").toInt(), srcStable);
    EXPECT_DOUBLE_EQ(row.value("sidechainLevel").toDouble(), 0.5);
    EXPECT_TRUE(row.value("sidechainEnabled").toBool());

    // RPC read.getFxSlots is the SAME document (one shared shaper) — byte-equal.
    const auto r = rpc("read.getFxSlots", QJsonObject{ { "trackIndex", 0 } });
    ASSERT_FALSE(r.isError);
    EXPECT_EQ(r.payload.toArray(), mcpRows)
        << "list_fx and read.getFxSlots must shape ONE payload (common/SendJson.h)";

    // The NON-compressor track's row must NOT gain the sidechain fields — on
    // EITHER surface.
    const QJsonArray srcMcp = QJsonDocument::fromJson(
        mcpText("list_fx", QJsonObject{ { "trackId", 1 } }).toUtf8()).array();
    ASSERT_EQ(srcMcp.size(), 1);
    const QJsonObject srcRow = srcMcp[0].toObject();
    EXPECT_EQ(srcRow.value("fxType").toString().toStdString(), "reverb");
    for (const char* key : { "sidechainSource", "sidechainLevel", "sidechainEnabled" })
        EXPECT_FALSE(srcRow.contains(key))
            << "a non-compressor row must not carry " << key;
    const auto rSrc = rpc("read.getFxSlots", QJsonObject{ { "trackIndex", 1 } });
    ASSERT_FALSE(rSrc.isError);
    EXPECT_EQ(rSrc.payload.toArray(), srcMcp)
        << "the non-compressor row must be identical on both surfaces too";
}

} // namespace
