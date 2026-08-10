// src/engine/FileLibraryManager.cpp
#include "FileLibraryManager.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

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

FileLibraryManager::FileLibraryManager(const juce::File& baseDir) {
    librariesDir = baseDir.getChildFile("libraries");
    librariesDir.createDirectory();
    registryFile = librariesDir.getChildFile("registry.json");
    loadRegistry();
}

FileLibraryManager::~FileLibraryManager() {
    threadPool.removeAllJobs(true, 1000);
}

void FileLibraryManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex);
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
    }
    auto entryFile = librariesDir.getChildFile(id + ".json");
    if (entryFile.existsAsFile()) entryFile.deleteFile();
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
    return scanning.load();
}

void FileLibraryManager::setScanProgressCallback(ScanProgressCallback cb) {
    progressCallback = std::move(cb);
}

void FileLibraryManager::setScanCompleteCallback(ScanCompleteCallback cb) {
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
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries) {
            if (lib.id == id) {
                scanDir = juce::File(lib.path);
                type = lib.type;
                break;
            }
        }
    }
    if (!scanDir.isDirectory()) return;

    scanning.store(true);
    threadPool.addJob([this, id, scanDir, type]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            entries[id].clear();
            loadedLibraries.erase(id);
        }

        scanDirectory(id, scanDir);

        int fileCount = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto& lib : libraries) {
                if (lib.id == id) {
                    lib.fileCount = (int)entries[id].size();
                    lib.lastScan = juce::Time::getCurrentTime().toISO8601(true);
                    fileCount = lib.fileCount;
                    break;
                }
            }
        }
        saveLibraryEntries(id);
        saveRegistry();
        scanning.store(false);

        if (completeCallback)
            completeCallback(id, true);
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
    if (loadedLibraries.count(id) > 0) return;
    auto entryFile = librariesDir.getChildFile(id + ".json");
    if (!entryFile.existsAsFile()) return;

    auto content = entryFile.loadFileAsString();
    if (content.isEmpty()) return;

    auto json = juce::JSON::parse(content);
    auto* obj = json.getDynamicObject();
    if (!obj) return;

    auto& items = obj->getProperty("entries");
    auto* arr = items.getArray();
    if (!arr) return;

    std::lock_guard<std::mutex> lock(mutex);
    auto& vec = entries[id];
    vec.clear();

    for (int i = 0; i < arr->size(); ++i) {
        const auto& item = (*arr)[i];
        auto* eObj = item.getDynamicObject();
        if (!eObj) continue;

        LibraryEntry e;
        e.name = eObj->getProperty("name").toString();
        e.path = eObj->getProperty("path").toString();
        e.size = (juce::int64)(double)eObj->getProperty("size");
        e.modified = eObj->getProperty("modified").toString();
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
        vec.push_back(std::move(e));
    }
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
        obj->setProperty("size", (double)e.size);
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
        arr.add(juce::var(obj.get()));
    }

    root->setProperty("entries", arr);
    auto entryFile = librariesDir.getChildFile(id + ".json");
    entryFile.getParentDirectory().createDirectory();
    entryFile.replaceWithText(juce::JSON::toString(juce::var(root.get())));
}

void FileLibraryManager::scanDirectory(const juce::String& id, const juce::File& dir) {
    juce::DirectoryIterator iter(dir, true, "*.mid;*.midi");
    while (iter.next()) {
        auto file = iter.getFile();
        if (file.isDirectory()) continue;

        LibraryEntry entry;
        if (file.getFileExtension().equalsIgnoreCase(".mid") ||
            file.getFileExtension().equalsIgnoreCase(".midi")) {
            entry = extractMidiMetadata(file);
        } else {
            continue;
        }

        entry.name = file.getFileName();
        entry.path = file.getFullPathName();
        entry.size = file.getSize();
        entry.modified = file.getLastModificationTime().toISO8601(true);

        std::lock_guard<std::mutex> lock(mutex);
        entries[id].push_back(std::move(entry));
    }
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

    std::vector<int> noteCounts(12, 0);
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
                noteCounts[pitch]++;
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

juce::String FileLibraryManager::detectKey(const std::vector<int>& noteCounts) const {
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
                majorScore += (double)noteCounts[idx];
                majorCount++;
            }
            if (minorPattern[i]) {
                minorScore += (double)noteCounts[idx];
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

LibraryEntry FileLibraryManager::extractAudioMetadata(const juce::File&) { return {}; }

std::vector<LibraryEntry> FileLibraryManager::search(const juce::String& query,
    const juce::String& typeFilter, const juce::String& libraryIdFilter,
    double durationMin, double durationMax, double bpmMin, double bpmMax,
    const juce::String& keyFilter, int offset, int limit) const {

    std::lock_guard<std::mutex> lock(mutex);
    std::vector<LibraryEntry> results;

    for (const auto& [libId, libEntries] : entries) {
        if (libraryIdFilter.isNotEmpty() && libId != libraryIdFilter) continue;

        for (const auto& entry : libEntries) {
            if (typeFilter.isNotEmpty() && entry.format != typeFilter) continue;
            if (durationMin >= 0 && entry.durationSeconds < durationMin) continue;
            if (durationMax >= 0 && entry.durationSeconds > durationMax) continue;
            if (bpmMin >= 0 && entry.tempo < bpmMin) continue;
            if (bpmMax >= 0 && entry.tempo > bpmMax) continue;
            if (keyFilter.isNotEmpty() && entry.key != keyFilter) continue;

            if (query.isNotEmpty()) {
                if (!entry.name.containsIgnoreCase(query) &&
                    !entry.path.containsIgnoreCase(query) &&
                    !entry.key.containsIgnoreCase(query))
                    continue;
            }

            results.push_back(entry);
        }
    }

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
    auto it = entries.find(libraryId);
    if (it == entries.end()) return {};
    for (const auto& e : it->second) {
        if (e.path == path) return e;
    }
    return {};
}

} // namespace HDAW
