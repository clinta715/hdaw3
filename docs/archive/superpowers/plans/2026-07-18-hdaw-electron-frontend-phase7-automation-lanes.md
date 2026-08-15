# Phase 7: Automation Lanes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tabbed bottom panel and an automation lane editor displaying track automation curves with canvas-based point editing.

**Architecture:** Bottom panel switches from flat side-by-side (mixer + piano roll) to a tabbed layout. AutomationPanel reads selected clip's parent track, fetches lanes and points via RPC, renders each lane as an 80px canvas with draggable points.

**Tech Stack:** React 19, TypeScript, Zustand, Canvas API, existing WebSocket RPC

---

### Task 1: Add automation types to RPC types

**Files:**
- Modify: `frontend/src/rpc/types.ts`

- [ ] **Add three type interfaces before `MetersPayload`**

```typescript
export interface AutomationLaneSnapshot {
  laneIndex: number;
  name: string;
  paramID: number;
  enabled: boolean;
}

export interface AutomationPointSnapshot {
  time: number;
  value: number;
}

export interface AutomatableParamSnapshot {
  slotIndex: number;
  paramIndex: number;
  name: string;
  automatable: boolean;
}
```

- [ ] **Verify types are consistent with existing patterns**

The existing types use `name: string`, `index: number`, `enabled: boolean` conventions. These match.

---

### Task 2: Create automation Zustand store

**Files:**
- Create: `frontend/src/store/automationStore.ts`

- [ ] **Write the store**

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

  fetchForTrack: (trackIndex: number, rpc: RpcClient) => Promise<void>;
  clear: () => void;
}

export const useAutomationStore = create<AutomationState>((set, get) => ({
  lanes: [],
  pointsByLane: new Map(),
  activeTrackIndex: null,
  loading: false,
  error: null,

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
    set({ lanes: [], pointsByLane: new Map(), activeTrackIndex: null, error: null });
  },
}));
```

---

### Task 3: Create BottomTabs component

**Files:**
- Create: `frontend/src/components/BottomTabs.tsx`
- Create: `frontend/src/components/BottomTabs.css`

- [ ] **Write BottomTabs.tsx**

```typescript
import { useState, ReactNode } from "react";
import "./BottomTabs.css";

interface Tab {
  id: string;
  label: string;
  content: ReactNode;
}

interface Props {
  tabs: Tab[];
  defaultTab?: string;
}

export default function BottomTabs({ tabs, defaultTab }: Props) {
  const [active, setActive] = useState(defaultTab ?? tabs[0]?.id ?? "");

  return (
    <div className="bottom-tabs">
      <div className="bt-tab-bar">
        {tabs.map((t) => (
          <button
            key={t.id}
            className={`bt-tab ${t.id === active ? "bt-tab--active" : ""}`}
            onClick={() => setActive(t.id)}
          >
            {t.label}
          </button>
        ))}
      </div>
      <div className="bt-content">
        {tabs.find((t) => t.id === active)?.content}
      </div>
    </div>
  );
}
```

- [ ] **Write BottomTabs.css**

```css
.bottom-tabs {
  display: flex;
  flex-direction: column;
  height: 100%;
  width: 100%;
}

.bt-tab-bar {
  display: flex;
  flex-shrink: 0;
  gap: 0;
  background: var(--bg-header);
  border-bottom: 1px solid var(--border);
  padding: 0 4px;
}

.bt-tab {
  padding: 4px 16px;
  background: transparent;
  border: none;
  border-bottom: 2px solid transparent;
  color: var(--text-secondary);
  font-size: 12px;
  cursor: pointer;
  white-space: nowrap;
  font-family: inherit;
}

.bt-tab:hover {
  color: var(--text-primary);
  background: rgba(255,255,255,0.04);
}

.bt-tab--active {
  color: var(--accent);
  border-bottom-color: var(--accent);
}

.bt-content {
  flex: 1;
  overflow: hidden;
  min-height: 0;
}
```

---

### Task 4: Update App.tsx and App.css for tabbed layout

**Files:**
- Modify: `frontend/src/App.tsx`
- Modify: `frontend/src/App.css`

- [ ] **Replace flat bottom-panel sections with BottomTabs in App.tsx**

```typescript
import "./App.css";
import TransportBar from "./components/TransportBar";
import TrackHeaders from "./components/TrackHeaders";
import Mixer from "./components/Mixer";
import PianoRoll from "./components/PianoRoll";
import TimelineMinimal from "./components/TimelineMinimal";
import ClipEditor from "./components/ClipEditor";
import AutomationPanel from "./components/AutomationPanel";
import BottomTabs from "./components/BottomTabs";
import { useUiStore } from "./store/uiStore";

function App() {
  const bottomTabs = [
    { id: "mixer", label: "Mixer", content: <Mixer /> },
    { id: "piano-roll", label: "Piano Roll", content: <PianoRoll /> },
    { id: "automation", label: "Automation", content: <AutomationPanel /> },
  ];

  return (
    <div className="app-shell">
      <header className="transport-bar">
        <TransportBar />
      </header>
      <aside className="track-headers">
        <TrackHeaders />
      </aside>
      <main className="timeline">
        <TimelineMinimal />
      </main>
      {useUiStore((s) => s.selectedClipId) != null && (
        <div className="clip-editor-container">
          <ClipEditor />
        </div>
      )}
      <footer className="bottom-panel">
        <BottomTabs tabs={bottomTabs} defaultTab="mixer" />
      </footer>
    </div>
  );
}

export default App;
```

- [ ] **Update App.css bottom-panel styles**

Replace the existing `.bottom-panel` and its children with:

```css
.bottom-panel {
  grid-area: bottom;
  background: var(--bg-panel);
  border-top: 1px solid var(--border);
  overflow: hidden;
  min-height: 0;
}

/* Remove or update these old selectors: .mixer, .piano-roll */
.mixer { height: 100%; overflow-x: auto; }
.piano-roll { height: 100%; overflow: auto; }
```

---

### Task 5: Update main.tsx to refresh automation on treeChanged

**Files:**
- Modify: `frontend/src/main.tsx`

- [ ] **Import automationStore and add refresh in setupSubscriptions**

Add `import { useAutomationStore } from "./store/automationStore";` at the top.

In the `notify.treeChanged` handler, after `syncSnapshot`, also refresh automation:

```typescript
cleanups.push(rpc.onNotification("notify.treeChanged", () => {
  useProjectStore.getState().syncSnapshot(rpc).catch(() => {});
  const activeIdx = useAutomationStore.getState().activeTrackIndex;
  if (activeIdx != null) {
    useAutomationStore.getState().fetchForTrack(activeIdx, rpc).catch(() => {});
  }
}));
```

---

### Task 6: Create AutomationLaneCanvas component

**Files:**
- Create: `frontend/src/components/AutomationLaneCanvas.tsx`
- Create: `frontend/src/components/AutomationLaneCanvas.css`

This is the most complex component. It renders a canvas with grid, curve, points, playhead, and loop region.

- [ ] **Write AutomationLaneCanvas.tsx**

```typescript
import { useEffect, useRef, useCallback } from "react";
import { AutomationPointSnapshot } from "../rpc/types";
import { rpc } from "../rpc";
import { useTransportStore } from "../store/transportStore";
import "./AutomationLaneCanvas.css";

interface Props {
  laneName: string;
  trackIndex: number;
  points: AutomationPointSnapshot[];
  viewRange: number;
}

const CANVAS_H = 80;
const PAD = 8;
const POINT_RADIUS = 6;

function clamp(v: number, min: number, max: number) {
  return Math.min(max, Math.max(min, v));
}

export default function AutomationLaneCanvas({ laneName, trackIndex, points, viewRange }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const dragRef = useRef<{ time: number; value: number } | null>(null);
  const ptsRef = useRef<AutomationPointSnapshot[]>(points);
  ptsRef.current = points;

  const transport = useTransportStore((s) => s.transport);
  const bpm = transport.bpm;
  const playheadBeats = transport.currentTimeSeconds * (bpm / 60);
  const showLoop = transport.isLooping;

  const toCanvas = useCallback((clientX: number, clientY: number) => {
    const canvas = canvasRef.current;
    if (!canvas) return { time: 0, value: 0 };
    const rect = canvas.getBoundingClientRect();
    const w = rect.width;
    const h = rect.height;
    const plotW = w - PAD * 2;
    const plotH = h - PAD * 2;
    const x = clientX - rect.left;
    const y = clientY - rect.top;
    const time = clamp(((x - PAD) / plotW) * viewRange, 0, viewRange);
    const value = clamp(1 - (y - PAD) / plotH, 0, 1);
    return { time, value };
  }, [viewRange]);

  const findNearest = useCallback((clientX: number, clientY: number) => {
    const canvas = canvasRef.current;
    if (!canvas) return -1;
    const rect = canvas.getBoundingClientRect();
    const w = rect.width;
    const h = rect.height;
    const plotW = w - PAD * 2;
    const plotH = h - PAD * 2;
    const mx = clientX - rect.left;
    const my = clientY - rect.top;
    for (let i = 0; i < ptsRef.current.length; i++) {
      const px = PAD + (ptsRef.current[i].time / viewRange) * plotW;
      const py = h - PAD - ptsRef.current[i].value * plotH;
      const dx = mx - px;
      const dy = my - py;
      if (dx * dx + dy * dy <= (POINT_RADIUS + 4) * (POINT_RADIUS + 4)) return i;
    }
    return -1;
  }, [viewRange]);

  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const idx = findNearest(e.clientX, e.clientY);
    if (idx >= 0) {
      // Start drag of existing point
      const pt = ptsRef.current[idx];
      dragRef.current = { time: pt.time, value: pt.value };
    } else {
      // Add new point
      const { time, value } = toCanvas(e.clientX, e.clientY);
      rpc.call("project.addAutomationPoint", { trackIndex, lane: laneName, time, value }).catch(() => {});
    }
  }, [trackIndex, laneName, findNearest, toCanvas]);

  const handleWindowMouseMove = useCallback((e: MouseEvent) => {
    const drag = dragRef.current;
    if (!drag) return;
    const { time, value } = toCanvas(e.clientX, e.clientY);
    // Update point: remove old, add new at (time, value)
    rpc.call("project.setAutomationPointValue", { trackIndex, lane: laneName, time: drag.time, value }).catch(() => {});
    drag.time = time;
    drag.value = value;
  }, [trackIndex, laneName, toCanvas]);

  const handleWindowMouseUp = useCallback(() => {
    if (!dragRef.current) return;
    dragRef.current = null;
  }, []);

  useEffect(() => {
    window.addEventListener("mousemove", handleWindowMouseMove);
    window.addEventListener("mouseup", handleWindowMouseUp);
    return () => {
      window.removeEventListener("mousemove", handleWindowMouseMove);
      window.removeEventListener("mouseup", handleWindowMouseUp);
    };
  }, [handleWindowMouseMove, handleWindowMouseUp]);

  // Canvas paint
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    ctx.scale(dpr, dpr);

    ctx.clearRect(0, 0, w, h);

    const plotW = w - PAD * 2;
    const plotH = h - PAD * 2;

    // Background
    ctx.fillStyle = "var(--bg-widget)";
    ctx.fillRect(0, 0, w, h);

    // Horizontal grid (value lines)
    for (let v = 0; v <= 1; v += 0.25) {
      const y = h - PAD - v * plotH;
      ctx.strokeStyle = v === 0.5 ? "rgba(255,255,255,0.12)" : "rgba(255,255,255,0.05)";
      ctx.lineWidth = v === 0.5 ? 1 : 0.5;
      ctx.beginPath();
      ctx.moveTo(PAD, y);
      ctx.lineTo(w - PAD, y);
      ctx.stroke();
    }

    // Vertical grid (bar lines every 4 beats)
    for (let b = 0; b <= Math.ceil(viewRange); b += 4) {
      const x = PAD + (b / viewRange) * plotW;
      const isBar = b % 4 === 0;
      ctx.strokeStyle = isBar ? "rgba(255,255,255,0.07)" : "rgba(255,255,255,0.03)";
      ctx.lineWidth = isBar ? 1 : 0.5;
      ctx.beginPath();
      ctx.moveTo(x, PAD);
      ctx.lineTo(x, h - PAD);
      ctx.stroke();
    }

    // Loop region
    if (showLoop) {
      const lx = PAD + (transport.loopStart / viewRange) * plotW;
      const rx = PAD + (transport.loopEnd / viewRange) * plotW;
      ctx.fillStyle = "rgba(6, 182, 212, 0.08)";
      ctx.fillRect(Math.max(lx, PAD), PAD, Math.min(rx, w - PAD) - Math.max(lx, PAD), h - PAD * 2);
    }

    // Playhead
    const phx = PAD + (playheadBeats / viewRange) * plotW;
    if (phx >= PAD && phx <= w - PAD) {
      ctx.strokeStyle = "var(--accent)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(phx, PAD);
      ctx.lineTo(phx, h - PAD);
      ctx.stroke();
    }

    // Sort points by time
    const pts = [...points].sort((a, b) => a.time - b.time);
    if (pts.length === 0) return;

    // Curve fill
    ctx.beginPath();
    ctx.moveTo(PAD + (pts[0].time / viewRange) * plotW, h - PAD - 0 * plotH);
    for (const p of pts) {
      const x = PAD + (p.time / viewRange) * plotW;
      const y = h - PAD - p.value * plotH;
      ctx.lineTo(x, y);
    }
    const lastX = PAD + (pts[pts.length - 1].time / viewRange) * plotW;
    ctx.lineTo(lastX, h - PAD - 0 * plotH);
    ctx.closePath();
    ctx.fillStyle = "var(--automation-fill, rgba(6, 182, 212, 0.16))";
    ctx.fill();

    // Curve line
    ctx.beginPath();
    for (let i = 0; i < pts.length; i++) {
      const x = PAD + (pts[i].time / viewRange) * plotW;
      const y = h - PAD - pts[i].value * plotH;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = "var(--automation-line, rgba(6, 182, 212, 0.78))";
    ctx.lineWidth = 2;
    ctx.stroke();

    // Points
    for (const p of pts) {
      const x = PAD + (p.time / viewRange) * plotW;
      const y = h - PAD - p.value * plotH;
      ctx.fillStyle = "#fff";
      ctx.beginPath();
      ctx.arc(x, y, POINT_RADIUS, 0, Math.PI * 2);
      ctx.fill();
      ctx.strokeStyle = "var(--automation-line, rgba(6, 182, 212, 0.78))";
      ctx.lineWidth = 2;
      ctx.stroke();
    }
  }, [points, viewRange, playheadBeats, transport.loopStart, transport.loopEnd, showLoop]);

  return (
    <canvas
      ref={canvasRef}
      className="alc-canvas"
      style={{ width: "100%", height: CANVAS_H, cursor: "crosshair" }}
      onMouseDown={handleMouseDown}
      onDoubleClick={(e) => {
        const idx = findNearest(e.clientX, e.clientY);
        if (idx >= 0 && ptsRef.current[idx]) {
          rpc.call("project.removeAutomationPoint", {
            trackIndex, lane: laneName, time: ptsRef.current[idx].time
          }).catch(() => {});
        }
      }}
    />
  );
}
```

- [ ] **Write AutomationLaneCanvas.css**

```css
.alc-canvas {
  display: block;
  border: 1px solid var(--border);
  border-radius: 4px;
}
```

---

### Task 7: Create AutomationPanel component

**Files:**
- Create: `frontend/src/components/AutomationPanel.tsx`
- Create: `frontend/src/components/AutomationPanel.css`

- [ ] **Write AutomationPanel.tsx**

```typescript
import { useEffect, useState, useRef, useCallback } from "react";
import { useUiStore } from "../store/uiStore";
import { useProjectStore } from "../store/projectStore";
import { useAutomationStore } from "../store/automationStore";
import { rpc } from "../rpc";
import { AutomatableParamSnapshot } from "../rpc/types";
import AutomationLaneCanvas from "./AutomationLaneCanvas";
import "./AutomationPanel.css";

const VIEW_RANGE = 32; // 8 bars

export default function AutomationPanel() {
  const selectedClipId = useUiStore((s) => s.selectedClipId);
  const snapshot = useProjectStore((s) => s.snapshot);
  const { lanes, pointsByLane, activeTrackIndex, loading } = useAutomationStore();
  const [menuOpen, setMenuOpen] = useState(false);
  const [automatableParams, setAutomatableParams] = useState<AutomatableParamSnapshot[]>([]);
  const menuRef = useRef<HTMLDivElement>(null);

  // Resolve track from selected clip
  const trackIndex = selectedClipId != null
    ? snapshot?.clips.find((c) => c.clipId === selectedClipId)?.trackIndex
    : null;

  const track = trackIndex != null
    ? snapshot?.tracks.find((t) => t.index === trackIndex)
    : null;

  // Fetch automation when track selection changes
  useEffect(() => {
    if (trackIndex != null) {
      useAutomationStore.getState().fetchForTrack(trackIndex, rpc);
    } else {
      useAutomationStore.getState().clear();
    }
  }, [trackIndex]);

  // Close dropdown on outside click
  useEffect(() => {
    if (!menuOpen) return;
    const handler = (e: MouseEvent) => {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) {
        setMenuOpen(false);
      }
    };
    document.addEventListener("mousedown", handler);
    return () => document.removeEventListener("mousedown", handler);
  }, [menuOpen]);

  const openAddMenu = useCallback(async () => {
    if (activeTrackIndex == null) return;
    try {
      const params = await rpc.call("read.getAutomatableParams", { trackIndex: activeTrackIndex }) as AutomatableParamSnapshot[];
      setAutomatableParams(params);
      setMenuOpen(true);
    } catch (err) {
      console.warn("Failed to fetch automatable params:", err);
    }
  }, [activeTrackIndex]);

  const addLane = useCallback(async (name: string) => {
    if (activeTrackIndex == null) return;
    try {
      await rpc.call("project.addAutomationLane", { trackIndex: activeTrackIndex, laneName: name });
      setMenuOpen(false);
    } catch (err) {
      console.warn("Failed to add lane:", err);
    }
  }, [activeTrackIndex]);

  const removeLane = useCallback((laneName: string) => {
    if (activeTrackIndex == null) return;
    rpc.call("project.removeAutomationLane", { trackIndex: activeTrackIndex, laneName }).catch(() => {});
  }, [activeTrackIndex]);

  const toggleEnabled = useCallback((laneName: string, enabled: boolean) => {
    if (activeTrackIndex == null) return;
    rpc.call("project.setAutomationEnabled", { trackIndex: activeTrackIndex, lane: laneName, enabled }).catch(() => {});
  }, [activeTrackIndex]);

  if (trackIndex == null) {
    return (
      <div className="automation-panel ap-empty">
        Select a clip to view automation
      </div>
    );
  }

  return (
    <div className="automation-panel">
      <div className="ap-header">
        <span className="ap-track-label">
          Track: {track?.name ?? `#${trackIndex}`}
        </span>
        <div className="ap-add-btn-wrap" ref={menuRef}>
          <button className="ap-add-btn" onClick={openAddMenu}>+ Add Lane</button>
          {menuOpen && (
            <div className="ap-dropdown">
              <div className="ap-dropdown-header">Track Parameters</div>
              {automatableParams
                .filter((p) => p.slotIndex === -1)
                .map((p) => (
                  <button key={p.name} className="ap-dropdown-item"
                    onClick={() => addLane(p.name)}
                    disabled={lanes.some((l) => l.name === p.name)}>
                    {p.name}
                  </button>
                ))}
              {automatableParams.some((p) => p.slotIndex >= 0) && (
                <>
                  <div className="ap-dropdown-header">Plugin Parameters</div>
                  {automatableParams
                    .filter((p) => p.slotIndex >= 0 && p.automatable)
                    .map((p) => (
                      <button key={`${p.slotIndex}-${p.paramIndex}`}
                        className="ap-dropdown-item"
                        onClick={() => addLane(p.name)}
                        disabled={lanes.some((l) => l.name === p.name)}>
                        Slot {p.slotIndex + 1}: {p.name}
                      </button>
                    ))}
                </>
              )}
            </div>
          )}
        </div>
      </div>

      <div className="ap-lanes">
        {loading && <div className="ap-loading">Loading...</div>}
        {!loading && lanes.length === 0 && (
          <div className="ap-empty">No automation lanes. Click "+ Add Lane" to create one.</div>
        )}
        {!loading && lanes.map((lane) => {
          const pts = pointsByLane.get(lane.name) ?? [];
          return (
            <div key={lane.name} className="ap-lane">
              <div className="ap-lane-header">
                <label className="ap-lane-name">{lane.name}</label>
                <label className="ap-lane-toggle">
                  <input
                    type="checkbox"
                    checked={lane.enabled}
                    onChange={(e) => toggleEnabled(lane.name, e.target.checked)}
                  />
                  <span>Auto</span>
                </label>
                <button
                  className="ap-lane-remove"
                  title="Remove lane"
                  onClick={() => removeLane(lane.name)}
                >
                  ×
                </button>
              </div>
              <AutomationLaneCanvas
                laneName={lane.name}
                trackIndex={trackIndex}
                points={pts}
                viewRange={VIEW_RANGE}
              />
            </div>
          );
        })}
      </div>
    </div>
  );
}
```

- [ ] **Write AutomationPanel.css**

```css
.automation-panel {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: var(--bg-panel);
  font-size: 12px;
}

.ap-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 4px 8px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.ap-track-label {
  color: var(--text-primary);
  font-weight: 600;
  font-size: 13px;
}

.ap-add-btn-wrap {
  position: relative;
}

.ap-add-btn {
  padding: 2px 10px;
  background: var(--accent);
  color: #fff;
  border: none;
  border-radius: 3px;
  cursor: pointer;
  font-size: 12px;
  font-family: inherit;
}

.ap-add-btn:hover {
  background: var(--accent-bright, #22d3ee);
}

.ap-dropdown {
  position: absolute;
  top: 100%;
  left: 0;
  z-index: 100;
  min-width: 200px;
  max-height: 300px;
  overflow-y: auto;
  background: var(--bg-elevated, #2e2e32);
  border: 1px solid var(--border);
  border-radius: 4px;
  padding: 4px 0;
  margin-top: 2px;
}

.ap-dropdown-header {
  padding: 4px 10px;
  color: var(--text-muted, #787880);
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.ap-dropdown-item {
  display: block;
  width: 100%;
  text-align: left;
  padding: 4px 10px;
  background: transparent;
  border: none;
  color: var(--text-primary);
  cursor: pointer;
  font-size: 12px;
  font-family: inherit;
}

.ap-dropdown-item:hover {
  background: rgba(255,255,255,0.06);
}

.ap-dropdown-item:disabled {
  opacity: 0.4;
  cursor: default;
}

.ap-lanes {
  flex: 1;
  overflow-y: auto;
  padding: 6px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.ap-lane {
  background: var(--bg-widget);
  border: 1px solid var(--border);
  border-radius: 4px;
  overflow: hidden;
}

.ap-lane-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 3px 6px;
  border-bottom: 1px solid var(--border);
  background: var(--bg-header);
}

.ap-lane-name {
  flex: 1;
  color: var(--text-primary);
  font-weight: 500;
  font-size: 11px;
}

.ap-lane-toggle {
  display: flex;
  align-items: center;
  gap: 3px;
  color: var(--text-secondary);
  font-size: 10px;
  cursor: pointer;
}

.ap-lane-toggle input {
  accent-color: var(--accent);
}

.ap-lane-remove {
  background: transparent;
  border: none;
  color: var(--text-muted);
  cursor: pointer;
  padding: 0 4px;
  font-size: 14px;
  line-height: 1;
}

.ap-lane-remove:hover {
  color: var(--danger);
}

.ap-empty {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: var(--text-muted);
  font-size: 13px;
}

.ap-loading {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: var(--text-secondary);
}
```

---

### Task 8: Add CSS variable fallbacks in App.tsx

The automation panel uses CSS variables like `--automation-fill` and `--automation-line` that aren't defined in the theme yet.

- [ ] **Add automation CSS variables to the theme**

Read `frontend/src/theme.ts` and add these to the `theme` object:

```typescript
automationFill: "rgba(6, 182, 212, 0.16)",
automationLine: "rgba(6, 182, 212, 0.78)",
```

---

### Verify

```bash
cd frontend && npx tsc --noEmit && npx vite build
```

---

### Out of scope (deferred)

- Automation zoom/scroll controls on canvas
- FX chain tab
- Multi-point selection / bulk edit
- Right-click context menu on points
- Keyboard shortcuts (Delete, Ctrl+C/X/V)
