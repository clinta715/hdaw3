# Plan: Timeline zoom — wheel, pointer-centered, marquee, repackage

> Handoff item #4 of `docs/handoffs/2026-08-12-render-content-quality-and-ui-zoom.md`.
> Status: LIVE. Implementation dispatch in progress.

## Goal

Make timeline zoom work correctly and idiomatically (Bitwig idiom per AGENTS.md
UI section) across wheel + drag gestures, with tests, and deliver it to the
packaged Electron app (the user's actual runtime).

## Diagnostic findings (verified)

- **Ctrl/Cmd+wheel zoom IS in source** (`useTimelineZoom.ts:34-45`, fix landed
  `b7cfdff` 2026-07-30). The handler is correctly wired (`{passive:false}`,
  `preventDefault`, functional `setPps`).
- **Vertical-drag-zoom IS in source** (`useTimelineRubberBand.ts:69-83`) with an
  existing E2E test (`editing.spec.ts:154`). Threshold is strict:
  `Math.abs(dy) > Math.abs(dx) * 2`.
- **Packaged asar (`frontend/release/win-unpacked/resources/app.asar`) is from
  2026-08-05** — missing 5 days of refactor commits (`746ecf4`, `c05ec4b`,
  `d9d14eb`, `182d650`) though NOT missing zoom code specifically. `dist/` is
  newer (2026-08-11). User confirmed they run the packaged Electron app.
- **Plain wheel** does nothing (handler early-returns on no-ctrl).
- **Marquee zoom** (Ctrl+Alt+drag, Bitwig's "zoom region") is not implemented.
- **Pointer-centered zoom** (Bitwig: zoom keeps the beat under the cursor
  fixed) is not implemented — current zoom just multiplies pps and the
  cursor-beat drifts.
- Dead duplicate PPS constants in `utils/timelineConstants.ts` (80/20/400,
  comment wrongly says "per second" — actually per beat). **Out of scope** for
  this plan; flagged for a later cleanup.

## Canonical Bitwig mapping (verified via official user guide + /r/Bitwig)

- Plain wheel in arrange = vertical scroll
- Shift+wheel = horizontal scroll (Chromium-native)
- **Ctrl/Cmd+wheel = horizontal zoom, centered on pointer**
- **Ctrl+Alt+drag = marquee-zoom to dragged region**
- Alt+wheel = vertical (track-height) zoom — not in scope (track heights are
  not zoomable in HDAW today)

## Success Gates (all must pass to declare done)

- [ ] G1: Plain wheel over `.tl-ruler` zooms (no modifier required).
- [ ] G2: Ctrl/Cmd+wheel anywhere on timeline zooms, **and the beat under the
      cursor stays under the cursor** (verified by a unit test that drives the
      handler with a synthetic wheel event at a known x and asserts
      `tracksRef.scrollLeft` lands on `beatAtCursor * newPps - cursorOffset`).
- [ ] G3: Ctrl+Alt+drag on the ruler draws a marquee rectangle; on mouseup the
      viewport zooms to fit the dragged beat range (clamped to MIN_PPS/MAX_PPS;
      right-to-left drags swap; sub-threshold drags are ignored as clicks).
- [ ] G4: Vertical-drag-zoom on tracks background still works (regression).
- [ ] G5: Plain wheel over `.tl-tracks` still scrolls natively (regression).
- [ ] G6: Shift+wheel still scrolls horizontally (regression — no preventDefault
      on shift-only wheel).
- [ ] G7: Status bar surfaces the zoom gestures (discoverability).
- [ ] G8: New Vitest unit test for `useTimelineZoom` (pointer-centered math)
      passes: `cd frontend && npm test -- useTimelineZoom`.
- [ ] G9: Existing Vitest suite still green: `cd frontend && npm test`.
- [ ] G10: Vite production build succeeds: `cd frontend && npm run build`.
- [ ] G11: New + existing Playwright E2E zoom tests pass:
       `cd frontend && npm run test:e2e -- --grep "zoom|Zoom"`.
- [ ] G12: CSS uses only `--var` tokens; no raw hex in new rules. Respects
       `prefers-reduced-motion`.
- [ ] G13: After repackage (`frontend\build.bat`), the packaged
       `frontend/release/win-unpacked/HDAW.exe` exhibits all gestures working
       (manual verify by orchestrator).

## Dependency Map

- **Blast radius** (consumers of `pps`/`setPps` — interface unchanged, so safe):
  `TimelineMinimal.tsx` (owner), `useTimelineDrag`, `useTimelineTrim`,
  `useTimelineFade`, `useTimelineLoopDrag`, `useTimelineRubberBand`,
  `useTimelineDrop`, `useTimelineRuler`, `useTimelineKeyboard`, `ArrangerLane`.
  All consume `pps` as a prop — no change required.
- **Files modified by this plan:**
  - `frontend/src/hooks/useTimelineZoom.ts` — pointer-centered math, ruler-wheel
    support, `tracksRef` + `rulerRef` params.
  - `frontend/src/components/TimelineMinimal/TimelineMinimal.tsx` — pass
    `tracksRef`/`rulerRef` into `useTimelineZoom`; render marquee overlay.
  - `frontend/src/components/TimelineMinimal/useTimelineRuler.ts` — add
    Ctrl+Alt+drag marquee-zoom handler (after scrub/loop-click early returns).
  - `frontend/src/components/TimelineMinimal.css` — `.tl-zoom-rect` overlay
    (token-only).
  - `frontend/src/components/StatusBar.tsx` — extend arrange-view hint.
  - **New tests:**
    - `frontend/src/hooks/useTimelineZoom.test.ts` (Vitest, pointer math).
    - E2E cases in `frontend/e2e/editing.spec.ts` (or a new `zoom.spec.ts`):
      wheel-over-ruler-zooms, ctrl+wheel-zooms-centered, marquee-zoom,
      vertical-drag-zoom (already exists — keep).
- **God nodes in scope:** none of the high-degree hubs (TimelineMinimal is a
  leaf component; the zoom hook has ~10 consumers but a stable interface).
- **Community boundaries:** none crossed. Pure frontend change.
- **Projections affected:** none — `pps` is component-local React state, not in
  the engine ValueTree, ReadModel, or frontend snapshot. No SPSC, no RPC.
- **Path integrity:** N/A (no new RPC/command; pure UI).

## Pitfall Gates Triggered

- **Gate 5 (Frontend stale closures / missing hook deps):**
  - The wheel listener is attached ONCE in a `useEffect`. Reading `pps`
    directly in the handler captures a stale value. **Mitigation:** keep a
    `ppsRef` (`useRef(pps)`, reassigned every render) and read `ppsRef.current`
    inside the handler. Keep `setPps` functional. The listener's effect dep
    array stays minimal (`[bodyRef, rulerRef, tracksRef]` — stable refs).
  - Marquee handler reads `pps` for beat math — use the same `ppsRef` or read
    from the `pps` prop in `useTimelineRuler` (which already lists `pps` in its
    `useCallback` dep array). Verify the dep array is correct.
- **Gate 8 (CSS design tokens):** the marquee overlay must use only `--var`
  tokens (e.g. `--accent` for the stroke, `--accent` at low alpha for fill).
  Respect `prefers-reduced-motion` (no animation required here, but if any is
  added, gate it).
- **Gate 4 (Stale binaries):** the orchestrator runs `frontend\build.bat` after
  the subagent finishes — this is the critical delivery step. The subagent
  runs `npm run build` (Vite) only; it must NOT skip that gate.

## Anti-Patterns to avoid

- Reading `pps` from closure inside the wheel listener (stale) — use `ppsRef`.
- Raw hex in CSS — use `var(--accent)` etc.
- Adding the marquee overlay as a floating/absolute element outside the
  timeline grid (spatial stability, AGENTS.md UI section) — keep it inside
  `.tl-tracks-inner` (the existing rubber-band overlay's container) or the
  ruler's inner, scoped to the timeline region.

## Design specifics

### Pointer-centered zoom math

For a wheel event at `clientX`:
1. `el = tracksRef.current; rect = el.getBoundingClientRect();`
2. `cursorOffset = clientX - rect.left;` (within tracks viewport)
3. `beatAtCursor = (cursorOffset + el.scrollLeft) / ppsRef.current;`
4. `next = clamp(ppsRef.current * factor, MIN_PPS, MAX_PPS);`
5. `targetScroll = max(0, beatAtCursor * next - cursorOffset);`
6. `setPps(next)` and store `targetScroll` in a `pendingScrollRef`.
7. A `useLayoutEffect` on `[pps]` applies `tracksRef.current.scrollLeft =
   pendingScrollRef.current` (after React commits the new `totalW`). The
   existing `onTracksScroll` handler then syncs `rulerRef` and `arrangerLaneRef`.

The same helper is reused by the marquee zoom (which computes target pps from
the beat range, then sets scrollLeft = beat1 * newPps).

### Wheel listener decision tree (single listener on `bodyRef`)

```
on wheel(e):
  isZoomGesture = e.ctrlKey || e.metaKey || isInsideRuler(e.target)
  isShiftOnly   = (e.shiftKey && !e.ctrlKey && !e.metaKey && !e.altKey)
  if isShiftOnly: return                          // native horizontal scroll
  if !isZoomGesture: return                       // native vertical scroll on tracks
  e.preventDefault()
  factor = e.deltaY < 0 ? 1.25 : 0.8
  zoomAt(e.clientX, factor)
```

`isInsideRuler(t)`: `rulerRef.current && t instanceof Node &&
rulerRef.current.contains(t)`.

### Marquee zoom (Ctrl+Alt+drag on ruler)

In `useTimelineRuler`, before the existing scrub/loop logic but AFTER the
`if (e.button !== 0)` guard (or as a parallel branch gated on
`e.ctrlKey && e.altKey`):

- On mousedown with `ctrlKey && altKey` (and only that combo), start a
  marquee: record `startClientX`, set a `zoomRect` state.
- On window mousemove: update `zoomRect.x2 = e.clientX`.
- On window mouseup: compute `b1, b2` from the rect (swap if reversed); if
  `|b2 - b1| * pps < 8px` (sub-threshold), cancel; else compute
  `newPps = clamp(viewportWidth / (b2 - b1), MIN_PPS, MAX_PPS)` and
  `newScroll = b1 * newPps`, then call a `onMarqueeZoom(newPps, newScroll)`
  callback passed from `TimelineMinimal` (which sets `setPps` and queues the
  scroll via the same `pendingScrollRef` mechanism — expose a helper from
  `useTimelineZoom`).
- Render the rectangle as a `<div className="tl-zoom-rect" />` inside the
  ruler inner (scoped, not floating).

**Gesture collision check (no regressions):**
- Plain drag on ruler → scrub (unchanged, falls through)
- Ctrl+click on ruler → loop start (unchanged — `e.altKey` is false, so the
  marquee branch is skipped)
- Alt+click on ruler → loop end (unchanged — `e.ctrlKey` is false)
- Ctrl+Alt+drag on ruler → marquee (new)
- Context menu (right-click) on ruler → marker prompt (unchanged)

## Steps

1. **Subagent (one task — tightly coupled):**
   1. Refactor `useTimelineZoom.ts`:
      - Add `ppsRef`, `pendingScrollRef`, `useLayoutEffect` to apply scroll.
      - Add `tracksRef` and `rulerRef` to the params interface.
      - Replace the wheel handler with the decision-tree version above.
      - Export a `zoomToRange(b1, b2, viewportWidth)` helper for marquee.
   2. Update `TimelineMinimal.tsx` to pass `tracksRef` + `rulerRef` into
      `useTimelineZoom`; thread `zoomToRange` to `useTimelineRuler`.
   3. Extend `useTimelineRuler.ts` with the Ctrl+Alt+drag marquee handler and
      expose `zoomRect` state for the overlay; early-return on plain/Ctrl/Alt
      combos to preserve existing scrub/loop behavior.
   4. Add `.tl-zoom-rect` CSS (token-only, `--accent` stroke + low-alpha fill).
   5. Render the overlay in the ruler inner in `TimelineMinimal.tsx`.
   6. Extend the arrange-view hint in `StatusBar.tsx` to mention zoom gestures.
   7. Add `useTimelineZoom.test.ts` (Vitest): assert pointer-centered math
      (synthetic wheel event at known x, assert `tracksRef.scrollLeft` after).
   8. Add E2E cases to `frontend/e2e/editing.spec.ts` (or new `zoom.spec.ts`):
      - wheel over ruler zooms (clip width changes)
      - Ctrl+wheel over tracks zooms centered (a clip under the cursor stays
        at the same screen-x — assert its `boundingBox().x` is unchanged)
      - Ctrl+Alt+drag on ruler marquee-zooms (drag across N beats → pps ≈
        viewport/N)
   9. Run gates G8–G12 (npm test, npm run build, npm run test:e2e --grep zoom).

2. **Orchestrator (after subagent returns):**
   1. Review the diff (anti-pattern scan, pitfall gate check).
   2. Run `frontend\build.bat` (repackages Electron; long — big timeout).
   3. Launch `frontend/release/win-unpacked/HDAW.exe`, manually verify G1–G7,
      G13.
   4. Report to user.

## Out of scope (flagged, not fixed here)

- Dead PPS constants in `utils/timelineConstants.ts` (80/20/400, wrong comment).
  Cleanup deferred — do not remove without confirming piano roll / other views
  don't reference them dynamically.
- Piano-roll / SessionView zoom (separate components, separate scope).
- Alt+wheel vertical (track-height) zoom (HDAW has no track-height zoom axis).
