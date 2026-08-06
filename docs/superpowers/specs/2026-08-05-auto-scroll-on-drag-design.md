# Auto-Scroll on Drag

**Date:** 2026-08-05
**Status:** Approved

## Problem

When dragging clips in the timeline or notes in the piano roll, moving the cursor off-screen stops updating the object's position. The user must drop, scroll, and re-grab — breaking flow. Standard DAWs auto-scroll the view when the cursor approaches or crosses the container edge during a drag.

## Scope

**In scope:**
- Timeline clip drag (horizontal + vertical scroll in `.tl-tracks`)
- Piano roll note drag and note resize (horizontal + vertical scroll in `.note-grid`)

**Out of scope (future):**
- Clip trim drag (`useTimelineTrim`)
- Loop region handles (`useTimelineLoopDrag`)
- Automation point drag (`AutomationLaneCanvas`)
- Fade drag (`useTimelineFade`)

## Design

### Shared hook: `useAutoScroll`

**File:** `frontend/src/hooks/useAutoScroll.ts`

```typescript
function useAutoScroll(containerRef: RefObject<HTMLElement>): {
  update: (clientX: number, clientY: number) => void;
  stop: () => void;
}
```

**Behavior:**
- `update(clientX, clientY)` — call on every `mousemove` during a drag
- `stop()` — call on `mouseup` / drag end
- Internally uses `requestAnimationFrame` loop
- RAF loop starts on first edge-zone entry, stops when cursor leaves zone or `stop()` is called
- Both axes are independent — horizontal and vertical can scroll simultaneously
- Scroll clamped to `[0, scrollWidth - clientWidth]` and `[0, scrollHeight - clientHeight]`

### Edge zone and speed

- **Edge zone:** 40px from each edge of the container's bounding rect
- **Speed:** linear acceleration from `MIN_SPEED` (2 px/frame) at the edge to `MAX_SPEED` (20 px/frame) at 40px past the edge

```
distancePastEdge = clamp(edgeZone - distanceFromEdge, 0, edgeZone)
speed = MIN_SPEED + (MAX_SPEED - MIN_SPEED) * (distancePastEdge / edgeZone)
```

- Positive speed for right/bottom edges (scroll forward), negative for left/top edges (scroll backward)
- If cursor is inside the container but not in any edge zone → speed = 0, no scroll

### Integration: Timeline clip drag (`useTimelineDrag.ts`)

- The hook receives `tracksRef` from the Timeline component
- On each `mousemove` in `handleMouseMove`, call `autoScroll.update(e.clientX, e.clientY)`
- On `mouseup` in `handleMouseUp`, call `autoScroll.stop()`
- The existing `mouseBeat` calculation already reads `el.scrollLeft`:
  ```typescript
  const mouseBeat = Math.max(0, (dragState.mouseX - cr.left + el.scrollLeft) / pps);
  ```
  As the container auto-scrolls, the next `mousemove` naturally picks up the new scroll position. No additional wiring needed.

### Integration: Piano roll note drag (`NoteGrid.tsx`)

- The hook receives `gridRef` from NoteGrid
- On each `mousemove` in the note drag handler, call `autoScroll.update(e.clientX, e.clientY)`
- On `mouseup`, call `autoScroll.stop()`
- Existing position calculations read `scrollTop`/`scrollLeft`:
  ```typescript
  const x = e.clientX - gridRect.left + gridRef.current.scrollLeft;
  const y = e.clientY - gridRect.top + gridRef.current.scrollTop;
  ```
  Auto-scroll updates scroll → next mousemove picks up new offset.

### Edge case: cursor held still during auto-scroll

When the user holds the mouse still near an edge, the container scrolls but no `mousemove` fires. The visual cursor position relative to content changes, but the committed drag position doesn't update until the next mouse movement. This is standard DAW behavior — the user sees the view scroll and can then move the mouse to place the object.

## Files

| File | Action |
|------|--------|
| `frontend/src/hooks/useAutoScroll.ts` | Create — new hook |
| `frontend/src/hooks/__tests__/useAutoScroll.test.ts` | Create — unit tests |
| `frontend/src/hooks/useTimelineDrag.ts` | Modify — integrate hook |
| `frontend/src/components/NoteGrid.tsx` | Modify — integrate hook |

## Testing

### Unit tests (`useAutoScroll.test.ts`)

1. **Edge-zone detection:** cursor at various positions → verify speed calculation
2. **Speed formula:** cursor at edge, 20px past, 40px past → correct speeds
3. **Scroll clamping:** auto-scroll stops at min/max scroll bounds
4. **RAF lifecycle:** `update` starts loop, `stop` cancels it, no dangling frames
5. **Both axes:** cursor in corner → both axes scroll

### Manual verification

1. Drag a clip to the right edge of the timeline → view scrolls right
2. Drag a clip to the left edge → view scrolls left
3. Drag a clip to the bottom edge → view scrolls down (if many tracks)
4. Drag a note to the right edge in piano roll → view scrolls right
5. Drag a note up/down past the piano roll edges → view scrolls vertically
6. Release mouse → scrolling stops immediately
7. Drag clip near edge, hold still → view scrolls, clip follows on next mouse move
