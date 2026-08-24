# Handoff: Voltage DnB bugs + streamlining shipped (2026-08-23)

## Context

Continued from `2026-08-23-voltage-dnb-mcp-composition.md`. Fixed all 6 bugs
surfaced during the Voltage DnB MCP composition session, implemented 4 of 7
streamlining suggestions, and fixed 3 pre-existing test failures. Pure engine +
MCP work — no frontend changes.

## What shipped

### Bugs fixed (6/6)

| # | Bug | Root Cause | Fix | Files |
|---|-----|------------|-----|-------|
| 1 | MCP `get_clip` velocity=0 | Float (0.0-1.0) cast to int without scaling | `* 127.0 + 0.5` | `McpTools_Project.cpp:150` |
| 2 | `targetTrackIds` ignores hats | `"Closed Hat"` vs `"ClosedHat"` string mismatch | Normalized lookup | `AudioEngineCommands_Clips.cpp:743` |
| 3 | Arpeggiator silence on sustained notes | `floor` in `kEnd` skipped steps at small block sizes | `floor` → `ceil` | `MidiFx.h:117` |
| 4 | `addAutomationLane` silent no-op | Returned `void` on collision | Returns `bool` | `AudioEngineCommands_Automation.cpp:9` |
| 5 | `audition_plugin` rejects internal FX | `isPlugin()` excluded fm_synth/sampler | Removed gate; null-routingManager fallback | `AudioEngineCommands_Composition.cpp:900` |
| 6 | `generate_arrangement` omits clip IDs | MCP response only had counts | Returns `parts` array | `McpTools_Project.cpp:1344` |

### Pre-existing test failures fixed (3/3)

| # | Failure | Root Cause | Fix |
|---|---------|------------|-----|
| 7 | `AutoGainGlobalScale` | Single fm_synth track too quiet to clip mix (peak 0.28) | Stack 4 tracks to push mix peak past 1.0 |
| 8 | `VerifyPartTool` heap corruption | Test ordering — AutoGainGlobalScale corrupted state before it | Fixed by #7 |
| 9 | `Audition.*` tests (3 tests) | `routingManager` null in tests (no audio device → `prepareToPlay` never called) | ValueTree fallback when `routingManager` is null |

### Bonus velocity fixes

- MCP `add_note` stored velocity as raw int instead of normalized float — fixed
- MCP `set_note` same issue — fixed
- MCP `set_note_velocities` relative mode read velocity as int — fixed

### Streamlining implemented (4/7)

| # | Feature | Change | Files |
|---|---------|--------|-------|
| S1 | Bulk `add_notes` MCP tool | New tool accepting array of notes, one undo unit | `McpTools_Project.cpp` |
| S3 | `paramID` in `list_fx_params` | Added `paramID` field (formula: `100 + slotIndex*100 + paramIndex`) | `McpTools_Audio.cpp` |
| S4 | Arrangement `velocityMin`/`velocityMax` | New params with linear remapping preserving relative dynamics | `ArrangementGenerator.h/cpp`, `McpTools_Project.cpp` |
| S5 | Rhythm `bars > 16` | Removed `"maximum",16` schema cap | `McpTools_Project.cpp` |

### Tests added

- `ArrangementIntegration.GeneratedNotesHaveVelocity` — BUG 1
- `ArrangementIntegration.TargetTrackIdsHonoredForAllRoles` — BUG 2
- `Arpeggiator.SustainedNotesAcrossBlocks` — BUG 3
- `Automation.AddLaneParamIdCollisionReturnsError` — BUG 4
- `Audition.InternalFmSynthSlotAudible` — BUG 5
- `Audition.InternalSamplerSlotAudible` — BUG 5
- `McpCoverageTest.AddNotesBulk` — S1
- `McpCoverageTest.GenerateRhythmPatternLongBars` — S5
- `ArrangementIntegration.VelocityRangeApplied` — S4
- `McpCoverageTest.GenerateArrangement` assertions expanded — BUG 6

### Test results

120 passed, 2 skipped (real plugin tests), 2 pre-existing failures (see below).

## Remaining items

### Pre-existing test failures (not caused by our changes)

- `McpServer.FxAddRemoveBypass` — `proc->getTrack()` returns null when `routingManager` is null (same root cause as #9, but these tests use the live processor directly rather than going through `auditionPlugin`)
- `McpCoverageTest.MidiFxAddListBypassSetRemove` — same root cause

These tests need the same `routingManager` null-guard pattern applied to their
code paths, or need to be restructured to not depend on the live processor in
test environments without an audio device.

### Streamlining suggestions not yet implemented

**S2 — Analyze → pattern-library bridge.** `analyze_midi_file` returns
bar-aligned patterns/motifs with notes, and `import_pattern`/`save_pattern`/
`list_patterns` exist — but there's no bridge. A tool (or extra field in the
analysis result) that saves an analyzed motif as a pattern preset would make
"import style from MIDI" a first-class workflow. Needs design work: should the
bridge be a new MCP tool (`save_analyzed_pattern`), or should `analyze_midi_file`
return pattern-ready data that `save_pattern` can consume directly?

**S6 — Slice-mode percussion cookbook.** One sampler track with
`detect_sampler_slices` + slice mode could host a whole Lekebusch kit (pitch →
slice mapping). Worth a cookbook example once slice auditioning
(`trigger_sampler_slice`) is proven. Documentation only.

**S7 — MCP composition cookbook.** Capture the verified pipeline as a doc
(`docs/`): analyze MIDI → set key/tempo/scale → tracks+FX → arranger skeleton
(with velocity fix) → euclidean layering → FM patches via sysex → MIDI FX →
envelope automation → per-section `verify_part` (seek first!) → export +
silence check. Include the pitfalls above; doubles as a regression checklist.
Documentation only.

## Session facts

- No frontend files were modified — pure engine + MCP work.
- Build: `cmake --build build --config Debug` (VS 2026, Qt 6.11.2 at `C:\Qt\6.11.2\msvc2022_64`)
- CMake configure: `cmake -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.11.2\msvc2022_64" -S . -B build`
- The `routingManager` null issue (items #9, pre-existing failures) affects any
  test that calls `proc->getTrack()` or `proc->getFXChain()` — the engine
  initializes but `prepareToPlay` never runs (no audio device), so
  `routingManager` stays null. The `auditionPlugin` function now has a
  ValueTree fallback; other code paths may need the same treatment.
- The `McpServer.FxAddRemoveBypass` and `McpCoverageTest.MidiFxAddListBypassSetRemove`
  failures are the same `routingManager` null issue in different code paths.
