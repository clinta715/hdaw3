#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/MidiClipProcessor.h"
#include "engine/Track.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <fstream>
#include <string>

namespace {

// The Access Virus B/C single fixture (copied from timbre-lib/testdata/virus/,
// proven against the real preset library in slice 1). Resolved via __FILE__ so
// the test is independent of the runner's working directory.
juce::File virusFixtureFile()
{
    juce::File self(__FILE__);
    juce::File dir = juce::File::isAbsolutePath(__FILE__)
        ? self.getParentDirectory().getChildFile("testdata/virus")
        : juce::File::getCurrentWorkingDirectory().getChildFile(
            "tests/unit/engine/testdata/virus");
    return dir.getChildFile("bcsingle.syx");
}

// Same hand-rolled RIFF writer as audio_pool_dedup_test.cpp — no JUCE writer
// dependency, keeps the test self-contained.
juce::File writeSineWav(const char* tag, int lengthSamples, double sr = 44100.0)
{
    const int numChannels = 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int dataSize = lengthSamples * numChannels * bytesPerSample;

    juce::File file = juce::File::getCurrentWorkingDirectory()
        .getChildFile(juce::String("fxrace_test_") + tag + ".wav");
    file.deleteFile();
    std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);

    auto writeChunk = [&](const char* id, const void* data, int size)
    {
        out.write(id, 4);
        out.write(reinterpret_cast<const char*>(&size), 4);
        out.write(static_cast<const char*>(data), size);
    };

    int sampleRate = static_cast<int>(sr);
    int byteRate = sampleRate * numChannels * bytesPerSample;
    int blockAlign = numChannels * bytesPerSample;
    out.write("RIFF", 4);
    int riffSize = 36 + dataSize;
    out.write(reinterpret_cast<const char*>(&riffSize), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    int fmtSize = 16;
    short audioFormat = 1;
    short channels = static_cast<short>(numChannels);
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

} // namespace

// Regression: addAudioClip queues the coalesced async routing rebuild on the
// pump thread (AudioEngine::valueTreeChildAdded → triggerAsyncUpdate), whose
// rebuildRoutingGraph destroys the RoutingManager/Tracks that the synchronous
// rebuildTrackFX below mutates. Before the message-thread marshal this raced a
// use-after-free (the window audio_pool_dedup_test.cpp dodges with sleep(50)).
// NO sleep here: the marshal must serialize FX rebuilds against the pending
// graph rebuild from any thread. Assertions are scoped to track 0 (lesson 9:
// the default project ships 3 tracks with empty clip lists — never assert
// absolute clip counts).
TEST(TrackFxRebuildRace, RebuildTrackFXSerializedAgainstAsyncGraphRebuild)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("engine", 44100);
    const juce::String path = file.getFullPathName();

    auto& cmds = engine.getProjectCommands();
    for (int i = 0; i < 25; ++i)
    {
        cmds.addAudioClip(0, 0.0, 1.0, path.toStdString(),
                          std::string("raceClip") + std::to_string(i));
        cmds.addFxSlot(0, "eq", 0, "");

        // Synchronous semantics: the moment the command returns, the LIVE
        // track exposes the slot — regardless of any pending async rebuild.
        auto* track = engine.getMainProcessor()->getTrack(0);
        ASSERT_NE(track, nullptr);
        EXPECT_GE(track->getNumFXSlots(), 1);

        // Remove path runs against a possibly-pending rebuild too.
        cmds.removeFxSlot(0, 0);
    }

    file.deleteFile();
}

TEST(TrackFxRebuildRace, SubSynthSlotSurvivesRebuildAndRestoresParams)
{
    AudioEngine engine;
    engine.initialize();

    auto& cmds = engine.getProjectCommands();
    cmds.addFxSlot(0, "sub_synth", 0, "");

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);

    cmds.setFxSlotParam(0, 0, 0, 2.0f);
    cmds.setFxSlotParam(0, 0, 7, 1200.0f);
    cmds.setFxSlotParam(0, 0, 15, 1.0f);
    cmds.setFxSlotParam(0, 0, 16, 0.25f);
    cmds.setFxSlotParam(0, 0, 17, 2.0f);
    cmds.setFxSlotParam(0, 0, 18, 36.0f);
    cmds.setFxSlotParam(0, 0, 23, 4.0f);

    auto fxChainTree = engine.getProjectModel().getTrackListTree()
        .getChild(0)
        .getChildWithName(IDs::FX_CHAIN);
    ASSERT_TRUE(fxChainTree.isValid());

    track->rebuildFXChain(fxChainTree);

    track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);

    auto& chain = track->getFXChain();
    ASSERT_FALSE(chain.empty());
    ASSERT_NE(chain[0], nullptr);
    EXPECT_EQ(chain[0]->getType(), "sub_synth");

    const auto defs = chain[0]->getInternalParamDefs();
    ASSERT_GE(defs.size(), 24u);
    EXPECT_EQ(defs[15].name, "Legato");
    EXPECT_EQ(defs[16].name, "Portamento");
    EXPECT_EQ(defs[17].name, "Filter Type");
    EXPECT_EQ(defs[18].name, "Filter Env Amount");
    EXPECT_EQ(defs[23].name, "Pitch Bend Range");

    const auto values = chain[0]->getInternalParamValues();
    ASSERT_GE(values.size(), 24u);
    EXPECT_FLOAT_EQ(values[0], 2.0f);
    EXPECT_FLOAT_EQ(values[7], 1200.0f);
    EXPECT_FLOAT_EQ(values[15], 1.0f);
    EXPECT_FLOAT_EQ(values[16], 0.25f);
    EXPECT_FLOAT_EQ(values[17], 2.0f);
    EXPECT_FLOAT_EQ(values[18], 36.0f);
    EXPECT_FLOAT_EQ(values[23], 4.0f);

    auto params = engine.getReadModel().getInternalFxParams(0, 0);
    ASSERT_FALSE(params.empty());
    EXPECT_EQ(params[0].value, 2.0f);
    EXPECT_EQ(params[7].value, 1200.0f);
    EXPECT_EQ(params[15].value, 1.0f);
    EXPECT_EQ(params[16].value, 0.25f);
    EXPECT_EQ(params[17].value, 2.0f);
    EXPECT_EQ(params[18].value, 36.0f);
    EXPECT_EQ(params[23].value, 4.0f);

    auto fxSlots = engine.getReadModel().getFxSlots(0);
    ASSERT_FALSE(fxSlots.empty());
    EXPECT_EQ(fxSlots[0].fxType, "sub_synth");
}

// Same shape on the default MIDI "Synth" track (index 1) for
// rebuildMidiTrackFX: MIDI clip adds queue the async graph rebuild while the
// MIDI-FX commands mutate the live track's chain from this thread.
TEST(TrackFxRebuildRace, RebuildMidiTrackFXSerialized)
{
    AudioEngine engine;
    engine.initialize();

    auto& cmds = engine.getProjectCommands();
    for (int i = 0; i < 25; ++i)
    {
        cmds.addMidiClip(1, 0.0, 1.0, std::string("raceMidi") + std::to_string(i));
        cmds.addMidiFxSlot(1, "arpeggiator", 0);

        auto* track = engine.getMainProcessor()->getTrack(1);
        ASSERT_NE(track, nullptr);
        EXPECT_GE(track->getNumMidiFxSlots(), 1);

        cmds.removeMidiFxSlot(1, 0);
    }
}

// Same race shape for rebuildModulation: addModulation mutates the track's
// MODULATION_LIST on this thread (child-added listener → rebuildModulation,
// AudioEngine.cpp valueTreeChildAdded) while addAudioClip's async graph
// rebuild can swap the RoutingManager under the live Track. The marshal must
// make the modulation count observable synchronously — no sleep.
TEST(TrackFxRebuildRace, RebuildModulationSerializedAgainstAsyncGraphRebuild)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("modrace", 44100);
    const juce::String path = file.getFullPathName();

    // addModulation/removeModulation are concrete AudioEngineCommands methods
    // (they take a juce::ValueTree), not on the abstract ProjectCommands face.
    auto& cmds = dynamic_cast<AudioEngineCommands&>(engine.getProjectCommands());
    for (int i = 0; i < 25; ++i)
    {
        cmds.addAudioClip(0, 0.0, 1.0, path.toStdString(),
                          std::string("modRace") + std::to_string(i));

        // Minimal LFO tree: ModulationManager::rebuild only consumes `type`
        // ("lfo"); every property LFOModulationSource::fromValueTree reads has
        // a default (waveform/rate/rateSync/depth/bipolar/phaseOffset/
        // targetParamID/enabled).
        juce::ValueTree modTree(IDs::MODULATION);
        modTree.setProperty("type", "lfo", nullptr);
        cmds.addModulation(0, modTree);

        auto* track = engine.getMainProcessor()->getTrack(0);
        ASSERT_NE(track, nullptr);
        EXPECT_GE(track->getNumModulations(), 1);

        // Removal path also runs against a possibly-pending rebuild; track 0
        // ships no seeded MODULATION_LIST, so the count must return to 0.
        cmds.removeModulation(0, 0);
        track = engine.getMainProcessor()->getTrack(0);
        ASSERT_NE(track, nullptr);
        EXPECT_EQ(track->getNumModulations(), 0);
    }

    file.deleteFile();
}

// Same race shape for rebuildAutomationCache: addAutomationLane calls it
// directly (AudioEngineCommands_Automation.cpp) on this thread while the
// async graph rebuild can swap the RoutingManager. Track 0 ships seeded
// Volume/Pan/Mute lanes (ProjectModel::createTrackAutomationList), so
// assertions are baseline-relative (lesson 9 — no absolute counts).
TEST(TrackFxRebuildRace, RebuildAutomationCacheSerializedAgainstAsyncGraphRebuild)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("autorace", 44100);
    const juce::String path = file.getFullPathName();

    auto& cmds = engine.getProjectCommands();
    for (int i = 0; i < 25; ++i)
    {
        cmds.addAudioClip(0, 0.0, 1.0, path.toStdString(),
                          std::string("autoRace") + std::to_string(i));

        auto* track = engine.getMainProcessor()->getTrack(0);
        ASSERT_NE(track, nullptr);
        const int baseline = track->getNumAutomations();

        // paramID 0 = unbound legacy lane — cannot conflict with the seeded
        // lanes' paramIDs (1/2/3). Point time is in beats (converted inside).
        cmds.addAutomationLane(0, "vol", 0);
        cmds.addAutomationPoint(0, "vol", 0.0, 0.8f);

        track = engine.getMainProcessor()->getTrack(0);
        ASSERT_NE(track, nullptr);
        EXPECT_GE(track->getNumAutomations(), baseline + 1);

        cmds.removeAutomationLane(0, "vol");
        track = engine.getMainProcessor()->getTrack(0);
        ASSERT_NE(track, nullptr);
        EXPECT_EQ(track->getNumAutomations(), baseline);
    }

    file.deleteFile();
}

// Same race shape for toggleFXEditor: the RPC/undo entry forwards to a live
// Track on the caller's thread. Internal-FX ("eq") slots no-op the editor
// itself (Track::toggleFXEditor logs and returns for non-plugin slots) — the
// marshaled live-Track lookup is the path under test.
TEST(TrackFxRebuildRace, ToggleFXEditorSerializedAgainstAsyncGraphRebuild)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("edrace", 44100);
    const juce::String path = file.getFullPathName();

    auto& cmds = engine.getProjectCommands();
    for (int i = 0; i < 25; ++i)
    {
        cmds.addAudioClip(0, 0.0, 1.0, path.toStdString(),
                          std::string("edRace") + std::to_string(i));
        cmds.addFxSlot(0, "eq", 0, "");

        engine.getMainProcessor()->toggleFXEditor(0, 0);
        engine.getMainProcessor()->toggleFXEditor(0, 0);

        auto* track = engine.getMainProcessor()->getTrack(0);
        ASSERT_NE(track, nullptr);
        EXPECT_EQ(track->getNumFXSlots(), 1);

        cmds.removeFxSlot(0, 0);
    }

    file.deleteFile();
}

// Same race shape for rebuildMidiClipCache: addNote's MIDI_NOTE child-added
// listener (AudioEngine.cpp valueTreeChildAdded) used to invalidate the live
// MidiClipProcessor's note cache directly on this thread while addAudioClip's
// async graph rebuild can swap (and destroy) the RoutingManager mid-iteration.
// The marshal must keep the cache observable synchronously — no sleep.
// Track 1 is the default MIDI "Synth" track with an empty clip list (lesson 9),
// so our clip is index 0 there; track 2 is an audio track whose clip adds
// queue the async rebuild without touching track 1's clip indices.
TEST(TrackFxRebuildRace, RebuildMidiClipCacheSerializedAgainstAsyncGraphRebuild)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("midirace", 44100);
    const juce::String path = file.getFullPathName();

    auto& cmds = engine.getProjectCommands();
    const int clipId = cmds.addMidiClip(1, 0.0, 4.0, "raceMidiCache");
    ASSERT_GE(clipId, 0);

    for (int i = 0; i < 25; ++i)
    {
        // Queue an async graph rebuild on the pump thread (CLIP child-added
        // → triggerAsyncUpdate) that will swap the RoutingManager.
        cmds.addAudioClip(2, 0.0, 1.0, path.toStdString(),
                          std::string("midiCacheRace") + std::to_string(i));

        // Real note command: mints a MIDI_NOTE child under the clip's
        // MIDI_NOTE_LIST → child-added listener → rebuildMidiClipCache.
        cmds.addNote(clipId, 60 + i, 100, static_cast<double>(i), 1.0);

        // Synchronous semantics: the CURRENT routing manager's live
        // processor for {track 1, clip 0} must already see at least i+1
        // cached notes — even if the async rebuild swapped the manager
        // between the command and this read.
        auto* main = engine.getMainProcessor();
        ASSERT_NE(main, nullptr);
        auto* rm = main->getRoutingManager();
        ASSERT_NE(rm, nullptr);
        const auto& sources = rm->getMidiClipSources();
        auto it = sources.find({ 1, 0 });
        ASSERT_NE(it, sources.end());
        ASSERT_NE(it->second, nullptr);
        EXPECT_GE(it->second->getNumCachedNotes(), i + 1);
    }

    file.deleteFile();
}

// Lesson-10 / Gate-10 discipline for the Virus patch loader: the imported
// patch writes sub_synth params 0..22 via setFxSlotParam (ValueTree), so it
// must survive a rebuildFXChain() on the LIVE processor — a ReadModel-only
// assertion would not prove the restored slot sounds right. The reserved
// param 23 (Pitch Bend Range) is never written and must stay at its default.
TEST(TrackFxRebuildRace, SubSynthPatchLoadSurvivesRebuild)
{
    AudioEngine engine;
    engine.initialize();

    auto& cmds = engine.getProjectCommands();
    cmds.addFxSlot(0, "sub_synth", 0, "");

    auto fixture = virusFixtureFile();
    ASSERT_TRUE(fixture.existsAsFile())
        << "missing fixture: " << fixture.getFullPathName();

    auto r = engine.getAudioEngineCommands().loadVirusPatch(
        0, 0, fixture.getFullPathName().toStdString(), 0);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.name, "~WELCOME");
    EXPECT_EQ(r.bank, 1);
    EXPECT_EQ(r.program, 0);
    EXPECT_EQ(r.mappedCount, 23);
    ASSERT_EQ(r.unmapped.size(), 12u);

    // The ValueTree carries the patch (params are the source of truth).
    auto fxChainTree = engine.getProjectModel().getTrackListTree()
        .getChild(0)
        .getChildWithName(IDs::FX_CHAIN);
    ASSERT_TRUE(fxChainTree.isValid());

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);

    // Rebuild the FX chain from the tree (the projection seam) and assert the
    // LIVE processor — params changed + the reserved param at default.
    track->rebuildFXChain(fxChainTree);

    track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);
    auto& chain = track->getFXChain();
    ASSERT_FALSE(chain.empty());
    ASSERT_NE(chain[0], nullptr);
    EXPECT_EQ(chain[0]->getType(), "sub_synth");

    const auto values = chain[0]->getInternalParamValues();
    ASSERT_GE(values.size(), 24u);
    EXPECT_FLOAT_EQ(values[0], 1.0f);           // osc1 wave -> Saw
    EXPECT_NEAR(values[1], 0.503937f, 1e-3f);   // osc1 level 64/127
    EXPECT_NEAR(values[4], 98.4375f, 1e-3f);    // osc2 detune +98.4 cents
    EXPECT_NEAR(values[7], 86.8611f, 1e-3f);    // cutoff 20*pow(1000,27/127)
    EXPECT_NEAR(values[8], 0.196850f, 1e-3f);   // resonance 25/127
    EXPECT_FLOAT_EQ(values[11], 5.0f);          // amp decay max
    EXPECT_NEAR(values[13], 0.052290f, 1e-3f);  // amp release
    EXPECT_NEAR(values[14], 1.181102f, 1e-3f);  // output 100/127*1.5
    EXPECT_NEAR(values[18], 14.25f, 1e-3f);     // filter env amount 83
    EXPECT_NEAR(values[19], 0.213826f, 1e-3f);  // filter attack
    EXPECT_FLOAT_EQ(values[22], 5.0f);          // filter release max
    EXPECT_FLOAT_EQ(values[23], 2.0f);          // reserved param stays default

    // ReadModel agrees (same ValueTree props the frontend renders).
    auto params = engine.getReadModel().getInternalFxParams(0, 0);
    bool foundCutoff = false;
    for (const auto& p : params)
        if (p.paramIndex == 7)
        {
            EXPECT_NEAR(p.value, 86.8611f, 1e-3f);
            foundCutoff = true;
        }
    EXPECT_TRUE(foundCutoff);
}

// Gate 6: the patch must persist through a save/load round-trip — saveProject
// writes the param_N props, loadProject restores them, and the rebuilt live
// processor plays the patch (not just the ReadModel).
TEST(TrackFxRebuildRace, SubSynthPatchLoadPersistsAcrossSaveLoad)
{
    AudioEngine engine;
    engine.initialize();

    auto& cmds = engine.getProjectCommands();
    cmds.addFxSlot(0, "sub_synth", 0, "");

    auto fixture = virusFixtureFile();
    ASSERT_TRUE(fixture.existsAsFile());
    auto r = engine.getAudioEngineCommands().loadVirusPatch(
        0, 0, fixture.getFullPathName().toStdString(), 0);
    ASSERT_TRUE(r.ok) << r.error;

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_virus_subsynth_roundtrip.hdaw");
    tmp.deleteFile();
    ASSERT_TRUE(cmds.saveProject(tmp.getFullPathName().toStdString()));
    ASSERT_TRUE(cmds.loadProject(tmp.getFullPathName().toStdString()));

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);
    auto& chain = track->getFXChain();
    ASSERT_FALSE(chain.empty());
    ASSERT_NE(chain[0], nullptr);
    EXPECT_EQ(chain[0]->getType(), "sub_synth");

    const auto values = chain[0]->getInternalParamValues();
    ASSERT_GE(values.size(), 24u);
    EXPECT_NEAR(values[1], 0.503937f, 1e-3f);
    EXPECT_NEAR(values[4], 98.4375f, 1e-3f);
    EXPECT_NEAR(values[7], 86.8611f, 1e-3f);
    EXPECT_NEAR(values[14], 1.181102f, 1e-3f);
    EXPECT_NEAR(values[18], 14.25f, 1e-3f);
    EXPECT_FLOAT_EQ(values[23], 2.0f);          // reserved param stays default

    auto params = engine.getReadModel().getInternalFxParams(0, 0);
    bool found = false;
    for (const auto& p : params)
        if (p.paramIndex == 4)
        {
            EXPECT_NEAR(p.value, 98.4375f, 1e-3f);
            found = true;
        }
    EXPECT_TRUE(found);

    tmp.deleteFile();
}
