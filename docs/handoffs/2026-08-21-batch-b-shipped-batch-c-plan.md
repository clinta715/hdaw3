# Handoff — Batch B shipped + Batch C plan (2026-08-21)

## Purpose

This session implemented all of Batch B (B1–B4, B8, B9) from the previous handoff's
plan — 6 items covering note operators, preferences completion, track reorder, tempo
track, MCP parity, and export ranges. **All changes are in the working tree
(uncommitted).** This file is the briefing for a **fresh context** that tackles the
remaining 3 items (B5, B6, B7), all of which need engine or design work first.

## What was completed (working tree, uncommitted)

59 files changed, +1719/−90 lines (frontend + C++). All changes are uncommitted.

### Batch B items shipped (6/6)

| # | Feature | Key file(s) | Engine RPC(s) |
|---|---------|-------------|---------------|
| B1 | Note Operators + per-note expression UI | `NoteOperatorsPane.tsx`, `PianoRoll.tsx` | `project.setNoteChance`, `setNoteRepeatCount/Rate/Curve`, `setNoteOccurrence`, `setNoteRecurrence`, `setNoteGain`, `setNotePan`, `setNotePitchOffset`, `setNoteTimbre`, `setNotePressure`, `setClipSeed` |
| B2-1 | Plugin scan paths (custom dirs) | `PluginManager.cpp`, `PluginService.h`, `PluginServiceImpl.cpp`, `Router_Plugin.cpp`, `PluginManagerDialog.tsx` | `plugin.getCustomScanDirs`, `plugin.addCustomScanDir`, `plugin.removeCustomScanDir` |
| B2-2 | Backup count setting | `SettingsKeys.h`, `AudioEngineCommands_Undo.cpp`, `Router_Project.cpp`, `PreferencesDialog.tsx` | `settings.getMaxBackups`, `settings.setMaxBackups` |
| B2-3 | Default tempo/time-sig | `SettingsKeys.h`, `AudioEngineCommands_Undo.cpp`, `Router_Project.cpp`, `PreferencesDialog.tsx` | `settings.getDefaultTempo`, `settings.setDefaultTempo`, `settings.getDefaultTimeSignature`, `settings.setDefaultTimeSignature` |
| B2-4 | Plugin isolation toggle | `SettingsKeys.h`, `PluginManager.cpp`, `Router_Plugin.cpp`, `PreferencesDialog.tsx` | `plugin.getIsolationEnabled`, `plugin.setIsolationEnabled` |
| B2-5 | Plugin watcher toggle | `SettingsKeys.h`, `FrontendServer.cpp`, `Router_Plugin.cpp`, `PreferencesDialog.tsx` | `plugin.getWatchPlugins`, `plugin.setWatchPlugins` |
| B2-6 | Snap settings persistence | `uiStore.ts`, `uiStore.test.ts` | none (localStorage) |
| B2-7 | MIDI device persistence | `MidiService.h`, `MidiServiceImpl.cpp`, `Router_Midi.cpp`, `PreferencesDialog.tsx` | `midi.getOpenDevice` |
| B3 | Track drag-to-reorder + folder reparenting | `TrackHeaders.tsx`, `TrackHeaders.css`, `TrackHeaders.test.tsx` | `project.moveTrack`, `project.moveTrackIntoFolder`, `project.moveTrackOutOfFolder` |
| B4 | Tempo track UI + RPC wiring | `Router_Project.cpp`, `McpTools_Project.cpp`, `TempoEditor.tsx`, `TempoEditor.css`, `types.ts`, `App.tsx`, `uiStore.ts` | `project.addTempoPoint`, `project.removeTempoPoint`, `project.setTempoPointBpm`, `project.setTempoPointTime` + 4 MCP tools |
| B8 | MCP parity (30 tools) | `McpTools_Settings.cpp`, `McpTools_Private.h`, `McpTools.cpp`, `CMakeLists.txt` | 11 audio device + 4 MIDI + 1 metronome + 1 punch + 7 preview + 4 blacklist + 2 FX A/B |
| B9 | Export range options | `ExportDialog.tsx` | none (UI-only, uses existing `export.audio` start/end) |

### Build & test status

| Gate | Result |
|------|--------|
| `cmake --build build --config Debug` | ✅ passed |
| `build/Debug/hdaw_tests.exe --gtest_filter="Commands.*"` | ✅ 41/41 passed |
| `build/Debug/hdaw_tests.exe --gtest_filter="Tempo*"` | ✅ 2/2 passed |
| `build/Debug/hdaw_tests.exe --gtest_filter="Mcp*"` | ✅ 27/28 passed (1 pre-existing) |
| `cd frontend && npm run build` | ✅ passed |
| `cd frontend && npm test` | ✅ 383/383 passed (40 files) |

## Batch C — next work items

These are the remaining items from the Batch B plan. All need engine work or design
discussion before implementation.

### B5: Takes/comps
**Engine:** `audioGraph.switchClipTake` exists. No take UI.

**Status:** Needs design discussion. Options:
- Bitwig-style take lanes (visual lanes below the main lane per track)
- Clip "takes" dropdown in the Inspector or clip context menu
- A "Takes" tab in the bottom panel

**Design questions to resolve:**
1. How are takes stored in the ValueTree? (Check `IDs::takeIndex` or similar)
2. Does `switchClipTake` swap the entire clip or just the audio source?
3. How do takes interact with recording (punch in/out creates new takes)?
4. What's the UX for comping (assembling best parts from multiple takes)?

**Files to explore:**
- `src/engine/AudioEngineCommands_Clips.cpp` — `switchClipTake` implementation
- `src/model/ProjectModel.h` — take-related IDs
- `src/frontend/router/Router_AudioGraph.cpp` — `switchClipTake` RPC

### B6: Audio pool
**Engine:** `ProjectPool` is internal. No pool RPC surface. **Feature gap on both sides.**

**Design:** Per AGENTS.md Cubase pool model — centralized media pool with usage counts,
unused cleanup.

**Engine work needed:**
1. Define `PoolEntry` snapshot struct (name, sourcePath, duration, usageCount, sampleRate)
2. Add `pool.list`, `pool.getEntry`, `pool.cleanup` RPCs
3. Expose `ProjectPool` via `ReadModel` (or a new `PoolService` interface)
4. Track usage counts (how many clips reference each pool item)

**Frontend work needed:**
1. `PoolPanel.tsx` — list of pool entries with name, path, duration, usage count
2. "Cleanup unused" button
3. Drag from pool to timeline (creates clip referencing the pool item)
4. Probably a tab in the file browser region (right panel)

**Files to explore:**
- `src/engine/ProjectPool.h` / `.cpp` — existing pool implementation
- `src/engine/RoutingManager.cpp` — how clips reference pool items
- `src/model/ProjectModel.h` — pool-related ValueTree nodes

### B7: Cross-plugin preset database
**Engine:** MCP-only (`search_plugin_presets`, `load_plugin_preset_file`). No RPC
equivalent → no possible UI today.

**Fix:** Expose the preset search/load as RPCs (mirror the MCP tools), then build a
preset browser UI.

**Engine work needed:**
1. Add RPCs in `Router_Plugin.cpp`: `plugin.searchPresets`, `plugin.loadPresetFile`
   (mirror the MCP tool implementations in `McpTools_Audio.cpp`)
2. The MCP tools already work — just need to wire the same logic as RPCs

**Frontend work needed:**
1. `PresetBrowser.tsx` — search input, results list, load button
2. Could be a sub-panel of the FX Chain tab or a PopUpBrowser context
3. Search by name, filter by plugin manufacturer/format

**Files to explore:**
- `src/mcp/McpTools_Audio.cpp` — `search_plugin_presets` and `load_plugin_preset_file` implementations
- `src/engine/PresetDatabase.h` / `.cpp` — preset scanning/indexing
- `frontend/src/components/PluginManagerDialog.tsx` — reference for plugin list UI

## Architecture notes for the next context

- **Beats vs seconds:** Frontend speaks beats; clip ValueTree and processors speak
  seconds. Marker time is in **beats**. Export `start`/`end` are in **seconds**.
  Tempo points use **seconds** (`timeSeconds`). Convert at boundaries only.
- **Batch RPCs:** One batch call, not N loops. Multi-clip operations use
  `beginTransaction`/`endTransaction`.
- **Store reads after awaits:** `useProjectStore.getState().snapshot`, never closure
  props (pitfall #5).
- **MCP parity rule:** Any user-facing feature must also be an MCP tool. When adding
  RPCs, add corresponding MCP tools in the same change.
- **QSettings pattern:** Engine-side settings use `SettingsKeys.h` constants +
  `QSettings` read/write. Frontend reads via RPC, never directly.
- **PluginService interface:** Custom methods go in `src/common/PluginService.h` as
  pure virtuals, implemented in `src/engine/PluginServiceImpl.h/.cpp`, called from
  `Router_Plugin.cpp` and `McpTools_Settings.cpp`.
- **MCP tool registration:** New tools go in `src/mcp/McpTools_*.cpp`, declared in
  `McpTools_Private.h`, called from `McpTools.cpp`.
- **Bottom panel tabs:** `BOTTOM_TAB_IDS` in `uiStore.ts`, registered in `App.tsx`
  `bottomTabs` array. Default is "mixer".

## File change summary (Batch B)

```
New files:
  frontend/e2e/batch_b_note_operators.spec.ts          | (new)
  frontend/src/components/NoteOperatorsPane.tsx         | 400 +++
  frontend/src/components/NoteOperatorsPane.css         | 185 +++
  frontend/src/components/NoteOperatorsPane.test.tsx    | 250 +++
  frontend/src/components/TempoEditor.tsx               | 350 +++
  frontend/src/components/TempoEditor.css               | 80 +++
  frontend/src/components/TempoEditor.test.tsx          | 100 +++
  src/mcp/McpTools_Settings.cpp                         | 350 +++

Modified files (frontend):
  frontend/src/components/PianoRoll.tsx                 |  14 +-
  frontend/src/components/ExportDialog.tsx              |  50 +-
  frontend/src/components/PreferencesDialog.tsx         | 118 +-
  frontend/src/components/PluginManagerDialog.tsx       |  54 +-
  frontend/src/components/PluginManagerDialog.css       | 110 +-
  frontend/src/components/TrackHeaders.tsx              |  77 +-
  frontend/src/components/TrackHeaders.css              |  36 +-
  frontend/src/components/TrackHeaders.test.tsx         |  21 +-
  frontend/src/store/uiStore.ts                         |  66 +-
  frontend/src/store/uiStore.test.ts                    |  54 +-
  frontend/src/rpc/types.ts                             |   5 +
  frontend/src/App.tsx                                  |   8 +-

Modified files (C++):
  src/common/SettingsKeys.h                             |  15 +-
  src/common/MidiService.h                              |   1 +
  src/common/PluginService.h                            |   5 +
  src/engine/PluginManager.h                            |  11 +
  src/engine/PluginManager.cpp                          |  69 +-
  src/engine/PluginServiceImpl.h                        |   4 +
  src/engine/PluginServiceImpl.cpp                      |  18 +
  src/engine/MidiServiceImpl.h                          |   1 +
  src/engine/MidiServiceImpl.cpp                        |   5 +
  src/engine/AudioEngineCommands_Undo.cpp               |  26 +-
  src/frontend/FrontendServer.cpp                       |  23 +-
  src/frontend/router/Router_Project.cpp                |  53 +
  src/frontend/router/Router_Plugin.cpp                 |  26 +
  src/frontend/router/Router_Midi.cpp                   |  16 +
  src/mcp/McpTools.cpp                                  |   1 +
  src/mcp/McpTools_Private.h                            |   1 +
  src/mcp/McpTools_Project.cpp                          |  37 +
  CMakeLists.txt                                        |   1 +
```

## Priority for next context

1. **B7 (Preset DB)** — smallest engine gap. The MCP tools already work; just need
   RPC wiring + a simple search UI. Do this first.
2. **B6 (Audio pool)** — medium engine work (new service interface + RPCs + UI).
   Design the snapshot struct and service interface before coding.
3. **B5 (Takes/comps)** — needs design discussion before any code. Explore the
   existing `switchClipTake` implementation and ValueTree structure first.
