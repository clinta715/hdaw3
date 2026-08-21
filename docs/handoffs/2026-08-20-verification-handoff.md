# Handoff — Composition tooling verification & commit (2026-08-20)

## Purpose

This session verified, fixed, and committed the composition tooling work from
the previous session (see `2026-08-20-composition-tooling-handoff.md`).

## What was done

1. **Build verified** — `cmake --build build --config Debug` succeeded with no
   errors (VS 18 2026 generator, Qt 6.11.2).
2. **Tests run** — full suite: **962 passed, 8 skipped, 1 pre-existing failure**.
3. **Bug found and fixed** — Percussion style `lowNote`/`highNote` range filtered
   out all GM drum pitches (36=kick, 38=snare, 42=hat were below default
   `lowNote=48`). Fixed by using `effLow=min(lowNote,36)` /
   `effHigh=max(highNote,42)` in the Percussion case.
4. **Committed** — `a06248b` — 16 files, +636/−95 lines. Includes all 19
   composition tooling items + CMakeLists.txt build optimizations (sccache, PCH,
   FetchContent settings).

## Pre-existing test failure (not from these changes)

| Test | Suite | File | Notes |
|------|-------|------|-------|
| `PluginIsolation.LiveDropDrainsStaleOutput` | PluginIsolation | `isolation_integration_test.cpp:842` | Expects `b3.getSample() == 0.3f` but gets 0. Drop-path audio passthrough not working in isolation mode. Unrelated to composition tooling. |

## Build optimization notes

### What shipped in this commit

- `FETCHCONTENT_UPDATES_DISCONNECTED ON` — skips network checks on configure
- `find_program(sccache)` + `CMAKE_C/CXX_COMPILER_LAUNCHER` — caches compilation
- `target_precompile_headers` — PCH for STL headers (string, vector, memory, etc.)

### Ninja + sccache + MSVC PDB issue (not resolved)

Ninja generator with sccache fails on parallel C compilation of aubio — multiple
`cl.exe` processes fight over a shared `aubio.pdb` even with `/FS`. The issue is
sccache intercepting the PDB write path. Workarounds:

1. **Use VS generator** (current approach) — MSBuild handles PDB serialization
   internally. Slower but works.
2. **Disable sccache for C only** — set `CMAKE_CXX_COMPILER_LAUNCHER=sccache`
   but not `CMAKE_C_COMPILER_LAUNCHER`. Ninja parallelism still helps CXX.
3. **Set `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`** — uses `/Zi` → `/Z7`
   (embedded PDB), avoids shared PDB entirely. Requires CMake 3.25+.

The VS generator build takes ~15 min for a clean build. Ninja + sccache (CXX
only) could cut this to ~5 min but needs the PDB issue resolved first.

## Skipped tests (require real plugins / special environment)

- `InstrumentPart.ProgramIndexSetsLiveProgram` — needs TyrellN6
- `Audition.RealPluginAudibleProgram` / `RealPluginReportsProgramNames`
- `ClapPresetProbe.ScanInstalledClaps` / `ClapProgram.*` — needs CLAP plugins

## Untracked files (not committed)

- `berlin_dub_project.hdw` — sample project file
- `projects/` — project directory

## All 19 items — final status

| # | Item | Status |
|---|------|--------|
| 1 | velocity=0 in generated phrases | ✅ Fixed |
| 2 | generate_phrase returns 0 notes | ✅ Fixed |
| 3 | Rhythm bars parameter docs | ✅ Documented |
| 4 | Euclidean timing alignment docs | ✅ Documented |
| 5 | Track ID shifting warning | ✅ Added |
| 6 | remove_track silently deletes clips | ✅ Fixed (force param) |
| 7 | Export progress info | ✅ Enhanced |
| 8 | Batch velocity setter | ✅ New tool |
| 9 | Arrangement target tracks | ✅ New feature |
| 10 | Percussion style | ✅ New style (+ range fix) |
| 11 | Euclidean multi-voice | ✅ New feature |
| 13 | Clip duration in beats | ✅ Already implemented |
| 14 | Clip loop/extend | ✅ New tool |
| 15 | Synth preset setting | ✅ Already implemented |
| 16 | Preset search | ✅ New tool |
| 17 | MIDI FX param discovery | ✅ New tool |
| 18 | Track FX param listing | ✅ Already implemented |
| 19 | Undo support for bulk ops | ✅ Already implemented |

**All 19 items complete. No remaining work from the original list.**
