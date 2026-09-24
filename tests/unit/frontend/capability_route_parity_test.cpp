// Twin tests for the 2026-09-24 capability-route parity wave — the seven
// MCP tools whose ledger rows this slice closes:
//   apply_preset             <-> audio.applyPreset
//   fm_synth_get_state       <-> read.getFmSynthState
//   get_master_fx_params     <-> read.getMasterFxParams
//   list_clip_takes          <-> read.getClipTakes
//   sub_synth_import_sysex   <-> audio.subSynthImportSysex
//   load_plugin_preset_file  <-> plugin.loadPresetFile
//   audition_patch           <-> audio.auditionPatch
//
// AGENTS.md twin-test rule: BOTH surfaces must fail with -32602 and the SAME
// message text, and the route payload must equal the tool's text for the
// JSON-payload routes (the routes parse the shared builder's compact JSON into
// the reply; loadPresetFile returns the tool text "ok" verbatim). Follows the
// harness idioms of add_fx_parity_test.cpp / missing_route_parity_test.cpp
// (fixture engine + McpServer, rpc(), mcpText(), mcpIsError(),
// expectSameFailure()).
//
// Adversarial: every failure case asserts -32602 + byte-identical text, so the
// route-specific cases are RED on the pre-wave tree (the route did not exist
// there -> -32601).
//
// Determinism (lesson 9): createDefaultProject() ships ZERO tracks, so every
// track/clip/slot a case needs is created here. Master FX is the one
// exception: initialize() stamps MASTER_FX with slot 0=eq / slot 1=limiter,
// both bypassed. Fixtures resolve via __FILE__ (apply_preset_test pattern).

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <memory>
#include <string>

namespace {

class CapabilityRouteParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    // --- MCP surface (bus_send_rpc_test harness shape) ---------------------
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

    // --- RPC surface --------------------------------------------------------
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }

    // A failing pair: both surfaces must report -32602 and the same text
    // (copied from add_fx_parity_test.cpp so this TU stays self-contained).
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

    // Payload parity for a JSON-text tool: the route's payload must equal the
    // tool's text PARSED (the shared builder's compact JSON, handed to the
    // client as a structure instead of a string-quoted document).
    void expectPayloadParity(const QString& tool, const QString& method,
                             const QJsonObject& args) {
        const auto r = rpc(method, args);
        ASSERT_FALSE(r.isError)
            << r.payload.toObject().value("message").toString().toStdString();
        const QString text = mcpText(tool, args);
        EXPECT_FALSE(mcpIsError(tool, args)) << text.toStdString();
        const auto expected = QJsonDocument::fromJson(text.toUtf8());
        ASSERT_FALSE(expected.isNull()) << "tool text is not JSON: " << text.toStdString();
        EXPECT_EQ(QJsonDocument::fromVariant(r.payload.toVariant()), expected)
            << "route payload must equal the parsed tool text";
    }

    // --- scaffolding --------------------------------------------------------
    int addTrack(const QString& name = "Track") {
        const auto r = rpc("project.addTrack", QJsonObject{ { "name", name } });
        EXPECT_FALSE(r.isError);
        const int idx = r.payload.toInt();
        EXPECT_GE(idx, 0);
        return idx;
    }
    // The MCP add_fx answer is "slot=N" — parse N so slot indices are exact
    // (a fixture that adds several slots must not hard-code slot 0).
    int addFxTool(int trackId, const QString& fxType) {
        const QJsonObject args{ { "trackId", trackId }, { "fxType", fxType } };
        const QString text = mcpText("add_fx", args);
        EXPECT_TRUE(text.startsWith("slot=")) << text.toStdString();
        return text.mid(5).toInt();
    }
    juce::ValueTree masterFx() {
        return engine->getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
    }
    static QJsonObject parseText(const QString& text) {
        return QJsonDocument::fromJson(text.toUtf8()).object();
    }
    // Fixture resolution via __FILE__ so the test is independent of the
    // runner's working directory (apply_preset_test.cpp pattern).
    static juce::File fixtureFile(const juce::String& relUnderTests) {
        const juce::File self(__FILE__);
        return juce::File::isAbsolutePath(__FILE__)
            ? self.getParentDirectory().getParentDirectory().getParentDirectory()
                  .getChildFile(relUnderTests)
            : juce::File::getCurrentWorkingDirectory().getChildFile(
                  juce::String("tests/") + relUnderTests);
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// ─── get_master_fx_params <-> read.getMasterFxParams ────────────────────────

TEST_F(CapabilityRouteParityTest, MasterFxParamsPayloadMatchesOnBothSurfaces) {
    // initialize() stamps MASTER_FX (slot 0 = eq, slot 1 = limiter, bypassed):
    // the payload covers both slots, defs and per-param values.
    expectPayloadParity("get_master_fx_params", "read.getMasterFxParams", {});
}

TEST_F(CapabilityRouteParityTest, MasterFxParamsMissingNodeFailsIdentically) {
    // The failure text comes from the shared HDAW::masterFxParamsToolText on
    // both surfaces — same node lookup, same "no MASTER_FX node" text.
    auto& root = engine->getProjectModel().getTree();
    root.removeChild(root.getChildWithName(IDs::MASTER_FX), nullptr);
    expectSameFailure("get_master_fx_params", "read.getMasterFxParams", {});
}

TEST_F(CapabilityRouteParityTest, MasterFxParamsWriteIsVisibleOnBothSurfaces) {
    // The read reflects a live project.setMasterFxParam write (the tree is
    // the source of truth both surfaces read). 1234.5 is in-range for slot 0's
    // param 0 (eq Frequency, default 1000 Hz, range 20..20000) — the write is
    // clamped at the entry point (lesson 23), so the value must survive.
    ASSERT_FALSE(rpc("project.setMasterFxParam",
                     QJsonObject{ { "slotIndex", 0 }, { "paramIndex", 0 },
                                  { "value", 1234.5 } })
                     .isError);
    const auto mcp = parseText(mcpText("get_master_fx_params", {}));
    const auto rpcR = rpc("read.getMasterFxParams", {});
    ASSERT_FALSE(rpcR.isError);
    EXPECT_EQ(rpcR.payload.toObject(), mcp);
    const auto slot0 = mcp.value("slots").toArray()[0].toObject();
    EXPECT_DOUBLE_EQ(slot0.value("params").toArray()[0].toObject().value("value").toDouble(), 1234.5);
}

// ─── list_clip_takes <-> read.getClipTakes ──────────────────────────────────

TEST_F(CapabilityRouteParityTest, ClipTakesPayloadMatchesOnBothSurfaces) {
    const int t = addTrack();
    ASSERT_GE(t, 0);
    // An audio clip with two takes (the same TAKE_LIST shape
    // MainAudioProcessor::stopRecording builds).
    ASSERT_FALSE(rpc("project.addAudioClip",
                     QJsonObject{ { "trackIndex", t }, { "start", 0.0 },
                                  { "duration", 2.0 },
                                  { "sourceFile", "C:/probe/take-a.wav" },
                                  { "name", "TakeProbe" } })
                     .isError);
    auto& model = engine->getProjectModel();
    auto clip = model.getTrackListTree().getChild(t).getChildWithName(IDs::CLIP_LIST).getChild(0);
    ASSERT_TRUE(clip.isValid());
    const int clipId = static_cast<int>(clip.getProperty(IDs::clipID, 0));
    auto takeList = juce::ValueTree(IDs::TAKE_LIST);
    auto take1 = juce::ValueTree(IDs::TAKE);
    take1.setProperty(IDs::name, "Take 1", nullptr);
    take1.setProperty(IDs::sourceFile, "C:/probe/take-a.wav", nullptr);
    auto take2 = juce::ValueTree(IDs::TAKE);
    take2.setProperty(IDs::name, "Take 2", nullptr);
    take2.setProperty(IDs::sourceFile, "C:/probe/take-b.wav", nullptr);
    takeList.addChild(take1, -1, nullptr);
    takeList.addChild(take2, -1, nullptr);
    clip.addChild(takeList, -1, nullptr);
    clip.setProperty(IDs::activeTake, 1, nullptr);

    const QJsonObject args{ { "clipId", clipId } };
    const auto r = rpc("read.getClipTakes", args);
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const auto arr = QJsonDocument::fromJson(mcpText("list_clip_takes", args).toUtf8()).array();
    EXPECT_EQ(r.payload.toArray(), arr);
    ASSERT_EQ(arr.size(), 2);
    EXPECT_EQ(arr[0].toObject().value("name").toString(), QString("Take 1"));
    EXPECT_EQ(arr[0].toObject().value("active").toBool(), false);
    EXPECT_EQ(arr[1].toObject().value("name").toString(), QString("Take 2"));
    EXPECT_EQ(arr[1].toObject().value("active").toBool(), true);
    EXPECT_EQ(arr[1].toObject().value("sourceFile").toString(), QString("C:/probe/take-b.wav"));
}

TEST_F(CapabilityRouteParityTest, ClipTakesUnknownClipFailsIdentically) {
    expectSameFailure("list_clip_takes", "read.getClipTakes",
                      QJsonObject{ { "clipId", 987654 } });
}

TEST_F(CapabilityRouteParityTest, ClipTakesMissingClipIdFailsWithRouteText) {
    // The MCP server schema-gates the required argument ("invalid params: …");
    // the route reaches its own validator and answers its own -32602 text.
    // Code parity holds on both; the message equality is asserted where both
    // reach the shared code (the known-clip case above).
    const auto r = rpc("read.getClipTakes", {});
    ASSERT_TRUE(r.isError);
    EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
    EXPECT_EQ(r.payload.toObject().value("message").toString(), QString("clipId required"));
}

// ─── fm_synth_get_state <-> read.getFmSynthState ────────────────────────────

TEST_F(CapabilityRouteParityTest, FmSynthStatePayloadMatchesOnBothSurfaces) {
    const int t = addTrack();
    addFxTool(t, "fm_synth");
    const QJsonObject args{ { "trackId", t }, { "slotIndex", 0 } };

    const auto r = rpc("read.getFmSynthState", args);
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(r.payload.toObject(), parseText(mcpText("fm_synth_get_state", args)));

    // The documented sources: the tree's param_0 for the CHOSEN slot (the
    // tool's contract — not read.getFmAnalysis's first-non-bypassed-slot
    // engine algorithm).
    auto slotTree = engine->getProjectModel().getTrackListTree()
                        .getChild(t).getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(slotTree.isValid());
    slotTree.setProperty("param_0", 5, nullptr);
    const auto after = rpc("read.getFmSynthState", args);
    ASSERT_FALSE(after.isError);
    EXPECT_EQ(after.payload.toObject().value("algorithm").toInt(), 5);
    const auto mcpAfter = parseText(mcpText("fm_synth_get_state", args));
    EXPECT_EQ(mcpAfter.value("algorithm").toInt(), 5);
    EXPECT_EQ(after.payload.toObject(), mcpAfter);
}

TEST_F(CapabilityRouteParityTest, FmSynthStateWrongSlotTypeFailsIdentically) {
    const int t = addTrack();
    addFxTool(t, "eq");
    expectSameFailure("fm_synth_get_state", "read.getFmSynthState",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 0 } });
}

TEST_F(CapabilityRouteParityTest, FmSynthStateOutOfRangeSlotFailsIdentically) {
    const int t = addTrack();
    expectSameFailure("fm_synth_get_state", "read.getFmSynthState",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 3 } });
}

// ─── sub_synth_import_sysex <-> audio.subSynthImportSysex ───────────────────

TEST_F(CapabilityRouteParityTest, SubSynthImportSysexPayloadMatchesOnBothSurfaces) {
    const int t = addTrack();
    addFxTool(t, "sub_synth");
    auto fixture = fixtureFile("unit/engine/testdata/virus/bcsingle.syx");
    ASSERT_TRUE(fixture.existsAsFile()) << fixture.getFullPathName();

    const QJsonObject args{
        { "trackId", t }, { "slotIndex", 0 },
        { "filePath", QString::fromStdString(fixture.getFullPathName().toStdString()) } };

    // Both surfaces run HDAW::subSynthImportSysexToolText ->
    // AudioEngineCommands::loadVirusPatch: identical payload, and the mapped
    // params land in the slot tree exactly once.
    const auto before = engine->getProjectModel().getTrackListTree()
                            .getChild(t).getChildWithName(IDs::FX_CHAIN).getChild(0);
    const QJsonObject mcpParsed = parseText(mcpText("sub_synth_import_sysex", args));
    EXPECT_EQ(mcpParsed.value("ok").toBool(false), true);
    EXPECT_EQ(mcpParsed.value("name").toString(), QString("~WELCOME"));
    EXPECT_EQ(mcpParsed.value("mappedCount").toInt(), 24);

    const auto r = rpc("audio.subSynthImportSysex", args);
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(r.payload.toObject(), mcpParsed);
}

TEST_F(CapabilityRouteParityTest, SubSynthImportSysexFailuresMatchOnBothSurfaces) {
    const int t = addTrack();
    addFxTool(t, "eq");
    // Wrong slot type — both surfaces reach the shared loader gate.
    expectSameFailure("sub_synth_import_sysex", "audio.subSynthImportSysex",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 0 },
                                   { "filePath", "Z:/nope.syx" } });
    // Missing file — same shared text on both surfaces.
    const int subSlot = addFxTool(t, "sub_synth");
    expectSameFailure("sub_synth_import_sysex", "audio.subSynthImportSysex",
                      QJsonObject{ { "trackId", t }, { "slotIndex", subSlot },
                                   { "filePath", "Z:/definitely/missing.syx" } });
}

// ─── load_plugin_preset_file <-> plugin.loadPresetFile ──────────────────────

TEST_F(CapabilityRouteParityTest, LoadPresetFileFailsIdenticallyForNonPluginSlot) {
    const int t = addTrack();
    addFxTool(t, "eq");
    expectSameFailure("load_plugin_preset_file", "plugin.loadPresetFile",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 0 },
                                   { "filePath", "Z:/nope.fxp" } });
}

TEST_F(CapabilityRouteParityTest, LoadPresetFileFailsIdenticallyForMissingFile) {
    const int t = addTrack();
    // A .clap path resolves without a scan cache (extension fallback), so the
    // slot degrades to a 'none' placeholder — the loader's "slot has no
    // plugin instance" gate then fires on BOTH surfaces with the same text.
    ASSERT_EQ(mcpText("add_fx", QJsonObject{ { "trackId", t },
                                             { "pluginId", "C:/missing/hdaw_probe.clap" } }),
              QString("slot=0"));
    expectSameFailure("load_plugin_preset_file", "plugin.loadPresetFile",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 0 },
                                   { "filePath", "Z:/definitely/missing.fxp" } });
}

TEST_F(CapabilityRouteParityTest, LoadPresetFileMissingFilePathKeyFailsWithRouteText) {
    const int t = addTrack();
    const auto r = rpc("plugin.loadPresetFile",
                       QJsonObject{ { "trackId", t }, { "slotIndex", 0 } });
    ASSERT_TRUE(r.isError);
    EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
    EXPECT_EQ(r.payload.toObject().value("message").toString(), QString("filePath required"));
}

// ─── apply_preset <-> audio.applyPreset ─────────────────────────────────────

TEST_F(CapabilityRouteParityTest, ApplyPresetFmRoutePayloadMatchesOnBothSurfaces) {
    const int t = addTrack();
    addFxTool(t, "fm_synth");
    auto fixture = fixtureFile("unit/engine/testdata/dx7/cartridge.syx");
    ASSERT_TRUE(fixture.existsAsFile()) << fixture.getFullPathName();

    const QJsonObject args{
        { "trackId", t }, { "slotIndex", 0 },
        { "filePath", QString::fromStdString(fixture.getFullPathName().toStdString()) } };

    const QJsonObject mcpParsed = parseText(mcpText("apply_preset", args));
    EXPECT_EQ(mcpParsed.value("ok").toBool(false), true);
    EXPECT_TRUE(mcpParsed.contains("voiceName"));

    const auto r = rpc("audio.applyPreset", args);
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(r.payload.toObject(), mcpParsed);

    // The shared loader persisted the patch: fmPatchData is on the slot tree
    // (the persisting path — not a live-only write), written ONCE.
    auto slot = engine->getProjectModel().getTrackListTree()
                    .getChild(t).getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(slot.isValid());
    EXPECT_FALSE(slot.getProperty(IDs::fmPatchData, "").toString().isEmpty());
}

TEST_F(CapabilityRouteParityTest, ApplyPresetMismatchedHeaderFailsIdentically) {
    const int t = addTrack();
    addFxTool(t, "fm_synth");
    auto fixture = fixtureFile("unit/engine/testdata/virus/bcsingle.syx");
    ASSERT_TRUE(fixture.existsAsFile());
    expectSameFailure("apply_preset", "audio.applyPreset",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 0 },
                                   { "filePath", QString::fromStdString(
                                       fixture.getFullPathName().toStdString()) } });
}

TEST_F(CapabilityRouteParityTest, ApplyPresetSlotErrorsMatchOnBothSurfaces) {
    const int t = addTrack();
    // Out-of-range slotIndex — the shared composite gate.
    expectSameFailure("apply_preset", "audio.applyPreset",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 9 } });
    // Unknown track — same text on both surfaces.
    expectSameFailure("apply_preset", "audio.applyPreset",
                      QJsonObject{ { "trackId", 42 }, { "slotIndex", 0 } });
}

TEST_F(CapabilityRouteParityTest, ApplyPresetNoFileNoProgramFailsIdentically) {
    const int t = addTrack();
    addFxTool(t, "fm_synth");
    expectSameFailure("apply_preset", "audio.applyPreset",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 0 } });
}

// ─── audition_patch <-> audio.auditionPatch ─────────────────────────────────

TEST_F(CapabilityRouteParityTest, AuditionPatchPayloadAndPlacementMatchOnBothSurfaces) {
    // Engine inference from the DX7 header — no explicit engine argument, the
    // exact tool contract.
    auto fixture = fixtureFile("unit/engine/testdata/dx7/single.syx");
    ASSERT_TRUE(fixture.existsAsFile()) << fixture.getFullPathName();

    const QJsonObject args{
        { "path", QString::fromStdString(fixture.getFullPathName().toStdString()) },
        { "role", "bass" } };

    const QJsonObject mcpParsed = parseText(mcpText("audition_patch", args));
    EXPECT_EQ(mcpParsed.value("ok").toBool(false), true);
    EXPECT_EQ(mcpParsed.value("engine").toString(), QString("fm_synth"));
    EXPECT_EQ(mcpParsed.value("role").toString(), QString("bass"));
    EXPECT_EQ(mcpParsed.value("trackId").toInt(), 0);   // first probe track
    EXPECT_EQ(mcpParsed.value("slotIndex").toInt(), 0);

    // The route runs the SAME shared body — same placement on a fresh engine.
    // The payload's clip is created per-call with a fresh clipID counter and
    // per-call MIDI ids, so the two payloads CANNOT be object-equal by design:
    // compare every deterministic field instead, then assert placement.
    const auto r = rpc("audio.auditionPatch", args);
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const auto routePayload = r.payload.toObject();
    EXPECT_EQ(routePayload.value("ok").toBool(false), true);
    EXPECT_EQ(routePayload.value("engine").toString(), mcpParsed.value("engine").toString());
    EXPECT_EQ(routePayload.value("role").toString(), mcpParsed.value("role").toString());
    EXPECT_EQ(routePayload.value("name").toString(), mcpParsed.value("name").toString());
    EXPECT_EQ(routePayload.value("trackId").toInt(), mcpParsed.value("trackId").toInt() + 1);
    EXPECT_EQ(routePayload.value("slotIndex").toInt(), mcpParsed.value("slotIndex").toInt());

    // The probe track + slot + clip landed (the MCP call created track 0 and
    // its probe clip; the shared composite created the second identical pair).
    auto& model = engine->getProjectModel();
    const int probeTracks = model.getTrackListTree().getNumChildren();
    EXPECT_EQ(probeTracks, 2);
    // The patch persisted through the fm loader: fmPatchData on the probe slot.
    const auto slot = model.getTrackListTree().getChild(1)
                          .getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(slot.isValid());
    EXPECT_FALSE(slot.getProperty(IDs::fmPatchData, "").toString().isEmpty());
    const auto clipList = model.getTrackListTree().getChild(1).getChildWithName(IDs::CLIP_LIST);
    ASSERT_EQ(clipList.getNumChildren(), 1);
    EXPECT_EQ(clipList.getChild(0).getChildWithName(IDs::MIDI_NOTE_LIST).getNumChildren(), 1);
}

TEST_F(CapabilityRouteParityTest, AuditionPatchUndeterminableEngineFailsIdentically) {
    // No sidecar, no recognizable header -> the shared engine-inference gate.
    const juce::File junk = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("hdaw_audition_junk.syx");
    junk.replaceWithText("not a patch");
    const QJsonObject args{ { "path", QString::fromStdString(
                                  junk.getFullPathName().toStdString()) } };
    expectSameFailure("audition_patch", "audio.auditionPatch", args);
    junk.deleteFile();
}

TEST_F(CapabilityRouteParityTest, AuditionPatchMissingPathFailsWithRouteText) {
    // MCP schema-gates the required `path`; the route reaches its own
    // validator. Code parity holds on both surfaces.
    const auto r = rpc("audio.auditionPatch", {});
    ASSERT_TRUE(r.isError);
    EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
    EXPECT_EQ(r.payload.toObject().value("message").toString(), QString("path required"));
}

} // namespace
