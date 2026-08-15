# Paint/Repeat, Duplicate, and Ghost Clip Design

**Date**: 2026-07-21
**Version**: v0.12.0+

## Overview

Three clip interaction features for the timeline arrange view:

1. **Alt+drag → Paint/Repeat** — tile end-to-end copies along a drag path
2. **Ctrl+drag → Duplicate** — create one standard copy, drag it to position
3. **Ctrl+Shift+drag → Ghost copy** — linked duplicate; content changes to the original propagate to all ghosts

---

## 1. Modifier Scheme

| Modifier | Behavior | Notes |
|----------|----------|-------|
| **Alt + drag** | Paint/repeat — tile copies end-to-end | Replaces old Alt-drag duplicate behavior |
| **Ctrl + drag** | Duplicate — single copy, drag to position | Same code path as old Alt-drag |
| **Ctrl+Shift + drag** | Ghost copy — linked duplicate | New feature |
| No modifier | Move clip(s) | Unchanged |

---

## 2. Ghost Copy — Engine Model

### 2.1 ValueTree Properties

New properties on the CLIP node (`src/model/ProjectModel.h`):

| ID | Type | Default | Purpose |
|----|------|---------|---------|
| `IDs::ghostSourceId` | int | `-1` | clipID of the source clip. `-1` = not a ghost. |
| `IDs::isGhost` | int | `0` | Redundant bool `1` = ghost copy. |

### 2.2 Ghost Creation

**New method** `AudioEngineCommands::createGhostClip(int sourceClipId, double newStart, int newTrackIndex)`:

1. Locate source clip by ID
2. If source is itself a ghost, walk the `ghostSourceId` chain to find the root source
3. Deep-copy the source clip ValueTree
4. Assign a fresh `clipID`
5. Set `ghostSourceId = rootSourceClipId`, `isGhost = 1`
6. Set `startTime = newStart` (the drop position)
7. Add to target track's `CLIP_LIST` via UndoManager
8. Return the new ghost clip ID

**Frontend RPC**: `project.createGhostClip({ sourceClipId, newStart, newTrackIndex })` → returns `{ clipId }`.

### 2.3 Property Propagation

Only **content** properties propagate from source to ghosts. Position, duration, and offset are independent per ghost.

**Propagated** (content):
- `sourceFile` — the audio source file path
- `gain`, `fadeIn`, `fadeOut` — audio mix properties
- `looping`, `offset` — playback behavior
- `stretchMode`, `stretchRatio`, `sourceBpm`, `sourceDuration` — timestretch settings
- `MIDI_NOTE_LIST` content — notes, their positions, velocities, etc.

**Not propagated** (independent):
- `startTime` — timeline position
- `duration` — clip length
- `offset` — playback offset within source (independent per ghost)
- `clipID`, `isGhost`, `ghostSourceId`
- `name`, `color` — cosmetic, independent

**`AudioEngine::valueTreePropertyChanged`** propagation logic:
1. Skip if the changed tree has `isGhost == 1` (ghosts don't propagate)
2. Filter: only propagate listed property IDs (ignore `startTime`, `duration`, `name`, `color`)
3. Scan all CLIP nodes across all tracks for `ghostSourceId == sourceClipId`
4. For each matching ghost, set the same property to `value` via UndoManager
5. Re-entrancy guard: do not propagate changes that originate FROM a ghost. Use an `isPropagating` bool flag (set before, cleared after).

### 2.4 MIDI Note Propagation

**`AudioEngine::valueTreeChildAdded(MIDI_NOTE_LIST parent, MIDI_NOTE child)`**:
- If the parent CLIP is a source (not a ghost):
  - Find all ghosts with `ghostSourceId == sourceClipId`
  - Add a matching note to each ghost's `MIDI_NOTE_LIST` with same properties
  - Ghost notes get new `noteID` values (each ghost has independent IDs)
  - Mutations go through UndoManager

**`AudioEngine::valueTreeChildRemoved(MIDI_NOTE_LIST parent, MIDI_NOTE child)`**:
- If the parent CLIP is a source:
  - Find all ghosts
  - Remove matching note from each ghost by `(noteNumber, startBeat)`

**`AudioEngine::valueTreePropertyChanged(MIDI_NOTE)`**:
- If the parent CLIP is a source, propagate to matching ghost notes
- Match by `(noteNumber, startBeat)`

### 2.5 Deletion Propagation

**`AudioEngine::valueTreeChildRemoved(CLIP)`**:
- If the removed clip has `isGhost == 0` (it's a source):
  - Find all clips where `ghostSourceId == removedClipId`
  - Remove each ghost within the same UndoManager transaction
- If the removed clip is a ghost, do nothing extra

**Guard**: No propagation during project load. Check `engineInitialized` flag.

### 2.6 Ghost-of-Ghost Chain Prevention

`createGhostClip` resolves the chain: if source has `ghostSourceId != -1`, walk up to `ghostSourceId == -1`. The root source becomes the ghost's `ghostSourceId`. Depth never exceeds 1.

### 2.7 ReadModel + FrontendRpc

**C++ `ClipSnapshot`** (`src/common/ReadModel.h`):
```cpp
struct ClipSnapshot {
    // existing fields ...
    bool isGhost = false;
    int ghostSourceId = -1;
};
```

**FrontendRpc `toJson`** (`src/frontend/FrontendRpc.h`):
```cpp
obj["isGhost"] = clip.isGhost;
obj["ghostSourceId"] = clip.ghostSourceId;
```

**TypeScript `ClipSnapshot`** (`frontend/src/rpc/types.ts`):
```typescript
export interface ClipSnapshot {
  // existing fields ...
  isGhost: boolean;
  ghostSourceId: number;
}
```

---

## 3. Paint/Repeat Tool

### 3.1 Trigger

Alt + drag from any clip (or multi-clip selection).

### 3.2 Algorithm (Frontend)

**On mousedown** (Alt held in `handleClipMouseDown`):
- Set paint mode: `isPaintRepeat = true` on drag state
- Record `paintOriginBeat = clip.startBeat`
- Record `paintSpacing = clip.durationBeats`
- Record `paintSourceIds` = set of source clip IDs
- Initialize `paintedClipIds: number[] = []`

**On mousemove** (`handleMouseMove`):
- Compute `mouseBeat` from mouse position relative to tracks div
- Compute `desiredCount = max(0, floor((mouseBeat - paintOriginBeat) / paintSpacing))`
- If `desiredCount > paintedClipIds.length`:
  - For each new tile index `i` from `paintedClipIds.length` to `desiredCount - 1`:
    - `newStart = paintOriginBeat + (i + 1) * paintSpacing`
    - Create clip via `paintClips` RPC (batched) or individual `duplicateClip` + `moveClipWithOverlap`
  - Append new IDs to `paintedClipIds`
- If `desiredCount < paintedClipIds.length`:
  - Remove excess clips via `project.removeClip` for indices beyond `desiredCount`
  - Truncate `paintedClipIds`
- **Throttle**: process at most once per 100ms
- **Collision**: before placing, check snapshot for existing clips at `[newStart, newStart + tileWidth)` on target track. Skip tile if occupied.

**On mouseup**:
- Run `syncSnapshot` to refresh
- Clear paint state

### 3.3 Multi-Clip Paint

When multiple clips are selected, the group's horizontal extent becomes the paint stamp:
- `paintSpacing = maxEnd - minStart` (full group width)
- Each tile duplicates ALL selected clips at their relative offsets within the tile
- Example: clips at beats 1-3 and 5-6 → tile width = 5 → second tile places clips at beats 6-8 and 10-11

### 3.4 Batched Engine RPC

**New method** `project.paintClips({ sourceClipIds, originBeat, spacing, targetTrackIndex, count })`:
- Creates `count` tiles in a single `beginTransaction`/`endTransaction`
- For each tile: deep-copy each source clip, set `startTime` to `originBeat + (tileIndex + 1) * spacing + relativeOffset`, add to target track
- Returns array of new clip IDs
- Avoids N round-trips per tile

### 3.5 Paint with Ghosts

If the source clip is a ghost, painted copies inherit `ghostSourceId`. The paint tool calls `duplicateClip` which deep-copies all properties including ghost metadata. No special handling needed.

---

## 4. Ctrl+drag Duplicate

Same as existing Alt-drag behavior, triggered by `e.ctrlKey`:

1. Mousedown with Ctrl on a clip → `isCtrlDuplicate = true` in drag state
2. First mousemove: duplicate clips via `project.duplicateClip`
3. Switch selection to new clips
4. Continue drag to position
5. Mouseup: commit positions via `project.moveClipWithOverlap`

**Frontend change**: Replace `e.altKey` check with `e.ctrlKey` in `handleClipMouseDown`.

---

## 5. Frontend Visual Changes

### 5.1 Ghost Clip Rendering

New CSS class `.tl-clip--ghost`:
- Dashed border (`2px dashed rgba(255,255,255,0.5)`)
- Subtle opacity reduction (`opacity: 0.85`)
- Small chain-link icon in top-right (CSS `::after`)
- Name suffix ` (ghost)` in tooltip

### 5.2 Paint Drag Visual

During Alt+drag paint:
- Semi-transparent preview of pending tiles at each computed position
- Committed tiles render normally
- Number badge on last tile: `+N`

### 5.3 Cursor

| Action | Cursor |
|--------|--------|
| Ctrl+drag | `copy` |
| Alt+drag (paint) | `crosshair` |
| Ctrl+Shift+drag | `alias` (link icon) |

### 5.4 Ghost Interaction

- Hover tooltip: "Ghost of clip {sourceClipId}"
- Context menu: "Select Original" — finds source clip, selects it, scrolls to it
- Double-click: select original (instead of opening editor)
- Ghosts can be moved/trimmed/deleted independently — position changes are local
- Ghost selection in multi-select: treats ghosts as regular clips for move/delete
- Deleting source: ghosts are deleted too (engine handles this)
- Deleting a ghost: only that ghost is removed (no effect on source or other ghosts)
