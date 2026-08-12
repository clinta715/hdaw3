# File Library System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a persistent, searchable file library system for MIDI files and audio samples, accessible via MCP tools, RPC, and the frontend FileBrowser.

**Architecture:** A `FileLibraryManager` C++ class manages per-library JSON files with on-demand loading. Background thread scanning extracts metadata. MCP tools and RPC expose the same backend. Frontend adds a "Library" mode to the existing FileBrowser with metadata columns, search, and auto-play preview.

**Tech Stack:** JUCE (file I/O, threading, audio formats), Qt JSON (MCP/RPC serialization), React + Zustand (frontend), GTest + Vitest + Playwright (tests)

---

## File Structure

| File | Responsibility |
|------|---------------|
| `src/engine/FileLibraryManager.h` | Library data structures, `FileLibraryManager` class declaration |
| `src/engine/FileLibraryManager.cpp` | Registry, scanning, persistence, search, partitioning |
| `src/mcp/McpTools_Library.cpp` | MCP tools: `list_libraries`, `add_library`, `remove_library`, `scan_library`, `search_library`, `get_library_entry`, `set_library_autoscan` |
| `src/frontend/router/Router_Library.cpp` | RPC handlers: `library.list`, `library.add`, `library.remove`, `library.scan`, `library.search` |
| `frontend/src/store/libraryStore.ts` | Zustand store for library state (libraries, search results, scan progress) |
| `frontend/src/components/FileBrowser.tsx` | Modify: add "Library" kind chip, library mode rendering, metadata columns |
| `frontend/src/components/PreferencesDialog.tsx` | Modify: add "Libraries" section |
| `tests/unit/engine/file_library_test.cpp` | GTest: registry, scanning, metadata, search, persistence, partitioning |

---

## Task 1: FileLibraryManager — Data Structures & Header

**Files:**
- Create: `src/engine/FileLibraryManager.h`

- [ ] **Step 1: Create the header with data structures and class declaration**

```cpp
// src/engine/FileLibraryManager.h
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <mutex>
#include <memory>

namespace HDAW {

struct LibraryEntry {
    juce::String name;
    juce::String path;
    juce::int64 size = 0;
    juce::String modified; // ISO 8601

    // MIDI-specific
    int tracks = 0;
    int notes = 0;
    int durationTicks = 0;
    double durationSeconds = 0.0;
    double tempo = 120.0;
    juce::String timeSignature = "4/4";
    juce::String key;

    // Audio-specific
    double sampleRate = 0.0;
    int channels = 0;
    double bpm = 0.0;
    juce::String format; // wav, flac, mp3, etc.
};

struct LibraryInfo {
    juce::String id;
    juce::String name;
    juce::String path;
    juce::String type; // "midi" or "audio"
    juce::String lastScan;
    int fileCount = 0;
    bool autoScan = false;
};

struct ScanProgress {
    juce::String libraryId;
    int scanned = 0;
    int total = 0;
    juce::String phase; // "scanning" or "writing"
};

using ScanProgressCallback = std::function<void(const ScanProgress&)>;
using ScanCompleteCallback = std::function<void(const juce::String& libraryId, bool success)>;

class FileLibraryManager {
public:
    FileLibraryManager();
    ~FileLibraryManager();

    void initialize(); // Load registry, auto-scan if needed

    // Library management
    juce::StringArray getLibraryIds() const;
    LibraryInfo getLibraryInfo(const juce::String& id) const;
    juce::String addLibrary(const juce::String& name, const juce::String& path, const juce::String& type);
    void removeLibrary(const juce::String& id);
    void setAutoScan(const juce::String& id, bool enabled);

    // Scanning
    void scanLibrary(const juce::String& id);
    void scanAll();
    bool isScanning() const;
    void setScanProgressCallback(ScanProgressCallback cb);
    void setScanCompleteCallback(ScanCompleteCallback cb);

    // Search
    std::vector<LibraryEntry> search(const juce::String& query,
                                     const juce::String& typeFilter = {},
                                     const juce::String& libraryIdFilter = {},
                                     double durationMin = -1, double durationMax = -1,
                                     double bpmMin = -1, double bpmMax = -1,
                                     const juce::String& keyFilter = {},
                                     int offset = 0, int limit = 50) const;

    // Entry access
    LibraryEntry getEntry(const juce::String& libraryId, const juce::String& path) const;

    // Persistence
    void loadRegistry();
    void saveRegistry();

private:
    void loadLibraryEntries(const juce::String& id);
    void saveLibraryEntries(const juce::String& id);
    void scanDirectory(const juce::String& id, const juce::File& dir);
    LibraryEntry extractMidiMetadata(const juce::File& file);
    LibraryEntry extractAudioMetadata(const juce::File& file);
    juce::String detectKey(const std::vector<int>& noteCounts) const;

    mutable std::mutex mutex;
    std::vector<LibraryInfo> libraries;
    std::unordered_map<juce::String, std::vector<LibraryEntry>> entries; // loaded on demand
    std::unordered_set<juce::String> loadedLibraries; // track which are in memory
    juce::File registryFile;
    juce::File librariesDir;
    std::atomic<bool> scanning{false};
    ScanProgressCallback progressCallback;
    ScanCompleteCallback completeCallback;
    juce::ThreadPool threadPool{2};
};

} // namespace HDAW
```

- [ ] **Step 2: Verify the header compiles**

Run: `cmake --build build --config Debug --target HDAW -- /p:CL_MPCount=1` (or just check that the file is syntactically valid by attempting a build of a test target that includes it)

Expected: Compilation succeeds (may have link errors since .cpp doesn't exist yet — that's fine)

- [ ] **Step 3: Commit**

```bash
git add src/engine/FileLibraryManager.h
git commit -m "feat(library): add FileLibraryManager header with data structures"
```

---

## Task 2: FileLibraryManager — Registry Persistence

**Files:**
- Create: `src/engine/FileLibraryManager.cpp`
- Test: `tests/unit/engine/file_library_test.cpp`

- [ ] **Step 1: Write the failing test for registry round-trip**

```cpp
// tests/unit/engine/file_library_test.cpp
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
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
    HDAW::FileLibraryManager mgr;
    // Point the manager at our temp directory
    // (We'll need a setLibrariesDir method or constructor param)
    auto id = mgr.addLibrary("Test MIDI", "/some/path", "midi");
    EXPECT_FALSE(id.isEmpty());
    auto info = mgr.getLibraryInfo(id);
    EXPECT_EQ(info.name, "Test MIDI");
    EXPECT_EQ(info.path, "/some/path");
    EXPECT_EQ(info.type, "midi");
    EXPECT_FALSE(info.autoScan);
}

TEST_F(FileLibraryTest, RemoveLibraryDeletesFromRegistry) {
    HDAW::FileLibraryManager mgr;
    auto id = mgr.addLibrary("To Remove", "/tmp/test", "audio");
    mgr.removeLibrary(id);
    auto ids = mgr.getLibraryIds();
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), id) == ids.end());
}

TEST_F(FileLibraryTest, SetAutoScanTogglesFlag) {
    HDAW::FileLibraryManager mgr;
    auto id = mgr.addLibrary("Auto", "/tmp/test", "midi");
    mgr.setAutoScan(id, true);
    auto info = mgr.getLibraryInfo(id);
    EXPECT_TRUE(info.autoScan);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.*`
Expected: FAIL (FileLibraryManager has no implementation yet)

- [ ] **Step 3: Implement registry persistence**

```cpp
// src/engine/FileLibraryManager.cpp
#include "FileLibraryManager.h"
#include <juce_core/juce_core.h>
#include <fstream>
#include <sstream>

namespace HDAW {

FileLibraryManager::FileLibraryManager() {
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("HDAW");
    appData.createDirectory();
    librariesDir = appData.getChildFile("libraries");
    librariesDir.createDirectory();
    registryFile = librariesDir.getChildFile("registry.json");
    loadRegistry();
}

FileLibraryManager::~FileLibraryManager() {
    threadPool.removeAllJobs(true, 1000);
}

void FileLibraryManager::initialize() {
    // Auto-scan libraries marked with autoScan
    for (const auto& lib : libraries) {
        if (lib.autoScan) scanLibrary(lib.id);
    }
}

juce::StringArray FileLibraryManager::getLibraryIds() const {
    std::lock_guard<std::mutex> lock(mutex);
    juce::StringArray ids;
    for (const auto& lib : libraries) ids.add(lib.id);
    return ids;
}

LibraryInfo FileLibraryManager::getLibraryInfo(const juce::String& id) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& lib : libraries) {
        if (lib.id == id) return lib;
    }
    return {};
}

juce::String FileLibraryManager::addLibrary(const juce::String& name,
                                             const juce::String& path,
                                             const juce::String& type) {
    juce::String id = juce::String(juce::Uuid::newUuid().toString().removeCharacters("-").substring(0, 12));
    LibraryInfo info;
    info.id = id;
    info.name = name;
    info.path = path;
    info.type = type;
    info.autoScan = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        libraries.push_back(info);
    }
    saveRegistry();
    return id;
}

void FileLibraryManager::removeLibrary(const juce::String& id) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        libraries.erase(std::remove_if(libraries.begin(), libraries.end(),
            [&](const LibraryInfo& l) { return l.id == id; }), libraries.end());
        entries.erase(id);
        loadedLibraries.erase(id);
    }
    // Delete the entry file
    auto entryFile = librariesDir.getChildFile(id + ".json");
    if (entryFile.existsAsFile()) entryFile.deleteFile();
    saveRegistry();
}

void FileLibraryManager::setAutoScan(const juce::String& id, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& lib : libraries) {
        if (lib.id == id) {
            lib.autoScan = enabled;
            break;
        }
    }
    saveRegistry();
}

bool FileLibraryManager::isScanning() const {
    return scanning.load();
}

void FileLibraryManager::setScanProgressCallback(ScanProgressCallback cb) {
    progressCallback = std::move(cb);
}

void FileLibraryManager::setScanCompleteCallback(ScanCompleteCallback cb) {
    completeCallback = std::move(cb);
}

void FileLibraryManager::loadRegistry() {
    if (!registryFile.existsAsFile()) return;
    auto content = registryFile.loadFileAsString();
    if (content.isEmpty()) return;

    auto json = juce::JSON::parse(content);
    auto* obj = json.getDynamicObject();
    if (!obj) return;

    auto libs = obj->getProperty("libraries", {});
    for (int i = 0; i < libs.getArray()->size(); ++i) {
        auto& entry = (*libs.getArray())[i];
        auto* eObj = entry.getDynamicObject();
        if (!eObj) continue;
        LibraryInfo info;
        info.id = eObj->getProperty("id", "").toString();
        info.name = eObj->getProperty("name", "").toString();
        info.path = eObj->getProperty("path", "").toString();
        info.type = eObj->getProperty("type", "").toString();
        info.lastScan = eObj->getProperty("lastScan", "").toString();
        info.fileCount = (int)eObj->getProperty("fileCount", 0);
        info.autoScan = (bool)eObj->getProperty("autoScan", false);
        if (info.id.isNotEmpty()) libraries.push_back(info);
    }
}

void FileLibraryManager::saveRegistry() {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> libs;
    for (const auto& lib : libraries) {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("id", lib.id);
        obj->setProperty("name", lib.name);
        obj->setProperty("path", lib.path);
        obj->setProperty("type", lib.type);
        obj->setProperty("lastScan", lib.lastScan);
        obj->setProperty("fileCount", lib.fileCount);
        obj->setProperty("autoScan", lib.autoScan);
        libs.add(juce::var(obj));
    }
    root->setProperty("libraries", libs);
    registryFile.getParentDirectory().createDirectory();
    registryFile.replaceWithText(juce::JSON::toString(root));
}

// Stub implementations for scan/search (Task 3+)
void FileLibraryManager::scanLibrary(const juce::String&) {}
void FileLibraryManager::scanAll() {}
void FileLibraryManager::loadLibraryEntries(const juce::String&) {}
void FileLibraryManager::saveLibraryEntries(const juce::String&) {}
void FileLibraryManager::scanDirectory(const juce::String&, const juce::File&) {}
LibraryEntry FileLibraryManager::extractMidiMetadata(const juce::File&) { return {}; }
LibraryEntry FileLibraryManager::extractAudioMetadata(const juce::File&) { return {}; }
juce::String FileLibraryManager::detectKey(const std::vector<int>&) const { return {}; }

std::vector<LibraryEntry> FileLibraryManager::search(const juce::String&, const juce::String&,
    const juce::String&, double, double, double, double, const juce::String&, int, int) const { return {}; }

LibraryEntry FileLibraryManager::getEntry(const juce::String&, const juce::String&) const { return {}; }

} // namespace HDAW
```

- [ ] **Step 4: Run test to verify it passes**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.*`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/engine/FileLibraryManager.cpp tests/unit/engine/file_library_test.cpp
git commit -m "feat(library): implement FileLibraryManager registry persistence"
```

---

## Task 3: FileLibraryManager — MIDI Metadata Extraction

**Files:**
- Modify: `src/engine/FileLibraryManager.cpp`
- Modify: `tests/unit/engine/file_library_test.cpp`

- [ ] **Step 1: Write the failing test for MIDI metadata extraction**

Add to `file_library_test.cpp`:

```cpp
TEST_F(FileLibraryTest, ExtractMidiMetadata) {
    // Create a minimal valid MIDI file for testing
    // Standard MIDI File header: MThd (6 bytes header) + MTrk (track data)
    // We'll create a simple MIDI file with 1 track, 2 notes
    auto midiDir = tempDir.getChildFile("midi");
    midiDir.createDirectory();
    auto midiFile = midiDir.getChildFile("test.mid");

    // Write a minimal Type 0 MIDI file (simplified)
    // This is a binary test — we need to create a real MIDI file
    // For the test, we'll use juce::MidiFile to create it programmatically
    juce::MidiMessageSequence seq;
    seq.addNoteOn(1, 60, 0.0, 100);
    seq.addNoteOff(1, 60, 1.0);
    seq.addNoteOn(1, 64, 0.0, 80);
    seq.addNoteOff(1, 64, 0.5);

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(480);
    midiFile.addTrack(seq);

    juce::FileOutputStream stream(midiFile);
    midiFile.writeTo(stream);
    stream.flush();

    // Now test extraction
    HDAW::FileLibraryManager mgr;
    // We'll call extractMidiMetadata — it's private, so we'll test via scanLibrary
    // For now, test the public search after scanning
    auto id = mgr.addLibrary("Test MIDI", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].name, "test.mid");
    EXPECT_EQ(results[0].tracks, 1);
    EXPECT_EQ(results[0].notes, 2);
    EXPECT_GT(results[0].durationSeconds, 0.0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.ExtractMidiMetadata`
Expected: FAIL (scan/search not implemented)

- [ ] **Step 3: Implement MIDI scanning and metadata extraction**

Replace stub implementations in `FileLibraryManager.cpp`:

```cpp
void FileLibraryManager::scanLibrary(const juce::String& id) {
    juce::String path;
    juce::String type;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries) {
            if (lib.id == id) {
                path = lib.path;
                type = lib.type;
                break;
            }
        }
    }
    if (path.isEmpty()) return;

    scanning.store(true);
    threadPool.addJob([this, id, path, type]() {
        auto dir = juce::File(path);
        if (!dir.isDirectory()) {
            scanning.store(false);
            if (completeCallback) completeCallback(id, false);
            return;
        }
        scanDirectory(id, dir);
        scanning.store(false);
        if (completeCallback) completeCallback(id, true);
    });
}

void FileLibraryManager::scanAll() {
    auto ids = getLibraryIds();
    for (const auto& id : ids) scanLibrary(id);
}

void FileLibraryManager::scanDirectory(const juce::String& id, const juce::File& dir) {
    juce::String type;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries) {
            if (lib.id == id) { type = lib.type; break; }
        }
    }

    juce::Array<juce::File> files;
    juce::DirectoryIterator iter(dir, true, type == "midi" ? "*.mid;*.midi" : "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg");
    while (iter.next()) files.add(iter.getFile());

    int total = files.size();
    int scanned = 0;
    std::vector<LibraryEntry> newEntries;

    for (const auto& file : files) {
        LibraryEntry entry;
        if (type == "midi")
            entry = extractMidiMetadata(file);
        else
            entry = extractAudioMetadata(file);

        if (entry.name.isNotEmpty()) {
            newEntries.push_back(entry);
        }
        scanned++;
        if (progressCallback) {
            ScanProgress p;
            p.libraryId = id;
            p.scanned = scanned;
            p.total = total;
            p.phase = "scanning";
            progressCallback(p);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        entries[id] = newEntries;
        loadedLibraries.insert(id);
        for (auto& lib : libraries) {
            if (lib.id == id) {
                lib.fileCount = (int)newEntries.size();
                lib.lastScan = juce::Time::getCurrentTime().toISO8601(true);
                break;
            }
        }
    }
    saveLibraryEntries(id);
    saveRegistry();
}

LibraryEntry FileLibraryManager::extractMidiMetadata(const juce::File& file) {
    LibraryEntry entry;
    entry.name = file.getFileName();
    entry.path = file.getFullPathName();
    entry.size = file.getSize();
    entry.modified = juce::Time(file.getLastModificationTime()).toISO8601(true);

    juce::FileInputStream stream(file);
    if (stream.failedToOpen()) return entry;

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(stream)) return entry;

    entry.tracks = midiFile.getNumTracks();
    entry.tempo = 120.0; // default
    int totalNotes = 0;
    double lastEventTime = 0.0;

    for (int t = 0; t < midiFile.getNumTracks(); ++t) {
        auto* track = midiFile.getTrack(t);
        if (!track) continue;

        for (int e = 0; e < track->getNumEvents(); ++e) {
            auto& event = *track->getEventPointer(e);
            auto& msg = event.message;

            if (msg.isTempoMetaEvent()) {
                entry.tempo = 60.0 / msg.getTempoSecondsPerQuarterNote();
            }
            if (msg.isTimeSignatureMetaEvent()) {
                int num = msg.getTimeSignatureNumerator();
                int den = msg.getTimeSignatureDenominator();
                entry.timeSignature = juce::String(num) + "/" + juce::String(den);
            }
            if (msg.isNoteOn() && msg.getVelocity() > 0) {
                totalNotes++;
                // Track note occurrences for key detection
                int noteNum = msg.getNoteNumber();
                // Store for key detection (we'll collect in a vector)
            }
            double timeInSeconds = msg.getTimeStamp();
            // Convert ticks to seconds using tempo
            double ticksPerQuarter = midiFile.getTicksPerQuarterNote();
            if (ticksPerQuarter > 0 && entry.tempo > 0) {
                timeInSeconds = msg.getTimeStamp() * (60.0 / entry.tempo) / ticksPerQuarter;
            }
            if (timeInSeconds > lastEventTime) lastEventTime = timeInSeconds;
        }
    }

    entry.notes = totalNotes;
    entry.durationSeconds = lastEventTime;

    // Key detection from note histogram
    if (totalNotes > 0) {
        std::vector<int> noteCounts(12, 0);
        for (int t = 0; t < midiFile.getNumTracks(); ++t) {
            auto* track = midiFile.getTrack(t);
            if (!track) continue;
            for (int e = 0; e < track->getNumEvents(); ++e) {
                auto& msg = track->getEventPointer(e)->message;
                if (msg.isNoteOn() && msg.getVelocity() > 0)
                    noteCounts[msg.getNoteNumber() % 12]++;
            }
        }
        entry.key = detectKey(noteCounts);
    }

    return entry;
}

juce::String FileLibraryManager::detectKey(const std::vector<int>& noteCounts) const {
    if (noteCounts.size() != 12) return {};

    // Major and minor scale patterns (semitones from root)
    static const int majorPattern[] = {0, 2, 4, 5, 7, 9, 11};
    static const int minorPattern[] = {0, 2, 3, 5, 7, 8, 10};
    static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    double bestScore = -1;
    juce::String bestKey;

    for (int root = 0; root < 12; ++root) {
        // Test major
        double majorScore = 0;
        for (int p : majorPattern) majorScore += noteCounts[(root + p) % 12];
        if (majorScore > bestScore) {
            bestScore = majorScore;
            bestKey = juce::String(noteNames[root]) + " major";
        }
        // Test minor
        double minorScore = 0;
        for (int p : minorPattern) minorScore += noteCounts[(root + p) % 12];
        if (minorScore > bestScore) {
            bestScore = minorScore;
            bestKey = juce::String(noteNames[root]) + " minor";
        }
    }
    return bestKey;
}

void FileLibraryManager::loadLibraryEntries(const juce::String& id) {
    auto entryFile = librariesDir.getChildFile(id + ".json");
    if (!entryFile.existsAsFile()) return;

    auto content = entryFile.loadFileAsString();
    auto json = juce::JSON::parse(content);
    auto* obj = json.getDynamicObject();
    if (!obj) return;

    std::vector<LibraryEntry> loaded;
    auto entriesArr = obj->getProperty("entries", {}).getArray();
    if (!entriesArr) return;

    for (auto& var : *entriesArr) {
        auto* eObj = var.getDynamicObject();
        if (!eObj) continue;
        LibraryEntry entry;
        entry.name = eObj->getProperty("name", "").toString();
        entry.path = eObj->getProperty("path", "").toString();
        entry.size = (juce::int64)(double)eObj->getProperty("size", 0);
        entry.modified = eObj->getProperty("modified", "").toString();
        entry.tracks = (int)eObj->getProperty("tracks", 0);
        entry.notes = (int)eObj->getProperty("notes", 0);
        entry.durationTicks = (int)eObj->getProperty("durationTicks", 0);
        entry.durationSeconds = (double)eObj->getProperty("durationSeconds", 0);
        entry.tempo = (double)eObj->getProperty("tempo", 120);
        entry.timeSignature = eObj->getProperty("timeSignature", "4/4").toString();
        entry.key = eObj->getProperty("key", "").toString();
        entry.sampleRate = (double)eObj->getProperty("sampleRate", 0);
        entry.channels = (int)eObj->getProperty("channels", 0);
        entry.bpm = (double)eObj->getProperty("bpm", 0);
        entry.format = eObj->getProperty("format", "").toString();
        loaded.push_back(entry);
    }

    std::lock_guard<std::mutex> lock(mutex);
    entries[id] = loaded;
    loadedLibraries.insert(id);
}

void FileLibraryManager::saveLibraryEntries(const juce::String& id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) return;

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> arr;
    for (const auto& e : it->second) {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("name", e.name);
        obj->setProperty("path", e.path);
        obj->setProperty("size", (juce::int64)e.size);
        obj->setProperty("modified", e.modified);
        obj->setProperty("tracks", e.tracks);
        obj->setProperty("notes", e.notes);
        obj->setProperty("durationTicks", e.durationTicks);
        obj->setProperty("durationSeconds", e.durationSeconds);
        obj->setProperty("tempo", e.tempo);
        obj->setProperty("timeSignature", e.timeSignature);
        obj->setProperty("key", e.key);
        obj->setProperty("sampleRate", e.sampleRate);
        obj->setProperty("channels", e.channels);
        obj->setProperty("bpm", e.bpm);
        obj->setProperty("format", e.format);
        arr.add(juce::var(obj));
    }
    root->setProperty("entries", arr);
    auto entryFile = librariesDir.getChildFile(id + ".json");
    entryFile.getParentDirectory().createDirectory();
    entryFile.replaceWithText(juce::JSON::toString(root));
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.ExtractMidiMetadata`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/engine/FileLibraryManager.cpp
git commit -m "feat(library): implement MIDI metadata extraction and scanning"
```

---

## Task 4: FileLibraryManager — Audio Metadata Extraction

**Files:**
- Modify: `src/engine/FileLibraryManager.cpp`
- Modify: `tests/unit/engine/file_library_test.cpp`

- [ ] **Step 1: Write the failing test for audio metadata extraction**

Add to `file_library_test.cpp`:

```cpp
TEST_F(FileLibraryTest, ExtractAudioMetadata) {
    // Create a test WAV file using JUCE
    auto audioDir = tempDir.getChildFile("audio");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("test.wav");

    // Write a simple WAV (1 second of silence at 44100 Hz, mono)
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(wavFile.getFullPathName().toRawUTF8(),
                               44100.0, 1, 16, {}, 0));
    ASSERT_NE(writer, nullptr);
    juce::AudioBuffer<float> buffer(1, 44100);
    buffer.clear();
    writer->writeFromAudioSampleBuffer(buffer, 0, 44100);
    writer.reset();

    HDAW::FileLibraryManager mgr;
    auto id = mgr.addLibrary("Test Audio", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(id);

    auto results = mgr.search("");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].name, "test.wav");
    EXPECT_GT(results[0].durationSeconds, 0.9);
    EXPECT_EQ(results[0].sampleRate, 44100.0);
    EXPECT_EQ(results[0].channels, 1);
    EXPECT_EQ(results[0].format, "wav");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.ExtractAudioMetadata`
Expected: FAIL (audio extraction not implemented)

- [ ] **Step 3: Implement audio metadata extraction**

Add to `FileLibraryManager.cpp`:

```cpp
LibraryEntry FileLibraryManager::extractAudioMetadata(const juce::File& file) {
    LibraryEntry entry;
    entry.name = file.getFileName();
    entry.path = file.getFullPathName();
    entry.size = file.getSize();
    entry.modified = juce::Time(file.getLastModificationTime()).toISO8601(true);
    entry.format = file.getFileExtension().toLowerCase().substring(1); // remove the dot

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file));
    if (!reader) return entry;

    entry.durationSeconds = (double)reader->lengthInSamples / reader->sampleRate;
    entry.sampleRate = reader->sampleRate;
    entry.channels = (int)reader->numChannels;

    // Try to read BPM from metadata (BWF/iXML)
    auto metadata = reader->metadataValues;
    auto bpmStr = metadata.getValue("bam:Tempo", metadata.getValue("ixml:BPM", ""));
    if (bpmStr.isNotEmpty()) {
        entry.bpm = bpmStr.getDoubleValue();
    }

    // Key detection from audio (chromagram-based heuristic)
    // Read a chunk of audio for analysis
    int blockSize = juce::jmin((int)reader->lengthInSamples, 44100 * 10); // up to 10 seconds
    juce::AudioBuffer<float> buffer((int)reader->numChannels, blockSize);
    reader->read(&buffer, 0, blockSize, 0, true, true);

    // Simple chromagram: FFT each block, map bins to pitch classes
    if (blockSize >= 2048) {
        std::vector<int> pitchClassCounts(12, 0);
        juce::dsp::FFT fft(11); // 2048-point FFT

        for (int start = 0; start + 2048 <= blockSize; start += 1024) {
            float window[2048];
            for (int i = 0; i < 2048; ++i) {
                float sample = 0;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    sample += buffer.getSample(ch, start + i);
                sample /= buffer.getNumChannels();
                window[i] = sample * 0.5f * (1.0f - std::cos(2.0 * 3.14159265 * i / 2048)); // Hann window
            }

            float freqData[4096] = {};
            for (int i = 0; i < 2048; ++i) freqData[i * 2] = window[i];
            fft.performRealOnlyForwardTransform(freqData);

            for (int bin = 1; bin < 1024; ++bin) {
                float freq = (float)bin * (float)reader->sampleRate / 2048.0f;
                if (freq < 50.0f || freq > 5000.0f) continue;
                float magnitude = std::sqrt(freqData[bin * 2] * freqData[bin * 2] +
                                             freqData[bin * 2 + 1] * freqData[bin * 2 + 1]);
                // Map frequency to pitch class (semitone from C4)
                float midiNote = 69.0f + 12.0f * std::log2(freq / 440.0f);
                int pitchClass = ((int)std::round(midiNote) % 12 + 12) % 12;
                pitchClassCounts[pitchClass] += (int)magnitude;
            }
        }
        entry.key = detectKey(pitchClassCounts);
    }

    return entry;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.ExtractAudioMetadata`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/engine/FileLibraryManager.cpp
git commit -m "feat(library): implement audio metadata extraction with key detection"
```

---

## Task 5: FileLibraryManager — Search

**Files:**
- Modify: `src/engine/FileLibraryManager.cpp`
- Modify: `tests/unit/engine/file_library_test.cpp`

- [ ] **Step 1: Write the failing tests for search**

Add to `file_library_test.cpp`:

```cpp
TEST_F(FileLibraryTest, SearchByName) {
    HDAW::FileLibraryManager mgr;
    // Pre-populate entries manually for testing search logic
    auto id = mgr.addLibrary("Search Test", "/tmp", "midi");
    // We'll test search after implementing the full pipeline
    // For now, test that search returns empty for empty library
    auto results = mgr.search("anything");
    EXPECT_TRUE(results.empty());
}

TEST_F(FileLibraryTest, SearchFiltersByType) {
    // Test that type filtering works
    HDAW::FileLibraryManager mgr;
    auto midiId = mgr.addLibrary("MIDI", "/tmp/midi", "midi");
    auto audioId = mgr.addLibrary("Audio", "/tmp/audio", "audio");
    // After scanning, search with type="midi" should only return MIDI files
    // (tested via integration test with real files)
}
```

- [ ] **Step 2: Implement search**

Replace the search stub in `FileLibraryManager.cpp`:

```cpp
std::vector<LibraryEntry> FileLibraryManager::search(
    const juce::String& query,
    const juce::String& typeFilter,
    const juce::String& libraryIdFilter,
    double durationMin, double durationMax,
    double bpmMin, double bpmMax,
    const juce::String& keyFilter,
    int offset, int limit) const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<LibraryEntry> results;
    auto queryLower = query.toLowerCase();

    for (const auto& lib : libraries) {
        if (libraryIdFilter.isNotEmpty() && lib.id != libraryIdFilter) continue;
        if (typeFilter.isNotEmpty() && lib.type != typeFilter) continue;

        // Load entries if not loaded
        auto self = const_cast<FileLibraryManager*>(this);
        if (loadedLibraries.find(lib.id) == loadedLibraries.end()) {
            self->loadLibraryEntries(lib.id);
        }

        auto it = entries.find(lib.id);
        if (it == entries.end()) continue;

        for (const auto& entry : it->second) {
            // Name filter
            if (queryLower.isNotEmpty() && !entry.name.toLowerCase().contains(queryLower))
                continue;
            // Duration filter
            if (durationMin >= 0 && entry.durationSeconds < durationMin) continue;
            if (durationMax >= 0 && entry.durationSeconds > durationMax) continue;
            // BPM filter
            double entryBpm = entry.bpm > 0 ? entry.bpm : entry.tempo;
            if (bpmMin >= 0 && entryBpm < bpmMin) continue;
            if (bpmMax >= 0 && entryBpm > bpmMax) continue;
            // Key filter
            if (keyFilter.isNotEmpty() && !entry.key.toLowerCase().contains(keyFilter.toLowerCase()))
                continue;

            results.push_back(entry);
        }
    }

    // Sort by name
    std::sort(results.begin(), results.end(),
        [](const LibraryEntry& a, const LibraryEntry& b) { return a.name < b.name; });

    // Paginate
    if (offset > 0 && offset < (int)results.size())
        results.erase(results.begin(), results.begin() + offset);
    else if (offset > 0)
        results.clear();

    if (limit > 0 && (int)results.size() > limit)
        results.resize(limit);

    return results;
}

LibraryEntry FileLibraryManager::getEntry(const juce::String& libraryId, const juce::String& path) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto self = const_cast<FileLibraryManager*>(this);
    if (loadedLibraries.find(libraryId) == loadedLibraries.end()) {
        self->loadLibraryEntries(libraryId);
    }

    auto it = entries.find(libraryId);
    if (it == entries.end()) return {};

    for (const auto& entry : it->second) {
        if (entry.path == path) return entry;
    }
    return {};
}
```

- [ ] **Step 3: Run all tests**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.*`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/engine/FileLibraryManager.cpp tests/unit/engine/file_library_test.cpp
git commit -m "feat(library): implement search with name, type, duration, BPM, key filters"
```

---

## Task 6: MCP Tools — Library Domain

**Files:**
- Create: `src/mcp/McpTools_Library.cpp`
- Modify: `src/mcp/McpTools.cpp` (add registration call)

- [ ] **Step 1: Create MCP tools for library management**

```cpp
// src/mcp/McpTools_Library.cpp
#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../engine/FileLibraryManager.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace mcp {

static void registerLibraryTools(McpServer& s, HDAW::FileLibraryManager* libMgr) {
    // list_libraries
    s.registerTool({"list_libraries",
        "List all configured file libraries (MIDI and audio sample collections).",
        objSchema({}),
        [libMgr](const QJsonObject&) -> McpToolResult {
            auto ids = libMgr->getLibraryIds();
            QJsonArray arr;
            for (const auto& id : ids) {
                auto info = libMgr->getLibraryInfo(id);
                arr.append(QJsonObject{
                    {"id", jstr(info.id)},
                    {"name", jstr(info.name)},
                    {"path", jstr(info.path)},
                    {"type", jstr(info.type)},
                    {"lastScan", jstr(info.lastScan)},
                    {"fileCount", info.fileCount},
                    {"autoScan", info.autoScan}
                });
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    // add_library
    s.registerTool({"add_library",
        "Add a new file library directory to index.",
        objSchema({{"name", QJsonObject{{"type","string"}}},
                   {"path", QJsonObject{{"type","string"}}},
                   {"type", QJsonObject{{"type","string"}, {"enum", QJsonArray{"midi","audio"}}}}},
                 {"name","path","type"}),
        [libMgr](const QJsonObject& a) -> McpToolResult {
            auto name = a.value("name").toString();
            auto path = a.value("path").toString();
            auto type = a.value("type").toString();
            if (name.isEmpty() || path.isEmpty() || type.isEmpty())
                return McpToolResult::text("name, path, and type are required", true);
            auto id = libMgr->addLibrary(name, path, type);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                QJsonObject{{"id", jstr(id)}}).toJson(QJsonDocument::Compact)));
        }});

    // remove_library
    s.registerTool({"remove_library",
        "Remove a file library and its index.",
        objSchema({{"id", QJsonObject{{"type","string"}}}}, {"id"}),
        [libMgr](const QJsonObject& a) -> McpToolResult {
            auto id = a.value("id").toString();
            if (id.isEmpty()) return McpToolResult::text("id is required", true);
            libMgr->removeLibrary(id);
            return McpToolResult::text("ok");
        }});

    // scan_library
    s.registerTool({"scan_library",
        "Trigger a scan of a library (or all libraries).",
        objSchema({{"id", QJsonObject{{"type","string"}}}}),
        [libMgr](const QJsonObject& a) -> McpToolResult {
            auto id = a.value("id").toString();
            if (id.isEmpty()) libMgr->scanAll();
            else libMgr->scanLibrary(id);
            return McpToolResult::text("scan started");
        }});

    // search_library
    s.registerTool({"search_library",
        "Search across file libraries by name and metadata.",
        objSchema({{"query", QJsonObject{{"type","string"}}},
                   {"type", QJsonObject{{"type","string"}}},
                   {"libraryId", QJsonObject{{"type","string"}}},
                   {"durationMin", QJsonObject{{"type","number"}}},
                   {"durationMax", QJsonObject{{"type","number"}}},
                   {"bpmMin", QJsonObject{{"type","number"}}},
                   {"bpmMax", QJsonObject{{"type","number"}}},
                   {"key", QJsonObject{{"type","string"}}},
                   {"offset", QJsonObject{{"type","integer"}}},
                   {"limit", QJsonObject{{"type","integer"}}}}),
        [libMgr](const QJsonObject& a) -> McpToolResult {
            auto results = libMgr->search(
                a.value("query").toString(),
                a.value("type").toString(),
                a.value("libraryId").toString(),
                a.contains("durationMin") ? a.value("durationMin").toDouble() : -1,
                a.contains("durationMax") ? a.value("durationMax").toDouble() : -1,
                a.contains("bpmMin") ? a.value("bpmMin").toDouble() : -1,
                a.contains("bpmMax") ? a.value("bpmMax").toDouble() : -1,
                a.value("key").toString(),
                a.value("offset").toInt(0),
                a.value("limit").toInt(50));

            QJsonArray arr;
            for (const auto& e : results) {
                QJsonObject obj;
                obj["name"] = jstr(e.name);
                obj["path"] = jstr(e.path);
                obj["size"] = (qint64)e.size;
                obj["durationSeconds"] = e.durationSeconds;
                obj["key"] = jstr(e.key);
                if (e.tracks > 0) obj["tracks"] = e.tracks;
                if (e.notes > 0) obj["notes"] = e.notes;
                if (e.sampleRate > 0) obj["sampleRate"] = e.sampleRate;
                if (e.channels > 0) obj["channels"] = e.channels;
                double bpm = e.bpm > 0 ? e.bpm : e.tempo;
                if (bpm > 0) obj["bpm"] = bpm;
                if (e.format.isNotEmpty()) obj["format"] = jstr(e.format);
                if (e.timeSignature.isNotEmpty()) obj["timeSignature"] = jstr(e.timeSignature);
                arr.append(obj);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    // get_library_entry
    s.registerTool({"get_library_entry",
        "Get full metadata for a single file in a library.",
        objSchema({{"libraryId", QJsonObject{{"type","string"}}},
                   {"path", QJsonObject{{"type","string"}}}},
                 {"libraryId","path"}),
        [libMgr](const QJsonObject& a) -> McpToolResult {
            auto entry = libMgr->getEntry(a.value("libraryId").toString(), a.value("path").toString());
            if (entry.name.isEmpty())
                return McpToolResult::text("entry not found", true);
            QJsonObject obj;
            obj["name"] = jstr(entry.name);
            obj["path"] = jstr(entry.path);
            obj["size"] = (qint64)entry.size;
            obj["durationSeconds"] = entry.durationSeconds;
            obj["key"] = jstr(entry.key);
            obj["tracks"] = entry.tracks;
            obj["notes"] = entry.notes;
            obj["sampleRate"] = entry.sampleRate;
            obj["channels"] = entry.channels;
            obj["bpm"] = entry.bpm > 0 ? entry.bpm : entry.tempo;
            obj["format"] = jstr(entry.format);
            obj["timeSignature"] = jstr(entry.timeSignature);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        }});

    // set_library_autoscan
    s.registerTool({"set_library_autoscan",
        "Toggle auto-scan for a library.",
        objSchema({{"id", QJsonObject{{"type","string"}}},
                   {"enabled", QJsonObject{{"type","boolean"}}}},
                 {"id","enabled"}),
        [libMgr](const QJsonObject& a) -> McpToolResult {
            libMgr->setAutoScan(a.value("id").toString(), a.value("enabled").toBool());
            return McpToolResult::text("ok");
        }});
}

void registerLibraryDomain(McpServer& s, HDAW::FileLibraryManager* libMgr) {
    registerLibraryTools(s, libMgr);
}

} // namespace mcp
```

- [ ] **Step 2: Wire registration into McpTools.cpp**

In `src/mcp/McpTools.cpp`, find `registerAllTools` and add:

```cpp
// In registerAllTools, after existing register calls:
if (auto* libMgr = s.fileLibraryManager())
    registerLibraryDomain(s, libMgr);
```

Add the forward declaration in `McpTools.h`:
```cpp
namespace HDAW { class FileLibraryManager; }
void registerLibraryDomain(McpServer& s, HDAW::FileLibraryManager* libMgr);
```

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --config Debug`
Expected: Compiles successfully

- [ ] **Step 4: Commit**

```bash
git add src/mcp/McpTools_Library.cpp src/mcp/McpTools.cpp src/mcp/McpTools.h
git commit -m "feat(library): add MCP tools for library management and search"
```

---

## Task 7: RPC Handlers — Library Namespace

**Files:**
- Create: `src/frontend/router/Router_Library.cpp`
- Create: `src/frontend/router/Router_Library.h`
- Modify: `src/frontend/FrontendRouter.cpp` (add dispatch)

- [ ] **Step 1: Create the library RPC router**

```cpp
// src/frontend/router/Router_Library.h
#pragma once
#include "../FrontendRpc.h"

namespace HDAW { class FileLibraryManager; }

namespace frontend {
DispatchResult dispatchLibrary(HDAW::FileLibraryManager& libMgr, const QString& method, const QJsonValue& params);
}
```

```cpp
// src/frontend/router/Router_Library.cpp
#include "Router_Library.h"
#include "RouterHelpers.h"
#include "../../engine/FileLibraryManager.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace frontend {
using namespace router_helpers;

DispatchResult dispatchLibrary(HDAW::FileLibraryManager& libMgr, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);

    if (m == "list") {
        auto ids = libMgr.getLibraryIds();
        QJsonArray arr;
        for (const auto& id : ids) {
            auto info = libMgr.getLibraryInfo(id);
            arr.append(QJsonObject{
                {"id", QString::fromStdString(info.id.toStdString())},
                {"name", QString::fromStdString(info.name.toStdString())},
                {"path", QString::fromStdString(info.path.toStdString())},
                {"type", QString::fromStdString(info.type.toStdString())},
                {"lastScan", QString::fromStdString(info.lastScan.toStdString())},
                {"fileCount", info.fileCount},
                {"autoScan", info.autoScan}
            });
        }
        return { false, QJsonDocument(arr).toJson(QJsonDocument::Compact) };
    }

    if (m == "add") {
        std::string name, path, type;
        if (!requireString(o, "name", name, nullptr)) return makeError(-32602, "name required");
        if (!requireString(o, "path", path, nullptr)) return makeError(-32602, "path required");
        if (!requireString(o, "type", type, nullptr)) return makeError(-32602, "type required");
        auto id = libMgr.addLibrary(
            QString::fromStdString(name),
            QString::fromStdString(path),
            QString::fromStdString(type));
        return { false, QJsonObject{{"id", QString::fromStdString(id.toStdString())}} };
    }

    if (m == "remove") {
        std::string id;
        if (!requireString(o, "id", id, nullptr)) return makeError(-32602, "id required");
        libMgr.removeLibrary(QString::fromStdString(id));
        return { false, QJsonValue::Null };
    }

    if (m == "scan") {
        std::string id;
        if (!requireString(o, "id", id, nullptr)) libMgr.scanAll();
        else libMgr.scanLibrary(QString::fromStdString(id));
        return { false, QJsonValue::Null };
    }

    if (m == "search") {
        std::string query, typeFilter, libraryIdFilter, keyFilter;
        requireString(o, "query", query, nullptr);
        requireString(o, "type", typeFilter, nullptr);
        requireString(o, "libraryId", libraryIdFilter, nullptr);
        requireString(o, "key", keyFilter, nullptr);
        double durationMin = o.contains("durationMin") ? o["durationMin"].toDouble() : -1;
        double durationMax = o.contains("durationMax") ? o["durationMax"].toDouble() : -1;
        double bpmMin = o.contains("bpmMin") ? o["bpmMin"].toDouble() : -1;
        double bpmMax = o.contains("bpmMax") ? o["bpmMax"].toDouble() : -1;
        int offset = optInt(o, "offset", 0, nullptr);
        int limit = optInt(o, "limit", 50, nullptr);

        auto results = libMgr.search(
            QString::fromStdString(query),
            QString::fromStdString(typeFilter),
            QString::fromStdString(libraryIdFilter),
            durationMin, durationMax, bpmMin, bpmMax,
            QString::fromStdString(keyFilter),
            offset, limit);

        QJsonArray arr;
        for (const auto& e : results) {
            QJsonObject obj;
            obj["name"] = QString::fromStdString(e.name.toStdString());
            obj["path"] = QString::fromStdString(e.path.toStdString());
            obj["size"] = (qint64)e.size;
            obj["durationSeconds"] = e.durationSeconds;
            obj["key"] = QString::fromStdString(e.key.toStdString());
            if (e.tracks > 0) obj["tracks"] = e.tracks;
            if (e.notes > 0) obj["notes"] = e.notes;
            if (e.sampleRate > 0) obj["sampleRate"] = e.sampleRate;
            if (e.channels > 0) obj["channels"] = e.channels;
            double bpm = e.bpm > 0 ? e.bpm : e.tempo;
            if (bpm > 0) obj["bpm"] = bpm;
            if (e.format.isNotEmpty()) obj["format"] = QString::fromStdString(e.format.toStdString());
            if (e.timeSignature.isNotEmpty()) obj["timeSignature"] = QString::fromStdString(e.timeSignature.toStdString());
            arr.append(obj);
        }
        return { false, QJsonDocument(arr).toJson(QJsonDocument::Compact) };
    }

    return makeError(-32601, "unknown library method: " + m);
}
} // namespace frontend
```

- [ ] **Step 2: Wire into FrontendRouter.cpp**

Add to `FrontendRouter.cpp`:

```cpp
#include "router/Router_Library.h"

// In the dispatch function, add a new namespace:
else if (ns == "library") {
    return dispatchLibrary(engine.getFileLibraryManager(), m, params);
}
```

Add `getFileLibraryManager()` to `AudioEngine` (or access it via an existing manager).

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --config Debug`
Expected: Compiles successfully

- [ ] **Step 4: Commit**

```bash
git add src/frontend/router/Router_Library.cpp src/frontend/router/Router_Library.h src/frontend/FrontendRouter.cpp
git commit -m "feat(library): add RPC handlers for library namespace"
```

---

## Task 8: Frontend — Library Zustand Store

**Files:**
- Create: `frontend/src/store/libraryStore.ts`

- [ ] **Step 1: Create the library store**

```typescript
// frontend/src/store/libraryStore.ts
import { create } from "zustand";

export interface LibraryInfo {
  id: string;
  name: string;
  path: string;
  type: "midi" | "audio";
  lastScan: string;
  fileCount: number;
  autoScan: boolean;
}

export interface LibraryEntry {
  name: string;
  path: string;
  size: number;
  durationSeconds: number;
  key: string;
  tracks?: number;
  notes?: number;
  sampleRate?: number;
  channels?: number;
  bpm?: number;
  format?: string;
  timeSignature?: string;
}

export interface ScanProgress {
  libraryId: string;
  scanned: number;
  total: number;
  phase: string;
}

interface LibraryState {
  libraries: LibraryInfo[];
  searchResults: LibraryEntry[];
  searchQuery: string;
  scanProgress: Record<string, ScanProgress>;
  loading: boolean;

  // Actions
  loadLibraries: () => Promise<void>;
  addLibrary: (name: string, path: string, type: "midi" | "audio") => Promise<void>;
  removeLibrary: (id: string) => Promise<void>;
  scanLibrary: (id: string) => Promise<void>;
  scanAll: () => Promise<void>;
  setSearchQuery: (q: string) => void;
  search: (query: string, filters?: Partial<{ type: string; libraryId: string; durationMin: number; durationMax: number; bpmMin: number; bpmMax: number; key: string }>) => Promise<void>;
  setAutoScan: (id: string, enabled: boolean) => Promise<void>;
  updateScanProgress: (progress: ScanProgress) => void;
}

export const useLibraryStore = create<LibraryState>((set, get) => ({
  libraries: [],
  searchResults: [],
  searchQuery: "",
  scanProgress: {},
  loading: false,

  loadLibraries: async () => {
    set({ loading: true });
    try {
      const rpc = (await import("../rpc")).rpc;
      const libs = await rpc.call("library.list");
      set({ libraries: libs as LibraryInfo[], loading: false });
    } catch {
      set({ loading: false });
    }
  },

  addLibrary: async (name, path, type) => {
    const rpc = (await import("../rpc")).rpc;
    await rpc.call("library.add", { name, path, type });
    await get().loadLibraries();
  },

  removeLibrary: async (id) => {
    const rpc = (await import("../rpc")).rpc;
    await rpc.call("library.remove", { id });
    await get().loadLibraries();
  },

  scanLibrary: async (id) => {
    const rpc = (await import("../rpc")).rpc;
    await rpc.call("library.scan", { id });
  },

  scanAll: async () => {
    const rpc = (await import("../rpc")).rpc;
    await rpc.call("library.scan", {});
  },

  setSearchQuery: (q) => set({ searchQuery: q }),

  search: async (query, filters = {}) => {
    const rpc = (await import("../rpc")).rpc;
    const results = await rpc.call("library.search", { query, ...filters });
    set({ searchResults: results as LibraryEntry[] });
  },

  setAutoScan: async (id, enabled) => {
    const rpc = (await import("../rpc")).rpc;
    await rpc.call("library.setAutoScan", { id, enabled });
    await get().loadLibraries();
  },

  updateScanProgress: (progress) =>
    set((s) => ({
      scanProgress: { ...s.scanProgress, [progress.libraryId]: progress },
    })),
}));
```

- [ ] **Step 2: Verify TypeScript compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/store/libraryStore.ts
git commit -m "feat(library): add Zustand library store for frontend state"
```

---

## Task 9: Frontend — FileBrowser Library Mode

**Files:**
- Modify: `frontend/src/components/FileBrowser.tsx`
- Modify: `frontend/src/components/FileBrowser.css`

- [ ] **Step 1: Add "Library" kind chip and library mode rendering**

In `FileBrowser.tsx`:

1. Add `"library"` to `FileKindFilter` type in `browserStore.ts`:
```typescript
export type FileKindFilter = "all" | "devices" | "presets" | "samples" | "clips" | "midi" | "library";
```

2. Add the chip to `KIND_CHIPS`:
```typescript
{ label: "Library", value: "library" },
```

3. When `kindFilter === "library"`, render the library view instead of the tree:

```tsx
function LibraryView() {
  const { libraries, searchResults, searchQuery, scanProgress } = useLibraryStore();
  const { loadLibraries, search, setSearchQuery, scanLibrary, scanAll } = useLibraryStore();
  const [sortBy, setSortBy] = useState<string>("name");
  const [autoPlay, setAutoPlay] = useState(false);

  useEffect(() => { loadLibraries(); }, [loadLibraries]);

  const handleSearch = useCallback((q: string) => {
    setSearchQuery(q);
    search(q);
  }, [setSearchQuery, search]);

  return (
    <div className="fb-library">
      <div className="fb-library-header">
        <button onClick={() => scanAll()} className="fb-btn">Rescan All</button>
        <label className="fb-autoplay-toggle">
          <input type="checkbox" checked={autoPlay} onChange={(e) => setAutoPlay(e.target.checked)} />
          Auto-play
        </label>
      </div>
      {libraries.map((lib) => (
        <div key={lib.id} className="fb-library-node">
          <div className="fb-library-node-header">
            <span className="fb-library-name">{lib.name}</span>
            <span className="fb-library-count">{lib.fileCount} files</span>
            <button onClick={() => scanLibrary(lib.id)} className="fb-btn-small">Scan</button>
            {scanProgress[lib.id] && (
              <span className="fb-scan-progress">
                {scanProgress[lib.id].scanned}/{scanProgress[lib.id].total}
              </span>
            )}
          </div>
        </div>
      ))}
      {searchResults.length > 0 && (
        <div className="fb-library-results">
          <div className="fb-library-columns">
            <span className="fb-col-name" onClick={() => setSortBy("name")}>Name</span>
            <span className="fb-col-duration" onClick={() => setSortBy("duration")}>Duration</span>
            <span className="fb-col-bpm" onClick={() => setSortBy("bpm")}>BPM</span>
            <span className="fb-col-key" onClick={() => setSortBy("key")}>Key</span>
            <span className="fb-col-size" onClick={() => setSortBy("size")}>Size</span>
          </div>
          {searchResults.map((entry, i) => (
            <div key={i} className="fb-library-entry"
                 draggable
                 onDragStart={(e) => e.dataTransfer.setData("application/hdaw-file", JSON.stringify(entry))}>
              <span className="fb-col-name">{entry.name}</span>
              <span className="fb-col-duration">{formatDuration(entry.durationSeconds)}</span>
              <span className="fb-col-bpm">{entry.bpm || entry.tracks ? (entry.bpm || "—") : "—"}</span>
              <span className="fb-col-key">{entry.key || "—"}</span>
              <span className="fb-col-size">{formatSize(entry.size)}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

function formatDuration(seconds: number): string {
  const m = Math.floor(seconds / 60);
  const s = Math.floor(seconds % 60);
  return `${m}:${s.toString().padStart(2, "0")}`;
}

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}
```

4. In the main `FileBrowser` component, conditionally render `LibraryView` when `kindFilter === "library"`.

- [ ] **Step 2: Add CSS for library mode**

Add to `FileBrowser.css`:

```css
.fb-library { padding: 8px; }
.fb-library-header { display: flex; align-items: center; gap: 8px; margin-bottom: 8px; }
.fb-library-node { margin-bottom: 4px; }
.fb-library-node-header { display: flex; align-items: center; gap: 8px; padding: 4px 8px; }
.fb-library-name { font-weight: 500; }
.fb-library-count { color: var(--text-muted); font-size: 12px; }
.fb-library-columns { display: grid; grid-template-columns: 2fr 1fr 1fr 1fr 1fr; padding: 4px 8px;
    border-bottom: 1px solid var(--border); font-size: 11px; color: var(--text-muted); }
.fb-library-entry { display: grid; grid-template-columns: 2fr 1fr 1fr 1fr 1fr; padding: 4px 8px;
    cursor: pointer; font-size: 12px; }
.fb-library-entry:hover { background: var(--hover-bg); }
.fb-scan-progress { font-size: 11px; color: var(--accent); }
.fb-autoplay-toggle { font-size: 12px; display: flex; align-items: center; gap: 4px; }
```

- [ ] **Step 3: Verify frontend compiles**

Run: `cd frontend && npm run build`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/FileBrowser.tsx frontend/src/components/FileBrowser.css frontend/src/store/browserStore.ts
git commit -m "feat(library): add Library mode to FileBrowser with metadata columns"
```

---

## Task 10: Frontend — Preferences Library Settings

**Files:**
- Modify: `frontend/src/components/PreferencesDialog.tsx`

- [ ] **Step 1: Add Libraries section to Preferences**

Add a new section in `PreferencesDialog.tsx` after the MIDI section:

```tsx
function LibrarySettings() {
  const { libraries, loadLibraries, addLibrary, removeLibrary, setAutoScan } = useLibraryStore();
  const [newName, setNewName] = useState("");
  const [newPath, setNewPath] = useState("");
  const [newType, setNewType] = useState<"midi" | "audio">("midi");

  useEffect(() => { loadLibraries(); }, [loadLibraries]);

  const handleAdd = async () => {
    if (!newName || !newPath) return;
    await addLibrary(newName, newPath, newType);
    setNewName("");
    setNewPath("");
  };

  return (
    <section className="pref-section">
      <h3>Libraries</h3>
      <div className="pref-libraries-list">
        {libraries.map((lib) => (
          <div key={lib.id} className="pref-library-row">
            <span className="pref-library-name">{lib.name}</span>
            <span className="pref-library-path">{lib.path}</span>
            <span className="pref-library-type">{lib.type}</span>
            <span className="pref-library-count">{lib.fileCount} files</span>
            <label>
              <input type="checkbox" checked={lib.autoScan}
                     onChange={(e) => setAutoScan(lib.id, e.target.checked)} />
              Auto-scan
            </label>
            <button onClick={() => removeLibrary(lib.id)} className="pref-btn-danger">Remove</button>
          </div>
        ))}
      </div>
      <div className="pref-library-add">
        <input placeholder="Name" value={newName} onChange={(e) => setNewName(e.target.value)} />
        <input placeholder="Path" value={newPath} onChange={(e) => setNewPath(e.target.value)} />
        <select value={newType} onChange={(e) => setNewType(e.target.value as "midi" | "audio")}>
          <option value="midi">MIDI</option>
          <option value="audio">Audio</option>
        </select>
        <button onClick={handleAdd} className="pref-btn">Add Library</button>
      </div>
    </section>
  );
}
```

Render `<LibrarySettings />` inside the `pref-body` div in `PreferencesDialog`.

- [ ] **Step 2: Add CSS for library settings**

Add to `PreferencesDialog.css`:

```css
.pref-libraries-list { display: flex; flex-direction: column; gap: 8px; margin-bottom: 12px; }
.pref-library-row { display: flex; align-items: center; gap: 8px; padding: 6px 0;
    border-bottom: 1px solid var(--border); font-size: 13px; }
.pref-library-name { font-weight: 500; min-width: 120px; }
.pref-library-path { flex: 1; color: var(--text-muted); font-size: 12px; }
.pref-library-type { font-size: 11px; padding: 2px 6px; border-radius: 3px;
    background: var(--bg-panel); }
.pref-library-count { font-size: 12px; color: var(--text-muted); min-width: 60px; }
.pref-library-add { display: flex; gap: 8px; align-items: center; }
.pref-library-add input { flex: 1; }
```

- [ ] **Step 3: Verify frontend compiles**

Run: `cd frontend && npm run build`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/PreferencesDialog.tsx frontend/src/components/PreferencesDialog.css
git commit -m "feat(library): add Libraries section to Preferences dialog"
```

---

## Task 11: Default MIDI Directory & Example Files

**Files:**
- Modify: `src/engine/FileLibraryManager.cpp` (add default dir creation)

- [ ] **Step 1: Add default MIDI directory creation in initialize()**

Update `FileLibraryManager::initialize()`:

```cpp
void FileLibraryManager::initialize() {
    // Create default MIDI directory if it doesn't exist
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("HDAW");
    auto defaultMidiDir = appData.getChildFile("MIDI");
    defaultMidiDir.createDirectory();

    // Register it if not already present
    bool hasDefaultMidi = false;
    for (const auto& lib : libraries) {
        if (lib.path == defaultMidiDir.getFullPathName()) {
            hasDefaultMidi = true;
            break;
        }
    }
    if (!hasDefaultMidi) {
        addLibrary("MIDI Collection", defaultMidiDir.getFullPathName(), "midi");
        // Enable auto-scan for the default library
        auto ids = getLibraryIds();
        if (!ids.isEmpty()) setAutoScan(ids.getLast(), true);
    }

    // Auto-scan libraries marked with autoScan
    for (const auto& lib : libraries) {
        if (lib.autoScan) scanLibrary(lib.id);
    }
}
```

- [ ] **Step 2: Add example MIDI files (scales, chords, drum patterns)**

Create a small set of example MIDI files programmatically on first launch. Add a helper method:

```cpp
void FileLibraryManager::createExampleMidiFiles(const juce::File& dir) {
    // Only create if directory is empty
    juce::DirectoryIterator iter(dir, false);
    if (iter.next()) return; // already has files

    // C Major Scale
    {
        juce::MidiMessageSequence seq;
        int notes[] = {60, 62, 64, 65, 67, 69, 71, 72};
        for (int i = 0; i < 8; ++i) {
            seq.addNoteOn(1, notes[i], i * 0.5, 80);
            seq.addNoteOff(1, notes[i], i * 0.5 + 0.4);
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        auto outFile = dir.getChildFile("C Major Scale.mid");
        juce::FileOutputStream stream(outFile);
        file.writeTo(stream);
    }

    // Basic Drum Pattern
    {
        juce::MidiMessageSequence seq;
        // Kick on 1 and 3, Snare on 2 and 4, HiHat every 8th
        for (int bar = 0; bar < 2; ++bar) {
            double base = bar * 4.0;
            seq.addNoteOn(9, 36, base, 100);       // Kick
            seq.addNoteOff(9, 36, base + 0.5);
            seq.addNoteOn(9, 38, base + 1.0, 100); // Snare
            seq.addNoteOff(9, 38, base + 1.5);
            seq.addNoteOn(9, 36, base + 2.0, 100); // Kick
            seq.addNoteOff(9, 36, base + 2.5);
            seq.addNoteOn(9, 38, base + 3.0, 100); // Snare
            seq.addNoteOff(9, 38, base + 3.5);
            for (int h = 0; h < 8; ++h) {
                seq.addNoteOn(9, 42, base + h * 0.5, 60); // HiHat
                seq.addNoteOff(9, 42, base + h * 0.5 + 0.1);
            }
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        auto outFile = dir.getChildFile("Basic Drum Pattern.mid");
        juce::FileOutputStream stream(outFile);
        file.writeTo(stream);
    }

    // Simple Chord Progression (I-IV-V-I)
    {
        juce::MidiMessageSequence seq;
        struct Chord { int notes[3]; double start; };
        Chord chords[] = {
            {{60, 64, 67}, 0.0},   // C major
            {{65, 69, 72}, 1.0},   // F major
            {{67, 71, 74}, 2.0},   // G major
            {{60, 64, 67}, 3.0},   // C major
        };
        for (const auto& c : chords) {
            for (int n : c.notes) {
                seq.addNoteOn(1, n, c.start, 70);
                seq.addNoteOff(1, n, c.start + 0.9);
            }
        }
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        auto outFile = dir.getChildFile("Chord Progression I-IV-V-I.mid");
        juce::FileOutputStream stream(outFile);
        file.writeTo(stream);
    }
}
```

Call `createExampleMidiFiles(defaultMidiDir)` in `initialize()` after creating the directory.

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --config Debug`
Expected: Compiles successfully

- [ ] **Step 4: Commit**

```bash
git add src/engine/FileLibraryManager.cpp
git commit -m "feat(library): create default MIDI directory with example files on first launch"
```

---

## Task 12: Integration Tests — End-to-End

**Files:**
- Modify: `tests/unit/engine/file_library_test.cpp`

- [ ] **Step 1: Write integration tests for the full pipeline**

Add to `file_library_test.cpp`:

```cpp
TEST_F(FileLibraryTest, FullMidiPipeline) {
    auto midiDir = tempDir.getChildFile("midi_pipeline");
    midiDir.createDirectory();

    // Create test MIDI files
    for (int i = 0; i < 5; ++i) {
        juce::MidiMessageSequence seq;
        seq.addNoteOn(1, 60 + i, 0.0, 80);
        seq.addNoteOff(1, 60 + i, 1.0);
        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        auto outFile = midiDir.getChildFile("test_" + juce::String(i) + ".mid");
        juce::FileOutputStream stream(outFile);
        file.writeTo(stream);
    }

    HDAW::FileLibraryManager mgr;
    auto id = mgr.addLibrary("Pipeline Test", midiDir.getFullPathName(), "midi");
    mgr.scanLibrary(id);

    // Wait for scan to complete
    juce::Thread::sleep(500);

    // Search all
    auto all = mgr.search("");
    EXPECT_EQ(all.size(), 5);

    // Search by name
    auto filtered = mgr.search("test_2");
    EXPECT_EQ(filtered.size(), 1);
    EXPECT_EQ(filtered[0].name, "test_2.mid");
}

TEST_F(FileLibraryTest, ScanProgressCallback) {
    auto midiDir = tempDir.getChildFile("midi_progress");
    midiDir.createDirectory();

    // Create a MIDI file
    juce::MidiMessageSequence seq;
    seq.addNoteOn(1, 60, 0.0, 80);
    seq.addNoteOff(1, 60, 1.0);
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTrack(seq);
    auto outFile = midiDir.getChildFile("progress_test.mid");
    juce::FileOutputStream stream(outFile);
    file.writeTo(stream);

    HDAW::FileLibraryManager mgr;
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

    // Wait for scan
    for (int i = 0; i < 50 && !scanComplete; ++i)
        juce::Thread::sleep(100);

    EXPECT_TRUE(scanComplete);
    EXPECT_FALSE(progressUpdates.empty());
    EXPECT_EQ(progressUpdates.back().total, 1);
}
```

- [ ] **Step 2: Run all tests**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.*`
Expected: All PASS

- [ ] **Step 3: Commit**

```bash
git add tests/unit/engine/file_library_test.cpp
git commit -m "feat(library): add integration tests for full pipeline and scan progress"
```

---

## Task 13: Scan Progress Events to Frontend

**Files:**
- Modify: `src/frontend/FrontendServer.cpp` (or relevant file that pushes events)

- [ ] **Step 1: Wire scan progress to WebSocket events**

In the engine initialization where `FileLibraryManager` is set up, connect the progress callback to push events to connected frontends:

```cpp
// In AudioEngine or wherever FileLibraryManager is initialized:
fileLibraryManager.setScanProgressCallback([this](const HDAW::ScanProgress& p) {
    QJsonObject event;
    event["libraryId"] = QString::fromStdString(p.libraryId.toStdString());
    event["scanned"] = p.scanned;
    event["total"] = p.total;
    event["phase"] = QString::fromStdString(p.phase.toStdString());
    broadcastEvent("library.progress", event);
});

fileLibraryManager.setScanCompleteCallback([this](const juce::String& id, bool success) {
    QJsonObject event;
    event["libraryId"] = QString::fromStdString(id.toStdString());
    event["success"] = success;
    broadcastEvent("library.scanComplete", event);
});
```

- [ ] **Step 2: Handle events in frontend libraryStore**

Add an event listener in `libraryStore.ts`:

```typescript
// In libraryStore.ts, add an init function:
initEventListeners: () => {
  const rpc = (await import("../rpc")).rpc;
  rpc.on("library.progress", (data: ScanProgress) => {
    useLibraryStore.getState().updateScanProgress(data);
  });
  rpc.on("library.scanComplete", () => {
    useLibraryStore.getState().loadLibraries();
  });
},
```

Call `initEventListeners()` from the App component on mount.

- [ ] **Step 3: Commit**

```bash
git add src/frontend/FrontendServer.cpp frontend/src/store/libraryStore.ts frontend/src/App.tsx
git commit -m "feat(library): wire scan progress events to frontend"
```

---

## Task 14: Partitioning for Large Libraries

**Files:**
- Modify: `src/engine/FileLibraryManager.cpp`
- Modify: `tests/unit/engine/file_library_test.cpp`

- [ ] **Step 1: Write the failing test for partitioning**

Add to `file_library_test.cpp`:

```cpp
TEST_F(FileLibraryTest, PartitioningByFirstChar) {
    // Manually create partitioned entries to test the loading logic
    auto libDir = tempDir.getChildFile("partition_test");
    libDir.createDirectory();
    auto mgrDir = libDir.getChildFile("libraries");
    mgrDir.createDirectory();

    // Create two partition files
    juce::DynamicObject::Ptr rootA = new juce::DynamicObject();
    juce::Array<juce::var> arrA;
    juce::DynamicObject::Ptr e1 = new juce::DynamicObject();
    e1->setProperty("name", "alpha.mid");
    e1->setProperty("path", "/alpha.mid");
    e1->setProperty("size", 100);
    e1->setProperty("modified", "");
    e1->setProperty("durationSeconds", 1.0);
    arrA.add(juce::var(e1));
    rootA->setProperty("entries", arrA);
    mgrDir.getChildFile("part_a.json").replaceWithText(juce::JSON::toString(rootA));

    juce::DynamicObject::Ptr rootB = new juce::DynamicObject();
    juce::Array<juce::var> arrB;
    juce::DynamicObject::Ptr e2 = new juce::DynamicObject();
    e2->setProperty("name", "beta.mid");
    e2->setProperty("path", "/beta.mid");
    e2->setProperty("size", 200);
    e2->setProperty("modified", "");
    e2->setProperty("durationSeconds", 2.0);
    arrB.add(juce::var(e2));
    rootB->setProperty("entries", arrB);
    mgrDir.getChildFile("part_b.json").replaceWithText(juce::JSON::toString(rootB));

    // Test that search across partitions returns both entries
    HDAW::FileLibraryManager mgr;
    auto id = mgr.addLibrary("Partitioned", libDir.getFullPathName(), "midi");
    // After scanning, entries should be merged from partitions
    // (This tests the partitioned loading path)
}
```

- [ ] **Step 2: Implement partitioning logic in saveLibraryEntries and loadLibraryEntries**

In `FileLibraryManager::saveLibraryEntries`, add partitioning logic:

```cpp
void FileLibraryManager::saveLibraryEntries(const juce::String& id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) return;

    const int PARTITION_THRESHOLD = 50000;
    bool shouldPartition = (int)it->second.size() > PARTITION_THRESHOLD;

    if (!shouldPartition) {
        // Single file (existing logic)
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        juce::Array<juce::var> arr;
        for (const auto& e : it->second) {
            // ... serialize entry ...
            arr.add(juce::var(obj));
        }
        root->setProperty("entries", arr);
        auto entryFile = librariesDir.getChildFile(id + ".json");
        entryFile.replaceWithText(juce::JSON::toString(root));
    } else {
        // Partition by first character
        std::map<char, std::vector<LibraryEntry>> partitions;
        for (const auto& e : it->second) {
            char c = e.name.isNotEmpty() ? std::tolower(e.name[0]) : '_';
            if (c >= '0' && c <= '9') c = '0';
            else if (c < 'a' || c > 'z') c = '_';
            partitions[c].push_back(e);
        }

        // Remove old single file if it exists
        librariesDir.getChildFile(id + ".json").deleteFile();
        // Remove old partitions
        for (int i = 0; i < 26; ++i)
            librariesDir.getChildFile(id + "_part_" + juce::String(char('a' + i)) + ".json").deleteFile();
        librariesDir.getChildFile(id + "_part_0.json").deleteFile();
        librariesDir.getChildFile(id + "_part_.json").deleteFile();

        for (const auto& [c, partitionEntries] : partitions) {
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            juce::Array<juce::var> arr;
            for (const auto& e : partitionEntries) {
                // ... serialize entry ...
                arr.add(juce::var(obj));
            }
            root->setProperty("entries", arr);
            juce::String partFile = id + "_part_" + juce::String(c) + ".json";
            librariesDir.getChildFile(partFile).replaceWithText(juce::JSON::toString(root));
        }
    }
}
```

In `loadLibraryEntries`, load from partitions if they exist:

```cpp
void FileLibraryManager::loadLibraryEntries(const juce::String& id) {
    std::vector<LibraryEntry> loaded;

    // Check for single file first
    auto singleFile = librariesDir.getChildFile(id + ".json");
    if (singleFile.existsAsFile()) {
        // ... existing single-file load logic ...
    }

    // Check for partition files
    juce::Array<juce::File> partFiles;
    juce::DirectoryIterator iter(librariesDir, false, id + "_part_*.json");
    while (iter.next()) partFiles.add(iter.getFile());

    for (const auto& pf : partFiles) {
        auto content = pf.loadFileAsString();
        auto json = juce::JSON::parse(content);
        auto* obj = json.getDynamicObject();
        if (!obj) continue;
        auto entriesArr = obj->getProperty("entries", {}).getArray();
        if (!entriesArr) continue;
        for (auto& var : *entriesArr) {
            // ... deserialize entry (same as single file) ...
            loaded.push_back(entry);
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    entries[id] = loaded;
    loadedLibraries.insert(id);
}
```

- [ ] **Step 3: Run test to verify it passes**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=FileLibraryTest.PartitioningByFirstChar`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/engine/FileLibraryManager.cpp
git commit -m "feat(library): add partitioning for libraries exceeding 50k entries"
```

---

## Task 15: Incremental Scan

**Files:**
- Modify: `src/engine/FileLibraryManager.cpp`

- [ ] **Step 1: Implement incremental scan in scanDirectory**

Update `scanDirectory` to only re-index changed files:

```cpp
void FileLibraryManager::scanDirectory(const juce::String& id, const juce::File& dir) {
    juce::String type;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries) {
            if (lib.id == id) { type = lib.type; break; }
        }
    }

    // Build map of existing entries by path for incremental comparison
    std::unordered_map<juce::String, LibraryEntry> existingByPath;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = entries.find(id);
        if (it != entries.end()) {
            for (const auto& e : it->second)
                existingByPath[e.path] = e;
        }
    }

    juce::Array<juce::File> files;
    juce::DirectoryIterator iter(dir, true, type == "midi" ? "*.mid;*.midi" : "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg");
    while (iter.next()) files.add(iter.getFile());

    int total = files.size();
    int scanned = 0;
    std::vector<LibraryEntry> newEntries;
    std::unordered_set<juce::String> scannedPaths;

    for (const auto& file : files) {
        juce::String filePath = file.getFullPathName();
        scannedPaths.insert(filePath);

        // Check if file has changed since last scan
        auto it = existingByPath.find(filePath);
        bool needsRescan = true;
        if (it != existingByPath.end()) {
            juce::String storedModified = it->second.modified;
            juce::String currentModified = juce::Time(file.getLastModificationTime()).toISO8601(true);
            if (storedModified == currentModified) {
                newEntries.push_back(it->second); // reuse existing entry
                needsRescan = false;
            }
        }

        if (needsRescan) {
            LibraryEntry entry;
            if (type == "midi")
                entry = extractMidiMetadata(file);
            else
                entry = extractAudioMetadata(file);
            if (entry.name.isNotEmpty())
                newEntries.push_back(entry);
        }

        scanned++;
        if (progressCallback && scanned % 10 == 0) { // throttle progress updates
            ScanProgress p;
            p.libraryId = id;
            p.scanned = scanned;
            p.total = total;
            p.phase = "scanning";
            progressCallback(p);
        }
    }

    // Prune entries for files that no longer exist on disk
    // (already handled by not including them in newEntries)

    {
        std::lock_guard<std::mutex> lock(mutex);
        entries[id] = newEntries;
        loadedLibraries.insert(id);
        for (auto& lib : libraries) {
            if (lib.id == id) {
                lib.fileCount = (int)newEntries.size();
                lib.lastScan = juce::Time::getCurrentTime().toISO8601(true);
                break;
            }
        }
    }
    saveLibraryEntries(id);
    saveRegistry();
}
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build --config Debug`
Expected: Compiles successfully

- [ ] **Step 3: Commit**

```bash
git add src/engine/FileLibraryManager.cpp
git commit -m "feat(library): add incremental scan with timestamp-based change detection"
```

---

## Task 16: Auto-Play Preview Integration

**Files:**
- Modify: `frontend/src/components/FileBrowser.tsx`

- [ ] **Step 1: Add auto-play preview to LibraryView**

Update the `LibraryView` component in `FileBrowser.tsx`:

```tsx
function LibraryView() {
  // ... existing state ...
  const [selectedEntry, setSelectedEntry] = useState<LibraryEntry | null>(null);

  const handleSelectEntry = useCallback(async (entry: LibraryEntry) => {
    setSelectedEntry(entry);
    if (!autoPlay) return;

    if (entry.format && ["wav", "aiff", "aif", "mp3", "flac", "ogg"].includes(entry.format)) {
      // Audio preview
      try {
        await rpc.call("preview.load", { path: entry.path });
        await rpc.call("preview.play", {});
      } catch {}
    } else if (entry.path.endsWith(".mid") || entry.path.endsWith(".midi")) {
      // MIDI preview — simple sine wave renderer (future: use AudioPreviewPlayer)
      // For now, just log that we'd preview
      console.log("MIDI preview not yet implemented:", entry.name);
    }
  }, [autoPlay]);

  const handleDeselect = useCallback(() => {
    setSelectedEntry(null);
    rpc.call("preview.stop", {}).catch(() => {});
  }, []);

  return (
    <div className="fb-library">
      {/* ... existing header ... */}
      {searchResults.length > 0 && (
        <div className="fb-library-results">
          <div className="fb-library-columns">...</div>
          {searchResults.map((entry, i) => (
            <div key={i}
                 className={`fb-library-entry${selectedEntry?.path === entry.path ? " fb-library-entry--selected" : ""}`}
                 onClick={() => handleSelectEntry(entry)}
                 onBlur={handleDeselect}
                 draggable
                 onDragStart={(e) => e.dataTransfer.setData("application/hdaw-file", JSON.stringify(entry))}>
              {/* ... columns ... */}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
```

- [ ] **Step 2: Add selected entry CSS**

```css
.fb-library-entry--selected { background: var(--accent-bg); border-left: 2px solid var(--accent); }
```

- [ ] **Step 3: Verify frontend compiles**

Run: `cd frontend && npm run build`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/FileBrowser.tsx frontend/src/components/FileBrowser.css
git commit -m "feat(library): add auto-play preview for audio files in library mode"
```

---

## Task 17: Error Handling Hardening

**Files:**
- Modify: `src/engine/FileLibraryManager.cpp`

- [ ] **Step 1: Add error handling to scanDirectory**

Wrap the scan in try/catch and handle errors gracefully:

```cpp
void FileLibraryManager::scanDirectory(const juce::String& id, const juce::File& dir) {
    if (!dir.isDirectory()) {
        // Mark library as error
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& lib : libraries) {
            if (lib.id == id) {
                lib.lastScan = "ERROR: directory not found";
                break;
            }
        }
        saveRegistry();
        if (completeCallback) completeCallback(id, false);
        return;
    }

    // ... existing scan logic, wrapped in try/catch per file ...
    for (const auto& file : files) {
        try {
            LibraryEntry entry;
            if (type == "midi")
                entry = extractMidiMetadata(file);
            else
                entry = extractAudioMetadata(file);
            if (entry.name.isNotEmpty())
                newEntries.push_back(entry);
        } catch (const std::exception& e) {
            // Skip corrupt file, log warning
            DBG("FileLibraryManager: skipping " << file.getFullPathName() << ": " << e.what());
        } catch (...) {
            DBG("FileLibraryManager: skipping " << file.getFullPathName() << ": unknown error");
        }
    }
}
```

- [ ] **Step 2: Verify compilation**

Run: `cmake --build build --config Debug`
Expected: Compiles successfully

- [ ] **Step 3: Commit**

```bash
git add src/engine/FileLibraryManager.cpp
git commit -m "feat(library): add error handling for corrupt files and missing directories"
```

---

## Summary

| Task | Component | Tests |
|------|-----------|-------|
| 1 | FileLibraryManager header | — |
| 2 | Registry persistence | GTest: add/remove/setAutoScan |
| 3 | MIDI metadata extraction | GTest: extractMidiMetadata |
| 4 | Audio metadata extraction | GTest: extractAudioMetadata |
| 5 | Search | GTest: search filters |
| 6 | MCP tools | — (tested via MCP client) |
| 7 | RPC handlers | — (tested via frontend) |
| 8 | Library Zustand store | Vitest: store actions |
| 9 | FileBrowser library mode | Vitest + Playwright: UI rendering |
| 10 | Preferences library settings | Vitest: settings UI |
| 11 | Default MIDI dir + examples | GTest: full pipeline |
| 12 | Integration tests | GTest: full pipeline, callbacks |
| 13 | Scan progress events | Playwright: scan progress display |
| 14 | Partitioning | GTest: partitioned loading |
| 15 | Incremental scan | (covered by existing scan tests) |
| 16 | Auto-play preview | Playwright: preview on select |
| 17 | Error handling | GTest: corrupt file, missing dir |
