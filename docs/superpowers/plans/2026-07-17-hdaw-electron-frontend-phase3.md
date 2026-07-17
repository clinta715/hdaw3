# HDAW Electron/React Frontend — Phase 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans.

**Goal:** Add drag-to-arrange for timeline clips and piano roll notes. Clip drag handles X (beat) and Y (track) repositioning. Note drag handles pitch (Y) and start time (X). Both commit on mouseup via RPC.

**Architecture:** Mouse event handlers on the clip/note DOM elements track drag start → move → end. During drag a "ghost" element follows the cursor. On release, the RPC mutation fires and the `notify.treeChanged` notification triggers a state refresh via the Phase 2 push system.

---

### Task 0: Write spec + plan docs

- [ ] **Done** — `docs/superpowers/specs/2026-07-17-hdaw-electron-frontend-phase3-design.md`
- [ ] **Done** — `docs/superpowers/plans/2026-07-17-hdaw-electron-frontend-phase3.md`

---

### Task 1: Timeline clip drag

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TimelineMinimal.css`

**Behaviour:**
1. `mousedown` on `.tl-clip` → set `dragState` with clip info and start mouse coords
2. `mousemove` on `.tl-tracks` container → compute newStart, newTrackIndex. Clamp both to valid range.
3. During drag: original clip goes to 0.3 opacity. A ghost clip div (`.tl-ghost`) renders at the new position with full opacity.
4. `mouseup` or `mouseleave` → call `rpc.call("project.moveClip", { clipId, newTrackIndex, newStart })`, clear drag state.
5. Handle `onMouseLeave` on the container as a cancel (no mutation, just clear drag state).

**Implementation approach:** Use React state with `useState` (not useRef) for the dragState so re-renders happen naturally. The ghost is a clone of the clip div rendered conditionally.

**RPC method:** `project.moveClip` — params: `{ clipId: number, newTrackIndex: number, newStart: number }`

**Verify:**

```bash
cd frontend && npx tsc --noEmit
```

**Commit:**

```bash
git add frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal.css
git commit -m "frontend: add clip drag in timeline (X beat + Y track repositioning)"
```

---

### Task 2: Note drag in piano roll

**Files:**
- Modify: `frontend/src/components/NoteGrid.tsx`
- Modify: `frontend/src/components/NoteGrid.css`

**Behaviour:**
1. `mousedown` on `.ng-note` → set drag state with note info and start coords. Store the initial pitch and startBeat.
2. `mousemove` on `.note-grid` container → compute newPitch (clamped 0–127) and newStart (clamped to ≥0). Update the note's inline style in real time.
3. During drag: original note gets `.ng-note--dragging` class (brighter border, higher z-index).
4. `mouseup` → call `rpc.call("project.setNotePitch", ...)` and `rpc.call("project.setNoteStart", ...)`, then `syncNotes(rpc, clipId)`. Clear drag state.
5. `mouseleave` on container → cancel (no mutation).

**RPC methods:**
- `project.setNotePitch` — params: `{ noteId: number, pitch: number }`
- `project.setNoteStart` — params: `{ noteId: number, startBeat: number }`

**Verify:**

```bash
cd frontend && npx tsc --noEmit
```

**Commit:**

```bash
git add frontend/src/components/NoteGrid.tsx frontend/src/components/NoteGrid.css
git commit -m "frontend: add note drag in piano roll (pitch + start beat)"
```

---

### Task 3: Build-check

- [ ] `cd frontend && npx tsc --noEmit && npx vite build`
