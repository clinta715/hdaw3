# Phase 8: Interactive Automation Point Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make automation lane canvases interactive — click to add points, drag to move, right-click to delete, multi-select with Ctrl/Shift.

**Architecture:** Extend the Phase 7 automation store with selection state and RPC-backed mutation actions; add mouse/keyboard event handling to AutomationLaneCanvas; wire Delete key in AutomationPanel. No backend changes needed.

**Tech Stack:** React 19, TypeScript, Zustand, HTML Canvas 2D

---

### Task 1: Store — selection state + mutation actions

**Files:**
- Modify: `frontend/src/store/automationStore.ts`

**Steps:**

- [ ] **Step 1: Read current file**

```bash
cat frontend/src/store/automationStore.ts
```

- [ ] **Step 2: Add `selectedPointTimes` and new actions to the store**

The updated store:

```typescript
import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { AutomationLaneSnapshot, AutomationPointSnapshot } from "../rpc/types";

interface AutomationState {
  lanes: AutomationLaneSnapshot[];
  pointsByLane: Map<string, AutomationPointSnapshot[]>;
  activeTrackIndex: number | null;
  loading: boolean;
  error: string | null;
  selectedPointTimes: Map<string, Set<number>>;

  fetchForTrack: (trackIndex: number, rpc: RpcClient) => Promise<void>;
  clear: () => void;

  selectPoint: (laneName: string, time: number, shift: boolean, ctrl: boolean) => void;
  selectAll: (laneName: string) => void;
  clearSelection: (laneName?: string) => void;
  addPoint: (trackIndex: number, laneName: string, time: number, value: number, rpc: RpcClient) => Promise<void>;
  removePoints: (trackIndex: number, laneName: string, times: number[], rpc: RpcClient) => Promise<void>;
  movePoint: (trackIndex: number, laneName: string, oldTime: number, newTime: number, newValue: number, rpc: RpcClient) => Promise<void>;
}

export const useAutomationStore = create<AutomationState>((set, get) => ({
  lanes: [],
  pointsByLane: new Map(),
  activeTrackIndex: null,
  loading: false,
  error: null,
  selectedPointTimes: new Map(),

  fetchForTrack: async (trackIndex: number, rpc: RpcClient) => {
    set({ loading: true, error: null, activeTrackIndex: trackIndex });
    try {
      const lanes = await rpc.call("read.getAutomationLanes", { trackIndex }) as AutomationLaneSnapshot[];
      const pointsByLane = new Map<string, AutomationPointSnapshot[]>();
      for (const lane of lanes) {
        const pts = await rpc.call("read.getAutomationPoints", { trackIndex, laneName: lane.name }) as AutomationPointSnapshot[];
        pointsByLane.set(lane.name, pts);
      }
      set({ lanes, pointsByLane, loading: false });
    } catch (err) {
      set({ loading: false, error: String(err) });
    }
  },

  clear: () => {
    set({ lanes: [], pointsByLane: new Map(), activeTrackIndex: null, error: null, selectedPointTimes: new Map() });
  },

  selectPoint: (laneName: string, time: number, shift: boolean, ctrl: boolean) => {
    const sel = new Map(get().selectedPointTimes);
    const laneSel = new Set(sel.get(laneName) ?? []);
    if (ctrl) {
      if (laneSel.has(time)) laneSel.delete(time);
      else laneSel.add(time);
    } else if (shift) {
      const pts = get().pointsByLane.get(laneName) ?? [];
      const sorted = pts.map((p) => p.time).sort((a, b) => a - b);
      const clickIdx = sorted.indexOf(time);
      if (laneSel.size > 0 && clickIdx >= 0) {
        const existingTimes = [...laneSel].map((t) => sorted.indexOf(t)).filter((i) => i >= 0);
        if (existingTimes.length > 0) {
          const lastIdx = existingTimes[existingTimes.length - 1];
          const minIdx = Math.min(lastIdx, clickIdx);
          const maxIdx = Math.max(lastIdx, clickIdx);
          for (let i = minIdx; i <= maxIdx; i++) laneSel.add(sorted[i]);
        } else {
          laneSel.add(time);
        }
      } else {
        laneSel.add(time);
      }
    } else {
      laneSel.clear();
      laneSel.add(time);
    }
    sel.set(laneName, laneSel);
    set({ selectedPointTimes: sel });
  },

  selectAll: (laneName: string) => {
    const pts = get().pointsByLane.get(laneName) ?? [];
    const sel = new Map(get().selectedPointTimes);
    sel.set(laneName, new Set(pts.map((p) => p.time)));
    set({ selectedPointTimes: sel });
  },

  clearSelection: (laneName?: string) => {
    const sel = new Map(get().selectedPointTimes);
    if (laneName) sel.delete(laneName);
    else sel.clear();
    set({ selectedPointTimes: sel });
  },

  addPoint: async (trackIndex: number, laneName: string, time: number, value: number, rpc: RpcClient) => {
    await rpc.call("project.addAutomationPoint", { trackIndex, lane: laneName, time, value });
    await get().fetchForTrack(trackIndex, rpc);
  },

  removePoints: async (trackIndex: number, laneName: string, times: number[], rpc: RpcClient) => {
    for (const t of times) {
      await rpc.call("project.removeAutomationPoint", { trackIndex, lane: laneName, time: t });
    }
    await get().fetchForTrack(trackIndex, rpc);
  },

  movePoint: async (trackIndex: number, laneName: string, oldTime: number, newTime: number, newValue: number, rpc: RpcClient) => {
    const needsTimeChange = Math.abs(oldTime - newTime) > 0.001;
    if (needsTimeChange) {
      await rpc.call("project.removeAutomationPoint", { trackIndex, lane: laneName, time: oldTime });
      await rpc.call("project.addAutomationPoint", { trackIndex, lane: laneName, time: newTime, value: newValue });
    } else {
      await rpc.call("project.setAutomationPointValue", { trackIndex, lane: laneName, time: oldTime, value: newValue });
    }
    await get().fetchForTrack(trackIndex, rpc);
  },
}));
```

- [ ] **Step 3: Verify TypeScript**

```bash
cd frontend && npx tsc --noEmit
```

Expected: clean exit (no errors).

- [ ] **Step 4: Commit**

```bash
git add frontend/src/store/automationStore.ts
git commit -m "frontend: add point selection state and mutation actions to automation store"
```

---

### Task 2: Canvas CSS — interaction styles

**Files:**
- Modify: `frontend/src/components/AutomationLaneCanvas.css`

- [ ] **Step 1: Read current file**

```bash
cat frontend/src/components/AutomationLaneCanvas.css
```

- [ ] **Step 2: Replace with updated CSS**

```css
.automation-lane-canvas {
  display: flex;
  align-items: stretch;
  height: 80px;
  border-bottom: 1px solid var(--border, #333);
  user-select: none;
}

.alc-header {
  width: 100px;
  flex-shrink: 0;
  display: flex;
  align-items: center;
  padding: 0 8px;
  font-size: 11px;
  color: var(--text-secondary, #999);
  border-right: 1px solid var(--border, #333);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.alc-canvas {
  flex: 1;
  height: 80px;
  display: block;
  cursor: crosshair;
}

.alc-canvas.alc-dragging {
  cursor: grabbing;
}

.alc-canvas.alc-hover-point {
  cursor: grab;
}
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/AutomationLaneCanvas.css
git commit -m "frontend: add interaction cursor styles to automation lane canvas"
```

---

### Task 3: Canvas interaction — mouse handlers and rendering

**Files:**
- Modify: `frontend/src/components/AutomationLaneCanvas.tsx`

- [ ] **Step 1: Read current file**

```bash
cat frontend/src/components/AutomationLaneCanvas.tsx
```

- [ ] **Step 2: Rewrite with interaction handlers**

```typescript
import { useRef, useEffect, useState, useCallback } from "react";
import { RpcClient } from "../rpc/client";
import { AutomationPointSnapshot } from "../rpc/types";
import { useAutomationStore } from "../store/automationStore";
import "./AutomationLaneCanvas.css";

interface Props {
  laneName: string;
  points: AutomationPointSnapshot[];
  trackIndex: number;
  rpc: RpcClient;
  viewStartBeat: number;
  viewEndBeat: number;
  paramID: number;
  color?: string;
}

const POINT_RADIUS = 3;
const HIT_RADIUS = 6;

function buildPath(pts: AutomationPointSnapshot[], vw: number, vh: number, viewStart: number, viewEnd: number): string {
  if (pts.length === 0) return "";
  const range = viewEnd - viewStart;
  if (range <= 0) return "";
  const visible = pts.filter((p) => p.time >= viewStart && p.time <= viewEnd).sort((a, b) => a.time - b.time);
  if (visible.length === 0) return "";
  return visible.map((p, i) => {
    const x = ((p.time - viewStart) / range) * vw;
    const y = (1 - p.value) * vh;
    return `${i === 0 ? "M" : "L"}${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(" ");
}

function beatFromX(mx: number, cw: number, viewStart: number, viewEnd: number): number {
  return viewStart + (mx / cw) * (viewEnd - viewStart);
}

function valueFromY(my: number, ch: number): number {
  return Math.max(0, Math.min(1, 1 - my / ch));
}

function distToPoint(mx: number, my: number, px: number, py: number): number {
  const dx = mx - px;
  const dy = my - py;
  return Math.sqrt(dx * dx + dy * dy);
}

export default function AutomationLaneCanvas({
  laneName,
  points,
  trackIndex,
  rpc,
  viewStartBeat = 0,
  viewEndBeat = 32,
  color = "var(--automation-line, #4fc3f7)",
}: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState({ w: 600, h: 80 });
  const [hoveredTime, setHoveredTime] = useState<number | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [dragStartMouse, setDragStartMouse] = useState({ x: 0, y: 0 });
  const [dragOrigTime, setDragOrigTime] = useState(0);
  const [dragOrigValue, setDragOrigValue] = useState(0);
  const [dragCurrentTime, setDragCurrentTime] = useState(0);
  const [dragCurrentValue, setDragCurrentValue] = useState(0);
  const dragOriginsRef = useRef<Map<number, { time: number; value: number }>>(new Map());

  const store = useAutomationStore();
  const laneSel = store.selectedPointTimes.get(laneName) ?? new Set<number>();

  // ResizeObserver for HiDPI
  useEffect(() => {
    const el = canvasRef.current;
    if (!el) return;
    const ro = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect;
      const dpr = window.devicePixelRatio || 1;
      el.width = width * dpr;
      el.height = height * dpr;
      el.style.width = `${width}px`;
      el.style.height = `${height}px`;
      setSize({ w: width, h: height });
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  // Draw
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, size.w, size.h);

    const sorted = [...points].filter(p => p.time >= viewStartBeat && p.time <= viewEndBeat).sort((a, b) => a.time - b.time);
    const range = viewEndBeat - viewStartBeat;
    if (range <= 0) return;

    // Fill under curve
    if (sorted.length >= 2) {
      ctx.beginPath();
      ctx.moveTo(0, size.h);
      sorted.forEach((p) => {
        const x = ((p.time - viewStartBeat) / range) * size.w;
        const y = (1 - p.value) * size.h;
        ctx.lineTo(x, y);
      });
      ctx.lineTo(((sorted[sorted.length - 1].time - viewStartBeat) / range) * size.w, size.h);
      ctx.closePath();
      ctx.fillStyle = color;
      ctx.globalAlpha = 0.15;
      ctx.fill();
      ctx.globalAlpha = 1;
    }

    // Line
    const linePath = buildPath(sorted, size.w, size.h, viewStartBeat, viewEndBeat);
    if (linePath) {
      const segments = new Path2D(linePath);
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.stroke(segments);
    }

    // Points
    sorted.forEach((p) => {
      const x = ((p.time - viewStartBeat) / range) * size.w;
      const y = (1 - p.value) * size.h;
      const isSelected = laneSel.has(p.time);
      const isHovered = hoveredTime === p.time;

      // Selection ring
      if (isSelected) {
        ctx.beginPath();
        ctx.arc(x, y, POINT_RADIUS + 3, 0, Math.PI * 2);
        ctx.strokeStyle = "#fff";
        ctx.lineWidth = 2;
        ctx.stroke();
      }

      ctx.beginPath();
      ctx.arc(x, y, isHovered ? POINT_RADIUS + 1 : POINT_RADIUS, 0, Math.PI * 2);
      ctx.fillStyle = isSelected ? "#fff" : color;
      ctx.fill();
    });

    // Drag preview
    if (isDragging) {
      const dx = ((dragCurrentTime - viewStartBeat) / range) * size.w;
      const dy = (1 - dragCurrentValue) * size.h;

      // Dashed line from original to current
      const ox = ((dragOrigTime - viewStartBeat) / range) * size.w;
      const oy = (1 - dragOrigValue) * size.h;

      ctx.beginPath();
      ctx.moveTo(ox, oy);
      ctx.lineTo(dx, dy);
      ctx.strokeStyle = "#fff";
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.stroke();
      ctx.setLineDash([]);

      // Preview point
      ctx.beginPath();
      ctx.arc(dx, dy, POINT_RADIUS + 2, 0, Math.PI * 2);
      ctx.fillStyle = "#fff";
      ctx.globalAlpha = 0.6;
      ctx.fill();
      ctx.globalAlpha = 1;
    }
  }, [points, size, laneSel, hoveredTime, isDragging, dragCurrentTime, dragCurrentValue, dragOrigTime, dragOrigValue, color, viewStartBeat, viewEndBeat]);

  const getPointAt = useCallback((mx: number, my: number): number | null => {
    const range = viewEndBeat - viewStartBeat;
    if (range <= 0) return null;
    const sorted = [...points].filter(p => p.time >= viewStartBeat && p.time <= viewEndBeat);
    for (const p of sorted) {
      const px = ((p.time - viewStartBeat) / range) * size.w;
      const py = (1 - p.value) * size.h;
      if (distToPoint(mx, my, px, py) < HIT_RADIUS) {
        return p.time;
      }
    }
    return null;
  }, [points, size, viewStartBeat, viewEndBeat]);

  const commitMove = useCallback(async () => {
    const deltaTime = dragCurrentTime - dragOrigTime;
    const deltaValue = dragCurrentValue - dragOrigValue;
    if (Math.abs(deltaTime) < 0.001 && Math.abs(deltaValue) < 0.001) return;

    const origins = dragOriginsRef.current;
    // Apply delta to all selected points (multi-move)
    for (const [origTime, orig] of origins) {
      const newTime = orig.time + deltaTime;
      const newValue = Math.max(0, Math.min(1, orig.value + deltaValue));
      const needsTimeChange = Math.abs(newTime - orig.time) > 0.001;
      if (needsTimeChange) {
        await rpc.call("project.removeAutomationPoint", { trackIndex, lane: laneName, time: orig.time });
        await rpc.call("project.addAutomationPoint", { trackIndex, lane: laneName, time: newTime, value: newValue });
      } else {
        await rpc.call("project.setAutomationPointValue", { trackIndex, lane: laneName, time: orig.time, value: newValue });
      }
    }
    await store.fetchForTrack(trackIndex, rpc);
    store.clearSelection(laneName);
  }, [dragOrigTime, dragOrigValue, dragCurrentTime, dragCurrentValue, trackIndex, laneName, rpc, store]);

  const addPointAt = useCallback(async (mx: number, my: number) => {
    const t = beatFromX(mx, size.w, viewStartBeat, viewEndBeat);
    const v = valueFromY(my, size.h);
    await store.addPoint(trackIndex, laneName, t, v, rpc);
  }, [size, viewStartBeat, viewEndBeat, trackIndex, laneName, rpc, store]);

  const handleMouseDown = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (e.button !== 0) return;
    const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;

    const hitTime = getPointAt(mx, my);
    if (hitTime !== null) {
      // If point is already selected, keep selection; otherwise single-select
      if (!laneSel.has(hitTime) && !e.shiftKey && !e.ctrlKey) {
        store.selectPoint(laneName, hitTime, false, false);
      }
      // Capture origins for all selected points (multi-move support)
      const currentSel = useAutomationStore.getState().selectedPointTimes.get(laneName) ?? new Set<number>();
      const origins = new Map<number, { time: number; value: number }>();
      for (const t of currentSel) {
        const pt = points.find((p) => p.time === t);
        if (pt) origins.set(t, { time: pt.time, value: pt.value });
      }
      // Ensure the dragged point is in origins
      if (!origins.has(hitTime)) {
        origins.set(hitTime, { time: hitTime, value: points.find((p) => p.time === hitTime)?.value ?? 0 });
      }
      dragOriginsRef.current = origins;
      setIsDragging(true);
      setDragStartMouse({ x: mx, y: my });
      setDragOrigTime(hitTime);
      setDragOrigValue(points.find((p) => p.time === hitTime)?.value ?? 0);
      setDragCurrentTime(hitTime);
      setDragCurrentValue(points.find((p) => p.time === hitTime)?.value ?? 0);
    } else {
      // Only add point if no modifier held
      if (!e.shiftKey && !e.ctrlKey) {
        store.clearSelection(laneName);
        addPointAt(mx, my);
      } else {
        store.clearSelection(laneName);
      }
    }
  }, [getPointAt, laneName, points, store, addPointAt]);

  const handleMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;

    if (isDragging) {
      const t = beatFromX(mx, size.w, viewStartBeat, viewEndBeat);
      const v = valueFromY(my, size.h);
      setDragCurrentTime(t);
      setDragCurrentValue(v);

      // If multi-selected, we only track preview for the dragged point;
      // the store handles the others on commit
    } else {
      const hitTime = getPointAt(mx, my);
      setHoveredTime(hitTime);
      const el = canvasRef.current;
      if (el) {
        el.classList.toggle("alc-hover-point", hitTime !== null);
        el.classList.toggle("alc-dragging", false);
      }
    }
  }, [isDragging, getPointAt, size, viewStartBeat, viewEndBeat]);

  const handleMouseUp = useCallback(async (e: React.MouseEvent<HTMLCanvasElement>) => {
    if (isDragging) {
      await commitMove();
      setIsDragging(false);
      const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const hitTime = getPointAt(mx, my);
      setHoveredTime(hitTime);
    }
  }, [isDragging, commitMove, getPointAt]);

  const handleContextMenu = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const rect = (e.target as HTMLCanvasElement).getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    const hitTime = getPointAt(mx, my);
    if (hitTime !== null) {
      const timesToDelete = laneSel.has(hitTime) ? [...laneSel] : [hitTime];
      store.removePoints(trackIndex, laneName, timesToDelete, rpc);
    }
  }, [getPointAt, laneSel, trackIndex, laneName, rpc, store]);

  return (
    <div className="automation-lane-canvas" ref={containerRef}>
      <div className="alc-header">{laneName}</div>
      <canvas
        ref={canvasRef}
        className="alc-canvas"
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={() => { setHoveredTime(null); setIsDragging(false); }}
        onContextMenu={handleContextMenu}
      />
    </div>
  );
}
```

- [ ] **Step 3: Verify TypeScript**

```bash
cd frontend && npx tsc --noEmit
```

Expected: clean exit (no errors).

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/AutomationLaneCanvas.tsx
git commit -m "frontend: add interactive point editing to automation lane canvas"
```

---

### Task 4: Panel keyboard handler

**Files:**
- Modify: `frontend/src/components/AutomationPanel.tsx`

- [ ] **Step 1: Read current file**

```bash
cat frontend/src/components/AutomationPanel.tsx
```

- [ ] **Step 2: Add keyboard handler**

Add a `handleKeyDown` function and attach it to the root div:

Insert the import for `useCallback`:
```typescript
import { useEffect, useState, useCallback } from "react";
```

Add the handler inside the component body (before the main return), but only include it when a track is active:

```typescript
const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
  const { lanes, pointsByLane, selectedPointTimes, activeTrackIndex, removePoints, selectAll, clearSelection } = useAutomationStore.getState();
  if (activeTrackIndex === null) return;

  // Find which lane's point times are selected
  for (const lane of lanes) {
    const sel = selectedPointTimes.get(lane.name);
    if (sel && sel.size > 0) {
      if (e.key === "Delete" || e.key === "Backspace") {
        e.preventDefault();
        removePoints(activeTrackIndex, lane.name, [...sel], rpc);
        clearSelection(lane.name);
      } else if (e.key === "a" && (e.ctrlKey || e.metaKey)) {
        e.preventDefault();
        selectAll(lane.name);
      } else if (e.key === "Escape") {
        clearSelection(lane.name);
      }
      break;
    }
  }
}, [rpc]);
```

Update the return statement to add `tabIndex` and `onKeyDown` on the root div. Look for the `<div className="automation-panel">` container and add:
```jsx
<div className="automation-panel" tabIndex={0} onKeyDown={handleKeyDown}>
```

Also add `outline: none` to the `.automation-panel` CSS to hide the focus ring:
```css
.automation-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  overflow: hidden;
  outline: none;
}
```

- [ ] **Step 3: Verify TypeScript**

```bash
cd frontend && npx tsc --noEmit
```

Expected: clean exit (no errors).

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/AutomationPanel.tsx frontend/src/components/AutomationPanel.css
git commit -m "frontend: add keyboard handler for delete/select-all/escape in automation panel"
```

---

### Task 5: Verify

- [ ] **Step 1: TypeScript check**

```bash
cd frontend && npx tsc --noEmit
```

Expected: clean exit, no errors.

- [ ] **Step 2: Build**

```bash
cd frontend && npm run build
```

Expected: vite build succeeds, exit code 0.

- [ ] **Step 3: Commit (if any fixes were needed)**

```bash
git add -A
git commit -m "frontend: fix lint/type issues from Phase 8 interactive automation"
```
