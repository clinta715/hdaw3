# Handoff — Batch A UI coverage + Batch B plan (2026-08-21)

## Purpose

This session audited HDAW's UI against the engine's capability surface, identified
~16 features the engine supports but the UI didn't expose, and implemented all 12
"Batch A" items (UI-only wiring of existing engine capabilities). **All changes are
in the working tree (uncommitted).** This file is the briefing for a **fresh context**
that tackles Batch B.

## What was completed (working tree, uncommitted)

31 files changed, +882/−44 lines (frontend + C++). All changes are uncommitted —
`git diff` shows everything.

### Batch A items shipped (12/12)

| # | Feature | Key file(s) | Engine RPC(s) |
|---|---------|-------------|---------------|
| A1 | Flatten Arranger button | `ArrangerChainEditor.tsx` | `project.flattenArranger` |
| A2 | Project key/scale display + editor in transport | `TransportBar.tsx` | `project.setScaleRoot`, `project.setScaleMode`, `composition.getScaleModes` |
| A3 | Export dialog: real cancel, sample rate dropdown, loop-region range, localStorage persistence | `ExportDialog.tsx` | `export.cancel`, `export.audio` (start/end/sampleRate) |
| A4 | Session: Stop All button + clip→scene context menu (1–8 + Remove) | `SessionView.tsx`, `TimelineContextMenu.tsx` | `session.stopAll`, `session.setClipScene` |
| A5 | Marker drag on ruler (snapped, Escape-cancel) | `useTimelineRuler.ts` | `project.setMarkerTime {index, time}` (time in beats) |
| A6 | Clip rename: new RPC + context menu "Rename…" + Inspector editable name field | `ProjectCommands.h`, `AudioEngineCommands_Clips.cpp`, `Router_Project.cpp`, `TimelineContextMenu.tsx`, `Inspector.tsx` | `project.setClipName {clipId, name}` (new) |
| A7 | Missing-file relink: "Search…" button on AudioClipEditor banner + FileMenu "Relink All Missing Files…" | `AudioClipEditor.tsx`, `FileMenu.tsx` | `project.findMissingClipSourceFile`, `project.relinkAllMissingFiles` |
| A8 | Real ghost clone: Ctrl+Shift+drag now calls `createGhostClip` inside a transaction (was silently doing a normal duplicate) | `useTimelineDrag.ts` | `project.createGhostClip`, `project.beginTransaction`/`endTransaction` |
| A9 | Remove dead "1Bar" count-in toggle + implement real Follow playhead auto-scroll | `TransportBar.tsx`, `TimelineMinimal.tsx`, `transportExtrasStore.ts` | none (local + scroll logic) |
| A10 | Automation lane enable/disable toggle per lane | `AutomationPanel.tsx` | `project.setAutomationEnabled {trackIndex, lane, enabled}` |
| A11 | Preferences MIDI "None" → calls `midi.closeDevice` (was calling `openDevice("")`) | `PreferencesDialog.tsx` | `midi.closeDevice` |
| A12 | Replace dead "MCP Server" stub with read-only engine endpoint info | `PreferencesDialog.tsx` | none (reads WS URL at runtime) |

### Build & test status

| Gate | Result |
|------|--------|
| `cmake --build build --config Debug` | ✅ passed |
| `build/Debug/hdaw_tests.exe --gtest_filter="Commands.*"` | ✅ 41/41 passed |
| `cd frontend && npm run build` | ✅ passed |
| `cd frontend && npm test` | ✅ 357/357 passed |
| `cd frontend && npx playwright test` | ✅ 241 passed, 11 failed (all pre-existing) |

All 11 E2E failures are pre-existing (editing copy-paste, import MIDI, midi-fx-chain
dropdown, modulation LFO/waveform, plugin-manager rescan, react-300 stress tests,
session.spec basic flow). None are regressions from Batch A.

## Batch B — next work items

These are the remaining items from the UI audit. They require varying amounts of
engine work (unlike Batch A which was mostly frontend wiring).

### B1: Note operators + per-note expression UI (piano-roll inspector pane)
**Scope:** Big feature. The entire probabilistic/generative note layer has zero UI:
- chance (`setNoteChance`)
- repeat count/rate/curve (`setNoteRepeatCount`/`setNoteRepeatRate`/`setNoteRepeatCurve`)
- occurrence bitmask (`setNoteOccurrence`)
- recurrence (`setNoteRecurrence`)
- per-note gain/pan/pitchOffset/timbre/pressure (`setNoteGain`/`setNotePan`/`setNotePitchOffset`/`setNoteTimbre`/`setNotePressure`)
- clip seed (`setClipSeed`)

**User decision:** Place in a **collapsible inspector pane inside the Piano Roll tab**
(not a separate tab, not the Inspector panel). This is HDAW's generative flagship —
the pane should feel like a natural extension of the note editor.

**Engine:** All RPCs already exist. Frontend needs a new component that:
1. Shows when one or more notes are selected in the NoteGrid
2. Displays current values (read from `read.getNotes` or the snapshot)
3. Calls the appropriate RPC on change
4. Has a "Clip Seed" control at the top that sets the deterministic seed for the clip
5. Bulk operations (set all selected notes' chance to X) via the existing bulk setters

**Files to create/modify:** New `NoteOperatorsPane.tsx` component, wire into `PianoRoll.tsx`.

### B2: Preferences completion
**Engine work needed:**

| Setting | Engine change | UI change |
|---------|--------------|-----------|
| Plugin scan paths | New `plugin.addScanPath`/`removeScanPath`/`getScanPaths` RPCs + persist to QSettings; merge custom dirs into `scanAll` + watcher | New section in PluginManagerDialog or PreferencesDialog |
| Backup count | Expose `maxBackups` (currently hard-coded 10) as QSettings key + RPC | Number input in Preferences |
| Default project tempo/time-sig | New QSettings keys for defaults; read in `createDefaultProject()` | Inputs in Preferences |
| Plugin isolation toggle | Promote `HDAW_NO_PLUGIN_ISOLATION` env var to QSettings + RPC | Toggle in Preferences (advanced section) |
| Plugin watcher toggle | Promote `HDAW_WATCH_PLUGINS` to QSettings + RPC | Toggle in Preferences |
| Snap settings persistence | Persist snap enabled/division/gridOffset/toEvents to localStorage like other layout prefs | Already in uiStore; just add persistence keys |
| MIDI device persistence | Add MIDI keys to `SettingsKeys.h`; add `midi.getOpenDevice` RPC | Preferences MIDI section: initialize dropdown from current device |

### B3: Track reorder + folder reparenting
**Engine RPCs exist:** `project.moveTrack`, `project.moveTrackIntoFolder`,
`project.moveTrackOutOfFolder`. No UI.

**UI:** Drag handle on track headers to reorder tracks. Right-click menu "Move Into
Folder" / "Move Out Of Folder" (or drag onto folder track row). The folder track
collapse chevron already exists.

### B4: Tempo track UI
**Engine:** `read.getTempoPoints` exists (tempo track in the model). Only global BPM
editable today (`project.setTempo`).

**UI:** A tempo automation lane below the ruler, or a "Tempo" tab in the bottom panel
with a tempo-point editor canvas (similar to AutomationLaneCanvas).

### B5: Takes/comps
**Engine:** `audioGraph.switchClipTake` exists. No take UI.

**UI:** TBD — needs design discussion. Bitwig-style take lanes? Clip "takes" dropdown?

### B6: Audio pool
**Engine:** `ProjectPool` is internal. No pool RPC surface. **Feature gap on both sides.**

**Design:** Per AGENTS.md Cubase pool model — centralized media pool with usage counts,
unused cleanup. Needs engine RPCs (`pool.list`, `pool.getEntry`, `pool.cleanup`)
+ UI (probably a tab in the file browser region).

### B7: Cross-plugin preset database
**Engine:** MCP-only (`search_plugin_presets`, `load_plugin_preset_file`). No RPC
equivalent → no possible UI today.

**Fix:** Expose the preset search/load as RPCs (mirror the MCP tools), then build a
preset browser UI (could be a sub-panel of the FX Chain or a PopUpBrowser context).

### B8: MCP parity (reverse direction)
No MCP tools for: audio-device config, MIDI device selection, metronome, punch,
preview player, plugin blacklist, FX A/B snapshots. Per AGENTS.md "MCP feature
parity" rule, these should exist.

### B9: Export range beyond loop region
`export.audio` already accepts `start`/`end` in seconds. ExportDialog now has "Full
Project" / "Loop Region". Could add "Selection" (selected clips' time range) and
"Between Markers" options.

## Architecture notes for the next context

- **Beats vs seconds:** Frontend speaks beats; clip ValueTree and processors speak
  seconds. Marker time is in **beats** (not seconds — T3 verified this). Export
  `start`/`end` are in **seconds**. `createGhostClip.newStart` is in **beats**.
  Convert at boundaries only.
- **Batch RPCs:** One batch call, not N loops. Multi-clip ghost drag uses
  `beginTransaction`/`endTransaction`.
- **Store reads after awaits:** `useProjectStore.getState().snapshot`, never closure
  props (pitfall #5).
- **`read.getClip`** returns a full clip object (not just the name). Access `.name`,
  `.sceneIndex`, etc.
- **Session view** is toggled via the Arr/Sess button in TransportBar
  (`[title="Toggle Session/Arrangement View (Tab)"]`). The "Stop All" button and
  session slots are in `.sv-root`.
- **Bottom-panel tabs** are identified by string ids in `uiStore.ts`
  `BOTTOM_TAB_IDS`. Default is "mixer". Inspector is "inspector". Arranger is
  "arranger".
- **E2E conventions:** `startApp` clicks "New Project". Clips targeted via
  `.tl-clip[data-clip-id="N"]`. Poll with `expect(...).toPass()` for async state
  changes. Context menu items are `<button>` inside `.clip-context-menu`.
- **Inspector clip name** is now an `<input type="text">` (not read-only text).
  Vitest tests use `getByDisplayValue("Kick")` not `getByText("Kick")`.
- **`setClipName`** is the new RPC for renaming clips. It uses the undo manager
  pattern identical to `setMarkerName`. The MCP `set_clip` tool also sets name via
  `setProperty(IDs::name, ..., &um)` — same path.

## File change summary (Batch A)

```
frontend/src/components/ArrangerChainEditor.css    |  10 +++
frontend/src/components/ArrangerChainEditor.tsx    |  20 ++++
frontend/src/components/AudioClipEditor.tsx        |  36 +++++-
frontend/src/components/AutomationPanel.css        |  23 ++++
frontend/src/components/AutomationPanel.tsx        |  14 +++
frontend/src/components/ExportDialog.css           |   7 +-
frontend/src/components/ExportDialog.tsx           | 133 +++++++++++++++---
frontend/src/components/FileMenu.tsx               |  35 ++++++
frontend/src/components/Inspector.tsx              |  18 ++-
frontend/src/components/PreferencesDialog.css      |   1 +
frontend/src/components/PreferencesDialog.tsx      |  17 ++-
frontend/src/components/SessionView.css            |  11 ++
frontend/src/components/SessionView.tsx            |   7 ++
frontend/src/components/TimelineContextMenu.test.tsx|  31 +++++
frontend/src/components/TimelineContextMenu.tsx    |  45 +++++++
frontend/src/components/TimelineMinimal.css        |  51 ++++++++
frontend/src/components/TimelineMinimal/TimelineMinimal.tsx | 17 +++
frontend/src/components/TimelineMinimal/useTimelineRuler.ts | 90 +++++++++++
frontend/src/components/TransportBar.css           |  48 +++++-
frontend/src/components/TransportBar.tsx           | 107 +++++++++++++-
frontend/src/hooks/useTimelineDrag.test.ts         | 106 ++++++++++++++
frontend/src/hooks/useTimelineDrag.ts              |  22 ++++
frontend/src/store/transportExtrasStore.ts         |   2 -
frontend/e2e/batch_a_flatten_relink.spec.ts        |  (new)
frontend/e2e/batch_a_session_rename.spec.ts        |  (new)
frontend/e2e/batch_a_transport.spec.ts             |  (new)
frontend/e2e/batch_a_export_prefs.spec.ts          |  (new)
frontend/e2e/batch_a_timeline_interactions.spec.ts  |  (new)
src/common/ProjectCommands.h                       |   1 +
src/engine/AudioEngineCommands.h                   |   1 +
src/engine/AudioEngineCommands_Clips.cpp           |   9 ++
src/frontend/router/Router_Project.cpp             |   1 +
tests/unit/common/commands_test.cpp                |  30 +++++
```

## Priority for next context

1. **B1 (note operators)** is the highest-impact item — it's HDAW's generative
   differentiator and currently invisible. Start here.
2. **B2 (Preferences completion)** — plugin scan paths is the #1 real-world user
   complaint. Do the engine + UI together.
3. **B3 (track reorder)** — small, high-UX-value, all engine RPCs exist.
4. **B4–B9** — order by user demand; B6 (audio pool) and B7 (preset DB) need
   engine work first.
