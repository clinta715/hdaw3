# Handoff — v0.24.0: Batch B + B7 shipped, B6 plan (2026-08-21)

## Purpose

This session completed B7 (Cross-Plugin Preset Browser) from the Batch C plan,
and committed all Batch B + B7 work as v0.24.0. This file is the briefing for
the next session tackling B6 (Audio Pool).

## What was shipped (v0.24.0)

All Batch B items (B1–B4, B8, B9) from the previous handoff plus B7. Committed
and pushed.

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
| B7 | Cross-plugin preset browser | `PresetBrowser.tsx`, `PluginService.h`, `Router_Plugin.cpp` |
| B8 | MCP parity (30 tools) | `McpTools_Settings.cpp`, `McpTools.cpp` |
| B9 | Export range options | `ExportDialog.tsx` |

### Build & test status at commit

| Gate | Result |
|------|--------|
| `cmake --build build --config Debug` | passed |
| `Commands.*` gtests | 41/41 passed |
| `Mcp*` gtests | 27/28 passed (1 pre-existing) |
| `cd frontend && npm test` | 387/387 passed (41 files) |
| `cd frontend && npm run build` | passed |

## B6 — Audio Pool (next work item)

### Design (from AGENTS.md)

Project media follows **Cubase's Audio Pool model** — a centralized pool of all
audio in the project, independent of the arrangement:

- Every imported/recorded audio file appears **once**, with name, source path,
  length, and **usage/reference count** — regardless of how many clips use it.
- Clips **reference** pool items; dragging from the pool to the timeline mints
  a clip. Deleting a clip does not delete the pool item.
- The pool surfaces **unused items** for cleanup and is the single place to see
  what media the project actually carries.
- This lives in / alongside the file-browser region as the project-media side.

### Engine work needed

1. **Define `PoolEntry` snapshot struct** — name, sourcePath, duration,
   usageCount, sampleRate, channels
2. **Add RPCs** in a new `Router_Pool.cpp` (or extend `Router_Project.cpp`):
   - `pool.list` — return all pool entries
   - `pool.getEntry` — return single entry by id/path
   - `pool.cleanup` — remove unused entries
   - `pool.import` — import a file into the pool
3. **Expose `ProjectPool` via service interface** — either a new `PoolService`
   in `src/common/` or extend `ReadModel` to include pool data
4. **Track usage counts** — how many clips reference each pool item
5. **Add MCP parity tools** for pool operations

### Frontend work needed

1. **`PoolPanel.tsx`** — list of pool entries with name, path, duration,
   usage count. Search/filter. "Cleanup unused" button.
2. **Tab registration** — add as a tab in the file browser region (right panel),
   NOT a bottom panel tab. Per AGENTS.md: "Probably a tab in the file browser
   region (right panel)."
3. **Drag from pool to timeline** — creates clip referencing the pool item
4. **Types** — add `PoolEntry` to `types.ts`

### Files to explore

- `src/engine/ProjectPool.h` / `.cpp` — existing pool implementation
- `src/engine/RoutingManager.cpp` — how clips reference pool items
- `src/model/ProjectModel.h` — pool-related ValueTree nodes
- `src/frontend/components/FileBrowser.tsx` — existing file browser for tab
  integration pattern

### Key questions to resolve

1. Does `ProjectPool` already exist as a class? What's its API?
2. How do clips reference pool items today? (ValueTree property? Clip ID?)
3. Where does the pool data live in the ValueTree?
4. Is the pool persisted in the .hdaw file?
5. What's the file browser tab pattern? (How are tabs registered there?)

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
- **PluginService interface:** Custom methods go in `src/common/PluginService.h`
  as pure virtuals, implemented in
  `src/engine/PluginServiceImpl.h/.cpp`, called from `Router_Plugin.cpp` and
  `McpTools_Settings.cpp`.
- **Bottom panel tabs:** `BOTTOM_TAB_IDS` in `uiStore.ts`, registered in
  `App.tsx` `bottomTabs` array. Default is "mixer".
- **File browser tabs:** Check `FileBrowser.tsx` for the tab pattern — likely
  similar to bottom panel but in the right sidebar.

## Version management

- `CMakeLists.txt` → `project(HDAW VERSION 0.24.0 ...)` — canonical for C++
- `frontend/package.json` → `"version": "0.24.0"` — canonical for frontend
- Both bumped in this commit.
