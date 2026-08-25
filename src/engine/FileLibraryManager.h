// src/engine/FileLibraryManager.h
#pragma once
#include "LibraryClusterer.h"
#include "ClusterPresetStore.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
    juce::int64 modifiedTime = 0; // raw milliseconds from getLastModificationTime()

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

    // TimbreLib sidecar (audio libraries)
    juce::String tags;        // comma-joined dsp_words + top captions + top tags
    juce::String description; // prose
    // Numeric `dsp` dict from the sidecar — accepted only when ALL 20 keys
    // (kDspFeatureKeys, LibraryClusterer.h) are present and finite; otherwise
    // empty. Ingested since schemaVersion 2 of the per-library entry cache.
    std::vector<double> dspFeatures;
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
    explicit FileLibraryManager(const juce::File& baseDir); // for testing
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

    // Clustering / nearest-neighbour over TimbreLib sidecar data
    // (docs/plans/2026-08-25-library-clustering.md). Both take an EMPTY
    // libraryIds array to mean ALL audio-type libraries (midi excluded);
    // a provided array is the exact scope — any unknown id (or a known
    // non-audio id) is an error, never a silent skip. Errors come back
    // through the out-param (empty = success); no exceptions escape.
    // Computation runs on the calling thread over a copied entry snapshot
    // (the mutex is held only for the copy).
    //
    // Cluster presets (docs/plans/2026-08-25-cluster-presets.md): when saveAs
    // is non-empty the outcome is persisted through the ClusterPresetStore as
    // a named preset (name capped at 200 chars by the store) and presetId
    // (if non-null) receives the generated "cp_<8hex>" id. saveClusterId
    // narrows the saved snapshot to one cluster ("c1".."cK", unassigned
    // omitted); an unknown cluster id fails the whole call with an error.
    ClusterOutcome clusterLibrary(const juce::StringArray& libraryIds, int k,
                                  const juce::String& method, juce::String& error,
                                  const juce::String& saveAs = {},
                                  const juce::String& saveClusterId = {},
                                  juce::String* presetId = nullptr) const;
    RelatedResult relatedSamples(const juce::StringArray& libraryIds,
                                 const juce::String& filePath, const juce::String& query,
                                 int limit, const juce::String& method, juce::String& error) const;

    // ── cluster presets (docs/plans/2026-08-25-cluster-presets.md) ──────────
    // All disk IO happens under the existing mutex (results are copied out);
    // unknown ids come back as errors, never exceptions.
    std::vector<ClusterPresetSummary> listClusterPresets() const;
    bool getClusterPreset(const juce::String& id, ClusterPreset& out,
                          juce::String& error) const;
    // refresh: loads the stored recipe (libraryIds/method/k — empty
    // libraryIds stays empty = all audio) and re-runs clusterLibrary; a
    // stored clusterId narrows the fresh result to that cluster (unassigned
    // omitted), matching the original single-cluster save shape. Returns the
    // stored record (never modified) plus the fresh outcome.
    bool refreshClusterPreset(const juce::String& id, ClusterPreset& outPreset,
                              ClusterOutcome& outOutcome, juce::String& error) const;
    bool deleteClusterPreset(const juce::String& id, juce::String& error);
    // Staleness probe over a snapshot copy: how many member paths no longer
    // exist on disk (checked = capped at `cap`, default 500).
    int countMissingPresetMembers(const ClusterPreset& p, int cap = 500) const;

    // Persistence
    void loadRegistry();
    void saveRegistry();

private:
    void loadLibraryEntries(const juce::String& id);
    void saveLibraryEntries(const juce::String& id);
    void scanDirectory(const juce::String& id, const juce::File& dir);
    LibraryEntry extractMidiMetadata(const juce::File& file);
    LibraryEntry extractAudioMetadata(const juce::File& file);
    static void applyTimbreSidecar(LibraryEntry& entry, const juce::File& audioFile);
    bool collectClusterEntries(const juce::StringArray& libraryIds,
                               std::vector<LibraryEntry>& out, juce::String& error) const;
    juce::String detectKey(const std::vector<double>& noteCounts) const;
    void createExampleMidiFiles(const juce::File& dir);

    // Owned cluster-preset store; constructed with librariesDir after the
    // registry load (loads <librariesDir>/cluster_presets.json itself).
    // mutable so the const clusterLibrary/refresh surfaces can persist saves.
    mutable std::unique_ptr<ClusterPresetStore> presetStore;

    mutable std::mutex mutex;
    std::vector<LibraryInfo> libraries;
    std::unordered_map<juce::String, std::vector<LibraryEntry>> entries;
    std::unordered_set<juce::String> loadedLibraries;
    std::unordered_set<juce::String> removedIds;
    juce::File registryFile;
    juce::File librariesDir;
    std::atomic<int> scanningCount{0};
    std::atomic<bool> shutdownRequested{false};
    ScanProgressCallback progressCallback;
    ScanCompleteCallback completeCallback;
    juce::ThreadPool threadPool{2};
};

} // namespace HDAW
