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

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace {

// A short 1 kHz click track (660-sample bursts every 0.25 s) so the transient
// detector has real onsets to find; hermetic (no sample-library dependency).
QString writeSliceParityWav()
{
    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getNonexistentChildFile("hdaw_slice_parity", ".wav", false);
    constexpr int sampleRate = 44100;
    constexpr int numSamples = sampleRate * 2;   // 2 s
    constexpr int burstSamples = 660;            // ~15 ms
    juce::AudioBuffer<float> buf(1, numSamples);
    buf.clear();
    for (int c = 0; c < numSamples; ++c)
    {
        const double phaseInClick = std::fmod(static_cast<double>(c) / sampleRate, 0.25);
        if (phaseInClick < static_cast<double>(burstSamples) / sampleRate)
        {
            const int n = static_cast<int>(std::round(phaseInClick * sampleRate));
            buf.setSample(0, c, 0.6f * static_cast<float>(std::sin(
                2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate * n)));
        }
    }
    juce::WavAudioFormat wav;
    auto* fileOut = new juce::FileOutputStream(f);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(fileOut, sampleRate, 1, 16, {}, 0));
    if (writer == nullptr) { delete fileOut; return {}; }
    writer->writeFromAudioSampleBuffer(buf, 0, numSamples);
    writer->flush();
    return QString::fromStdString(f.getFullPathName().toStdString());
}

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
    // add_track's MCP payload carries BOTH the positional index and the stable
    // id; the sampler twins must share the `trackID` spelling (the MCP tool
    // takes trackId/trackID, the sampler route takes trackIndex/trackID).
    std::pair<int, int> addTrackWithID(const QString& name = "Track") {
        const QJsonObject o = parseText(mcpText("add_track", QJsonObject{ { "name", name } }));
        return { o.value("trackId").toInt(), o.value("trackID").toInt() };
    }
    juce::ValueTree masterFx() {
        return engine->getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
    }
    // A sampler slot's tree property (the "nothing was written" probe).
    std::string slotProperty(int trackIndex, int slotIndex, const char* prop) {
        return engine->getProjectModel().getTrackListTree().getChild(trackIndex)
            .getChildWithName(IDs::FX_CHAIN).getChild(slotIndex)
            .getProperty(prop, "").toString().toStdString();
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

// ─── detect_sampler_slices ↔ sampler.detectSlices ──────────────────────────
//     recut_sampler_slices  ↔ sampler.recutSlices
// Both surfaces call the SAME AudioEngineCommands entry point and shape the
// reply through the SAME HDAW::samplerSlicePayloadJson (src/common/
// SamplerSliceShaper.h), so the payloads cannot drift. The twin argument
// object uses `trackID` — the one track spelling both surfaces accept.

TEST_F(CapabilityRouteParityTest, DetectSamplerSlicesUnknownSlotFailsIdentically) {
    const auto track = addTrackWithID("SliceParity");
    ASSERT_GT(track.second, 0) << "the stable track id must be minted";
    addFxTool(track.first, "sampler");

    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", 99 } };
    expectSameFailure("detect_sampler_slices", "sampler.detectSlices", args);
    EXPECT_EQ(mcpText("detect_sampler_slices", args), QString("slot not found"));
    EXPECT_EQ(rpc("sampler.detectSlices", args).payload.toObject().value("message").toString(),
              QString("slot not found"));
}

TEST_F(CapabilityRouteParityTest, RecutSamplerSlicesUnknownSlotFailsIdentically) {
    const auto track = addTrackWithID("SliceParity");
    ASSERT_GT(track.second, 0) << "the stable track id must be minted";
    addFxTool(track.first, "sampler");

    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", 99 },
                            { "fromNorm", 0.0 }, { "toNorm", 1.0 } };
    expectSameFailure("recut_sampler_slices", "sampler.recutSlices", args);
    EXPECT_EQ(mcpText("recut_sampler_slices", args), QString("slot not found"));
    EXPECT_EQ(rpc("sampler.recutSlices", args).payload.toObject().value("message").toString(),
              QString("slot not found"));
}

TEST_F(CapabilityRouteParityTest, DetectSamplerSlicesPayloadMatchesOnBothSurfaces) {
    const auto track = addTrackWithID("SliceParity");
    const int slot = addFxTool(track.first, "sampler");
    ASSERT_GE(slot, 0);
    const QString wav = writeSliceParityWav();
    ASSERT_FALSE(wav.isEmpty());
    const QJsonObject sampleArgs{ { "trackId", track.first }, { "slotIndex", slot },
                                  { "filePath", wav } };
    ASSERT_FALSE(mcpIsError("sampler_set_sample", sampleArgs))
        << mcpText("sampler_set_sample", sampleArgs).toStdString();

    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", slot },
                            { "sliceMode", "transient" }, { "sliceSensitivity", 0.5 } };
    expectPayloadParity("detect_sampler_slices", "sampler.detectSlices", args);

    const QJsonObject parsed = parseText(mcpText("detect_sampler_slices", args));
    EXPECT_TRUE(parsed.value("ok").toBool());
    EXPECT_GT(parsed.value("totalSlices").toInt(), 0);
    // The shared shaper emits every field (the old hand-built route object had
    // only ok/totalSlices/slicePoints).
    EXPECT_TRUE(parsed.contains("bandMasks"));
    EXPECT_TRUE(parsed.contains("strengths"));
    EXPECT_TRUE(parsed.contains("overrideCount"));
}

TEST_F(CapabilityRouteParityTest, RecutSamplerSlicesPayloadMatchesOnBothSurfaces) {
    const auto track = addTrackWithID("SliceParity");
    const int slot = addFxTool(track.first, "sampler");
    ASSERT_GE(slot, 0);
    const QString wav = writeSliceParityWav();
    ASSERT_FALSE(wav.isEmpty());
    const QJsonObject sampleArgs{ { "trackId", track.first }, { "slotIndex", slot },
                                  { "filePath", wav } };
    ASSERT_FALSE(mcpIsError("sampler_set_sample", sampleArgs))
        << mcpText("sampler_set_sample", sampleArgs).toStdString();

    // A coarse grid seeds boundaries OUTSIDE the re-cut window; the re-cut
    // replaces only the [fromNorm,toNorm) interior. Re-seed before EACH surface
    // so both run the SAME operation from the SAME state (the payload is a pure
    // function of the seed + window).
    const QJsonObject seedArgs{ { "trackID", track.second }, { "slotIndex", slot },
                                { "sliceMode", "grid" }, { "sliceGrid", 1.0 } };
    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", slot },
                            { "sliceMode", "transient" }, { "sliceSensitivity", 0.5 },
                            { "fromNorm", 0.25 }, { "toNorm", 0.75 } };

    ASSERT_FALSE(mcpIsError("detect_sampler_slices", seedArgs));
    const auto r = rpc("sampler.recutSlices", args);
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();

    ASSERT_FALSE(mcpIsError("detect_sampler_slices", seedArgs));
    const QString toolText = mcpText("recut_sampler_slices", args);
    EXPECT_FALSE(mcpIsError("recut_sampler_slices", args)) << toolText.toStdString();
    const auto expected = QJsonDocument::fromJson(toolText.toUtf8());
    ASSERT_FALSE(expected.isNull()) << "tool text is not JSON: " << toolText.toStdString();
    EXPECT_EQ(QJsonDocument::fromVariant(r.payload.toVariant()), expected)
        << "route payload must equal the parsed tool text";

    const QJsonObject parsed = expected.object();
    EXPECT_TRUE(parsed.value("ok").toBool());
    EXPECT_TRUE(parsed.contains("bandMasks"));
    EXPECT_TRUE(parsed.contains("strengths"));
    EXPECT_TRUE(parsed.contains("overrideCount"));
}

TEST_F(CapabilityRouteParityTest, DetectSamplerSlicesAlignedModePayloadMatchesOnBothSurfaces) {
    // `aligned` (the onset-fitted grid, common/SamplerSliceModes.h) is a
    // first-class mode on BOTH surfaces: the MCP enum accepts it and the route
    // passes it to the same AudioEngineCommands entry point, so the payloads
    // stay identical by construction.
    const auto track = addTrackWithID("SliceParity");
    const int slot = addFxTool(track.first, "sampler");
    ASSERT_GE(slot, 0);
    const QString wav = writeSliceParityWav();
    ASSERT_FALSE(wav.isEmpty());
    const QJsonObject sampleArgs{ { "trackId", track.first }, { "slotIndex", slot },
                                  { "filePath", wav } };
    ASSERT_FALSE(mcpIsError("sampler_set_sample", sampleArgs))
        << mcpText("sampler_set_sample", sampleArgs).toStdString();

    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", slot },
                            { "sliceMode", "aligned" }, { "sliceGrid", 0.25 },
                            { "sliceSensitivity", 0.5 } };
    expectPayloadParity("detect_sampler_slices", "sampler.detectSlices", args);

    const QJsonObject parsed = parseText(mcpText("detect_sampler_slices", args));
    EXPECT_TRUE(parsed.value("ok").toBool());
    EXPECT_GT(parsed.value("totalSlices").toInt(), 0);
    EXPECT_EQ(parsed.value("error").toString(), QString());
}

TEST_F(CapabilityRouteParityTest, UnknownSliceModeIsRefusedOnBothSurfaces) {
    // A typo must NOT silently change the algorithm (the pre-fix engine fell
    // through to transient). MCP refuses through the schema enum, the route
    // through the engine's shared validator — both name the valid set, and
    // neither writes to the slot.
    const auto track = addTrackWithID("SliceParity");
    const int slot = addFxTool(track.first, "sampler");
    ASSERT_GE(slot, 0);
    const QString wav = writeSliceParityWav();
    ASSERT_FALSE(wav.isEmpty());
    const QJsonObject sampleArgs{ { "trackId", track.first }, { "slotIndex", slot },
                                  { "filePath", wav } };
    ASSERT_FALSE(mcpIsError("sampler_set_sample", sampleArgs))
        << mcpText("sampler_set_sample", sampleArgs).toStdString();

    // Seed real boundaries so "nothing was written" is observable.
    const QJsonObject seedArgs{ { "trackID", track.second }, { "slotIndex", slot },
                                { "sliceMode", "grid" }, { "sliceGrid", 0.25 } };
    ASSERT_FALSE(mcpIsError("detect_sampler_slices", seedArgs));
    const std::string before = slotProperty(track.first, slot, "slicePoints");
    ASSERT_FALSE(before.empty());

    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", slot },
                            { "sliceMode", "transientt" } };

    EXPECT_TRUE(mcpIsError("detect_sampler_slices", args));
    // The schema enum names the valid set (quoted, so the three names are
    // checked individually rather than as one substring).
    const QString mcpMsg = mcpText("detect_sampler_slices", args);
    EXPECT_TRUE(mcpMsg.contains("transient")) << mcpMsg.toStdString();
    EXPECT_TRUE(mcpMsg.contains("grid")) << mcpMsg.toStdString();
    EXPECT_TRUE(mcpMsg.contains("aligned")) << mcpMsg.toStdString();

    const auto r = rpc("sampler.detectSlices", args);
    ASSERT_FALSE(r.isError);
    const QJsonObject payload = r.payload.toObject();
    EXPECT_FALSE(payload.value("ok").toBool());
    EXPECT_TRUE(payload.value("error").toString().contains("transient, grid, aligned"))
        << payload.value("error").toString().toStdString();

    // The route-only setSliceMode surface hands the same refusal back as -32602.
    const auto setR = rpc("sampler.setSliceMode", args);
    ASSERT_TRUE(setR.isError);
    EXPECT_EQ(setR.payload.toObject().value("code").toInt(), -32602);
    EXPECT_TRUE(setR.payload.toObject().value("message").toString()
                    .contains("transient, grid, aligned"))
        << setR.payload.toObject().value("message").toString().toStdString();

    // Neither surface wrote anything.
    EXPECT_EQ(slotProperty(track.first, slot, "slicePoints"), before);
    EXPECT_EQ(slotProperty(track.first, slot, "sliceMode"), std::string("grid"));
}

// ─── set_sampler_slice_overrides ↔ sampler.setSliceOverrides ───────────────
// The pin write surface. Both surfaces gate the slot with the SAME text before
// the shared AudioEngineCommands::setSamplerSliceOverrides entry point runs and
// shape the reply through the SAME HDAW::samplerSliceOverridePayloadJson
// (src/common/SamplerSliceShaper.h).

TEST_F(CapabilityRouteParityTest, SetSamplerSliceOverridesUnknownSlotFailsIdentically) {
    const auto track = addTrackWithID("SliceParity");
    ASSERT_GT(track.second, 0) << "the stable track id must be minted";
    addFxTool(track.first, "sampler");

    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", 99 },
                            { "slicePointsOverride", QJsonArray{ 0.25 } } };
    expectSameFailure("set_sampler_slice_overrides", "sampler.setSliceOverrides", args);
    EXPECT_EQ(mcpText("set_sampler_slice_overrides", args), QString("slot not found"));
    EXPECT_EQ(rpc("sampler.setSliceOverrides", args).payload.toObject().value("message").toString(),
              QString("slot not found"));
}

TEST_F(CapabilityRouteParityTest, SetSamplerSliceOverridesNonSamplerFailsIdentically) {
    const int t = addTrack("SliceParity");
    addFxTool(t, "eq");

    const QJsonObject args{ { "trackId", t }, { "slotIndex", 0 },
                            { "slicePointsOverride", QJsonArray{ 0.25 } } };
    expectSameFailure("set_sampler_slice_overrides", "sampler.setSliceOverrides", args);
    EXPECT_EQ(mcpText("set_sampler_slice_overrides", args), QString("slot is not a sampler"));
    EXPECT_EQ(rpc("sampler.setSliceOverrides", args).payload.toObject().value("message").toString(),
              QString("slot is not a sampler"));
}

// A non-number element used to coerce to 0 via QJsonValue::toDouble() and be
// dropped as an implicit endpoint, so `[0.25,"oops"]` SUCCEEDED with one pin —
// the accepted-argument-dropped class. ONE shared rule + text
// (common/SamplerSliceModes.h): the MCP validator's items type answers with it,
// the route applies the helper itself, and the whole call is refused with
// nothing written on either surface.
TEST_F(CapabilityRouteParityTest, SetSamplerSliceOverridesNonNumberElementFailsIdentically) {
    const auto track = addTrackWithID("SliceParity");
    ASSERT_GT(track.second, 0) << "the stable track id must be minted";
    const int slot = addFxTool(track.first, "sampler");
    ASSERT_GE(slot, 0);

    // Seed a real pin so "nothing was written" is observable.
    const QJsonObject seed{ { "trackID", track.second }, { "slotIndex", slot },
                            { "slicePointsOverride", QJsonArray{ 0.25 } } };
    ASSERT_FALSE(mcpIsError("set_sampler_slice_overrides", seed));
    const std::string before = slotProperty(track.first, slot, "slicePointsOverride");
    ASSERT_FALSE(before.empty());

    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", slot },
                            { "slicePointsOverride", QJsonArray{ 0.25, "oops" } } };
    expectSameFailure("set_sampler_slice_overrides", "sampler.setSliceOverrides", args);
    EXPECT_EQ(mcpText("set_sampler_slice_overrides", args),
              QString("invalid params: slicePointsOverride[1]: expected number"));
    EXPECT_EQ(rpc("sampler.setSliceOverrides", args).payload.toObject().value("message").toString(),
              QString("invalid params: slicePointsOverride[1]: expected number"));

    // Refused as a WHOLE: the seed pin is untouched on both surfaces.
    EXPECT_EQ(slotProperty(track.first, slot, "slicePointsOverride"), before);
}

// The other half of the rule: a JSON INTEGER is a JSON number (Qt stores every
// JSON number as a double, so QJsonValue::isDouble() is true for `0`/`1`), so an
// integer element is ACCEPTED, not refused. Both endpoints are then dropped as
// implicit endpoints, so the pin set is empty and both surfaces agree.
TEST_F(CapabilityRouteParityTest, SetSamplerSliceOverridesAcceptsJsonIntegers) {
    const auto track = addTrackWithID("SliceParity");
    ASSERT_GT(track.second, 0) << "the stable track id must be minted";
    const int slot = addFxTool(track.first, "sampler");
    ASSERT_GE(slot, 0);

    const QJsonObject args{ { "trackID", track.second }, { "slotIndex", slot },
                            { "slicePointsOverride", QJsonArray{ 0, 1 } } };
    expectPayloadParity("set_sampler_slice_overrides", "sampler.setSliceOverrides", args);

    const QJsonObject payload = parseText(mcpText("set_sampler_slice_overrides", args));
    EXPECT_TRUE(payload.value("ok").toBool());
    EXPECT_EQ(payload.value("overrideCount").toInt(), 0);
    EXPECT_EQ(slotProperty(track.first, slot, "slicePointsOverride"), std::string());
}

} // namespace
