#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
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
