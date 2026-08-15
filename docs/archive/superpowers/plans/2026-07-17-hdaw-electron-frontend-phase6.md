# HDAW Electron/React Frontend — Phase 6 Implementation Plan

**Goal:** Add per-clip audio editor panel with gain, fades, looping,
timestretch, and gain envelope editing.

---

### Task 0: Write spec + plan docs

- [ ] **Done**

---

### Task 1: Create `uiStore.ts`

```typescript
import { create } from "zustand";

interface UiState {
  selectedClipId: number | null;
  selectClip: (id: number | null) => void;
}

export const useUiStore = create<UiState>((set) => ({
  selectedClipId: null,
  selectClip: (id) => set({ selectedClipId: id }),
}));
```

### Task 2: Create `GainEnvelopeEditor.tsx`

A `<canvas>` showing gain envelope points for the selected clip. Renders
a grid background, gain curve as lines, and points as draggable circles.

**Props:**
- `clipId: number`
- `points: GainEnvelopePoint[]`
- `durationBeats: number`

**Behaviour:**
- Canvas width = `durationBeats * 40`, height = `80`
- Y-axis: 0 at top, 2.0 at bottom (gain range 0–2.0)
- X-axis: time in beats from 0 to durationBeats
- Background: grid lines every beat (vertical), horizontal lines at 0, 0.5, 1.0, 1.5, 2.0
- Points rendered as 6px filled circles (white fill, accent border)
- Curve: polyline connecting consecutive points
- Click on empty area near an existing point → add point at that position
- Drag existing point → `project.moveGainEnvelopePoint` on mouseup
- Double-click on point → `project.removeGainEnvelopePoint`

### Task 3: Create `ClipEditor.tsx`

Reads `selectedClipId` from `useUiStore`, reads clip data from
`useProjectStore`, and renders controls.

**Layout:**
```
.clip-editor
├── .ce-header: "Clip {name} (Track {index})"
├── .ce-row: Gain [=========o===] {1.00}x
├── .ce-row: Fade In  [===o=======] {0.50} beats
├── .ce-row: Fade Out [===o=======] {0.50} beats
├── .ce-row: Looping  [✓]
├── .ce-section: Timestretch
│   ├── .ce-row: Source BPM [120]
│   ├── .ce-row: Mode [Off | Tempo Match | Manual ▼]
│   └── .ce-row: Ratio [1.00] (disabled unless Manual)
└── .ce-section: Gain Envelope
    └── GainEnvelopeEditor
```

Each control calls the appropriate RPC method. The component reads clip
data from the project store (refreshed via Phase 2's `treeChanged` push).

### Task 4: Wire clip selection in `TimelineMinimal.tsx`

Add an `onClick` handler on each `.tl-clip` div that calls
`useUiStore.getState().selectClip(clip.clipId)`. Also add a click handler
on the track area background to deselect (`selectClip(null)`).

### Task 5: Update `App.tsx` to render ClipEditor

Add a `clip-editor-panel` section in the footer alongside mixer and
piano roll. Only render when `selectedClipId` is not null.

### Verify

```bash
cd frontend && npx tsc --noEmit && npx vite build
```

### Commit

```bash
git add frontend/src/store/uiStore.ts frontend/src/components/ClipEditor.tsx frontend/src/components/ClipEditor.css frontend/src/components/GainEnvelopeEditor.tsx frontend/src/components/GainEnvelopeEditor.css frontend/src/components/TimelineMinimal.tsx frontend/src/App.tsx
git commit -m "frontend: add clip editor with gain, fades, looping, timestretch, gain envelope"
```
