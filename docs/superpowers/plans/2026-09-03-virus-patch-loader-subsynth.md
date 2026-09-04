# Virus Patch Loader (sub_synth) — C++ Sysex Import + MCP Tool

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking. **MANDATORY:** invoke the
> `hdaw-guard` skill before any code change.

**Goal:** Add a C++ `VirusSysexImport` that parses Access Virus sysex
containers (B/C single dump + TI bank) into the existing `sub_synth` internal
FX params, plus an MCP tool `sub_synth_import_sysex` and an undo-able command
`loadVirusPatch`, so a rendered Virus patch can be dropped into a `sub_synth`
slot and survive save/load/rebuild.

**Slice 2 of the Virus-adaptation work.** Slice 1 (committed `6dfb58f`) proved
the byte layouts and the mapping contract in Python (`timbre-lib/virus_patch.py`,
5,040 patches parsed, ~23/24 params mapped). This slice ports the loader to C++
mirroring the proven `Dx7SysexImport` precedent (`src/engine/Dx7SysexImport.{h,cpp}`
+ `McpTools_FmSynth.cpp` `fm_synth_import_sysex`). It does NOT add DSP — FM,
ring mod, LFOs, keytrack remain unmapped and are reported back to the caller.

**Tech Stack:** C++17 + JUCE 8, gtest, Qt JSON MCP, existing internal-FX
ValueTree path.

---

## File Structure

### Create
| File | Responsibility |
|------|---------------|
| `src/engine/VirusSysexImport.h` | `VirusPatch` struct, `parseBcSingle`, `parseTiBank`, `mapToSubSynth` (real-unit param values) |
| `src/engine/VirusSysexImport.cpp` | Parsers (header/checksum validated), Virus byte → sub_synth real-unit converters |
| `tests/unit/engine/virus_sysex_import_test.cpp` | Parser + mapping tests (real fixtures from `timbre-lib/testdata/virus/`) |

### Modify
| File | Change |
|------|--------|
| `src/engine/AudioEngineCommands.h` | Declare `loadVirusPatch(int trackIndex, int slotIndex, const std::string& filePath, int voiceIndex)` |
| `src/engine/AudioEngineCommands_Fx.cpp` | Implement `loadVirusPatch` — parse → map → `setFxSlotParam` per mapped param, one undo unit |
| `src/mcp/McpTools_FxSlot.cpp` | Register `sub_synth_import_sysex` tool (trackId, slotIndex, filePath, voiceIndex) |
| `tests/unit/engine/track_fx_rebuild_race_test.cpp` | SubSynth patch-load → rebuild → live values assert |
| `tests/CMakeLists.txt` | Register the new test file |
| `CMakeLists.txt` | Add `src/engine/VirusSysexImport.cpp` |

No frontend changes — the FX panel already renders `sub_synth` params via
`list_fx_params` (the legato plan's precedent: no component work needed).

---

## Success Gates

- [x] Gate 1: `cmake --build build --config Debug` succeeds. — exit 0 (app build; the only blocker was a backrest/restic backup lock on `HDAW.pdb`, released and rebuilt).
- [x] `build/hdaw_tests.exe --gtest_filter=VirusSysexImport*` passes. — 7/7 PASSED (parse B/C fixture, TI slice, reject bad size/header/checksum, Python↔C++ mapping match, out-of-range guard).
- [x] `build/hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynth*` passes
      (patch-loaded values survive `rebuildFXChain()` — asserts LIVE processor,
      not ReadModel only). — 3/3 PASSED (incl. `SubSynthPatchLoadSurvivesRebuild` + `SubSynthPatchLoadPersistsAcrossSaveLoad`).
- [x] Gate 4: `build/hdaw_tests.exe --gtest_filter=McpTools*SubSynth*` (or the
      existing MCP FX-slot suite) passes. — `*SubSynth*` 3/3; `FxChainPreset.*:FmPatchPersistence.*:Dx7SysexImport*` 25/25 (confirms no collision with the parallel FM work).
- [x] Gate 5: C++ mapped values match the Python decoder for the same fixture:
      a test asserts `mapToSubSynth(parseBcSingle(fixture))` equals the values
      `timbre-lib/virus_patch.py --dump` produces (within real-unit rounding). —
      verified: same raw bytes (`raw 27` cutoff → 86.86 Hz, `raw 59` amp-release → 0.0523 s, `raw 83` filter-env → 14.25 semis) in both; C++ emits real units, Python normalized.
- [x] Gate 6: Round-trip via the MCP tool: load a fixture into a `sub_synth`
      slot, save project, load project → params persist. —
      `SubSynthPatchLoadPersistsAcrossSaveLoad` PASSED.
- [x] No new anti-patterns: params written only via the existing command path
      (`setFxSlotParam` holds `stateLock`, writes `param_N`); no raw DSP writes
      from listeners; no allocation in the audio thread (parsing is
      message-thread only).

Note: the test binary requires a reconfigure to pick up new test files (the
stale-graph reverse: `build.ninja` was newer than the `tests/CMakeLists.txt`
edit, so ninja skipped the CMake re-run — `cmake -S . -B build` forced it).
Two test-helper fixes were needed on pickup: JUCE 8 has no `File::isAbsolute()`
(use static `File::isAbsolutePath`) and gtest `ASSERT_*` can't be used in a
value-returning helper (switched to `std::runtime_error` throws).

---

## Dependency Map

- **Blast radius:** `AudioEngineCommands_Fx.cpp` (add one command), `McpTools_FxSlot.cpp` (add one tool), new `VirusSysexImport` module, two test files, CMake lists. No changes to `TrackFXSlot.h`/`.cpp`, the engine, ReadModel, or frontend.
- **Upstream:** MCP tool → `loadVirusPatch` → existing `setFxSlotParam` → existing `param_N` write + `Track::stateLock` + live `setInternalParam`. This is the *already-proven* DX7-import command shape, minus a dedicated blob.
- **Downstream:** `sub_synth` slot params are persisted as `param_N` props and restored by the existing `TrackFXSlot::prepare()` SubSynth branch (params 0–23 already handled — verified `:774-791`). No new restore path needed.
- **Reference:** `Dx7SysexImport` (parser shape, checksum, name extraction), `McpTools_FmSynth.cpp:61-143` (`fm_synth_import_sysex` tool shape), `AudioEngineCommands_Fx.cpp:295` (`setFxSlotParam`), `AudioEngineCommands_Fx.cpp:324` (`setFmPatch` tree-first pattern).
- **Projections:** ValueTree only (params). ReadModel/frontend read the same `param_N`. No SPSC crossing beyond what `setFxSlotParam` already does.

---

## Pitfall Gates Triggered

- **Gate 2 (unimplemented path):** the tool must produce an audible, persisted
  result — not just set props. Tests assert live processor values after load
  AND after rebuild/save-load (Gate 6).
- **Gate 9 (trust boundary):** file existence + size + header + checksum all
  validated before any param write; bad input returns a clear MCP error and
  leaves the slot unchanged (mirror `fm_synth_import_sysex` error paths).
- **Gate 10/1/6 (rebuild restore):** params 0–23 already restore in the
  existing SubSynth prepare branch; the new test must set params via the
  patch-load command, rebuild, and assert the live slot (not ReadModel only).
- **Gate 13 (DSP-state writes):** the loader calls `setFxSlotParam` (existing
  command, holds `stateLock`, writes `param_N`, routes through
  `setInternalParam`). It never touches the engine's DSP objects directly.
- **Gate 15 (verify the artifact):** gates run the freshly built binary.

---

## Design

### `VirusSysexImport.h`

```cpp
namespace HDAW {

struct VirusPatch {
    std::string name;                 // trimmed ASCII (may be empty)
    bool isValid = false;
    int bank = 0, program = 0;        // when available
    // Real-unit sub_synth param values (indices 0..23), matching
    // TrackFXSlot::getParamDefsForType("sub_synth") ranges. Only indices
    // present in `mapped` are written.
    std::array<std::optional<float>, 24> mapped;
    std::vector<std::string> unmapped; // features with no sub_synth equivalent
};

std::optional<VirusPatch> parseBcSingle(const uint8_t* data, size_t size);
std::vector<VirusPatch>     parseTiBank(const uint8_t* data, size_t size);
VirusPatch                  mapToSubSynth(const uint8_t* page, size_t len,
                                          const std::string& name);

} // namespace HDAW
```

### Byte layouts (proven in slice 1, re-validate against fixtures)

- **B/C single** (267 B): `F0 00 20 33 01 <dev> 10 <bank> <prog>` (9-byte header)
  + 256-byte page A+B payload + checksum byte at 265 + `F7` at 266. Checksum =
  `(dev + 0x10 + bank + prog + sum(payload)) & 0x7F`. Name at payload[240:250].
- **TI bank** (67072 B): 128 × 524 B self-contained sysex blocks. Block =
  9-byte header + 512-byte payload + checksum at 522 + `F7` at 523. Name at
  payload[240:250].
- **`mapToSubSynth`** reads the same `PARAM_OFFSETS` the Python module uses
  (page-A offsets documented in `virus_patch.py`) and converts Virus 0–127
  bytes into **real-unit** sub_synth values matching the param defs in
  `TrackFXSlot.h` (cutoff → Hz 20..20000, envs → seconds 0.001..5, resonance →
  0..0.99, wave → 0..3 int, detune → ±1200 cents, filter type → 0..3 int).
  Conversions mirror `virus_patch.py`'s `_CONVERTERS` scaled to real units.

### Command `loadVirusPatch` (AudioEngineCommands)

1. Validate slot is `sub_synth` (via ReadModel, like the FM tool).
2. Read file → detect 267B single / 67072B bank.
3. Parse; pick voice (default 0 for banks). On parse failure return error, slot unchanged.
4. For each `mapped[i]` present: `setFxSlotParam(trackIndex, slotIndex, i, value)` — one undo unit (wrap in begin/end transaction if the command layer needs it).
5. Return `{ok, name, bank, program, mappedCount, unmapped:[...]}`.

### MCP tool `sub_synth_import_sysex`

Schema `{trackId:int, slotIndex:int, filePath:string, voiceIndex:int?}`. Mirrors
`fm_synth_import_sysex` (`McpTools_FmSynth.cpp:61`): slot-type check → file read →
parse → command → JSON result.

---

## Steps

1. Copy the smallest fixtures into `tests/unit/engine/testdata/virus/` (or read
   from `timbre-lib/testdata/virus/` at test time): the 267B single + a 524B
   TI block sliced from a bank. Keep total testdata small.
2. Write `virus_sysex_import_test.cpp` (failing): parse fixture → expected
   name + checksum; bad size/header/checksum → nullopt; `mapToSubSynth` values
   match the Python decoder's `--dump` for the same fixture (hard-code expected
   real-unit values from a one-time Python run).
3. Implement `VirusSysexImport.{h,cpp}`; iterate until parser tests pass.
4. Add `loadVirusPatch` command + the rebuild test (load → rebuild → assert
   live slot values). Add the MCP tool.
5. Build + run the four gate suites.
6. Verify gates 5 (Python↔C++ match) and 6 (save/load round-trip).
7. Commit.

---

## Verification Commands

- `cmake --build build --config Debug`
- `build\hdaw_tests.exe --gtest_filter=VirusSysexImport*`
- `build\hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynth*`
- `build\hdaw_tests.exe --gtest_filter=*SubSynth*` (broad MCP/slot sweep)
- Confirm `build/hdaw_tests.exe` mtime postdates the build (Gate 15).