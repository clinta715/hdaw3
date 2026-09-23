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

#include "common/KeyConflict.h"
#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands_Helpers.h"
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

TEST_F(SongPlanRpcTest, FillCellsTilesPhraseCellsWithTileBeats) {
    // 2026-09-22 dogfood: a phrase cell over a LONG section generated ONE
    // sparse pass. params.tileBeats opts into the same tiling rhythm cells use
    // (phrase generated at the tile length, repeated across the window).
    setPlan();
    const QJsonObject cells { { "cells", QJsonArray {
        QJsonObject { { "section", "drop" }, { "role", "bass" }, { "trackId", 1 },
                      { "source", "phrase" },
                      { "params", QJsonObject { { "style", "BassLine" },
                                                 { "density", 4 },
                                                 { "tileBeats", 16 } } },
                      { "seed", 21 } } } } };
    ASSERT_EQ(rpcPayload("composition.setCellRecipes", cells).toObject().value("count").toInt(), 1);

    const QJsonObject filled = rpcPayload("composition.fillCells", QJsonObject { { "mode", "all" } }).toObject();
    ASSERT_EQ(filled.value("failed").toInt(), 0);
    // drop = 16 bars = 64 beats; a 16-beat tile repeats 4x -> the bass cell is
    // no longer a single sparse pass (legacy behavior: ~density notes total).
    const int noteCount = filled.value("cells").toArray().at(0).toObject().value("noteCount").toInt();
    EXPECT_GE(noteCount, 8) << "tiled phrase cell must repeat across the window";

    // Notes actually reach late tiles (clip-local startBeat >= 16).
    const auto bassClips = engine->getProjectModel().getTrackListTree().getChild(1)
                               .getChildWithName(IDs::CLIP_LIST);
    ASSERT_TRUE(bassClips.isValid());
    const auto clip = bassClips.getChild(bassClips.getNumChildren() - 1);
    const auto notes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    ASSERT_TRUE(notes.isValid());
    double maxStart = 0.0;
    for (int i = 0; i < notes.getNumChildren(); ++i)
        maxStart = std::max(maxStart,
            static_cast<double>(notes.getChild(i).getProperty(IDs::startBeat, 0.0)));
    EXPECT_GE(maxStart, 16.0) << "notes must land in later tiles";
}

TEST_F(SongPlanRpcTest, FillCellsRefitsReusedClipAfterBriefWindowChange) {
    setPlan();
    const QJsonObject cells { { "cells", QJsonArray {
        QJsonObject { { "section", "drop" }, { "role", "bass" }, { "trackId", 1 },
                      { "source", "rhythm" }, { "seed", 11 } } } } };
    ASSERT_EQ(rpcPayload("composition.setCellRecipes", cells).toObject().value("count").toInt(), 1);

    const QJsonObject first = rpcPayload("composition.fillCells", QJsonObject { { "mode", "all" } }).toObject();
    const int clipId = first.value("cells").toArray().at(0).toObject().value("clipId").toInt();
    ASSERT_GT(clipId, 0);

    ProjectCommands::SongPlanData longer;
    longer.bpm = 138.0;
    longer.keyRoot = 5;
    longer.scaleMode = 7;
    longer.style = "full-on-refit";
    longer.seed = 778;
    longer.totalBars = 72;
    longer.sections = { { "intro", "intro", 16 },
                        { "build", "build", 24 },
                        { "drop", "mainA", 32 } };
    const auto planResult = engine->getProjectCommands().setSongPlan(longer);
    ASSERT_TRUE(planResult.ok) << planResult.error;

    const QJsonObject refilled = rpcPayload("composition.fillCells", QJsonObject { { "mode", "all" } }).toObject();
    ASSERT_EQ(refilled.value("failed").toInt(), 0)
        << QJsonDocument(refilled).toJson(QJsonDocument::Compact).constData();
    ASSERT_EQ(refilled.value("cells").toArray().at(0).toObject().value("clipId").toInt(), clipId)
        << "re-fill should reuse the generated clip, but retarget its window";

    const auto bassClips = engine->getProjectModel().getTrackListTree().getChild(1)
                               .getChildWithName(IDs::CLIP_LIST);
    ASSERT_TRUE(bassClips.isValid());
    juce::ValueTree clip;
    for (int i = 0; i < bassClips.getNumChildren(); ++i)
        if (static_cast<int>(bassClips.getChild(i).getProperty(IDs::clipID, 0)) == clipId)
            clip = bassClips.getChild(i);
    ASSERT_TRUE(clip.isValid());

    const double bpm = engine->getTransportManager().getBPM();
    EXPECT_NEAR(HDAW::secondsToBeats(static_cast<double>(clip.getProperty(IDs::startTime)), bpm),
                160.0, 1e-6);
    EXPECT_NEAR(HDAW::secondsToBeats(static_cast<double>(clip.getProperty(IDs::duration)), bpm),
                128.0, 1e-6);
}

// ============================================================================
// key_check — the tonality-conflict capability (docs/plans/2026-09-23-key-check.md)
//
// The shared theory (HDAW::checkKeyConflict) plus BOTH surfaces: the MCP
// `key_check` tool and its name-derived RPC twin `composition.keyCheck`.
// Expected values below are computed from PhraseGenerator's scale table:
//   A minor (root 9, aeolian) pcs = {9,11,0,2,4,5,7}
//   C major (root 0, ionian)  pcs = {0,2,4,5,7,9,11}  (identical -> relative)
// ============================================================================

// G1: the theory returns the EXPECTED relation, interval, and overlap — the
// verdict comes from both modes' pitch-class sets, not the root interval alone.
TEST(KeyConflictTheory, ExpectedRelations)
{
    // Same key -> unison.
    const auto u = HDAW::checkKeyConflict(9, 1, 9, 1);
    ASSERT_TRUE(u.ok) << u.error;
    EXPECT_EQ(u.relation, "unison");
    EXPECT_EQ(u.intervalSemitones, 0);
    EXPECT_NEAR(u.pitchClassOverlap, 1.0, 1e-6);
    EXPECT_NE(u.reason.find("A minor"), std::string::npos);

    // A minor <-> C major -> relative (compatible), in BOTH directions.
    const auto rel = HDAW::checkKeyConflict(9, 1, 0, 0);
    ASSERT_TRUE(rel.ok) << rel.error;
    EXPECT_EQ(rel.relation, "relative");
    EXPECT_EQ(rel.intervalSemitones, 3);
    EXPECT_NEAR(rel.pitchClassOverlap, 1.0, 1e-6);
    const auto relRev = HDAW::checkKeyConflict(0, 0, 9, 1);
    ASSERT_TRUE(relRev.ok) << relRev.error;
    EXPECT_EQ(relRev.relation, "relative");
    EXPECT_EQ(relRev.intervalSemitones, 9);
    EXPECT_NEAR(relRev.pitchClassOverlap, 1.0, 1e-6);

    // Parallel: same root, different mode (C major vs C minor).
    const auto par = HDAW::checkKeyConflict(0, 0, 0, 1);
    ASSERT_TRUE(par.ok) << par.error;
    EXPECT_EQ(par.relation, "parallel");
    EXPECT_EQ(par.intervalSemitones, 0);
    EXPECT_NEAR(par.pitchClassOverlap, 4.0 / 7.0, 1e-6); // {0,2,5,7} shared: C, D, F, G

    // Tritone roots -> conflicting (C major vs F# major, 2 of 12 shared).
    const auto tri = HDAW::checkKeyConflict(0, 0, 6, 0);
    ASSERT_TRUE(tri.ok) << tri.error;
    EXPECT_EQ(tri.relation, "conflicting");
    EXPECT_EQ(tri.intervalSemitones, 6);
    EXPECT_NEAR(tri.pitchClassOverlap, 2.0 / 7.0, 1e-6);

    // One semitone apart -> conflicting.
    const auto semi = HDAW::checkKeyConflict(0, 0, 1, 0);
    ASSERT_TRUE(semi.ok) << semi.error;
    EXPECT_EQ(semi.relation, "conflicting");
    EXPECT_EQ(semi.intervalSemitones, 1);
    EXPECT_NEAR(semi.pitchClassOverlap, 2.0 / 7.0, 1e-6);

    // Perfect fifth (and its inverse, the fourth) -> consonant.
    const auto fifth = HDAW::checkKeyConflict(0, 0, 7, 0);
    ASSERT_TRUE(fifth.ok) << fifth.error;
    EXPECT_EQ(fifth.relation, "consonant");
    EXPECT_EQ(fifth.intervalSemitones, 7);
    EXPECT_NEAR(fifth.pitchClassOverlap, 6.0 / 7.0, 1e-6);
    const auto fourth = HDAW::checkKeyConflict(7, 0, 0, 0);
    ASSERT_TRUE(fourth.ok) << fourth.error;
    EXPECT_EQ(fourth.relation, "consonant");
    EXPECT_EQ(fourth.intervalSemitones, 5);

    // A minor third apart in the SAME mode is a plain modulation -> neutral.
    const auto neut = HDAW::checkKeyConflict(9, 1, 5, 1);
    ASSERT_TRUE(neut.ok) << neut.error;
    EXPECT_EQ(neut.relation, "neutral");
    EXPECT_EQ(neut.intervalSemitones, 8);
    EXPECT_NEAR(neut.pitchClassOverlap, 3.0 / 7.0, 1e-6);
}

// G1b: the verdict reflects PITCH-CLASS OVERLAP, not only the root interval —
// a tritone-rooted pair that shares most of its material reads less hostile
// than one that shares none.
TEST(KeyConflictTheory, OverlapDrivesHostility)
{
    // Tritone roots, harmonic minor vs harmonic minor: 4 of 7 shared
    // (0.57 >= kOverlapRescue) -> rescued to neutral.
    const auto high = HDAW::checkKeyConflict(0, 7, 6, 7);
    ASSERT_TRUE(high.ok) << high.error;
    EXPECT_EQ(high.intervalSemitones, 6);
    EXPECT_EQ(high.relation, "neutral");
    EXPECT_NEAR(high.pitchClassOverlap, 4.0 / 7.0, 1e-6);

    // Tritone roots, minor pentatonic vs minor pentatonic: NO shared pitch
    // class at all -> conflicting.
    const auto none = HDAW::checkKeyConflict(0, 10, 6, 10);
    ASSERT_TRUE(none.ok) << none.error;
    EXPECT_EQ(none.relation, "conflicting");
    EXPECT_NEAR(none.pitchClassOverlap, 0.0, 1e-6);

    // Same-mode major tritone: 2 of 7 shared -> conflicting.
    const auto maj = HDAW::checkKeyConflict(0, 0, 6, 0);
    ASSERT_TRUE(maj.ok) << maj.error;
    EXPECT_EQ(maj.relation, "conflicting");
    EXPECT_NEAR(maj.pitchClassOverlap, 2.0 / 7.0, 1e-6);

    // Same 6-semitone root distance, but the verdict tracks the overlap.
    EXPECT_GT(high.pitchClassOverlap, maj.pitchClassOverlap);
    EXPECT_GT(high.pitchClassOverlap, none.pitchClassOverlap);
    EXPECT_NE(high.relation, maj.relation);
    EXPECT_NE(high.relation, none.relation);
}

// Surfaces fixture: the MCP tool AND the RPC route over one engine. Every
// test starts from a known project key: A minor.
class KeyCheckTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        engine->getProjectModel().setScaleRoot(9);
        engine->getProjectModel().setScaleMode(1);
    }

    void TearDown() override {
        server.reset();
        engine.reset();
    }

    // --- MCP surface -------------------------------------------------------
    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject { { "name", tool }, { "arguments", args } })
            .toObject();
    }
    QJsonObject mcpPayload(const QString& tool, const QJsonObject& args) {
        const auto result = mcpResult(tool, args);
        EXPECT_FALSE(result.value("isError").toBool(true))
            << "MCP " << tool.toStdString() << " errored: "
            << result.value("content").toArray().at(0).toObject()
                   .value("text").toString().toStdString();
        const auto content = result.value("content").toArray();
        if (content.isEmpty()) return {};
        return QJsonDocument::fromJson(
                   content[0].toObject().value("text").toString().toUtf8())
            .object();
    }
    QString mcpErrorText(const QString& tool, const QJsonObject& args) {
        const auto result = mcpResult(tool, args);
        EXPECT_TRUE(result.value("isError").toBool(false))
            << "expected MCP " << tool.toStdString() << " to fail, got: "
            << result.value("content").toArray().at(0).toObject()
                   .value("text").toString().toStdString();
        const auto content = result.value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }

    // --- RPC surface -------------------------------------------------------
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }
    QJsonObject rpcPayload(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_FALSE(r.isError)
            << "RPC " << method.toStdString() << " errored: "
            << r.payload.toObject().value("message").toString().toStdString();
        return r.payload.toObject();
    }
    QJsonObject rpcError(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_TRUE(r.isError) << "expected RPC " << method.toStdString() << " to fail";
        return r.payload.toObject();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// G2: no candidate arguments -> the check reads the project's current key
// (the same accessors get_scale reports).
TEST_F(KeyCheckTest, DefaultsToProjectKey)
{
    const auto scale = mcpPayload("get_scale", {});
    ASSERT_EQ(scale.value("root").toInt(), 9);
    ASSERT_EQ(scale.value("mode").toInt(), 1);

    const auto v = mcpPayload("key_check", {});
    EXPECT_TRUE(v.value("ok").toBool());
    EXPECT_EQ(v.value("relation").toString(), QString("unison"));
    EXPECT_EQ(v.value("intervalSemitones").toInt(), 0);
    EXPECT_NEAR(v.value("pitchClassOverlap").toDouble(), 1.0, 1e-9);
    // The reason names the PROJECT key — a hardcoded default would say
    // "C major" (root 0) instead of "A minor" (root 9).
    EXPECT_TRUE(v.value("reason").toString().contains("A minor"))
        << v.value("reason").toString().toStdString();
}

// G3: every candidate shape the surface already produces resolves — and the
// verdict values match hand-computed expectations against A minor.
TEST_F(KeyCheckTest, AcceptsEveryCandidateShape)
{
    // (a) Human key string (analyze_midi_file's `key`).
    const auto k = mcpPayload("key_check", { { "key", "F minor" } });
    EXPECT_EQ(k.value("relation").toString(), QString("neutral"));
    EXPECT_EQ(k.value("intervalSemitones").toInt(), 8);
    EXPECT_NEAR(k.value("pitchClassOverlap").toDouble(), 3.0 / 7.0, 1e-4);
    EXPECT_TRUE(k.value("reason").toString().contains("F minor"))
        << k.value("reason").toString().toStdString();

    // (b) root + scaleMode ints (get_scale's shape): C major vs A minor.
    const auto b = mcpPayload("key_check", { { "root", 0 }, { "scaleMode", 0 } });
    EXPECT_EQ(b.value("relation").toString(), QString("relative"));
    EXPECT_EQ(b.value("intervalSemitones").toInt(), 3);
    EXPECT_NEAR(b.value("pitchClassOverlap").toDouble(), 1.0, 1e-9);

    // (c) analyze_midi_file's fingerprint shape: MIDI rootNote + scaleType
    // (root 65 = F). F major vs A minor: 6 of 7 shared, roots 8 apart.
    const auto c = mcpPayload("key_check", { { "root", 65 }, { "scaleType", 0 } });
    EXPECT_EQ(c.value("relation").toString(), QString("neutral"));
    EXPECT_EQ(c.value("intervalSemitones").toInt(), 8);
    EXPECT_NEAR(c.value("pitchClassOverlap").toDouble(), 6.0 / 7.0, 1e-4);

    // (d) scale-mode NAME (scale_note's spelling), root defaults to the
    // project root: A dorian vs A minor -> same root, different mode.
    const auto d = mcpPayload("key_check", { { "scale", "dorian" } });
    EXPECT_EQ(d.value("relation").toString(), QString("parallel"));
    EXPECT_EQ(d.value("intervalSemitones").toInt(), 0);

    // (e) root only: the mode defaults to the project's mode (C minor here).
    const auto e = mcpPayload("key_check", { { "root", 0 } });
    EXPECT_EQ(e.value("relation").toString(), QString("neutral"));
    EXPECT_EQ(e.value("intervalSemitones").toInt(), 3);
}

// G3: unparsable / "unknown" candidates fail with a clear error, never a
// confident wrong answer.
TEST_F(KeyCheckTest, RejectsUnparsableCandidates)
{
    struct Case { QJsonObject args; QString detail; };
    const Case cases[] = {
        { { { "key", "unknown" } },            "unknown" },      // not a note
        { { { "key", "F" } },                  "no scale mode" },// undetected key
        { { { "key", "F majorish" } },         "majorish" },     // unknown mode
        { { { "key", "Hb minor" } },           "Hb" },           // unknown note
        { { { "root", 0 }, { "scaleType", -1 } }, "scaleType" },  // analyzer -1
        { { { "root", 0 }, { "scaleMode", 99 } }, "scaleMode" },
        { { { "root", 200 } },                 "200" },          // out of range
        { { { "scale", "unknown" } },          "unknown" },      // unknown name
    };
    for (const auto& c : cases) {
        const QString text = mcpErrorText("key_check", c.args);
        EXPECT_TRUE(text.contains("cannot determine the candidate's key"))
            << text.toStdString();
        EXPECT_TRUE(text.contains(c.detail)) << text.toStdString();
    }
}

// G4: MCP and RPC return IDENTICAL payloads for success and IDENTICAL failure
// text (plus the -32602 argument-error class) for the same args.
TEST_F(KeyCheckTest, McpRpcParity)
{
    const QJsonObject successArgs[] = {
        {},
        { { "key", "F minor" } },
        { { "root", 0 }, { "scaleMode", 0 } },
        { { "root", 65 }, { "scaleType", 0 } },
        { { "scale", "dorian" } },
        { { "root", 0 } },
    };
    for (const auto& args : successArgs) {
        const QJsonObject viaMcp = mcpPayload("key_check", args);
        EXPECT_FALSE(viaMcp.isEmpty());
        const auto viaRpc = rpc("composition.keyCheck", args);
        ASSERT_FALSE(viaRpc.isError)
            << "RPC errored: "
            << viaRpc.payload.toObject().value("message").toString().toStdString()
            << " for args " << QJsonDocument(args).toJson(QJsonDocument::Compact).constData();
        EXPECT_EQ(viaRpc.payload.toObject(), viaMcp)
            << "payloads differ for args "
            << QJsonDocument(args).toJson(QJsonDocument::Compact).constData();
    }

    const QJsonObject failArgs[] = {
        { { "key", "unknown" } },
        { { "key", "F" } },
        { { "key", "F majorish" } },
        { { "root", 0 }, { "scaleType", -1 } },
        { { "root", 200 } },
        { { "scale", "unknown" } },
    };
    for (const auto& args : failArgs) {
        const QString mcpText = mcpErrorText("key_check", args);
        const auto err = rpcError("composition.keyCheck", args);
        EXPECT_EQ(err.value("message").toString(), mcpText)
            << "failure text differs for args "
            << QJsonDocument(args).toJson(QJsonDocument::Compact).constData();
        EXPECT_EQ(err.value("code").toInt(), -32602);
    }
}

} // namespace
