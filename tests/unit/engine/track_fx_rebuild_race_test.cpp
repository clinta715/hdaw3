#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <fstream>
#include <string>

namespace {

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
