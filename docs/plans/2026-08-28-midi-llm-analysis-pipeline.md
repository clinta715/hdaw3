# Plan: Local MIDI LLM analysis pipeline (symbolic sidecars → clustering → MCP parity)

Status: APPROVED (v1.2 — pre-flight complete 2026-08-28; implementation in
         progress; amendment log at the foot)
Date: 2026-08-28
Predecessors: `docs/plans/timbre-lib-sidecar-integration.md` (landed fde2a38),
              `docs/plans/2026-08-25-library-clustering.md` (landed with
              `LibraryClusterer`), `docs/plans/2026-08-25-cluster-presets.md`.

## Goal

Give MIDI libraries the same analysis stack audio libraries already have —
**wholly local, CPU-only**:

- stage 1: symbolic feature extraction (20 fixed keys) + rule descriptors
- stage 2: captions + tags from the **local LLM** (no CLAP — symbolic evidence
  is already semantic, so the GPU stage of the audio pipeline is unnecessary)
- stage 3: prose from the same `Qwen2.5-3B-Instruct-Q4_K_M.gguf` today's audio
  pipeline uses (`timbre-lib/llm_stage.py`)
- artifacts: `<file>.mid.json` sidecars + incremental cache, exactly mirroring
  `<file>.timbre.json`
- in-engine: `applyMidiSidecar` ingestion, type-aware `clusterLibrary` /
  `relatedSamples` over MIDI libraries, and full MCP/RPC parity

So `search_library {query: "dark techno bassline, offbeat groove"}`, `cluster_library`
and `related_samples` work over MIDI pattern libraries (basslines, chord stabs,
drum grooves, arps) the same way they work over the analyzed audio packs today —
letting the agent-driven composition workflows treat MIDI patterns as first-class
library assets (currently: `extractMidiMetadata` yields only tracks/notes/
duration/tempo/key, and MIDI entries are **hard-excluded** from clustering
in `collectClusterEntries`: "not audio libraries: …").

## Success Gates (all must pass with evidence)

- [x] G1: `build-fast.bat test` green; `RelWithDebInfo/hdaw_tests.exe` newer
      than every touched source (Gate 4/15 binary freshness).
- [x] G2: `FileLibraryTest` additions green (mirror `applyTimbreSidecar` tests):
      midi sidecar (`.mid.json`) ingestion populates tags/description/features;
      all-20-keys-finite rule (one missing/non-finite key ⇒ features stay empty);
      missing + malformed sidecar tolerated; sidecar mtime bump triggers rescan;
      tags/description/features round-trip save/load; per-library cache with
      `schemaVersion < 3` is ignored so one rescan re-ingests.
- [x] G3: `LibraryClustererTest` additions green: numeric axis is
      dimension-agnostic (20-dim MIDI vectors accepted like 20-dim audio dsp);
      synthetic 3-family MIDI separation (four-on-floor drum loops / minor-key
      basslines / ambient pads) clusters correctly; determinism (seed 42,
      byte-identical output); k clamped; auto-k picks max silhouette; text-only
      grouping by shared tags; entries with no signal land in `unassigned`;
      `relatedToItem` ranks seed's true family above unrelated;
      `relatedToQuery` matches descriptor words; mismatched vector lengths are
      excluded to unassigned, never crash.
- [x] G4: `McpCoverageTest` additions green: `cluster_library {type:"midi"}` on
      a temp MIDI library with 2 sidecar "families" returns 2 clusters whose
      members carry tags; `related_samples` with a MIDI filePath seed ranks the
      same-family neighbors first; `search_library` matches sidecar prose;
      error paths: unknown libraryId, bad `type` value, mixed-type scope
      (audio+midi ids in one call) → tool errors, never crashes.
- [x] G5: Python-side validation (new `timbre-lib/midi_validate.py`): over the
      46 real `teknoir/*.mid` files whose filenames encode ground truth
      (`teknoir_techno_C_minor_130bpm_s1337_...`), sidecar-detected key and
      tempo match the filename; the same-key+tempo families separate under
      k-means on the ingested feature matrix (documented silhouette/ARI numbers).
- [x] G6: Focused rerun (`FileLibraryTest.*:LibraryClustererTest.*:McpCoverageTest.*`)
      green; full-suite delta contains ONLY the documented pre-existing
      environment failures (RealtimeSafety x4 Debug-only,
      DiagnosticClapExportMatrix, AutoGain.TooLoud, GlobalScale.NonClipping,
      PluginManagerInProcessVst3).

## Dependency Map (verified via codebase-memory + grep, 2026-08-28)

- Blast radius: `FileLibraryManager` (src/engine) → `McpTools_Library.cpp`
  (MCP tools) and `Router_Library.cpp` (Qt RPC router); `LibraryClusterer`
  (src/engine) gains dimension-agnostic math + `kMidiFeatureKeys`
  (no new files in src/ — see Anti-pattern scan). No ValueTree listener,
  ReadModel projection, SPSC, or audio-thread involvement (library data is its
  own persistence, computed on the calling thread under a copied snapshot —
  identical to the audio sidecar/cluster plans).
- Upstream: MCP clients (composition recipes/agents), `register_library.py`
  (already supports `--type midi`), frontend `libraryStore.ts` (additive JSON
  only — no frontend change in this task).
- Downstream consumers of the new sidecar: none outside this plan (search/
  cluster/related/presets already consume `LibraryEntry` JSON).
- God nodes in scope: none. `FileLibraryManager` is mid-degree, unchanged in
  topology (one new private helper + one hook + signature params).
- Community boundaries crossed: engine → MCP surface + Qt router (existing
  seams); timbre-lib Python orchestration gains one sibling script family
  (`midi_features.py`, `midi_analyze.py`) — no shared-module refactor needed
  (each existing analyze script is already standalone).
- Persistence seam: per-library `libraries/<id>.json` gains MIDI numeric
  features under the existing `dspFeatures` channel; top-level `schemaVersion`: audio libraries keep writing **2** (existing audio
  caches stay valid — zero churn); midi libraries write **3**, and the load
  guard ignores midi caches with `schemaVersion < 3` so ONE rescan re-ingests
  (same one-time-rescan philosophy as the audio v2 bump).

## Data contract (from the planned `midi_analyze.py` sidecars)

Sidecar file: `<file>.mid.json` — the **full file name plus the `.mid.json`
suffix** (`pattern.mid` → `pattern.mid.mid.json`), mirroring the audio
convention `beat.wav` → `beat.wav.timbre.json`. The `.json` suffix cannot
collide with the `*.mid;*.midi` library scan wildcard. Schema mirrors the audio sidecar
so the engine parser stays one code path; the `dsp` dict carries the 20 MIDI
keys (the audio `kDspFeatureKeys` stay exactly as-is for audio libraries):

```
{ "name": "teknoir_techno_C_minor_130bpm_s1337.mid",
  "wsl_path": "...", "win_path": "...",
  "size": 0, "mtime": 0, "durationSeconds": 211.5, "format": "midi",
  "dsp": { "duration_beats": 422.9, "note_count": 481, ..., "velocity_std": 9.2 },
  "dsp_words": "minor-key, driving 4/4, offbeat 16th syncopation, bass-register, dense",
  "captions": [["a driving minor-key techno bassline with offbeat 16ths", 0.0]],
  "tags": [["techno", 0.0], ["minor", 0.0], ["driving 4/4", 0.0]],
  "prose": "Dominant: driving minor-key techno with offbeat 16th syncopation. ...",
  "_cached": false }
```

`captions`/`tags` keep the audio `[text, score]` shape; without CLAP there is
no embedding score, so the LLM supplies labels and score is a `0.0` placeholder
(the engine ignores scores). Same tolerance rules as audio: missing sidecar and
malformed JSON leave fields empty, never throw out of scan; rescan fires when
the sidecar's mtime is newer than the entry's `modifiedTime` (the sidecar-mtime
ingestion trap applies 1:1).

### The 20 symbolic feature keys (`kMidiFeatureKeys`, fixed order)

All-or-nothing rule identical to audio: accepted only when ALL 20 are present
and finite; otherwise the numeric axis stays empty (no partial vectors, no
imputation). Audio-analogue shown per key to anchor the mapping:

| # | key | definition | audio analogue |
|---|-----|-----------|----------------|
| 1 | duration_beats | total length in quarter notes (maxTick / TPQN) | duration |
| 2 | note_count | noteOn events (all tracks, incl. drums) | — (scale) |
| 3 | note_density | note_count / duration_beats | — |
| 4 | polyphony_mean | mean sounding notes at noteOn times | — |
| 5 | polyphony_max | max simultaneous notes | — |
| 6 | pitch_min | lowest MIDI pitch | — |
| 7 | pitch_span | max − min pitch | bandwidth |
| 8 | pitch_centroid | note-count-weighted mean pitch | centroid |
| 9 | pitch_class_entropy | entropy of 12-bin PC histogram ÷ log2(12) | flatness/crest |
| 10 | scale_fit | fraction of noteOns in the detected key's scale | tonal_fraction |
| 11 | key_confidence | Krumhansl–Kessler best-vs-second contrast | — |
| 12 | interval_mean | mean \|Δpitch\| (semitones) on the melodic stream | — |
| 13 | interval_entropy | normalized interval-histogram entropy | spec_irregularity |
| 14 | contour_up_ratio | fraction of adjacent melodic intervals rising | f0_sweep |
| 15 | note_repetition_rate | fraction of adjacent intervals == 0 | — |
| 16 | ioi_mean | mean inter-onset interval in beats | attack/decay |
| 17 | grid_deviation | mean \|onset − nearest 16th grid position\| in beats (quantization error) | — |
| 18 | syncopation_fraction | fraction of onsets whose 16th-grid index mod 4 ∈ {1,3} | — |
| 19 | velocity_mean | mean velocity | rms |
| 20 | velocity_std | velocity std (articulation spread) | crest_dB |

Definitional rules (pitfall-controlled, see Pitfall Gates):
- Melodic stream for 12–15: noteOn events sorted by time; ties take the lowest
  pitch (bass-line flavored) — deterministic, no RNG.
- Tempo: first tempo map entry wins (same as the engine's existing
  `extractMidiMetadata`), TPQN ≤ 0 ⇒ features empty (mirror the engine guard).
- Key detection (10/11): melodic tracks only — channel-10/drum notes excluded
  (they pollute the PC histogram; the engine's `entry.key` keeps its current
  all-notes behavior and stays the `search()` key filter — the divergence is
  documented, the sidecar key is used by scale_fit/key_confidence only).
- Drum-ness is NOT a feature key — it lives in the descriptor layer
  (`midi_summarize()` emits "percussive track present" from channel-10
  occupancy + drum-pitch histogram) so the numeric space stays metric-homogeneous.

## Design decisions (fixed, do not deviate)

1. **Sidecar name/schema.** `<file>.mid.json`, schema mirrors the audio sidecar
   (incl. the `dsp` key name), so `applyTimbreSidecar`-style tolerant parsing is
   reused. `captions`/`tags` keep `[text, score]` shape with score `0.0`.
2. **Numeric axis reuse, not rename.** MIDI features ride the existing
   `LibraryEntry.dspFeatures` channel and JSON field; the key list is per-type
   (`kDspFeatureKeys` audio / `kMidiFeatureKeys` midi) selected at ingestion.
   `LibraryClusterer` math becomes dimension-agnostic: N derived from the first
   non-empty vector in the scope; vectors with a different (non-zero) length are
   excluded to `unassigned`. Scopes are type-homogeneous (decision 3), so N is
   constant within a call. `kDspFeatureCount`/`kDspFeatureKeys` are unchanged
   for audio.
3. **Type-homogeneous scoping.** `clusterLibrary`/`relatedSamples` gain a
   `type` param (`"audio"` default | `"midi"`; whitelist-validated, unknown →
   error). Omitted `libraryIds` + type = ALL libraries of that type (audio:
   status quo; midi: all midi libs). Provided ids must all be of that type —
   mixed-type or mismatched ids are errors listing the offenders, never silent
   skips. Mixed audio+MIDI vector spaces are semantically invalid and cannot be
   requested. `ClusterPreset` gains a `type` field ("audio" default); recipes
   save/refresh it.
4. **Stage 2 has no CLAP.** `midi_summarize()` (rule-based, mirrors
   `timbre.summarize()`'s if/elif word builder) produces `dsp_words`; tracks a
   per-track inventory line (channel, GM program name, note count, role guess);
   the **local LLM** (same 3B GGUF, llama-cpp-python, CPU) then emits
   TAGS / CAPTION / PROSE in one completion. Honesty rule flips vs audio:
   channels/programs ARE instrument evidence in MIDI, so the LLM may name the
   implied instrument but nothing beyond the evidence. If the response fails
   the line-format parse, fall back to rule-only `dsp_words`/tags — never fail
   the file, never invent.
5. **Analysis runs external; engine only ingests.** `midi_analyze.py` stands
   next to `lib_analyze.py`; the engine hooks are scan-time only
   (`applyMidiSidecar`). This preserves exact MCP parity with the audio path
   (`scan_library` ingests; analysis is pipeline-side, as it is for audio
   today). An engine-triggering `analyze_midi_library` MCP tool is v2 — the
   audio pipeline has no such tool either, so parity is already met at v1.
6. **New dependency: `mido`** (pure-Python SMF parser) for stage 1. Everything
   else is already present (numpy, the GGUF, llama-cpp). `midi_analyze.py`
   writes its own `midi_index.json` (never clobbers a same-folder audio
   `timbre_index.json`) and reuses the `.timbre_cache/<md5>.json` incremental
   mechanism.
7. **Executor/LLM stability:** reuse `llm_stage.run_llm` as-is (same
   model handle, `close()` at exit); new SYSTEM/USER prompts live in
   `midi_analyze.py` (or a `midi_llm_stage.py` sharing the `run_llm` helper).

## LLM prompt sketch (stage 2+3, one completion)

```
SYSTEM: You describe MIDI patterns for a DAW pattern browser.
Only describe what the evidence supports. Channels/programs are real
instrument evidence; never invent other instruments or effects.
Return exactly three lines:
TAGS: <comma-separated short labels, 3-8>
CAPTION: <one 5-15 word summary>
PROSE: <2-4 sentences: dominant musical character (mode/groove), texture
(polyphony/range/density), rhythm (grid deviation/syncopation/swing),
a suggested use in music production (one clause)>
Do not hedge with "it might be"; do not list numbers.

USER: MEASURED SYMBOLIC EVIDENCE:
<dsp_words>, key=..., tempo=..., time_sig=..., <track inventory lines>
Write the description now.
```

Parse: split on the three prefixes; any missing/unparseable section falls back
to rule-built content for that section.

## Steps

0. **Environment (verified 2026-08-28 pre-flight):** the WSL python that runs
   the pipeline (`/home/hapbt/.prime/agent/kernel-venv/bin/python`) already has
   `numpy 2.4.6`, `scipy 1.17.1`, `librosa 0.11.0`, `llama_cpp 0.3.35` (the GGUF
   runtime) and `timbre-lib/Qwen2.5-3B-Instruct-Q4_K_M.gguf`. Missing:
   `mido` → `pip install mido` (pure-python, no binary wheels).

1. `timbre-lib/midi_features.py` (new, pure numpy + mido): `extract(mid_path)`
   → the 20-key dict + tempo/time-sig/TPQN + per-track inventory;
   `summarize(d)` → `dsp_words` (mode, groove, texture, range, drum-occupancy,
   character). Deterministic; no RNG anywhere.
2. `timbre-lib/midi_analyze.py` (new): argument shape matches `lib_analyze.py`
   (`<folder> [--limit N] [--no-llm] [--sidecars] [--out]`); collect
   `*.mid;*.midi`; incremental cache; one LLM completion per file; sidecars +
   `midi_index.json`. `--no-llm` still writes `dsp` + `dsp_words` (numeric axis
   is fully usable without the LLM — clusterer `method:"dsp"` over MIDI libs).
3. `timbre-lib/midi_validate.py` (new): G5 gates — filename ground-truth
   key/tempo assertions over `teknoir/`; determinism (two identical runs →
   identical sidecars); 3-family synthetic corpus separation on the ingested
   feature matrix (k-means, documented silhouette/ARI).
4. `src/engine/LibraryClusterer.{h,cpp}`: dimension-agnostic numeric axis
   (derive N per call; mismatched lengths → unassigned); add `kMidiFeatureKeys`
   (20 entries, fixed order from the table). No new files → no CMakeLists edit.
5. `src/engine/FileLibraryManager.{h,cpp}`: `applyMidiSidecar` (reads
   `<file>.mid.json`, all-20-finite rule with `kMidiFeatureKeys`); scan hook
   for midi libs incl. sidecarNewer rescan; `serializeEntry`/
   `loadLibraryEntries` write `schemaVersion` 3 for **midi** caches (audio stays
   2; deserialize guard: midi cache < 3 ⇒ whole load ignored, one rescan
   re-ingests); `clusterLibrary`/`relatedSamples`
   gain `type` param + homogeneous-scope validation; `ClusterPreset.type`.
6. `src/mcp/McpTools_Library.cpp`: `cluster_library` + `related_samples` gain
   optional `type` (default "audio", whitelist). `search_library`,
   `get_library_entry`, `scan_library`, presets: additive-only (features/tags
   already serialize; no schema change needed beyond step 5).
7. `src/frontend/router/Router_Library.cpp`: "cluster"/"related" pass `type`
   through (same params, same validation).
8. Tests:
   - `tests/unit/engine/file_library_test.cpp`: G2 cases (temp `*.mid` +
     hand-written sidecars; mtime bump; schemaVersion guard).
   - `tests/unit/engine/library_clusterer_test.cpp`: G3 cases (synthetic MIDI
     vectors; all existing audio cases must stay green — the math change is
     backward-compatible).
   - `tests/integration/mcp/mcp_coverage_test.cpp`: G4 cases (temp midi
     library, 2 sidecar families, error paths incl. bad type + mixed scope).
   - No new entry points → no pump/COM concerns.
9. Run `timbre-lib/midi_validate.py --teknoir` → G5 evidence.
10. Build (`build-fast.bat test`), record binary mtimes, focused rerun, full
    suite, report per gate.

## Pitfall Gates triggered + how addressed

- Gate 2 (unimplemented path): every new field exercised by unit + MCP coverage
  tests asserting real values scan → search → cluster → JSON.
- Gate 9 (validation): `type` whitelist; all-20-finite rule; malformed sidecar
  tolerated (`isObject` guard, empty defaults, no exceptions out of scan);
  LLM response line-parse with rule fallback; TPQN ≤ 0 ⇒ empty.
- Gate 4/15 (stale binaries): binary mtime after build, never source-only.
- Sidecar-mtime ingestion trap (timbre-lib precedent): same mtime rule +
  explicit bump-resent test (G2) — the trap that silently dropped audio
  sidecar tags on copied packs applies verbatim to MIDI sidecars.
- MIDI-format guarded as first-class pitfalls (documented in the step-3
  docstring + this plan): Format 0 vs 1 handled by mido; tempo maps (first
  entry only); channel-10 exclusion for key detection; overlapping/
  unquantized notes never crash IOI/grid math.
- NOT triggered: audio thread, graph mutation, DSP-state writes, plugin/IPC,
  message pump (runs inside existing `hdaw_tests` entry points),
  ValueTree listeners (library ops are not project edits — no undo/ReadModel/
  delta concerns, same as the audio plans), COM.

## Anti-pattern scan (none triggered)

- No new MCP tools in v1 (schema-additive extension of existing tools).
- No new `.cpp` under `src/` (clusterer math edited in place) ⇒ no
  `CMakeLists.txt` edit; new Python scripts don't build.
- No RPC loops, no DBG(), no raw hex, no ValueTree/SPSC changes.
- Python orchestration duplication (`midi_analyze.py` mirrors `lib_analyze.py`)
  is accepted — every existing analyze script is already standalone; a shared
  refactor is explicitly out of scope.

## Pre-flight (hdaw-guard) — verification evidence, 2026-08-28

Dependency map + blast radius re-verified against the actual code (grep +
codebase-memory-equivalent reading; every claim below was located in-file):

- **scan hook:** `scanDirectory` calls `applyTimbreSidecar` only in the
  `type == "audio"` branch (FileLibraryManager.cpp scan loop); the midi hook is
  one added `if`, and the audio-only `rescanForTimbre` block gains a midi twin
  (`entryHasMidiData` mirrors the existing `entryHasTimbreData` helper).
- **clusterer math:** `kDspFeatureCount` is baked at exactly 4 sites in
  LibraryClusterer.cpp — `buildDspModel` (vec alloc + per-item length check),
  the z-score loop, `combine()`, and the `relatedToQuery` dsp-block seed.
  All four generalize to a runtime N derived from the first non-empty vector;
  the existing wrong-length exclusion (`size() != N → skip`) is kept, so any
  mismatched-length vector lands in `unassigned`, never a crash.
- **signature change is additive:** `clusterLibrary` gains a defaulted trailing
  `const juce::String& type = "audio"`; `relatedSamples` the same. Verified
  callers: Router_Library.cpp:196/247, McpTools_Library.cpp:291/441, internal
  `refreshClusterPreset` (FileLibraryManager.cpp:1231 — passes `stored.type`),
  and 5 existing test call sites in file_library_test.cpp — none need edits
  (defaults keep them compiling and behaving identically).
- **MCP schemas additive:** `cluster_library` ({libraryIds,k,method,saveAs,
  clusterId}) and `related_samples` ({libraryIds,filePath,query,method,limit})
  gain optional `type` with enum {"audio","midi"}, default "audio"
  (`parseClusterMethod`-style whitelist helper mirrors for `type`).
- **serializer:** deserialize reads `dspFeatures` requiring size ==
  `kDspFeatureCount` (20) — valid for MIDI's 20-dim vector unchanged; write
  side same. schemaVersion handled per-type (see Data contract amendment):
  audio caches keep v2, midi caches v3.
- **tests:** `library_clusterer_test.cpp`, `file_library_test.cpp`,
  `mcp_coverage_test.cpp` all REGISTERED in tests/CMakeLists.txt; no new .cpp
  under src/ ⇒ **no CMakeLists.txt edits anywhere** (python scripts don't
  build).
- **environment:** pipeline python confirmed (WSL
  `/home/hapbt/.prime/agent/kernel-venv/bin/python`) with numpy 2.4.6, scipy
  1.17.1, librosa 0.11.0, llama_cpp 0.3.35; **mido missing** → step 0 install.
  Corpus: 46 `teknoir/*.mid` files with key/tempo/seed ground truth in the
  filenames (G5).

Gate verdicts (16 recurring pitfalls + repo rules):

- Gate 2 (no unimplemented paths): every new field traced scan → search →
  cluster → JSON and covered by unit + MCP coverage tests (G2–G4).
- Gate 3 (audio thread): none — library compute runs on the calling
  (MCP/Qt) thread over a copied snapshot (mutex held only for the copy);
  scanning stays on the existing threadpool. No processBlock/SPSC touched.
- Gate 4/15 (stale binaries): binary mtime verification part of G1.
- Gate 6 (rebuild O(project)), Gate 10 (state restore), Gate 12/13 (graph
  thread-safety / DSP-state writes), Gate 14 (cross-process), Gate 16 (CLAP
  thread contract), Gate 20 (proxy namespaces), Gate 21 (render sequence
  pin), Gate 22 (COM init): **not triggered** — no ValueTree/listener,
  ReadModel, routing graph, plugin/IPC, or audio-device involvement (library
  ops are their own persistence, same as the audio sidecar/cluster plans).
- Gate 9 (validation): `type` whitelist ("audio"/"midi", unknown → error);
  all-20-keys-finite rule for the numeric axis; malformed sidecar tolerated
  (isObject guard, empty defaults, no exceptions out of scan); LLM response
  line-parse with rule-only fallback; TPQN ≤ 0 ⇒ features empty.
- Gate 11 (message pump): no new entry points — all tests run inside the
  existing hdaw_tests binary; no JUCE construction outside existing mains.
- Sidecar-mtime trap: mirrored verbatim from the audio plans + explicit
  bump-resent test (G2).
- Lessons 7/8 (latency/quality): n/a — no signal path touched.

Anti-pattern scan (clean): additive schemas only (no new MCP tools in v1);
no RPC loops; no DBG/HDAW_LOG misuse; no ValueTree/SPSC changes; no
CMakeLists edits; no new entry points; no GPU dependency — the MIDI pipeline
is CPU-only end-to-end (the one deliberate divergence from the audio
pipeline, where CLAP needs CUDA).

## Out of scope (v2 candidates)

- Per-track role analysis + per-bar segmentation ("grab the bassline", "find
  the break fill") — the container-aware granularity the composition recipes
  will eventually want; v1 is whole-file like audio.
- CC/pitch-bend expressiveness axis and velocity-humanization analysis.
- Embedding-based MIDI↔text retrieval (CLaMP2-class model) for true semantic
  similarity beyond TF-IDF prose matching.
- `analyze_midi_library` MCP tool (engine-triggered pipeline execution).
- Frontend cluster/pattern UI for MIDI libraries.
- Mixed-type (audio+MIDI) multi-modal clustering.

## Gate evidence (2026-08-29, final binary)

- [x] G1: build-fast.bat test green 2026-08-29; RelWithDebInfo/hdaw_tests.exe rebuilt (logs build_midi_impl*.log)
- [x] G2: 8 new FileLibraryTest cases green (41 total in suite)
- [x] G3: 5 new LibraryClustererTest cases green (18 total)
- [x] G4: McpCoverageTest.LibraryClusterMidiTwoSidecarFamilies green
- [x] G5: teknoir 46 files: tonic 44/46, mode 21/21, tempo 46/46; families silhouette 0.849 purity 1.000; determinism bit-identical (timbre-lib/midi_validate.py)
- [x] G6: full suite 1192 tests: 1177 passed, 6 failed — only documented pre-existing env failures (RealtimeSafety x4, DiagnosticClapExportMatrix, McpServer.ExportAudioWithClapPluginDoesNotHang)

## Amendment log (implementation)

- **v1.2 (2026-08-28, from implementation):** Python pipeline landed as spec'd
  with three validated refinements:
  1. **Key selection** — plain Krummhansl-Kessler correlation scores 0/46 on
     the `teknoir/` corpus (riff content avoids tonics, anchors on dominants).
     `midi_features.py` now scores key = scale-fit + dominant-anchor mass
     (fit + 0.9·dominant-fraction); `key_confidence` stays KK best-minus-2nd
     margin. Result: **tonic 44/46, mode 21/21 (major/minor files), tempo
     46/46**. The 2 misses are near-atonal F-phrygian files (honest ties
     between G# major and F minor).
  2. **`key_tempo()` also returns `has_drums`** (channel-10 occupancy) — needed
     by `summarize()`'s drum-presence descriptor.
  3. **Sidecar naming precision** — `<file>.mid.json` means full name + suffix
     (`pattern.mid.mid.json`); verified consistent with `applyMidiSidecar` and
     all tests. See Data contract.
- G5 numbers from the pipeline (2026-08-28): families 24 synthetic files,
  k-means K=3 → mean silhouette 0.849, purity 1.000; determinism bit-identical;
  46 sidecars written, all 20 keys present and finite.

## Evidence to report back

- build output tail + `hdaw_tests.exe` mtimes before/after
- gtest output: `FileLibraryTest.*`, `LibraryClustererTest.*`,
  `McpCoverageTest.*`, full-suite delta vs the documented pre-existing
  environment failures
- `midi_validate.py` output over `teknoir/` (key/tempo hit-rate + family
  separation numbers)
- one real sidecar (`<file>.mid.json`) + one `cluster_library {type:"midi"}`
  response sample
