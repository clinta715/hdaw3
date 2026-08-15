# HDAW Electron/React Frontend — Phase 3 Design

## Status

Approved. Adds drag-to-arrange for clips in the timeline and notes in the piano roll.

## Problem

Phase 1 renders clips and notes as static rectangles — the user cannot
reposition them via drag-and-drop. The engine already supports
`project.moveClip`, `project.setNotePitch`, `project.setNoteStart`,
etc.; Phase 3 adds the mouse-event plumbing.

## Scope

| Feature | Current | After Phase 3 |
|---------|---------|---------------|
| Timeline clips | Static rectangles | Drag to reposition (X=beat, Y=track), ghost clip follows mouse, commits on drop |
| Piano roll notes | Static rectangles (DOM divs) | Drag to change pitch (Y) and start time (X), visual update during drag, commits on release |
| Mixer faders | Already draggable via `<input type="range">` | No change needed |

## Timeline Clip Drag

**Interaction:**
1. `mousedown` on `.tl-clip` → record `dragStart = { clipId, trackIndex, startBeat, mouseX, mouseY }`
2. `mousemove` on `.tl-tracks` (or document) → compute:
   - `newStart = max(0, dragStart.startBeat + (mouseX - dragStart.mouseX) / PIXELS_PER_BEAT)`
   - `newTrackIndex = clamp(floor(mouseY / TRACK_HEIGHT), 0, tracks.length - 1)`
   - Render a ghost clip at (newStart, newTrackIndex)
   - Also show the original clip faded (or hide it)
3. `mouseup` → call `rpc.call("project.moveClip", { clipId, newTrackIndex, newStart })`

**Visual:**
- Original clip opacity drops to 0.3 during drag
- "Ghost" clone follows the mouse with full opacity
- Track highlight on hover (subtle border change on the target track row)

## Note Drag (Piano Roll)

**Interaction:**
1. `mousedown` on `.ng-note` → record `dragStart = { noteId, pitch, startBeat, mouseX, mouseY }`
2. `mousemove` on `.note-grid` → compute:
   - `newPitch = clamp(pitch - round((mouseY - dragStart.mouseY) / KEY_HEIGHT), 0, 127)`
   - `newStart = max(0, startBeat + (mouseX - dragStart.mouseX) / PIXELS_PER_BEAT)`
   - Redraw note at new position in-memory (inline style update)
3. `mouseup` → call `rpc.call("project.setNotePitch", { noteId, pitch: newPitch })` and `rpc.call("project.setNoteStart", { noteId, startBeat: newStart })` — followed by `syncNotes(rpc, clipId)` to refresh

**Edge cases:**
- Drag out of bounds → clamp to valid range, don't lose the note
- Very rapid drag → OK, mouseup still fires
- Multiple notes selected → Phase 3 only handles single-note drag (multi-select deferred)

## Files Changed

| File | Change |
|------|--------|
| `frontend/src/components/TimelineMinimal.tsx` | Add drag state, mouse handlers, ghost clip rendering, RPC call on drop |
| `frontend/src/components/TimelineMinimal.css` | Ghost clip style, dragging cursor, track hover highlight |
| `frontend/src/components/NoteGrid.tsx` | Add drag state, mouse handlers, note follow during drag, RPC call on release |
| `frontend/src/components/NoteGrid.css` | Dragging cursor, note-drag visual state |
