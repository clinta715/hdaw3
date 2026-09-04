// src/engine/FileLibraryManager.cpp
#include "FileLibraryManager.h"
#include "BpmDetector.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace HDAW {

FileLibraryManager::FileLibraryManager() {
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("HDAW");
    appData.createDirectory();
    librariesDir = appData.getChildFile("libraries");
    librariesDir.createDirectory();
    registryFile = librariesDir.getChildFile("registry.json");
    loadRegistry();
    presetStore = std::make_unique<ClusterPresetStore>(librariesDir);
}

FileLibraryManager::FileLibraryManager(const juce::File& baseDir) {
    librariesDir = baseDir.getChildFile("libraries");
    librariesDir.createDirectory();
    registryFile = librariesDir.getChildFile("registry.json");
    loadRegistry();
    presetStore = std::make_unique<ClusterPresetStore>(librariesDir);
}

FileLibraryManager::~FileLibraryManager() {
    shutdownRequested.store(true);
    threadPool.removeAllJobs(true, 1000);
}

void FileLibraryManager::initialize() {
    // Create default MIDI directory if it doesn't exist
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("HDAW");
    auto defaultMidiDir = appData.getChildFile("MIDI");
    defaultMidiDir.createDirectory();

    // Register it if not already present
    bool hasDefaultMidi = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries) {
            if (lib.path == defaultMidiDir.getFullPathName()) {
                hasDefaultMidi = true;
                break;
            }
        }
    }
    if (!hasDefaultMidi) {
        addLibrary("MIDI Collection", defaultMidiDir.getFullPathName(), "midi");
        // Enable auto-scan for the default library
        auto ids = getLibraryIds();
        if (!ids.isEmpty()) setAutoScan(ids[ids.size() - 1], true);
    }

    // Create example MIDI files if directory is empty
    createExampleMidiFiles(defaultMidiDir);

    // Auto-scan libraries marked with autoScan
    juce::StringArray autoScanIds;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries) {
            if (lib.autoScan) autoScanIds.add(lib.id);
        }
    }
    for (const auto& id : autoScanIds) {
        scanLibrary(id);
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
    // Gate 9: whitelist the library type at the trust boundary — midi/audio
    // (pre-existing) and patch (2026-09-04). Unknown types are rejected with
    // an empty id rather than silently persisted.
    if (type != "midi" && type != "audio" && type != "patch")
        return {};
    juce::String id = juce::Uuid().toString().removeCharacters("-{}").substring(0, 12);
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
        removedIds.insert(id);
    }
    auto entryFile = librariesDir.getChildFile(id + ".json");
    if (entryFile.existsAsFile()) entryFile.deleteFile();
    // Also remove partition files
    for (int i = 0; i < 26; ++i)
        librariesDir.getChildFile(id + "_part_" + juce::String(char('a' + i)) + ".json").deleteFile();
    librariesDir.getChildFile(id + "_part_0.json").deleteFile();
    librariesDir.getChildFile(id + "_part_.json").deleteFile();
    saveRegistry();
}

void FileLibraryManager::setAutoScan(const juce::String& id, bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& lib : libraries) {
            if (lib.id == id) {
                lib.autoScan = enabled;
                break;
            }
        }
    }
    saveRegistry();
}

bool FileLibraryManager::isScanning() const {
    return scanningCount.load() > 0;
}

void FileLibraryManager::setScanProgressCallback(ScanProgressCallback cb) {
    std::lock_guard<std::mutex> lock(mutex);
    progressCallback = std::move(cb);
}

void FileLibraryManager::setScanCompleteCallback(ScanCompleteCallback cb) {
    std::lock_guard<std::mutex> lock(mutex);
    completeCallback = std::move(cb);
}

void FileLibraryManager::loadRegistry() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!registryFile.existsAsFile()) return;
    auto content = registryFile.loadFileAsString();
    if (content.isEmpty()) return;

    auto json = juce::JSON::parse(content);
    auto* obj = json.getDynamicObject();
    if (!obj) return;

    auto& libs = obj->getProperty("libraries");
    auto* libsArray = libs.getArray();
    if (!libsArray) return;

    for (int i = 0; i < libsArray->size(); ++i) {
        auto entry = (*libsArray)[i];
        auto* eObj = entry.getDynamicObject();
        if (!eObj) continue;
        LibraryInfo info;
        info.id = eObj->getProperty("id").toString();
        info.name = eObj->getProperty("name").toString();
        info.path = eObj->getProperty("path").toString();
        info.type = eObj->getProperty("type").toString();
        info.lastScan = eObj->getProperty("lastScan").toString();
        info.fileCount = (int)eObj->getProperty("fileCount");
        info.autoScan = (bool)eObj->getProperty("autoScan");
        if (info.id.isNotEmpty()) libraries.push_back(info);
    }
}

void FileLibraryManager::saveRegistry() {
    std::lock_guard<std::mutex> lock(mutex);
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
        libs.add(juce::var(obj.get()));
    }
    root->setProperty("libraries", libs);
    registryFile.getParentDirectory().createDirectory();
    registryFile.replaceWithText(juce::JSON::toString(juce::var(root.get())));
}

void FileLibraryManager::scanLibrary(const juce::String& id) {
    juce::File scanDir;
    juce::String type;
    ScanProgressCallback progressCb;
    ScanCompleteCallback completeCb;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries) {
            if (lib.id == id) {
                scanDir = juce::File(lib.path);
                type = lib.type;
                break;
            }
        }
        progressCb = progressCallback;
        completeCb = completeCallback;
    }
    if (!scanDir.isDirectory()) {
        if (completeCb)
            completeCb(id, false);
        return;
    }

    scanningCount.fetch_add(1);
    threadPool.addJob([this, id, scanDir, type, progressCb, completeCb]() {
        struct ScanCountGuard {
            std::atomic<int>& counter;
            ~ScanCountGuard() { counter.fetch_sub(1); }
        };
        ScanCountGuard guard{scanningCount};

        try {
            scanDirectory(id, scanDir);

            // After scanDirectory returns, check if the library was removed
            // concurrently — if so, bail out without resurrecting it on disk.
            int fileCount = 0;
            bool stillPresent = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (removedIds.count(id) == 0) {
                    for (auto& lib : libraries) {
                        if (lib.id == id) {
                            stillPresent = true;
                            auto eIt = entries.find(id);
                            fileCount = (eIt != entries.end()) ? (int)eIt->second.size() : 0;
                            lib.fileCount = fileCount;
                            lib.lastScan = juce::Time::getCurrentTime().toISO8601(true);
                            break;
                        }
                    }
                }
            }
            if (!stillPresent) return;

            if (progressCb) {
                ScanProgress sp;
                sp.libraryId = id;
                sp.scanned = fileCount;
                sp.total = fileCount;
                sp.phase = "scanning";
                progressCb(sp);
            }

            saveLibraryEntries(id);
            saveRegistry();

            if (completeCb)
                completeCb(id, true);
        } catch (const std::exception& e) {
            juce::Logger::writeToLog("FileLibraryManager: scan job failed for "
                + id + ": " + juce::String(e.what()));
            if (completeCb)
                completeCb(id, false);
        } catch (...) {
            juce::Logger::writeToLog("FileLibraryManager: unknown error in scan job for " + id);
            if (completeCb)
                completeCb(id, false);
        }
    });
}

void FileLibraryManager::scanAll() {
    juce::StringArray ids;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries)
            ids.add(lib.id);
    }
    for (const auto& id : ids)
        scanLibrary(id);
}

void FileLibraryManager::loadLibraryEntries(const juce::String& id) {
    // Fast-path check under lock — avoids data race on loadedLibraries
    // (concurrent scans erase from this set under the same mutex).
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (loadedLibraries.count(id) > 0) return;
    }

    // File I/O + JSON parse OUTSIDE the lock (don't block search/getEntry/scan).
    std::vector<LibraryEntry> loaded;

    auto deserializeEntry = [](const juce::var& item) -> LibraryEntry {
        LibraryEntry e;
        auto* eObj = item.getDynamicObject();
        if (!eObj) return e;
        e.name = eObj->getProperty("name").toString();
        e.path = eObj->getProperty("path").toString();
        e.size = (juce::int64)(double)eObj->getProperty("size");
        e.modified = eObj->getProperty("modified").toString();
        e.modifiedTime = (juce::int64)(double)eObj->getProperty("modifiedTime");
        e.tracks = (int)(double)eObj->getProperty("tracks");
        e.notes = (int)(double)eObj->getProperty("notes");
        e.durationTicks = (int)(double)eObj->getProperty("durationTicks");
        e.durationSeconds = (double)eObj->getProperty("durationSeconds");
        e.tempo = (double)eObj->getProperty("tempo");
        e.timeSignature = eObj->getProperty("timeSignature").toString();
        e.key = eObj->getProperty("key").toString();
        e.sampleRate = (double)eObj->getProperty("sampleRate");
        e.channels = (int)(double)eObj->getProperty("channels");
        e.bpm = (double)eObj->getProperty("bpm");
        e.format = eObj->getProperty("format").toString();
        e.tags = eObj->getProperty("tags").toString(); // missing -> empty
        e.description = eObj->getProperty("description").toString(); // missing -> empty
        e.patchEngine = eObj->getProperty("patchEngine").toString(); // missing -> empty
        e.patchParams = eObj->getProperty("patchParams").toString(); // missing -> empty
        e.roleVerdict = eObj->getProperty("roleVerdict").toString(); // missing -> empty
        e.unmapped = eObj->getProperty("unmapped").toString();       // missing -> empty
        if (auto* dspArr = eObj->getProperty("dspFeatures").getArray()) {
            std::vector<double> vals;
            vals.reserve(dspArr->size());
            bool ok = true;
            for (const auto& v : *dspArr) {
                if (!v.isDouble() && !v.isInt() && !v.isInt64()) { ok = false; break; }
                vals.push_back((double)v);
            }
            if (ok && vals.size() == (size_t)kDspFeatureCount)
                e.dspFeatures = std::move(vals);
        }
        return e;
    };

    auto loadEntriesFromFile = [&](const juce::File& f) {
        auto content = f.loadFileAsString();
        if (content.isEmpty()) return;
        auto json = juce::JSON::parse(content);
        auto* obj = json.getDynamicObject();
        if (!obj) return;
        // Schema guard: caches written before dspFeatures (schemaVersion 2)
        // are missing the dsp axis. Ignore them so ONE rescan re-ingests with
        // the current parser instead of silently serving stale features.
        const int schemaVersion = (int)(double)obj->getProperty("schemaVersion");
        if (schemaVersion < 2) return;
        auto& items = obj->getProperty("entries");
        auto* arr = items.getArray();
        if (!arr) return;
        for (int i = 0; i < arr->size(); ++i)
            loaded.push_back(deserializeEntry((*arr)[i]));
    };

    // Try single file first
    auto singleFile = librariesDir.getChildFile(id + ".json");
    bool foundAnyFile = false;
    if (singleFile.existsAsFile()) {
        loadEntriesFromFile(singleFile);
        foundAnyFile = true;
    } else {
        // Try partition files: {id}_part_*.json
        juce::DirectoryIterator iter(librariesDir, false, id + "_part_*.json");
        while (iter.next()) {
            if (shutdownRequested.load()) return;
            loadEntriesFromFile(iter.getFile());
            foundAnyFile = true;
        }
    }

    // Commit under lock — re-check in case another thread loaded it while we did I/O.
    // Even an empty loaded vector is committed when at least one file existed on disk,
    // so subsequent searches don't re-read the empty file every call.
    if (!foundAnyFile) return;

    std::lock_guard<std::mutex> lock(mutex);
    if (loadedLibraries.count(id) > 0) return;
    entries[id] = std::move(loaded);
    loadedLibraries.insert(id);
}

void FileLibraryManager::saveLibraryEntries(const juce::String& id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(id);
    if (it == entries.end()) return;

    constexpr int PARTITION_THRESHOLD = 50000;
    const bool shouldPartition = (int)it->second.size() > PARTITION_THRESHOLD;

    auto serializeEntry = [](const LibraryEntry& e) -> juce::var {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("name", e.name);
        obj->setProperty("path", e.path);
        obj->setProperty("size", (double)e.size);
        obj->setProperty("modified", e.modified);
        obj->setProperty("modifiedTime", (double)e.modifiedTime);
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
        obj->setProperty("tags", e.tags);
        obj->setProperty("description", e.description);
        obj->setProperty("patchEngine", e.patchEngine);
        obj->setProperty("patchParams", e.patchParams);
        obj->setProperty("roleVerdict", e.roleVerdict);
        obj->setProperty("unmapped", e.unmapped);
        if (e.dspFeatures.size() == (size_t)kDspFeatureCount) {
            juce::Array<juce::var> dspArr;
            for (const double d : e.dspFeatures) dspArr.add(d);
            obj->setProperty("dspFeatures", dspArr);
        }
        return juce::var(obj.get());
    };

    // Clean up stale partition files in ALL cases — covers both the
    // shrink-to-non-partitioned path (left stale partition files behind)
    // and the normal partition re-write path. Enumerate by wildcard so we
    // catch every naming variant regardless of how the partitioned write
    // branch happens to spell the suffix.
    juce::DirectoryIterator partIter(librariesDir, false, id + "_part_*.json");
    while (partIter.next()) partIter.getFile().deleteFile();

    if (!shouldPartition) {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        juce::Array<juce::var> arr;
        for (const auto& e : it->second)
            arr.add(serializeEntry(e));
        root->setProperty("entries", arr);
        root->setProperty("schemaVersion", 2); // 2 = dspFeatures per entry
        auto entryFile = librariesDir.getChildFile(id + ".json");
        entryFile.getParentDirectory().createDirectory();
        entryFile.replaceWithText(juce::JSON::toString(juce::var(root.get())));
    } else {
        // Partition by first character
        std::map<char, std::vector<const LibraryEntry*>> partitions;
        for (const auto& e : it->second) {
            char c = e.name.isNotEmpty() ? (char)std::tolower(e.name[0]) : '_';
            if (c >= '0' && c <= '9') c = '0';
            else if (c < 'a' || c > 'z') c = '_';
            partitions[c].push_back(&e);
        }

        // Remove old single file (partition files already cleaned up above).
        librariesDir.getChildFile(id + ".json").deleteFile();

        for (const auto& [c, partitionEntries] : partitions) {
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            juce::Array<juce::var> arr;
            for (const auto* e : partitionEntries)
                arr.add(serializeEntry(*e));
            root->setProperty("entries", arr);
            root->setProperty("schemaVersion", 2); // 2 = dspFeatures per entry
            juce::String partFile = id + "_part_" + juce::String(c) + ".json";
            librariesDir.getChildFile(partFile).replaceWithText(juce::JSON::toString(juce::var(root.get())));
        }
    }
}

// True when any TimbreLib sidecar-derived field is populated. Used to detect
// entries that were scanned before a sidecar existed, or whose sidecar was
// skipped because its mtime was not newer than the audio file's (e.g.
// future-dated audio mtime on copied packs), so the incremental scan
// re-ingests the missed sidecar.
static bool entryHasTimbreData(const LibraryEntry& entry) {
    return !entry.tags.isEmpty() || !entry.description.isEmpty()
        || !entry.dspFeatures.empty();
}

// True when any patch-sidecar-derived field is populated. Same incremental
// rescan role as entryHasTimbreData for audio: an entry scanned before a
// sidecar existed, or whose sidecar mtime is not newer than a future-dated
// patch mtime, must be re-ingested on the next scan.
static bool entryHasPatchData(const LibraryEntry& entry) {
    return !entry.patchEngine.isEmpty() || !entry.patchParams.isEmpty()
        || !entry.roleVerdict.isEmpty() || !entry.unmapped.isEmpty()
        || !entry.description.isEmpty() || !entry.tags.isEmpty();
}

void FileLibraryManager::scanDirectory(const juce::String& id, const juce::File& dir) {
    if (!dir.isDirectory()) {
        juce::Logger::writeToLog("FileLibraryManager: directory not found: " + dir.getFullPathName());
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& lib : libraries) {
            if (lib.id == id) {
                lib.lastScan = juce::Time::getCurrentTime().toISO8601(true);
                lib.fileCount = 0;
                break;
            }
        }
        saveRegistry();
        return;
    }

    juce::String type;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries)
            if (lib.id == id) { type = lib.type; break; }
    }

    const juce::String wildcard = (type == "audio")
        ? juce::String("*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg")
        : (type == "patch")
            ? juce::String("*.syx;*.mid;*.midi;*.vhc")
            : juce::String("*.mid;*.midi");

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

    // Collect all files first for accurate total count
    juce::Array<juce::File> files;
    juce::DirectoryIterator iter(dir, true, wildcard);
    while (iter.next()) {
        if (shutdownRequested.load()) return;
        auto f = iter.getFile();
        if (!f.isDirectory()) files.add(f);
    }

    const int total = files.size();
    int scanned = 0;
    std::vector<LibraryEntry> newEntries;

    for (const auto& file : files) {
        if (shutdownRequested.load()) return;
        const juce::String filePath = file.getFullPathName();

        // Check if file has changed since last scan (millisecond granularity —
        // ISO8601 string compare silently missed sub-second changes).
        auto it = existingByPath.find(filePath);
        bool needsRescan = true;
        if (it != existingByPath.end()) {
            const juce::int64 currentModifiedTime = file.getLastModificationTime().toMilliseconds();
            if (it->second.modifiedTime == currentModifiedTime) {
                // Audio libraries: a TimbreLib sidecar can require a rescan even
                // though the audio itself is unchanged:
                //  1. the sidecar is NEWER than the audio — its analysis was
                //     refreshed, so tags/description must be re-ingested;
                //  2. a sidecar EXISTS but the stored entry has none of its
                //     data — the entry predates the sidecar, or the sidecar's
                //     mtime is not newer than a future-dated audio mtime, so
                //     the tags were skipped. Re-run to ingest them.
                // Patch libraries mirror the same logic over the virus/dx7
                // sidecar (entryHasPatchData).
                bool rescanForSidecar = false;
                if (type == "audio") {
                    auto sidecar = juce::File(filePath + ".timbre.json");
                    if (sidecar.existsAsFile()) {
                        if (sidecar.getLastModificationTime().toMilliseconds() > it->second.modifiedTime)
                            rescanForSidecar = true;
                        else if (!entryHasTimbreData(it->second))
                            rescanForSidecar = true;
                    }
                } else if (type == "patch") {
                    juce::File sidecar;
                    auto virusSidecar = juce::File(filePath + ".virus.json");
                    auto dx7Sidecar = juce::File(filePath + ".dx7.json");
                    if (virusSidecar.existsAsFile()) sidecar = virusSidecar;
                    else if (dx7Sidecar.existsAsFile()) sidecar = dx7Sidecar;
                    if (sidecar.existsAsFile()) {
                        if (sidecar.getLastModificationTime().toMilliseconds() > it->second.modifiedTime)
                            rescanForSidecar = true;
                        else if (!entryHasPatchData(it->second))
                            rescanForSidecar = true;
                    }
                }
                if (!rescanForSidecar) {
                    newEntries.push_back(it->second); // reuse existing entry
                    needsRescan = false;
                }
            }
        }

        if (needsRescan) {
            try {
                LibraryEntry entry;
                if (type == "audio")
                    entry = extractAudioMetadata(file);
                else if (type == "patch")
                    entry = extractPatchMetadata(file);
                else
                    entry = extractMidiMetadata(file);
                entry.name = file.getFileName();
                entry.path = filePath;
                entry.size = file.getSize();
                entry.modifiedTime = file.getLastModificationTime().toMilliseconds();
                entry.modified = juce::Time(entry.modifiedTime).toISO8601(true);
                if (type == "audio")
                    applyTimbreSidecar(entry, file);
                else if (type == "patch")
                    applyPatchSidecar(entry, file);
                newEntries.push_back(std::move(entry));
            } catch (const std::exception& e) {
                juce::Logger::writeToLog("FileLibraryManager: failed to extract metadata for "
                    + file.getFileName() + ": " + juce::String(e.what()));
            } catch (...) {
                juce::Logger::writeToLog("FileLibraryManager: unknown error extracting metadata for "
                    + file.getFileName());
            }
        }

        ++scanned;
        // Throttle progress updates to every 10 files
        if (scanned % 10 == 0 || scanned == total) {
            ScanProgressCallback cb;
            {
                std::lock_guard<std::mutex> lock(mutex);
                cb = progressCallback;
            }
            if (cb) {
                ScanProgress sp;
                sp.libraryId = id;
                sp.scanned = scanned;
                sp.total = total;
                sp.phase = "scanning";
                cb(sp);
            }
        }
    }

    // Update entries and library info. Bail out if the library was removed
    // concurrently — otherwise the commit + save below would resurrect it.
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (removedIds.count(id) > 0) return;
        entries[id] = std::move(newEntries);
        loadedLibraries.insert(id);
        for (auto& lib : libraries) {
            if (lib.id == id) {
                auto eIt = entries.find(id);
                lib.fileCount = (eIt != entries.end()) ? (int)eIt->second.size() : 0;
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
    entry.format = "midi";

    auto stream = file.createInputStream();
    if (!stream) return entry;

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(*stream)) return entry;

    int totalNotes = 0;
    double firstTempo = 120.0;
    int ticksPerQuarter = midiFile.getTimeFormat();
    if (ticksPerQuarter <= 0) ticksPerQuarter = 480;

    std::vector<double> noteCounts(12, 0.0);
    int maxTick = 0;
    bool foundTempo = false;
    int numerator = 4, denominator = 4;

    for (int t = 0; t < midiFile.getNumTracks(); ++t) {
        auto* track = midiFile.getTrack(t);
        if (!track) continue;

        for (int i = 0; i < track->getNumEvents(); ++i) {
            auto* event = track->getEventPointer(i);
            if (!event) continue;

            const auto& msg = event->message;

            if (msg.isNoteOn() && msg.getVelocity() > 0) {
                totalNotes++;
                int pitch = msg.getNoteNumber() % 12;
                noteCounts[pitch] += 1.0;
            }

            if (msg.isMetaEvent()) {
                if (msg.getMetaEventType() == 0x51 && !foundTempo) {
                    int len = msg.getMetaEventLength();
                    const auto* data = reinterpret_cast<const unsigned char*>(msg.getMetaEventData());
                    if (len >= 3) {
                        int microsPerBeat = (data[0] << 16) | (data[1] << 8) | data[2];
                        firstTempo = 60000000.0 / microsPerBeat;
                        foundTempo = true;
                    }
                }
                if (msg.getMetaEventType() == 0x58) {
                    int len = msg.getMetaEventLength();
                    const auto* data = reinterpret_cast<const unsigned char*>(msg.getMetaEventData());
                    if (len >= 2) {
                        numerator = data[0];
                        denominator = 1 << data[1];
                    }
                }
            }

            int tick = (int)msg.getTimeStamp();
            if (tick > maxTick) maxTick = tick;
        }
    }

    entry.tracks = midiFile.getNumTracks();
    entry.notes = totalNotes;
    entry.durationTicks = maxTick;
    entry.tempo = firstTempo;
    entry.timeSignature = juce::String(numerator) + "/" + juce::String(denominator);
    entry.durationSeconds = (ticksPerQuarter > 0 && firstTempo > 0.0)
        ? (double)maxTick * (60.0 / firstTempo) / (double)ticksPerQuarter
        : 0.0;

    if (totalNotes > 0)
        entry.key = detectKey(noteCounts);

    return entry;
}

juce::String FileLibraryManager::detectKey(const std::vector<double>& noteCounts) const {
    if (noteCounts.size() != 12) return {};

    // Major scale intervals: 0,2,4,5,7,9,11
    static const int majorPattern[12] = {1,0,1,0,1,1,0,1,0,1,0,1};
    // Minor scale intervals: 0,2,3,5,7,8,10
    static const int minorPattern[12] = {1,0,1,1,0,1,0,1,1,0,1,0};

    static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

    double bestScore = -1.0;
    juce::String bestKey;
    bool bestIsMinor = false;

    for (int root = 0; root < 12; ++root) {
        double majorScore = 0.0;
        double minorScore = 0.0;
        int majorCount = 0;
        int minorCount = 0;

        for (int i = 0; i < 12; ++i) {
            int idx = (i + root) % 12;
            if (majorPattern[i]) {
                majorScore += noteCounts[idx];
                majorCount++;
            }
            if (minorPattern[i]) {
                minorScore += noteCounts[idx];
                minorCount++;
            }
        }

        double majorPct = majorCount > 0 ? majorScore / (double)majorCount : 0.0;
        double minorPct = minorCount > 0 ? minorScore / (double)minorCount : 0.0;

        if (majorPct > bestScore) {
            bestScore = majorPct;
            bestKey = juce::String(noteNames[root]);
            bestIsMinor = false;
        }
        if (minorPct > bestScore) {
            bestScore = minorPct;
            bestKey = juce::String(noteNames[root]) + "m";
            bestIsMinor = true;
        }
    }

    return bestKey;
}

// Reads a TimbreLib sidecar (<audiofile>.timbre.json) next to the audio file and
// composes LibraryEntry tags/description. Fields used:
//   dsp_words (string), prose (string),
//   captions (array of [text, score]), tags (array of [label, score]).
// Missing sidecar and malformed JSON are tolerated — fields stay empty and no
// exception escapes (must never break the scan's per-file try/catch).
void FileLibraryManager::applyTimbreSidecar(LibraryEntry& entry, const juce::File& audioFile) {
    auto sidecar = juce::File(audioFile.getFullPathName() + ".timbre.json");
    if (!sidecar.existsAsFile()) return;

    juce::var json;
    try {
        auto content = sidecar.loadFileAsString();
        if (content.isEmpty()) return;
        json = juce::JSON::parse(content);
    } catch (...) {
        return; // malformed/unreadable sidecar — leave fields empty
    }
    auto* obj = json.getDynamicObject();
    if (!obj) return;

    juce::StringArray parts;

    auto dspWords = obj->getProperty("dsp_words").toString();
    if (dspWords.isNotEmpty())
        parts.add(dspWords.trim());

    // captions: array of [text, score] — keep the top 3 by score.
    if (auto* captions = obj->getProperty("captions").getArray()) {
        std::vector<std::pair<double, juce::String>> scored;
        for (const auto& c : *captions) {
            auto* cArr = c.getArray();
            if (cArr == nullptr || cArr->size() < 2) continue;
            double score = (double)(*cArr)[1];
            auto text = (*cArr)[0].toString().trim();
            if (text.isNotEmpty()) scored.push_back({score, text});
        }
        std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < scored.size() && i < 3; ++i)
            parts.add(scored[i].second);
    }

    // tags: array of [label, score] — keep the top 3 by score.
    if (auto* tags = obj->getProperty("tags").getArray()) {
        std::vector<std::pair<double, juce::String>> scored;
        for (const auto& t : *tags) {
            auto* tArr = t.getArray();
            if (tArr == nullptr || tArr->size() < 2) continue;
            double score = (double)(*tArr)[1];
            auto label = (*tArr)[0].toString().trim();
            if (label.isNotEmpty()) scored.push_back({score, label});
        }
        std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < scored.size() && i < 3; ++i)
            parts.add(scored[i].second);
    }

    entry.tags = parts.joinIntoString(", ");
    entry.description = obj->getProperty("prose").toString();

    // key/bpm: written by the TimbreLib analyzer (filename-tag first, then
    // audio-based estimation). The sidecar is the curated source, so a
    // non-empty sidecar key OVERWRITES the native chroma guess; bpm only
    // fills in when the entry has none (0.0 = unknown).
    auto sidecarKey = obj->getProperty("key").toString().trim();
    if (sidecarKey.isNotEmpty())
        entry.key = sidecarKey;
    auto sidecarBpm = obj->getProperty("bpm");
    if (sidecarBpm.isDouble() || sidecarBpm.isInt() || sidecarBpm.isInt64()) {
        const double b = (double) sidecarBpm;
        if (b > 0.0 && b < 1000.0 && entry.bpm <= 0.0)
            entry.bpm = b;
    }

    // dsp: object with the 20 numeric keys in kDspFeatureKeys. Accepted only
    // when ALL 20 keys are present and finite — no partial vectors, no
    // imputation (features stay empty).
    if (auto* dsp = obj->getProperty("dsp").getDynamicObject()) {
        std::vector<double> vals;
        vals.reserve((size_t)kDspFeatureCount);
        bool ok = true;
        for (int i = 0; i < kDspFeatureCount; ++i) {
            const juce::var& v = dsp->getProperty(kDspFeatureKeys[i]);
            if (!v.isDouble() && !v.isInt() && !v.isInt64()) { ok = false; break; }
            const double d = (double)v;
            if (!std::isfinite(d)) { ok = false; break; }
            vals.push_back(d);
        }
        if (ok) entry.dspFeatures = std::move(vals);
    }
}

// Patch-library metadata: a patch file (syx/mid/midi/vhc) has no structural
// metadata to decode at scan time — name/path/size/modified are filled by the
// scanDirectory per-file branch, and the searchable fields (patchEngine,
// patchParams, roleVerdict, unmapped, tags, description) come from the
// sidecar via applyPatchSidecar. Only `format` is set here.
LibraryEntry FileLibraryManager::extractPatchMetadata(const juce::File& file) {
    LibraryEntry entry;
    entry.format = file.getFileExtension().toLowerCase()
                       .fromFirstOccurrenceOf(".", false, false);
    return entry;
}

// Reads a patch sidecar next to the patch file — `<patch>.virus.json` first,
// else `<patch>.dx7.json` (chosen by whichever exists; the sidecar's `engine`
// key is honored on top of the naming). Populates:
//   tags        — unmapped feature names + "role:<verdict>" + up to 2 mapped
//                 param names (comma-joined, so search_library finds patches)
//   description — the sidecar `description`
//   patchEngine — sidecar `engine` ("sub_synth" | "fm_synth"), else derived
//                 from the sidecar filename
//   patchParams — compact JSON of the sidecar mappedParams (virus) / params
//                 (dx7) — the bytes a loader can replay onto a synth slot
//   roleVerdict — sidecar roleCheck.verdict (pass/fail/unknown/empty)
//   unmapped    — comma-joined `unmapped` array (Virus features with no
//                 sub_synth equivalent)
//   dspFeatures — the sidecar `dsp` object: the 20-key rendered-probe timbre
//                 vector (kDspFeatureKeys) written by sweep_dx7_patches.py
//                 --sidecars. Accepted only when ALL 20 keys are present and
//                 finite (same contract as the audio sidecar); otherwise the
//                 entry has no dsp signal and clusters by text only.
// Missing sidecar and malformed JSON are tolerated — fields stay empty and no
// exception escapes (must never break the scan's per-file try/catch).
void FileLibraryManager::applyPatchSidecar(LibraryEntry& entry, const juce::File& patchFile) {
    auto virusSidecar = juce::File(patchFile.getFullPathName() + ".virus.json");
    auto dx7Sidecar = juce::File(patchFile.getFullPathName() + ".dx7.json");
    juce::File sidecar;
    if (virusSidecar.existsAsFile()) sidecar = virusSidecar;
    else if (dx7Sidecar.existsAsFile()) sidecar = dx7Sidecar;
    if (!sidecar.existsAsFile()) return;

    juce::var json;
    try {
        auto content = sidecar.loadFileAsString();
        if (content.isEmpty()) return;
        json = juce::JSON::parse(content);
    } catch (...) {
        return; // malformed/unreadable sidecar — leave fields empty
    }
    auto* obj = json.getDynamicObject();
    if (!obj) return;

    juce::String engine = obj->getProperty("engine").toString().trim();
    if (engine.isEmpty())
        engine = sidecar.getFileName().contains(".virus.") ? "sub_synth"
               : sidecar.getFileName().contains(".dx7.")   ? "fm_synth"
               : juce::String();
    entry.patchEngine = engine;
    entry.description = obj->getProperty("description").toString();

    // roleVerdict from roleCheck.verdict (the analyze_probe.py shape).
    if (auto* rc = obj->getProperty("roleCheck").getDynamicObject())
        entry.roleVerdict = rc->getProperty("verdict").toString().trim();

    // unmapped: comma-joined array (virus sidecar); dx7 sidecars carry no
    // unmapped list, so it stays empty.
    juce::StringArray unmappedParts;
    if (auto* unmappedArr = obj->getProperty("unmapped").getArray()) {
        for (const auto& u : *unmappedArr) {
            const juce::String s = u.toString().trim();
            if (s.isNotEmpty()) unmappedParts.add(s);
        }
    }
    entry.unmapped = unmappedParts.joinIntoString(",");

    // patchParams: compact JSON of mappedParams (virus) else params (dx7).
    juce::var paramsVal = obj->getProperty("mappedParams");
    if (!paramsVal.isObject())
        paramsVal = obj->getProperty("params");
    if (paramsVal.isObject())
        entry.patchParams = juce::JSON::toString(paramsVal);

    // tags: unmapped + role verdict + up to 2 mapped param names. The
    // mappedParams values carry the real name in their `param` field
    // (virus: {"0":{"param":"osc1_wave",...}}); dx7 params fall back to a
    // `name`/`param` field, then the bare key.
    juce::StringArray tags;
    for (const auto& u : unmappedParts) tags.add(u.trim());
    if (entry.roleVerdict.isNotEmpty()) tags.add("role:" + entry.roleVerdict);
    if (auto* mp = paramsVal.getDynamicObject()) {
        int count = 0;
        for (const auto& p : mp->getProperties()) {
            if (count >= 2) break;
            juce::String pname;
            if (auto* vObj = p.value.getDynamicObject()) {
                pname = vObj->getProperty("param").toString().trim();
                if (pname.isEmpty())
                    pname = vObj->getProperty("name").toString().trim();
            }
            if (pname.isEmpty())
                pname = p.name.toString();
            if (pname.isNotEmpty()) { tags.add(pname); ++count; }
        }
    }
    entry.tags = tags.joinIntoString(", ");

    // dsp: object with the 20 numeric keys in kDspFeatureKeys (same contract
    // as the audio sidecar ingest above — accepted only when ALL 20 keys are
    // present and finite, no partial vectors / no imputation). Written by
    // sweep_dx7_patches.py --sidecars from the rendered-probe
    // timbre.extract() vector; probe-context-specific, so cluster only
    // patches swept with the same role/seed/window/bpm.
    if (auto* dsp = obj->getProperty("dsp").getDynamicObject()) {
        std::vector<double> vals;
        vals.reserve((size_t)kDspFeatureCount);
        bool ok = true;
        for (int i = 0; i < kDspFeatureCount; ++i) {
            const juce::var& v = dsp->getProperty(kDspFeatureKeys[i]);
            if (!v.isDouble() && !v.isInt() && !v.isInt64()) { ok = false; break; }
            const double d = (double)v;
            if (!std::isfinite(d)) { ok = false; break; }
            vals.push_back(d);
        }
        if (ok) entry.dspFeatures = std::move(vals);
    }
}

LibraryEntry FileLibraryManager::extractAudioMetadata(const juce::File& file) {
    LibraryEntry entry;
    entry.format = file.getFileExtension().toLowerCase()
                       .fromFirstOccurrenceOf(".", false, false);

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) return entry;
    if (reader->sampleRate <= 0.0) return entry;

    entry.durationSeconds = (double)reader->lengthInSamples / reader->sampleRate;
    entry.sampleRate = reader->sampleRate;
    entry.channels = (int)reader->numChannels;

    auto bpmStr = reader->metadataValues.getValue(
        "bam:Tempo", reader->metadataValues.getValue("ixml:BPM", juce::String()));
    if (bpmStr.isNotEmpty())
        entry.bpm = bpmStr.getDoubleValue();

    // Best-effort key detection via chromagram. Runs on the scan threadpool,
    // not the audio thread, so heap allocation / FFT work are fine here.
    constexpr int kFftOrder = 11;           // 2^11 = 2048-point FFT
    constexpr int kFftSize = 1 << kFftOrder; // 2048
    constexpr int kHopSize = kFftSize / 2;   // 1024
    constexpr int kFreqDataSize = kFftSize * 2; // 4096 (real + imag)
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kMinFreq = 50.0f;
    constexpr float kMaxFreq = 5000.0f;
    constexpr float kReferenceA = 440.0f;

    int blockSize = juce::jmin((int)reader->lengthInSamples, 44100 * 10);
    if (blockSize >= kFftSize) {
        try {
            juce::AudioBuffer<float> buf((int)reader->numChannels, blockSize);
            reader->read(&buf, 0, blockSize, 0, true, true);

            // BPM fallback: when file metadata carries no usable tempo, run the
            // aubio-based detector on a mono mixdown of the SAME decoded buffer
            // (still on the scan threadpool — never the audio thread). Best-effort:
            // 0.0 means "unknown" and leaves the field empty for consumers.
            if (entry.bpm <= 0.0) {
                juce::AudioBuffer<float> mono(1, blockSize);
                mono.clear();
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    mono.addFrom(0, 0, buf, ch, 0, blockSize);
                auto bpmRes = BpmDetector::detect(mono.getWritePointer(0), blockSize,
                                                  reader->sampleRate);
                if (bpmRes.bpm > 0.0)
                    entry.bpm = bpmRes.bpm;
            }

            std::vector<double> pitchClassCounts(12, 0.0);
            juce::dsp::FFT fft(kFftOrder);
            for (int start = 0; start + kFftSize <= blockSize; start += kHopSize) {
                std::array<float, kFreqDataSize> freqData{};
                for (int i = 0; i < kFftSize; ++i) {
                    float sample = 0.0f;
                    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                        sample += buf.getSample(ch, start + i);
                    sample /= (float)buf.getNumChannels();
                    float hann = 0.5f * (1.0f - std::cos(2.0f * kPi * (float)i / (float)kFftSize));
                    freqData[i] = sample * hann;
                }
                fft.performRealOnlyForwardTransform(freqData.data());
                for (int bin = 1; bin < kHopSize; ++bin) {
                    float freq = (float)bin * (float)reader->sampleRate / (float)kFftSize;
                    if (freq < kMinFreq || freq > kMaxFreq) continue;
                    float mag = std::sqrt(freqData[bin * 2] * freqData[bin * 2]
                                          + freqData[bin * 2 + 1] * freqData[bin * 2 + 1]);
                    float midi = 69.0f + 12.0f * std::log2(freq / kReferenceA);
                    int pc = ((int)std::round(midi) % 12 + 12) % 12;
                    pitchClassCounts[pc] += (double)mag;
                }
            }
            entry.key = detectKey(pitchClassCounts);
        } catch (...) {
            // best-effort key detection — never crash the scan
        }
    }

    return entry;
}

std::vector<LibraryEntry> FileLibraryManager::search(const juce::String& query,
    const juce::String& typeFilter, const juce::String& libraryIdFilter,
    double durationMin, double durationMax, double bpmMin, double bpmMax,
    const juce::String& keyFilter, int offset, int limit) const {

    // Phase 1 — under lock: collect candidate library ids whose metadata matches
    // the library-level filters (libraryIdFilter + typeFilter on lib.type, NOT
    // entry.format). Filter on lib.type here so search("", "audio") returns the
    // audio library's entries, not nothing.
    std::vector<juce::String> candidateIds;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries) {
            if (libraryIdFilter.isNotEmpty() && lib.id != libraryIdFilter) continue;
            if (typeFilter.isNotEmpty() && lib.type != typeFilter) continue;
            candidateIds.push_back(lib.id);
        }
    }

    // Phase 2 — OUTSIDE the lock: lazy-load each candidate from disk.
    // loadLibraryEntries is idempotent (guards on loadedLibraries) and takes its
    // own std::lock_guard on the same mutex — calling it under our own lock would
    // deadlock (std::mutex is non-recursive).
    auto* self = const_cast<FileLibraryManager*>(this);
    for (const auto& id : candidateIds)
        self->loadLibraryEntries(id);

    // Phase 3 — under lock: iterate loaded entries and apply per-entry filters.
    std::vector<LibraryEntry> results;
    auto queryLower = query.toLowerCase();
    auto keyLower = keyFilter.toLowerCase();
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& id : candidateIds) {
            auto it = entries.find(id);
            if (it == entries.end()) continue;
            for (const auto& entry : it->second) {
                // Tokenized AND match: split the (lowercased) query on
                // whitespace and require EVERY non-empty token to appear
                // (case-insensitively) in the entry's combined searchable
                // text. This is a strict superset of the former whole-string
                // literal substring match, so multi-word natural-language
                // queries ("gritty dark bass") work while single-word and
                // exact-substring behavior is unchanged.
                if (queryLower.isNotEmpty())
                {
                    juce::String haystack = (entry.name + " " + entry.path + " " + entry.key + " "
                                             + entry.tags + " " + entry.description).toLowerCase();
                    juce::StringArray tokens;
                    tokens.addTokens(queryLower, true); // split on whitespace
                    bool matched = true;
                    for (const auto& t : tokens)
                    {
                        if (t.isEmpty()) continue;
                        if (!haystack.contains(t))
                        {
                            matched = false;
                            break;
                        }
                    }
                    if (!matched)
                        continue;
                }
                if (durationMin >= 0 && entry.durationSeconds < durationMin) continue;
                if (durationMax >= 0 && entry.durationSeconds > durationMax) continue;
                double effBpm = entry.bpm > 0.0 ? entry.bpm : entry.tempo;
                if (bpmMin >= 0 && effBpm < bpmMin) continue;
                if (bpmMax >= 0 && effBpm > bpmMax) continue;
                if (keyLower.isNotEmpty()
                    && !entry.key.toLowerCase().contains(keyLower))
                    continue;
                results.push_back(entry);
            }
        }
    }

    // Phase 4 — OUTSIDE the lock: sort by name (case-insensitive) + paginate.
    std::sort(results.begin(), results.end(),
        [](const LibraryEntry& a, const LibraryEntry& b) {
            return a.name.toLowerCase() < b.name.toLowerCase();
        });
    if (offset > 0 && offset < (int)results.size())
        results.erase(results.begin(), results.begin() + offset);
    else if (offset > 0)
        results.clear();
    if (limit > 0 && (int)results.size() > limit)
        results.resize(limit);
    return results;
}

LibraryEntry FileLibraryManager::getEntry(const juce::String& libraryId, const juce::String& path) const {
    // Lazy-load outside the lock — loadLibraryEntries takes its own lock.
    auto* self = const_cast<FileLibraryManager*>(this);
    self->loadLibraryEntries(libraryId);
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(libraryId);
    if (it == entries.end()) return {};
    for (const auto& e : it->second)
        if (e.path == path) return e;
    return {};
}

// ── clustering / related-samples (docs/plans/2026-08-25-library-clustering.md) ──

bool FileLibraryManager::collectClusterEntries(const juce::StringArray& libraryIds,
                                               std::vector<LibraryEntry>& out,
                                               juce::String& error,
                                               std::vector<juce::String>* outLibraryIds) const {
    std::vector<juce::String> selected;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (libraryIds.isEmpty()) {
            // Omitted scope = ALL audio- and patch-type libraries (midi
            // excluded). Patch entries cluster by timbre only when their
            // sidecar carries a rendered-probe dsp vector (the sweep writes
            // it); entries without one fall to text-only / unassigned.
            for (const auto& lib : libraries)
                if (lib.type == "audio" || lib.type == "patch") selected.push_back(lib.id);
            if (selected.empty()) {
                error = "no audio or patch libraries found";
                return false;
            }
        } else {
            // Provided scope = exactly those libraries. Unknown ids and known
            // non-audio/non-patch ids are errors listing the offenders — never
            // silent skips.
            juce::StringArray unknown, notAudio;
            for (const auto& id : libraryIds) {
                const LibraryInfo* found = nullptr;
                for (const auto& lib : libraries)
                    if (lib.id == id) { found = &lib; break; }
                if (found == nullptr) unknown.add(id);
                else if (found->type != "audio" && found->type != "patch") notAudio.add(id);
            }
            if (!unknown.isEmpty()) {
                error = "unknown library ids: " + unknown.joinIntoString(", ");
                return false;
            }
            if (!notAudio.isEmpty()) {
                error = "not audio or patch libraries: " + notAudio.joinIntoString(", ");
                return false;
            }
            selected.assign(libraryIds.begin(), libraryIds.end());
        }
    }

    // Lazy-load OUTSIDE the lock (same pattern as search(): the loader takes
    // its own lock — a nested std::lock_guard would self-deadlock).
    auto* self = const_cast<FileLibraryManager*>(this);
    for (const auto& id : selected)
        self->loadLibraryEntries(id);

    // Copy the entry snapshot under the lock; all clustering math runs on the
    // caller's thread against the copy. When asked, pair each entry with the
    // library id it came from (same order/index as `out`).
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& id : selected) {
            auto it = entries.find(id);
            if (it == entries.end()) continue;
            if (outLibraryIds != nullptr)
                outLibraryIds->reserve(outLibraryIds->size() + it->second.size());
            for (const auto& e : it->second) {
                out.push_back(e);
                if (outLibraryIds != nullptr) outLibraryIds->push_back(id);
            }
        }
    }
    if (out.empty()) {
        error = "no entries found in the selected libraries (run scan_library first)";
        return false;
    }
    return true;
}

static bool parseClusterMethod(const juce::String& method, ClusterMethod& out,
                               juce::String& error) {
    // Whitelist comparison — no stoi/number parsing on client input.
    if (method.isEmpty() || method == "hybrid") { out = ClusterMethod::Hybrid; return true; }
    if (method == "text") { out = ClusterMethod::Text; return true; }
    if (method == "dsp")  { out = ClusterMethod::Dsp;  return true; }
    error = "unknown method: " + method + " (expected hybrid, text, or dsp)";
    return false;
}

static std::vector<ClusterItem> toClusterItems(const std::vector<LibraryEntry>& entries) {
    std::vector<ClusterItem> items;
    items.reserve(entries.size());
    for (const auto& e : entries) {
        ClusterItem it;
        it.name = e.name;
        it.path = e.path;
        it.tags = e.tags;
        it.description = e.description;
        if (e.dspFeatures.size() == (size_t)kDspFeatureCount)
            it.dsp = e.dspFeatures;
        items.push_back(std::move(it));
    }
    return items;
}

namespace {

ClusterPresetMember toPresetMember(const ClusterMember& m) {
    ClusterPresetMember pm;
    pm.name = m.name;
    pm.path = m.path;
    pm.tags = m.tags;
    pm.description = m.description;
    pm.similarity = m.similarity;
    return pm;
}

} // namespace

ClusterOutcome FileLibraryManager::clusterLibrary(const juce::StringArray& libraryIds,
                                                  int k, const juce::String& method,
                                                  juce::String& error,
                                                  const juce::String& saveAs,
                                                  const juce::String& saveClusterId,
                                                  juce::String* presetId) const {
    error = {};
    if (presetId) presetId->clear();
    ClusterMethod methodEnum = ClusterMethod::Hybrid;
    if (!parseClusterMethod(method, methodEnum, error)) return {};

    std::vector<LibraryEntry> entries;
    if (!collectClusterEntries(libraryIds, entries, error)) return {};

    auto outcome = cluster(toClusterItems(entries), k, methodEnum);
    if (outcome.clusters.empty()) {
        error = "no usable signal entries: every entry lacks tags/description and dsp features";
        return {};
    }

    // Save-as-preset (increment 2): persist a snapshot + recipe when requested.
    if (saveAs.isNotEmpty()) {
        ClusterPreset record;
        record.name = saveAs; // store caps at 200 chars (Gate 9)
        record.createdAt = juce::Time::getCurrentTime().toISO8601(true);
        record.libraryIds = libraryIds; // empty stays empty = all-audio recipe
        record.method = outcome.method;
        record.k = k; // as requested (0 = auto)

        if (saveClusterId.isNotEmpty()) {
            // Narrow the saved snapshot to ONE cluster; unassigned omitted.
            record.clusterId = saveClusterId;
            const Cluster* found = nullptr;
            for (const auto& c : outcome.clusters) {
                if (c.id == saveClusterId) { found = &c; break; }
            }
            if (found == nullptr) {
                error = "cluster id not found: " + saveClusterId;
                return {};
            }
            ClusterPresetCluster pc;
            pc.id = found->id;
            pc.label = found->label;
            for (const auto& m : found->members) pc.members.push_back(toPresetMember(m));
            record.clusters.push_back(std::move(pc));
            // unassigned stays empty — serialized as null (single-cluster save)
        } else {
            for (const auto& c : outcome.clusters) {
                ClusterPresetCluster pc;
                pc.id = c.id;
                pc.label = c.label;
                for (const auto& m : c.members) pc.members.push_back(toPresetMember(m));
                record.clusters.push_back(std::move(pc));
            }
            for (const auto& u : outcome.unassigned) record.unassigned.push_back(toPresetMember(u));
        }
        // entryCount is recomputed inside the store's upsert from the snapshot.

        std::lock_guard<std::mutex> lock(mutex);
        const juce::String id = presetStore->upsert(std::move(record));
        if (presetId) *presetId = id;
    }
    return outcome;
}

RelatedResult FileLibraryManager::relatedSamples(const juce::StringArray& libraryIds,
                                                 const juce::String& filePath,
                                                 const juce::String& query, int limit,
                                                 const juce::String& method,
                                                 juce::String& error) const {
    error = {};
    ClusterMethod methodEnum = ClusterMethod::Hybrid;
    if (!parseClusterMethod(method, methodEnum, error)) return {};

    // Exactly one of filePath / query is required.
    if (filePath.isEmpty() == query.isEmpty()) {
        error = "exactly one of filePath or query is required";
        return {};
    }
    if (!query.isEmpty() && methodEnum == ClusterMethod::Dsp) {
        error = "query seed requires method 'text' or 'hybrid' (a text query has no dsp axis)";
        return {};
    }

    std::vector<LibraryEntry> entries;
    std::vector<juce::String> entryLibraryIds;
    if (!collectClusterEntries(libraryIds, entries, error, &entryLibraryIds)) return {};
    // P3-3: attribute each result hit to the library it came from. The pure
    // clusterer (LibraryClusterer) has no library concept — the wrapper does,
    // so build a path -> library map from the collected entries here. The
    // cluster math itself is not changed.
    std::map<juce::String, juce::String> pathToLibrary;
    for (size_t i = 0; i < entries.size(); ++i)
        pathToLibrary.emplace(entries[i].path, entryLibraryIds[i]);
    const auto items = toClusterItems(entries);

    RelatedResult r;
    if (filePath.isNotEmpty()) {
        r = relatedToItem(items, filePath, methodEnum, limit);
        if (!r.found) {
            error = "entry not found: " + filePath;
            return {};
        }
        if (!r.hasSeedSignal) {
            error = "seed entry has no tags, description, or dsp features to compare";
            return {};
        }
    } else {
        r = relatedToQuery(items, query, methodEnum, limit);
        if (!r.hasSeedSignal) {
            error = "query matched no indexed terms";
            return {};
        }
    }
    for (auto& h : r.results) {
        auto it = pathToLibrary.find(h.path);
        if (it != pathToLibrary.end()) h.libraryId = it->second;
    }
    return r;
}

// ── cluster presets (docs/plans/2026-08-25-cluster-presets.md) ───────────
// All disk IO is guarded by the existing mutex; results are copied out.

std::vector<ClusterPresetSummary> FileLibraryManager::listClusterPresets() const {
    std::lock_guard<std::mutex> lock(mutex);
    return presetStore ? presetStore->list() : std::vector<ClusterPresetSummary>();
}

bool FileLibraryManager::getClusterPreset(const juce::String& id, ClusterPreset& out,
                                          juce::String& error) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (!presetStore || !presetStore->get(id, out)) {
        error = "preset not found: " + id;
        return false;
    }
    return true;
}

bool FileLibraryManager::refreshClusterPreset(const juce::String& id,
                                              ClusterPreset& outPreset,
                                              ClusterOutcome& outOutcome,
                                              juce::String& error) const {
    ClusterPreset stored;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!presetStore || !presetStore->get(id, stored)) {
            error = "preset not found: " + id;
            return false;
        }
    }

    // Re-run the stored recipe (empty libraryIds stays empty = all audio).
    auto outcome = clusterLibrary(stored.libraryIds, stored.k, stored.method, error);
    if (error.isNotEmpty()) return false;

    // Single-cluster presets refresh to the same narrow shape they were saved
    // with: only that cluster's members, unassigned omitted.
    if (stored.clusterId.isNotEmpty()) {
        Cluster* found = nullptr;
        for (auto& c : outcome.clusters) {
            if (c.id == stored.clusterId) { found = &c; break; }
        }
        if (found == nullptr) {
            error = "cluster " + stored.clusterId + " no longer present after refresh";
            return false;
        }
        Cluster kept = std::move(*found);
        outcome.clusters.clear();
        outcome.clusters.push_back(std::move(kept));
        outcome.unassigned.clear();
        outcome.note.clear();
    }

    outPreset = std::move(stored);
    outOutcome = std::move(outcome);
    return true;
}

bool FileLibraryManager::deleteClusterPreset(const juce::String& id, juce::String& error) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!presetStore) {
        error = "preset store unavailable";
        return false;
    }
    return presetStore->remove(id, error);
}

int FileLibraryManager::countMissingPresetMembers(const ClusterPreset& p, int cap) const {
    int missing = 0;
    int checked = 0;
    auto check = [&](const juce::String& path) {
        if (checked >= cap) return;
        ++checked;
        if (path.isNotEmpty() && !juce::File(path).existsAsFile()) ++missing;
    };
    for (const auto& c : p.clusters)
        for (const auto& m : c.members) check(m.path);
    for (const auto& u : p.unassigned) check(u.path);
    return missing;
}

void FileLibraryManager::createExampleMidiFiles(const juce::File& dir) {
    // Only create if directory is empty
    juce::DirectoryIterator iter(dir, false);
    if (iter.next()) return; // already has files

    auto addNote = [](juce::MidiMessageSequence& seq, int ch, int note, int vel, double onTime, double offTime) {
        auto on = juce::MidiMessage::noteOn(ch, note, (juce::uint8)vel);
        on.setTimeStamp(onTime);
        seq.addEvent(on);
        auto off = juce::MidiMessage::noteOff(ch, note);
        off.setTimeStamp(offTime);
        seq.addEvent(off);
    };

    // C Major Scale
    {
        juce::MidiMessageSequence seq;
        int notes[] = {60, 62, 64, 65, 67, 69, 71, 72};
        for (int i = 0; i < 8; ++i)
            addNote(seq, 1, notes[i], 80, i * 0.5, i * 0.5 + 0.4);

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
        for (int bar = 0; bar < 2; ++bar) {
            double base = bar * 4.0;
            addNote(seq, 9, 36, 100, base, base + 0.5);
            addNote(seq, 9, 38, 100, base + 1.0, base + 1.5);
            addNote(seq, 9, 36, 100, base + 2.0, base + 2.5);
            addNote(seq, 9, 38, 100, base + 3.0, base + 3.5);
            for (int h = 0; h < 8; ++h)
                addNote(seq, 9, 42, 60, base + h * 0.5, base + h * 0.5 + 0.1);
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
            {{60, 64, 67}, 0.0},
            {{65, 69, 72}, 1.0},
            {{67, 71, 74}, 2.0},
            {{60, 64, 67}, 3.0},
        };
        for (const auto& c : chords)
            for (int n : c.notes)
                addNote(seq, 1, n, 70, c.start, c.start + 0.9);

        juce::MidiFile file;
        file.setTicksPerQuarterNote(480);
        file.addTrack(seq);
        auto outFile = dir.getChildFile("Chord Progression I-IV-V-I.mid");
        juce::FileOutputStream stream(outFile);
        file.writeTo(stream);
    }
}

} // namespace HDAW
