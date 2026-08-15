# Phase 8: Interactive Automation Point Editing

**Date**: 2026-07-18
**Status**: Draft
**Version**: 0.9.2 candidate

## Overview

Phase 8 adds interactive point editing to the automation lane canvas introduced in Phase 7. Users can click to add points, drag points to change value or time, right-click to delete, and use keyboard modifiers for multi-select. The canvas transitions from a read-only preview to a full editing surface.

## Interaction Model

| Action | Gesture | RPC Call |
|--------|---------|----------|
| Add point | Click empty area on canvas (no modifier held) | `project.addAutomationPoint(trackIndex, lane, time, value)` |
| Select point | Click point (single-select, deselects others) | none (local state) |
| Toggle selection | Ctrl+click point | none |
| Range select | Shift+click point | none |
| Move point vertically | Drag point up/down | `setAutomationPointValue(trackIndex, lane, time, newValue)` on release |
| Move point horizontally | Drag point left/right | `removeAutomationPoint(oldTime)` + `addAutomationPoint(newTime, value)` on release |
| Move multiple selected points | Drag any selected point (all move same delta) | batch per point on release |
| Delete point(s) | Right-click a single point → "Delete" or Delete key with selection | `removeAutomationPoint(trackIndex, lane, time)` per point |
| Deselect all | Ctrl+click or Shift+click empty area | none |
| Select all points in lane | Ctrl+A | none (local state) |

### Canvas-to-beat/value mapping

- **Time**: `beat = (mouseX / canvasWidth) * viewRange` where `viewRange = viewEndBeat - viewStartBeat` (default 32 beats)
- **Value**: `value = clamp(1.0 - mouseY / canvasHeight, 0.0, 1.0)`

### Drag behavior

During a drag, the canvas renders a **preview** point at the current mouse position. No RPC calls fire until mouse release. On release:
1. For vertical-only movement: call `setAutomationPointValue()` with the new value at the original time
2. For movement that also changed time: call `removeAutomationPoint(oldTime)` then `addAutomationPoint(newTime, value)`
3. For multi-selected points: each point's delta is calculated relative to its original position, then batch-committed

After the RPC call, the `treeChanged` notification from the backend triggers `automationStore.fetchForTrack()` to refresh all points from the server.

Right-clicking empty canvas area is ignored (browser default context menu is suppressed via `preventDefault`).

### Hit testing

A point is "hit" if the mouse is within 6px (canvas pixels, not CSS pixels) of the point's rendered position. The first hit wins (topmost/last-drawn point has priority; since points are sorted by time, the rightmost point in a collision wins).

## State Changes

### automationStore additions

```typescript
selectedPointTimes: Map<string, Set<number>>;  // laneName → sorted set of selected times
```

Selection is keyed by lane name so switching tracks or lanes doesn't lose state for previously-viewed lanes. Each value is a `Set<number>` of point times (doubles).

**Store actions:**

| Action | Purpose |
|--------|---------|
| `selectPoint(laneName, time, shift, ctrl)` | Update selection for lane; shift=range-select, ctrl=toggle |
| `selectAll(laneName)` | Select all points in lane |
| `clearSelection(laneName?)` | Clear selection for lane (or all if no lane given) |
| `addPoint(trackIndex, laneName, time, value, rpc)` | RPC call then refresh |
| `removePoints(trackIndex, laneName, times, rpc)` | Batch remove then refresh |
| `movePoint(trackIndex, laneName, oldTime, newTime, newValue, rpc)` | Remove+add then refresh |

## Component Changes

### AutomationLaneCanvas.tsx — major rewrite

Convert from static canvas to interactive editing surface:

**Props — unchanged API** (same as Phase 7):
- `laneName`, `points`, `trackIndex`, `rpc`, `viewStartBeat`, `viewEndBeat`, `paramID`, `color?`

**New internal state:**
- `dragState: { pointTime: number; startX: number; startY: number; originalTime: number; originalValue: number } | null`
- `previewPos: { time: number; value: number } | null`
- `hoveredPoint: number | null` (time of point under cursor)

**Mouse handlers:**
- `onMouseDown(e)`:
  - Hit-test existing points. If hit → start drag on that point. If no Shift/Ctrl, deselect others. If point isn't already selected and not using modifier, select it (single-select).
  - If miss → if not holding Shift/Ctrl, check if click is near an existing point (within 6px). If not, add new point at click position (RPC call immediately).
- `onMouseMove(e)`:
  - If dragging → update `previewPos` from mouse position
  - If not dragging → update `hoveredPoint` for cursor feedback
- `onMouseUp(e)`:
  - If dragging → commit the move via RPC (setAutomationPointValue or remove+add), then clear dragState
- `onContextMenu(e)`:
  - If right-clicking on a point → show context menu with Delete option, or just delete immediately

**Rendering additions:**
- Selected points: white ring (2px stroke) around the point circle, brighter fill
- Drag preview: dashed line from original position to current preview, semi-transparent circle at preview position
- Hovered point: slightly larger circle, cursor changes to `grab`
- All points get a larger hit target (6px radius vs 3px visual radius)

### AutomationPanel.tsx — keyboard handler

Add `tabIndex={0}` and `onKeyDown` to the root `.automation-panel` div:

| Key | Action |
|-----|--------|
| Delete / Backspace | Remove all selected points in the active lane |
| Ctrl+A | Select all points in the active lane |
| Escape | Clear selection for active lane |

Attach the handler via `useEffect` with a `keydown` listener on the panel element.

### automationStore.ts — new actions

Implement action methods:

- **selectPoint(laneName, time, shift, ctrl)**: Clone the map, get the set for the lane. If ctrl: toggle the time in the set. If shift: select range from nearest selected point to this time. Else: replace with `new Set([time])`. Update state.

- **addPoint(trackIndex, laneName, time, value, rpc)**: Call `rpc.call("project.addAutomationPoint", {trackIndex, lane: laneName, time, value})`. Then call `fetchForTrack(trackIndex, rpc)` to refresh.

- **removePoints(trackIndex, laneName, times, rpc)**: For each time, call `rpc.call("project.removeAutomationPoint", {trackIndex, lane: laneName, time})`. Then refresh.

- **movePoint(trackIndex, laneName, oldTime, newTime, newValue, rpc)**: If `oldTime !== newTime`: call `removeAutomationPoint` then `addAutomationPoint`. Else: call `setAutomationPointValue`. Then refresh.

## Files to Modify

| File | Change |
|------|--------|
| `frontend/src/store/automationStore.ts` | Add selection state + new actions |
| `frontend/src/components/AutomationLaneCanvas.tsx` | Full interaction rewrite |
| `frontend/src/components/AutomationLaneCanvas.css` | Cursor styles, selection highlight |
| `frontend/src/components/AutomationPanel.tsx` | Keyboard handler + Delete key |

## Out of Scope

- Drag-to-create a new point (click-to-add only; drag-to-create would require distinguishing click vs drag-start, which is a future enhancement)
- Rubber-band multi-select (only Ctrl/Shift modifiers; rectangular selection is deferred)
- Snap-to-grid for points (deferred to a future phase)
- Point time/value labels on hover (tooltip, deferred)
- Touch support (desktop mouse only)
- Undo history in the frontend (backend undo via Ctrl+Z in the C++ UI is unaffected)

## Backend Dependencies

No new backend RPC endpoints needed. The existing four automation mutation calls (`addAutomationPoint`, `removeAutomationPoint`, `setAutomationPointValue`, `setAutomationEnabled`) are sufficient. The `treeChanged` notification already triggers a frontend store refresh via the Phase 7 wiring in `main.tsx`.
