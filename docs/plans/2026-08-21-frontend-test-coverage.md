# Frontend Test Coverage — All Untested UI Components

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Vitest unit/component tests for all 14 untested frontend components, bringing frontend test coverage from 71% to ~100%.

**Architecture:** Each component gets a dedicated `.test.tsx` file co-located with the source. Tests follow established patterns: Zustand store seeding via `setState`, RPC module mocking via `vi.mock`, `@testing-library/react` for rendering, and `fireEvent` for interactions. No new test utilities or setup changes required.

**Tech Stack:** Vitest 4.x, @testing-library/react 16.x, @testing-library/jest-dom 7.x, jsdom, Zustand (direct `setState` seeding).

---

## Testing Conventions (Reference)

All tests follow these patterns — do not deviate:

- **Store seeding:** `useProjectStore.setState({...})` in `beforeEach` — no Provider wrappers needed.
- **RPC mocking:** `vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }))` at module level, `mockReset()` + `mockResolvedValue(null)` in `beforeEach`.
- **Factory functions:** `mkTrack(overrides)` style helpers for reusable fixtures.
- **Rendering:** `render(<Component />)` from `@testing-library/react`, no wrapper needed.
- **Cleanup:** `afterEach(() => cleanup())`.
- **DOM assertions:** `container.querySelector(...)` for class-based checks, `screen.getByText(...)` for text.
- **No `waitFor`** — synchronous store updates + `rerender()` pattern.

---

## Task 1: Mixer

**Files:** Create `frontend/src/components/Mixer.test.tsx`

Tests: empty state ("No tracks"), "+ Add Track" button calls RPC, renders MixerStrip per track, master strip present, correct strip count, master class applied, meter store wiring, empty list crash-free, multiple tracks crash-free.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/Mixer.test.tsx`
- [ ] Commit

---

## Task 2: NoteGrid

**Files:** Create `frontend/src/components/NoteGrid/NoteGrid.test.tsx`

Tests: empty state "No notes", renders note rectangles, .ng-note class, grid click clears selection, note click calls onSelectionChange, selected notes get .ng-note--selected, Ctrl+wheel zooms, context menu on right-click, context menu actions (Quantize/Humanize/Transpose/Delete), empty crash-free, notes crash-free, velocity-based opacity.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/NoteGrid/NoteGrid.test.tsx`
- [ ] Commit

---

## Task 3: AudioClipEditor

**Files:** Create `frontend/src/components/AudioClipEditor.test.tsx`

Tests: renders without crash, displays clip name, gain slider exists, gain slider range, gain change fires setClipGain, fade-in/out controls, gainToDb/dbToGain pure functions, file-missing banner, stretch mode controls, no-selection crash-free, multi-selection crash-free, loop toggle exists.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/AudioClipEditor.test.tsx`
- [ ] Commit

---

## Task 4: ExportDialog

**Files:** Create `frontend/src/components/ExportDialog.test.tsx`

Tests: "Export Audio" title, format select (WAV/AIFF/FLAC), bit depth select (16/24/32), sample rate select (44100/48000/96000), range select (Full/Loop/Selection/Markers), format change updates filename, default path "export.wav", export disabled when empty path, export calls export.audio RPC, Close calls onClose, prefs persist to localStorage, prefs restore from localStorage, renders crash-free.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/ExportDialog.test.tsx`
- [ ] Commit

---

## Task 5: AutomationLaneCanvas

**Files:** Create `frontend/src/components/AutomationLaneCanvas.test.tsx`

Tests: renders canvas, correct canvas width, crash-free with empty points, crash-free with points, click adds point, right-click no crash, HiDPI style dimensions, crash-free with single point, crash-free with many points, canvas tag check.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/AutomationLaneCanvas.test.tsx`
- [ ] Commit

---

## Task 6: CCLane

**Files:** Create `frontend/src/components/CCLane.test.tsx`

Tests: renders crash-free, renders canvas, displays CC label (e.g. "CC7"), fetches CC points on mount, crash-free with existing points, remove button when onRemove provided, remove calls onRemove, collapse toggle hides canvas, second click restores canvas, canvas width correct.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/CCLane.test.tsx`
- [ ] Commit

---

## Task 7: VelocityLane

**Files:** Create `frontend/src/components/VelocityLane.test.tsx`

Tests: crash-free empty, crash-free with notes, renders velocity bar per note, height proportional to velocity, selected notes get --selected class, drag calls onVelocityChange, lane width correct, handles same-beat notes, zero-velocity crash-free, lane container present.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/VelocityLane.test.tsx`
- [ ] Commit

---

## Task 8: PreferencesDialog

**Files:** Create `frontend/src/components/PreferencesDialog.test.tsx`

Tests: crash-free, shows Audio section, shows General section, shows MIDI section, shows Plugins section, shows Engine Connection section, WebSocket URL displayed, Close calls onClose, audio driver select present, sample rate select present, buffer size select present, loads prefs via RPC on mount.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/PreferencesDialog.test.tsx`
- [ ] Commit

---

## Task 9: StartupDialog

**Files:** Create `frontend/src/components/StartupDialog.test.tsx`

Tests: crash-free, shows HDAW text, "New Project" button, "Open Project" button, New Project calls RPC, recent projects from localStorage, no-recent-projects crash-free, max 8 recent projects shown.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/StartupDialog.test.tsx`
- [ ] Commit

---

## Task 10: ErrorBoundary

**Files:** Create `frontend/src/components/ErrorBoundary.test.tsx`

Tests: renders children normally, catches child errors, shows error message in fallback, shows Reload button, Reload calls window.location.reload, custom fallback prop rendered.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/ErrorBoundary.test.tsx`
- [ ] Commit

---

## Task 11: LoadingOverlay

**Files:** Create `frontend/src/components/LoadingOverlay.test.tsx`

Tests: renders nothing when not loading, shows spinner when loading, shows progress percentage, shows progress bar when percent between 0-1, hides bar when percent 0, crash-free.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/LoadingOverlay.test.tsx`
- [ ] Commit

---

## Task 12: ArrangerChainEditor

**Files:** Create `frontend/src/components/ArrangerChainEditor.test.tsx`

Tests: crash-free, chain selector/area present, create chain button, create calls addArrangerChain, regions column present, flatten button present, flatten calls flattenArrangerChain, crash-free with existing chains, regions listed, empty chain entries state.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/ArrangerChainEditor.test.tsx`
- [ ] Commit

---

## Task 13: FmAnalysisPanel

**Files:** Create `frontend/src/components/FmAnalysisPanel.test.tsx`

Tests: crash-free, "No active FM synth" empty state, operator meters visible, 6 operator labels, algorithm displayed, voice count displayed, percentage readouts, hot-level CSS class.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/FmAnalysisPanel.test.tsx`
- [ ] Commit

---

## Task 14: TimelineMinimal

**Files:** Create `frontend/src/components/TimelineMinimal/TimelineMinimal.test.tsx`

Tests: crash-free, empty state, ruler present, track lanes with tracks, clip rectangles with clips, zoom controls present, playhead present, multi-track crash-free, toolbar renders, handles empty project.

- [ ] Write test file
- [ ] Run tests: `npx vitest run src/components/TimelineMinimal/TimelineMinimal.test.tsx`
- [ ] Commit

---

## Verification

After all 14 tasks:

1. Run full test suite: `npx vitest run`
2. Verify no regressions in existing tests
3. Check coverage report: `npx vitest run --coverage`
4. Confirm all 14 new test files pass

---

## Summary

| Task | Component | Priority | Tests |
|------|-----------|----------|-------|
| 1 | Mixer | HIGH | 10 |
| 2 | NoteGrid | HIGH | 12 |
| 3 | AudioClipEditor | HIGH | 12 |
| 4 | ExportDialog | HIGH | 14 |
| 5 | AutomationLaneCanvas | MEDIUM | 10 |
| 6 | CCLane | MEDIUM | 10 |
| 7 | VelocityLane | MEDIUM | 10 |
| 8 | PreferencesDialog | MEDIUM | 12 |
| 9 | StartupDialog | LOW | 8 |
| 10 | ErrorBoundary | LOW | 6 |
| 11 | LoadingOverlay | LOW | 6 |
| 12 | ArrangerChainEditor | LOW | 10 |
| 13 | FmAnalysisPanel | LOW | 8 |
| 14 | TimelineMinimal | LOW | 10 |
| **Total** | | | **138** |
