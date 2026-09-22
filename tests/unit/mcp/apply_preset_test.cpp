// apply_preset MCP tool (docs/plans/2026-09-18-apply-preset-mcp-dispatch.md):
// one thin dispatcher that routes by slot target + file header onto the same
// loaders as the five individual preset tools (which stay registered).
//
// Layers tested:
//   1. resolvePresetRoute() pure dispatch table — every dispatch row, no engine.
//   2. Tool-level loopback through the REAL registered tool with a live
//      (deviceless) AudioEngine: internal-engine routes assert the real
//      underlying command effect (fmPatchData / loadVirusPatch result JSON);
//      sendFxMidi routes assert the loader-specific "queued ..." result text
//      (queueing itself is safe without a live plugin instance).
//   3. Backwards compat: the five individual tools remain registered.
#include <gtest/gtest.h>

#include "common/NordBankLoader.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "common/ProjectCommands.h"
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTools_Private.h"
#include "mcp/PresetRoute.h"
#include "model/ProjectModel.h"

#include <string>
#include <vector>

namespace {

QJsonObject callTool(mcp::McpServer& s, int id, const char* name,
                     const QJsonObject& args)
{
    auto r = s.handleRequestOnTestThread(id, "tools/call",
        QJsonObject{{"name", name}, {"arguments", args}});
    return r.toObject();
}

// handleRequestOnTestThread returns the BARE tool result object
// ({content:[...]}, plus "isError" on tool errors) — not a JSON-RPC envelope.
QString resultText(const QJsonObject& r)
{
    return r.value("content").toArray().at(0).toObject()
        .value("text").toString();
}

bool resultIsError(const QJsonObject& r)
{
    return r.value("isError").toBool(false);
}

QJsonObject argsOf(const QJsonObject& r) // convenience: parse a text payload as JSON
{
    return QJsonDocument::fromJson(resultText(r).toUtf8()).object();
}

// Fixture resolution via __FILE__ so the test is independent of the runner's
// working directory (same pattern as track_fx_rebuild_race_test.cpp).
juce::File fixtureFile(const juce::String& relUnderTests)
{
    juce::File self(__FILE__);
    return juce::File::isAbsolutePath(__FILE__)
        ? self.getParentDirectory().getChildFile(relUnderTests)
        : juce::File::getCurrentWorkingDirectory().getChildFile(
              juce::String("tests/unit/mcp/") + relUnderTests);
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Pure dispatch table
// ---------------------------------------------------------------------------

TEST(ApplyPresetResolver, NordSyxIntoNodalRed2x)
{
    const std::vector<uint8_t> dump { 0xF0, 0x33, 0x0F, 0x04, 0x01, 0x00, 0xF7 };
    auto r = mcp::resolvePresetRoute("plugin", "NodalRed2x.clap",
        dump.data(), dump.size(), ".syx", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::NordBank);
    EXPECT_TRUE(r.error.isEmpty());
}

TEST(ApplyPresetResolver, NordMidIntoNodalRed2x)
{
    // .mid files start with "MThd" — routed by extension for the Nord slot.
    const std::vector<uint8_t> mid { 'M', 'T', 'h', 'd', 0, 0, 0, 6 };
    auto r = mcp::resolvePresetRoute("plugin", "NodalRed2x.clap",
        mid.data(), mid.size(), ".mid", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::NordBank);
}

TEST(ApplyPresetResolver, WaldorfSyxIntoXeniaAndVavra)
{
    const std::vector<uint8_t> xeniaDump { 0xF0, 0x3E, 0x0E, 0x7F, 0x10, 0x00, 0xF7 };
    auto xr = mcp::resolvePresetRoute("plugin", "Xenia.clap",
        xeniaDump.data(), xeniaDump.size(), ".syx", false);
    EXPECT_EQ(xr.kind, mcp::PresetRouteKind::WaldorfSysex);

    const std::vector<uint8_t> vavraDump { 0xF0, 0x3E, 0x10, 0x7F, 0x10, 0x00, 0xF7 };
    auto vr = mcp::resolvePresetRoute("plugin", "Vavra.clap",
        vavraDump.data(), vavraDump.size(), ".syx", false);
    EXPECT_EQ(vr.kind, mcp::PresetRouteKind::WaldorfSysex);

    auto wrongMachine = mcp::resolvePresetRoute("plugin", "Xenia.clap",
        vavraDump.data(), vavraDump.size(), ".syx", false);
    EXPECT_EQ(wrongMachine.kind, mcp::PresetRouteKind::None);
}

TEST(ApplyPresetResolver, VirusRomPresetNeedsNoFile)
{
    auto r = mcp::resolvePresetRoute("plugin", "OsTIrus.clap",
        nullptr, 0, {}, true);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::VirusRom);
    for (const char* id : { "Osirus", "Vavra", "Xenia", "JE8086" })
    {
        auto r2 = mcp::resolvePresetRoute("plugin", id, nullptr, 0, {}, true);
        EXPECT_EQ(r2.kind, mcp::PresetRouteKind::VirusRom) << id;
    }
}

TEST(ApplyPresetResolver, VirusRomWithoutProgramErrors)
{
    auto r = mcp::resolvePresetRoute("plugin", "Osirus.clap",
        nullptr, 0, {}, false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::None);
    EXPECT_TRUE(r.error.contains("program"));
}

TEST(ApplyPresetResolver, F043IntoPluginSlotSteersToFmSynth)
{
    // DX7 SysEx into a plugin slot has no reliable route (probed silent
    // 2026-09-14): None with an actionable error, still matching the
    // generic "cannot determine preset type" contract.
    const std::vector<uint8_t> dump { 0xF0, 0x43, 0x00, 0x09, 0x00, 0xF7 };
    auto r = mcp::resolvePresetRoute("plugin", "Dexed.clap",
        dump.data(), dump.size(), ".syx", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::None);
    EXPECT_TRUE(r.error.contains("cannot determine preset type"));
    EXPECT_TRUE(r.error.contains("fm_synth_import_sysex"));
}

TEST(ApplyPresetResolver, F043IntoInternalFmSynth)
{
    const std::vector<uint8_t> dump { 0xF0, 0x43, 0x00, 0x00, 0x00, 0xF7 };
    auto r = mcp::resolvePresetRoute("fm_synth", "",
        dump.data(), dump.size(), ".syx", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::FmSysex);
}

TEST(ApplyPresetResolver, VirusSyxIntoInternalSubSynth)
{
    const std::vector<uint8_t> dump { 0xF0, 0x00, 0x20, 0x33, 0x00, 0xF7 };
    auto r = mcp::resolvePresetRoute("sub_synth", "",
        dump.data(), dump.size(), ".syx", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::SubSynthVirus);
    // pluginId carrying sub_synth also matches (belt and braces).
    auto r2 = mcp::resolvePresetRoute("plugin", "sub_synth_helper",
        dump.data(), dump.size(), ".syx", false);
    EXPECT_EQ(r2.kind, mcp::PresetRouteKind::SubSynthVirus);
}

TEST(ApplyPresetResolver, XferJsonIntoAnyPluginSlot)
{
    const std::vector<uint8_t> serum { 'X', 'f', 'e', 'r', 'J', 's', 'o', 'n',
                                       0, 1, 2, 3, 0, 0, 0, 0, '{', '}' };
    auto r = mcp::resolvePresetRoute("plugin", "Serum.clap",
        serum.data(), serum.size(), ".serumpreset", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::PluginPresetFile);
}

TEST(ApplyPresetResolver, CcnKFxpIntoAnyPluginSlot)
{
    const std::vector<uint8_t> fxp { 0x43, 0x63, 0x6e, 0x4b, 0, 0, 0, 0,
                                     0x46, 0x50, 0x43, 0x68 };
    auto r = mcp::resolvePresetRoute("plugin", "AnyThing.clap",
        fxp.data(), fxp.size(), ".fxp", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::PluginPresetFile);
}

TEST(ApplyPresetResolver, NoMatchReportsCannotDetermine)
{
    // Wrong header for the slot: DX7 dump into an eq slot.
    const std::vector<uint8_t> dump { 0xF0, 0x43, 0x00, 0x00, 0x00, 0xF7 };
    auto r = mcp::resolvePresetRoute("eq", "",
        dump.data(), dump.size(), ".syx", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::None);
    EXPECT_TRUE(r.error.contains("cannot determine preset type"));
}

TEST(ApplyPresetResolver, F043IntoGenericPluginIsNotDexedRoute)
{
    // Per the dispatch table, F0 43 routes via the Dexed/DX7-engine key only;
    // a generic plugin slot has no file-based route for it.
    const std::vector<uint8_t> dump { 0xF0, 0x43, 0x00, 0x00, 0x00, 0xF7 };
    auto r = mcp::resolvePresetRoute("plugin", "SomeOther.clap",
        dump.data(), dump.size(), ".syx", false);
    EXPECT_EQ(r.kind, mcp::PresetRouteKind::None);
    EXPECT_TRUE(r.error.contains("cannot determine preset type"));
}

// ---------------------------------------------------------------------------
// 2. Tool-level loopback (real registered tool, live deviceless engine)
// ---------------------------------------------------------------------------

class ApplyPresetToolTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }
    void TearDown() override
    {
        server.reset();
        engine.reset();
    }
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

TEST_F(ApplyPresetToolTest, AllFiveToolsRegistered)
{
    // load_dexed_cartridge removed 2026-09-14 (probed silent): DX7 SysEx
    // into plugin slots changes nothing audible; use fm_synth_import_sysex.
    EXPECT_TRUE(server->tools().contains("apply_preset"));
    EXPECT_TRUE(server->tools().contains("load_nord_bank"));
    EXPECT_TRUE(server->tools().contains("load_virus_preset"));
    EXPECT_FALSE(server->tools().contains("load_dexed_cartridge"));
    EXPECT_TRUE(server->tools().contains("fm_synth_import_sysex"));
    EXPECT_TRUE(server->tools().contains("sub_synth_import_sysex"));
    EXPECT_TRUE(server->tools().contains("load_plugin_preset_file"));
}

// Retrofit backlog item 7: load_nord_bank was the last preset-loading MCP tool with no RPC route.
// The route (plugin.loadNordBank) delegates to the same HDAW::loadNordBankFile, so a bad path must
// fail with the same loader message on both surfaces — and neither may queue anything.
TEST_F(ApplyPresetToolTest, LoadNordBankRpcTwinSharesLoaderFailure)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{ "name", "NordHost" }})));
    // Same parameter NAMES as the MCP tool (trackId) — a renamed argument is not a parity twin.
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0 },
                            { "filePath", "Z:/definitely/missing.nl2x" } };

    const auto mcpRes = callTool(*server, 2, "load_nord_bank", args);
    EXPECT_TRUE(resultIsError(mcpRes)) << resultText(mcpRes).toStdString();
    const QString mcpMsg = resultText(mcpRes);

    const auto rpc = frontend::dispatch(*engine, "plugin.loadNordBank", args);
    ASSERT_TRUE(rpc.isError) << QJsonDocument(rpc.payload.toObject()).toJson().constData();
    const QString rpcMsg = rpc.payload.toObject().value("message").toString();
    EXPECT_FALSE(rpcMsg.isEmpty());
    EXPECT_TRUE(rpcMsg.contains(mcpMsg) || mcpMsg.contains(rpcMsg))
        << "both surfaces must report the loader's message; mcp='"
        << mcpMsg.toStdString() << "' rpc='" << rpcMsg.toStdString() << "'";

    // Nothing was queued into the (non-existent) slot by either attempt.
    const auto chain = engine->getProjectModel().getTrackListTree()
        .getChild(0).getChildWithName(IDs::FX_CHAIN);
    EXPECT_EQ(chain.getNumChildren(), 0);
}

TEST_F(ApplyPresetToolTest, FmSysexRouteWritesFmPatchData)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{"name", "Track"}})));
    ASSERT_EQ(resultText(callTool(*server, 2, "add_fx",
        QJsonObject{{"trackId", 0}, {"fxType", "fm_synth"}})), QString("slot=0"));

    auto fixture = fixtureFile("../engine/testdata/dx7/cartridge.syx");
    ASSERT_TRUE(fixture.existsAsFile()) << fixture.getFullPathName();

    auto r = callTool(*server, 3, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                    {"filePath", QString::fromStdString(
                        fixture.getFullPathName().toStdString())}});
    EXPECT_FALSE(resultIsError(r)) << resultText(r).toStdString();
    const auto parsed = argsOf(r);
    EXPECT_EQ(parsed.value("ok").toBool(false), true);
    EXPECT_TRUE(parsed.contains("voiceName"));

    // The real underlying command ran: fmPatchData is on the slot tree.
    const auto slot = engine->getProjectModel().getTrackListTree()
        .getChild(0).getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(slot.isValid());
    const auto b64 = slot.getProperty(IDs::fmPatchData, "").toString();
    EXPECT_FALSE(b64.isEmpty()) << "fmPatchData not written (dispatch missed setFmPatch)";
}

TEST_F(ApplyPresetToolTest, SubSynthRouteMapsVirusPatch)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{"name", "Track"}})));
    ASSERT_EQ(resultText(callTool(*server, 2, "add_fx",
        QJsonObject{{"trackId", 0}, {"fxType", "sub_synth"}})), QString("slot=0"));

    auto fixture = fixtureFile("../engine/testdata/virus/bcsingle.syx");
    ASSERT_TRUE(fixture.existsAsFile()) << fixture.getFullPathName();

    auto r = callTool(*server, 3, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                    {"filePath", QString::fromStdString(
                        fixture.getFullPathName().toStdString())}});
    EXPECT_FALSE(resultIsError(r)) << resultText(r).toStdString();
    const auto parsed = argsOf(r);
    EXPECT_EQ(parsed.value("ok").toBool(false), true);
    EXPECT_EQ(parsed.value("name").toString(), QString("~WELCOME"));
    EXPECT_EQ(parsed.value("mappedCount").toInt(), 24);
}

TEST_F(ApplyPresetToolTest, MismatchedFileHeaderErrors)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{"name", "Track"}})));
    ASSERT_EQ(resultText(callTool(*server, 2, "add_fx",
        QJsonObject{{"trackId", 0}, {"fxType", "fm_synth"}})), QString("slot=0"));

    // A Virus dump into an fm_synth slot matches no dispatch row.
    auto fixture = fixtureFile("../engine/testdata/virus/bcsingle.syx");
    ASSERT_TRUE(fixture.existsAsFile());
    auto r = callTool(*server, 3, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                    {"filePath", QString::fromStdString(
                        fixture.getFullPathName().toStdString())}});
    EXPECT_TRUE(resultIsError(r));
    EXPECT_TRUE(resultText(r).contains("cannot determine preset type"))
        << resultText(r).toStdString();
}

TEST_F(ApplyPresetToolTest, NoFileNoProgramErrors)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{"name", "Track"}})));
    ASSERT_EQ(resultText(callTool(*server, 2, "add_fx",
        QJsonObject{{"trackId", 0}, {"fxType", "fm_synth"}})), QString("slot=0"));
    auto r = callTool(*server, 3, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}});
    EXPECT_TRUE(resultIsError(r));
    EXPECT_TRUE(resultText(r).contains("cannot determine preset type"))
        << resultText(r).toStdString();
}

TEST_F(ApplyPresetToolTest, VirusRomRouteQueuesCc0AndProgramChange)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{"name", "Track"}})));
    ASSERT_EQ(resultText(callTool(*server, 2, "add_fx",
        QJsonObject{{"trackId", 0}, {"pluginId", "OsirusFake.clap"}})), QString("slot=0"));

    auto r = callTool(*server, 3, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                    {"bank", 2}, {"program", 40}});
    EXPECT_FALSE(resultIsError(r)) << resultText(r).toStdString();
    EXPECT_TRUE(resultText(r).contains("queued bank=2 program=40"))
        << resultText(r).toStdString();
}

// The dedupe (retrofit backlog item 4): ONE loader behind both nord paths — the MCP tools
// (load_nord_bank, apply_preset's NordRoute) and the matrix tool's loose .syx step route. This
// pins the SHARED core directly: what gets queued and the error CLASS the RPC surface maps, so a
// later edit to either adapter cannot silently change the queued batch.
TEST(NordBankLoaderTest, SharedLoaderQueuesAndClassifiesErrors) {
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();
    const int t = pc.addTrack("Nord");
    ASSERT_GE(t, 0);
    pc.addFxSlot(t, "plugin", 0, "NodalRed2xFake.clap");

    // Clavia dump: F0 33 <dev> 04 01 <spec> + 132 payload bytes + F7 = 139 bytes.
    const auto makeDump = [](uint8_t spec) {
        std::vector<uint8_t> d { 0xF0, 0x33, 0x0F, 0x04, 0x01, spec };
        for (int i = 0; i < 132; ++i) d.push_back(static_cast<uint8_t>(i % 128));
        d.push_back(0xF7);
        return d;
    };
    const auto dumpA = makeDump(0), dumpB = makeDump(1);
    std::vector<uint8_t> bank;
    bank.insert(bank.end(), dumpA.begin(), dumpA.end());
    bank.insert(bank.end(), dumpB.begin(), dumpB.end());
    const juce::File bankFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                    .getChildFile("hdaw_nord_loader_bank.syx");
    bankFile.replaceWithData(bank.data(), static_cast<int>(bank.size()));
    const QString bankPath = QString::fromStdString(bankFile.getFullPathName().toStdString());

    const auto ok = HDAW::loadNordBankFile(engine, t, 0, bankPath, -1, /*captureToTree=*/true);
    ASSERT_TRUE(ok.ok) << ok.error.toStdString();
    EXPECT_EQ(ok.queued, 3);            // 2 bank dumps + the trailing CC125
    EXPECT_EQ(ok.totalBytes, 2 * 139);  // 2 x 139 SysEx bytes
    EXPECT_EQ(ok.program, -1);
    EXPECT_TRUE(ok.error.isEmpty());

    // A missing file is an ENVIRONMENT failure (the RPC maps it to -32603).
    const auto missing = HDAW::loadNordBankFile(engine, t, 0,
                                                "Z:/definitely/missing.syx", -1, true);
    EXPECT_FALSE(missing.ok);
    EXPECT_TRUE(missing.environmentFailure);
    EXPECT_TRUE(missing.error.contains("file not found")) << missing.error.toStdString();

    // An unsupported container is an INVALID-PARAMS failure (-32602).
    const juce::File txt = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("hdaw_nord_loader_bad.txt");
    txt.replaceWithText("not sysex at all");
    const auto bad = HDAW::loadNordBankFile(engine, t, 0,
                                            QString::fromStdString(txt.getFullPathName().toStdString()),
                                            -1, true);
    EXPECT_FALSE(bad.ok);
    EXPECT_FALSE(bad.environmentFailure);
    EXPECT_TRUE(bad.error.contains("unsupported file type")) << bad.error.toStdString();
}

TEST_F(ApplyPresetToolTest, NordRouteValidatesDumpsBeforeQueueing)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{"name", "Track"}})));
    ASSERT_EQ(resultText(callTool(*server, 2, "add_fx",
        QJsonObject{{"trackId", 0}, {"pluginId", "NodalRed2xFake.clap"}})), QString("slot=0"));

    // Clavia header but structurally invalid (7 bytes < 8) -> the nord
    // loader's validation rejects BEFORE anything is queued (the loader
    // actually ran).
    const std::vector<uint8_t> badDump { 0xF0, 0x33, 0x0F, 0x04, 0x01, 0xF7 };
    const juce::File badFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("hdaw_apply_preset_bad.syx");
    badFile.replaceWithData(badDump.data(), static_cast<int>(badDump.size()));

    auto r = callTool(*server, 3, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                    {"filePath", QString::fromStdString(
                        badFile.getFullPathName().toStdString())}});
    EXPECT_TRUE(resultIsError(r));
    EXPECT_TRUE(resultText(r).contains("invalid Nord dump"))
        << resultText(r).toStdString();

    // Valid two-dump bank -> the nord loader queues (2 dumps + CC125).
    const auto makeDump = [](uint8_t spec) {
        std::vector<uint8_t> d { 0xF0, 0x33, 0x0F, 0x04, 0x01, spec };
        for (int i = 0; i < 132; ++i) d.push_back(static_cast<uint8_t>(i % 128));
        d.push_back(0xF7);
        return d;
    };
    const auto dumpA = makeDump(0), dumpB = makeDump(1);
    std::vector<uint8_t> bank;
    bank.insert(bank.end(), dumpA.begin(), dumpA.end());
    bank.insert(bank.end(), dumpB.begin(), dumpB.end());
    const juce::File bankFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("hdaw_apply_preset_bank.syx");
    bankFile.replaceWithData(bank.data(), static_cast<int>(bank.size()));

    auto r2 = callTool(*server, 4, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                    {"filePath", QString::fromStdString(
                        bankFile.getFullPathName().toStdString())}});
    EXPECT_FALSE(resultIsError(r2)) << resultText(r2).toStdString();
    // 2 bank dumps + the trailing CC125 = 3 queued events, 2x139 bytes.
    EXPECT_TRUE(resultText(r2).contains("queued 3 sysex dumps (278 bytes)"))
        << resultText(r2).toStdString();

    badFile.deleteFile();
    bankFile.deleteFile();
}

TEST_F(ApplyPresetToolTest, SerumPresetRouteReachesPluginLoader)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{"name", "Track"}})));
    ASSERT_EQ(resultText(callTool(*server, 2, "add_fx",
        QJsonObject{{"trackId", 0}, {"pluginId", "SerumFake.clap"}})), QString("slot=0"));

    // Minimal XferJson container (metadata len 2 "{}" then a binary payload).
    std::vector<uint8_t> serum { 'X', 'f', 'e', 'r', 'J', 's', 'o', 'n',
                                 0x00,
                                 0x02, 0x00, 0x00, 0x00,   // metadata length
                                 0x00, 0x00, 0x00, 0x00,   // reserved
                                 '{', '}',
                                 0xBA, 0xAD, 0xF0, 0x0D }; // binary payload
    const juce::File serumFile = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("hdaw_apply_preset.serumPreset");
    serumFile.replaceWithData(serum.data(), static_cast<int>(serum.size()));

    auto r = callTool(*server, 3, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                    {"filePath", QString::fromStdString(
                        serumFile.getFullPathName().toStdString())}});
    // The dispatch must reach the load_plugin_preset_file loader — the
    // result is the loader's own outcome (ok with a captured pluginState, or
    // its slot/instance error), never the dispatch error.
    EXPECT_FALSE(resultText(r).contains("cannot determine preset type"))
        << resultText(r).toStdString();
    if (resultIsError(r))
    {
        std::cout << "[ApplyPreset] plugin-preset route error: "
                  << resultText(r).toStdString() << std::endl;
    }
    else
    {
        // ok => setStateInformation ran on a live instance; the tree must
        // carry the captured state.
        const auto slotTree = engine->getProjectModel().getTrackListTree()
            .getChild(0).getChildWithName(IDs::FX_CHAIN).getChild(0);
        ASSERT_TRUE(slotTree.isValid());
        EXPECT_FALSE(slotTree.getProperty(IDs::pluginState, "").toString().isEmpty())
            << "pluginState not captured by the plugin-preset loader";
    }

    serumFile.deleteFile();
}

TEST_F(ApplyPresetToolTest, UnknownSlotAndTrackErrors)
{
    ASSERT_FALSE(resultIsError(callTool(*server, 1, "add_track",
        QJsonObject{{"name", "Track"}})));
    auto r = callTool(*server, 2, "apply_preset",
        QJsonObject{{"trackId", 0}, {"slotIndex", 9}});
    EXPECT_TRUE(resultIsError(r));
    EXPECT_TRUE(resultText(r).contains("slot not found"));
    auto r2 = callTool(*server, 3, "apply_preset",
        QJsonObject{{"trackId", 42}, {"slotIndex", 0}});
    EXPECT_TRUE(resultIsError(r2));
    EXPECT_TRUE(resultText(r2).contains("slot not found"));
}
