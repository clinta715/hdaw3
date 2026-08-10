#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/FileLibraryManager.h"

class FileLibraryTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("hdaw_file_library_test");
        tempDir.createDirectory();
    }
    void TearDown() override {
        tempDir.deleteRecursively();
    }
    juce::File tempDir;
};

TEST_F(FileLibraryTest, AddLibraryPersistsToRegistry) {
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Test MIDI", "/some/path", "midi");
    EXPECT_FALSE(id.isEmpty());
    auto info = mgr.getLibraryInfo(id);
    EXPECT_EQ(info.name, "Test MIDI");
    EXPECT_EQ(info.path, "/some/path");
    EXPECT_EQ(info.type, "midi");
    EXPECT_FALSE(info.autoScan);
}

TEST_F(FileLibraryTest, RemoveLibraryDeletesFromRegistry) {
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("To Remove", "/tmp/test", "audio");
    mgr.removeLibrary(id);
    auto ids = mgr.getLibraryIds();
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), id) == ids.end());
}

TEST_F(FileLibraryTest, SetAutoScanTogglesFlag) {
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Auto", "/tmp/test", "midi");
    mgr.setAutoScan(id, true);
    auto info = mgr.getLibraryInfo(id);
    EXPECT_TRUE(info.autoScan);
}

TEST_F(FileLibraryTest, RegistryPersistenceRoundTrip) {
    {
        HDAW::FileLibraryManager mgr(tempDir);
        mgr.addLibrary("Persist Test", "/tmp/test", "midi");
    }
    HDAW::FileLibraryManager mgr2(tempDir);
    auto ids = mgr2.getLibraryIds();
    ASSERT_EQ(ids.size(), 1);
    auto info = mgr2.getLibraryInfo(ids[0]);
    EXPECT_EQ(info.name, "Persist Test");
}

TEST_F(FileLibraryTest, ExtractMidiMetadata) {
    auto midiDir = tempDir.getChildFile("midi");
    midiDir.createDirectory();
    auto midiFile = midiDir.getChildFile("test.mid");

    // Create MIDI file programmatically
    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100).withTimeStamp(0.0));
    seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(1.0));
    seq.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)80).withTimeStamp(0.0));
    seq.addEvent(juce::MidiMessage::noteOff(1, 64).withTimeStamp(0.5));

    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTrack(seq);

    juce::FileOutputStream stream(midiFile);
    file.writeTo(stream);
    stream.flush();

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Test MIDI", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);

    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].name, "test.mid");
    EXPECT_EQ(results[0].tracks, 1);
    EXPECT_EQ(results[0].notes, 2);
    EXPECT_GT(results[0].durationSeconds, 0.0);
}

TEST_F(FileLibraryTest, ExtractAudioMetadata) {
    auto audioDir = tempDir.getChildFile("audio");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("test.wav");

    // Write 1 second of silence at 44100 Hz, mono, 16-bit.
    auto outStream = wavFile.createOutputStream();
    ASSERT_NE(outStream, nullptr);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(outStream.get(), 44100.0, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    outStream.release(); // writer takes ownership
    juce::AudioBuffer<float> buffer(1, 44100);
    buffer.clear();
    writer->writeFromAudioSampleBuffer(buffer, 0, 44100);
    writer.reset();

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Test Audio", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);

    // Async scan runs on a threadpool — poll, don't sleep a fixed duration.
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "test.wav");
    EXPECT_GT(results[0].durationSeconds, 0.9);
    EXPECT_EQ(results[0].sampleRate, 44100.0);
    EXPECT_EQ(results[0].channels, 1);
    EXPECT_EQ(results[0].format, "wav");
}
