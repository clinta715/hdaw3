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
        tempDir.deleteRecursively();
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

TEST_F(FileLibraryTest, SearchByName) {
    auto midiDir = tempDir.getChildFile("search_name");
    midiDir.createDirectory();
    // Create three MIDI files: alpha, beta, gamma
    const char* names[] = {"alpha.mid", "beta.mid", "gamma.mid"};
    for (const auto* nm : names) {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(0.5));
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        juce::FileOutputStream stream(midiDir.getChildFile(nm));
        file.writeTo(stream);
        stream.flush();
    }
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("SBN", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);

    // Empty query returns all 3
    auto all = mgr.search("");
    ASSERT_EQ(all.size(), 3u);

    // Substring filter
    auto filtered = mgr.search("bet");
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name, "beta.mid");

    // Case-insensitive
    auto upper = mgr.search("ALPHA");
    ASSERT_EQ(upper.size(), 1u);
    EXPECT_EQ(upper[0].name, "alpha.mid");
}

TEST_F(FileLibraryTest, SearchFiltersByType) {
    auto midiDir = tempDir.getChildFile("ftype_midi");
    midiDir.createDirectory();
    auto audioDir = tempDir.getChildFile("ftype_audio");
    audioDir.createDirectory();

    // One MIDI file
    {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(0.5));
        juce::MidiFile file; file.setTicksPerQuarterNote(480); file.addTrack(seq);
        juce::FileOutputStream s(midiDir.getChildFile("song.mid")); file.writeTo(s); s.flush();
    }
    // One WAV file (proven pattern from ExtractAudioMetadata)
    {
        auto wavFile = audioDir.getChildFile("beat.wav");
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
    }

    HDAW::FileLibraryManager mgr(tempDir);
    auto midiId = mgr.addLibrary("M", midiDir.getFullPathName(), "midi");
    auto audioId = mgr.addLibrary("A", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(midiId);
    mgr.scanLibrary(audioId);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);

    // No filter → both
    EXPECT_EQ(mgr.search("").size(), 2u);
    // type=midi → only the MIDI entry
    auto midis = mgr.search("", "midi");
    ASSERT_EQ(midis.size(), 1u);
    EXPECT_EQ(midis[0].name, "song.mid");
    // type=audio → only the WAV entry
    auto audios = mgr.search("", "audio");
    ASSERT_EQ(audios.size(), 1u);
    EXPECT_EQ(audios[0].name, "beat.wav");
}

TEST_F(FileLibraryTest, SearchSortsByName) {
    auto midiDir = tempDir.getChildFile("search_sort");
    midiDir.createDirectory();
    // Create files out of order: zebra, apple, mango
    const char* names[] = {"zebra.mid", "apple.mid", "mango.mid"};
    for (const auto* nm : names) {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(0.5));
        juce::MidiFile file; file.setTicksPerQuarterNote(480); file.addTrack(seq);
        juce::FileOutputStream s(midiDir.getChildFile(nm)); file.writeTo(s); s.flush();
    }
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("SS", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].name, "apple.mid");
    EXPECT_EQ(results[1].name, "mango.mid");
    EXPECT_EQ(results[2].name, "zebra.mid");
}

TEST_F(FileLibraryTest, SearchPaginates) {
    auto midiDir = tempDir.getChildFile("search_page");
    midiDir.createDirectory();
    // Create 5 files: f0..f4
    for (int i = 0; i < 5; ++i) {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(0.5));
        juce::MidiFile file; file.setTicksPerQuarterNote(480); file.addTrack(seq);
        juce::FileOutputStream s(midiDir.getChildFile("f" + juce::String(i) + ".mid")); file.writeTo(s); s.flush();
    }
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("SP", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);

    // limit=2, offset=0 → first 2 (sorted: f0, f1, f2, f3, f4)
    auto page1 = mgr.search("", {}, {}, -1, -1, -1, -1, {}, 0, 2);
    ASSERT_EQ(page1.size(), 2u);
    EXPECT_EQ(page1[0].name, "f0.mid");
    EXPECT_EQ(page1[1].name, "f1.mid");
    // offset=2, limit=2 → next 2
    auto page2 = mgr.search("", {}, {}, -1, -1, -1, -1, {}, 2, 2);
    ASSERT_EQ(page2.size(), 2u);
    EXPECT_EQ(page2[0].name, "f2.mid");
    EXPECT_EQ(page2[1].name, "f3.mid");
    // offset=4, limit=2 → last 1
    auto page3 = mgr.search("", {}, {}, -1, -1, -1, -1, {}, 4, 2);
    ASSERT_EQ(page3.size(), 1u);
    EXPECT_EQ(page3[0].name, "f4.mid");
    // offset beyond end → empty
    auto page4 = mgr.search("", {}, {}, -1, -1, -1, -1, {}, 10, 2);
    EXPECT_TRUE(page4.empty());
}

TEST_F(FileLibraryTest, SearchLazyLoadsFromDiskAcrossInstances) {
    auto midiDir = tempDir.getChildFile("lazy_load");
    midiDir.createDirectory();

    // Create one MIDI file
    {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(0.5));
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        juce::FileOutputStream stream(midiDir.getChildFile("persisted.mid"));
        file.writeTo(stream);
        stream.flush();
    }

    // Instance 1: add + scan (writes <id>.json to disk)
    juce::String id;
    {
        HDAW::FileLibraryManager mgr(tempDir);
        id = mgr.addLibrary("LazyLoad", midiDir.getFullPathName(), "midi");
        mgr.scanLibrary(id);
        for (int i = 0; i < 50 && mgr.isScanning(); ++i)
            juce::Thread::sleep(100);
        ASSERT_EQ(mgr.search("").size(), 1u);  // in-memory, post-scan
    } // mgr destroyed — entries flushed to disk via saveLibraryEntries

    // Instance 2: NEW manager on same tempDir — NO scan.
    // search() must lazy-load via loadLibraryEntries.
    HDAW::FileLibraryManager mgr2(tempDir);
    auto results = mgr2.search("");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "persisted.mid");
}

TEST_F(FileLibraryTest, FullMidiPipeline) {
    auto midiDir = tempDir.getChildFile("midi_pipeline");
    midiDir.createDirectory();

    for (int i = 0; i < 5; ++i) {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60 + i).withTimeStamp(1.0));
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        juce::FileOutputStream stream(midiDir.getChildFile("test_" + juce::String(i) + ".mid"));
        file.writeTo(stream);
        stream.flush();
    }

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Pipeline Test", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);

    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);

    auto all = mgr.search("");
    EXPECT_EQ(all.size(), 5u);

    auto filtered = mgr.search("test_2");
    EXPECT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].name, "test_2.mid");
}

TEST_F(FileLibraryTest, ScanProgressCallback) {
    auto midiDir = tempDir.getChildFile("midi_progress");
    midiDir.createDirectory();

    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
    seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(1.0));
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTrack(seq);
    juce::FileOutputStream stream(midiDir.getChildFile("progress_test.mid"));
    file.writeTo(stream);
    stream.flush();

    HDAW::FileLibraryManager mgr(tempDir);
    std::vector<HDAW::ScanProgress> progressUpdates;
    mgr.setScanProgressCallback([&](const HDAW::ScanProgress& p) {
        progressUpdates.push_back(p);
    });

    bool scanComplete = false;
    mgr.setScanCompleteCallback([&](const juce::String&, bool) {
        scanComplete = true;
    });

    auto id = mgr.addLibrary("Progress", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);

    for (int i = 0; i < 50 && !scanComplete; ++i)
        juce::Thread::sleep(100);

    EXPECT_TRUE(scanComplete);
    EXPECT_FALSE(progressUpdates.empty());
    EXPECT_EQ(progressUpdates.back().total, 1);
}

TEST_F(FileLibraryTest, PartitioningByFirstChar) {
    auto libDir = tempDir.getChildFile("partition_test");
    libDir.createDirectory();

    HDAW::FileLibraryManager mgr(libDir);
    auto id = mgr.addLibrary("Partitioned", libDir.getFullPathName(), "midi");

    // Manually create partition files in the manager's libraries dir
    auto mgrDir = libDir.getChildFile("libraries");

    // Partition 'a': alpha.mid
    {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        juce::Array<juce::var> arr;
        juce::DynamicObject::Ptr e1 = new juce::DynamicObject();
        e1->setProperty("name", "alpha.mid");
        e1->setProperty("path", "/alpha.mid");
        e1->setProperty("size", (double)100);
        e1->setProperty("modified", "");
        e1->setProperty("tracks", 0);
        e1->setProperty("notes", 0);
        e1->setProperty("durationTicks", 0);
        e1->setProperty("durationSeconds", 1.0);
        e1->setProperty("tempo", 120.0);
        e1->setProperty("timeSignature", "4/4");
        e1->setProperty("key", "");
        e1->setProperty("sampleRate", 0.0);
        e1->setProperty("channels", 0);
        e1->setProperty("bpm", 0.0);
        e1->setProperty("format", "");
        arr.add(juce::var(e1.get()));
        root->setProperty("entries", arr);
        mgrDir.getChildFile(id + "_part_a.json").replaceWithText(juce::JSON::toString(juce::var(root.get())));
    }

    // Partition 'b': beta.mid
    {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        juce::Array<juce::var> arr;
        juce::DynamicObject::Ptr e2 = new juce::DynamicObject();
        e2->setProperty("name", "beta.mid");
        e2->setProperty("path", "/beta.mid");
        e2->setProperty("size", (double)200);
        e2->setProperty("modified", "");
        e2->setProperty("tracks", 0);
        e2->setProperty("notes", 0);
        e2->setProperty("durationTicks", 0);
        e2->setProperty("durationSeconds", 2.0);
        e2->setProperty("tempo", 120.0);
        e2->setProperty("timeSignature", "4/4");
        e2->setProperty("key", "");
        e2->setProperty("sampleRate", 0.0);
        e2->setProperty("channels", 0);
        e2->setProperty("bpm", 0.0);
        e2->setProperty("format", "");
        arr.add(juce::var(e2.get()));
        root->setProperty("entries", arr);
        mgrDir.getChildFile(id + "_part_b.json").replaceWithText(juce::JSON::toString(juce::var(root.get())));
    }

    // search() should lazy-load from partitions and return both entries
    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 2u);
    // Sorted by name: alpha first, beta second
    EXPECT_EQ(results[0].name, "alpha.mid");
    EXPECT_EQ(results[1].name, "beta.mid");
    EXPECT_DOUBLE_EQ(results[0].durationSeconds, 1.0);
    EXPECT_DOUBLE_EQ(results[1].durationSeconds, 2.0);
}
