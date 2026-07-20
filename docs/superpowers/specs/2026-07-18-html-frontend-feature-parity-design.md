# HTML Frontend Feature Parity — Design Spec

**Date**: 2026-07-18
**Status**: Approved
**Scope**: Bring the React/HTML frontend to core workflow parity with the Qt GUI

## Problem

The HTML/Electron frontend (`frontend/`) covers ~60% of the Qt GUI's feature surface.
The remaining ~40% blocks daily-use workflows: multi-clip selection, file I/O dialogs,
real waveforms, audio clip editor, piano roll depth, FX chain plugin management, and
markers. The goal is to close these gaps in 4 incremental phases.

## Architecture

- **No new backend RPC methods** are needed for most features. The `FrontendServer`
  already exposes ~100 methods across 7 namespaces (`project.*`, `transport.*`,
  `audioGraph.*`, `read.*`, `plugin.*`, `pluginParam.*`, `midi.*`).
  **Verification needed**: confirm `project.exportAudio` exists in `FrontendRouter.cpp`;
  if not, it must be added (delegates to `ExportManager`).
- **One backend addition**: `read.getWaveformPeaks({clipId})` returns downsampled
  peak data for real waveform rendering. This is a small C++ addition that reads from
  the existing `ProjectPool` thumbnail cache.
- **Plugin editors** open as native windows via `audioGraph.toggleFXEditor` — zero
  frontend work needed.
- **Existing stores extend naturally**: `uiStore` gains multi-selection,
  `markerStore` is new, `automationStore` gains clipboard support.

## Phase 1: Timeline Interactions + Markers

### 1A. Multi-clip selection

**`uiStore` changes:**
- Replace `selectedClipId: number | null` with `selectedClipIds: Set<number>`
  (**breaking change**: `App.tsx:33`, `ClipEditor.tsx`, `TimelineMinimal.tsx`,
  `AutomationPanel.tsx`, and `PianoRoll.tsx` all reference `selectedClipId` —
  each must be updated to check `selectedClipIds.size === 1` or iterate the set)
- Add `lastSelectedClipId: number | null` for range-select anchor
- `selectClip(id, trackIndex?)` — single select (clears others, sets anchor)
- `toggleClipSelection(id)` — Ctrl+click toggle (adds/removes from set)
- `selectRange(fromId, toId, clips)` — Shift+click range (same track, between
  anchor and clicked)
- `selectAllClips(clips)` — Ctrl+A
- `clearSelection()` — Escape

**`TimelineMinimal.tsx` changes:**
- Clip click: single select (clears others)
- Ctrl+click on clip: toggle in selection set
- Shift+click on clip: range select (same track, from anchor)
- Rubber band: mousedown on empty area + drag draws selection rectangle;
  on release, all clips whose bounding box intersects are selected
- Ctrl/Shift+rubber band: additive to existing selection
- All selected clips move together during drag (iterate `selectedClipIds`)
- Delete/Backspace removes all selected clips via `project.removeClip` per clip
- Ghost clips show for all selected clips during drag
- Visual: selected clips get a bright outline (CSS class `tl-clip--selected`)

### 1B. Copy/Cut/Paste/Duplicate clips

**`uiStore` changes:**
- `clipClipboard: ClipSnapshot[]` — copied clip data (not IDs — paste creates new clips)

**New keyboard shortcuts in `TimelineMinimal.tsx`:**
- Ctrl+C: copy selected clips to clipboard (store snapshot data with current
  properties)
- Ctrl+X: copy to clipboard + remove originals
- Ctrl+V: paste at playhead position. Offset clips so the earliest clip starts
  at the playhead. Call `project.addMidiClip` or `project.addAudioClip` for each.
- Ctrl+D: duplicate selected clips at playhead (same as copy+paste but without
  clipboard intermediate)

### 1C. Alt+click duplicate drag

- Alt+mousedown on clip: begin drag. On first mousemove, call
  `project.duplicateClip` for each selected clip, update selection to new IDs,
  then continue drag with the new clips.

### 1D. Context menu enhancements

**Right-click on clip — add:**
- Copy (Ctrl+C)
- Cut (Ctrl+X)
- Duplicate (Ctrl+D)
- (Existing: Delete, Split)

**Right-click on empty area — add:**
- Add Track
- Paste (if clipboard non-empty, greyed out otherwise)
- Add Tempo Change Here... → `window.prompt("BPM:")` → `project.setTempo`
- Set Global BPM... → `window.prompt("BPM:")` → `project.setTempo`
- Add MIDI Clip (4s at click position) → `project.addMidiClip`

### 1E. Fade handle drag on clips

- Detect mouse within top-left 12px of clip body → `ew-resize` cursor
- Detect mouse within top-right 12px of clip body → `ew-resize` cursor
- Mousedown on fade handle starts fade drag
- Mousemove updates `fadeIn`/`fadeOut` proportionally to drag distance
- Mouseup commits via `project.setClipFadeIn` / `project.setClipFadeOut`
- Visual: curved fade overlay rendered on clip (canvas or SVG path)

### 1F. Markers

**New store `markerStore.ts`:**
```ts
interface MarkerSnapshot {
  index: number;
  name: string;
  time: number;  // beat position
  color: number;
}

interface MarkerState {
  markers: MarkerSnapshot[];
  syncMarkers: (rpc: RpcClient) => Promise<void>;
}
```

**Timeline ruler additions:**
- Render marker pins (triangles) at beat positions above the ruler
- Click marker: seek to that position (`transport.seekToSeconds`)
- Drag marker: move to new beat → `project.setMarkerTime`
- Double-click marker: inline rename (input overlay) → `project.setMarkerName`
- Right-click ruler empty area: "Add Marker Here" → `project.addMarker`
- Right-click marker: "Rename", "Delete" context menu

---

## Phase 2: File I/O + Real Waveforms

### 2A. File menu enhancements

**`FileMenu.tsx` rewrite — full menu:**
```
File
├── New Project          (Ctrl+N) — unsaved-changes prompt
├── Open...              (Ctrl+O) — path input or Electron dialog
├── Open Recent ▶        — last 8 from localStorage
│   ├── project1.hdaw
│   ├── project2.hdaw
│   └── Clear Recent
├── Save                 (Ctrl+S)
├── Save As...           (Ctrl+Shift+S)
├── ─────────────
├── Import Audio...      (Ctrl+Shift+I)
├── Import MIDI...       (Ctrl+Shift+M)
├── Export Audio...      (Ctrl+E)
├── ─────────────
└── Exit                 (Ctrl+Q)
```

**Unsaved changes prompt:**
- Before New/Open/Exit: check `isDirty`. If true, show
  `window.confirm("Project has unsaved changes. Continue?")`.
- Save: uses current `filePath` (stored in `projectStore`). If none, falls back
  to Save As.
- Save As: `window.prompt("Save path:", "project.hdaw")` → `project.saveProject`
- Open: `window.prompt("Open path:", "project.hdaw")` → `project.loadProject`

**Recent projects:**
- Stored in `localStorage` key `hdaw_recent_projects` (JSON array of paths, max 8)
- On load: push current path to front, deduplicate, trim to 8
- "Clear Recent" empties the list
- Missing files auto-removed on click (try load, catch error)

### 2B. Import Audio / Import MIDI dialogs

**New component `ImportDialog.tsx`:**
```
┌──────────────────────────────────────┐
│ Import Audio                         │
├──────────────────────────────────────┤
│ File: [path................] [Browse] │
│ Target Track: [▼ Track 1 / New Track]│
│ BPM Metadata: 120.0 (auto-detected)  │
│ □ Auto tempo-match to project BPM    │
│                                      │
│              [Import]  [Cancel]       │
└──────────────────────────────────────┘
```

- File path: `window.prompt` or Electron `dialog.showOpenDialog` if available
- Track selector: dropdown of existing tracks + "New Track" option
- BPM metadata: displayed if detected (backend reads WAV `bext`/`INFO`/ID3v2)
- Auto tempo-match checkbox: if checked, sets `stretchMode=1` on the clip after import
- Import: calls `project.addAudioClip` or `project.addMidiClip` with selected track

### 2C. Export Audio dialog

**New component `ExportDialog.tsx`:**
```
┌──────────────────────────────────────┐
│ Export Audio                         │
├──────────────────────────────────────┤
│ Output: [path................] [Browse]│
│ Format: [▼ WAV]  Bit Depth: [▼ 24]   │
│                                      │
│ [████████████████░░░░░] 75% Exporting │
│                                      │
│            [Export]  [Cancel]         │
└──────────────────────────────────────┘
```

- Format combo: WAV, AIFF, FLAC
- Bit depth combo: 16, 24, 32
- Progress bar: updated via `notify.exportProgress` notification
- Export button: calls `project.exportAudio` (or the MCP `export_audio` equivalent
  if the frontend server exposes it — check `FrontendRouter`). **If the method
  does not exist**, add it to `FrontendRouter.cpp` delegating to `ExportManager`.

### 2D. Real waveform rendering

**Backend addition** (new RPC method):
- `read.getWaveformPeaks({clipId})` → `{peaks: number[], sampleRate: number, numSamples: number}`
  (JSON array of floats, min/max pairs interleaved)
- Implementation: `ProjectPool` already caches thumbnails for the Qt GUI.
  The new method reads from that cache and returns downsampled min/max pairs
  (one pair per pixel at default zoom, ~1000-2000 points per clip).
- If no cached data exists, returns null (frontend falls back to placeholder).

**`WaveformCanvas.tsx` rewrite:**
- Accept `clipId` prop
- Fetch peaks via RPC on mount / clipId change (cache in component state)
- Render min/max pairs as filled area using canvas:
  - Upper line: max values, lower line: min values
  - Fill between with gradient (amber tones, matching current theme)
  - HiDPI support (already present)
- If peaks are null (no data), render a flat line at center

### 2E. Keyboard shortcuts for file operations

Add to global `keydown` handler in `App.tsx` or `TimelineMinimal.tsx`:
- Ctrl+N: New Project (with unsaved prompt)
- Ctrl+O: Open (with unsaved prompt)
- Ctrl+S: Save
- Ctrl+Shift+S: Save As
- Ctrl+Shift+I: Import Audio
- Ctrl+Shift+M: Import MIDI
- Ctrl+E: Export Audio

---

## Phase 3: Audio Clip Editor

**New component `AudioClipEditor.tsx`** — bottom panel tab index 4.

### Layout
```
┌─────────────────────────────────────────────────────────┐
│ [▶][■] [+][-] Clip Name — Track Name  [Audio]   [X]   │
├─────────────────────────────────────────────────────────┤
│                                                         │
│              Waveform Display (canvas)                  │
│              - zoom/scroll                              │
│              - fade handles (top-left/right)            │
│              - playhead cursor                          │
│              - region selection (click+drag)            │
│              - gain envelope overlay                    │
│                                                         │
├─────────────────────────────────────────────────────────┤
│ Src BPM [___] Mode [▼] Ratio [===] [Fit Loop]          │
│ Gain [=== -3dB] FadeIn [___] FadeOut [___] Loop [□]     │
│ Offset [___] Duration [___]                             │
│ [Slice @ Playhead] [Slice @ Transients] [Slice @ Sel]   │
│ Selection: 1.0s – 2.5s  [Copy] [Cut] [Paste]            │
└─────────────────────────────────────────────────────────┘
```

### Subcomponents

**`AudioWaveformDisplay.tsx`:**
- Canvas-based waveform rendering (reuses `WaveformCanvas` peak data)
- Zoom: +/- buttons, Ctrl+wheel (pixels per second)
- Scroll: horizontal drag, or scrollbar
- Playhead: vertical line updated at 16ms via timer during playback
- Fade handles: draggable corners (top-left = fade-in, top-right = fade-out)
- Region selection: click+drag draws time range; shift+click extends
- Gain envelope overlay: same as `GainEnvelopeEditor` but composited on waveform
- Right-click context menu: Copy, Cut, Paste, Select All

**`AudioClipControls.tsx`:**
- Source file label (with "!! Missing" warning if file not found)
- Gain slider (0-2 range, display in dB)
- Fade In/Out spins (0-60s, 3 decimal places)
- Loop checkbox
- Offset/Duration spins
- Timestretch controls (Src BPM, Mode, Ratio, Fit to Loop)
- Slice buttons (at playhead, at transients, at selection)
- Selection label + region clipboard buttons

### Local playback
- Play button: calls `transport.seekToSeconds` to clip start, then
  `transport.play` with a clip-boundary watcher that stops at clip end
- Stop button: `transport.stop`
- 16ms timer updates playhead position on waveform

### Integration
- Double-click audio clip → opens `AudioClipEditor` in bottom tab
- Adds "Audio Editor" tab to `BottomTabs.tsx` (index 4)
- `BottomTabs` now has 5 tabs: Mixer, Piano Roll, Automation, FX Chain, Audio Editor
- `uiStore` tracks `activeBottomTab: string` for programmatic tab switching
- `clipSelected` handler: if audio clip → switch to Audio Editor tab; if MIDI → switch to Piano Roll tab

### RPC methods used
- `read.getWaveformPeaks` — waveform data
- `read.getClip` — clip properties
- `project.setClipGain`, `setClipFadeIn`, `setClipFadeOut`, `setClipLooping`
- `project.setClipOffset`, `setClipDuration`
- `project.setClipSourceBpm`, `setClipStretchMode`, `setClipStretchRatio`
- `project.tempoMatchClip`, `fitClipToLoop`
- `project.sliceClipAtPlayhead`, `sliceClipAtTransients`, `sliceClipAtTimes`
- `project.copyAudioClipRegion`, `cutAudioClipRegion`, `pasteAudioClipRegion`
- `transport.play`, `transport.stop`, `transport.seekToSeconds`

---

## Phase 4: Piano Roll Depth + FX Chain

### 4A. Piano Roll enhancements

#### Velocity lane
- Bottom strip below note grid, 40px tall
- Vertical bars per note, height = velocity/127
- Color: same accent as note opacity
- Click+drag to adjust velocity → `project.setNoteVelocity`
- Syncs with note selection (selected notes highlighted in velocity lane)

#### CC lane
- Combo selector (CC1–CC127) in piano roll toolbar
- Displays CC data curve (canvas rendering)
- Click to add CC point → `project.addCcPoint`
- Drag to move CC points

#### Keyboard shortcuts (add to `NoteGrid.tsx` global handler)
- Up/Down arrow: transpose selected ±1 semitone → `project.setNotePitch`
- Ctrl+Up/Down: transpose selected ±1 octave
- Q: quantize selected notes (snap startBeat to grid) → `project.setNoteStart`
- H: humanize (randomize startBeat ± small amount, velocity ± small amount)
- Ctrl+C: copy selected notes to note clipboard
- Ctrl+X: cut selected notes
- Ctrl+V: paste notes at scroll position
- Ctrl+A: select all notes in current clip

#### Context menu on note
- Quantize
- Humanize
- Transpose Up +1 / Down -1 / Up Octave / Down Octave
- Delete Selected

#### Chord stamp mode
- Checkbox in piano roll toolbar: "Chord Stamp"
- Chord type combo: populated from `PhraseGenerator::getChordTypes()` equivalent
  (Major, Minor, Dim, Aug, Maj7, Min7, Dom7, etc.)
- Voicing combo: Close, Open, Spread
- When enabled, click on grid stamps a chord (multiple notes) instead of single note

#### Snap enhancements
- Add triplet divisions: 1/3, 1/6, 1/12, 1/24
- Snap combo in piano roll toolbar (independent of timeline snap, persisted separately)

### 4B. FX Chain enhancements

#### Plugin selection menu
- "Add FX" button opens a dropdown menu:
  - **Internal**: EQ, Compressor, Reverb, Delay
  - **Separator**
  - **Instruments**: list from `plugin.getInstrumentPlugins()`
  - **Effects**: list from `plugin.getEffectPlugins()`
- Selecting a plugin calls `project.addFxSlot` with the appropriate type/pluginId

#### Per-slot enhancements
- **Edit button**: calls `audioGraph.toggleFXEditor({trackIndex, slotIndex})`
  — opens native plugin editor window
- **Move up/down buttons**: calls `project.reorderFxSlots({trackIndex, fromSlot, toSlot})`
- **Parameter sliders**: for each slot with `paramCount > 0`:
  - Fetch params via `pluginParam.getParams({trackIndex, pluginID})`
  - Render a collapsible section with labeled sliders
  - On slider change: `pluginParam.setParam({trackIndex, pluginID, paramIndex, normalizedValue})`
  - Display current value text via `pluginParam.getParamText`
- **Plugin name**: already displayed; ensure it shows the actual plugin name, not "Slot N"

#### Drag reorder
- Each slot row is draggable (HTML5 drag-and-drop or pointer events)
- Visual: drop indicator line between slots
- On drop: `project.reorderFxSlots({trackIndex, fromSlot, toSlot})`

#### FX types
- The `addFxSlot` RPC already supports `fxType` strings: "eq", "compressor",
  "reverb", "delay", "plugin"
- For plugin types, pass `pluginId` from the scanned plugin list

---

## New Stores Summary

### `markerStore.ts` (Phase 1)
```ts
interface MarkerState {
  markers: MarkerSnapshot[];
  syncMarkers: (rpc: RpcClient) => Promise<void>;
}
```

### Extensions to existing stores

**`uiStore` (Phase 1):**
- `selectedClipIds: Set<number>` (replaces `selectedClipId`)
- `lastSelectedClipId: number | null` (range anchor)
- `clipClipboard: ClipSnapshot[]`
- `activeBottomTab: string` (Phase 3)

**`projectStore` (Phase 2):**
- `filePath: string | null` (current save path)
- `recentProjects: string[]` (from localStorage)

---

## New Components Summary

| Component | Phase | Location |
|-----------|-------|----------|
| `ImportDialog.tsx` | 2 | `frontend/src/components/` |
| `ExportDialog.tsx` | 2 | `frontend/src/components/` |
| `AudioClipEditor.tsx` | 3 | `frontend/src/components/` |
| `AudioWaveformDisplay.tsx` | 3 | `frontend/src/components/` |
| `AudioClipControls.tsx` | 3 | `frontend/src/components/` |
| `VelocityLane.tsx` | 4 | `frontend/src/components/` |
| `CcLane.tsx` | 4 | `frontend/src/components/` |

## Modified Components Summary

| Component | Phase | Changes |
|-----------|-------|---------|
| `TimelineMinimal.tsx` | 1 | Multi-select, rubber band, copy/paste, context menus, fade handles, markers |
| `uiStore.ts` | 1 | Multi-selection state, clipboard |
| `FileMenu.tsx` | 2 | Full menu with import/export/recent |
| `WaveformCanvas.tsx` | 2 | Real peak data rendering |
| `TransportBar.tsx` | 2 | File keyboard shortcuts |
| `BottomTabs.tsx` | 3 | Add Audio Editor tab |
| `NoteGrid.tsx` | 4 | Keyboard shortcuts, context menu, chord stamp |
| `PianoRoll.tsx` | 4 | Velocity lane, CC lane, snap enhancements |
| `FXChain.tsx` | 4 | Plugin selection, reorder, param sliders, edit button |
| `App.tsx` | 2 | Global keyboard shortcuts |

## Backend Changes

| Change | Phase | Effort |
|--------|-------|--------|
| `read.getWaveformPeaks({clipId})` | 2 | Small — reads from existing `ProjectPool` thumbnail cache |

All other features use existing RPC methods already exposed by `FrontendServer`.

## Testing Strategy

Each phase should be verified by:
1. Manual testing: run `npm run dev` in `frontend/` + `HDAW.exe --headless --port=8766`
2. Verify all new interactions work end-to-end
3. Verify no regressions in existing functionality
4. Check WebSocket reconnection after engine restart

## Out of Scope (deferred to post-parity)

These features exist in the Qt GUI but are not part of the core workflow:
- Step Sequencer panel
- Modulation (LFO) panel
- Plugin Scanner dialog
- Preferences dialog (audio/MIDI/MCP settings)
- Project Pool Browser (file browser with preview)
- Status Bar
- Phrase Generator dialog
- About dialog
- Startup dialog
- MIDI device selector
- Input monitoring toggle in track headers
- Track height resize drag
- Volume/pan knob drag in track headers
- Metronome toggle
- Time signature selector
- Follow (auto-scroll) toggle
- CC Rec button
- Count-in toggle
