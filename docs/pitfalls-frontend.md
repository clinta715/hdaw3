# HDAW Frontend Pitfalls

Domain-specific documentation split from AGENTS.md.
For the original combined file, see `../AGENTS.md`.

Sections: Stale Closures, Optimistic Placement, Drag Behavior,
Window Listeners, Store vs Prop Reads.

## 1. Stale `clips` closure after async mutations

Hooks receive `clips` as a prop (from the component's render). After an
async operation (duplicate, syncSnapshot, etc.), the store has newer
data than the prop. Any code that looks up clips by ID **after** an
async op must read from `useProjectStore.getState().snapshot?.clips`,
NOT from the `clips` prop.

**The original ctrl-drag bug (recurring):**

`handleMouseUp` in `useTimelineDrag.ts` used `clips.find(id)` on the
stale prop to look up clip positions for the RPC move call. After
ctrl-drag duplicated clips via RPC, `dragSelectedIdsRef.current` held
the NEW clip IDs, but those clips didn't exist in the stale `clips`
array. So:

1. Ctrl+drag → duplicates clip via RPC (new clip at original position)
2. Drag continues → optimistic placement moves the new clip in the store
3. Mouse up → `handleMouseUp` tries `clips.find(newId)` → **NOT FOUND**
4. RPC move call is silently skipped
5. `syncSnapshot` replaces store → clip jumps back to original position

**Fix:** Read from the current store state:

```typescript
// BAD — stale closure, clips may not contain duplicated clips
const clip = clips.find(c => c.clipId === id);

// GOOD — reads current state from the store
const currentSnapshot = useProjectStore.getState().snapshot;
const clip = currentSnapshot?.clips.find(c => c.clipId === id)
           ?? clips.find(c => c.clipId === id);
```

This pattern applies to ANY hook that receives clips/tracks as props
and performs async mutations. The rule: **after an await, always read
from the store, never from the closure prop.**

## 2. Optimistic placement + `syncSnapshot` conflict

If you optimistically update the store AND then call `syncSnapshot`,
the sync replaces the entire snapshot. If the RPC didn't actually make
the change on the backend (e.g., it was skipped due to pitfall #1),
the optimistic update is lost.

**Symptom:** Clips appear to move to the correct position, then
"jump back" to their original position after a brief delay.

**Fix:** Always verify the RPC path runs correctly before calling
`syncSnapshot`. If the RPC was skipped (e.g., clip not found), either:
- Don't call `syncSnapshot` (keep the optimistic state), OR
- Fix the RPC path so it actually runs (see pitfall #1)

**Affected operations:**
- Ctrl-drag (duplicate) — the duplicated clips weren't found in the
  stale closure, so the move RPC was skipped
- Alt-drag (paint repeat) — same pattern
- Any operation that creates new clips and then tries to move them

## 3. Optimistic placement during continuous drag is wrong

Creating optimistic clips at the initial mouse position during a drag
causes double-movement: the clip is placed at position X, then the drag
applies an offset relative to the original, moving it again.

**Symptom:** Duplicated/painted clips appear at the wrong position or
jump erratically during the drag.

**Why it fails:**
1. Optimistic clip created at mouse position X (e.g., beat 4.0)
2. User continues dragging to position Y (e.g., beat 8.0)
3. `handleMouseUp` calculates `deltaStart = 8.0 - 0.0 = 8.0`
4. Applies delta to the optimistic clip: `4.0 + 8.0 = 12.0` — wrong!

**Fix for drag operations where position changes continuously:**
- Don't use optimistic placement during the drag (let RPC complete first)
- Only use optimistic placement on `mouseup` (like normal drag does)
- The drag preview (`dragPreviewStyle`) provides visual feedback instead

**Operations where optimistic placement IS safe:**
- Note creation, deletion, transpose, quantize, humanize, paste
  (these are discrete operations, not continuous drags)
- FX slot add, remove, bypass, reorder
  (these are discrete operations with rollback on error)

## 4. `dragSelectedIdsRef` changes identity during async ops

When ctrl-drag duplicates clips, `dragSelectedIdsRef.current` is
replaced with new clip IDs. But the `clips` array in the closure still
references the old clips. Any code that iterates over
`dragSelectedIdsRef.current` after the async duplicate must look up
clips from the store, not from the closure prop.

**Pattern:**
```typescript
// BAD — ids may reference clips not in the stale closure
const ids = dragSelectedIdsRef.current;
for (const id of ids) {
  const clip = clips.find(c => c.clipId === id); // NOT FOUND
}

// GOOD — read from the store after async operations
const ids = dragSelectedIdsRef.current;
const currentClips = useProjectStore.getState().snapshot?.clips ?? clips;
for (const id of ids) {
  const clip = currentClips.find(c => c.clipId === id);
}
```

## 5. Window-level listeners and stale closures

The drag hook registers `mousemove`/`mouseup` on `window` via refs
(`handleMouseMoveRef`/`handleMouseUpRef`) to avoid stale closures.
But the refs point to `useCallback` functions that may still close
over stale `clips`. The fix from pitfall #1 applies here too: read
from the store inside the callback, not from the closure prop.

**Why the ref pattern doesn't fully solve it:**
- The refs avoid re-registering listeners on every render (good)
- But the `useCallback` functions still capture `clips` from the render
  where they were created (bad)
- After an async operation, the captured `clips` is stale

**Fix:** Inside the callback, always read from the store for any
state that may have been mutated by an async operation:

```typescript
const handleMouseUp = useCallback(() => {
  const d = dragRef.current;
  if (!d) return;
  // ...
  const ids = dragSelectedIdsRef.current;

  // Read from store, not from closure
  const currentSnapshot = useProjectStore.getState().snapshot;
  const currentClips = currentSnapshot?.clips ?? clips;

  (async () => {
    for (const id of ids) {
      const clip = currentClips.find(c => c.clipId === id);
      if (!clip) continue;
      // ... RPC call
    }
  })();
}, [pps, trackCount, clips, updateDrag, tracksRef, rpc, TRACK_HEIGHT]);
```

Note: `clips` is still in the dependency array (for initial render),
but the actual lookup uses the store state, which is always current.

## 6. Use batch RPCs for multi-clip operations

When operating on multiple clips (ctrl-drag duplicate, multi-select move,
clipboard paste, batch delete), use the batch RPCs instead of looping
individual RPCs. Batch RPCs:
- Execute in a single transaction (one undo step)
- Avoid stale-closure bugs (no per-iteration `clips.find()` on stale data)
- Reduce round trips (one call instead of N)
- Are atomic (all succeed or all fail)

**Available batch RPCs:**
- `project.duplicateClips({ clipIds, newStarts, newTrackIndices })` → `number[]`
- `project.moveClips({ clipIds, newStarts, newTrackIndices })` → `null`
- `project.removeClips({ clipIds })` → `null`
- `project.addClips({ trackIndex, starts, durations, names })` → `number[]`
- `project.paintClips({ sourceClipIds, originBeat, spacing, targetTrackIndex, count })` → `number[]`

**Pattern:**
```typescript
// BAD — loop of individual RPCs, stale closure, N round trips
for (const id of ids) {
  const clip = clips.find(c => c.clipId === id); // stale closure!
  await rpc.call("project.duplicateClipTo", { clipId: id, ... });
}
await rpc.call("project.endTransaction");

// GOOD — single batch RPC, no stale closure, 1 round trip
const clipIds = [...dragSelectedIdsRef.current];
const currentClips = useProjectStore.getState().snapshot?.clips ?? clips;
const newStarts = clipIds.map(id => {
  const clip = currentClips.find(c => c.clipId === id);
  return clip ? clip.startBeat + delta : 0;
});
await rpc.call("project.duplicateClips", { clipIds, newStarts, newTrackIndices });
```

## Audit Checklist

When reviewing drag/drop or async UI code, check:

- [ ] After any `await`, reads from `useProjectStore.getState()` not props
- [ ] `syncSnapshot` is only called after verifying the RPC ran
- [ ] Optimistic placement is NOT used during continuous drag
- [ ] `dragSelectedIdsRef.current` lookups use store state, not closure
- [ ] Window-level listener callbacks read from store, not closure
- [ ] Error rollback exists for optimistic operations
- [ ] Multi-clip operations use batch RPCs, not loops of individual RPCs
