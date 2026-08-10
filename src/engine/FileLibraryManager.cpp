// src/engine/FileLibraryManager.cpp
#include "FileLibraryManager.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>

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
    return scanningCount.load() > 0;
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
        scanningCount.fetch_sub(1);

        if (completeCb)
            completeCb(id, true);
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
    juce::String type;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& lib : libraries)
            if (lib.id == id) { type = lib.type; break; }
    }

    const juce::String wildcard = (type == "audio")
        ? juce::String("*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg")
        : juce::String("*.mid;*.midi");

    std::vector<LibraryEntry> newEntries;
    juce::DirectoryIterator iter(dir, true, wildcard);
    while (iter.next()) {
        auto file = iter.getFile();
        if (file.isDirectory()) continue;

        LibraryEntry entry = (type == "audio")
            ? extractAudioMetadata(file)
            : extractMidiMetadata(file);

        entry.name = file.getFileName();
        entry.path = file.getFullPathName();
        entry.size = file.getSize();
        entry.modified = file.getLastModificationTime().toISO8601(true);

        newEntries.push_back(std::move(entry));
    }

    std::lock_guard<std::mutex> lock(mutex);
    entries[id] = std::move(newEntries);
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

LibraryEntry FileLibraryManager::extractAudioMetadata(const juce::File& file) {
    LibraryEntry entry;
    entry.format = file.getFileExtension().toLowerCase()
                       .fromFirstOccurrenceOf(".", false, false);

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) return entry;

    entry.durationSeconds = (double)reader->lengthInSamples / reader->sampleRate;
    entry.sampleRate = reader->sampleRate;
    entry.channels = (int)reader->numChannels;

    auto bpmStr = reader->metadataValues.getValue(
        "bam:Tempo", reader->metadataValues.getValue("ixml:BPM", juce::String()));
    if (bpmStr.isNotEmpty())
        entry.bpm = bpmStr.getDoubleValue();

    // Best-effort key detection via chromagram. Runs on the scan threadpool,
    // not the audio thread, so heap allocation / FFT work are fine here.
    int blockSize = juce::jmin((int)reader->lengthInSamples, 44100 * 10);
    if (blockSize >= 2048) {
        try {
            juce::AudioBuffer<float> buf((int)reader->numChannels, blockSize);
            reader->read(&buf, 0, blockSize, 0, true, true);

            std::vector<int> pitchClassCounts(12, 0);
            juce::dsp::FFT fft(11); // 2^11 = 2048-point
            for (int start = 0; start + 2048 <= blockSize; start += 1024) {
                std::array<float, 4096> freqData{};
                for (int i = 0; i < 2048; ++i) {
                    float sample = 0.0f;
                    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                        sample += buf.getSample(ch, start + i);
                    sample /= (float)buf.getNumChannels();
                    float hann = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * (float)i / 2048.0f));
                    freqData[i] = sample * hann;
                }
                fft.performRealOnlyForwardTransform(freqData.data());
                for (int bin = 1; bin < 1024; ++bin) {
                    float freq = (float)bin * (float)reader->sampleRate / 2048.0f;
                    if (freq < 50.0f || freq > 5000.0f) continue;
                    float mag = std::sqrt(freqData[bin * 2] * freqData[bin * 2]
                                          + freqData[bin * 2 + 1] * freqData[bin * 2 + 1]);
                    float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
                    int pc = ((int)std::round(midi) % 12 + 12) % 12;
                    pitchClassCounts[pc] += (int)mag;
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
                if (queryLower.isNotEmpty()
                    && !entry.name.toLowerCase().contains(queryLower)
                    && !entry.path.toLowerCase().contains(queryLower)
                    && !entry.key.toLowerCase().contains(queryLower))
                    continue;
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

} // namespace HDAW
