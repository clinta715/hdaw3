# Plan: Library clustering + related-samples over TimbreLib sidecar data

Status: APPROVED (v1.1 — multi-library scoping added after first dispatch)
Date: 2026-08-25; v1.1 addendum same day
Predecessor: docs/plans/timbre-lib-sidecar-integration.md (landed in fde2a38)

## Goal
Let an MCP client request clusters of related samples (variable k, on request)
and nearest-neighbor lookups, computed in-engine from the TimbreLib sidecar
values already ingested (tags/description text) plus the not-yet-ingested
numeric `dsp` feature vector from `<file>.timbre.json`.

## Success Gates (all must pass with evidence)

- [ ] G1: `build-fast.bat test` green; RelWithDebInfo hdaw_tests.exe newer
      than every touched source (Gate 4/15 binary freshness).
- [ ] G2: New `LibraryClustererTest` suite green, minimum coverage:
      determinism (two identical calls -> byte-identical cluster output),
      synthetic 3-group dsp separation clusters correctly, text-only grouping
      by shared tags, `k` respected and clamped to n, auto-k (k=0) picks the
      best silhouette deterministically, entries with no signal land in
      `unassigned`, single-entry library returns 1 cluster, empty library
      returns empty result (no crash), relatedToItem ranks seed's true
      neighbors above unrelated, relatedToQuery matches tag words.
- [ ] G3: `FileLibraryTest` additions green: dspFeatures ingested from the
      sidecar `dsp` dict (all 20 keys required, else features stay empty),
      dspFeatures round-trip through save/load, per-library index JSON with
      schemaVersion < 2 (or missing) is ignored so one rescan re-ingests.
- [ ] G4: `McpCoverageTest` additions green: `cluster_library` on a temp audio
      library with 2 sidecar "families" (dark vs bright) returns 2 clusters
      whose members carry tags; `related_samples` by filePath returns the
      same-family neighbors ranked first; unknown libraryId and bad method
      return tool errors, not crashes.
- [ ] G5: Focused rerun (`FileLibraryTest.*:McpCoverageTest.*:LibraryClustererTest.*`)
      green; full-suite delta contains ONLY the documented pre-existing
      environment failures (RealtimeSafety x4 Debug-only, DiagnosticClapExportMatrix,
      AutoGain.TooLoud, GlobalScale.NonClipping, PluginManagerInProcessVst3).

## Dependency Map (verified via codebase-memory + grep, 2026-08-25)

- Blast radius: `FileLibraryManager` (src/engine) -> `McpTools_Library.cpp`
  (MCP tools) and `Router_Library.cpp` (Qt RPC router). Graph trace_path
  (inbound, search): Router_Library. New pure module `LibraryClusterer` has
  NO callers until wired; both surfaces are additive registrations.
- Upstream: MCP client calls (opencode/agents), frontend `libraryStore.ts`
  (unchanged — additive JSON only, no frontend change in this task).
- Downstream: consumers of cluster output are MCP clients (JSON) and future
  UI. No projection (ReadModel/audio graph/SPSC) is touched.
- God nodes in scope: none (FileLibraryManager is mid-degree; LibraryEntry is
  a data struct).
- Community boundaries crossed: engine -> MCP surface (existing seam, same
  as sidecar commit fde2a38). No ValueTree/listener involvement.
- SPSC paths touched: none. Audio thread: never (compute on command thread).
- Persistence seam: per-library `libraries/<id>.json` gains `dspFeatures`
  array per entry + top-level `schemaVersion` (see Step 3).

## Data contract (from real sidecars, timbre-lib/samples/*.timbre.json)

- Text axis (already ingested): `LibraryEntry.tags` (dsp_words + top-3
  captions + top-3 tags, comma-joined) and `description` (prose).
- DSP axis (new): sidecar `dsp` object with exactly these 20 keys, in this
  fixed order (kDspFeatureKeys in LibraryClusterer.h):
  duration, rms, peak, crest_dB, zcr, centroid, bandwidth, rolloff85,
  rolloff95, flatness, spectral_crest, spec_irregularity, mel_low, mel_mid,
  mel_high, attack_s, decay_s, f0_hz, tonal_fraction, f0_sweep
  Rule: a dsp vector is accepted only if ALL 20 keys are present and finite;
  otherwise features stay empty (no partial vectors, no imputation).

## Vectorization (LibraryClusterer, pure C++, deterministic)

- Text: tokenize [a-z0-9']+ (lowercase, length > 1) over tags+description;
  TF-IDF with document frequency; vocabulary capped to top 512 tokens
  (tiebreak: token string asc). L2-normalize.
- DSP: z-score each of the 20 dims across the library (population std; std==0
  -> dim dropped from scaling by leaving all-zero contribution); L2-normalize.
- Combined vector = concatenation: hybrid = 0.5*text + 0.5*dsp; method
  `text` or `dsp` uses only that block. A missing block contributes zeros
  (its sibling block keeps the entry comparable). Entries with BOTH blocks
  missing are excluded to `unassigned`.
- Distance: Euclidean on the combined vector.

## Clustering (LibraryClusterer)

- k-means with k-means++ init, fixed PRNG (std::mt19937, seed 42); max 100
  iterations; empty cluster -> reseed at the entry farthest from its centroid.
- k semantics: `k` given -> clamp to [1, n]; `k` omitted (0) -> auto: try
  k = 2..min(8, max(2, n/3)), pick max mean silhouette (ties -> smaller k).
- Output: clusters sorted by size desc then label asc; ids "c1".."cK".
  Label = highest summed TF-IDF token across member texts (fallback
  "cluster N"). Members carry similarity-to-centroid.
- Complexity guard: silhouette is O(n^2) per k — acceptable to ~10k entries;
  note in output when n > 4000 (field `note`), no hard cap in v1.

## MCP + RPC surface (MCP parity rule)

- MCP tools (McpTools_Library.cpp, same objSchema style as search_library):
  - `cluster_library` {libraryIds (array of strings, OPTIONAL — omitted/empty
    = cluster ALL audio-type libraries; provided = union of entries from
    exactly those libraries; any unknown id -> error listing them, never
    silently skipped), k (integer, optional), method (string
    "hybrid"|"text"|"dsp", optional, default "hybrid")}
    -> {method, k, clusters: [{id, label, size, members: [{name, path, tags,
    description, similarity}]}], unassigned: [{name, path}], note?}
  - `related_samples` {libraryIds (same scoping semantics as cluster_library:
    optional, omitted = all audio libraries), filePath (string, optional),
    query (string, optional — exactly one of filePath/query required),
    limit (integer, optional, default 10, max 100)}
    -> {method, seed?: {name, path}, results: [{name, path, tags,
    description, similarity}]} ranked by similarity desc. File seed excludes
    itself. Query seed builds a text-axis pseudo-vector (idf-weighted).
- Router (Router_Library.cpp): methods "cluster" and "related" mirroring the
  same params (libraryIds as QJsonArray of strings), QJsonArray/QJsonObject
  responses (frontend later).
- Error cases: unknown libraryId, empty library, no usable signal entries,
  bad method, missing both filePath and query -> tool error text, no crash.

## Pitfall Gates Triggered

- Gate 2 (full path): every new RPC method traced MCP->manager->clusterer->JSON
  and covered by McpCoverageTest assertions on real responses.
- Gate 4: new LibraryClusterer.cpp must be added to CMakeLists.txt source
  list; LibraryClusterer_test to tests/CMakeLists.txt.
- Gate 9: no stoi on unvalidated input; method string parsed via comparison
  whitelist, unknown -> error. getEntry-by-path guarded (empty -> error).
- Gate 3/12/13/16: not applicable — no audio-thread, graph, or DSP-state
  involvement; compute on the calling (MCP/Qt) thread under a copied entry
  snapshot (mutex held only for the copy).
- Anti-patterns: none applicable (single-call RPCs, no CSS, no loops of RPCs).

## Steps

1. `src/engine/LibraryClusterer.{h,cpp}` (new, pure): types ClusterItem,
   ClusterOptions, ClusterOutcome, RelatedResult; functions cluster(),
   relatedToItem(), relatedToQuery(). No JUCE audio deps; juce::String ok.
   Add LibraryClusterer.cpp to CMakeLists.txt.
2. `FileLibraryManager`: extend LibraryEntry with
   `std::vector<double> dspFeatures;`; extend applyTimbreSidecar to parse the
   sidecar `dsp` dict (all-20-keys rule); persist/load dspFeatures in
   save/loadLibraryEntries; add top-level `schemaVersion: 2` and ignore
   caches with version < 2 (one-time rescan).
3. `FileLibraryManager::clusterLibrary(const juce::StringArray& libraryIds,
   int k, const juce::String& method)` and `relatedSamples(const
   juce::StringArray& libraryIds, const juce::String& filePath, const
   juce::String& query, int limit)` — empty array = ALL audio-type libraries
   (midi excluded); provided = union of those libraries only, unknown ids ->
   error. Copy entries under mutex,
   build ClusterItems (text = tags + " " + description), call clusterer,
   map results back to LibraryEntry fields.
4. `McpTools_Library.cpp`: register cluster_library + related_samples
   (objSchema style, JSON responses incl. per-member tags/description).
5. `Router_Library.cpp`: "cluster" + "related" methods with identical params.
6. Tests:
   - tests/unit/engine/library_clusterer_test.cpp (new suite, >= 10 cases
     from G2; synthetic fixtures only, no files on disk).
   - tests/unit/engine/file_library_test.cpp: 3 new cases from G3
     (ingest/persist/schemaVersion).
   - tests/integration/mcp/mcp_coverage_test.cpp: 3-4 new cases from G4
     (temp WAVs + sidecar families, incl. error paths).
   - Add library_clusterer_test.cpp to tests/CMakeLists.txt.
7. Build + run gates G1-G5; report evidence (command + tail summary).

## Out of scope (v2 candidates)

- Embedding-based similarity (pipeline change); HDBSCAN/outlier semantics;
  frontend UI (cluster view in browser region); cluster persistence/caching;
  MIDI-library clustering (audio libraries only for now).
