## Plan: Add `MidiFxChain.test.tsx` (Vitest unit tests)

**Goal:** Close the highest-priority recent-UI test gap — `MidiFxChain.tsx` (added in `615b5d4`, 2026-07-26) currently has zero test coverage. Per `AGENTS.md`'s "regression wall" rule, a brand-new interactive panel calling 4 RPCs should ship with tests.

**File to create:** `frontend/src/components/MidiFxChain.test.tsx`

### Patterns to mirror (already in the codebase)
- **RPC mock:** `vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }))` + `const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>` — exactly as `WaveformCanvas.test.tsx` does it.
- **Store setup:** `useUiStore.setState({ selectedTrackIndex: 2 })` to drive the component (Zustand's `setState` is the standard test seam; no dedicated setter exists).
- **Async effects:** real timers (the component has no `setTimeout` retries, unlike WaveformCanvas) + `await act(async () => { ... })` to flush the effect's `.then()`.
- **Error-toast assertion:** read `useNotifyStore.getState().toasts` after triggering a rejecting RPC (mirrors `Toaster.test.tsx`'s store-direct assertions).
- **Reset between tests:** `beforeEach` → `mockedCall.mockReset()`, `useUiStore.setState({ selectedTrackIndex: null })`, `useNotifyStore.getState().clear()`.

### Test cases (10)
**Render / read path:**
1. `selectedTrackIndex == null` → renders `mfx-empty` "Select a track to edit MIDI FX" and does **not** call `rpc.call`.
2. Track selected, `read.getMidiFxSlots` → `[]` → renders header "MIDI FX — Track 2", the "+ Add MIDI FX…" dropdown with all 5 options (Arp/Velocity/Chord/Scale/NoteLength), and "No MIDI FX on this track".
3. `read.getMidiFxSlots` → 2 slots (arpeggiator active, velocity bypassed) → renders 2 `.mfx-slot` rows with correct index spans, type labels, and the bypassed row carries `mfx-slot--bypassed`.
4. Non-array RPC response (`null`) → treated as empty (the `Array.isArray(data) ? ... : []` guard) → "No MIDI FX on this track".

**Interactions → RPC contracts:**
5. Changing the dropdown to "chord" → `rpc.call("project.addMidiFxSlot", { trackIndex: 2, type: "chord" })` then a follow-up `read.getMidiFxSlots` (refresh).
6. Clicking "Del" on slot 0 → `rpc.call("project.removeMidiFxSlot", { trackIndex: 2, slotIndex: 0 })` then refresh.
7. Clicking "Byp" on an **active** slot → `rpc.call("project.setMidiFxSlotBypassed", { ..., bypassed: true })`.
8. Clicking "Byp" on a **bypassed** slot → `bypassed: false`.

**Reactivity / error path:**
9. Changing `selectedTrackIndex` (2 → 3) re-fetches with `trackIndex: 3`.
10. `project.addMidiFxSlot` rejects → a toast appears in `useNotifyStore.getState().toasts` with `level: "error"` and message containing `project.addMidiFxSlot` (proves the `reportRpcError` catch path works).

### Verification
- `cd frontend && npm test -- MidiFxChain` (or `npm run test:watch`) — all 10 pass.
- `npm test` — full suite still green (new file is auto-discovered via `src/**/*.test.tsx` glob in `vitest.config.ts`).

### Out of scope (recommended follow-ups, separate changes)
- `StepSequencer.test.tsx` render + interaction tests (only the pure math is tested today).
- `PianoRoll` Swing slider + `NoteGrid` swing-prop integration test (the `grooveUtils` math is tested; the component wiring is not).

These are noted for a later pass so this change stays focused on the single clearest gap.