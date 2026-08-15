# Tempo Changes on the Ruler — Design Spec

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create the implementation plan.

**Goal:** Add tempo point interactions to the TimeRuler — add/remove/edit via context menu, drag to reposition.

**Architecture:** Add 4 tempo-point CRUD commands to `ProjectCommands`/`AudioEngineCommands`. Extend `TimeRuler` with hit-testing, context menu actions, and drag interaction. The existing `AudioEngine` ValueTree listeners already trigger `rebuildTempoMap()` on any tempo point mutation — no engine changes needed.

**Tech Stack:** Qt 6 (QMenu, QInputDialog, mouse events), JUCE ValueTree + UndoManager

---

## Current State

- Tempo points stored in `TEMPO_POINT_LIST` > `TEMPO_POINT` (properties: `startTime` seconds, `tempo` BPM)
- Ruler already **paints** tempo markers (accent-colored tick + BPM label)
- TimelineView context menu has "Add Tempo Change Here..." (inline ValueTree manipulation, not via ProjectCommands)
- `AudioEngine::rebuildTempoMap()` auto-triggers on add/remove/property-change of tempo points
- No `ProjectCommands` for tempo point CRUD
- No ruler interactions for tempo points

---

## Design

### 1. ProjectCommands — Tempo Point CRUD

Add to `src/common/ProjectCommands.h`:

```cpp
virtual int addTempoPoint(double timeSeconds, double bpm) = 0;
virtual void removeTempoPoint(int index) = 0;
virtual void setTempoPointBpm(int index, double bpm) = 0;
virtual void setTempoPointTime(int index, double timeSeconds) = 0;
```

Implement in `AudioEngineCommands.cpp`:

- `addTempoPoint`: Get/create `TEMPO_POINT_LIST`, create `TEMPO_POINT` child with `startTime` and `tempo` properties, add via UndoManager. Return the new index.
- `removeTempoPoint`: Bounds-check index, `removeChild(index, &um)`.
- `setTempoPointBpm`: Bounds-check, `setProperty(IDs::tempo, bpm, &um)`.
- `setTempoPointTime`: Bounds-check, `setProperty(IDs::startTime, timeSeconds, &um)`.

All mutations go through `UndoManager` for Ctrl+Z. The existing `AudioEngine` listeners handle `rebuildTempoMap()` automatically.

### 2. TimeRuler — Hit Testing

Add a private helper:

```cpp
int tempoPointIndexAtX(double x) const;
```

Iterates `readModel->getTempoPoints()`, converts each point's `timeSeconds` to screen X via `xFromTime()`, returns the index of the first point within ±6px of the click. Returns -1 if none.

### 3. TimeRuler — Context Menu

In `contextMenuEvent`, after the existing loop/marker actions, add tempo actions:

**Always visible:**
- "Add Tempo Change Here..." — `QInputDialog::getDouble` for BPM (20–999, 1 decimal), calls `projectCmds->addTempoPoint(timeAtClick, bpm)`.

**Visible only when clicking near a tempo marker** (hit test returns ≥ 0):
- "Edit BPM..." — `QInputDialog::getDouble` pre-filled with current BPM, calls `projectCmds->setTempoPointBpm(idx, newBpm)`.
- "Remove Tempo Point" — calls `projectCmds->removeTempoPoint(idx)`.

### 4. TimeRuler — Drag to Reposition

**State:** Add members:
```cpp
int draggingTempoIndex = -1;
```

**mousePressEvent:** After the existing loop-drag logic, check `tempoPointIndexAtX(x)`. If hit, set `draggingTempoIndex` and accept the event.

**mouseMoveEvent:** If `draggingTempoIndex >= 0`, compute new time from mouse X via `timeFromX(x)`, clamp to ≥ 0, call `projectCmds->setTempoPointTime(draggingTempoIndex, newTime)`.

**mouseReleaseEvent:** If `draggingTempoIndex >= 0`, reset to -1. The ValueTree listener already triggered `rebuildTempoMap()` during the drag.

### 5. What Stays the Same

- `AudioEngine::rebuildTempoMap()` — already triggered by ValueTree listeners on add/remove/property-change
- `TransportManager` — already consumes the tempo map lock-free
- `ReadModel::getTempoPoints()` — already returns all points
- Ruler painting — already draws tempo markers (tick + BPM label)
- TimelineView "Add Tempo Change Here..." — remains as-is (can be refactored later to use the new command)

### Edge Cases

- **Drag past time 0:** Clamp to 0.0 seconds.
- **Multiple points at same time:** Allowed (no dedup). The tempo map sorts by time; equal-time points resolve to the last one in sort order.
- **Removing the only tempo point:** Allowed. `TransportManager::getBpmAtTime()` falls back to global BPM when the map is empty.
- **Undo:** All mutations go through UndoManager. Ctrl+Z reverses add/remove/edit/drag.
