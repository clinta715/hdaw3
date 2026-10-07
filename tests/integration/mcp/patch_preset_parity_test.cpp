// MCP save_patch / load_patch / list_patches <-> RPC project.savePatch /
// project.loadPatch / project.listPatches twin tests — the SURFACE half of the
// slot-scoped PATCH verbs (the engine half is AudioEngineCommands::
// exportPatch / applyPatch + ChainLibrary::patchLibrary, pinned by
// tests/unit/engine/patch_preset_test.cpp: this file must not change engine
// semantics).
//
// AGENTS.md feature-parity rule: the capability must be reachable on BOTH
// surfaces and each route gets a twin test asserting the SAME failure (and the
// SAME payload) on both. Both surfaces call ONE shared reader/body
// (src/common/PatchPreset.h), so the success payload and every refusal are
// byte-identical BY CONSTRUCTION — these tests prove both wirings.
//
// Root cause this pins: load_fx_chain is CHAIN-scoped (it preserves the
// target's instrument slots and APPENDS the preset's instrument as a new slot),
// so the patch verbs are the SLOT-scoped sibling: applyPatch writes INTO the
// addressed slot — never appends, never removes.
//
// Harness idioms follow tests/integration/mcp/sidechain_parity_test.cpp and
// matrix_presets_rpc_test.cpp (this TU's neighbours: a fixture engine +
// McpServer for the tool surface and frontend::dispatch for the route surface).
//
// Determinism (lesson 9): createDefaultProject() ships ZERO tracks, so every
// track/slot a case needs is created here. The fixture cleans up every patch it
// saves, so the user's HDAW/patches roster is left exactly as found.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/ChainLibrary.h"
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
#include <string>
#include <vector>

// Qt's `slots` macro (pulled in by McpServer.h's QObject) would textually blank
// ChainPreset::slots below; this TU uses no Qt signals/slots keywords.
#ifdef slots
#undef slots
#endif

namespace {

class PatchPresetParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    void TearDown() override {
        // Leave the patch library as found: delete every patch this fixture
        // saved (the patch roster is real user data under HDAW/patches).
        // ASSERTED, not best-effort: a silently-failing delete is how 17
        // test-generated patches leaked into the real library during the
        // 2026-10-06 wiring of these verbs.
        for (const QString& id : savedIds)
            EXPECT_TRUE(HDAW::ChainLibrary::patchLibrary()
                            .deletePreset(juce::String(id.toStdString())))
                << "patch left behind: " << id.toStdString();
        savedIds.clear();
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
    QJsonValue mcpValue(const QString& tool, const QJsonObject& args) {
        const QString text = mcpText(tool, args);
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isArray()) return QJsonValue(doc.array());
        if (doc.isObject()) return QJsonValue(doc.object());
        return QJsonValue(text);
    }

    // --- RPC surface --------------------------------------------------------
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

    // A failing pair: both surfaces must report the same text and the route
    // -32602 (the tool reports it as an isError result).
    void expectSameFailure(const QString& tool, const QString& method,
                           const QJsonObject& args) {
        const auto r = rpc(method, args);
        ASSERT_TRUE(r.isError) << "expected " << method.toStdString() << " to fail";
        EXPECT_TRUE(mcpIsError(tool, args)) << "expected " << tool.toStdString() << " to fail";
        const QString rpcMessage = r.payload.toObject().value("message").toString();
        const QString mcpMessage = mcpText(tool, args);
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
    void setSlotParam(int trackIndex, int slotIndex, int paramIndex, float value) {
        engine->getAudioEngineCommands().setFxSlotParam(trackIndex, slotIndex, paramIndex, value);
        engine->drainPendingRoutingRebuild();
    }
    void setSlotParamRoute(int trackIndex, int slotIndex, int paramIndex, float value) {
        const auto r = rpc("project.setFxSlotParam",
                           QJsonObject{ { "trackIndex", trackIndex },
                                        { "slotIndex", slotIndex },
                                        { "paramIndex", paramIndex },
                                        { "value", value } });
        ASSERT_FALSE(r.isError);
        engine->drainPendingRoutingRebuild();
    }
    QString saveVia(const char* surface, const QJsonObject& args) {
        QJsonValue v;
        if (std::string(surface) == "mcp") {
            // ONE invocation only: save_patch is NOT idempotent — each call
            // writes a NEW uniquely-suffixed file. Reading the error through
            // mcpIsError() AND the payload through mcpValue() would therefore
            // save twice and leak the first, untracked file into the real
            // patch library (measured: one leaked patch per saving test).
            const QJsonObject res = mcpResult("save_patch", args);
            const auto content = res.value("content").toArray();
            const QString text = content.isEmpty()
                                     ? QString()
                                     : content[0].toObject().value("text").toString();
            EXPECT_FALSE(res.value("isError").toBool()) << text.toStdString();
            const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
            v = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(text);
        } else {
            v = rpcPayload("project.savePatch", args);
        }
        const QString id = v.toObject().value("id").toString();
        if (!id.isEmpty()) savedIds.push_back(id);
        return id;
    }
    QJsonObject listedPatch(const QString& id) {
        for (const auto& v : mcpValue("list_patches", QJsonObject{}).toArray())
            if (v.toObject().value("id").toString() == id) return v.toObject();
        return QJsonObject{};
    }
    // A unique-per-run patch name so a leftover from an interrupted run can
    // never collide with this run's save.
    QString uniqueName(const char* stem) {
        return QString(stem) + QString::number(juce::Time::getMillisecondCounter());
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::vector<QString> savedIds;
};

// ─── (a)+(b) save → list → load round-trip on BOTH surfaces ─────────────────

TEST_F(PatchPresetParityTest, SaveListLoadRoundTripMatchesOnBothSurfaces) {
    const int src = addTrack("Source");     // 0
    const int dst = addTrack("Dest");       // 1
    addSlot(src, "reverb");
    addSlot(dst, "reverb");

    // Differentiate the two slots' params through the ROUTE (not a surface
    // under test), so the patch has a state the destination does not already
    // hold.
    setSlotParamRoute(src, 0, 0, 0.91f);
    setSlotParamRoute(src, 0, 3, 0.33f);
    setSlotParamRoute(dst, 0, 0, 0.10f);
    setSlotParamRoute(dst, 0, 3, 0.80f);

    const HDAW::ChainPreset srcBefore = engine->getProjectCommands().exportPatch(src, 0);
    ASSERT_EQ(srcBefore.slots.size(), 1u);

    // (a) save on the TOOL.
    const QString nameTool = uniqueName("Parity Reverb ");
    const QJsonObject saveTool{ { "trackId", src }, { "slotIndex", 0 },
                                { "name", nameTool } };
    const QString idTool = saveVia("mcp", saveTool);
    ASSERT_FALSE(idTool.isEmpty());
    EXPECT_TRUE(idTool.startsWith("user/"));

    // …and save on the ROUTE, same payload shape ({id}).
    const QString nameRoute = uniqueName("Parity Reverb R ");
    const QJsonObject saveRoute{ { "trackId", src }, { "slotIndex", 0 },
                                 { "name", nameRoute } };
    const QString idRoute = saveVia("route", saveRoute);
    ASSERT_FALSE(idRoute.isEmpty());
    EXPECT_NE(idTool, idRoute);

    // (a) list_patches shows both, with the source slot's fxType/slotCount, and
    // the tool and the route agree on the roster.
    const QJsonObject rowTool = listedPatch(idTool);
    ASSERT_FALSE(rowTool.isEmpty());
    EXPECT_EQ(rowTool.value("name").toString(), nameTool);
    EXPECT_EQ(rowTool.value("fxType").toString(), QString("reverb"));
    EXPECT_EQ(rowTool.value("slotCount").toInt(), 1);
    EXPECT_EQ(rowTool.value("source").toString(), QString("user"));
    EXPECT_FALSE(listedPatch(idRoute).isEmpty());
    EXPECT_EQ(rpcPayload("project.listPatches", QJsonObject{}), mcpValue("list_patches", {}))
        << "the roster payload must be identical on both surfaces";

    // The roster also carries the shipped factory bass patches: at least one
    // source:"factory" row whose single slot is a reese_bass.
    {
        bool foundFactoryReese = false;
        for (const auto& v : mcpValue("list_patches", QJsonObject{}).toArray()) {
            const QJsonObject row = v.toObject();
            if (row.value("source").toString() == "factory"
                && row.value("fxType").toString() == "reese_bass") {
                foundFactoryReese = true;
                EXPECT_TRUE(row.value("id").toString().startsWith("_factory/"));
                EXPECT_EQ(row.value("slotCount").toInt(), 1);
            }
        }
        EXPECT_TRUE(foundFactoryReese)
            << "the shipped factory bass roster must list a reese_bass patch";
    }

    // (b) load the TOOL-saved patch on the ROUTE onto the destination…
    const QJsonObject loadRoute{ { "trackId", dst }, { "slotIndex", 0 }, { "id", idTool } };
    EXPECT_EQ(rpcPayload("project.loadPatch", loadRoute).toString(), QString("ok"));
    // …and the ROUTE-saved patch on the TOOL onto the destination.
    const QJsonObject loadTool{ { "trackId", dst }, { "slotIndex", 0 }, { "id", idRoute } };
    EXPECT_EQ(mcpValue("load_patch", loadTool).toString(), QString("ok"))
        << mcpText("load_patch", loadTool).toStdString();

    // The destination slot now equals the source slot, and the chain was NOT
    // grown (a patch writes INTO the slot).
    const HDAW::ChainPreset dstAfter = engine->getProjectCommands().exportPatch(dst, 0);
    ASSERT_EQ(dstAfter.slots.size(), 1u);
    EXPECT_EQ(dstAfter.slots[0].fxType, srcBefore.slots[0].fxType);
    EXPECT_EQ(dstAfter.slots[0].params, srcBefore.slots[0].params)
        << "applyPatch must restore the source slot's params exactly";
    EXPECT_EQ(engine->getReadModel().getFxSlots(dst).size(), 1u)
        << "a patch must never append a slot";
}

// ─── (c) an UNKNOWN patch id is refused identically ─────────────────────────

TEST_F(PatchPresetParityTest, UnknownIdRefusedIdenticallyOnBothSurfaces) {
    const int t = addTrack("A");
    addSlot(t, "eq");
    const QString unknown = "user/No Such Patch 424242.json";
    const QJsonObject args{ { "trackId", t }, { "slotIndex", 0 }, { "id", unknown } };
    expectSameFailure("load_patch", "project.loadPatch", args);
    EXPECT_EQ(mcpText("load_patch", args).toStdString(),
              "preset not found: " + unknown.toStdString())
        << "an unknown id must be a REFUSAL, never a silent no-op";
    // Nothing was written: the slot's params are untouched.
    EXPECT_EQ(engine->getReadModel().getFxSlots(t).size(), 1u);
}

// ─── (d) a bad slotIndex is refused identically ─────────────────────────────

TEST_F(PatchPresetParityTest, BadSlotIndexRefusedIdenticallyOnBothSurfaces) {
    const int t = addTrack("A");
    addSlot(t, "eq");

    const QJsonObject saveArgs{ { "trackId", t }, { "slotIndex", 5 },
                                { "name", uniqueName("Nope ") } };
    expectSameFailure("save_patch", "project.savePatch", saveArgs);
    EXPECT_EQ(mcpText("save_patch", saveArgs).toStdString(), "slot not found");

    const QJsonObject loadArgs{ { "trackId", t }, { "slotIndex", 5 },
                                { "id", "user/whatever.json" } };
    expectSameFailure("load_patch", "project.loadPatch", loadArgs);
    EXPECT_EQ(mcpText("load_patch", loadArgs).toStdString(), "slot not found");
}

// ─── fxType mismatch: the command's refusal travels verbatim on both surfaces ─

TEST_F(PatchPresetParityTest, FxTypeMismatchRefusedIdenticallyOnBothSurfaces) {
    const int src = addTrack("Source");   // 0, reverb
    const int dst = addTrack("Dest");     // 1, eq
    addSlot(src, "reverb");
    addSlot(dst, "eq");

    const QString id = saveVia("mcp", QJsonObject{ { "trackId", src }, { "slotIndex", 0 },
                                                   { "name", uniqueName("Rev ") } });
    ASSERT_FALSE(id.isEmpty());

    const QJsonObject args{ { "trackId", dst }, { "slotIndex", 0 }, { "id", id } };
    expectSameFailure("load_patch", "project.loadPatch", args);
    const QString msg = mcpText("load_patch", args);
    EXPECT_TRUE(msg.contains("fxType mismatch")) << msg.toStdString();
    // Nothing written: the destination is still a 1-slot eq with its own params.
    const HDAW::ChainPreset dstAfter = engine->getProjectCommands().exportPatch(dst, 0);
    ASSERT_EQ(dstAfter.slots.size(), 1u);
    EXPECT_EQ(dstAfter.slots[0].fxType.toStdString(), "eq");
}

// ─── empty-name save and the mirrored schema gate ──────────────────────────

TEST_F(PatchPresetParityTest, EmptyNameAndMissingArgsRefusedIdentically) {
    const int t = addTrack("A");
    addSlot(t, "eq");

    // (1) empty name (present, so the schema passes; the body refuses).
    const QJsonObject emptyName{ { "trackId", t }, { "slotIndex", 0 }, { "name", "" } };
    expectSameFailure("save_patch", "project.savePatch", emptyName);
    EXPECT_EQ(mcpText("save_patch", emptyName).toStdString(), "name required");

    // (2) missing required key: the mirrored schema gate words it exactly as
    // mcp::validateSchema does (byte-identical on the validatorless route).
    const QJsonObject missingSlot{ { "trackId", t }, { "name", "x" } };
    expectSameFailure("save_patch", "project.savePatch", missingSlot);
    EXPECT_EQ(mcpText("save_patch", missingSlot).toStdString(),
              "invalid params: slotIndex: missing required property 'slotIndex'");

    // (3) an unknown key is refused by the same gate.
    const QJsonObject unknownKey{ { "trackId", t }, { "slotIndex", 0 },
                                  { "name", "x" }, { "bogus", 1 } };
    expectSameFailure("save_patch", "project.savePatch", unknownKey);
    EXPECT_EQ(mcpText("save_patch", unknownKey).toStdString(),
              "invalid params: bogus: unknown property");

    // (4) the ZERO-ARG list_patches schema: the parity ratchet's dispatch probe
    // sends `{}`, which must NOT read as an unknown method; any key is unknown.
    const auto probe = rpc("project.listPatches", QJsonObject{});
    EXPECT_FALSE(probe.isError) << "an empty argument object is a legal list request";
    const QJsonObject listUnknownKey{ { "bogus", 1 } };
    expectSameFailure("list_patches", "project.listPatches", listUnknownKey);
    EXPECT_EQ(mcpText("list_patches", listUnknownKey).toStdString(),
              "invalid params: bogus: unknown property");
}

// ─── stable-id semantics: the id wins; a disagreement is an error ──────────

TEST_F(PatchPresetParityTest, StableTrackIdDrivesPatchVerbsOnBothSurfaces) {
    const int target = addTrack("Target");   // 0
    const int decoy  = addTrack("Decoy");    // 1
    addSlot(target, "reverb");
    addSlot(decoy, "reverb");
    setSlotParamRoute(target, 0, 0, 0.77f);
    setSlotParamRoute(decoy, 0, 0, 0.11f);
    const int targetID = stableID(target);
    ASSERT_GT(targetID, 0);

    const HDAW::ChainPreset targetBefore = engine->getProjectCommands().exportPatch(target, 0);

    // A DISAGREEING positional+stable pair is REFUSED on both surfaces: the
    // shared stable-ref contract makes the id authoritative by REQUIRING the two
    // spellings to name the same track (positional `decoy` is not stable
    // `targetID`), never silently preferring one.
    {
        const QJsonObject disagree{ { "trackId", decoy }, { "trackID", targetID },
                                    { "slotIndex", 0 },
                                    { "name", uniqueName("Disagree ") } };
        EXPECT_TRUE(mcpIsError("save_patch", disagree))
            << mcpText("save_patch", disagree).toStdString();
        EXPECT_TRUE(rpc("project.savePatch", disagree).isError);
    }

    // Addressed by the STABLE id alone, the verb saves THAT track's slot — the
    // id's own slot, not a positional neighbour's.
    const QString id = saveVia("mcp", QJsonObject{ { "trackID", targetID },
                                                   { "slotIndex", 0 },
                                                   { "name", uniqueName("Target Only ") } });
    ASSERT_FALSE(id.isEmpty());
    const HDAW::ChainPreset saved = HDAW::ChainLibrary::patchLibrary().loadPreset(
        juce::String(id.toStdString()));
    ASSERT_EQ(saved.slots.size(), 1u);
    // Disk round-trip: the preset's params travel through JSON, which emits a
    // double at printable precision, so a stored float can differ from the live
    // capture in the last double bit (0.769999980926514 vs …51367). Every value
    // is a float by construction (setFxSlotParam takes float), so the round-trip
    // is lossless where it matters — compare with a tiny tolerance rather than
    // an exact double map equality.
    ASSERT_EQ(saved.slots[0].params.size(), targetBefore.slots[0].params.size());
    for (const auto& kv : targetBefore.slots[0].params) {
        const auto it = saved.slots[0].params.find(kv.first);
        ASSERT_NE(it, saved.slots[0].params.end()) << kv.first.toStdString();
        EXPECT_NEAR(kv.second, it->second, 1e-9) << kv.first.toStdString();
    }

    // Unknown id: identical text, naming the spelling and the value.
    const QJsonObject unknownId{ { "trackID", 4242 }, { "slotIndex", 0 },
                                 { "name", uniqueName("Nope ") } };
    expectSameFailure("save_patch", "project.savePatch", unknownId);
    EXPECT_EQ(mcpText("save_patch", unknownId).toStdString(), "unknown trackID 4242");

    // trackId 0 vs trackID naming track 1: a disagreement is refused.
    if (targetID != 1) {
        const QJsonObject disagree{ { "trackId", 0 }, { "trackID", targetID },
                                    { "slotIndex", 0 },
                                    { "name", uniqueName("Nope ") } };
        expectSameFailure("save_patch", "project.savePatch", disagree);
        EXPECT_TRUE(mcpText("save_patch", disagree).contains("disagree"));
    }
}

// ─── psy_fm: the patch carries the modulation matrix (the live smoke's core) ─

TEST_F(PatchPresetParityTest, PsyFmMatrixRoundTripsThroughPatch) {
    const int src = addTrack("FmSrc");   // 0
    const int dst = addTrack("FmDst");   // 1
    addSlot(src, "psy_fm");
    addSlot(dst, "psy_fm");

    // Give the source a distinctive routing through the TOOL under test's
    // sibling (psy_fm_set_mod_route), so the matrix is what travels.
    const QJsonObject route{ { "trackId", src }, { "slotIndex", 0 },
                             { "source", "feedbackLFO" }, { "dest", "op6Feedback" },
                             { "depth", 0.42 } };
    EXPECT_FALSE(mcpIsError("psy_fm_set_mod_route", route))
        << mcpText("psy_fm_set_mod_route", route).toStdString();

    const QString id = saveVia("mcp", QJsonObject{ { "trackId", src }, { "slotIndex", 0 },
                                                   { "name", uniqueName("Fm Patch ") } });
    ASSERT_FALSE(id.isEmpty());

    // Clear the destination, then load the patch onto it via the ROUTE.
    EXPECT_FALSE(mcpIsError("psy_fm_clear_mod_matrix", QJsonObject{ { "trackId", dst },
                                                                    { "slotIndex", 0 } }));
    const QJsonObject loadArgs{ { "trackId", dst }, { "slotIndex", 0 }, { "id", id } };
    EXPECT_EQ(rpcPayload("project.loadPatch", loadArgs).toString(), QString("ok"));

    // The destination's persisted matrix now equals the source's.
    const HDAW::ChainPreset srcPatch = engine->getProjectCommands().exportPatch(src, 0);
    const HDAW::ChainPreset dstPatch = engine->getProjectCommands().exportPatch(dst, 0);
    ASSERT_EQ(srcPatch.slots.size(), 1u);
    ASSERT_EQ(dstPatch.slots.size(), 1u);
    EXPECT_FALSE(srcPatch.slots[0].psyFmMatrix.isEmpty());
    EXPECT_EQ(dstPatch.slots[0].psyFmMatrix, srcPatch.slots[0].psyFmMatrix);

    // And the READ-side debug view agrees on both surfaces (the live smoke's
    // assertion, pinned here offline).
    const QJsonValue dbgTool = mcpValue("psy_fm_mod_matrix_debug",
                                        QJsonObject{ { "trackId", dst }, { "slotIndex", 0 } });
    // The RPC twin's positional spelling is `trackIndex` (StableRefKeys, see
    // Router_PsyFm.cpp modMatrixDebug), not the tool's `trackId`.
    const QJsonValue dbgRoute = rpcPayload("psy_fm.modMatrixDebug",
                                           QJsonObject{ { "trackIndex", dst }, { "slotIndex", 0 } });
    EXPECT_EQ(dbgRoute, dbgTool);
}

} // namespace
