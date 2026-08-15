# HTML Frontend Feature Parity — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the React/HTML frontend to core workflow parity with the Qt GUI across 4 phases: timeline interactions, file I/O + waveforms, audio clip editor, and piano roll + FX chain.

**Architecture:** Extend existing Zustand stores and React components. One backend addition (`read.getWaveformPeaks`). All other features use existing FrontendServer RPC methods.

**Tech Stack:** React 19, TypeScript, Zustand, Vite, Electron 33, WebSocket JSON-RPC 2.0

**Spec:** `docs/superpowers/specs/2026-07-18-html-frontend-feature-parity-design.md`

---

## Phase 1: Timeline Interactions + Markers

### Task 1: Multi-clip selection in uiStore

**Files:**
- Modify: `frontend/src/store/uiStore.ts`
- Modify: `frontend/src/App.tsx:33` (references `selectedClipId`)
- Modify: `frontend/src/components/ClipEditor.tsx:8` (references `selectedClipId`)
- Modify: `frontend/src/components/TimelineMinimal.tsx` (references `selectedClipId`)
- Modify: `frontend/src/components/AutomationPanel.tsx:14` (references `selectedClipId`)
- Modify: `frontend/src/components/PianoRoll.tsx` (references via store)

- [ ] **Step 1: Rewrite uiStore with multi-selection**

Replace the contents of `frontend/src/store/uiStore.ts`:

```ts
import { create } from "zustand";
import { ClipSnapshot } from "../rpc/types";

interface UiState {
  selectedClipIds: Set<number>;
  lastSelectedClipId: number | null;
  selectedTrackIndex: number | null;
  clipClipboard: ClipSnapshot[];
  activeBottomTab: string;
  snapEnabled: boolean;
  snapDivision: number;

  selectClip: (id: number | null, trackIndex?: number | null) => void;
  toggleClipSelection: (id: number) => void;
  selectRange: (fromId: number, toId: number, clips: ClipSnapshot[]) => void;
  selectAllClips: (clips: ClipSnapshot[]) => void;
  clearSelection: () => void;
  setClipboard: (clips: ClipSnapshot[]) => void;
  setActiveBottomTab: (tab: string) => void;
  setSnapEnabled: (enabled: boolean) => void;
  setSnapDivision: (division: number) => void;
}

export const useUiStore = create<UiState>((set, get) => ({
  selectedClipIds: new Set(),
  lastSelectedClipId: null,
  selectedTrackIndex: null,
  clipClipboard: [],
  activeBottomTab: "mixer",
  snapEnabled: true,
  snapDivision: 1,

  selectClip: (id, trackIndex) => set({
    selectedClipIds: id != null ? new Set([id]) : new Set<number>(),
    lastSelectedClipId: id,
    selectedTrackIndex: trackIndex ?? null,
  }),

  toggleClipSelection: (id) => set((state) => {
    const next = new Set(state.selectedClipIds);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    return { selectedClipIds: next, lastSelectedClipId: id };
  }),

  selectRange: (fromId, toId, clips) => set((state) => {
    const fromClip = clips.find(c => c.clipId === fromId);
    const toClip = clips.find(c => c.clipId === toId);
    if (!fromClip || !toClip) return state;
    const trackIdx = fromClip.trackIndex;
    const minBeat = Math.min(fromClip.startBeat, toClip.startBeat);
    const maxBeat = Math.max(fromClip.startBeat, toClip.startBeat);
    const next = new Set(state.selectedClipIds);
    for (const c of clips) {
      if (c.trackIndex === trackIdx && c.startBeat >= minBeat && c.startBeat <= maxBeat) {
        next.add(c.clipId);
      }
    }
    return { selectedClipIds: next, lastSelectedClipId: toId };
  }),

  selectAllClips: (clips) => set({
    selectedClipIds: new Set(clips.map(c => c.clipId)),
  }),

  clearSelection: () => set({ selectedClipIds: new Set(), lastSelectedClipId: null }),

  setClipboard: (clips) => set({ clipClipboard: clips }),

  setActiveBottomTab: (tab) => set({ activeBottomTab: tab }),

  setSnapEnabled: (enabled) => set({ snapEnabled: enabled }),
  setSnapDivision: (division) => set({ snapDivision: division }),
}));
```

- [ ] **Step 2: Update App.tsx to use selectedClipIds**

In `frontend/src/App.tsx`, replace line 33:
```tsx
// OLD:
{useUiStore((s) => s.selectedClipId) != null && (

// NEW:
{useUiStore((s) => s.selectedClipIds.size === 1) && (
```

- [ ] **Step 3: Update ClipEditor.tsx to use selectedClipIds**

In `frontend/src/components/ClipEditor.tsx`, replace line 8:
```tsx
// OLD:
const clipId = useUiStore((s) => s.selectedClipId);

// NEW:
const clipId = useUiStore((s) => {
  const ids = s.selectedClipIds;
  return ids.size === 1 ? ids.values().next().value! : null;
});
```

- [ ] **Step 4: Update AutomationPanel.tsx to use selectedClipIds**

In `frontend/src/components/AutomationPanel.tsx`, replace line 14:
```tsx
// OLD:
const { selectedClipId, selectedTrackIndex } = useUiStore((s) => ({ selectedClipId: s.selectedClipId, selectedTrackIndex: s.selectedTrackIndex }));

// NEW:
const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
```
(Remove `selectedClipId` reference — it's unused in the component body beyond the destructuring.)

- [ ] **Step 5: Update TimelineMinimal.tsx selectClip calls**

In `frontend/src/components/TimelineMinimal.tsx`, find all `useUiStore.getState().selectClip(...)` calls and verify they still work with the new signature (they do — `selectClip` accepts `(id, trackIndex?)`). No changes needed to call sites.

- [ ] **Step 6: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/store/uiStore.ts frontend/src/App.tsx frontend/src/components/ClipEditor.tsx frontend/src/components/AutomationPanel.tsx
git commit -m "frontend: multi-clip selection in uiStore"
```

---

### Task 2: Multi-select interactions in TimelineMinimal

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TimelineMinimal.css`

- [ ] **Step 1: Add rubber band state and rendering**

In `frontend/src/components/TimelineMinimal.tsx`, add state after the `loopDrag` state (around line 63):

```tsx
// --- Rubber band state ---
const [rubberBand, setRubberBand] = useState<{ x1: number; y1: number; x2: number; y2: number } | null>(null);
const rubberBandRef = useRef(rubberBand);
rubberBandRef.current = rubberBand;
```

- [ ] **Step 2: Add rubber band mousedown handler on empty area**

Add a handler for mousedown on the tracks inner div (the empty area). Wire it into the `tl-tracks-inner` div's `onMouseDown`. The handler should:
1. Check if the click target is NOT a clip (`.tl-clip`)
2. Start rubber band tracking
3. On mousemove, update rubber band rect
4. On mouseup, find all clips intersecting the rect and select them

```tsx
const handleRubberBandStart = useCallback((e: React.MouseEvent) => {
  if ((e.target as HTMLElement).closest(".tl-clip")) return;
  const el = tracksRef.current;
  if (!el) return;
  const rect = el.getBoundingClientRect();
  const x = e.clientX - rect.left + el.scrollLeft;
  const y = e.clientY - rect.top + el.scrollTop;
  setRubberBand({ x1: x, y1: y, x2: x, y2: y });

  const onMove = (ev: globalThis.MouseEvent) => {
    const r = el.getBoundingClientRect();
    setRubberBand(prev => prev ? {
      ...prev,
      x2: ev.clientX - r.left + el.scrollLeft,
      y2: ev.clientY - r.top + el.scrollTop,
    } : null);
  };

  const onUp = () => {
    window.removeEventListener("mousemove", onMove);
    window.removeEventListener("mouseup", onUp);
    const rb = rubberBandRef.current;
    if (rb) {
      const minX = Math.min(rb.x1, rb.x2);
      const maxX = Math.max(rb.x1, rb.x2);
      const minY = Math.min(rb.y1, rb.y2);
      const maxY = Math.max(rb.y1, rb.y2);
      const selected = new Set<number>();
      for (const clip of clips) {
        const cx = clip.startBeat * pps;
        const cy = clip.trackIndex * TRACK_HEIGHT;
        const cw = clip.durationBeats * pps;
        const ch = TRACK_HEIGHT;
        if (cx + cw >= minX && cx <= maxX && cy + ch >= minY && cy <= maxY) {
          selected.add(clip.clipId);
        }
      }
      if (selected.size > 0) {
        useUiStore.setState({ selectedClipIds: selected });
      }
    }
    setRubberBand(null);
  };

  window.addEventListener("mousemove", onMove);
  window.addEventListener("mouseup", onUp);
}, [clips, pps]);
```

- [ ] **Step 3: Update clip click to support Ctrl/Shift**

In the clip `onClick` handler (around line 444), replace with:

```tsx
onClick={(e) => {
  e.stopPropagation();
  if (e.ctrlKey || e.metaKey) {
    useUiStore.getState().toggleClipSelection(clip.clipId);
  } else if (e.shiftKey) {
    const anchor = useUiStore.getState().lastSelectedClipId;
    if (anchor != null) {
      useUiStore.getState().selectRange(anchor, clip.clipId, clips);
    } else {
      useUiStore.getState().selectClip(clip.clipId, idx);
    }
  } else {
    useUiStore.getState().selectClip(clip.clipId, idx);
  }
}}
```

- [ ] **Step 4: Update empty area click to clear selection**

In the `tl-tracks-inner` div's `onClick` (line 424), update:
```tsx
onClick={() => useUiStore.getState().clearSelection()}
```

- [ ] **Step 5: Update Delete to remove all selected clips**

In the `handleKeyDown` callback (around line 317), replace the delete handler:
```tsx
if (e.key === "Delete" || e.key === "Backspace") {
  const { selectedClipIds } = useUiStore.getState();
  if (selectedClipIds.size > 0) {
    e.preventDefault();
    (async () => {
      for (const id of selectedClipIds) {
        await rpc.call("project.removeClip", { clipId: id }).catch(() => {});
      }
      useUiStore.getState().clearSelection();
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }
}
```

- [ ] **Step 6: Update drag to move all selected clips**

In `handleClipMouseDown`, store all selected clip IDs. In `handleMouseUp`, call `project.moveClip` for each selected clip (not just the dragged one).

- [ ] **Step 7: Add rubber band rendering**

Add to the JSX, after the ghost clip div:
```tsx
{rubberBand && (
  <div
    className="tl-rubber-band"
    style={{
      left: Math.min(rubberBand.x1, rubberBand.x2),
      top: Math.min(rubberBand.y1, rubberBand.y2),
      width: Math.abs(rubberBand.x2 - rubberBand.x1),
      height: Math.abs(rubberBand.y2 - rubberBand.y1),
    }}
  />
)}
```

- [ ] **Step 8: Add rubber band CSS**

In `frontend/src/components/TimelineMinimal.css`:
```css
.tl-rubber-band {
  position: absolute;
  border: 1px solid var(--accent, #d97706);
  background: rgba(217, 119, 6, 0.15);
  pointer-events: none;
  z-index: 100;
}

.tl-clip--selected {
  outline: 2px solid var(--accent, #d97706);
  outline-offset: -1px;
}
```

- [ ] **Step 9: Add selected class to clips**

In the clip rendering, add `tl-clip--selected` class when clip is in `selectedClipIds`:
```tsx
const isSelected = useUiStore((s) => s.selectedClipIds.has(clip.clipId));
// Add to className: ${isSelected ? " tl-clip--selected" : ""}
```

- [ ] **Step 10: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 11: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal.css
git commit -m "frontend: multi-select interactions (rubber band, ctrl/shift click, batch delete)"
```

---

### Task 3: Copy/Cut/Paste/Duplicate clips

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Add copy handler (Ctrl+C)**

In the `handleKeyDown` callback, add after the delete handler:
```tsx
} else if ((e.ctrlKey || e.metaKey) && e.key === "c") {
  const { selectedClipIds } = useUiStore.getState();
  if (selectedClipIds.size > 0) {
    e.preventDefault();
    const snapshot = useProjectStore.getState().snapshot;
    if (!snapshot) return;
    const copied = snapshot.clips.filter(c => selectedClipIds.has(c.clipId));
    useUiStore.getState().setClipboard(copied);
  }
}
```

- [ ] **Step 2: Add cut handler (Ctrl+X)**

```tsx
} else if ((e.ctrlKey || e.metaKey) && e.key === "x") {
  const { selectedClipIds } = useUiStore.getState();
  if (selectedClipIds.size > 0) {
    e.preventDefault();
    const snapshot = useProjectStore.getState().snapshot;
    if (!snapshot) return;
    const copied = snapshot.clips.filter(c => selectedClipIds.has(c.clipId));
    useUiStore.getState().setClipboard(copied);
    (async () => {
      for (const id of selectedClipIds) {
        await rpc.call("project.removeClip", { clipId: id }).catch(() => {});
      }
      useUiStore.getState().clearSelection();
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }
}
```

- [ ] **Step 3: Add paste handler (Ctrl+V)**

```tsx
} else if ((e.ctrlKey || e.metaKey) && e.key === "v") {
  const { clipClipboard } = useUiStore.getState();
  if (clipClipboard.length > 0) {
    e.preventDefault();
    const transport = useTransportStore.getState().transport;
    const playheadBeats = transport.currentTimeSeconds * (transport.bpm / 60);
    const minStart = Math.min(...clipClipboard.map(c => c.startBeat));
    (async () => {
      for (const clip of clipClipboard) {
        const newStart = playheadBeats + (clip.startBeat - minStart);
        if (clip.isMidi) {
          await rpc.call("project.addMidiClip", {
            trackIndex: clip.trackIndex,
            start: newStart,
            duration: clip.durationBeats,
            name: clip.name,
          }).catch(() => {});
        } else {
          await rpc.call("project.addAudioClip", {
            trackIndex: clip.trackIndex,
            start: newStart,
            duration: clip.durationBeats,
            sourceFile: clip.sourceFile,
            name: clip.name,
          }).catch(() => {});
        }
      }
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }
}
```

- [ ] **Step 4: Add duplicate handler (Ctrl+D)**

```tsx
} else if ((e.ctrlKey || e.metaKey) && e.key === "d") {
  const { selectedClipIds } = useUiStore.getState();
  if (selectedClipIds.size > 0) {
    e.preventDefault();
    (async () => {
      for (const id of selectedClipIds) {
        await rpc.call("project.duplicateClip", { clipId: id }).catch(() => {});
      }
      await useProjectStore.getState().syncDirtyFlag(rpc);
      await useProjectStore.getState().syncSnapshot(rpc);
    })();
  }
}
```

- [ ] **Step 5: Update context menu to include Copy/Cut/Duplicate**

In the clip context menu JSX, add three buttons before Delete:
```tsx
<button onClick={() => { useUiStore.getState().setClipboard([contextMenu.clip]); setContextMenu(null); }}>
  Copy
</button>
<button onClick={() => {
  useUiStore.getState().setClipboard([contextMenu.clip]);
  setContextMenu(null);
  rpc.call("project.removeClip", { clipId: contextMenu.clip.clipId }).catch(() => {});
}}>
  Cut
</button>
<button onClick={() => {
  setContextMenu(null);
  rpc.call("project.duplicateClip", { clipId: contextMenu.clip.clipId }).catch(() => {});
}}>
  Duplicate
</button>
```

- [ ] **Step 6: Add empty-area context menu**

Add a new context menu for right-click on empty area. Add state:
```tsx
const [emptyContextMenu, setEmptyContextMenu] = useState<{ x: number; y: number; beat: number } | null>(null);
```

Handle right-click on `tl-tracks-inner`:
```tsx
onContextMenu={(e) => {
  if ((e.target as HTMLElement).closest(".tl-clip")) return;
  e.preventDefault();
  const el = tracksRef.current;
  if (!el) return;
  const rect = el.getBoundingClientRect();
  const beat = (e.clientX - rect.left + el.scrollLeft) / pps;
  setEmptyContextMenu({ x: e.clientX, y: e.clientY, beat });
}}
```

Render the menu:
```tsx
{emptyContextMenu && (
  <div className="clip-context-menu" style={{ left: emptyContextMenu.x, top: emptyContextMenu.y }}
    onClick={(e) => e.stopPropagation()}>
    <button onClick={() => { rpc.call("project.addTrack").catch(() => {}); setEmptyContextMenu(null); }}>
      Add Track
    </button>
    {useUiStore.getState().clipClipboard.length > 0 && (
      <button onClick={() => { /* paste logic */ setEmptyContextMenu(null); }}>
        Paste
      </button>
    )}
    <button onClick={() => {
      const bpm = prompt("BPM:", "120");
      if (bpm) rpc.call("project.setTempo", { bpm: parseFloat(bpm) || 120 }).catch(() => {});
      setEmptyContextMenu(null);
    }}>
      Add Tempo Change Here...
    </button>
    <button onClick={() => {
      rpc.call("project.addMidiClip", {
        trackIndex: 0,
        start: emptyContextMenu.beat,
        duration: 4,
        name: "New MIDI Clip",
      }).catch(() => {});
      setEmptyContextMenu(null);
    }}>
      Add MIDI Clip
    </button>
  </div>
)}
```

- [ ] **Step 7: Add Alt+click duplicate drag**

In `handleClipMouseDown`, check if Alt key is held. If so, call `project.duplicateClip` for each selected clip on first mousemove, update selection to new IDs, then continue drag with new clips:

```tsx
const handleClipMouseDown = useCallback(
  (e: React.MouseEvent, clipId: number, trackIndex: number, startBeat: number) => {
    e.preventDefault();
    const el = e.currentTarget as HTMLElement;
    const r = el.getBoundingClientRect();
    const isAlt = e.altKey;
    updateDrag({
      clipId,
      startTrackIndex: trackIndex,
      startBeat,
      offsetX: e.clientX - r.left,
      offsetY: e.clientY - r.top,
      mouseX: e.clientX,
      mouseY: e.clientY,
      isAltDuplicate: isAlt,
      altDuplicated: false,
    });
  },
  [updateDrag]
);
```

Extend `DragState` with `isAltDuplicate: boolean` and `altDuplicated: boolean`. In `handleMouseMove`, if `isAltDuplicate && !altDuplicated`, call `project.duplicateClip` for each selected clip, update `selectedClipIds` to new IDs, and set `altDuplicated = true`.

- [ ] **Step 8: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 9: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "frontend: copy/cut/paste/duplicate clips + context menus + alt-dup drag"
```

---

### Task 4: Fade handle drag on clips

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TimelineMinimal.css`

- [ ] **Step 1: Add fade drag state**

Add after the trim state:
```tsx
// --- Fade drag state ---
const [fadeDrag, setFadeDrag] = useState<{ clipId: number; side: "in" | "out"; initialValue: number; startBeat: number; durationBeats: number } | null>(null);
const fadeDragRef = useRef(fadeDrag);
fadeDragRef.current = fadeDrag;
```

- [ ] **Step 2: Add fade handle detection in clip rendering**

In the clip div, add two invisible fade handle elements:
```tsx
<div className="fade-handle fade-handle-in" onMouseDown={(e) => handleFadeStart(e, clip, "in")} />
<div className="fade-handle fade-handle-out" onMouseDown={(e) => handleFadeStart(e, clip, "out")} />
```

- [ ] **Step 3: Implement handleFadeStart**

```tsx
const handleFadeStart = useCallback((e: React.MouseEvent, clip: typeof clips[0], side: "in" | "out") => {
  e.stopPropagation();
  e.preventDefault();
  setFadeDrag({
    clipId: clip.clipId,
    side,
    initialValue: side === "in" ? clip.fadeIn : clip.fadeOut,
    startBeat: clip.startBeat,
    durationBeats: clip.durationBeats,
  });

  const onMove = (ev: globalThis.MouseEvent) => {
    const el = tracksRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const scroll = el.scrollLeft;
    const mouseBeat = (ev.clientX - rect.left + scroll) / pps;
    const d = fadeDragRef.current;
    if (!d) return;
    const clipStart = d.startBeat * pps;
    const clipEnd = (d.startBeat + d.durationBeats) * pps;
    const mousePx = ev.clientX - rect.left + scroll;
    if (d.side === "in") {
      const newFade = Math.max(0, Math.min(d.durationBeats / 2, (mousePx - clipStart) / pps));
      setFadeDrag(prev => prev ? { ...prev, initialValue: newFade } : null);
    } else {
      const newFade = Math.max(0, Math.min(d.durationBeats / 2, (clipEnd - mousePx) / pps));
      setFadeDrag(prev => prev ? { ...prev, initialValue: newFade } : null);
    }
  };

  const onUp = () => {
    window.removeEventListener("mousemove", onMove);
    window.removeEventListener("mouseup", onUp);
    const d = fadeDragRef.current;
    if (d) {
      const method = d.side === "in" ? "project.setClipFadeIn" : "project.setClipFadeOut";
      rpc.call(method, { clipId: d.clipId, [d.side === "in" ? "fadeIn" : "fadeOut"]: d.initialValue }).catch(() => {});
    }
    setFadeDrag(null);
  };

  window.addEventListener("mousemove", onMove);
  window.addEventListener("mouseup", onUp);
}, [pps]);
```

- [ ] **Step 4: Add fade handle CSS**

In `TimelineMinimal.css`:
```css
.fade-handle {
  position: absolute;
  top: 0;
  width: 12px;
  height: 100%;
  cursor: ew-resize;
  z-index: 5;
}

.fade-handle-in {
  left: 0;
}

.fade-handle-out {
  right: 0;
}
```

- [ ] **Step 5: Add fade overlay visual**

Render a curved SVG path on clips to show fade-in/fade-out:
```tsx
{(clip.fadeIn > 0 || clip.fadeOut > 0 || fadeDrag?.clipId === clip.clipId) && (
  <svg className="fade-overlay" style={{ position: "absolute", top: 0, left: 0, width: "100%", height: "100%", pointerEvents: "none" }}>
    <path
      d={`M0,${TRACK_HEIGHT - 8} L${(fadeDrag?.clipId === clip.clipId && fadeDrag.side === "in" ? fadeDrag.initialValue : clip.fadeIn) / clip.durationBeats * 100}%,0 L${100 - (fadeDrag?.clipId === clip.clipId && fadeDrag.side === "out" ? fadeDrag.initialValue : clip.fadeOut) / clip.durationBeats * 100}%,0 L100%,${TRACK_HEIGHT - 8}`}
      fill="rgba(255,255,255,0.1)"
      stroke="rgba(255,255,255,0.3)"
      strokeWidth="1"
    />
  </svg>
)}
```

- [ ] **Step 6: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal.css
git commit -m "frontend: fade handle drag on timeline clips"
```

---

### Task 5: Marker store and display

**Files:**
- Create: `frontend/src/store/markerStore.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TimelineMinimal.css`

- [ ] **Step 1: Create markerStore**

Create `frontend/src/store/markerStore.ts`:
```ts
import { create } from "zustand";
import { RpcClient } from "../rpc/client";

export interface MarkerSnapshot {
  index: number;
  name: string;
  time: number;
  color: number;
}

interface MarkerState {
  markers: MarkerSnapshot[];
  syncMarkers: (rpc: RpcClient) => Promise<void>;
}

export const useMarkerStore = create<MarkerState>((set) => ({
  markers: [],
  syncMarkers: async (rpc: RpcClient) => {
    try {
      const result = await rpc.call("read.getMarkers");
      if (Array.isArray(result)) {
        set({ markers: result as MarkerSnapshot[] });
      }
    } catch {
      // ignore
    }
  },
}));
```

- [ ] **Step 2: Sync markers on project load**

In `frontend/src/store/projectStore.ts`, after `syncSnapshot`, add marker sync:
```ts
// In syncSnapshot, after set({ snapshot, lastSync: Date.now() }):
// Import and call useMarkerStore.getState().syncMarkers(rpc);
```

- [ ] **Step 3: Render markers on the ruler**

In `TimelineMinimal.tsx`, import `useMarkerStore` and render marker pins in the ruler:
```tsx
const markers = useMarkerStore((s) => s.markers);

// In the ruler inner div, after rulerMarkers:
{markers.map((m) => (
  <div
    key={m.index}
    className="tl-marker-pin"
    style={{ left: m.time * pps }}
    title={m.name}
    onClick={(e) => {
      e.stopPropagation();
      const sec = m.time * 60 / transport.bpm;
      rpc.call("transport.seekToSeconds", { seconds: sec }).catch(() => {});
    }}
    onDoubleClick={(e) => {
      e.stopPropagation();
      const newName = prompt("Marker name:", m.name);
      if (newName != null) rpc.call("project.setMarkerName", { index: m.index, name: newName }).catch(() => {});
    }}
    onContextMenu={(e) => {
      e.preventDefault();
      e.stopPropagation();
      // Show marker context menu (rename, delete)
    }}
  />
))}
```

- [ ] **Step 4: Add marker CSS**

In `TimelineMinimal.css`:
```css
.tl-marker-pin {
  position: absolute;
  top: 0;
  width: 0;
  height: 0;
  border-left: 6px solid transparent;
  border-right: 6px solid transparent;
  border-top: 10px solid #ef4444;
  cursor: pointer;
  z-index: 10;
  transform: translateX(-6px);
}

.tl-marker-pin:hover {
  border-top-color: #f87171;
}
```

- [ ] **Step 5: Add right-click ruler to add marker**

In the ruler's `onContextMenu` handler:
```tsx
onContextMenu={(e) => {
  e.preventDefault();
  const el = tracksRef.current;
  if (!el) return;
  const rect = el.getBoundingClientRect();
  const beat = (e.clientX - rect.left + el.scrollLeft) / pps;
  const name = prompt("Marker name:", `Marker`);
  if (name != null) {
    rpc.call("project.addMarker", { name, time: beat }).catch(() => {});
    useMarkerStore.getState().syncMarkers(rpc);
  }
}}
```

- [ ] **Step 6: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/store/markerStore.ts frontend/src/store/projectStore.ts frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal.css
git commit -m "frontend: markers on timeline ruler"
```

---

## Phase 2: File I/O + Real Waveforms

### Task 6: Enhanced FileMenu

**Files:**
- Modify: `frontend/src/components/FileMenu.tsx`
- Modify: `frontend/src/components/FileMenu.css`
- Modify: `frontend/src/store/projectStore.ts`

- [ ] **Step 1: Add filePath and recentProjects to projectStore**

In `frontend/src/store/projectStore.ts`, add to state:
```ts
filePath: string | null;
recentProjects: string[];
setFilePath: (path: string | null) => void;
loadRecentProjects: () => void;
addRecentProject: (path: string) => void;
```

Initialize from localStorage:
```ts
filePath: null,
recentProjects: JSON.parse(localStorage.getItem("hdaw_recent_projects") || "[]"),
```

Add actions:
```ts
setFilePath: (path) => set({ filePath: path }),
loadRecentProjects: () => set({ recentProjects: JSON.parse(localStorage.getItem("hdaw_recent_projects") || "[]") }),
addRecentProject: (path) => set((state) => {
  const list = [path, ...state.recentProjects.filter(p => p !== path)].slice(0, 8);
  localStorage.setItem("hdaw_recent_projects", JSON.stringify(list));
  return { recentProjects: list };
}),
```

- [ ] **Step 2: Rewrite FileMenu with full menu**

Replace `frontend/src/components/FileMenu.tsx` with the full menu implementation including: New, Open, Open Recent (submenu), Save, Save As, Import Audio, Import MIDI, Export Audio, Exit. Use `window.prompt` for paths and `window.confirm` for unsaved changes.

- [ ] **Step 3: Add unsaved-changes prompt to New/Open**

Before calling `project.newProject` or `project.loadProject`, check `isDirty` and confirm:
```ts
if (useProjectStore.getState().isDirty) {
  if (!confirm("Project has unsaved changes. Continue?")) return;
}
```

- [ ] **Step 4: Wire recent projects to localStorage**

On successful load, call `addRecentProject(filePath)`. On menu open, call `loadRecentProjects()`.

- [ ] **Step 5: Add keyboard shortcuts**

In `App.tsx`, add a global `keydown` handler for Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+Shift+I, Ctrl+Shift+M, Ctrl+E.

- [ ] **Step 6: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/FileMenu.tsx frontend/src/components/FileMenu.css frontend/src/store/projectStore.ts frontend/src/App.tsx
git commit -m "frontend: enhanced file menu with import/export/recent/shortcuts"
```

---

### Task 7: Import dialog

**Files:**
- Create: `frontend/src/components/ImportDialog.tsx`
- Create: `frontend/src/components/ImportDialog.css`

- [ ] **Step 1: Create ImportDialog component**

Create `frontend/src/components/ImportDialog.tsx` with: file path input, track selector dropdown (existing tracks + "New Track"), BPM metadata display, auto tempo-match checkbox, Import/Cancel buttons.

- [ ] **Step 2: Wire to FileMenu**

In `FileMenu.tsx`, when "Import Audio" or "Import MIDI" is clicked, show the dialog (state flag). On import, call `project.addAudioClip` or `project.addMidiClip`.

- [ ] **Step 3: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/ImportDialog.tsx frontend/src/components/ImportDialog.css frontend/src/components/FileMenu.tsx
git commit -m "frontend: import audio/MIDI dialog"
```

---

### Task 8: Export dialog

**Files:**
- Create: `frontend/src/components/ExportDialog.tsx`
- Create: `frontend/src/components/ExportDialog.css`

- [ ] **Step 1: Create ExportDialog component**

Create with: output path input, format combo (WAV/AIFF/FLAC), bit depth combo (16/24/32), progress bar, Export/Cancel buttons. Subscribe to `notify.exportProgress` for progress updates.

- [ ] **Step 2: Wire to FileMenu**

In `FileMenu.tsx`, when "Export Audio" is clicked, show the dialog. On export, call the appropriate RPC method.

- [ ] **Step 3: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/ExportDialog.tsx frontend/src/components/ExportDialog.css frontend/src/components/FileMenu.tsx
git commit -m "frontend: export audio dialog"
```

---

### Task 9: Real waveform rendering

**Files:**
- Modify: `frontend/src/components/WaveformCanvas.tsx`
- Modify: `frontend/src/rpc/types.ts` (add WaveformPeaks type)

- [ ] **Step 1: Add WaveformPeaks type to types.ts**

```ts
export interface WaveformPeaks {
  peaks: number[];
  sampleRate: number;
  numSamples: number;
}
```

- [ ] **Step 2: Rewrite WaveformCanvas to fetch real peaks**

Accept `clipId` prop. On mount/change, call `read.getWaveformPeaks({clipId})`. Render min/max pairs as filled area on canvas. Fall back to flat line if null.

- [ ] **Step 3: Update TimelineMinimal to pass clipId**

In the clip rendering, pass `clipId={clip.clipId}` to `WaveformCanvas`.

- [ ] **Step 4: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/WaveformCanvas.tsx frontend/src/rpc/types.ts frontend/src/components/TimelineMinimal.tsx
git commit -m "frontend: real waveform rendering from peak data"
```

---

## Phase 3: Audio Clip Editor

### Task 10: AudioClipEditor component

**Files:**
- Create: `frontend/src/components/AudioClipEditor.tsx`
- Create: `frontend/src/components/AudioClipEditor.css`
- Modify: `frontend/src/components/BottomTabs.tsx`
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Create AudioClipEditor with waveform display**

Create the component with: Play/Stop buttons, zoom controls, waveform canvas (reuses WaveformCanvas with zoom/scroll), fade handles, region selection, gain envelope overlay.

- [ ] **Step 2: Add controls section**

Add: source file label, gain slider, fade in/out spins, loop checkbox, offset/duration spins, timestretch controls, slice buttons, selection label, region clipboard buttons.

- [ ] **Step 3: Add local playback**

Play button: seek to clip start, play, watch for clip end. Stop: stop transport. 16ms timer updates playhead.

- [ ] **Step 4: Add Audio Editor tab to BottomTabs**

In `App.tsx`, add the tab:
```tsx
{ id: "audio-editor", label: "Audio Editor", content: <AudioClipEditor /> },
```

- [ ] **Step 5: Wire double-click to open audio editor**

In `TimelineMinimal.tsx`, when an audio clip is double-clicked, set `activeBottomTab` to "audio-editor".

- [ ] **Step 6: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/AudioClipEditor.tsx frontend/src/components/AudioClipEditor.css frontend/src/App.tsx frontend/src/components/BottomTabs.tsx
git commit -m "frontend: audio clip editor panel"
```

---

## Phase 4: Piano Roll Depth + FX Chain

### Task 11: Velocity lane

**Files:**
- Create: `frontend/src/components/VelocityLane.tsx`
- Create: `frontend/src/components/VelocityLane.css`
- Modify: `frontend/src/components/PianoRoll.tsx`

- [ ] **Step 1: Create VelocityLane component**

40px tall canvas. Renders vertical bars per note (height = velocity/127). Click+drag adjusts velocity via `project.setNoteVelocity`.

- [ ] **Step 2: Add to PianoRoll layout**

Below the NoteGrid, with synced horizontal scroll.

- [ ] **Step 3: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/VelocityLane.tsx frontend/src/components/VelocityLane.css frontend/src/components/PianoRoll.tsx
git commit -m "frontend: piano roll velocity lane"
```

---

### Task 12: Piano roll keyboard shortcuts and context menu

**Files:**
- Modify: `frontend/src/components/NoteGrid.tsx`
- Modify: `frontend/src/components/NoteGrid.css`

- [ ] **Step 1: Add multi-note selection**

Replace `selectedNoteId: number | null` with `selectedNoteIds: Set<number>`. Add Ctrl+click toggle, Shift+click range.

- [ ] **Step 2: Add transpose shortcuts**

Up/Down: ±1 semitone. Ctrl+Up/Down: ±1 octave. Call `project.setNotePitch` for each selected note.

- [ ] **Step 3: Add quantize (Q) and humanize (H)**

Q: snap startBeat to grid. H: randomize startBeat ± small amount, velocity ± small amount.

- [ ] **Step 4: Add copy/cut/paste notes**

Ctrl+C/X/V with a note clipboard (local state or store).

- [ ] **Step 5: Add select all (Ctrl+A)**

Select all notes in current clip.

- [ ] **Step 6: Add context menu on note**

Right-click: Quantize, Humanize, Transpose Up/Down/Octave, Delete Selected.

- [ ] **Step 7: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 8: Commit**

```bash
git add frontend/src/components/NoteGrid.tsx frontend/src/components/NoteGrid.css
git commit -m "frontend: piano roll shortcuts (transpose, quantize, humanize, copy/paste, context menu)"
```

---

### Task 13: FX chain enhancements

**Files:**
- Modify: `frontend/src/components/FXChain.tsx`
- Modify: `frontend/src/components/FXChain.css`

- [ ] **Step 1: Add plugin selection menu**

Replace "+ Add Internal" with a dropdown: Internal (EQ/Compressor/Reverb/Delay) + Instruments (from `plugin.getInstrumentPlugins()`) + Effects (from `plugin.getEffectPlugins()`).

- [ ] **Step 2: Add edit button per slot**

Calls `audioGraph.toggleFXEditor({trackIndex, slotIndex})` — opens native plugin editor window.

- [ ] **Step 3: Add move up/down buttons**

Calls `project.reorderFxSlots({trackIndex, fromSlot, toSlot})`.

- [ ] **Step 4: Add parameter sliders**

For slots with `paramCount > 0`, fetch params via `pluginParam.getParams`, render collapsible section with labeled sliders, set via `pluginParam.setParam`.

- [ ] **Step 5: Add drag reorder**

HTML5 drag-and-drop on slot rows. On drop, call `project.reorderFxSlots`.

- [ ] **Step 6: Verify build compiles**

Run: `cd frontend && npx tsc --noEmit`
Expected: no errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/FXChain.tsx frontend/src/components/FXChain.css
git commit -m "frontend: FX chain plugin selection, reorder, param sliders, edit button"
```

---

## Summary

| Phase | Tasks | Key Changes |
|-------|-------|-------------|
| 1 | Tasks 1–5 | Multi-select, copy/paste, fade handles, markers |
| 2 | Tasks 6–9 | File menu, import/export dialogs, real waveforms |
| 3 | Task 10 | Audio clip editor panel |
| 4 | Tasks 11–13 | Velocity lane, piano roll depth, FX chain |

**Total: 13 tasks, ~60 steps**
