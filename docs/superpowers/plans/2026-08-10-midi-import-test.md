# Task 8: Write gtest for MIDI File Import

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create gtest suite `MidiImportTest` with 3 tests covering the `HDAW::importMidiFile` function.

**Architecture:** The test file writes a minimal valid MIDI binary to a temp file, calls `importMidiFile`, and asserts on the resulting ValueTree (clip + notes). Follows the existing test patterns in `tests/unit/engine/`.

**Tech Stack:** C++, gtest, JUCE MidiFile, Windows temp file API.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `tests/unit/engine/midi_import_test.cpp` | Create | 3 gtests for MIDI import |
| `tests/CMakeLists.txt:52` | Modify | Add test to `hdaw_tests` source list |

---

## Dependency Map

- **Upstream:** `HDAW::importMidiFile` (src/engine/MidiImport.h) — already implemented
- **Downstream:** None (test-only change)
- **Projections affected:** None (test reads ValueTree directly, not live processor)
- **SPSC paths touched:** None
- **Pitfall gates triggered:** Gate 4 (verify binary after build)

---

## Steps

### Step 1: Create the test file

Create `tests/unit/engine/midi_import_test.cpp` with:
- `writeTestMidiFile()` helper: writes minimal MIDI binary (format 0, 1 track, 1 note, 480 ticks/quarter, 120 BPM) to a temp file via `GetTempFileNameA`
- `ImportIntoExistingTrackCreatesClipWithNotes`: adds a MIDI track, imports, asserts 1 clip with notes
- `ImportIntoNewTracksCreatesTracksAndClips`: imports with trackIdx=-1, asserts new track + clip created
- `ImportNonexistentFileReturnsEmpty`: imports nonexistent path, asserts empty result

### Step 2: Add test to CMakeLists.txt

Add `unit/engine/midi_import_test.cpp` after `unit/engine/merge_clips_test.cpp` (line 52).

### Step 3: Build and verify

Run: `cmake --build build --config Debug --target hdaw_tests`
Then: `build\Debug\hdaw_tests.exe --gtest_filter=MidiImportTest.*`
Expected: All 3 tests pass.

---

## Success Gates

- [ ] `cmake --build build --config Debug --target hdaw_tests` succeeds
- [ ] `build\Debug\hdaw_tests.exe --gtest_filter=MidiImportTest.*` — all 3 tests pass
- [ ] No existing tests regress
