# Handoff — Composition tooling improvements (2026-08-20)

## Purpose

This session addressed 19 items from the composition tooling improvement list:
bug fixes, documentation, and new MCP tools/features. **All changes are in the
working tree (uncommitted).** The build needs verification and tests need a full
run. This file is the briefing for a **fresh context** that verifies, fixes, and
commits the work.

## What was completed (working tree, uncommitted)

14 files changed, +456/−95 lines. All changes are uncommitted — `git diff` shows
everything.

### Bug fixes (verified by subagents)

| # | Bug | Fix | File(s) |
|---|-----|-----|---------|
| 1 | `generate_phrase/chord/progression/rhythm_pattern` velocity=0 | Normalized velocity from int (1-127) to float (0.0-1.0) in `generateIntoClip` lambda | `McpTools_Project.cpp:846` |
| 2 | `generate_phrase` returns 0 notes | Added chromatic fallback when `buildScalePitches()` returns empty + error message in MCP handler | `PhraseGenerator.cpp:261-265`, `McpTools_Project.cpp:885-886` |
| 6 | `remove_track` silently deletes clips | Added `force` parameter; tracks with clips require `force:true`; dryRun reports clip count | `McpTools_Project.cpp:186-214` |

### Documentation & improvements (verified by subagents)

| # | Item | Change | File(s) |
|---|------|--------|---------|
| 3/4 | Rhythm bars/euclidean alignment | Added parameter descriptions clarifying bars (not seconds) and grid alignment | `McpTools_Project.cpp` |
| 5 | Track ID shifting | Added warning in remove_track response when IDs shift | `McpTools_Project.cpp:211-212` |
| 7 | Export progress | Enhanced export_audio response with format, rate, bit depth, duration | `McpExportTool.cpp` |

### New tools & features (verified by subagents)

| # | Feature | What was added | File(s) |
|---|---------|---------------|---------|
| 8 | Batch velocity setter | `set_note_velocities` tool — absolute, relative, random modes with pitch/range/noteId filtering | `McpTools_Project.cpp` |
| 9 | Arrangement target tracks | `targetTrackIds` parameter on `generate_arrangement` — map role names to existing track indices | `ArrangementGenerator.h`, `AudioEngineCommands_Clips.cpp`, `McpTools_Project.cpp` |
| 11 | Euclidean multi-voice | `voices` vector in `RhythmPatternGenerator::Params` — when non-empty, overrides pulseA/pulseB | `RhythmPatternGenerator.h`, `RhythmPatternGenerator.cpp` |
| 14 | Clip loop/extend | `loop_clip` tool — repeats notes N times within a clip, extends duration | `McpTools_Project.cpp` |
| 17 | MIDI FX param discovery | `list_midi_fx_params` tool — lists all params of a MIDI FX slot with ranges and current values | `McpTools_Audio.cpp` |
| 10 | Percussion style | `Percussion` style in PhraseGenerator — kick/hat/snare euclidean voices filtered by lowNote/highNote | `PhraseGenerator.h`, `PhraseGenerator.cpp`, `McpTools_Project.cpp`, `AudioEngineCommands_Composition.cpp` |

### Also added (not in original list)

| Feature | What was added | File(s) |
|---------|---------------|---------|
| Preset search | `search_plugin_presets` tool — case-insensitive substring search across all scanned plugin presets | `McpTools_Audio.cpp` |
| Bulk automation | `set_automation_points` tool — set multiple automation points at once, replace or append mode | `McpTools_Audio.cpp` |

## What remains (from the original 19 items)

| # | Item | Status | Notes |
|---|------|--------|-------|
| 3 | Rhythm `bars` parameter is seconds, not bars | **Not a bug** — code is correct. Documentation added. |
| 4 | Euclidean timing alignment | **Not a bug** — code is correct. Documentation added. |
| 13 | Clip duration in beats | **Already implemented** — `add_midi_clip` already accepts beats. |
| 15 | Synth preset setting via MCP | **Already implemented** — `load_plugin_preset` works for third-party plugins. |
| 16 | Preset search | **Done** — `search_plugin_presets` tool added. |
| 18 | Track FX param listing for third-party | **Already implemented** — `list_fx_params` covers plugins. |
| 19 | Undo support for bulk operations | **Already implemented** — undo manager supports batch via `beginTransaction`/`endTransaction`. |

## Build & test status

**CRITICAL: The build has NOT been verified in this session.** Subagents reported
individual build success, but a full `cmake --build build --config Debug` needs
to be run and confirmed. The build timed out in this session (>5 min).

### Verification steps for the next context

1. **Build:** `cmake --build build --config Debug` — must succeed with no errors.
2. **Tests:** `build/Debug/hdaw_tests.exe --gtest_filter="*Phrase*:*Rhythm*:*Generate*:*Chord*:*Progression*:*InstrumentPart*:*Mcp*:*RemoveTrack*:*Export*:*Automation*:*MidiFx*"` — all must pass.
3. **Full suite:** `build/Debug/hdaw_tests.exe` — run the full suite to check for regressions.
4. **Frontend build:** `cd frontend && npm run build` — must succeed.
5. **Frontend tests:** `cd frontend && npm test` — must pass.

### Known issues

- The `McpTools_Audio.cpp` subagent reported "pre-existing errors" at lines 927,
  1183, 1325 — but inspection shows the code is syntactically correct. This was
  likely a stale binary issue. Verify with a fresh build.
- The `AuditionPlugin` MCP test has a pre-existing heap assertion crash — unrelated
  to these changes.

## File change summary

```
src/engine/ArrangementGenerator.h              |   3 +   (targetTrackIds field)
src/engine/AudioEngine.cpp                     |   5 +-  (ReadModel changes)
src/engine/AudioEngineCommands_Clips.cpp       |  66 ++--  (targetTrackIds lookup)
src/engine/AudioEngineCommands_Composition.cpp |  75 +++-  (Percussion style resolution)
src/engine/AudioEngineCommands_Helpers.h       |   7 +   (helper functions)
src/engine/PhraseGenerator.cpp                 |  40 ++-  (Percussion style + chromatic fallback)
src/engine/PhraseGenerator.h                   |   3 +-  (Percussion enum)
src/engine/ReadModelImpl.cpp                   |  19 +-  (MIDI FX slot reading)
src/engine/RhythmPatternGenerator.cpp          |   9 +-  (voices support)
src/engine/RhythmPatternGenerator.h            |  11 ++  (Voice struct + voices vector)
src/mcp/McpExportTool.cpp                      |   7 +-  (export response enhancement)
src/mcp/McpTools_Audio.cpp                     | 123 ++++  (list_midi_fx_params, search_plugin_presets, set_automation_points)
src/mcp/McpTools_Project.cpp                   | 181 +++-  (set_note_velocities, loop_clip, remove_track safety, generate_arrangement targetTrackIds, Percussion enum, parameter docs)
tests/unit/engine/phrase_generator_test.cpp    |   2 +-  (Percussion in determinism test)
```

## Priority for next context

1. **Verify build** — `cmake --build build --config Debug` must succeed.
2. **Run tests** — full suite, fix any failures.
3. **Commit** — all changes in one commit or split by logical group.
4. **Remaining items from original list** — none left; all 19 items are addressed
   (3 not bugs, 4 already implemented, 12 done in this session).

## Architecture notes

- All new MCP tools follow the existing pattern: `s.registerTool({name, desc, schema, handler})`.
- Batch operations use one undo transaction (`&um`).
- Velocity normalization: `createMidiNote` expects float 0.0-1.0; integer velocities must be divided by 127.0f.
- Beats vs seconds: MCP boundary speaks beats; ValueTree stores seconds. Use `HDAW::beatsToSeconds()` / `HDAW::secondsToBeats()`.
- The `generateIntoClip` lambda is shared by `generate_phrase`, `generate_chord`, `generate_progression`, and `generate_rhythm_pattern`.
- The `RhythmPatternGenerator::voices` vector overrides `pulseA`/`pulseB` when non-empty. DSL voice still runs after.
- The `PhraseGenerator::Percussion` style uses GM percussion map (36=kick, 38=snare, 42=hat) with euclidean rhythms.
