# Plan: Cluster presets — save, list, get, delete, refresh (increment 2)

Status: APPROVED by user (sequenced behind v1.1 landing)
Date: 2026-08-25
Predecessor: docs/plans/2026-08-25-library-clustering.md (v1.1, in flight)

## Goal
Clusters produced by `cluster_library` can be saved as named presets so the
user (future frontend) and MCP clients can re-use them: list, fetch, refresh
(recompute from the stored recipe), and delete. Presets store BOTH a snapshot
(result as saved) and the recipe (libraryIds/method/k/clusterId) so they can
be re-materialized against a changed library.

## Success Gates

- [ ] G1: build-fast.bat test green; RelWithDebInfo hdaw_tests.exe newer than
      every touched source.
- [ ] G2: ClusterPresetStoreTest green: save->list->get round-trip (all fields
      byte-faithful incl. clusters+unassigned members); unknown id error;
      duplicate name allowed but ids unique; delete removes only that preset;
      file reload after manager re-instantiation preserves presets (persistence
      on disk); corrupted JSON file -> empty store + warning log, no crash.
- [ ] G3: McpCoverageTest green: cluster_library with saveAs returns presetId;
      list_cluster_presets shows it (name, libraryIds, method, k, counts);
      get_cluster_preset returns the snapshot; refresh=true recomputes and
      equals the original snapshot when the library is unchanged
      (determinism bridge); delete_cluster_preset removes it; single-cluster
      save (clusterId) stores only that cluster's members.
- [ ] G4: Focused rerun green: FileLibraryTest.*:McpCoverageTest.*:
      LibraryClustererTest.*:ClusterPresetStoreTest.*; full-suite delta limited
      to the documented pre-existing env failures.

## Dependency Map

- Blast radius: FileLibraryManager (owns new ClusterPresetStore),
  McpTools_Library.cpp (+4 tools, 1 param), Router_Library.cpp (+3 methods,
  1 param). No ValueTree/projection/SPSC involvement; disk IO under the
  existing mutex pattern.
- Upstream: MCP clients; frontend later (browser "Clusters" view is v2 UI).
- Downstream: preset file on disk: `<librariesDir>/cluster_presets.json`
  (same directory as registry.json + libraries/<id>.json — the established
  library persistence home).
- God nodes: none. Community boundary: engine -> MCP surface (same seam as v1).

## Design

### Storage: ClusterPresetStore (new, src/engine/ClusterPresetStore.{h,cpp})
- Owned by FileLibraryManager (unique_ptr, initialized with librariesDir).
- File: cluster_presets.json in the libraries dir. JSON array under root
  object { "presets": [...] }. Load once at initialize(); save on every
  mutation (atomic write: temp file + move, matching registry save style).
- Preset record:
    id           "cp_<8 hex>" (generated, collision-checked)
    name         user/agent-provided string (may repeat; id is the key)
    createdAt    ISO 8601
    libraryIds   [..] scope snapshot (empty array = all-audio scope)
    method       "hybrid"|"text"|"dsp"
    k            int as requested (0 = auto)
    clusterId    string or null (null = whole result was saved)
    clusters     snapshot: [{id,label,size,members:[{name,path,tags,
                 description,similarity}]}]
    unassigned   snapshot: [{name,path}] (null when single-cluster save)
    entryCount   int
- Mutex: guarded by FileLibraryManager's existing mutex (store methods take a
  plain struct in/out; no callbacks).

### Surface (MCP parity)
- cluster_library: NEW optional param saveAs (string) — when present, save the
  result as a named preset and include presetId in the response. Optional
  clusterId (string) narrows the save to one cluster (unassigned omitted).
- list_cluster_presets {} -> {presets:[{id,name,createdAt,libraryIds,method,
  k,clusterId,clusterCount,entryCount}]}
- get_cluster_preset {id, refresh?: bool=false} -> full preset; when
  refresh=true, recompute via the stored recipe (libraryIds/method/k) and
  return the fresh result in the same shape (preset fields unchanged except a
  `computedAt` echo); include `missingMemberCount` (cheap File::existsAsFile
  per member, capped at 500 members, counted from the SNAPSHOT paths) when
  refresh=false so staleness is visible without recomputing.
- delete_cluster_preset {id} -> {deleted:true} or error for unknown id.
- Router_Library.cpp: methods "clusterPresetsList" / "clusterPresetsGet" /
  "clusterPresetsDelete"; "cluster" method gains saveAs/clusterId passthrough.

### Pitfall gates
- Gate 2: every tool traced MCP->manager->store->JSON; McpCoverage asserts
  real responses including error paths.
- Gate 4: ClusterPresetStore.cpp added to CMakeLists; store test added to
  tests/CMakeLists.txt.
- Gate 9: unknown id -> error result; name may be any string (stored as-is,
  length-capped at 200 chars; no stoi).
- Persistence hygiene: atomic write (temp+move); corrupted file tolerated.

## Steps

1. src/engine/ClusterPresetStore.{h,cpp} + CMakeLists entry.
2. FileLibraryManager: own the store; clusterLibrary gains optional
   saveAs/clusterId out-params (or a small options struct) that populate +
   persist the preset when set; refreshPreset(id) re-runs clusterLibrary with
   the stored recipe; list/get/delete passthroughs.
3. McpTools_Library.cpp: 3 new tools + saveAs/clusterId params on
   cluster_library (schema + handlers).
4. Router_Library.cpp: 3 methods + param passthrough.
5. Tests: tests/unit/engine/cluster_preset_store_test.cpp (new);
   mcp_coverage_test additions; tests/CMakeLists entry.
6. Build + gates G1-G4 with evidence.

## Out of scope (v3 candidates)
- Frontend UI (browser Clusters view); preset rename; per-member "still in
  library" reconciliation beyond missingMemberCount; sharing/export of
  presets; auto-refresh on scan.
