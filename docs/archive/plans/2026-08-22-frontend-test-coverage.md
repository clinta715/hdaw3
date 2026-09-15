# Plan: Add Unit Tests for Missing Frontend Components

## Goal
Add Vitest unit tests for 9 UI components that currently lack unit test coverage, and fix TypeScript errors in 9 existing test files.

## Success Gates
- [x] Gate 1: All new test files pass (`npm test -- --run`)
- [x] Gate 2: TypeScript errors in existing test files resolved
- [x] Gate 3: No regression in existing 539 tests
- [x] Gate 4: Test coverage for critical user interactions (button clicks, state changes, RPC calls)

## Final Results
- **New test files**: 9 components (301 new tests)
- **Fixed test files**: 9 files (TypeScript errors resolved)
- **Total tests**: 840 (up from 539)
- **Test files**: 65 (up from 56)
- **TypeScript errors in test files**: 0

## Dependency Map
- **Upstream**: Components consume Zustand stores (transportStore, projectStore, uiStore, etc.)
- **Downstream**: Tests verify component behavior in isolation
- **Projections affected**: None (tests only, no production code changes)
- **SPSC paths touched**: None

## Pitfall Gates Triggered
- **Gate 5 (Frontend Stale Closures)**: Tests must verify that async operations read from `useProjectStore.getState()` not closure props
- **Gate 8 (CSS Design-Token Violations)**: Tests should not assert on raw CSS values

## Anti-Patterns to Avoid
- N separate `await rpc.call()` in test setup - use batch RPC or mock
- Testing implementation details instead of user-visible behavior
- Over-mocking that hides real bugs

## Components to Test (Priority Order)

### High Priority (Complex Components)
1. **TransportBar.tsx** (434 lines) - Core UI, BPM/time sig editing, key/scale popover
2. **PianoRoll.tsx** (623 lines) - Complex note editing, velocity/CC lanes
3. **FXChain.tsx** (729 lines) - Plugin management, parameter editing

### Medium Priority
4. **ArrangerLane.tsx** (207 lines) - Region drag/resize, no E2E coverage
5. **FileMenu.tsx** - File operations
6. **ImportDialog.tsx** - File import
7. **ModulationPanel.tsx** - Modulation settings
8. **PluginManagerDialog.tsx** - Plugin scanning
9. **StepSequencer.tsx** - Step sequencing

### Fix Existing Tests
10. **AudioClipEditor.test.tsx** - Fix `ProjectSnapshot` mock
11. **ErrorBoundary.test.tsx** - Fix `ThrowingChild` component type
12. **meterStore.test.ts** - Fix `MeterLevels` mock
13. **projectStore.test.ts** - Fix `TransportSnapshot` mock
14. **ExportDialog.test.tsx** - Fix `Set<string>` to `Set<number>`
15. **Inspector.test.tsx** - Add missing transport properties
16. **MidiThumbnailCanvas.test.tsx** - Add missing `NoteSnapshot` properties
17. **Mixer.test.tsx** - Add missing `MeterLevels` properties
18. **MixerStrip.test.tsx** - Add missing `MeterLevels` properties

## Steps

### Phase 1: Fix Existing Test Errors (Completed)
1. Update `AudioClipEditor.test.tsx` to include missing `name`, `transport`, `scaleRoot`, `scaleMode` in mock
2. Fix `ErrorBoundary.test.tsx` `ThrowingChild` to return JSX instead of void
3. Update `meterStore.test.ts` mocks to include `rmsL`, `rmsR`, `lufs`
4. Update `projectStore.test.ts` mock to include `timeSigNumerator`, `timeSigDenominator`

### Phase 2: Add High Priority Tests (Completed)
5. Create `TransportBar.test.tsx` with:
   - Button click → RPC call verification
   - BPM inline edit (double-click, enter, escape)
   - Time sig edit
   - Key/scale popover interactions
   - Store state transitions (active classes, dirty indicator)
   - Tap tempo algorithm (mock performance.now)

6. Create `PianoRoll.test.tsx` with:
   - Note grid rendering
   - Velocity lane display
   - CC lane add/remove
   - Chord type selection
   - Quantize/swing controls

7. Create `FXChain.test.tsx` with:
   - Slot rendering
   - Plugin loading flow
   - Parameter expansion
   - Preset selection
   - A/B comparison toggle

### Phase 3: Add Medium Priority Tests (Completed)
8. Create `ArrangerLane.test.tsx` with:
   - Region rendering
   - Drag/resize interactions
   - Rename flow
   - Delete confirmation

9. Create `FileMenu.test.tsx` with:
   - Menu open/close
   - New/Open/Save/SaveAs clicks
   - Recent projects display

10. Create `ImportDialog.test.tsx` with:
    - File selection
    - Format options
    - Import confirmation

11. Create `ModulationPanel.test.tsx` with:
    - LFO parameter display
    - Target selection
    - Depth/rate controls

12. Create `PluginManagerDialog.test.tsx` with:
    - Plugin list rendering
    - Scan button
    - Enable/disable toggle

13. Create `StepSequencer.test.tsx` with:
    - Step grid rendering
    - Pattern selection
    - Velocity editing

### Phase 4: Verification (Completed)
14. Run full test suite: `npm test -- --run`
15. Verify no TypeScript errors: `npx tsc --noEmit`
16. Run E2E tests for affected components: `npm run test:e2e`

## Test Patterns to Follow

### Mock Pattern
```tsx
// Mock stores with realistic data
vi.mock("../store/transportStore", () => ({
  useTransportStore: vi.fn((selector) => selector({
    transport: {
      bpm: 120, isPlaying: false, isLooping: false,
      timeSigNumerator: 4, timeSigDenominator: 4,
      // ... full TransportSnapshot
    }
  }))
}));
```

### RPC Mock Pattern
```tsx
vi.mock("../rpc", () => ({
  rpc: { call: vi.fn().mockResolvedValue({}) }
}));
```

### Component Test Pattern
```tsx
it("play button calls transport.play RPC", async () => {
  render(<TransportBar />);
  await userEvent.click(screen.getByTitle("Play"));
  expect(rpc.call).toHaveBeenCalledWith("transport.play");
});
```

## Verification Commands
```bash
# Run all tests
cd frontend; npm test -- --run

# Check TypeScript
cd frontend; npx tsc --noEmit

# Run E2E for affected components
cd frontend; npx playwright test transport-bar piano-roll fx-chain
```
