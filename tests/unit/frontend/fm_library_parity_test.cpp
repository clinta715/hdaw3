// Twin tests for the 2026-09-24 ledger decisions 1+2 — the three tools that
// gained/changed a JSON-RPC route in one pass:
//   add_library            <-> library.add              (widen: accept "patch")
//   fm_synth_import_sysex  <-> audio.fm_synthImportSysex (persist via setFmPatch)
//   fm_synth_load_preset   <-> audio.fmSynthLoadPreset   (new route)
//
// AGENTS.md twin-test rule: BOTH surfaces must fail with -32602 and the SAME
// message text where both reach the shared code, and the route payload must
// equal the tool's text (parsed) for the JSON-payload routes. Shared entry
// points: HDAW::fmLoadPresetToolText (src/common/FmPatchLoad.h) and the
// fm-import loader behind runFmImportSysex (src/mcp/PresetRoute.h -> shared
// by both surfaces) -> ProjectCommands::setFmPatch / FileLibraryManager::
// addLibrary. Follows the harness idioms of add_fx_parity_test.cpp /
// missing_route_parity_test.cpp (fixture engine + McpServer, rpc(),
// mcpText(), mcpIsError(), expectSameFailure).
//
// Adversarial: the persistence assertion (fmPatchData on the slot tree +
// LIVE engine patch bytes) is RED on the pre-fix tree, where the route went
// live-only and rejected raw VMEM banks.
//
// Determinism (lesson 9): createDefaultProject() ships ZERO tracks, so the
// track each fm case needs is created here; the sysex fixtures are built in
// temp files (dx7_sysex_import_test.cpp builder pattern) or copied from
// tests/unit/engine/testdata/dx7 resolved relative to this source file
// (fm_patch_offline_export_test.cpp precedent).

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "engine/FmSynthEngine.h"
#include "engine/Dx7SysexImport.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

class FmLibraryParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    // --- MCP surface (missing_route_parity_test harness shape) -------------
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

    // A failing pair: both surfaces must report -32602 and the same text.
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

    // The MCP server validates a tool's JSON schema (required properties)
    // BEFORE the handler runs, so schema-invalid args answer "invalid params"
    // there while the route reaches the shared gate. Both refuse with -32602;
    // text equality is asserted only where both reach the shared code.
    void expectBothReject(const QString& tool, const QString& method,
                          const QJsonObject& args) {
        const auto r = rpc(method, args);
        ASSERT_TRUE(r.isError) << "expected " << method.toStdString() << " to fail";
        EXPECT_TRUE(mcpIsError(tool, args)) << "expected " << tool.toStdString() << " to fail";
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
        EXPECT_FALSE(mcpText(tool, args).isEmpty());
    }

    static QJsonObject parseText(const QString& text) {
        return QJsonDocument::fromJson(text.toUtf8()).object();
    }

    // --- scaffolding --------------------------------------------------------
    int addTrack(const QString& name = "Track") {
        const auto r = rpc("project.addTrack", QJsonObject{ { "name", name } });
        EXPECT_FALSE(r.isError);
        const int idx = r.payload.toInt();
        EXPECT_GE(idx, 0);
        return idx;
    }

    int addFmSlotViaRpc(int trackIndex) {
        const auto r = rpc("project.addFxSlot",
                           QJsonObject{ { "trackIndex", trackIndex },
                                        { "fxType", "fm_synth" } });
        EXPECT_FALSE(r.isError);
        // project.addFxSlot answers Null (the MCP add_fx twin answers "slot=N"),
        // so the slot index comes from the tree, not the payload.
        EXPECT_TRUE(r.payload.isNull() || r.payload.toString().isEmpty());
        const int slot = engine->getProjectModel().getTrackListTree()
                             .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN)
                             .getNumChildren() - 1;
        EXPECT_GE(slot, 0);
        // Drain the coalesced routing rebuild so live-processor reads below
        // are deterministic (lesson 10/12 discipline; audio_pool_dedup_test
        // pattern).
        engine->drainPendingRoutingRebuild();
        return slot;
    }

    juce::ValueTree fmSlotTree(int trackIndex, int slotIndex) {
        return engine->getProjectModel().getTrackListTree()
            .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN)
            .getChild(slotIndex);
    }

    static std::vector<uint8_t> liveEnginePatch(AudioEngine& eng, int ti, int si) {
        std::vector<uint8_t> out;
        auto* proc = eng.getMainProcessor();
        if (!proc) return out;
        auto* track = proc->getTrack(ti);
        if (!track) return out;
        auto& chain = track->getFXChain();
        if (si < 0 || si >= static_cast<int>(chain.size()) || !chain[si]) return out;
        auto* fm = chain[si]->fmSynthEngine();
        if (!fm) return out;
        out.assign(fm->patchData(), fm->patchData() + FmSynthEngine::kPatchSize);
        return out;
    }

    // --- DX7 fixture builders (dx7_sysex_import_test.cpp pattern) -----------
    static void finalizeChecksum(std::vector<uint8_t>& syx, int firstData, int lastDataIncl,
                                 int checksumAt) {
        int sum = 0;
        for (int i = firstData; i <= lastDataIncl; ++i) sum += syx[i];
        syx[checksumAt] = static_cast<uint8_t>((~sum + 1) & 0x7F);
    }

    static std::vector<uint8_t> makeSingleVoiceSysex(const char* name, int algorithm,
                                                     int feedback) {
        std::vector<uint8_t> syx(163, 0);
        syx[0] = 0xF0; syx[1] = 0x43; syx[2] = 0x00; syx[3] = 0x00;
        syx[4] = 0x00; syx[5] = 0x9B;
        syx[6 + 134] = static_cast<uint8_t>(algorithm);
        syx[6 + 135] = static_cast<uint8_t>(feedback);
        for (int i = 0; i < 10 && name[i]; ++i)
            syx[6 + 145 + i] = static_cast<uint8_t>(name[i]);
        finalizeChecksum(syx, 6, 160, 161);
        syx[162] = 0xF7;
        return syx;
    }

    static std::vector<uint8_t> makeCartridgeSysex(const char* voice0Name, int algorithm) {
        std::vector<uint8_t> syx(4104, 0);
        syx[0] = 0xF0; syx[1] = 0x43; syx[2] = 0x00; syx[3] = 0x09;
        syx[4] = 0x20; syx[5] = 0x00;
        for (int i = 6; i < 4102; ++i)
            syx[i] = static_cast<uint8_t>(i & 0x7F);
        // Voice 0 in the packed VMEM layout: algorithm at packed[110],
        // feedback at packed[111] bit 1, name at packed[118..127].
        auto* v0 = syx.data() + 6;
        v0[110] = static_cast<uint8_t>(algorithm);
        v0[111] = 0x02; // feedback 1
        for (int i = 0; i < 10 && voice0Name[i]; ++i)
            v0[118 + i] = static_cast<uint8_t>(voice0Name[i]);
        finalizeChecksum(syx, 6, 4101, 4102);
        syx[4103] = 0xF7;
        return syx;
    }

    // Raw 4096-byte VMEM bank: NO F0 43 framing, no checksum. The tool and
    // (since the 2026-09-24 fix) the route must accept it. A trailing F7
    // (4097 bytes) is accepted too.
    static std::vector<uint8_t> makeRawVmemBank() {
        std::vector<uint8_t> bank(4096, 0);
        for (int i = 0; i < 4096; ++i)
            bank[i] = static_cast<uint8_t>(i & 0x7F);
        auto* v0 = bank.data();
        v0[110] = 9;               // algorithm
        v0[111] = 0x04;            // feedback 2
        // The name field is 10 bytes and the parser reports it VERBATIM (no
        // NUL-stripping), so pad "VMEMBASS" with spaces for a clean assert.
        const char* n = "VMEMBASS  ";
        for (int i = 0; i < 10 && n[i]; ++i) v0[118 + i] = static_cast<uint8_t>(n[i]);
        return bank;
    }

    juce::File writeTempSyx(const QString& stem, const std::vector<uint8_t>& bytes) {
        const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile(("hdaw_" + stem + ".syx").toStdString());
        f.replaceWithData(bytes.data(), static_cast<int>(bytes.size()));
        tempFiles_.push_back(f);
        return f;
    }

    void TearDown() override {
        for (const auto& f : tempFiles_) f.deleteFile();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::vector<juce::File> tempFiles_;
};

// ─── add_library <-> library.add ─────────────────────────────────────────────

// The decision-1 close: type="patch" must create a patch library via the
// route — the SAME FileLibraryManager::addLibrary call the tool makes — and
// both surfaces must see the identical library record afterwards.
TEST_F(FmLibraryParityTest, AddLibraryPatchTypeMatchesOnBothSurfaces) {
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("hdaw_parity_patchlib_a");
    dir.createDirectory();

    const QJsonObject args{ { "name", "Parity Patches" },
                            { "path", dir.getFullPathName().toStdString().c_str() },
                            { "type", "patch" } };

    const auto routeRes = rpc("library.add", args);
    ASSERT_FALSE(routeRes.isError)
        << routeRes.payload.toObject().value("message").toString().toStdString();
    const QString routeId = routeRes.payload.toObject().value("id").toString();
    EXPECT_EQ(routeId.size(), 12) << "addLibrary returns a 12-char id";

    // Tool run FIRST (against the same manager): the duplicate-path rule
    // returns the EXISTING id, so the tool's answer must equal the route's —
    // proving both surfaces ran the same call with the same shaping.
    const QString toolText = mcpText("add_library", args);
    const auto toolPayload = parseText(toolText);
    EXPECT_FALSE(mcpIsError("add_library", args));
    EXPECT_EQ(toolPayload.value("id").toString(), routeId);

    // The created library is visible with type "patch" on the route surface.
    const auto listRes = rpc("library.list", QJsonObject{});
    ASSERT_FALSE(listRes.isError);
    bool found = false;
    for (const auto& v : listRes.payload.toArray()) {
        const auto lib = v.toObject();
        if (lib.value("id").toString() == routeId) {
            EXPECT_EQ(lib.value("type").toString(), "patch");
            EXPECT_EQ(lib.value("name").toString(), "Parity Patches");
            found = true;
        }
    }
    EXPECT_TRUE(found) << "patch library missing from library.list";

    dir.deleteRecursively();
}

// An invalid type must fail with -32602 on both surfaces. The MCP tool's JSON
// schema ENUMS the type (midi|audio|patch), so a non-enum value is rejected by
// the schema layer with "invalid params: ..." BEFORE the handler runs, while
// the route reaches the manager gate and answers its own text — the measured
// schema-gate asymmetry (see the ledger note; same class as place_patterns).
// Code parity (-32602) + both-refuse is the contract here; text equality is
// asserted on the classes that reach the shared handler on BOTH surfaces.
TEST_F(FmLibraryParityTest, AddLibraryInvalidTypeFailsIdenticallyOnBothSurfaces) {
    expectBothReject("add_library", "library.add",
                     QJsonObject{ { "name", "Bogus" }, { "path", "Z:/nowhere" },
                                  { "type", "notatype" } });

    // Empty NAME/PATH pass the tool's schema (type string, no enum) and reach
    // the handler on BOTH surfaces -> byte-identical text.
    expectSameFailure("add_library", "library.add",
                      QJsonObject{ { "name", "" }, { "path", "Z:/nowhere" },
                                   { "type", "patch" } });
    expectSameFailure("add_library", "library.add",
                      QJsonObject{ { "name", "Bogus" }, { "path", "" },
                                   { "type", "patch" } });
    // Empty TYPE is rejected by the schema's enum on the MCP side before the
    // handler (the route reaches the manager gate) — schema-gate asymmetry,
    // code parity only.
    expectBothReject("add_library", "library.add",
                     QJsonObject{ { "name", "Bogus" }, { "path", "Z:/nowhere" },
                                  { "type", "" } });

    // Fully missing required args: the MCP schema rejects with "invalid
    // params" before the handler; the route reaches the shared gate. Code
    // parity (-32602) still holds on both surfaces.
    expectBothReject("add_library", "library.add", QJsonObject{});
}

// ─── fm_synth_import_sysex <-> audio.fm_synthImportSysex ────────────────────

// The decision-2 close: the route must PERSIST (parse + setFmPatch + live
// load), accept the same dumps as the tool (single voice, cartridge with
// voiceIndex, raw 4096/4097 VMEM banks), and answer the tool's parsed
// payload. RED on the pre-fix tree (live-only write, VMEM rejected, no
// fmPatchData).
TEST_F(FmLibraryParityTest, FmImportSysexRoutePersistsAndMatchesToolPayload) {
    const int t = addTrack("Fm");
    ASSERT_GE(t, 0);
    const int s = addFmSlotViaRpc(t);

    const auto syx = makeSingleVoiceSysex("E.PIANO   ", 5, 3);
    const auto f = writeTempSyx("parity_voice", syx);

    // MCP surface first (the reference implementation).
    const QJsonObject toolArgs{ { "trackId", t }, { "slotIndex", s },
                                { "filePath", f.getFullPathName().toStdString().c_str() } };
    const QString toolText = mcpText("fm_synth_import_sysex", toolArgs);
    EXPECT_FALSE(mcpIsError("fm_synth_import_sysex", toolArgs))
        << toolText.toStdString();
    const auto toolPayload = parseText(toolText);
    EXPECT_TRUE(toolPayload.value("ok").toBool());
    EXPECT_EQ(toolPayload.value("algorithm").toInt(), 5);
    // Name contract: trailing spaces trimmed, NUL padding stripped (same
    // normalization the engine's Dx7SysexImport applies on every path).
    EXPECT_EQ(toolPayload.value("voiceName").toString(), QString("E.PIANO"));

    // RPC surface: identical payload.
    const QJsonObject routeArgs{ { "trackId", t }, { "slotIndex", s },
                                 { "filePath", f.getFullPathName().toStdString().c_str() } };
    const auto routeRes = rpc("audio.fm_synthImportSysex", routeArgs);
    ASSERT_FALSE(routeRes.isError)
        << routeRes.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(routeRes.payload, QJsonValue(toolPayload));

    // PERSISTENCE (the fix): fmPatchData on the slot tree, written by BOTH
    // surfaces' path (same shared loader -> setFmPatch). A tree-copy render
    // and save/load read exactly this property.
    const auto treeB64 = fmSlotTree(t, s).getProperty(IDs::fmPatchData, "").toString();
    EXPECT_FALSE(treeB64.isEmpty()) << "fmPatchData missing from the slot tree";

    // LIVE load also happened (setFmPatch's best-effort branch).
    const auto live = liveEnginePatch(*engine, t, s);
    ASSERT_EQ(live.size(), static_cast<size_t>(FmSynthEngine::kPatchSize));
    juce::MemoryBlock persisted;
    persisted.fromBase64Encoding(treeB64);
    ASSERT_EQ(persisted.getSize(), static_cast<size_t>(FmSynthEngine::kPatchSize));
    EXPECT_EQ(std::memcmp(live.data(), persisted.getData(), FmSynthEngine::kPatchSize), 0)
        << "live engine patch must equal the persisted fmPatchData";
}

TEST_F(FmLibraryParityTest, FmImportSysexCartridgeAndVmemParity) {
    const int t = addTrack("FmCart");
    ASSERT_GE(t, 0);
    const int s = addFmSlotViaRpc(t);

    // 32-voice cartridge: the route must report the same voice list the tool
    // reports (totalVoices/voices[]/voiceIndex).
    const auto cart = makeCartridgeSysex("BASS 1    ", 31);
    const auto cartFile = writeTempSyx("parity_cart", cart);

    QJsonObject toolArgs{ { "trackId", t }, { "slotIndex", s },
                          { "filePath", cartFile.getFullPathName().toStdString().c_str() } };
    const QString toolText = mcpText("fm_synth_import_sysex", toolArgs);
    ASSERT_FALSE(mcpIsError("fm_synth_import_sysex", toolArgs)) << toolText.toStdString();
    const auto toolPayload = parseText(toolText);
    EXPECT_EQ(toolPayload.value("totalVoices").toInt(), 32);
    EXPECT_EQ(toolPayload.value("voiceIndex").toInt(), 0);
    EXPECT_EQ(toolPayload.value("voices").toArray().size(), 32);
    EXPECT_EQ(toolPayload.value("voiceName").toString(), QString("BASS 1"));

    const auto routeRes = rpc("audio.fm_synthImportSysex",
                              QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                           { "filePath", cartFile.getFullPathName().toStdString().c_str() } });
    ASSERT_FALSE(routeRes.isError)
        << routeRes.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(routeRes.payload, QJsonValue(toolPayload));

    // voiceIndex selects the cartridge voice — same on both surfaces.
    QJsonObject v5Args = toolArgs; v5Args.insert("voiceIndex", 5);
    const auto v5Payload = parseText(mcpText("fm_synth_import_sysex", v5Args));
    EXPECT_EQ(v5Payload.value("voiceIndex").toInt(), 5);
    const auto v5Route = rpc("audio.fm_synthImportSysex",
                             QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                          { "filePath", cartFile.getFullPathName().toStdString().c_str() },
                                          { "voiceIndex", 5 } });
    ASSERT_FALSE(v5Route.isError);
    EXPECT_EQ(v5Route.payload, QJsonValue(v5Payload));

    // Raw 4096-byte VMEM bank (no framing): the tool accepts it, and since
    // the 2026-09-24 fix the route must too — previously a -32602
    // "not a recognized DX7 SysEx file" rejection (the parity gap).
    const auto vmem = makeRawVmemBank();
    const auto vmemFile = writeTempSyx("parity_vmem", vmem);
    QJsonObject vmemArgs{ { "trackId", t }, { "slotIndex", s },
                          { "filePath", vmemFile.getFullPathName().toStdString().c_str() } };
    const auto vmemPayload = parseText(mcpText("fm_synth_import_sysex", vmemArgs));
    ASSERT_FALSE(mcpIsError("fm_synth_import_sysex", vmemArgs))
        << "tool must accept raw VMEM banks";
    EXPECT_EQ(vmemPayload.value("voiceName").toString(), QString("VMEMBASS"));
    EXPECT_EQ(vmemPayload.value("algorithm").toInt(), 9);
    EXPECT_EQ(vmemPayload.value("totalVoices").toInt(), 32);

    const auto vmemRoute = rpc("audio.fm_synthImportSysex",
                               QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                            { "filePath", vmemFile.getFullPathName().toStdString().c_str() } });
    ASSERT_FALSE(vmemRoute.isError)
        << "route must accept raw VMEM banks like the tool: "
        << vmemRoute.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(vmemRoute.payload, QJsonValue(vmemPayload));

    // VMEM import persisted too (tree + live agree).
    const auto vmemB64 = fmSlotTree(t, s).getProperty(IDs::fmPatchData, "").toString();
    EXPECT_FALSE(vmemB64.isEmpty());
    const auto live = liveEnginePatch(*engine, t, s);
    juce::MemoryBlock persisted;
    persisted.fromBase64Encoding(vmemB64);
    ASSERT_EQ(persisted.getSize(), static_cast<size_t>(FmSynthEngine::kPatchSize));
    EXPECT_EQ(std::memcmp(live.data(), persisted.getData(), FmSynthEngine::kPatchSize), 0);
}

// Shared gate parity: every failure the tool raises must fire identically on
// the route — missing file, wrong-type slot, bad dump, out-of-range slot.
TEST_F(FmLibraryParityTest, FmImportSysexFailuresMatchOnBothSurfaces) {
    const int t = addTrack("FmFail");
    ASSERT_GE(t, 0);
    const int s = addFmSlotViaRpc(t);

    const auto syx = makeSingleVoiceSysex("OK", 5, 3);
    const auto f = writeTempSyx("parity_fail_voice", syx);
    const auto pathStd = f.getFullPathName().toStdString();

    // Missing file.
    expectSameFailure("fm_synth_import_sysex", "audio.fm_synthImportSysex",
                      QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                   { "filePath", "Z:/definitely/missing.syx" } });

    // Not a recognized dump (deterministic bytes: no F0 43 header, wrong size).
    const auto junk = writeTempSyx("parity_fail_junk",
                                   std::vector<uint8_t>{ 1, 2, 3, 4, 5, 6, 7, 8 });
    expectSameFailure("fm_synth_import_sysex", "audio.fm_synthImportSysex",
                      QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                   { "filePath", junk.getFullPathName().toStdString().c_str() } });

    // Bad checksum inside an otherwise valid single-voice dump.
    auto corrupt = syx;
    corrupt[10] ^= 0x01;
    const auto corruptFile = writeTempSyx("parity_fail_badsum", corrupt);
    expectSameFailure("fm_synth_import_sysex", "audio.fm_synthImportSysex",
                      QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                   { "filePath", corruptFile.getFullPathName().toStdString().c_str() } });

    // Wrong slot type (eq slot on a second track).
    ASSERT_GE(addTrack("EqFail"), 0);
    ASSERT_FALSE(rpc("project.addFxSlot",
                     QJsonObject{ { "trackIndex", t + 1 }, { "fxType", "eq" } }).isError);
    engine->drainPendingRoutingRebuild();
    expectSameFailure("fm_synth_import_sysex", "audio.fm_synthImportSysex",
                      QJsonObject{ { "trackId", t + 1 }, { "slotIndex", 0 },
                                   { "filePath", pathStd.c_str() } });

    // Out-of-range slot index.
    expectSameFailure("fm_synth_import_sysex", "audio.fm_synthImportSysex",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 99 },
                                   { "filePath", pathStd.c_str() } });

    // Schema-gated class: missing required keys are rejected MCP-side with
    // "invalid params"; the route reaches the shared gate. Both -32602.
    expectBothReject("fm_synth_import_sysex", "audio.fm_synthImportSysex",
                     QJsonObject{ { "trackIndex", t } });
}

// The tool's own surface is unchanged by the shared-loader move: a valid
// single-voice import still lands fmPatchData + live patch via the MCP path
// alone.
TEST_F(FmLibraryParityTest, FmImportSysexToolPathAlonePersists) {
    const int t = addTrack("FmTool");
    ASSERT_GE(t, 0);
    const int s = addFmSlotViaRpc(t);

    const auto syx = makeSingleVoiceSysex("TOOLOK 1  ", 7, 2);
    const auto f = writeTempSyx("parity_tool_voice", syx);
    const auto toolRes = mcpText("fm_synth_import_sysex",
                                 QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                              { "filePath", f.getFullPathName().toStdString().c_str() } });
    EXPECT_FALSE(parseText(toolRes).isEmpty());

    const auto b64 = fmSlotTree(t, s).getProperty(IDs::fmPatchData, "").toString();
    EXPECT_FALSE(b64.isEmpty()) << "tool-only run must still persist fmPatchData";
    const auto live = liveEnginePatch(*engine, t, s);
    juce::MemoryBlock persisted;
    persisted.fromBase64Encoding(b64);
    ASSERT_EQ(persisted.getSize(), static_cast<size_t>(FmSynthEngine::kPatchSize));
    EXPECT_EQ(std::memcmp(live.data(), persisted.getData(), FmSynthEngine::kPatchSize), 0);
}

// ─── fm_synth_load_preset <-> audio.fmSynthLoadPreset ────────────────────────

// The decision-3 close: a raw patch (312-char hex) can be written over RPC
// through the SAME shared loader as the tool; "ok" on both, fmPatchData on
// the tree, live engine bytes equal. RED on the pre-wave tree (no route).
TEST_F(FmLibraryParityTest, FmLoadPresetRouteMatchesToolAndPersists) {
    const int t = addTrack("FmLoad");
    ASSERT_GE(t, 0);
    const int s = addFmSlotViaRpc(t);

    // 156-byte patch: algorithm 5, feedback 3, name "RAWHEX 1".
    uint8_t patch[FmSynthEngine::kPatchSize] = {};
    patch[134] = 5;
    patch[135] = 3;
    std::memcpy(patch + 145, "RAWHEX 1", 8);
    QString hex;
    for (int b : patch)
        hex += QString("%1").arg(b, 2, 16, QChar('0'));

    const QJsonObject args{ { "trackId", t }, { "slotIndex", s },
                            { "patchData", hex } };

    // Tool run first (trackId key), then the route — identical text, then
    // identical effects from the SAME shared entry point.
    const QJsonObject toolArgs{ { "trackId", t }, { "slotIndex", s },
                                { "patchData", hex } };
    const QString toolText = mcpText("fm_synth_load_preset", toolArgs);
    EXPECT_FALSE(mcpIsError("fm_synth_load_preset", toolArgs))
        << toolText.toStdString();
    EXPECT_EQ(toolText, QString("ok"));

    const auto routeRes = rpc("audio.fmSynthLoadPreset", args);
    ASSERT_FALSE(routeRes.isError)
        << routeRes.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(routeRes.payload.toString(), QString("ok"));

    // The tool's write and the route's write produce byte-identical tree
    // state (both ran setFmPatch with the same patch bytes; last write wins).
    const auto b64 = fmSlotTree(t, s).getProperty(IDs::fmPatchData, "").toString();
    EXPECT_FALSE(b64.isEmpty());
    juce::MemoryBlock persisted;
    persisted.fromBase64Encoding(b64);
    ASSERT_EQ(persisted.getSize(), static_cast<size_t>(FmSynthEngine::kPatchSize));
    EXPECT_EQ(std::memcmp(persisted.getData(), patch, FmSynthEngine::kPatchSize), 0)
        << "fmPatchData must equal the requested patch bytes";

    const auto live = liveEnginePatch(*engine, t, s);
    ASSERT_EQ(live.size(), static_cast<size_t>(FmSynthEngine::kPatchSize));
    EXPECT_EQ(std::memcmp(live.data(), patch, FmSynthEngine::kPatchSize), 0)
        << "live engine must carry the loaded patch";
}

// Failure parity for the new route: bad hex length, invalid hex digits,
// wrong slot type, out-of-range slot — all byte-identical -32602 texts.
TEST_F(FmLibraryParityTest, FmLoadPresetFailuresMatchOnBothSurfaces) {
    const int t = addTrack("FmLoadFail");
    ASSERT_GE(t, 0);
    const int s = addFmSlotViaRpc(t);

    expectSameFailure("fm_synth_load_preset", "audio.fmSynthLoadPreset",
                      QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                   { "patchData", QString(312, 'g') } });

    expectSameFailure("fm_synth_load_preset", "audio.fmSynthLoadPreset",
                      QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                   { "patchData", QString("abcd") } });

    // Wrong slot type (eq slot on a second track). NOTE: project.addFxSlot
    // (unlike MCP add_fx) takes `trackIndex` and answers Null.
    ASSERT_GE(addTrack("EqLoadFail"), 0);
    ASSERT_FALSE(rpc("project.addFxSlot",
                     QJsonObject{ { "trackIndex", t + 1 }, { "fxType", "eq" } }).isError);
    engine->drainPendingRoutingRebuild();
    expectSameFailure("fm_synth_load_preset", "audio.fmSynthLoadPreset",
                      QJsonObject{ { "trackId", t + 1 }, { "slotIndex", 0 },
                                   { "patchData", QString(312, 'a') } });

    // Out-of-range slot.
    expectSameFailure("fm_synth_load_preset", "audio.fmSynthLoadPreset",
                      QJsonObject{ { "trackId", t }, { "slotIndex", 99 },
                                   { "patchData", QString(312, 'a') } });

    // Schema-gated class: patchData missing entirely.
    expectBothReject("fm_synth_load_preset", "audio.fmSynthLoadPreset",
                     QJsonObject{ { "trackId", t }, { "slotIndex", s } });
}

// The 4097-byte VMEM variant (trailing F7) is accepted by both surfaces.
TEST_F(FmLibraryParityTest, FmImportSysexAccepts4097ByteVmemBank) {
    const int t = addTrack("FmVmem97");
    ASSERT_GE(t, 0);
    const int s = addFmSlotViaRpc(t);

    auto bank = makeRawVmemBank();
    bank.push_back(0xF7);
    ASSERT_EQ(bank.size(), static_cast<size_t>(4097));
    const auto f = writeTempSyx("parity_vmem97", bank);
    const QJsonObject toolArgs{ { "trackId", t }, { "slotIndex", s },
                                { "filePath", f.getFullPathName().toStdString().c_str() } };
    const auto payload = parseText(mcpText("fm_synth_import_sysex", toolArgs));
    ASSERT_FALSE(mcpIsError("fm_synth_import_sysex", toolArgs));
    EXPECT_EQ(payload.value("voiceName").toString(), QString("VMEMBASS"));

    const auto routeRes = rpc("audio.fm_synthImportSysex",
                              QJsonObject{ { "trackId", t }, { "slotIndex", s },
                                           { "filePath", f.getFullPathName().toStdString().c_str() } });
    ASSERT_FALSE(routeRes.isError);
    EXPECT_EQ(routeRes.payload, QJsonValue(payload));
}

} // namespace
