# Piano Roll — Snap-to-Grid

**Date**: 2026-06-10
**Status**: Approved design, pending implementation
**Project**: HDAW v0.2.0 → v0.3.0
**Feature area**: Piano roll polish (priority #1)

## Motivation

The piano roll's note grid has no snap-to-grid. Notes can be placed at
arbitrary fractional beat positions, which makes editing imprecise and
prevents rhythmic alignment. This is the first piano-roll polish feature
because quantize, copy/paste, and other editing tools depend on a reliable
grid.

## Snap resolution

Ten snap divisions, exposed in a QComboBox:

| Label | Value  | Note              |
|-------|--------|-------------------|
| 1/1   | 1.0    | Whole note        |
| 1/2   | 0.5    | Half note         |
| 1/4   | 0.25   | Quarter note      |
| 1/8   | 0.125  | Eighth note       |
| 1/16  | 0.0625 | Sixteenth note    |
| 1/32  | 0.03125| Thirty-second     |
| 1/3   | 1.0/3  | Quarter triplet   |
| 1/6   | 1.0/6  | Eighth triplet    |
| 1/12  | 1.0/12 | Sixteenth triplet |
| 1/24  | 1.0/24 | Thirty-second tpl |

Default: 1/16 (sixteenth note).

## Snap math

A free function (or private method) in `NoteGridWidget`:

```
snapToGrid(beat, division) = max(0.0, round(beat / division) * division)
```

## Where snap applies

| Operation | What snaps               | Notes                             |
|-----------|--------------------------|-----------------------------------|
| Click to create note | `startBeat`              | `createNoteAtPos`                 |
| Drag move            | `startBeat`              | `mouseMoveEvent` → Move           |
| Drag resize-right    | End beat → `duration`    | Recalculate from snapped end beat |
| Drag resize-left     | `startBeat` → `duration` | Recalculate from snapped start    |
| Note pitch           | Never                    | Already snapped by integer keys   |

## UI changes

### PianoRollWidget header bar

Two new widgets between the title label and the zoom buttons:

1. A **checkable QPushButton** labelled "Snap", checked by default
2. A **QComboBox** with the 10 division labels

Both are constructed in `PianoRollWidget::setupUI()`.

### Connections

```cpp
connect(snapBtn, &QPushButton::toggled, noteGrid, &NoteGridWidget::setSnapEnabled);
connect(snapCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
    double divisions[] = {1.0, 0.5, 0.25, 1.0/8, 1.0/16, 1.0/32, 1.0/3, 1.0/6, 1.0/12, 1.0/24};
    noteGrid->setSnapDivision(divisions[idx]);
});
```

## Edge cases

1. **Resize-left with snap**: Compute `newStart = snapToGrid(newStart)`. If
   `newStart >= endBeat`, clamp to `endBeat - division`.
2. **Resize-right with snap**: Compute `endBeat = snapToGrid(startBeat + duration)`.
   If `endBeat <= startBeat`, clamp to `startBeat + division`.
3. **Move with snap**: Snap the new `startBeat` only. Pitch never snaps (it
   is quantised to integer note numbers by the pixel-to-key calculation).
4. **Create with snap**: Snap `beat` in both `createNoteAtPos` and the
   inline creation path in `mousePressEvent`.

## Grid rendering

When `snapEnabled == true`, paint faint vertical lines at every snap
division boundary. Use `QPen(QColor(255,255,255,4), 1)` so they are
visible but do not clutter the beat/bar lines.

## Files changed

- **`NoteGridWidget.h`** — add members `snapEnabled`, `snapDivision`;
  methods `setSnapEnabled(bool)`, `setSnapDivision(double)`, `snapToGrid(double)`.
- **`NoteGridWidget.cpp`** — snap calls in `createNoteAtPos`, `mouseMoveEvent`
  (Move/ResizeLeft/ResizeRight); paint snap grid lines in `paintEvent`.
- **`PianoRollWidget.cpp`** — add Snap button + combo in `setupUI`; connect
  signals in `connectSignals`.

No changes to `PianoRollModel.h`, `PianoRollWidget.h`, or `NoteGridWidget.cpp`
signature beyond the new methods above.

## Testing

Manual verification checklist:
1. Snap off — notes place at any fractional position (existing behaviour)
2. Snap on (1/16 default) — new notes snap to nearest sixteenth
3. Drag-move a note — startBeat snaps while dragging
4. Drag-resize right — duration ends on snapped boundary
5. Drag-resize left — startBeat and duration are both snapped
6. Each of the 10 division values snaps correctly
7. Grid lines appear when snap is on and disappear when off

No automated test infrastructure exists yet. Manual verification is sufficient
for this change.

## Future work (not in scope)

- Quantize selected notes to the current grid
- Snap to timeline grid from the piano roll
- Key commands for snap toggle / cycle resolution
