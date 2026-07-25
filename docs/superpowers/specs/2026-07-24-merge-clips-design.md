# Merge Clips — Design Spec

**Date:** 2026-07-24
**Status:** Approved
**Scope:** Merge multiple selected MIDI clips into a single clip in the arrange window.

## Requirements

- Select ≥2 MIDI clips on the same track, trigger merge (context menu or Ctrl+M).
- Merged clip spans the full range of all input clips (gaps become silence).
- All notes from input clips are copied into the merged clip with correct beat offsets.
- Original clips are deleted.
- Operation is atomic and undoable (one undo step).
- Same-track only — reject if clips span multiple tracks.
- Audio clips and ghost clips are excluded from merge.

## Approach

Single backend RPC (`project.mergeClips`). Frontend triggers it via context menu + Ctrl+M. Backend does all work (validate, create, copy notes, delete originals) in one undo transaction. Frontend reconciles via delta sync.

## Section 1: Backend RPC

**Command:** `project.mergeClips({ clipIds: number[] })` → `number` (new clip ID)

**Algorithm (in one undo transaction):**

1. **Validate:**
   - `clipIds` has ≥2 entries → reject `"need at least 2 clips to merge"` if not.
   - All clips exist → reject `"clip not found"` if any ID is invalid.
   - All clips are MIDI (`clipType == "midi"`) → reject `"can only merge MIDI clips"` if any is audio.
   - All clips are on the same track → reject `"clips must be on the same track"` if they span tracks.

2. **Compute range:**
   - `newStart = min(clip.startBeat)` across all input clips.
   - `newEnd = max(clip.startBeat + clip.durationBeats)` across all input clips.
   - `newDuration = newEnd - newStart`.

3. **Create merged clip:**
   - `ProjectModel::createMidiClipEmpty("Merged", newStart, newDuration)` on the same track.
   - Uses default clip properties (gain=1, fadeIn=0, fadeOut=0, looping=false). Not copied from source clips — the merged clip is a new entity.
   - Add to the track's `CLIP_LIST` via the undo manager.

4. **Copy notes:**
   - For each source clip, iterate its `MIDI_NOTE_LIST`.
   - For each note, create a new note in the merged clip's `MIDI_NOTE_LIST`:
     - `noteID` = freshly allocated (via `ProjectModel::allocateNoteID()`).
     - `noteNumber` = same as source note.
     - `velocity` = same as source note.
     - `startBeat` = `note.startBeat + (sourceClip.startBeat - newStart)`.
     - `durationBeats` = same as source note.
   - This preserves the relative position of every note within the merged timeline.

5. **Remove originals:**
   - For each source clip, remove from its parent `CLIP_LIST` via the undo manager.

6. **Return** the new clip ID.

**Implementation files:**
- `src/engine/AudioEngineCommands_Clips.cpp` — new `mergeClips` method.
- `src/engine/AudioEngineCommands.h` — declaration.
- `src/common/ProjectCommands.h` — virtual interface.
- `src/frontend/FrontendRouter.cpp` — RPC dispatch.

## Section 2: Frontend trigger

**Context menu (`TimelineContextMenu.tsx`):**
- New "Merge Clips" button in the clip context menu.
- Visible only when ≥2 clips are selected AND all selected clips are MIDI (`isMidi === true`) and not ghosts (`isGhost === false`).
- Calls `rpc.call("project.mergeClips", { clipIds: [...selectedClipIds] })`.
- Waits for the RPC response (returns the new clip ID), then sets selection: `useUiStore.setState({ selectedClipIds: new Set([newClipId]) })`.
- The delta sync reconciles the snapshot (new clip + removed originals) independently.

**Keyboard shortcut (`TimelineMinimal.tsx`):**
- Ctrl+M handler alongside existing Ctrl+D / Delete / Ctrl+C/V/X.
- Same precondition guard (≥2 MIDI clips, no ghosts).
- Calls the same RPC, same post-merge selection update.

**No optimistic placeholder** — discrete operation, not a drag. User sees the change in one delta broadcast.

## Section 3: Validation & error handling

**Backend guards:**
| Condition | Error message |
|---|---|
| <2 clip IDs | `"need at least 2 clips to merge"` |
| Any clip not found | `"clip not found"` |
| Any clip is audio | `"can only merge MIDI clips"` |
| Clips span multiple tracks | `"clips must be on the same track"` |

**Frontend guard (both context menu + keyboard):**
- Button/shortcut enabled only when: `selectedClipIds.size >= 2` AND every selected clip in `snapshot.clips` has `isMidi === true` AND `isGhost === false`.
- If the guard fails, the button is hidden and the keyboard shortcut is a no-op.

**Edge cases:**
- **Overlapping notes:** both copies are kept (same pitch/time → two notes in the merged clip). Same behavior as the piano roll with overlapping notes today.
- **Ghost clips:** rejected by the frontend guard. Merging ghosts would break their source relationship.

## Section 4: Testing

**C++ gtest (`mergeClips_test.cpp`):**
- Merge 2 contiguous clips → new clip spans combined range, note count = sum, originals removed.
- Merge clips with a gap → merged clip spans full range, notes from both present with correct offsets.
- Reject: <2 clips, mixed tracks, non-MIDI clip.
- Undo → originals restored, merged clip removed.
- Return value: new clip ID > 0.

**E2E Playwright (`editing.spec.ts`):**
- Create 2 MIDI clips on same track, select both (click + ctrl-click), Ctrl+M → assert clip count drops to 1, merged clip visible in snapshot with correct `startBeat`/`durationBeats`.
- Undo → originals restored.

**Frontend Vitest:**
- Merge guard logic (≥2 MIDI, no ghosts) tested via store/component test if the guard is extracted. Otherwise covered by E2E.
