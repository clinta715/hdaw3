// src/engine/ClusterPresetStore.h
#pragma once

// Named, persistent snapshots of cluster_library results
// (docs/plans/2026-08-25-cluster-presets.md, increment 2).
//
// A preset stores BOTH a snapshot (the result as saved) and the recipe
// (libraryIds/method/k/clusterId) so it can be re-materialized against a
// changed library via refresh. Persistence lives at
// <librariesDir>/cluster_presets.json, the established library persistence
// home (same directory as registry.json + libraries/<id>.json).
//
// The store owns NO mutex of its own: FileLibraryManager guards every call
// with its existing mutex (methods take plain structs in/out; no callbacks).
// Corrupted/missing files are tolerated — the store loads empty and logs a
// warning; no exceptions escape (save failures are logged, in-memory state
// is kept).

#include <juce_core/juce_core.h>
#include <vector>

namespace HDAW {

struct ClusterPresetMember {
    juce::String name;
    juce::String path;
    juce::String tags;
    juce::String description;
    double similarity = 0.0;
};

struct ClusterPresetCluster {
    juce::String id;    // "c1".."cK" — same ids as ClusterOutcome
    juce::String label;
    std::vector<ClusterPresetMember> members; // size derived from members
};

// One saved preset record (see plan §Storage).
struct ClusterPreset {
    juce::String id;             // "cp_<8 hex>", collision-checked
    juce::String name;           // user/agent-provided, capped at 200 chars
    juce::String createdAt;      // ISO 8601
    juce::StringArray libraryIds;// scope snapshot; empty = all-audio scope
    juce::String method;         // "hybrid" | "text" | "dsp"
    int k = 0;                   // as requested (0 = auto)
    juce::String clusterId;      // empty = whole result saved; else "c1"...
    std::vector<ClusterPresetCluster> clusters;      // snapshot
    std::vector<ClusterPresetMember> unassigned;     // empty when single-cluster save
    int entryCount = 0;
};

// List-entry shape (no clusters/members payload — cheap to enumerate).
struct ClusterPresetSummary {
    juce::String id;
    juce::String name;
    juce::String createdAt;
    juce::StringArray libraryIds;
    juce::String method;
    int k = 0;
    juce::String clusterId;
    int clusterCount = 0;
    int entryCount = 0;
};

class ClusterPresetStore {
public:
    explicit ClusterPresetStore(const juce::File& librariesDir); // loads immediately

    // Disk (re)load: missing file -> empty; corrupted JSON -> empty store +
    // warning log, never a crash. save() writes atomically (temp + move).
    void load();
    void save();

    std::vector<ClusterPresetSummary> list() const;
    bool get(const juce::String& id, ClusterPreset& out) const;

    // New record (record.id empty -> generated "cp_<8hex>", collision-checked)
    // or replacement in place (id matches an existing preset). Name is capped
    // at 200 chars; entryCount is recomputed from the snapshot. Returns the id.
    juce::String upsert(ClusterPreset record);

    // Removes the preset; returns false (with a non-empty error) when the id
    // is unknown. No exceptions escape.
    bool remove(const juce::String& id, juce::String& error);

    // Canonical single-record JSON (used by the file writer and by tests for
    // byte-faithful round-trip assertions).
    static juce::String recordToJson(const ClusterPreset& p);

    // Name length cap (Gate 9: name capped, stored as-is otherwise).
    static constexpr int kMaxNameLength = 200;

private:
    static juce::var recordToVar(const ClusterPreset& p);
    static bool varToRecord(const juce::var& v, ClusterPreset& out);
    juce::String generateId() const;

    juce::File presetFile;
    std::vector<ClusterPreset> presets;
};

} // namespace HDAW
