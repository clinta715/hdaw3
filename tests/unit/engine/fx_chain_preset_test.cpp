#include <gtest/gtest.h>
#include "engine/ChainLibrary.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"
#include <cmath>
#include <fstream>

// Qt defines `slots` as a keyword macro (signals/slots); once Qt headers are
// pulled in (via AudioEngine.h) it textually blanks ChainPreset::slots. This
// TU uses no Qt signals/slots keywords, so undef it. Task 1's
// chain_library_test.cpp dodges this by including no Qt headers at all.
#ifdef slots
#undef slots
#endif

// Task 2 (plans/2026-09-02-fx-chain-presets.md): apply a chain and assert the
// LIVE processor (lesson-10 seam), rebuild, assert again, then round-trip
// through exportFxChain.
TEST(FxChainPreset, ApplySurvivesRebuildLive)
{
    AudioEngine engine;
    engine.initialize();
    auto& commands = engine.getAudioEngineCommands();

    HDAW::ChainPreset p;
    p.name = "T";
    HDAW::ChainPreset::Slot a;
    a.fxType = "compressor";
    a.params = { { "param_0", -12.0 } };
    HDAW::ChainPreset::Slot b;
    b.fxType = "filter";
    b.params = { { "param_0", 800.0 } };
    b.bypassed = true;
    p.slots = { a, b };

    juce::String error;
    ASSERT_TRUE(commands.applyFxChain(0, p, &error)) << error.toStdString();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->getFXChain().size(), 2u);
    EXPECT_EQ(track->getFXChain()[0]->getType().toStdString(), "compressor");

    commands.rebuildTrackFX(0);

    track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->getFXChain().size(), 2u);
    ASSERT_GE(track->getFXChain()[0]->getInternalParamValues().size(), 1u);
    EXPECT_DOUBLE_EQ(track->getFXChain()[0]->getInternalParamValues()[0], -12.0);
    EXPECT_TRUE(track->getFXChain()[1]->isBypassed());

    // Round-trip: export must equal what was applied.
    auto exported = commands.exportFxChain(0);
    ASSERT_EQ(exported.slots.size(), 2u);
    EXPECT_EQ(exported.slots[0].fxType.toStdString(), "compressor");
    ASSERT_TRUE(exported.slots[0].params.count("param_0") > 0);
    EXPECT_DOUBLE_EQ(exported.slots[0].params.at("param_0"), -12.0);
    EXPECT_EQ(exported.slots[1].fxType.toStdString(), "filter");
    EXPECT_TRUE(exported.slots[1].bypassed);
    ASSERT_TRUE(exported.slots[1].params.count("param_0") > 0);
    EXPECT_DOUBLE_EQ(exported.slots[1].params.at("param_0"), 800.0);
}

namespace {

// Minimal 16-bit mono 440 Hz sine WAV, same approach as
// audio_pool_dedup_test.cpp's writeSineWav (self-contained so this TU needs
// no fixture sharing).
juce::File writeSineWavForChainTest(const char* tag, int lengthSamples = 44100)
{
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int dataSize = lengthSamples * bytesPerSample;

    juce::File file = juce::File::getCurrentWorkingDirectory()
        .getChildFile(juce::String("chain_preset_test_") + tag + ".wav");
    file.deleteFile();
    std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);

    int sampleRate = 44100;
    int byteRate = sampleRate * bytesPerSample;
    int blockAlign = bytesPerSample;
    out.write("RIFF", 4);
    int riffSize = 36 + dataSize;
    out.write(reinterpret_cast<const char*>(&riffSize), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    int fmtSize = 16;
    short audioFormat = 1;
    short channels = 1;
    out.write(reinterpret_cast<const char*>(&fmtSize), 4);
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&sampleRate), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    short bits = bitsPerSample;
    out.write(reinterpret_cast<const char*>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataSize), 4);
    for (int i = 0; i < lengthSamples; ++i)
    {
        short v = static_cast<short>(std::sin(2.0 * 3.14159 * 440.0 * i / sampleRate) * 32000.0);
        out.write(reinterpret_cast<const char*>(&v), 2);
    }
    out.close();
    return file;
}

// Synchronous tree-level FX slot count (0 when the track has no FX_CHAIN yet,
// -1 for a bad track index). Live-processor reads need a
// drainPendingRoutingRebuild() first (async coalesced rebuild, same race as
// audio_pool_dedup_test.cpp documents).
int treeFxSlotCount(AudioEngine& engine, int trackIndex)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return -1;
    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
        return 0;
    return fxChain.getNumChildren();
}

HDAW::ChainPreset makeTwoSlotPreset()
{
    HDAW::ChainPreset p;
    p.name = "T";
    HDAW::ChainPreset::Slot a;
    a.fxType = "compressor";
    a.params = { { "param_0", -12.0 } };
    HDAW::ChainPreset::Slot b;
    b.fxType = "filter";
    b.params = { { "param_0", 800.0 } };
    b.bypassed = true;
    p.slots = { a, b };
    return p;
}

} // namespace

// Spec gap 1: sampler slot round-trips sampleFile/mode/rootNote identically.
TEST(FxChainPreset, SamplerRoundTripPreservesFileModeAndRoot)
{
    AudioEngine engine;
    engine.initialize();
    auto& commands = engine.getAudioEngineCommands();

    auto file = writeSineWavForChainTest("sampler");
    const juce::String path = file.getFullPathName();

    HDAW::ChainPreset p;
    p.name = "SamplerRT";
    HDAW::ChainPreset::Slot s;
    s.fxType = "sampler";
    s.sampler["sampleFile"] = path;
    s.sampler["mode"] = "one-shot";
    s.sampler["rootNote"] = "60";
    p.slots = { s };

    juce::String error;
    ASSERT_TRUE(commands.applyFxChain(0, p, &error)) << error.toStdString();

    engine.drainPendingRoutingRebuild();
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->getFXChain().size(), 1u);
    EXPECT_EQ(track->getFXChain()[0]->getType().toStdString(), "sampler");

    auto exported = commands.exportFxChain(0);
    ASSERT_EQ(exported.slots.size(), 1u);
    EXPECT_EQ(exported.slots[0].fxType.toStdString(), "sampler");
    ASSERT_TRUE(exported.slots[0].sampler.count("sampleFile") > 0);
    EXPECT_EQ(exported.slots[0].sampler.at("sampleFile").toStdString(), path.toStdString());
    ASSERT_TRUE(exported.slots[0].sampler.count("mode") > 0);
    EXPECT_EQ(exported.slots[0].sampler.at("mode").toStdString(), "one-shot");
    ASSERT_TRUE(exported.slots[0].sampler.count("rootNote") > 0);
    EXPECT_EQ(exported.slots[0].sampler.at("rootNote").toStdString(), "60");

    file.deleteFile();
}

// Spec gap 2: plugin state export is byte-identical. Hand-built tree slot
// (plugin_state_save_load_test.cpp:131-136 pattern, with &undoManager); the
// apply-half with live plugins is out of scope (no VST hosting in unit tests).
// The export pre-pass skips slots with no live instance, so the hand-set blob
// must survive untouched.
TEST(FxChainPreset, PluginStateExportIsByteIdentical)
{
    AudioEngine engine;
    engine.initialize();
    engine.drainPendingRoutingRebuild();
    auto& commands = engine.getAudioEngineCommands();

    auto& um = engine.getProjectModel().getUndoManager();
    auto trackList = engine.getProjectModel().getTrackListTree();
    ASSERT_GT(trackList.getNumChildren(), 0);
    auto trackTree = trackList.getChild(0);
    auto fxChain = trackTree.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        trackTree.addChild(fxChain, -1, &um);
    }

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String("plugin"), &um);
    slot.setProperty(IDs::pluginID, juce::String("test.plugin"), &um);
    slot.setProperty(IDs::pluginFormat, juce::String("VST3"), &um);
    slot.setProperty(IDs::pluginState, juce::String("aGVsbG8="), &um);
    fxChain.addChild(slot, -1, &um);

    auto exported = commands.exportFxChain(0);
    ASSERT_EQ(exported.slots.size(), 1u);
    EXPECT_EQ(exported.slots[0].fxType.toStdString(), "plugin");
    EXPECT_EQ(exported.slots[0].plugin.id.toStdString(), "test.plugin");
    EXPECT_EQ(exported.slots[0].plugin.format.toStdString(), "VST3");
    EXPECT_EQ(exported.slots[0].plugin.stateBase64.toStdString(), "aGVsbG8=");
}

// Spec gap 3: the whole apply is ONE undo unit topped by exactly
// "Apply FX chain preset" (JUCE getUndoDescriptions()[0] is the most recent;
// beginNewTransaction is lazy so endTransaction adds no empty entry), and
// undo() restores the empty chain.
TEST(FxChainPreset, ApplyIsSingleUndoUnit)
{
    AudioEngine engine;
    engine.initialize();
    auto& commands = engine.getAudioEngineCommands();

    const size_t before = commands.getUndoDescriptions().size();

    auto p = makeTwoSlotPreset();
    juce::String error;
    ASSERT_TRUE(commands.applyFxChain(0, p, &error)) << error.toStdString();

    auto descs = commands.getUndoDescriptions();
    ASSERT_EQ(descs.size(), before + 1u);
    ASSERT_FALSE(descs.empty());
    EXPECT_EQ(descs.front(), "Apply FX chain preset");

    commands.undo();

    // Tree-level read is synchronous. NOTE: FX_SLOT add/remove has no
    // listener-driven live rebuild (per-op commands call rebuildTrackFX
    // explicitly), so undo() alone leaves the live chain stale — the same
    // rebuild the UI issues after undo is required here.
    EXPECT_EQ(treeFxSlotCount(engine, 0), 0);

    commands.rebuildTrackFX(0);
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->getFXChain().size(), 0u);
}

// Spec gap 4: validation happens before any write (AudioEngineCommands_Fx.cpp
// Gate 9 pre-pass), so every failure leaves the chain unchanged.
TEST(FxChainPreset, ApplyErrorPathsLeaveChainUnchanged)
{
    AudioEngine engine;
    engine.initialize();
    auto& commands = engine.getAudioEngineCommands();

    auto valid = makeTwoSlotPreset();
    juce::String error;

    // Invalid track index.
    EXPECT_FALSE(commands.applyFxChain(999, valid, &error));
    EXPECT_EQ(treeFxSlotCount(engine, 0), 0);

    // Unknown fxType.
    HDAW::ChainPreset badType;
    HDAW::ChainPreset::Slot n;
    n.fxType = "not_a_real_fx";
    badType.slots = { n };
    error.clear();
    EXPECT_FALSE(commands.applyFxChain(0, badType, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(treeFxSlotCount(engine, 0), 0);

    // Param index beyond the def count for compressor.
    HDAW::ChainPreset badParam;
    HDAW::ChainPreset::Slot c;
    c.fxType = "compressor";
    c.params = { { "param_99", 1.0 } };
    badParam.slots = { c };
    error.clear();
    EXPECT_FALSE(commands.applyFxChain(0, badParam, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(treeFxSlotCount(engine, 0), 0);

    // Live processor untouched too.
    engine.drainPendingRoutingRebuild();
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->getFXChain().size(), 0u);
}
