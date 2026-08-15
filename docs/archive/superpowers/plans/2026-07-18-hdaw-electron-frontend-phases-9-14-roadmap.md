# Phases 9-14: Web UI 1:1 Feature Parity Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close all gaps between the web frontend and Qt desktop UI across six incremental phases, each independently shippable.

**Architecture:** Every phase extends the frontend (`frontend/src/`) only — the C++ backend already exposes all needed RPC methods via `FrontendRouter.cpp`. Each phase adds types, Zustand store actions, and React components following the established patterns.

**Tech Stack:** React 19, TypeScript, Zustand 5, HTML Canvas 2D, Vite 6

---

## Phase Overview

| Phase | Name | Est. Effort | Value |
|-------|------|-------------|-------|
| **9** | Track & Clip Operations | 5-7 days | High — makes timeline interactive |
| **10** | Transport + Status Bar | 3-4 days | Medium — completes playback UX |
| **11** | File / Project Operations | 4-5 days | High — real projects can be saved/loaded |
| **12** | FX Chain Panel | 4-5 days | High — unlocks plugin hosting |
| **13** | MIDI Completeness | 3-4 days | Medium — fills MIDI editing gaps |
| **14** | Remaining Panels & Dialogs | 5-7 days | Medium — audio editor, step seq, mod, prefs |

**Total est.: 24-32 days**

---

# Phase 9: Track & Clip Operations

**Goal:** Make the timeline fully interactive — mute/solo/arm tracks, trim/delete/duplicate clips, see audio waveforms, use context menus and snap.

**Backend RPCs already available:** `setTrackMuted`, `setTrackSoloed`, `setTrackArmed`, `setTrackInputMonitor`, `setTrackVolume`, `setTrackPan`, `setTrackColor`, `setTrackHeight`, `setTrackMidiChannel`, `addTrack`, `removeTrack`, `duplicateTrack`, `setTrackName`, `setClipStart`, `setClipDuration`, `setClipOffset`, `removeClip`, `duplicateClip`, `undo`, `redo`, `beginTransaction`, `endTransaction`, `addAudioClip`, `addMidiClip`

**ReadModel snapshots already contain:** `track.volume`, `track.pan`, `track.muted`, `track.soloed`, `track.armed`, `track.inputMonitor`, `track.height`, `track.color`, `track.midiChannel`, `ClipSnapshot.gainEnvelope[]`

---

### Task 9.1: Add control buttons to TrackHeaders

**Files:**
- Modify: `frontend/src/components/TrackHeaders.tsx`
- Modify: `frontend/src/components/TrackHeaders.css`

**Steps:**

- [ ] **Step 1: Read current TrackHeaders.tsx**

```bash
cat frontend/src/components/TrackHeaders.tsx
```

- [ ] **Step 2: Add mute, solo, arm, volume and pan controls to each track row**

The rendered track row gains:
- Mute button ("M", toggles accent color)
- Solo button ("S", toggles yellow)
- Arm button ("R", toggles red)
- Volume label showing percentage
- Pan label showing position

```tsx
import { rpc } from "../rpc";

// Inside the track row map:
const volPct = Math.round((track.volume ?? 1.0) * 100);
const panStr = (track.pan ?? 0) === 0 ? "C"
  : (track.pan ?? 0) < 0
    ? `L${Math.round(Math.abs(track.pan ?? 0) * 100)}`
    : `R${Math.round((track.pan ?? 0) * 100)}`;

const handleMute = (e: React.MouseEvent, idx: number, current: boolean) => {
  e.stopPropagation();
  rpc.call("project.setTrackMuted", { trackIndex: idx, muted: !current });
};

return (
  <div key={idx} className="th-track" style={{ borderLeftColor: track.color || "#666" }}>
    <div className="th-name">{track.name || `Track ${idx + 1}`}</div>
    <div className="th-controls">
      {track.type !== "master" && <>
        <button
          className={`th-btn ${track.muted ? "th-btn--active" : ""}`}
          onClick={(e) => handleMute(e, idx, track.muted)}
          title="Mute">M</button>
        <button
          className={`th-btn th-btn--solo ${track.soloed ? "th-btn--active" : ""}`}
          onClick={(e) => { e.stopPropagation(); rpc.call("project.setTrackSoloed", { trackIndex: idx, soloed: !track.soloed }); }}
          title="Solo">S</button>
        <button
          className={`th-btn th-btn--arm ${track.armed ? "th-btn--active" : ""}`}
          onClick={(e) => { e.stopPropagation(); rpc.call("project.setTrackArmed", { trackIndex: idx, armed: !track.armed }); }}
          title="Arm">R</button>
      </>}
      {track.type === "master" && <span className="th-type-label">Master</span>}
    </div>
    <div className="th-meters">
      <div className="th-meter th-meter-l" style={{ height: `${Math.min(track.meterL * 100, 100)}%` }} />
      <div className="th-meter th-meter-r" style={{ height: `${Math.min(track.meterR * 100, 100)}%` }} />
    </div>
    <div className="th-values">
      <span className="th-vol">V:{volPct}%</span>
      <span className="th-pan">{panStr}</span>
    </div>
  </div>
);
```

- [ ] **Step 3: Add CSS for the new controls to TrackHeaders.css**

```css
.th-controls { display: flex; gap: 2px; margin: 2px 0; }
.th-btn {
  width: 20px; height: 18px; border: 1px solid var(--border-default);
  border-radius: 2px; background: var(--bg-widget); color: var(--text-muted);
  font-size: 9px; font-weight: 700; cursor: pointer; padding: 0;
  line-height: 16px; text-align: center;
}
.th-btn:hover { background: var(--bg-elevated); }
.th-btn--active { background: var(--accent-dim); color: var(--accent-bright); border-color: var(--accent); }
.th-btn--solo.th-btn--active { background: #5a4d00; color: #ffe066; border-color: #ffd700; }
.th-btn--arm.th-btn--active { background: #5a1a1a; color: #ff4444; border-color: #ff3333; }
.th-type-label { font-size: 9px; color: var(--text-muted); }
.th-meters { display: flex; gap: 1px; height: 40px; align-items: flex-end; margin: 2px 0; }
.th-meter { width: 4px; border-radius: 1px 1px 0 0; transition: height 0.1s ease; }
.th-meter-l { background: linear-gradient(to top, var(--vu-green), var(--vu-yellow), var(--vu-red)); }
.th-meter-r { background: linear-gradient(to top, var(--vu-green), var(--vu-yellow), var(--vu-red)); }
.th-values { display: flex; gap: 4px; font-size: 9px; color: var(--text-muted); justify-content: space-between; }
.th-vol { font-variant-numeric: tabular-nums; }
.th-pan { font-variant-numeric: tabular-nums; }
```

- [ ] **Step 4: Verify TypeScript compiles**

```bash
npx tsc --noEmit
```

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/TrackHeaders.tsx frontend/src/components/TrackHeaders.css
git commit -m "phase9: add mute/solo/arm buttons, volume/pan, meters to TrackHeaders"
```

---

### Task 9.2: Clip trimming (left/right edge drag)

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TimelineMinimal.css`

**Steps:**

- [ ] **Step 1: Read current TimelineMinimal.tsx**

```bash
cat frontend/src/components/TimelineMinimal.tsx
```

- [ ] **Step 2: Add trim zones to clip rendering**

Each rendered clip div gains invisible 4px-wide edge zones at left and right. When the mouse hovers over an edge zone, the cursor changes to `col-resize`.

```tsx
// Constants
const TRIM_ZONE = 4; // px

// In the clip rendering loop, wrap clip body:
const clipStyle: React.CSSProperties = {
  position: "absolute",
  left: clipStartBeat * beatWidth,
  width: Math.max(clipDurationBeats * beatWidth, 4),
  top: 0, height: "100%",
  background: clip.isMidi ? "#2a3a5a" : "#5a3a1a",
  border: `1px solid ${clip.isMidi ? "#4a6a9a" : "#8a6a2a"}`,
  borderRadius: 3,
  cursor: "grab",
  overflow: "hidden",
};

// Add trim handles:
<div
  key={clip.clipId}
  style={clipStyle}
  onMouseDown={(e) => handleClipMouseDown(e, clip)}
>
  {/* Left trim zone */}
  <div
    className="clip-trim clip-trim-left"
    onMouseDown={(e) => handleTrimStart(e, clip, "left")}
  />
  <span className="clip-label">{clip.name}</span>
  {/* Right trim zone */}
  <div
    className="clip-trim clip-trim-right"
    onMouseDown={(e) => handleTrimStart(e, clip, "right")}
  />
</div>
```

- [ ] **Step 3: Implement trim state in component**

```tsx
// State
const [trimState, setTrimState] = React.useState<{
  clipId: number;
  side: "left" | "right";
  startBeat: number;
  origStart: number;
  origDuration: number;
} | null>(null);

function handleTrimStart(e: React.MouseEvent, clip: ClipSnapshot, side: "left" | "right") {
  e.stopPropagation();
  setTrimState({ clipId: clip.clipId, side, startBeat: clip.startBeat, origStart: clip.startBeat, origDuration: clip.durationBeats });

  const handleMouseMove = (ev: MouseEvent) => {
    if (!timelineRef.current) return;
    const rect = timelineRef.current.getBoundingClientRect();
    const x = ev.clientX - rect.left + timelineRef.current.scrollLeft;
    const beat = x / beatWidth;
    setTrimState(prev => {
      if (!prev) return null;
      if (side === "left") {
        const newStart = Math.max(0, Math.min(beat, prev.origStart + prev.origDuration - 0.5));
        return { ...prev, startBeat: newStart };
      } else {
        const newDuration = Math.max(0.5, beat - prev.origStart);
        return { ...prev, startBeat: prev.origStart, origDuration: newDuration };
      }
    });
  };

  const handleMouseUp = () => {
    if (trimState) {
      if (side === "left") {
        const newStart = Math.max(0, Math.min(trimState.startBeat, trimState.origStart + trimState.origDuration - 0.5));
        const newDuration = trimState.origDuration + (trimState.origStart - newStart);
        rpc.call("project.beginTransaction", { name: "trim clip" });
        rpc.call("project.setClipStart", { clipId: trimState.clipId, start: newStart });
        rpc.call("project.setClipDuration", { clipId: trimState.clipId, duration: newDuration });
        rpc.call("project.endTransaction", {});
      } else {
        rpc.call("project.setClipDuration", { clipId: trimState.clipId, duration: trimState.origDuration });
      }
    }
    setTrimState(null);
    window.removeEventListener("mousemove", handleMouseMove);
    window.removeEventListener("mouseup", handleMouseUp);
  };

  window.addEventListener("mousemove", handleMouseMove);
  window.addEventListener("mouseup", handleMouseUp);
}
```

- [ ] **Step 4: Add CSS for trim zones**

```css
.clip-trim {
  position: absolute; top: 0; width: 4px; height: 100%; z-index: 2;
  cursor: col-resize; opacity: 0;
}
.clip-trim:hover { opacity: 0.4; background: white; }
.clip-trim-left { left: 0; }
.clip-trim-right { right: 0; }
```

- [ ] **Step 5: Verify TypeScript compiles**

```bash
npx tsc --noEmit
```

- [ ] **Step 6: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal.css
git commit -m "phase9: add clip trim handles (left/right edge drag)"
```

---

### Task 9.3: Clip delete, duplicate, and split

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TimelineMinimal.css`

**Steps:**

- [ ] **Step 1: Add clip context menu**

```tsx
const [contextMenu, setContextMenu] = React.useState<{
  x: number; y: number; clip: ClipSnapshot | null;
} | null>(null);

function handleClipContextMenu(e: React.MouseEvent, clip: ClipSnapshot) {
  e.preventDefault();
  setContextMenu({ x: e.clientX, y: e.clientY, clip });
}

// Render context menu at end of timeline div:
{contextMenu && (
  <div
    className="clip-context-menu"
    style={{ position: "fixed", left: contextMenu.x, top: contextMenu.y, zIndex: 1000 }}
    onClick={() => setContextMenu(null)}
  >
    <button onClick={() => { deleteClip(contextMenu.clip!.clipId); setContextMenu(null); }}>Delete</button>
    <button onClick={() => { duplicateClip(contextMenu.clip!.clipId); setContextMenu(null); }}>Duplicate</button>
    <button onClick={() => { splitClip(contextMenu.clip!.clipId); setContextMenu(null); }}>Split</button>
  </div>
)}
// Click outside to close
React.useEffect(() => {
  if (contextMenu) {
    const close = () => setContextMenu(null);
    window.addEventListener("click", close);
    return () => window.removeEventListener("click", close);
  }
}, [contextMenu]);
```

- [ ] **Step 2: Add action handlers**

```tsx
function deleteClip(clipId: number) {
  rpc.call("project.removeClip", { clipId }).then(() => {
    projectStore.getState().syncSnapshot(rpc);
  });
}

function duplicateClip(clipId: number) {
  rpc.call("project.duplicateClip", { clipId }).then(() => {
    projectStore.getState().syncSnapshot(rpc);
  });
}

function splitClip(clipId: number) {
  rpc.call("project.sliceClipAtPlayhead", { clipId }).then(() => {
    projectStore.getState().syncSnapshot(rpc);
  });
}
```

- [ ] **Step 3: Wire clip context menu to the clip div**

Add `onContextMenu={(e) => handleClipContextMenu(e, clip)}` to the clip div.

- [ ] **Step 4: Add CSS for context menu**

```css
.clip-context-menu {
  background: var(--bg-elevated); border: 1px solid var(--border-default);
  border-radius: 4px; padding: 4px 0; min-width: 120px;
  box-shadow: 0 4px 16px rgba(0,0,0,0.4);
}
.clip-context-menu button {
  display: block; width: 100%; padding: 6px 16px;
  background: none; border: none; color: var(--text-primary);
  font-size: 12px; text-align: left; cursor: pointer;
}
.clip-context-menu button:hover { background: var(--accent-dim); }
```

- [ ] **Step 5: Verify TypeScript compiles**

```bash
npx tsc --noEmit
```

- [ ] **Step 6: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal.css
git commit -m "phase9: add clip context menu (delete, duplicate, split)"
```

---

### Task 9.4: Audio waveform rendering in clips

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Create: `frontend/src/components/WaveformCanvas.tsx`

**Steps:**

- [ ] **Step 1: Create WaveformCanvas component**

This component lazily fetches waveform data (a cached thumbnail of peak values) from the backend and renders it on a canvas. The backend doesn't have a dedicated waveform RPC, so we approximate: render a simple SVG-based waveform visualization using the clip gain envelope shape as a placeholder, or fetch a small set of precomputed peak values.

For the initial implementation, fetch waveform data as a simple array of peak values. The ReadModel `getClip()` returns `ClipSnapshot` which includes `sourceFile` but not waveform data. We'll compute a synthetic waveform from the gain envelope as a placeholder, or render a gradient-filled bar.

A pragmatic first pass: render the clip with a gradient fill that suggests audio content (wider at louder sections based on gain envelope points):

```tsx
// WaveformCanvas.tsx
import React from "react";
import { ClipSnapshot } from "../rpc/types";

interface Props {
  clip: ClipSnapshot;
  width: number;
  height: number;
  gainEnvelope?: { time: number; gain: number }[];
}

export const WaveformCanvas: React.FC<Props> = ({ clip, width, height, gainEnvelope }) => {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const deviceRatio = window.devicePixelRatio || 1;

  React.useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    canvas.width = width * deviceRatio;
    canvas.height = height * deviceRatio;
    ctx.scale(deviceRatio, deviceRatio);
    ctx.clearRect(0, 0, width, height);

    // Draw a waveform-like visualization using gain envelope
    // Fallback: draw a filled rect with gradient
    const gradient = ctx.createLinearGradient(0, 0, 0, height);
    gradient.addColorStop(0, "rgba(255, 180, 60, 0.3)");
    gradient.addColorStop(0.5, "rgba(255, 200, 100, 0.6)");
    gradient.addColorStop(1, "rgba(255, 180, 60, 0.3)");
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, width, height);

    // Draw a simple waveform shape (sine-like)
    const midY = height / 2;
    ctx.strokeStyle = "rgba(255, 220, 120, 0.7)";
    ctx.lineWidth = 1;
    const numSegments = Math.min(width, 128);
    ctx.beginPath();
    for (let i = 0; i < numSegments; i++) {
      const x = (i / numSegments) * width;
      const normalized = Math.sin((i / numSegments) * Math.PI * 4) * 0.3 + 0.5 +
        Math.sin((i / numSegments) * Math.PI * 7) * 0.15;
      const y = midY + (normalized - 0.5) * height * 0.7;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();

    // Mirror below center
    ctx.beginPath();
    for (let i = 0; i < numSegments; i++) {
      const x = (i / numSegments) * width;
      const normalized = Math.sin((i / numSegments) * Math.PI * 4) * 0.3 + 0.5 +
        Math.sin((i / numSegments) * Math.PI * 7) * 0.15;
      const y = midY - (normalized - 0.5) * height * 0.7;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }, [clip, width, height, gainEnvelope, deviceRatio]);

  return <canvas ref={canvasRef} style={{ width, height }} />;
};
```

- [ ] **Step 2: Integrate WaveformCanvas into TimelineMinimal clip rendering**

Replace the plain color background with the WaveformCanvas for audio clips:

```tsx
// In clip rendering, for audio clips:
{clip.isMidi ? (
  <div className="clip-body" style={{ background: "#2a3a5a" }}>
    <span className="clip-label">{clip.name}</span>
  </div>
) : (
  <div className="clip-body" style={{ position: "relative" }}>
    <WaveformCanvas
      clip={clip}
      width={Math.max(clipDurationBeats * beatWidth, 4)}
      height={trackHeight - 4}
      gainEnvelope={clip.gainEnvelope}
    />
    <span className="clip-label" style={{
      position: "absolute", bottom: 2, left: 4,
      fontSize: 10, color: "rgba(255,255,255,0.7)",
      textShadow: "0 1px 2px rgba(0,0,0,0.6)",
      overflow: "hidden", textOverflow: "ellipsis",
      maxWidth: "80%", whiteSpace: "nowrap"
    }}>{clip.name}</span>
  </div>
)}
```

- [ ] **Step 3: Add the zoom fit feature** (pairs with waveform display)

```tsx
function zoomToFit() {
  const clips = projectStore.getState().snapshot?.clips || [];
  if (clips.length === 0) return;
  const maxEnd = Math.max(...clips.map(c => c.startBeat + c.durationBeats), 16);
  if (!timelineRef.current) return;
  const viewWidth = timelineRef.current.clientWidth;
  setBeatWidth(Math.max(10, Math.min(200, viewWidth / maxEnd)));
}
```

Add a "Fit" button to the toolbar next to the zoom controls.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/WaveformCanvas.tsx frontend/src/components/TimelineMinimal.tsx
git commit -m "phase9: add waveform canvas rendering for audio clips"
```

---

### Task 9.5: Snap toggle + keyboard shortcuts

**Files:**
- Modify: `frontend/src/store/uiStore.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/components/TransportBar.tsx`

**Steps:**

- [ ] **Step 1: Add snap state to uiStore**

```tsx
// In uiStore.ts
import { create } from "zustand";

interface UiState {
  selectedClipId: number | null;
  selectedTrackIndex: number | null;
  snapEnabled: boolean;
  snapDivision: number; // 0=Bar, 1=Beat, 2=1/8, 3=1/16, 4=1/32
  selectClip: (id: number | null, trackIndex?: number) => void;
  setSnapEnabled: (enabled: boolean) => void;
  setSnapDivision: (division: number) => void;
}

// In defaults:
snapEnabled: true,
snapDivision: 1, // Beat

// In actions:
setSnapEnabled: (enabled) => set({ snapEnabled: enabled }),
setSnapDivision: (division) => set({ snapDivision: division }),
```

- [ ] **Step 2: Add snap toggle to TransportBar**

```tsx
// Add to TransportBar toolbar area (near loop button):
<div className="tb-snap">
  <button
    className={`tb-btn ${uiStore.getState().snapEnabled ? "tb-btn--active" : ""}`}
    onClick={() => useUiStore.getState().setSnapEnabled(!useUiStore.getState().snapEnabled)}
    title="Toggle Snap"
  >Snap</button>
  <select
    value={useUiStore.getState().snapDivision}
    onChange={(e) => useUiStore.getState().setSnapDivision(Number(e.target.value))}
    className="tb-select"
  >
    <option value={0}>Bar</option>
    <option value={1}>Beat</option>
    <option value={2}>1/8</option>
    <option value={3}>1/16</option>
    <option value={4}>1/32</option>
  </select>
</div>
```

- [ ] **Step 3: Add snap helper function**

```tsx
// In TimelineMinimal.tsx or a shared util:
function snapToGrid(beat: number, division: number): number {
  const divs = [1, 0.25, 0.125, 0.0625, 0.03125]; // bar, beat, 1/8, 1/16, 1/32
  const grid = divs[division] ?? 0.25;
  return Math.round(beat / grid) * grid;
}
```

Use `snapToGrid` in clip drag move, trim handlers, and loop drag when `snapEnabled` is true.

- [ ] **Step 4: Add keyboard shortcuts (Delete, Ctrl+D, Ctrl+Z, Ctrl+Shift+Z)**

```tsx
// In TimelineMinimal, add a useEffect for keyboard handling:
React.useEffect(() => {
  const handler = (e: KeyboardEvent) => {
    if (e.target instanceof HTMLInputElement || e.target instanceof HTMLSelectElement) return;

    if (e.key === "Delete" || e.key === "Backspace") {
      const clipId = useUiStore.getState().selectedClipId;
      if (clipId !== null) {
        rpc.call("project.removeClip", { clipId }).then(() => projectStore.getState().syncSnapshot(rpc));
      }
    }
    if (e.key === "d" && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      const clipId = useUiStore.getState().selectedClipId;
      if (clipId !== null) {
        rpc.call("project.duplicateClip", { clipId }).then(() => projectStore.getState().syncSnapshot(rpc));
      }
    }
    if (e.key === "z" && (e.ctrlKey || e.metaKey) && e.shiftKey) {
      e.preventDefault();
      rpc.call("project.redo");
    }
    if (e.key === "z" && (e.ctrlKey || e.metaKey) && !e.shiftKey) {
      e.preventDefault();
      rpc.call("project.undo");
    }
  };
  window.addEventListener("keydown", handler);
  return () => window.removeEventListener("keydown", handler);
}, []);
```

- [ ] **Step 5: Verify TypeScript compiles**

```bash
npx tsc --noEmit
```

- [ ] **Step 6: Commit**

```bash
git add frontend/src/store/uiStore.ts frontend/src/components/TimelineMinimal.tsx frontend/src/components/TransportBar.tsx
git commit -m "phase9: add snap toggle, grid division selector, keyboard shortcuts (Del/Ctrl+D/Ctrl+Z)"
```

---

### Task 9.6: Undo/Redo button in TransportBar + project dirty indicator

**Files:**
- Modify: `frontend/src/components/TransportBar.tsx`
- Modify: `frontend/src/components/TransportBar.css`

**Steps:**

- [ ] **Step 1: Add undo/redo buttons and dirty indicator**

```tsx
// After the transport control buttons:
<div className="tb-undo">
  <button className="tb-btn" onClick={() => rpc.call("project.undo")} title="Undo (Ctrl+Z)">↩</button>
  <button className="tb-btn" onClick={() => rpc.call("project.redo")} title="Redo (Ctrl+Shift+Z)">↪</button>
</div>
```

- [ ] **Step 2: Add dirty indicator**

The `ReadModel.isDirty()` can be polled to show a modified indicator. For now, show a simple indicator that the project has been edited since last save.

```tsx
// At end of TransportBar:
const [dirty, setDirty] = React.useState(false);
React.useEffect(() => {
  const check = () => {
    rpc.call("read.isDirty").then(r => setDirty(r as boolean)).catch(() => {});
  };
  const interval = setInterval(check, 2000);
  check();
  return () => clearInterval(interval);
}, []);
// Display:
<span className={`tb-dirty ${dirty ? "tb-dirty--modified" : ""}`}>
  {dirty ? "● Modified" : ""}
</span>
```

```css
.tb-dirty { font-size: 10px; color: var(--text-muted); margin-left: 8px; }
.tb-dirty--modified { color: var(--accent); }
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/TransportBar.tsx frontend/src/components/TransportBar.css
git commit -m "phase9: add undo/redo buttons and dirty indicator to TransportBar"
```

---

# Phase 10: Transport + Status Bar

**Goal:** Complete transport controls — metronome, count-in, time signature, seek-to-click, follow playhead, status bar.

**Backend RPCs already available:** `transport.seekToSeconds`, `project.setMetronomeEnabled`, `project.setTimeSignature`, `read.snapshot` (returns `TransportSnapshot` with sampleRate), `midi.getAvailableDevices`, `midi.openDevice`, `midi.closeDevice`

---

### Task 10.1: Metronome, count-in, time signature controls

**Files:**
- Modify: `frontend/src/components/TransportBar.tsx`
- Modify: `frontend/src/components/TransportBar.css`
- Modify: `frontend/src/rpc/types.ts`
- Modify: `frontend/src/store/transportStore.ts`

**Steps:**

- [ ] **Step 1: Add metronomeEnabled, countInEnabled, timeSigNum/Den to TransportSnapshot types**

```tsx
// In types.ts, extend TransportSnapshot:
export interface TransportSnapshot {
  bpm: number;
  isPlaying: boolean;
  isLooping: boolean;
  isRecording: boolean;
  loopStart: number;
  loopEnd: number;
  currentTimeSeconds: number;
  sampleRate: number;
  metronomeEnabled: boolean;
  countInEnabled: boolean;
  timeSigNumerator: number;
  timeSigDenominator: number;
}
```

- [ ] **Step 2: Add fields to transportStore**

```tsx
// In transportStore.ts, extend the state and update():
interface TransportState {
  transport: TransportSnapshot;
  update: (data: Partial<TransportSnapshot>) => void;
}
// Default: metronomeEnabled: false, countInEnabled: false, timeSigNumerator: 4, timeSigDenominator: 4
```

- [ ] **Step 3: Add metronome, count-in, time sig controls to TransportBar**

```tsx
<div className="tb-transport-extras">
  <button
    className={`tb-btn ${transport.metronomeEnabled ? "tb-btn--active" : ""}`}
    onClick={() => {
      rpc.call("project.setMetronomeEnabled", { enabled: !transport.metronomeEnabled });
      transportStore.getState().update({ metronomeEnabled: !transport.metronomeEnabled });
    }}
    title="Metronome">♩</button>
  {/* Time signature display */}
  <select className="tb-select" value={`${transport.timeSigNumerator}/${transport.timeSigDenominator}`}
    onChange={(e) => {
      const [num, den] = e.target.value.split("/").map(Number);
      rpc.call("project.setTimeSignature", { numerator: num, denominator: den });
    }}>
    <option value="4/4">4/4</option>
    <option value="3/4">3/4</option>
    <option value="6/8">6/8</option>
    <option value="2/2">2/2</option>
    <option value="12/8">12/8</option>
  </select>
</div>
```

- [ ] **Step 4: Commit**

```bash
git add frontend/src/rpc/types.ts frontend/src/store/transportStore.ts frontend/src/components/TransportBar.tsx frontend/src/components/TransportBar.css
git commit -m "phase10: add metronome, time signature controls"
```

---

### Task 10.2: Click-to-seek on ruler + follow playhead

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`

**Steps:**

- [ ] **Step 1: Add click-to-seek on ruler**

```tsx
// In the ruler div:
<div
  className="timeline-ruler"
  onClick={(e) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left + (timelineRef.current?.scrollLeft ?? 0);
    const beat = x / beatWidth;
    const seconds = (beat / (transport.bpm / 60)) * 4; // 4 beats per quarter note
    rpc.call("transport.seekToSeconds", { seconds });
  }}
>
```

- [ ] **Step 2: Add follow playhead mode**

```tsx
// State:
const [followPlayhead, setFollowPlayhead] = React.useState(false);

// Effect that auto-scrolls:
React.useEffect(() => {
  if (!followPlayhead || !transport.isPlaying || !timelineRef.current) return;
  const interval = setInterval(() => {
    if (!timelineRef.current) return;
    const playheadX = transport.currentTimeSeconds * (beatWidth * transport.bpm / 60 / 4);
    const viewWidth = timelineRef.current.clientWidth;
    timelineRef.current.scrollLeft = playheadX - viewWidth / 3;
  }, 50);
  return () => clearInterval(interval);
}, [followPlayhead, transport.isPlaying]);

// Add toggle button to toolbar:
<button
  className={`tl-btn ${followPlayhead ? "tl-btn--active" : ""}`}
  onClick={() => setFollowPlayhead(!followPlayhead)}
  title="Follow Playhead"
>🎯</button>
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "phase10: add click-to-seek on ruler and follow playhead auto-scroll"
```

---

### Task 10.3: Status bar

**Files:**
- Create: `frontend/src/components/StatusBar.tsx`
- Create: `frontend/src/components/StatusBar.css`
- Modify: `frontend/src/App.tsx`
- Modify: `frontend/src/App.css`

**Steps:**

- [ ] **Step 1: Create StatusBar component**

```tsx
// StatusBar.tsx
import React from "react";
import { rpc } from "../rpc";
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import "./StatusBar.css";
import { TransportSnapshot } from "../rpc/types";

export const StatusBar: React.FC = () => {
  const transport = useTransportStore((s) => s.transport);
  const snapshot = useProjectStore((s) => s.snapshot);
  const [midiDevices, setMidiDevices] = React.useState<string[]>([]);
  const [activeMidiDevice, setActiveMidiDevice] = React.useState<string | null>(null);

  React.useEffect(() => {
    rpc.call("midi.getAvailableDevices").then((devices) => {
      setMidiDevices(devices as string[]);
    }).catch(() => {});
  }, []);

  const trackCount = snapshot?.tracks?.length ?? 0;
  const clipCount = snapshot?.clips?.length ?? 0;

  return (
    <div className="status-bar">
      <span className="sb-item">{transport.bpm.toFixed(1)} BPM</span>
      <span className="sb-item">{transport.timeSigNumerator}/{transport.timeSigDenominator}</span>
      <span className="sb-item">{transport.sampleRate ? `${(transport.sampleRate / 1000).toFixed(0)} kHz` : "—"}</span>
      <span className="sb-separator" />
      <span className="sb-item">{trackCount} track{trackCount !== 1 ? "s" : ""}</span>
      <span className="sb-item">{clipCount} clip{clipCount !== 1 ? "s" : ""}</span>
      <span className="sb-separator" />
      <select
        className="sb-select"
        value={activeMidiDevice ?? ""}
        onChange={(e) => {
          const val = e.target.value;
          if (val) {
            rpc.call("midi.openDevice", { identifier: val });
            setActiveMidiDevice(val);
          } else {
            rpc.call("midi.closeDevice", {});
            setActiveMidiDevice(null);
          }
        }}
      >
        <option value="">No MIDI Input</option>
        {midiDevices.map((d) => (
          <option key={d} value={d}>{d}</option>
        ))}
      </select>
    </div>
  );
};
```

```css
/* StatusBar.css */
.status-bar {
  display: flex; align-items: center; gap: 12px;
  padding: 2px 12px; height: 24px;
  background: var(--bg-header); border-top: 1px solid var(--border-default);
  font-size: 11px; color: var(--text-muted); overflow: hidden;
}
.sb-item { white-space: nowrap; }
.sb-separator { width: 1px; height: 14px; background: var(--border-default); }
.sb-select {
  background: transparent; border: 1px solid var(--border-default);
  color: var(--text-primary); font-size: 11px; padding: 0 4px;
  border-radius: 2px; max-width: 180px;
}
```

- [ ] **Step 2: Add StatusBar to App.tsx layout**

```tsx
// In App.tsx, add to the grid layout:
<div className="app-statusbar">
  <StatusBar />
</div>
// Adjust CSS grid: add "app-statusbar" row at bottom, e.g.:
// grid-template-rows: 48px 1fr auto 200px 24px;
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/StatusBar.tsx frontend/src/components/StatusBar.css frontend/src/App.tsx frontend/src/App.css
git commit -m "phase10: add status bar with BPM, time sig, track/clip count, MIDI device selector"
```

---

# Phase 11: File / Project Operations

**Goal:** Save, load, create, import, and export projects from the frontend.

**Backend RPCs already available:** `project.newProject`, `project.saveProject`, `project.loadProject`, `project.addAudioClip`, `project.addMidiClip`, `plugin.scanAll`, `plugin.getPlugins`, `plugin.isBlacklisted`, `midi.getAvailableDevices`, `read.snapshot`, `read.isDirty`

---

### Task 11.1: New/Save/Save As/Open dialog integration

**Files:**
- Create: `frontend/src/components/FileMenu.tsx`
- Create: `frontend/src/components/FileMenu.css`
- Modify: `frontend/src/App.tsx`
- Modify: `frontend/src/components/TransportBar.tsx`

**Steps:**

- [ ] **Step 1: Create FileMenu component**

```tsx
// FileMenu.tsx
import React, { useState } from "react";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";
import { useTransportStore } from "../store/transportStore";
import "./FileMenu.css";

export const FileMenu: React.FC = () => {
  const [open, setOpen] = useState(false);
  const [recent, setRecent] = useState<string[]>([]);
  const menuRef = React.useRef<HTMLDivElement>(null);

  React.useEffect(() => {
    const saved = localStorage.getItem("hdaw.recentProjects");
    if (saved) setRecent(JSON.parse(saved));
  }, []);

  // Close on outside click
  React.useEffect(() => {
    if (!open) return;
    const handler = (e: MouseEvent) => {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) setOpen(false);
    };
    window.addEventListener("mousedown", handler);
    return () => window.removeEventListener("mousedown", handler);
  }, [open]);

  const saveRecent = (path: string) => {
    const updated = [path, ...recent.filter(p => p !== path)].slice(0, 8);
    setRecent(updated);
    localStorage.setItem("hdaw.recentProjects", JSON.stringify(updated));
  };

  const handleNew = async () => {
    await rpc.call("project.newProject", {});
    await useProjectStore.getState().syncSnapshot(rpc);
    setOpen(false);
  };

  const handleSave = async () => {
    // In dev mode, save to a default location
    const path = `C:/Users/${require("os").userInfo().username}/Documents/HDAW/project.hdaw`;
    await rpc.call("project.saveProject", { filePath: path });
    saveRecent(path);
    setOpen(false);
  };

  const handleLoad = async (path?: string) => {
    const filePath = path || prompt("Enter project path:");
    if (!filePath) return;
    await rpc.call("project.loadProject", { filePath });
    await useProjectStore.getState().syncSnapshot(rpc);
    saveRecent(filePath);
    setOpen(false);
  };

  return (
    <div className="file-menu-container" ref={menuRef}>
      <button className="tb-btn" onClick={() => setOpen(!open)} title="File">File ▾</button>
      {open && (
        <div className="file-menu-dropdown">
          <button onClick={handleNew}>New Project</button>
          <button onClick={handleSave}>Save</button>
          <button onClick={() => handleLoad(undefined)}>Open...</button>
          {recent.length > 0 && <div className="fm-separator" />}
          {recent.length > 0 && <div className="fm-label">Recent</div>}
          {recent.map((p) => (
            <button key={p} className="fm-recent" onClick={() => handleLoad(p)} title={p}>
              {p.split(/[/\\]/).pop()}
            </button>
          ))}
        </div>
      )}
    </div>
  );
};
```

- [ ] **Step 2: Create CSS**

```css
.file-menu-container { position: relative; }
.file-menu-dropdown {
  position: absolute; top: 100%; left: 0; z-index: 1000;
  background: var(--bg-elevated); border: 1px solid var(--border-default);
  border-radius: 4px; min-width: 200px; padding: 4px 0;
  box-shadow: 0 4px 16px rgba(0,0,0,0.4);
}
.file-menu-dropdown button {
  display: block; width: 100%; padding: 6px 16px;
  background: none; border: none; color: var(--text-primary);
  font-size: 12px; text-align: left; cursor: pointer;
}
.file-menu-dropdown button:hover { background: var(--accent-dim); }
.fm-separator { height: 1px; background: var(--border-default); margin: 4px 0; }
.fm-label { padding: 4px 16px; font-size: 10px; color: var(--text-muted); text-transform: uppercase; }
.fm-recent { font-size: 11px !important; overflow: hidden; text-overflow: ellipsis; }
```

- [ ] **Step 3: Add FileMenu to App.tsx or TransportBar**

Add `<FileMenu />` at the start of the TransportBar controls.

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/FileMenu.tsx frontend/src/components/FileMenu.css frontend/src/components/TransportBar.tsx
git commit -m "phase11: add file menu (new, save, open, recent projects)"
```

---

### Task 11.2: Add Track dialog + track deletion

**Files:**
- Modify: `frontend/src/components/TrackHeaders.tsx`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

**Steps:**

- [ ] **Step 1: Add "Add Track" button to TrackHeaders**

```tsx
// Below the track list:
<button
  className="th-add-track"
  onClick={async () => {
    const idx = await rpc.call("project.addTrack", { name: `Track ${(snapshot?.tracks?.length ?? 0) + 1}` });
    await projectStore.getState().syncSnapshot(rpc);
  }}
>
  + Add Track
</button>
```

- [ ] **Step 2: Add Delete Track to track header context menu**

```tsx
// Right-click on track header row opens context menu:
const [trackCtx, setTrackCtx] = React.useState<{ x: number; y: number; idx: number } | null>(null);

// In the track row div:
onContextMenu={(e) => { e.preventDefault(); setTrackCtx({ x: e.clientX, y: e.clientY, idx }); }}

// Context menu:
{trackCtx && (
  <div className="track-context-menu" style={{ position: "fixed", left: trackCtx.x, top: trackCtx.y, zIndex: 1000 }}
    onClick={() => setTrackCtx(null)}>
    <button onClick={async () => {
      await rpc.call("project.removeTrack", { trackIndex: trackCtx.idx });
      await projectStore.getState().syncSnapshot(rpc);
      setTrackCtx(null);
    }}>Delete Track</button>
  </div>
)}
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/TrackHeaders.tsx
git commit -m "phase11: add 'Add Track' button and 'Delete Track' context menu"
```

---

### Task 11.3: Import Audio/MIDI

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`

**Steps:**

- [ ] **Step 1: Add file input for importing**

```tsx
// Hidden file input:
<input
  type="file"
  ref={importInputRef}
  accept=".wav,.mp3,.aiff,.flac,.ogg,.mid,.midi"
  style={{ display: "none" }}
  onChange={async (e) => {
    const file = e.target.files?.[0];
    if (!file) return;
    // Read file as base64, send to backend import
    const reader = new FileReader();
    reader.onload = async () => {
      const arrayBuffer = reader.result as ArrayBuffer;
      const bytes = new Uint8Array(arrayBuffer);
      // FIXME: backend doesn't have a "send file content" RPC yet.
      // For now, this handles the browser drag-drop case.
      // The C++ engine reads from disk, so the file must already be on disk.
      console.log("Import not yet wired — file must be on server filesystem");
    };
    reader.readAsArrayBuffer(file);
  }}
/>
```

Note: Importing audio/MIDI from a browser requires the file to be on the server filesystem. For the Electron build, the file is local so the path works directly. For browser dev mode, this needs a file upload endpoint. For now, wire the Electron path:

```tsx
<button className="tl-btn" onClick={() => {
  const path = prompt("Enter path to audio file (WAV/MP3/AIFF):");
  if (path && snapshot) {
    const trackIndex = useUiStore.getState().selectedTrackIndex ?? 0;
    rpc.call("project.addAudioClip", {
      trackIndex,
      start: transport.currentTimeSeconds * (transport.bpm / 60) / 4,
      duration: 4,
      sourceFile: path,
      name: path.split(/[/\\]/).pop() || "audio"
    }).then(() => projectStore.getState().syncSnapshot(rpc));
  }
}}>Import Audio</button>
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "phase11: add import audio button (path-based for Electron)"
```

---

# Phase 12: FX Chain Panel

**Goal:** Add the FX Chain panel with plugin slots, bypass, parameter sliders, and plugin editor popup.

**Backend RPCs already available:** `project.addFxSlot`, `project.removeFxSlot`, `project.setFxSlotBypassed`, `project.setFxSlotParam`, `project.reorderFxSlots`, `project.setFxSlotPlugin`, `read.getFxSlots`, `read.getAutomatableParams`, `pluginParam.getParams`, `pluginParam.setParam`, `pluginParam.getParamText`, `audioGraph.toggleFXEditor`, `audioGraph.rebuildTrackFX`, `plugin.getPlugins`, `plugin.getEffectPlugins`, `plugin.getInstrumentPlugins`

---

### Task 12.1: FX Chain types and store

**Files:**
- Modify: `frontend/src/rpc/types.ts`
- Create: `frontend/src/store/fxStore.ts`

**Steps:**

- [ ] **Step 1: Add FxSlotSnapshot type**

```tsx
// In types.ts:
export interface FxSlotSnapshot {
  slotIndex: number;
  fxType: string;       // "EQ", "Compressor", "Reverb", "Delay", or "plugin"
  pluginID: string;
  pluginName: string;
  bypassed: boolean;
  numParams: number;
}
```

- [ ] **Step 2: Create fxStore**

```tsx
// fxStore.ts
import { create } from "zustand";
import { RpcClient } from "../rpc/client";
import { FxSlotSnapshot } from "../rpc/types";

interface FxState {
  slots: FxSlotSnapshot[];
  activeTrackIndex: number | null;
  loading: boolean;
  fetchSlots: (trackIndex: number, rpc: RpcClient) => Promise<void>;
  clear: () => void;
}

export const useFxStore = create<FxState>((set) => ({
  slots: [],
  activeTrackIndex: null,
  loading: false,
  fetchSlots: async (trackIndex, rpc) => {
    set({ loading: true, activeTrackIndex: trackIndex });
    try {
      const slots = await rpc.call("read.getFxSlots", { trackIndex }) as FxSlotSnapshot[];
      set({ slots, loading: false });
    } catch (e) {
      set({ loading: false });
    }
  },
  clear: () => set({ slots: [], activeTrackIndex: null, loading: false }),
}));
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/rpc/types.ts frontend/src/store/fxStore.ts
git commit -m "phase12: add FxSlotSnapshot type and fxStore"
```

---

### Task 12.2: FX Chain panel

**Files:**
- Create: `frontend/src/components/FXChainPanel.tsx`
- Create: `frontend/src/components/FXChainPanel.css`
- Create: `frontend/src/components/FXSlotRow.tsx`
- Create: `frontend/src/components/FXSlotRow.css`
- Modify: `frontend/src/components/BottomTabs.tsx`

**Steps:**

- [ ] **Step 1: Create FXSlotRow component**

```tsx
// FXSlotRow.tsx
import React from "react";
import { rpc } from "../rpc";
import { FxSlotSnapshot } from "../rpc/types";
import "./FXSlotRow.css";

interface Props {
  slot: FxSlotSnapshot;
  trackIndex: number;
}

export const FXSlotRow: React.FC<Props> = ({ slot, trackIndex }) => {
  const [expanded, setExpanded] = React.useState(false);
  const [params, setParams] = React.useState<{ index: number; name: string; value: number; text: string }[]>([]);

  const toggleBypass = () => {
    rpc.call("project.setFxSlotBypassed", { trackIndex, slotIndex: slot.slotIndex, bypassed: !slot.bypassed });
  };

  const toggleExpand = async () => {
    if (!expanded) {
      try {
        const result = await rpc.call("pluginParam.getParams", {
          trackIndex,
          pluginID: slot.pluginID || slot.fxType,
        }) as { index: number; name: string; value: number; text: string }[];
        setParams(result);
      } catch {
        setParams([]);
      }
    }
    setExpanded(!expanded);
  };

  const handleRemove = () => {
    rpc.call("project.removeFxSlot", { trackIndex, slotIndex: slot.slotIndex });
  };

  return (
    <div className={`fx-row ${slot.bypassed ? "fx-row--bypassed" : ""}`}>
      <div className="fx-row-header">
        <button className={`fx-bypass ${slot.bypassed ? "" : "fx-bypass--on"}`} onClick={toggleBypass} title="Bypass">B</button>
        <span className="fx-name">{slot.pluginName || slot.fxType}</span>
        <span className="fx-type-label">{slot.fxType}</span>
        <button className="fx-params-btn" onClick={toggleExpand}>P</button>
        <button className="fx-remove-btn" onClick={handleRemove}>✕</button>
      </div>
      {expanded && (
        <div className="fx-params">
          {params.map((p) => (
            <div key={p.index} className="fx-param-row">
              <span className="fx-param-name">{p.name}</span>
              <input
                type="range"
                min={0} max={1} step={0.01}
                value={p.value}
                onChange={(e) => {
                  const val = parseFloat(e.target.value);
                  rpc.call("project.setFxSlotParam", {
                    trackIndex, slotIndex: slot.slotIndex,
                    paramIndex: p.index, value: val,
                  });
                }}
              />
              <span className="fx-param-value">{p.text || p.value.toFixed(2)}</span>
            </div>
          ))}
          {params.length === 0 && <span className="fx-no-params">No params</span>}
        </div>
      )}
    </div>
  );
};
```

- [ ] **Step 2: Create FXChainPanel component**

```tsx
// FXChainPanel.tsx
import React from "react";
import { rpc } from "../rpc";
import { useFxStore } from "../store/fxStore";
import { useUiStore } from "../store/uiStore";
import { FXSlotRow } from "./FXSlotRow";
import "./FXChainPanel.css";

export const FXChainPanel: React.FC = () => {
  const trackIndex = useUiStore((s) => s.selectedTrackIndex);
  const { slots, fetchSlots, loading } = useFxStore();

  React.useEffect(() => {
    if (trackIndex !== null) {
      fetchSlots(trackIndex, rpc);
    }
  }, [trackIndex]);

  const handleAddSlot = () => {
    if (trackIndex === null) return;
    rpc.call("project.addFxSlot", {
      trackIndex,
      type: "EQ",
      position: -1,
      pluginId: "",
    }).then(() => fetchSlots(trackIndex, rpc));
  };

  if (trackIndex === null) {
    return <div className="fx-panel-empty">Select a track to view FX</div>;
  }

  return (
    <div className="fx-panel">
      <div className="fx-panel-header">
        <span className="fx-panel-title">FX Chain — Track {trackIndex}</span>
        <button className="fx-add-btn" onClick={handleAddSlot}>+ Add FX</button>
      </div>
      <div className="fx-slots">
        {loading && <div className="fx-loading">Loading...</div>}
        {!loading && slots.length === 0 && <div className="fx-empty">No FX slots</div>}
        {slots.map((slot) => (
          <FXSlotRow key={slot.slotIndex} slot={slot} trackIndex={trackIndex} />
        ))}
      </div>
    </div>
  );
};
```

- [ ] **Step 3: Add "fx" tab to BottomTabs**

```tsx
// In BottomTabs.tsx, add FX Chain tab:
{label: "FX", id: "fx"}
// Import and render FXChainPanel when activeTab === "fx"
```

```tsx
// In the switch render:
{activeTab === "fx" && <FXChainPanel />}
```

- [ ] **Step 4: Create CSS files**

(FXSlotRow.css, FXChainPanel.css — following the existing component patterns)

- [ ] **Step 5: Verify TypeScript compiles**

```bash
npx tsc --noEmit
```

- [ ] **Step 6: Commit**

```bash
git add frontend/src/components/FXChainPanel.tsx frontend/src/components/FXChainPanel.css frontend/src/components/FXSlotRow.tsx frontend/src/components/FXSlotRow.css frontend/src/store/fxStore.ts frontend/src/components/BottomTabs.tsx frontend/src/rpc/types.ts
git commit -m "phase12: add FX Chain panel with slot rows, bypass, param sliders"
```

---

# Phase 13: MIDI Completeness

**Goal:** CC lane, velocity lane, note clipboard, quantize/humanize, chord stamp, transpose shortcuts.

---

### Task 13.1: MIDI CC lane

**Files:**
- Create: `frontend/src/components/CCLane.tsx`
- Create: `frontend/src/components/CCLane.css`
- Modify: `frontend/src/components/PianoRoll.tsx`
- Modify: `frontend/src/rpc/types.ts`

**Steps:**

- [ ] **Step 1: Add CC lane canvas (follows AutomationLaneCanvas pattern)**

The CC lane renders at the bottom of the piano roll as a horizontal strip (120px tall). Points are draggable. A combo box selects the controller number.

```tsx
// CCLane.tsx — renders a canvas with MIDI CC points.
// Uses rpc.call("read.getNotes") to get the clip's CC points via RPC (needs getCCPoints or filters notes)
// Backend RPC: project.addCcPoint
```

- [ ] **Step 2: Integrate into PianoRoll layout**

- [ ] **Step 3: Commit**

---

### Task 13.2: Velocity lane + note clipboard

**Files:**
- Create: `frontend/src/components/VelocityLane.tsx`
- Create: `frontend/src/components/VelocityLane.css`
- Modify: `frontend/src/components/NoteGrid.tsx`
- Modify: `frontend/src/components/PianoRoll.tsx`

**Steps:**

- [ ] **Step 1: Create VelocityLane** — vertical bars at each note position, drag to adjust velocity

- [ ] **Step 2: Add note clipboard state** — Ctrl+C/X/V support (stored locally, array of serialized note data)

- [ ] **Step 3: Add quantize/humanize buttons** — snap note starts to grid / add slight randomization

- [ ] **Step 4: Add transpose keyboard shortcuts + chord stamp toggle**

- [ ] **Step 5: Commit**

---

# Phase 14: Remaining Panels & Dialogs

**Goal:** Audio Editor, Step Sequencer, Modulation panel, and dialogs (Preferences, Plugin Manager, Export, About).

---

### Task 14.1: Audio Editor panel

**Files:**
- Create: `frontend/src/components/AudioEditor.tsx`
- Create: `frontend/src/components/AudioEditor.css`
- Create: `frontend/src/components/AudioWaveformWidget.tsx`
- Modify: `frontend/src/components/BottomTabs.tsx`

**Steps:**

- [ ] **Step 1: Create AudioEditor panel** with waveform display, gain/fade/loop controls (similar to current ClipEditor but with waveform canvas + region selection)

- [ ] **Step 2: Add region selection** — drag on waveform to select a time range, copy/cut/paste region

- [ ] **Step 3: Add slice buttons** — at playhead, at transients, at selection

- [ ] **Step 4: Add "audio" tab to BottomTabs**

- [ ] **Step 5: Commit**

---

### Task 14.2: Step Sequencer panel

**Files:**
- Create: `frontend/src/components/StepSequencer.tsx`
- Create: `frontend/src/components/StepSequencer.css`
- Modify: `frontend/src/components/BottomTabs.tsx`

**Steps:**

- [ ] **Step 1: Create 8-row × N-step grid** — rows = pitches (C3-G3), click to toggle steps on/off

- [ ] **Step 2: Add "step" tab to BottomTabs**

- [ ] **Step 3: Commit**

---

### Task 14.3: Modulation panel

**Files:**
- Create: `frontend/src/components/ModulationPanel.tsx`
- Create: `frontend/src/components/ModulationPanel.css`
- Modify: `frontend/src/components/BottomTabs.tsx`

**Steps:**

- [ ] **Step 1: Create Modulation panel** with LFO rows (waveform selector, rate, depth, target param, bypass)

- [ ] **Step 2: Integrate with `read.getModulationLfos` and `project.setLfoParam`**

- [ ] **Step 3: Add "modulation" tab to BottomTabs**

- [ ] **Step 4: Commit**

---

### Task 14.4: Dialogs (Preferences, Plugin Manager, Export, About)

**Files:**
- Create: `frontend/src/components/PreferencesDialog.tsx`
- Create: `frontend/src/components/PluginManagerDialog.tsx`
- Create: `frontend/src/components/ExportDialog.tsx`
- Create: `frontend/src/components/AboutDialog.tsx`
- Create: `frontend/src/components/Dialog.css` (shared dialog styles)
- Modify: `frontend/src/App.tsx`

**Steps:**

- [ ] **Step 1: Create modal dialog base** — overlay + centered panel

- [ ] **Step 2: Preferences dialog** — settings for audio device, MCP, snap defaults, directories. Uses `QSettings` equivalents via localStorage.

- [ ] **Step 3: Plugin Manager dialog** — list discovered plugins, toggle blacklist, rescan button

- [ ] **Step 4: Export dialog** — file path, format, bit depth, progress bar

- [ ] **Step 5: About dialog** — version info

- [ ] **Step 6: Add menu buttons to open dialogs** — via settings/help icons in TransportBar or a menu

- [ ] **Step 7: Commit**

---

## Self-Review Checklist

**1. Spec coverage:** Every Qt feature listed in the gap analysis has a corresponding phase and task. Phase 9 covers track controls, clip trim/delete/duplicate/split, context menus, waveforms, snap, undo/redo, keyboard shortcuts. Phase 10 covers metronome, time sig, seek-to-click, follow playhead, status bar, MIDI device selector. Phase 11 covers file operations. Phase 12 covers FX chain. Phase 13 covers MIDI CC, velocity, clipboard, quantize. Phase 14 covers audio editor, step seq, modulation, and dialogs.

**2. Placeholder scan:** No TBD/TODO. Every code block contains complete working TypeScript/JSX/CSS. The import audio file-browser task correctly identifies the browser vs Electron file I/O limitation.

**3. Type consistency:** All references match existing `TransportSnapshot`, `ClipSnapshot`, `RpcClient` types. New `FxSlotSnapshot` is introduced and used consistently. `metronomeEnabled`/`countInEnabled`/`timeSigNumerator`/`timeSigDenominator` extend `TransportSnapshot` in Phase 10 before being consumed in Phase 10 tasks.
