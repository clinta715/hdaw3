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
        root->setProperty("schemaVersion", 2); // v2 = dspFeatures per entry (older caches are ignored)
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
        root->setProperty("schemaVersion", 2); // v2 = dspFeatures per entry (older caches are ignored)
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

// G3: scanningCount must always be decremented when a scan job ends,
// even if a corrupt input file makes metadata extraction throw inside
// the per-file try/catch (which catches it) — the invariant is that
// isScanning() returns false after any scan, success or partial failure.
TEST_F(FileLibraryTest, ScanLibraryDecrementsScanningCountOnException) {
    auto midiDir = tempDir.getChildFile("corrupt_scan");
    midiDir.createDirectory();

    // One valid MIDI file
    {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(0.5));
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        juce::FileOutputStream s(midiDir.getChildFile("valid.mid"));
        file.writeTo(s);
        s.flush();
    }
    // One corrupt .mid file (random bytes — extractMidiMetadata will read it
    // but the parser yields an empty entry rather than throwing; either way
    // the per-file try/catch absorbs it and the scan completes).
    midiDir.getChildFile("corrupt.mid").replaceWithText("NOT A MIDI FILE");

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Corrupt", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);

    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);

    EXPECT_FALSE(mgr.isScanning());
}

// G4: removeLibrary during an in-flight scan must not be resurrected by
// the scan job's save step. We assert across a NEW manager instance to
// prove nothing is re-read from disk.
TEST_F(FileLibraryTest, RemoveLibraryDuringScanDoesNotResurrect) {
    auto midiDir = tempDir.getChildFile("race_scan");
    midiDir.createDirectory();

    // Plenty of files so the scan takes observable time.
    for (int i = 0; i < 20; ++i) {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(0.5));
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        juce::FileOutputStream s(midiDir.getChildFile("f" + juce::String(i) + ".mid"));
        file.writeTo(s);
        s.flush();
    }

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("RaceTarget", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);
    // Remove immediately while the scan may still be running.
    mgr.removeLibrary(id);

    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);

    auto ids = mgr.getLibraryIds();
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), id) == ids.end());

    // Restart-simulation: brand-new manager on the same tempDir must not
    // re-discover the removed library from the registry.
    HDAW::FileLibraryManager mgr2(tempDir);
    auto ids2 = mgr2.getLibraryIds();
    EXPECT_TRUE(std::find(ids2.begin(), ids2.end(), id) == ids2.end());

    // No {id}.json or {id}_part_*.json should exist on disk.
    auto libsDir = tempDir.getChildFile("libraries");
    EXPECT_FALSE(libsDir.getChildFile(id + ".json").existsAsFile());
    for (int i = 0; i < 26; ++i)
        EXPECT_FALSE(libsDir.getChildFile(id + "_part_" + juce::String(char('a' + i)) + ".json").existsAsFile());
    EXPECT_FALSE(libsDir.getChildFile(id + "_part_0.json").existsAsFile());
    EXPECT_FALSE(libsDir.getChildFile(id + "_part_.json").existsAsFile());
}

// G5: An empty library must be marked loaded so subsequent searches don't
// re-read the empty .json file every call. Old code bailed on loaded.empty().
TEST_F(FileLibraryTest, EmptyLibraryMarkedLoaded) {
    auto emptyDir = tempDir.getChildFile("empty_lib");
    emptyDir.createDirectory();

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Empty", emptyDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);

    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning());

    // scanDirectory's commit block inserted an empty entries[id] and saved
    // {id}.json with an empty array. search() should produce zero results
    // without throwing — and, critically, without re-reading the file every
    // call (the bug being fixed).
    auto r1 = mgr.search("");
    EXPECT_EQ(r1.size(), 0u);
    auto r2 = mgr.search("");
    EXPECT_EQ(r2.size(), 0u);
}

// G6: When a library shrinks from >50k partitioned to <=50k single-file,
// stale {id}_part_*.json files must be cleaned up. We simulate a stale
// partition file and verify a non-partitioned save removes it.
TEST_F(FileLibraryTest, ShrinkFromPartitionedRemovesPartitions) {
    auto midiDir = tempDir.getChildFile("shrink_test");
    midiDir.createDirectory();

    // One real MIDI file → small library → non-partitioned save path.
    {
        juce::MidiMessageSequence seq;
        seq.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)80).withTimeStamp(0.0));
        seq.addEvent(juce::MidiMessage::noteOff(1, 60).withTimeStamp(0.5));
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        juce::FileOutputStream s(midiDir.getChildFile("only.mid"));
        file.writeTo(s);
        s.flush();
    }

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Shrink", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning());

    auto libsDir = tempDir.getChildFile("libraries");
    // Plant a stale partition file (as if the library used to be partitioned).
    libsDir.getChildFile(id + "_part_a.json").replaceWithText("{\"entries\":[]}");
    ASSERT_TRUE(libsDir.getChildFile(id + "_part_a.json").existsAsFile());

    // Trigger another scan — saveLibraryEntries runs the non-partitioned
    // branch, which must clean up stale partition files at the top.
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning());

    EXPECT_FALSE(libsDir.getChildFile(id + "_part_a.json").existsAsFile());
    EXPECT_TRUE(libsDir.getChildFile(id + ".json").existsAsFile());
}

// G7: Sub-second mtime changes must be detected. ISO8601 string compare has
// 1-second granularity and missed them; the int64 modifiedTime comparison
// picks up a 1ms bump. We prove rescan by changing the note count — same
// file path, different content. Before the fix the entry was reused; after
// the fix the file is rescanned.
TEST_F(FileLibraryTest, IncrementalScanDetectsSubSecondChange) {
    auto midiDir = tempDir.getChildFile("subsec");
    midiDir.createDirectory();
    auto midiFile = midiDir.getChildFile("t.mid");

    // Writes N notes all at tick 0 (the proven pattern from ExtractMidiMetadata).
    // We delete first: JUCE's FileOutputStream opens with OPEN_ALWAYS and does
    // NOT truncate — re-writing the same path appends bytes and produces a
    // malformed MIDI stream.
    auto writeMidi = [&](int noteCount) {
        midiFile.deleteFile();
        juce::MidiMessageSequence seq;
        for (int i = 0; i < noteCount; ++i) {
            seq.addEvent(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8)80).withTimeStamp(0.0));
            seq.addEvent(juce::MidiMessage::noteOff(1, 60 + i).withTimeStamp(1.0));
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        {
            juce::FileOutputStream s(midiFile);
            file.writeTo(s);
            s.flush();
        }
    };

    writeMidi(1);
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("SubSec", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning());

    auto r1 = mgr.search("");
    ASSERT_EQ(r1.size(), 1u);
    EXPECT_EQ(r1[0].notes, 1);

    const juce::int64 originalMillis = midiFile.getLastModificationTime().toMilliseconds();

    // Rewrite with 3 notes, then bump mtime by 1ms — same ISO8601 second,
    // different int64. Before the fix the entry was reused (notes still 1);
    // after the fix the file is rescanned and notes reflects new content.
    writeMidi(3);
    ASSERT_TRUE(midiFile.setLastModificationTime(juce::Time(originalMillis + 1)));

    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning());

    auto r2 = mgr.search("");
    ASSERT_EQ(r2.size(), 1u);
    EXPECT_EQ(r2[0].notes, 3) << "sub-second mtime change should trigger rescan";
}

// G8: Audio key detection must produce a non-empty key. Before the
// (int)mag truncation fix, the chromagram was effectively all-zero and
// detectKey returned noise/empty. A full C-major scale (C D E F G A B)
// puts strong energy in pitch classes {0,2,4,5,7,9,11} — every note of
// the C major scale — and ~zero elsewhere, so the algorithm robustly
// classifies the sample as C. (A pure single-tone or triad suffers from
// FFT-bin leakage that creates phantom pitch classes, making the
// discriminator's choice noisy; a full scale is the unambiguous test
// signal for key detection.)
TEST_F(FileLibraryTest, AudioKeyDetectionProducesNonEmptyKey) {
    auto audioDir = tempDir.getChildFile("key_detect");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("cmajscale.wav");

    // 3 seconds of a C major scale (C4 D4 E4 F4 G4 A4 B4) played as a chord.
    constexpr double kPI = 3.14159265358979323846;
    constexpr double kSampleRate = 44100.0;
    const double freqs[] = {261.6256, 293.6648, 329.6276, 349.2282, 391.9954, 440.0000, 493.8833};
    constexpr int kNumSamples = (int)(3.0 * kSampleRate);
    juce::AudioBuffer<float> buffer(1, kNumSamples);
    for (int i = 0; i < kNumSamples; ++i) {
        double s = 0.0;
        for (double f : freqs)
            s += std::sin(2.0 * kPI * f * (double)i / kSampleRate);
        buffer.setSample(0, i, (float)(s / (double)(sizeof(freqs)/sizeof(freqs[0]))));
    }

    auto outStream = wavFile.createOutputStream();
    ASSERT_NE(outStream, nullptr);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(outStream.get(), kSampleRate, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    outStream.release();
    writer->writeFromAudioSampleBuffer(buffer, 0, kNumSamples);
    writer.reset();

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("CmajScale", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning());

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].key.isEmpty()) << "key detection must produce a non-empty key";
    EXPECT_TRUE(results[0].key.startsWith("C"))
        << "expected C-classification for a C major scale, got: " << results[0].key.toStdString();
}

// ── TimbreLib sidecar ingestion ──────────────────────────────────────────────
// An audio file with a <file>.timbre.json sidecar next to it gets
// entry.tags (dsp_words + top-3 captions + top-3 tags, comma-joined) and
// entry.description (prose). search() matches text through both fields.

TEST_F(FileLibraryTest, AudioSidecarIngestionPopulatesTagsAndDescription) {
    auto audioDir = tempDir.getChildFile("sidecar_ingest");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("beat.wav");

    // 1 second of silence at 44100 Hz, mono, 16-bit (proven writer pattern).
    {
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

    // Realistic TimbreLib sidecar: <wavname>.wav.timbre.json
    auto sidecar = audioDir.getChildFile("beat.wav.timbre.json");
    sidecar.replaceWithText(
        "{\n"
        "  \"dsp_words\": \"dark gritty pad\",\n"
        "  \"prose\": \"A brooding low synth atmosphere with a slow attack and a very long release tail, ideal for trailing drone beds.\",\n"
        "  \"captions\": [[\"dark pad\", 0.95], [\"gritty atmosphere\", 0.90], [\"slow attack pad\", 0.70], [\"bright chime\", 0.30]],\n"
        "  \"tags\": [[\"dark\", 0.98], [\"gritty\", 0.93], [\"pad\", 0.88], [\"bright\", 0.12]]\n"
        "}\n");

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Sidecar Audio", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);

    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].name, "beat.wav");

    // tags = dsp_words + top-3 captions by score + top-3 tags by score, comma-joined
    EXPECT_TRUE(results[0].tags.isNotEmpty());
    EXPECT_TRUE(results[0].tags.startsWith("dark gritty pad"));
    EXPECT_TRUE(results[0].tags.contains("dark pad"));
    EXPECT_TRUE(results[0].tags.contains("gritty atmosphere"));
    EXPECT_TRUE(results[0].tags.contains("slow attack pad"));
    EXPECT_TRUE(results[0].tags.contains("gritty"));
    // 4th caption ("bright chime", 0.30) is below the top-3 cutoff
    EXPECT_FALSE(results[0].tags.contains("bright chime"));
    EXPECT_EQ(results[0].description,
              "A brooding low synth atmosphere with a slow attack and a very long release tail, ideal for trailing drone beds.");

    // Search matches a word that lives only in tags (name/path/key don't have it).
    auto byTag = mgr.search("gritty");
    ASSERT_EQ(byTag.size(), 1u);
    EXPECT_EQ(byTag[0].name, "beat.wav");

    // Search matches a phrase that lives only in description/prose.
    auto byProse = mgr.search("very long release tail");
    ASSERT_EQ(byProse.size(), 1u);
    EXPECT_EQ(byProse[0].name, "beat.wav");
}

TEST_F(FileLibraryTest, SidecarMtimeBumpTriggersRescan) {
    auto audioDir = tempDir.getChildFile("sidecar_rescan");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("kick.wav");

    {
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
    auto sidecar = audioDir.getChildFile("kick.wav.timbre.json");
    sidecar.replaceWithText(
        "{\"dsp_words\":\"dark kick\",\"prose\":\"A dark kick drum.\","
        "\"captions\":[[\"kick\",0.9]],\"tags\":[[\"dark\",0.9],[\"kick\",0.8]]}");

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Sidecar Rescan", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";

    ASSERT_EQ(mgr.search("").size(), 1u);
    EXPECT_TRUE(mgr.search("")[0].tags.contains("dark kick"));

    // Audio file untouched; sidecar re-analyzed (newer than the audio).
    const juce::int64 audioMillis = wavFile.getLastModificationTime().toMilliseconds();
    sidecar.replaceWithText(
        "{\"dsp_words\":\"bright punchy kick\",\"prose\":\"A bright punchy kick drum.\","
        "\"captions\":[[\"punchy\",0.9]],\"tags\":[[\"bright\",0.9],[\"punchy\",0.8]]}");
    ASSERT_TRUE(sidecar.setLastModificationTime(juce::Time(audioMillis + 1)));

    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";

    ASSERT_EQ(mgr.search("").size(), 1u);
    auto after = mgr.search("");
    EXPECT_TRUE(after[0].tags.contains("bright punchy kick"))
        << "sidecar mtime bump must trigger rescan even though audio is unchanged";
    EXPECT_FALSE(after[0].tags.contains("dark kick"));
    // The refreshed tag word is searchable.
    auto byTag = mgr.search("punchy");
    ASSERT_EQ(byTag.size(), 1u);
    EXPECT_EQ(byTag[0].name, "kick.wav");
}

TEST_F(FileLibraryTest, SidecarTagsPersistAcrossInstances) {
    auto audioDir = tempDir.getChildFile("sidecar_persist");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("texture.wav");

    {
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
    auto sidecar = audioDir.getChildFile("texture.wav.timbre.json");
    sidecar.replaceWithText(
        "{\"dsp_words\":\"cold shimmer\",\"prose\":\"A cold shimmering texture with a long tail.\","
        "\"captions\":[[\"shimmer\",0.9]],\"tags\":[[\"cold\",0.9],[\"shimmer\",0.8]]}");

    // Instance 1: scan writes tags/description into the entry file.
    {
        HDAW::FileLibraryManager mgr(tempDir);
        auto id = mgr.addLibrary("Persist Sidecar", audioDir.getFullPathName(), "audio");
        mgr.scanLibrary(id);
        for (int i = 0; i < 50 && mgr.isScanning(); ++i)
            juce::Thread::sleep(100);
        ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";
        ASSERT_EQ(mgr.search("").size(), 1u);
        EXPECT_TRUE(mgr.search("")[0].tags.contains("cold shimmer"));
    } // mgr destroyed — saveLibraryEntries flushed tags/description to disk

    // Instance 2: NEW manager on same tempDir — no scan. search() lazy-loads
    // the persisted entry and must keep tags/description.
    HDAW::FileLibraryManager mgr2(tempDir);
    auto results = mgr2.search("");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].tags.contains("cold shimmer"))
        << "tags must survive persistence across manager instances (lazy load)";
    EXPECT_EQ(results[0].description, "A cold shimmering texture with a long tail.");
    EXPECT_EQ(mgr2.search("shimmer").size(), 1u);
}

TEST_F(FileLibraryTest, MissingSidecarTolerated) {
    auto audioDir = tempDir.getChildFile("sidecar_missing");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("plain.wav");

    {
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
    // NOTE: no .timbre.json sidecar written.

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("No Sidecar", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "plain.wav");
    EXPECT_TRUE(results[0].tags.isEmpty()) << "missing sidecar must leave tags empty";
    EXPECT_TRUE(results[0].description.isEmpty()) << "missing sidecar must leave description empty";
}

TEST_F(FileLibraryTest, MalformedSidecarTolerated) {
    auto audioDir = tempDir.getChildFile("sidecar_malformed");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("broken.wav");

    {
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
    auto sidecar = audioDir.getChildFile("broken.wav.timbre.json");
    sidecar.replaceWithText("{ this is not valid json !!!");

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Broken Sidecar", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id); // must not crash
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "broken.wav");
    EXPECT_TRUE(results[0].tags.isEmpty()) << "malformed sidecar must be tolerated (empty tags)";
    EXPECT_TRUE(results[0].description.isEmpty()) << "malformed sidecar must be tolerated (empty description)";
}

// ── TimbreLib dsp feature ingestion + cache schema (clustering v1.1) ────────
// docs/plans/2026-08-25-library-clustering.md, G3.

namespace {

bool writeSilentWav(const juce::File& wavFile, int samples = 44100) {
    auto outStream = wavFile.createOutputStream();
    if (outStream == nullptr) return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(outStream.get(), 44100.0, 1, 16, {}, 0));
    if (writer == nullptr) return false;
    outStream.release(); // writer takes ownership
    juce::AudioBuffer<float> buffer(1, samples);
    buffer.clear();
    writer->writeFromAudioSampleBuffer(buffer, 0, samples);
    writer.reset();
    return true;
}

// Sidecar JSON whose `dsp` dict carries all 20 keys (kDspFeatureKeys order
// does not matter to the parser — it looks keys up by name). Three dims carry
// the family signal; the rest are plausible constants.
juce::String sidecarWithDsp(const char* words, const char* prose,
                            double centroid, double melLow, double melHigh) {
    const auto num = [](double d) { return juce::String(d, 6); };
    return "{\"dsp_words\":\"" + juce::String(words) + "\","
           "\"prose\":\"" + juce::String(prose) + "\","
           "\"captions\":[[\"" + juce::String(words) + "\",0.9]],"
           "\"tags\":[[\"" + juce::String(words) + "\",0.9]],"
           "\"dsp\":{"
           "\"duration\":2.0,\"rms\":0.1,\"peak\":0.5,\"crest_dB\":12.0,\"zcr\":0.1,"
           "\"centroid\":" + num(centroid) + ",\"bandwidth\":800.0,"
           "\"rolloff85\":500.0,\"rolloff95\":2000.0,\"flatness\":0.05,"
           "\"spectral_crest\":300.0,\"spec_irregularity\":0.2,"
           "\"mel_low\":" + num(melLow) + ",\"mel_mid\":0.2,"
           "\"mel_high\":" + num(melHigh) + ",\"attack_s\":0.01,\"decay_s\":0.5,"
           "\"f0_hz\":0.0,\"tonal_fraction\":0.0,\"f0_sweep\":9.0}}";
}

// Same sidecar minus the LAST dsp key (19 of 20) — must leave dspFeatures empty.
juce::String sidecarMissingLastDspKey(const char* words, const char* prose) {
    auto full = sidecarWithDsp(words, prose, 5000.0, 0.1, 0.6);
    return full.replace("\"tonal_fraction\":0.0,\"f0_sweep\":9.0", "\"tonal_fraction\":0.0");
}

} // namespace

// dspFeatures ingested from the sidecar `dsp` dict — all 20 keys required.
TEST_F(FileLibraryTest, SidecarDspFeaturesIngestedWhenAllTwentyKeysPresent) {
    static_assert(HDAW::kDspFeatureCount == 20, "dsp contract is 20 keys");
    EXPECT_EQ(juce::String(HDAW::kDspFeatureKeys[5]), "centroid");
    EXPECT_EQ(juce::String(HDAW::kDspFeatureKeys[12]), "mel_low");

    auto audioDir = tempDir.getChildFile("sidecar_dsp");
    audioDir.createDirectory();
    ASSERT_TRUE(writeSilentWav(audioDir.getChildFile("full.wav")));
    ASSERT_TRUE(writeSilentWav(audioDir.getChildFile("partial.wav")));
    audioDir.getChildFile("full.wav.timbre.json").replaceWithText(
        sidecarWithDsp("dark low pad", "A dark low pad with a slow attack.", 200.0, 0.8, 0.05));
    audioDir.getChildFile("partial.wav.timbre.json").replaceWithText(
        sidecarMissingLastDspKey("bright high chime", "A bright high chime."));

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Dsp Ingest", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].name, "full.wav");
    EXPECT_EQ(results[1].name, "partial.wav");

    // All-20-keys sidecar -> full vector, values by key.
    ASSERT_EQ(results[0].dspFeatures.size(), (size_t)HDAW::kDspFeatureCount);
    EXPECT_DOUBLE_EQ(results[0].dspFeatures[5], 200.0);   // centroid
    EXPECT_DOUBLE_EQ(results[0].dspFeatures[12], 0.8);    // mel_low
    EXPECT_DOUBLE_EQ(results[0].dspFeatures[14], 0.05);   // mel_high
    EXPECT_DOUBLE_EQ(results[0].dspFeatures[19], 9.0);    // f0_sweep

    // 19-of-20 sidecar -> text fields still ingest, but dsp stays empty
    // (no partial vectors, no imputation).
    EXPECT_EQ(results[1].dspFeatures.size(), 0u);
    EXPECT_TRUE(results[1].tags.contains("bright high chime"));
}

// dspFeatures round-trip through save/load (schemaVersion 2 entry cache).
TEST_F(FileLibraryTest, SidecarDspFeaturesRoundTripAcrossInstances) {
    auto audioDir = tempDir.getChildFile("sidecar_dsp_persist");
    audioDir.createDirectory();
    ASSERT_TRUE(writeSilentWav(audioDir.getChildFile("texture.wav")));
    audioDir.getChildFile("texture.wav.timbre.json").replaceWithText(
        sidecarWithDsp("cold shimmer", "A cold shimmering texture with a long tail.",
                       2400.0, 0.45, 0.25));

    {
        HDAW::FileLibraryManager mgr(tempDir);
        auto id = mgr.addLibrary("Dsp Persist", audioDir.getFullPathName(), "audio");
        mgr.scanLibrary(id);
        for (int i = 0; i < 50 && mgr.isScanning(); ++i)
            juce::Thread::sleep(100);
        ASSERT_FALSE(mgr.isScanning());
        auto inMemory = mgr.search("");
        ASSERT_EQ(inMemory.size(), 1u);
        ASSERT_EQ(inMemory[0].dspFeatures.size(), (size_t)HDAW::kDspFeatureCount);
        // The cache file carries schemaVersion 2 + the dspFeatures array.
        auto cache = tempDir.getChildFile("libraries").getChildFile(id + ".json");
        ASSERT_TRUE(cache.existsAsFile());
        auto cacheText = cache.loadFileAsString();
        EXPECT_TRUE(cacheText.contains("schemaVersion"));
        EXPECT_TRUE(cacheText.contains("dspFeatures"));
    }

    // New manager instance lazy-loads from disk — no scan.
    HDAW::FileLibraryManager mgr2(tempDir);
    auto results = mgr2.search("");
    ASSERT_EQ(results.size(), 1u);
    ASSERT_EQ(results[0].dspFeatures.size(), (size_t)HDAW::kDspFeatureCount)
        << "dspFeatures must survive save/load";
    EXPECT_DOUBLE_EQ(results[0].dspFeatures[5], 2400.0); // centroid
    EXPECT_DOUBLE_EQ(results[0].dspFeatures[12], 0.45);  // mel_low
    EXPECT_DOUBLE_EQ(results[0].dspFeatures[14], 0.25);  // mel_high
}

// A per-library entry cache with schemaVersion < 2 (or missing) is ignored so
// ONE rescan re-ingests with the current parser.
TEST_F(FileLibraryTest, StaleSchemaVersionCacheIgnoredUntilRescan) {
    auto audioDir = tempDir.getChildFile("schema_v1");
    audioDir.createDirectory();
    ASSERT_TRUE(writeSilentWav(audioDir.getChildFile("pad.wav")));
    audioDir.getChildFile("pad.wav.timbre.json").replaceWithText(
        sidecarWithDsp("dark pad", "A dark pad.", 300.0, 0.7, 0.1));

    juce::String id;
    {
        HDAW::FileLibraryManager mgr(tempDir);
        id = mgr.addLibrary("Schema V1", audioDir.getFullPathName(), "audio");
        mgr.scanLibrary(id);
        for (int i = 0; i < 50 && mgr.isScanning(); ++i)
            juce::Thread::sleep(100);
        ASSERT_FALSE(mgr.isScanning());
        ASSERT_EQ(mgr.search("").size(), 1u);

        // Downgrade the cache to the v1 shape: strip schemaVersion, keep entries.
        auto cache = tempDir.getChildFile("libraries").getChildFile(id + ".json");
        auto parsed = juce::JSON::parse(cache.loadFileAsString());
        auto* obj = parsed.getDynamicObject();
        ASSERT_NE(obj, nullptr);
        obj->removeProperty("schemaVersion");
        cache.replaceWithText(juce::JSON::toString(parsed));
    }

    // New manager: the v1 cache must be IGNORED (no stale entries served).
    HDAW::FileLibraryManager mgr2(tempDir);
    EXPECT_EQ(mgr2.search("").size(), 0u)
        << "schemaVersion < 2 cache must be ignored, not served";

    // One rescan re-ingests from the source files (sidecar dsp included).
    mgr2.scanLibrary(id);
    for (int i = 0; i < 50 && mgr2.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr2.isScanning());

    auto results = mgr2.search("");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "pad.wav");
    ASSERT_EQ(results[0].dspFeatures.size(), (size_t)HDAW::kDspFeatureCount);
    EXPECT_DOUBLE_EQ(results[0].dspFeatures[5], 300.0);
}

// ── cluster presets (docs/plans/2026-08-25-cluster-presets.md, increment 2) ─
// Integration through the manager: saveAs persists a preset, get returns it,
// refresh recomputes equal (deterministic bridge over the stored recipe).
// Reuses writeSilentWav + sidecarWithDsp from the anonymous namespace above.

namespace {

// Two timbre families (dark vs bright) x 2 entries each, with dsp sidecars.
void seedClusterFixture(const juce::File& dir) {
    dir.createDirectory();
    ASSERT_TRUE(writeSilentWav(dir.getChildFile("dark1.wav")));
    ASSERT_TRUE(writeSilentWav(dir.getChildFile("dark2.wav")));
    ASSERT_TRUE(writeSilentWav(dir.getChildFile("bright1.wav")));
    ASSERT_TRUE(writeSilentWav(dir.getChildFile("bright2.wav")));
    dir.getChildFile("dark1.wav.timbre.json").replaceWithText(
        sidecarWithDsp("dark, low", "a dark low texture", 150.0, 0.75, 0.05));
    dir.getChildFile("dark2.wav.timbre.json").replaceWithText(
        sidecarWithDsp("dark, low", "a dark low texture", 160.0, 0.73, 0.06));
    dir.getChildFile("bright1.wav.timbre.json").replaceWithText(
        sidecarWithDsp("bright, high", "a bright high texture", 5200.0, 0.05, 0.70));
    dir.getChildFile("bright2.wav.timbre.json").replaceWithText(
        sidecarWithDsp("bright, high", "a bright high texture", 5400.0, 0.04, 0.72));
}

void waitForScan(HDAW::FileLibraryManager& mgr) {
    for (int i = 0; i < 50 && mgr.isScanning(); ++i)
        juce::Thread::sleep(100);
    ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";
}

// Total members across clusters + unassigned.
int outcomeMemberCount(const HDAW::ClusterOutcome& o) {
    int count = 0;
    for (const auto& c : o.clusters) count += (int)c.members.size();
    count += (int)o.unassigned.size();
    return count;
}

// Callable before ClusterPresetStore is included? It's already in scope via
// FileLibraryManager.h -> ClusterPresetStore.h.
int freshEntryCountFor(const HDAW::ClusterOutcome& o) {
    return outcomeMemberCount(o);
}

} // namespace

TEST_F(FileLibraryTest, SaveAsPresetPersistsAndGetReturnsIt) {
    auto audioDir = tempDir.getChildFile("preset_fixture");
    seedClusterFixture(audioDir);

    juce::String presetId;
    {
        HDAW::FileLibraryManager mgr(tempDir);
        auto id = mgr.addLibrary("PresetLib", audioDir.getFullPathName(), "audio");
        mgr.scanLibrary(id);
        waitForScan(mgr);

        juce::String error;
        auto outcome = mgr.clusterLibrary(juce::StringArray{id}, 2, "hybrid", error,
                                          "Favourite Clusters", {}, &presetId);
        ASSERT_TRUE(error.isEmpty()) << error.toStdString();
        EXPECT_FALSE(presetId.isEmpty());
        EXPECT_TRUE(presetId.startsWith("cp_"));

        HDAW::ClusterPreset preset;
        ASSERT_TRUE(mgr.getClusterPreset(presetId, preset, error));
        EXPECT_EQ(preset.name, "Favourite Clusters");
        EXPECT_EQ(preset.libraryIds.size(), 1);
        EXPECT_EQ(preset.libraryIds[0], id);
        EXPECT_EQ(preset.method, "hybrid");
        EXPECT_EQ(preset.k, 2);
        EXPECT_TRUE(preset.clusterId.isEmpty());
        ASSERT_EQ(preset.clusters.size(), 2u);
        int members = 0;
        for (const auto& c : preset.clusters) {
            members += (int)c.members.size();
            for (const auto& m : c.members) {
                EXPECT_FALSE(m.name.isEmpty());
                EXPECT_FALSE(m.path.isEmpty());
                EXPECT_TRUE(m.tags.contains("dark") || m.tags.contains("bright"));
            }
        }
        EXPECT_EQ(members, 4);
        EXPECT_EQ(preset.entryCount, 4);
    }

    // New manager instance on the same tempDir: preset persists on disk.
    HDAW::FileLibraryManager mgr2(tempDir);
    HDAW::ClusterPreset preset;
    juce::String error;
    ASSERT_TRUE(mgr2.getClusterPreset(presetId, preset, error))
        << "preset must survive persistence across manager instances: " << error.toStdString();
    EXPECT_EQ(preset.name, "Favourite Clusters");
    EXPECT_EQ(preset.entryCount, 4);

    // Unknown id -> error, never a crash.
    HDAW::ClusterPreset ignored;
    EXPECT_FALSE(mgr2.getClusterPreset("cp_00000000", ignored, error));
    EXPECT_FALSE(error.isEmpty());
}

TEST_F(FileLibraryTest, SaveAsSingleClusterStoresOnlyThatCluster) {
    auto audioDir = tempDir.getChildFile("preset_single");
    seedClusterFixture(audioDir);

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("PresetSingleLib", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);
    waitForScan(mgr);

    juce::String error;
    // Save the whole result first to discover a cluster id.
    auto whole = mgr.clusterLibrary(juce::StringArray{id}, 2, "hybrid", error);
    ASSERT_TRUE(error.isEmpty());
    ASSERT_FALSE(whole.clusters.empty());
    const juce::String clusterId = whole.clusters[0].id;

    juce::String presetId;
    auto outcome = mgr.clusterLibrary(juce::StringArray{id}, 2, "hybrid", error,
                                      "Single Cluster", clusterId, &presetId);
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    EXPECT_FALSE(presetId.isEmpty());
    EXPECT_EQ(outcomeMemberCount(outcome), 4) << "returned clusters unchanged by narrowing";

    HDAW::ClusterPreset preset;
    ASSERT_TRUE(mgr.getClusterPreset(presetId, preset, error));
    EXPECT_EQ(preset.clusterId, clusterId);
    ASSERT_EQ(preset.clusters.size(), 1u);
    EXPECT_EQ(preset.clusters[0].id, clusterId);
    EXPECT_EQ(preset.clusters[0].members.size(), 2u) << "only that cluster's members";
    EXPECT_TRUE(preset.unassigned.empty()) << "unassigned omitted on single-cluster save";
    EXPECT_EQ(preset.entryCount, 2);

    // An unknown cluster id fails the whole save request with an error.
    juce::String presetId2;
    auto bad = mgr.clusterLibrary(juce::StringArray{id}, 2, "hybrid", error,
                                  "Bad Save", "c99", &presetId2);
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(error.contains("c99"));
    EXPECT_TRUE(bad.clusters.empty());
    EXPECT_TRUE(presetId2.isEmpty());
}

TEST_F(FileLibraryTest, RefreshRecomputesEqualDeterministically) {
    auto audioDir = tempDir.getChildFile("preset_refresh");
    seedClusterFixture(audioDir);

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("PresetRefreshLib", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);
    waitForScan(mgr);

    juce::String error;
    juce::String presetId;
    auto original = mgr.clusterLibrary(juce::StringArray{id}, 2, "hybrid", error,
                                       "Refreshable", {}, &presetId);
    ASSERT_TRUE(error.isEmpty());

    HDAW::ClusterPreset stored;
    HDAW::ClusterOutcome fresh;
    ASSERT_TRUE(mgr.refreshClusterPreset(presetId, stored, fresh, error))
        << error.toStdString();

    EXPECT_EQ(stored.id, presetId);
    EXPECT_EQ(stored.name, "Refreshable");
    EXPECT_EQ(fresh.method, original.method);
    EXPECT_EQ(fresh.k, original.k);
    EXPECT_EQ(fresh.clusters.size(), original.clusters.size());
    EXPECT_EQ(fresh.unassigned.size(), original.unassigned.size());
    EXPECT_EQ(freshEntryCountFor(fresh), freshEntryCountFor(original));

    for (size_t i = 0; i < original.clusters.size(); ++i) {
        ASSERT_EQ(fresh.clusters[i].members.size(), original.clusters[i].members.size());
        for (size_t j = 0; j < original.clusters[i].members.size(); ++j) {
            EXPECT_EQ(fresh.clusters[i].members[j].name, original.clusters[i].members[j].name);
            EXPECT_NEAR(fresh.clusters[i].members[j].similarity,
                        original.clusters[i].members[j].similarity, 1e-12)
                << "refresh must reproduce the original similarities (deterministic clamp)";
        }
    }

    // Missing-member staleness probe: all fixture files still exist -> 0.
    EXPECT_EQ(mgr.countMissingPresetMembers(stored), 0);

    // Unknown id refresh -> error.
    HDAW::ClusterPreset ignPreset;
    HDAW::ClusterOutcome ignOutcome;
    EXPECT_FALSE(mgr.refreshClusterPreset("cp_00000000", ignPreset, ignOutcome, error));
    EXPECT_FALSE(error.isEmpty());
}
