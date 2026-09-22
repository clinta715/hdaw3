// MCP <-> RPC parity for the song-plan / layer-handoff / capture-receipt gap —
// slice 3 of docs/plans/2026-09-21-rpc-parity-retrofit.md.
//
// Four MCP tools had no RPC route: the BATCH set_cells (the one-round-trip variant
// the performance rules tell agents to prefer over N separate set_cell calls),
// audit_song_structure, get_layer_handoffs (the ledger was write-only over RPC —
// setLayerHandoff/clearLayerHandoff existed but nothing could read it back), and
// get_fx_capture_status (the polling companion every capture/preset flow points at).
//
// Each now shares its shaping with the RPC method through src/common
// (SongPlanView / FxCaptureStatus), so the strongest assertion available is the
// direct one: call both surfaces and require identical payloads.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
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
#include <QString>

#include <memory>
#include <string>

namespace {

class SongPlanRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        auto& cmds = engine->getProjectCommands();
        cmds.addTrack("Kick");
        cmds.addTrack("Bass");
        engine->drainPendingRoutingRebuild();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
    }

    void TearDown() override {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
    }

    // A deterministic 3-section plan (intro/build/drop) — the cell recipes below
    // validate their section against it.
    void setPlan() {
        ProjectCommands::SongPlanData plan;
        plan.bpm = 138.0;
        plan.keyRoot = 5;
        plan.scaleMode = 7;
        plan.style = "full-on";
        plan.seed = 777;
        plan.totalBars = 32;
        plan.sections = { { "intro", "intro", 8 },
                          { "build", "build", 8 },
                          { "drop", "mainA", 16 } };
        const auto r = engine->getProjectCommands().setSongPlan(plan);
        ASSERT_TRUE(r.ok) << r.error;
    }

    // --- MCP surface -------------------------------------------------------
    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject { { "name", tool }, { "arguments", args } })
            .toObject();
    }
    QJsonValue mcpValue(const QString& tool, const QJsonObject& args) {
        const auto result = mcpResult(tool, args);
        EXPECT_FALSE(result.value("isError").toBool(true))
            << "MCP " << tool.toStdString() << " errored: "
            << result.value("content").toArray().at(0).toObject()
                   .value("text").toString().toStdString();
        const auto content = result.value("content").toArray();
        if (content.isEmpty()) return {};
        // MCP tool payloads are compact JSON text (audit/list/cells — object OR array)
        // or prose (the capture-status line), so dispatch on the parsed root.
        const QString text = content[0].toObject().value("text").toString();
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isArray()) return QJsonValue(doc.array());
        if (doc.isObject()) return QJsonValue(doc.object());
        return QJsonValue(text);
    }
    QString mcpText(const QString& tool, const QJsonObject& args) {
        const auto result = mcpResult(tool, args);
        const auto content = result.value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }

    // --- RPC surface -------------------------------------------------------
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }
    QJsonValue rpcPayload(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_FALSE(r.isError)
            << "RPC " << method.toStdString() << " errored: "
            << r.payload.toObject().value("message").toString().toStdString();
        return r.payload;
    }
    QJsonObject rpcError(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_TRUE(r.isError) << "expected RPC " << method.toStdString() << " to fail";
        return r.payload.toObject();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
};

// G1: the BATCH cell write is now reachable over RPC with an identical payload.
TEST_F(SongPlanRpcTest, BatchSetCellRecipesMatchesMcp) {
    setPlan();
    const QJsonObject args {
        { "cells", QJsonArray {
            QJsonObject { { "section", "drop" }, { "role", "bass" }, { "trackId", 1 },
                          { "source", "rhythm" }, { "seed", 11 } },
            QJsonObject { { "section", "intro" }, { "role", "lead" }, { "trackId", 1 },
                          { "source", "phrase" }, { "seed", 12 } } } } };
    const QJsonValue viaMcp = mcpValue("set_cells", args);
    const QJsonValue viaRpc = rpcPayload("composition.setCellRecipes", args);
    EXPECT_FALSE(viaMcp.toObject().isEmpty());
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaRpc.toObject().value("count").toInt(), 2);
    EXPECT_EQ(viaRpc.toObject().value("failed").toInt(), 0);
    EXPECT_TRUE(viaRpc.toObject().value("ok").toBool());

    // Both wrote the same cells (the RPC path is not a separate code path).
    const auto cells = engine->getProjectCommands().getCells();
    EXPECT_EQ(cells.size(), 2u);
}

// G1b: a batch with an invalid section reports it per-recipe without aborting, and
// the surfaces agree on the report.
TEST_F(SongPlanRpcTest, BatchSetCellRecipesPartialFailureMatchesMcp) {
    setPlan();
    const QJsonObject args {
        { "cells", QJsonArray {
            QJsonObject { { "section", "drop" }, { "role", "bass" }, { "trackId", 1 },
                          { "source", "rhythm" } },
            QJsonObject { { "section", "nosuchsection" }, { "role", "lead" },
                          { "trackId", 1 }, { "source", "phrase" } } } } };
    const QJsonValue viaMcp = mcpValue("set_cells", args);
    const QJsonValue viaRpc = rpcPayload("composition.setCellRecipes", args);
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaRpc.toObject().value("failed").toInt(), 1);
    EXPECT_FALSE(viaRpc.toObject().value("ok").toBool());
}

// G2: the arrangement-variety audit payload is identical across surfaces, both with
// and without a plan.
TEST_F(SongPlanRpcTest, AuditSongStructureMatchesMcp) {
    const QJsonValue noPlan = rpcPayload("composition.auditSongStructure", {});
    EXPECT_EQ(noPlan, mcpValue("audit_song_structure", {}));
    EXPECT_FALSE(noPlan.toObject().value("hasPlan").toBool());

    setPlan();
    const QJsonValue viaRpc = rpcPayload("composition.auditSongStructure", {});
    EXPECT_EQ(viaRpc, mcpValue("audit_song_structure", {}));
    EXPECT_TRUE(viaRpc.toObject().value("hasPlan").toBool());
    ASSERT_EQ(viaRpc.toObject().value("sections").toArray().size(), 3);
    EXPECT_TRUE(viaRpc.toObject().contains("gates"));
}

// G3: the layer-handoff ledger is READABLE over RPC (it was write-only before this
// slice), with the same payload as the MCP tool — including after a real write.
TEST_F(SongPlanRpcTest, LayerHandoffsMatchesMcp) {
    // Empty ledger: every track present with hasHandoff=false.
    const QJsonValue empty = rpcPayload("composition.getLayerHandoffs", {});
    EXPECT_EQ(empty, mcpValue("get_layer_handoffs", {}));
    ASSERT_EQ(empty.toArray().size(), 2);
    EXPECT_FALSE(empty.toArray()[0].toObject().value("hasHandoff").toBool());

    // Write through the existing project.setLayerHandoff route, then read back.
    const auto wrote = rpc("project.setLayerHandoff",
                           QJsonObject { { "trackIndex", 0 }, { "role", "bass" },
                                         { "soundIntent", "acid psy_fm" },
                                         { "modulation", QJsonObject {
                                             { "target", "F1Cutoff" }, { "depth", 0.4 } } },
                                         { "verify", QJsonObject {
                                             { "beforeRms", 0.10 }, { "afterRms", 0.14 } } } });
    ASSERT_FALSE(wrote.isError);

    const QJsonValue all = rpcPayload("composition.getLayerHandoffs", {});
    EXPECT_EQ(all, mcpValue("get_layer_handoffs", {}));
    const auto first = all.toArray()[0].toObject();
    EXPECT_TRUE(first.value("hasHandoff").toBool());
    EXPECT_EQ(first.value("role").toString(), QString("bass"));
    // Nested evidence survives the round trip as JSON (not as an escaped string).
    EXPECT_EQ(first.value("modulation").toObject().value("target").toString(),
              QString("F1Cutoff"));
    EXPECT_TRUE(first.value("verify").isObject());

    // Single-track read agrees with the all-tracks entry.
    const QJsonValue one = rpcPayload("composition.getLayerHandoffs",
                                      QJsonObject { { "trackIndex", 0 } });
    EXPECT_EQ(one, mcpValue("get_layer_handoffs", QJsonObject { { "trackId", 0 } }));
    EXPECT_EQ(one.toArray().first().toObject(), first);
}

// G4: the capture receipt is reachable over RPC. The MCP tool renders the same four
// values as prose, so parity here is data parity, not byte parity.
TEST_F(SongPlanRpcTest, FxCaptureStatusMatchesMcp) {
    auto& cmds = engine->getProjectCommands();
    ASSERT_GE(cmds.addTrack("Fx"), 0);
    cmds.addFxSlot(2, "eq", 0, "");
    engine->drainPendingRoutingRebuild();

    const QJsonValue viaRpc = rpcPayload("audio.getFxCaptureStatus",
                                         QJsonObject { { "trackIndex", 2 }, { "slotIndex", 0 } });
    const auto o = viaRpc.toObject();
    EXPECT_FALSE(o.isEmpty());
    for (const char* key : { "status", "stateBytes", "capturedAtMs", "hasPluginState" })
        EXPECT_TRUE(o.contains(key)) << key;

    // No capture has run for this slot: the receipt is the "none" default, and the
    // MCP prose reports the same status + counters.
    EXPECT_EQ(o.value("status").toString(), QString("none"));
    EXPECT_EQ(o.value("stateBytes").toInt(), 0);
    EXPECT_FALSE(o.value("hasPluginState").toBool());
    const QString prose = mcpText("get_fx_capture_status",
                                  QJsonObject { { "trackId", 2 }, { "slotIndex", 0 } });
    EXPECT_TRUE(prose.contains("status=none")) << prose.toStdString();
    EXPECT_TRUE(prose.contains("hasPluginState=0")) << prose.toStdString();
}

// G5: argument failures are clean -32602 on the RPC side, and the two surfaces agree
// on the message text where both report it.
TEST_F(SongPlanRpcTest, ErrorClasses) {
    EXPECT_EQ(rpcError("composition.setCellRecipes", {}).value("code").toInt(), -32602);
    EXPECT_EQ(rpcError("composition.setCellRecipes", {})
                  .value("message").toString(),
              QString("cells array required"));

    setPlan();
    EXPECT_EQ(rpcError("composition.getLayerHandoffs",
                       QJsonObject { { "trackIndex", 99 } }).value("code").toInt(),
              -32602);
    EXPECT_EQ(rpcError("audio.getFxCaptureStatus", {}).value("code").toInt(), -32602);
    EXPECT_EQ(rpcError("audio.getFxCaptureStatus",
                       QJsonObject { { "trackIndex", 99 }, { "slotIndex", 0 } })
                  .value("message").toString(),
              QString("slot not found in tree"));

    // Both surfaces reject an out-of-range track, but they name the argument
    // differently by convention (MCP `trackId` vs RPC `trackIndex`), so the exact
    // wording differs — assert the shared verdict, not string equality.
    const QString mcpErr = mcpText("get_layer_handoffs",
                                   QJsonObject { { "trackId", 99 } });
    EXPECT_TRUE(mcpErr.contains("out of range")) << mcpErr.toStdString();
    const QString rpcMsg = rpcError("composition.getLayerHandoffs",
                                    QJsonObject { { "trackIndex", 99 } })
                                .value("message").toString();
    EXPECT_TRUE(rpcMsg.contains("out of range")) << rpcMsg.toStdString();
}

// G6 (P1 surface fix from the 2026-09-21 dogfood run): a fill that wrote nothing MUST say
// so. "ok:true, filled:0" used to be returned both for a legitimate no-op re-fill AND for
// the state a FAILED set_cells leaves behind (no cells at all) — which silently produced an
// empty song in the dogfood run (docs/handoffs/2026-09-21-mcp-dogfood-composition.md).
TEST_F(SongPlanRpcTest, FillCellsFlagsNoCellsWhenNoneDefined) {
    // With no plan at all a stricter guard already refuses — pinned here so the noCells
    // guard below is understood to be the plan-present case.
    EXPECT_TRUE(rpcError("composition.fillCells", QJsonObject { { "mode", "all" } })
                    .value("message").toString().contains("no song plan"));

    // Plan present, NO cells defined — exactly the state a FAILED set_cells leaves behind,
    // which is what silently produced an empty song in the dogfood run.
    setPlan();
    const QJsonValue viaRpcV = rpcPayload("composition.fillCells", QJsonObject { { "mode", "all" } });
    const QJsonObject viaRpc = viaRpcV.toObject();
    EXPECT_TRUE(viaRpc.value("noCells").toBool())
        << QJsonDocument(viaRpc).toJson(QJsonDocument::Compact).constData();
    EXPECT_FALSE(viaRpc.value("warning").toString().isEmpty());
    EXPECT_EQ(viaRpc.value("filled").toInt(), 0);
    // The MCP twin reports the identical payload (shared builder).
    EXPECT_EQ(viaRpcV, mcpValue("fill_cells", QJsonObject { { "mode", "all" } }));
}

TEST_F(SongPlanRpcTest, FillCellsFlagsNothingToDoOnRefill) {
    setPlan();
    const QJsonObject cells { { "cells", QJsonArray {
        QJsonObject { { "section", "drop" }, { "role", "bass" }, { "trackId", 1 },
                      { "source", "rhythm" }, { "seed", 11 } } } } };
    ASSERT_EQ(rpcPayload("composition.setCellRecipes", cells).toObject().value("count").toInt(), 1);

    const QJsonObject first = rpcPayload("composition.fillCells", QJsonObject { { "mode", "all" } }).toObject();
    EXPECT_EQ(first.value("filled").toInt(), 1);
    EXPECT_FALSE(first.contains("noCells"));
    EXPECT_FALSE(first.contains("nothingToDo"));

    const QJsonValue againV = rpcPayload("composition.fillCells", QJsonObject { { "mode", "unfilled" } });
    const QJsonObject again = againV.toObject();
    EXPECT_TRUE(again.value("nothingToDo").toBool())
        << QJsonDocument(again).toJson(QJsonDocument::Compact).constData();
    EXPECT_TRUE(again.contains("warning"));
    EXPECT_EQ(againV, mcpValue("fill_cells", QJsonObject { { "mode", "unfilled" } }));
}

} // namespace
