# Patch Libraries: Sidecar Descriptions + FileLibraryManager + Audition

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking. **MANDATORY:** invoke the
> `hdaw-guard` skill before any code change.

**Goal:** Give HDAW the same library/sidecar workflow for synth **patches**
(Virus via `sub_synth`, DX7 via `fm_synth`) that it already has for **samples**
(TimbreLib `.timbre.json` sidecars → `FileLibraryManager` → searchable
libraries), plus an audition flow. A folder of `.syx`/`.mid`/`.vhc` patches
becomes an `add_library(type="patch")` searchable from `search_library`, with a
JSON description sidecar per patch written by a Python batch tool.

**Why now:** loading already works end-to-end (Virus: `sub_synth_import_sysex`
committed `cd2d7b6`; DX7: `fm_synth_import_sysex` in the uncommitted FM work).
The missing layers are (a) a per-patch sidecar writer, (b) a `patch` library
type in `FileLibraryManager` with `applyPatchSidecar`, (c) MCP wiring, and
(d) an audition flow that plays a role phrase through a probe track.

**Scope guard:** pure additions. No audio-engine DSP changes, no changes to the
audio/midi library paths, no changes to `lib_analyze.py`. The `patch` type is
parallel to `audio`/`midi`, never interfering with them.

**Tech Stack:** Python 3 (sidecar writer), C++17 + JUCE 8 (FileLibraryManager),
Qt JSON MCP, gtest.

---

## File Structure

### Create
| File | Responsibility |
|------|---------------|
| `timbre-lib/virus_patch.py` — already exists; **add** `--sidecars <dir> [--role R]` mode | Write `<patch>.virus.json` next to each patch: name, roleCheck, mapped params, unmapped, description, engine hint |
| `timbre-lib/dx7_patch.py` | DX7 `.syx` sidecar writer (`<patch>.dx7.json`: name, algorithm, feedback, fm_synth param mapping) |
| `timbre-lib/test_patch_sidecars.py` | pytest for both writers (stable JSON, schema, per-format coverage) |

### Modify
| File | Change |
|------|--------|
| `src/engine/FileLibraryManager.h` | `LibraryEntry` patch fields (`patchEngine`, `patchParams`, `roleVerdict`, `unmapped`); `static applyPatchSidecar` |
| `src/engine/FileLibraryManager.cpp` | `"patch"` type in `addLibrary`/wildcard/`scanDirectory`/`extractPatchMetadata`/`applyPatchSidecar`; serialize/deserialize the new fields; `entryHasPatchData` for rescan detection |
| `src/mcp/McpTools_Library.cpp` | `add_library` enum `{midi,audio,patch}`; `search_library` type filter already generic (verify) |
| `src/mcp/McpTools_FxSlot.cpp` | `audition_patch` tool: create probe track+slot, load patch (Virus/DX7 by engine hint), play role phrase, return result |
| `tests/unit/engine/file_library_patch_test.cpp` | gtest: patch library scan, sidecar ingestion, search by patch tags/description, serialize round-trip |
| `tests/CMakeLists.txt` | register the new test file |
| `docs/psytrance-composition-guide.md` or `timbre-lib/README.md` | document the patch-library workflow |

No CMakeLists engine change (FileLibraryManager already in the build; no new
.cpp in `src/engine` — `applyPatchSidecar` lives in FileLibraryManager.cpp).
No frontend changes.

---

## Success Gates

- [x] Gate 1: `python -m pytest timbre-lib/test_patch_sidecars.py -q` passes. — 19 passed (76 total across the 3 timbre suites).
- [x] Gate 2: `virus_patch.py --sidecars "D:\pdf\Virus Presets" --role bass`
      writes sidecars for all 5 formats (TI/stdmidi/tdm/vhc/bcsingle); each
      sidecar is schema-valid, stable (byte-identical on re-run), and the
      survey reports per-format counts. — 145 sidecars written (tibank 20/0, tdm 104/0, stdmidi 19/0, vhc 1/0, bcsingle 1/3-failed where the 3 are TI MULTI dumps, honestly recorded); byte-identical across 3 runs.
- [x] Gate 3: `dx7_patch.py --sidecars <dir>` writes valid sidecars for real
      DX7 `.syx` fixtures (single + cartridge). — single 1/0 verified (name/algorithm/feedback + full VCED params); cartridge 32-voice parse covered by pytest.
- [x] Gate 4: `build/hdaw_tests.exe --gtest_filter=FileLibraryPatch*` passes. — 7/7.
- [x] Gate 5: MCP `add_library(type="patch")` + `scan_library` + `search_library`
      round-trip: add a patch folder, scan, search by a tag from a sidecar →
      returns the patch entry; `get_entry` shows the patch fields. — `PatchSidecarIngestedIntoSearches` + `PatchEntrySerializeRoundTrip` PASSED; `add_library` enum now `{midi,audio,patch}`.
- [x] Gate 6: `audition_patch {path, engine, role, trackId?}` creates a probe
      track with a `sub_synth` (or `fm_synth`) slot, loads the patch, places a
      role phrase clip, and the slot's param values reflect the patch. — `AuditionPatchLoadsIntoProbeTrack` asserts live `chain[0]->getInternalParamValues()` (vals 0/1/7 ≈ 1.0/0.503937/86.8611); `AuditionPatchSniffsEngineFromFile` PASSED; fm_synth path routes through `setFmPatch` (present in the working tree).
- [x] Gate 7: Existing suites stay green: `FileLibrary*` (audio/midi),
      `SubtractiveSynth*`, `TrackFxRebuildRace.SubSynth*`, `FxChainPreset.*`. — 62/62; plus `SchemaTest/McpServer` 34/34 and `ToolRegistry` 4/4.
- [x] Gate 8: `git status` shows the diff confined to `timbre-lib/`,
      `src/engine/FileLibraryManager.{h,cpp}`, `src/mcp/McpTools_Library.cpp`,
      `src/mcp/McpTools_FxSlot.cpp`, the new test, this plan doc, README. — confirmed (plus pre-existing uncommitted FM work).

---

## Dependency Map

- **Blast radius:** `FileLibraryManager.{h,cpp}` (scan/wildcard/extract/
  sidecar/serialize paths), `McpTools_Library.cpp` (add_library enum),
  `McpTools_FxSlot.cpp` (new tool only), `timbre-lib/virus_patch.py`
  (additive CLI mode), new `dx7_patch.py` + tests. No DSP, no Transport, no
  ValueTree model changes.
- **Upstream callers of modified functions:** `scanLibrary` → `scanDirectory`
  (also called by `scanAll`); `addLibrary` (called by MCP `add_library`);
  `serializeEntry`/`deserializeEntry` (persistence only). `applyTimbreSidecar`
  is a pattern to mirror, not modify.
- **Downstream:** `search` (typeFilter already generic — filters on
  `lib.type` at `FileLibraryManager.cpp:949`, no change needed); MCP
  `list_libraries`/`get_entry` (read `LibraryInfo.type` and `LibraryEntry`
  generically — verify no hardcoded `audio`/`midi` assumption).
- **Reference patterns:** `applyTimbreSidecar` (`:763`), the audio-type
  wildcard + rescan branch (`:506`, `:553-561`), `serializeEntry` (`:402`),
  `sweep_dx7_patches.py` probe-phrase builder (`build_probe_notes`) and
  `analyze_probe.py` role-check JSON.
- **SPSC / projections:** none new — patch params are slot ValueTree props,
  written through existing commands. FileLibraryManager is message-thread-only.
- **Known engine caveat (document, don't block on):** `sweep_dx7_patches.py`
  notes offline export renders an FM tone invariant to imported patch bytes
  (dated 2026-09-04). The sub_synth tree-copy path restores `param_N`, so
  `audition_patch`'s live audition is expected to work; the export/audition
  path for FM is a separate pre-existing engine issue outside this plan.

---

## Pitfall Gates Triggered

- **Gate 2 (unimplemented path):** `audition_patch` must produce an audible
  result — Gate 6 asserts the live slot values reflect the patch after the
  probe track is built. A tool that only writes props is not complete.
- **Gate 9 (trust boundary):** sidecar JSON parsed defensively (malformed →
  fields empty, never a scan crash — mirror `applyTimbreSidecar`'s try/catch
  at `:766-774`); `--role` validated against `role_targets.SUPPORTED_ROLES`;
  `add_library(type="patch")` path must exist and be a directory.
- **Gate 15 (verify the artifact):** gates run actual pytest/gtest output and
  the real MCP round-trip, not code reading.
- **Gate 8 (no raw hex / no new build config):** no CSS; no new `.cpp` so no
  CMake engine edit; the new test file must be registered in `tests/CMakeLists.txt`.

## Anti-Patterns

- Do NOT touch the audio/midi library paths (`extractAudioMetadata`,
  `extractMidiMetadata`, `applyTimbreSidecar`) — the `patch` path is a
  separate `if (type == "patch")` branch mirroring the audio pattern.
- Do NOT write sidecars into the engine — the Python tools own sidecar
  writing; FileLibraryManager only reads them.
- Do NOT embed role thresholds in prompts/CLI — reuse `role_targets.py`
  `check_role` (thresholds live there, never duplicated).

---

## Design

### Sidecar schemas (Python writers)

`<patch>.virus.json` (name from filename; one sidecar per patch file):
```json
{
  "schema": "hdaw.virus.patch.v1",
  "name": "~WELCOME", "engine": "sub_synth",
  "format": "bcsingle", "bank": 1, "program": 0,
  "roleCheck": {"role":"bass","verdict":"pass","passed_count":3,"total_count":3,"checks":[...],"summary":"3/3 checks passed"},
  "mappedParams": {"0":{"param":"osc1_wave","value":1.0,"raw":0}, ...},
  "unmapped": ["osc2_fm_amount","ring_mod","lfo1","lfo2","keytrack","filter_slope_24db","osc_sync","fx_chorus","fx_delay","fx_reverb","mod_matrix","noise_level"],
  "description": "<timbre.summarize over mapped params>"
}
```
`<patch>.dx7.json`:
```json
{
  "schema": "hdaw.dx7.patch.v1",
  "name": "...", "engine": "fm_synth",
  "format": "single|cartridge", "algorithm": 5, "feedback": 3,
  "params": {...fm_synth param mapping from the 156-byte VCED...},
  "description": "DX7 algorithm 5, feedback 3"
}
```
- Batch modes: `--sidecars <dir>` walks the dir, writes one sidecar per
  patch-containing file (Virus: all 5 formats via existing parsers; DX7:
  single + cartridge via a `dx7_patch.py` that parses the documented DX7
  sysex — the 267B single and 4104B cartridge, mirroring
  `Dx7SysexImport.cpp` semantics in Python).
- Stable JSON: `json.dumps(sort_keys=True)`; re-run is byte-identical.

### FileLibraryManager `patch` type

- `addLibrary(name, path, "patch")`: accepted alongside `midi`/`audio`
  (validate at `addLibrary`; the MCP enum gains `"patch"`).
- `scanDirectory`: wildcard `"*.syx;*.mid;*.midi;*.vhc"` for `type == "patch"`;
  `extractPatchMetadata(file)` returns a `LibraryEntry` with `format` set and
  patch fields empty (filled from sidecar).
- `applyPatchSidecar(LibraryEntry&, const juce::File&)`: reads
  `<file>.virus.json` OR `<file>.dx7.json` (pick by which exists / by
  `engine` key), populates `tags` (from unmapped + roleCheck summary),
  `description` (sidecar description), and patch fields
  (`patchEngine`, `patchParams` JSON string, `roleVerdict`, `unmapped`).
- Rescan detection: `entryHasPatchData` (mirror `entryHasTimbreData` `:479`)
  + sidecar-newer-than-patch check in the audio-style branch (`:553-561`,
  generalized to `type == "patch"`).
- Serialization: add the patch fields to `serializeEntry`/`deserializeEntry`
  (bump entry `schemaVersion` only if needed for forward-compat; prefer
  additive keys that old readers ignore).
- `LibraryEntry` new fields: `juce::String patchEngine;`
  `juce::String patchParams;` (compact JSON) `juce::String roleVerdict;`
  `juce::String unmapped;`.

### MCP wiring

- `McpTools_Library.cpp`: `add_library` enum `{"midi","audio","patch"}`.
  Verify `list_libraries`/`get_entry`/`search_library` read
  `LibraryInfo.type`/`LibraryEntry` generically (they do — no hardcoded
  audio/midi; confirm at `:936-949` and the serializers).
- `McpTools_FxSlot.cpp`: new `audition_patch`:
  `{path, engine: "sub_synth"|"fm_synth", role?, root?, trackId?}` →
  1. validate path exists + engine hint matches the sidecar (or sniff the
     file with `virus_patch.detect_format` semantics / DX7 header)
  2. create a probe track (or reuse `trackId`) with a synth slot of that type
  3. load via `loadVirusPatch` or `setFmPatch` (route by engine)
  4. place a role phrase MIDI clip (root from role map, reusing
     `sweep_dx7_patches.py` `build_probe_notes` translated to the clip model)
  5. return `{ok, trackId, slotIndex, name, engine, role}`.
  Live audition = user presses play on the probe track; no offline render
  dependency.

### Tests

- `test_patch_sidecars.py`: virus writer schema/stable-JSON per format;
  dx7 single+cartridge; error paths; role validation.
- `file_library_patch_test.cpp`:
  - `AddPatchLibraryAndScan` — add type=patch, scan a fixture dir, entries exist
  - `PatchSidecarIngestedIntoSearches` — write a `.virus.json` sidecar next to a
    fixture, rescan, `search("...")` by a tag/description returns it
  - `PatchEntrySerializeRoundTrip` — entry with patch fields survives save/load
  - `MalformedPatchSidecarTolerated` — garbage JSON → entry stays, no crash
  - `PatchLibraryDoesNotBreakAudioLibrary` — both types scan together
  - `AuditionPatchLoadsIntoProbeTrack` — `audition_patch` builds the slot,
    loads, live slot params reflect the patch

---

## Steps

1. Add `--sidecars` mode to `timbre-lib/virus_patch.py` (writer + CLI) and
   create `timbre-lib/dx7_patch.py`; write `test_patch_sidecars.py` first.
2. Add `LibraryEntry` patch fields + `applyPatchSidecar` +
   `extractPatchMetadata` + patch-type branches in `FileLibraryManager.{h,cpp}`;
   wire serialize/deserialize + rescan detection.
3. Update `McpTools_Library.cpp` `add_library` enum; verify generic read paths.
4. Add `audition_patch` to `McpTools_FxSlot.cpp`.
5. Write `file_library_patch_test.cpp`; register in `tests/CMakeLists.txt`.
6. Build + run gates (2,4,5,6,7).
7. Run the real `--sidecars` over `D:\pdf\Virus Presets` + a DX7 folder;
   verify stable JSON.
8. Document in `timbre-lib/README.md` (+ optionally the composition guide).
9. Commit.

---

## Verification Commands

- `python -m pytest timbre-lib/test_patch_sidecars.py -q`
- `python timbre-lib/virus_patch.py --sidecars "D:\pdf\Virus Presets" --role bass`
- `python timbre-lib/dx7_patch.py --sidecars <dx7-fixture-dir>`
- `build\hdaw_tests.exe --gtest_filter=FileLibraryPatch*`
- `build\hdaw_tests.exe --gtest_filter=FileLibrary*:SubtractiveSynth*:TrackFxRebuildRace.SubSynth*:FxChainPreset.*`
- `python -m py_compile timbre-lib/virus_patch.py timbre-lib/dx7_patch.py timbre-lib/test_patch_sidecars.py`
- `ruff check timbre-lib/virus_patch.py timbre-lib/dx7_patch.py timbre-lib/test_patch_sidecars.py` (if installed)
- `git status --short` (diff confined to the planned files)