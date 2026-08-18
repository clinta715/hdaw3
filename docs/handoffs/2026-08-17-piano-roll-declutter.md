# Handoff: Piano Roll De-clutter + Per-Tab Bottom Panel Height

**Project root:** `D:\pdf\roo projects\hdaw3`
**Current version:** 0.23.1 (in sync in `CMakeLists.txt` + `frontend/package.json`)
**Scope:** Frontend only — no engine/RPC/MCP surface changed.

---

## What is complete (this session)

### Motivation
At the default 200px bottom panel, the piano roll's note grid got ~79px (~9 note
rows); ~60% of the editor was chrome: an unbounded clip-button strip, a
velocity lane always open, a CC-adder row always rendered (with CC1 always
open), and four inline sliders whenever notes were selected.

### Changes

1. **Velocity lane collapsed by default** (`PianoRoll.tsx/.css`)
   - 5px `.pr-lane-handle` strip in its place; click to open/close.
   - "Vel" pill in the toolbar (`aria-pressed`), same toggle.

2. **CC lanes on demand** (`PianoRoll.tsx/.css`, `CCLane.tsx/.css`)
   - `ccLanes` default `[]` (was `[1]`); `.pr-cc-row` renders only when lanes
     exist; the old always-visible `pr-cc-add` block is gone.
   - "+CC" pill opens a popover (CC-number select, default 7 + Add button).
   - Each `CCLane` header collapses to ~16px (`cc-lane--collapsed`); `×` removes
     (stopPropagation so it doesn't toggle collapse).

3. **Clip strip → dropdown** — one `.pr-clip-select` native select replaces N
   `.pr-clip-btn` buttons. Timeline selection still takes precedence over the
   dropdown's internal selection (`timelineSelectedId ?? internalClipId`).

4. **Edit popover** — Vel/Dur/Q.Str/Swing sliders moved from inline toolbar into
   an "Edit" popover that only appears when notes are selected.
   - CRITICAL: `quantizeStrength`/`swing` state stays in `PianoRoll` and keeps
     flowing to `NoteGrid` regardless of popover visibility (they affect note
     placement, not just the controls).
   - Popovers close on outside-click, Escape, clip switch, selection→0.
     Outside-click uses refs + functional setState (no stale closures).

5. **Per-tab bottom panel height** (`uiStore.ts`, `App.tsx`)
   - `bottomPanelHeights` record persisted at `hdaw_bottom_panel_h_per_tab`
     (JSON; loader filters to `BOTTOM_TAB_IDS` keys with finite int ≥ min).
   - Precedence: per-tab override → `TAB_DEFAULT_HEIGHTS` (piano-roll: 300) →
     legacy global `bottomPanelHeight` (200, key/function unchanged).
   - Divider drag (`startPanelResize`) reads tab + start height from
     `useUiStore.getState()` inside the handler and saves per active tab.

Net effect: ~150px of note grid at the old default height, ~240px+ at the
piano-roll default — roughly 3× the visible note rows.

### Files
- `frontend/src/components/PianoRoll.tsx` / `.css`
- `frontend/src/components/CCLane.tsx` / `.css`
- `frontend/src/store/uiStore.ts` / `uiStore.test.ts`
- `frontend/src/App.tsx`
- `frontend/e2e/piano-roll.spec.ts`
- (separate commit) `frontend/electron/main.ts` — `intentionalQuit` flag from a
  prior session: engine exit after deliberate kill no longer shows the crash
  dialog.

### Verified (all evidence this session)
- `npm run build` ✓ (zero TS errors)
- Vitest 349/349 (38 files; 5 new uiStore tests: set+persist, precedence,
  invalid tab rejected, clamp, junk-entry filtering at load)
- Playwright 31/31: `piano-roll.spec.ts` (rewritten velocity/CC tests + new
  clip-picker, edit-popover, decluttered-default journeys), `bottom-tabs.spec.ts`,
  `app.spec.ts`

---

## Remaining / follow-ups

1. **Keys column** still fixed 52px — a collapse-to-20px toggle would buy
   horizontal space (idea #6 from the audit, not implemented).
2. **Default-height survey** — only piano-roll has a `TAB_DEFAULT_HEIGHTS`
   entry; consider 240–280 for automation/modulation tabs if they feel cramped.
3. **`.pr-slider` "Vel/Dur" scale sliders** still N-loop `setNoteVelocity`
   calls inside a begin/endTransaction batch (pre-existing). If it ever hurts,
   a batch `project.setNotesVelocity` RPC would be the fix (engine change →
   gtest + MCP parity).
4. Electron `main.ts` intentional-quit fix shipped, but the packaged app's
   engine (`build/RelWithDebInfo`) was NOT rebuilt — next package run must
   include it (stale-`.asar`/stale-engine trap, AGENTS.md).
