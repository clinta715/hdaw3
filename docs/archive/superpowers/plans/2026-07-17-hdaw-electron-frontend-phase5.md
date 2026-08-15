# HDAW Electron/React Frontend — Phase 5 Implementation Plan

**Goal:** Add playhead cursor, time ruler, loop region, and horizontal
zoom to the timeline.

---

### Task 0: Write spec + plan docs

- [ ] **Done**

---

### Task 1: Implement full timeline (playhead, ruler, loop, zoom)

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TimelineMinimal.css`

The component restructures to:

```
.timeline-minimal
├── .tl-toolbar (zoom +/-)
├── .tl-body (position: relative, overflow: hidden)
│   ├── .tl-ruler (24px, overflow: hidden)
│   │   └── .tl-ruler-inner (width: totalW)
│   │       ├── bar and beat markers
│   │       ├── loop band + handles
│   │       └── ruler labels
│   ├── .tl-tracks (overflow: auto, onScroll→sync ruler)
│   │   ├── track rows (existing)
│   │   ├── clip divs (existing, drag support preserved)
│   │   └── .tl-playhead (position: absolute, pointer-events: none)
│   │       (2px accent line spanning tracks height)
│   └── loop highlight band (also in tl-tracks for proper scrolling)
```

Key implementation notes:
- Playhead position: `beats = currentTimeSeconds * (bpm / 60)`, `left = beats * pps`
- 30 Hz updates via `useTransportStore` selector
- Ruler markers rendered from `useMemo`
- Loop highlight rendered only when `isLooping`
- Loop handles draggable via `onMouseDown` → `onMouseMove` → `onMouseUp`, commit via RPC `transport.setLoopStart` / `transport.setLoopEnd`
- Zoom: Ctrl+Wheel handler, +/- toolbar buttons
- Scroll sync: `tracksRef → onScroll → rulerRef.scrollLeft = tracksRef.scrollLeft`
- Clip drag from Phase 3 preserved

**Verify:**
```bash
cd frontend && npx tsc --noEmit && npx vite build
```

**Commit:**
```bash
git add frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal.css
git commit -m "frontend: full timeline with playhead, ruler, loop region, zoom"
```
