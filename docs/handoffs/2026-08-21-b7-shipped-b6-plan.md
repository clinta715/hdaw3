# Handoff — v0.24.0: Batch B + B6 + B7 shipped, B5 plan (2026-08-21)

## Purpose

This session completed B7 (Cross-Plugin Preset Browser) and B6 (Audio Pool)
from the Batch C plan, plus all Batch B items. Committed as v0.24.0. This file
briefs the next session tackling B5 (Takes/Comps).

## What was shipped (v0.24.0)

All Batch B items (B1–B4, B8, B9) plus B6 and B7. Committed and pushed.

### Items included

| # | Feature | Key files |
|---|---------|-----------|
| B1 | Note Operators + per-note expression UI | `NoteOperatorsPane.tsx`, `PianoRoll.tsx` |
| B2-1 | Plugin scan paths | `PluginManager.cpp`, `PluginServiceImpl.cpp`, `PluginManagerDialog.tsx` |
| B2-2 | Backup count setting | `SettingsKeys.h`, `PreferencesDialog.tsx` |
| B2-3 | Default tempo/time-sig | `SettingsKeys.h`, `PreferencesDialog.tsx` |
| B2-4 | Plugin isolation toggle | `SettingsKeys.h`, `PreferencesDialog.tsx` |
| B2-5 | Plugin watcher toggle | `SettingsKeys.h`, `PreferencesDialog.tsx` |
| B2-6 | Snap settings persistence | `uiStore.ts` |
| B2-7 | MIDI device persistence | `MidiService.h`, `PreferencesDialog.tsx` |
| B3 | Track drag-to-reorder | `TrackHeaders.tsx` |
| B4 | Tempo track UI + RPC wiring | `TempoEditor.tsx`, `Router_Project.cpp` |
| B6 | Audio Pool | `PoolView.tsx`, `Router_Pool.cpp`, `FileBrowser.tsx` |
| B7 | Cross-plugin preset browser | `PresetBrowser.tsx`, `PluginService.h`, `Router_Plugin.cpp` |
| B8 | MCP parity (30 tools) | `McpTools_Settings.cpp`, `McpTools.cpp` |
| B9 | Export range options | `ExportDialog.tsx` |

### Build & test status at commit

| Gate | Result |
|------|--------|
| `cmake --build build --config Debug` | passed |
| `Commands.*` gtests | 41/41 passed |
| `Mcp*` gtests | 27/28 passed (1 pre-existing) |
| `cd frontend && npm test` | 391/391 passed (42 files) |
| `cd frontend && npm run build` | passed |

## B5 — Takes/Comps (last remaining item)

### Current state

From the previous handoff: "`audioGraph.switchClipTake` exists. No take UI."

### Design questions to resolve (from previous handoff)

1. How are takes stored in the ValueTree? (Check `IDs::takeIndex` or similar)
2. Does `switchClipTake` swap the entire clip or just the audio source?
3. How do takes interact with recording (punch in/out creates new takes)?
4. What's the UX for comping (assembling best parts from multiple takes)?

### Design options (from previous handoff)

- **Bitwig-style take lanes** — visual lanes below the main lane per track
- **Clip "takes" dropdown** — in the Inspector or clip context menu
- **A "Takes" tab** — in the bottom panel

### Files to explore

- `src/engine/AudioEngineCommands_Clips.cpp` — `switchClipTake` implementation
- `src/model/ProjectModel.h` — take-related IDs (`IDs::takeIndex` etc.)
- `src/frontend/router/Router_AudioGraph.cpp` — `switchClipTake` RPC
- `src/engine/RoutingManager.cpp` — how takes are handled in the audio graph
- `src/engine/ClipSourceProcessor.cpp` — how the audio source is swapped

### Approach

1. **Explore first** — understand the existing engine take infrastructure
2. **Design discussion** — present options to user, get decision
3. **Implement** — based on chosen design

## Architecture notes

- **Beats vs seconds:** Frontend speaks beats; clip ValueTree and processors
  speak seconds. Marker time is in **beats**. Export `start`/`end` are in
  **seconds**. Tempo points use **seconds** (`timeSeconds`).
- **Batch RPCs:** One batch call, not N loops.
- **Store reads after awaits:** `useProjectStore.getState().snapshot`, never
  closure props (pitfall #5).
- **MCP parity rule:** Any user-facing feature must also be an MCP tool.
- **QSettings pattern:** Engine-side settings use `SettingsKeys.h` constants +
  `QSettings` read/write. Frontend reads via RPC, never directly.
- **Bottom panel tabs:** `BOTTOM_TAB_IDS` in `uiStore.ts`, registered in
  `App.tsx` `bottomTabs` array. Default is "mixer".
- **File browser filter chips:** `FileKindFilter` in `browserStore.ts`, chips
  rendered in `FileBrowser.tsx`. "Pool" was added for B6.

## Version management

- `CMakeLists.txt` → `project(HDAW VERSION 0.24.0 ...)` — canonical for C++
- `frontend/package.json` → `"version": "0.24.0"` — canonical for frontend
