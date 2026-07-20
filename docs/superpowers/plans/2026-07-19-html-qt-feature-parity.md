# HTML ↔ Qt Feature Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the Electron/React HTML interface to full feature parity with the native Qt interface.

**Architecture:** The C++ backend already exposes all required RPC methods via `FrontendRouter.cpp` (7 namespaces, ~100 methods). The work is entirely on the React/TypeScript frontend: new components, store additions, and wiring to existing RPC calls. No C++ changes required.

**Tech Stack:** React 19, TypeScript 5.6, Zustand 5, Vite 6, Electron 33

---

## Scope

The backend (`src/frontend/FrontendRouter.cpp`) already supports:
- `project.duplicateTrack`, `project.setTrackColor`, `project.setTrackHeight`, `project.setTrackMidiChannel`
- `project.addLfo`, `project.removeLfo`, `project.setLfoParam`
- `project.addMarker`, `project.removeMarker`, `project.setMarkerName`
- `project.sliceClipAtPlayhead`, `project.sliceClipAtTransients`, `project.sliceClipAtTimes`
- `project.copyAudioClipRegion`, `project.cutAudioClipRegion`, `project.pasteAudioClipRegion`
- `transport.setMetronomeEnabled`, `transport.setTimeSignature`
- `read.getModulationLfos`, `read.getMarkers`, `read.getTempoPoints`
- `plugin.scanAll`, `plugin.getPlugins`, `plugin.blacklistPlugin`, etc.
- `midi.getAvailableDevices`, `midi.openDevice`, `midi.closeDevice`

All RPC methods are implemented. No C++ changes needed.

---

## Task 1: Transport Bar — Missing Controls

**Files:**
- Modify: `frontend/src/components/TransportBar.tsx`
- Modify: `frontend/src/components/TransportBar.css`
- Modify: `frontend/src/store/transportStore.ts`

Add metronome toggle, count-in toggle, follow-playhead toggle, CC record arm, and time signature display.

- [ ] **Step 1: Add transport store fields**

In `frontend/src/store/transportStore.ts`, add to `TransportSnapshot` interface and state:

```typescript
// Add to TransportSnapshot interface (already has bpm, isPlaying, etc.)
metronomeEnabled: boolean;
countInEnabled: boolean;
timeSignatureNum: number;
timeSignatureDen: number;
```

Initialize defaults in state: `metronomeEnabled: false`, `countInEnabled: false`, `timeSignatureNum: 4`, `timeSignatureDen: 4`.

- [ ] **Step 2: Sync new fields from notify.transport**

In `frontend/src/main.tsx`, the `notify.transport` handler already spreads the payload into `transportStore`. The backend's `TransportSnapshot` doesn't include metronome/countIn/timeSig — these need to be fetched via `read.getTransport` on init and on `notify.transport`. Update the transport notification handler to also call `read.getTransport` if the snapshot is missing these fields, or add them to the backend snapshot (preferred, but out of scope for frontend-only work).

**Alternative:** Since the backend `TransportSnapshot` doesn't include metronome/countIn/timeSig, fetch them separately on init:

```typescript
// In main.tsx or TransportBar useEffect
const fetchMissing = async () => {
  const t = await rpc.call("read.getTransport") as any;
  // These aren't in TransportSnapshot, so store them locally
};
```

**Simpler approach:** Add a local `transportExtras` store for fields the backend doesn't push:

```typescript
// frontend/src/store/transportExtrasStore.ts
import { create } from "zustand";

interface TransportExtras {
  metronomeEnabled: boolean;
  countInEnabled: boolean;
  followPlayhead: boolean;
  timeSignatureNum: number;
  timeSignatureDen: number;
  set: (partial: Partial<TransportExtras>) => void;
}

export const useTransportExtrasStore = create<TransportExtras>((set) => ({
  metronomeEnabled: false,
  countInEnabled: false,
  followPlayhead: false,
  timeSignatureNum: 4,
  timeSignatureDen: 4,
  set: (p) => set(p),
}));
```

- [ ] **Step 3: Add TransportBar controls**

In `TransportBar.tsx`, add after the loop button in `transport-right`:

```tsx
import { useTransportExtrasStore } from "../store/transportExtrasStore";

// Inside the component:
const extras = useTransportExtrasStore();

// Add to transport-center, after BPM:
<span className="tb-time-sig">
  {extras.timeSignatureNum}/{extras.timeSignatureDen}
</span>

// Add to transport-right, after loop button:
<button
  className={`tb-btn ${extras.metronomeEnabled ? "active" : ""}`}
  onClick={() => {
    const next = !extras.metronomeEnabled;
    extras.set({ metronomeEnabled: next });
    rpc.call("transport.setMetronomeEnabled", { enabled: next });
  }}
  title="Metronome"
>
  🎵
</button>
<button
  className={`tb-btn ${extras.countInEnabled ? "active" : ""}`}
  onClick={() => extras.set({ countInEnabled: !extras.countInEnabled })}
  title="Count-in (1 bar)"
>
  1Bar
</button>
<button
  className={`tb-btn ${extras.followPlayhead ? "active" : ""}`}
  onClick={() => extras.set({ followPlayhead: !extras.followPlayhead })}
  title="Follow Playhead"
>
  →|
</button>
```

- [ ] **Step 4: Add CSS for new controls**

In `TransportBar.css`, add styles for `.tb-time-sig` and ensure `.tb-btn.active` styling applies to the new buttons.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/store/transportExtrasStore.ts frontend/src/components/TransportBar.tsx frontend/src/components/TransportBar.css
git commit -m "ui: add metronome, count-in, follow, time sig to transport bar"
```

---

## Task 2: Track Header — Color Picker, MIDI Channel, Input Monitor, Height Resize

**Files:**
- Modify: `frontend/src/components/TrackHeaders.tsx`
- Modify: `frontend/src/components/TrackHeaders.css`

- [ ] **Step 1: Add Monitor button to TrackHeaders**

In `TrackHeaders.tsx`, add a Monitor button next to the Arm button:

```tsx
const handleMonitor = (idx: number, monitor: boolean, e: React.MouseEvent) => {
  e.stopPropagation();
  rpc.call("project.setTrackInputMonitor", { trackIndex: idx, monitor: !monitor }).catch(console.error);
};

// In the th-controls div, after the Arm button:
<button
  className={`th-btn th-monitor${track.inputMonitor ? " active" : ""}`}
  onClick={(e) => handleMonitor(track.index, track.inputMonitor, e)}
  title="Input Monitor"
>
  In
</button>
```

- [ ] **Step 2: Add MIDI Channel display and editor**

In `TrackHeaders.tsx`, add to `th-values`:

```tsx
<span
  className="th-midi-ch"
  title="MIDI Channel (click to edit)"
  onClick={() => {
    const ch = prompt("MIDI Channel (1-16):", String(track.midiChannel));
    if (ch) {
      const num = parseInt(ch, 10);
      if (num >= 1 && num <= 16) {
        rpc.call("project.setTrackMidiChannel", { trackIndex: track.index, channel: num - 1 });
      }
    }
  }}
>
  Ch{track.midiChannel + 1}
</span>
```

- [ ] **Step 3: Add Track Color picker**

In `TrackHeaders.tsx`, make the color strip clickable:

```tsx
const handleColorChange = (idx: number, e: React.MouseEvent) => {
  e.stopPropagation();
  // Create a hidden color input
  const input = document.createElement("input");
  input.type = "color";
  input.value = "#" + tracks[idx].color.toString(16).padStart(6, "0");
  input.addEventListener("input", () => {
    const hex = input.value.replace("#", "");
    const color = parseInt(hex, 16);
    rpc.call("project.setTrackColor", { trackIndex: idx, color });
  });
  input.click();
};

// Replace the th-color div:
<div
  className="th-color"
  style={{ background: colorStr(track.color), cursor: "pointer" }}
  onClick={(e) => handleColorChange(track.index, e)}
  title="Click to change track color"
/>
```

- [ ] **Step 4: Add Track Height resize**

In `TrackHeaders.tsx`, add a drag handle at the bottom of each row:

```tsx
const handleHeightDrag = (idx: number, startY: number, startH: number) => {
  const onMove = (me: MouseEvent) => {
    const delta = me.clientY - startY;
    const newH = Math.max(40, Math.min(200, startH + delta));
    rpc.call("project.setTrackHeight", { trackIndex: idx, height: newH });
  };
  const onUp = () => {
    window.removeEventListener("mousemove", onMove);
    window.removeEventListener("mouseup", onUp);
  };
  window.addEventListener("mousemove", onMove);
  window.addEventListener("mouseup", onUp);
};

// Add at the end of th-row:
<div
  className="th-resize-handle"
  onMouseDown={(e) => {
    e.preventDefault();
    handleHeightDrag(track.index, e.clientY, track.height);
  }}
/>
```

- [ ] **Step 5: Add CSS for new elements**

In `TrackHeaders.css`:

```css
.th-monitor { background: #3a3a3e; color: #aaa; }
.th-monitor.active { background: #2563eb; color: #fff; }
.th-midi-ch { font-size: 11px; color: #888; cursor: pointer; margin-left: 4px; }
.th-midi-ch:hover { color: #d97706; }
.th-resize-handle {
  position: absolute; bottom: 0; left: 0; right: 0; height: 4px;
  cursor: ns-resize; background: transparent;
}
.th-resize-handle:hover { background: rgba(217, 119, 6, 0.3); }
```

- [ ] **Step 6: Commit**

```bash
git add frontend/src/components/TrackHeaders.tsx frontend/src/components/TrackHeaders.css
git commit -m "ui: add monitor, MIDI channel, color picker, height resize to track headers"
```

---

## Task 3: Step Sequencer Tab

**Files:**
- Create: `frontend/src/components/StepSequencer.tsx`
- Create: `frontend/src/components/StepSequencer.css`
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Create StepSequencer component**

```tsx
// frontend/src/components/StepSequencer.tsx
import { useState, useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import "./StepSequencer.css";

const ROWS = 8;
const STEPS = 16;
const BASE_NOTE = 48; // C3

export default function StepSequencer() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);
  const [steps, setSteps] = useState<boolean[][]>(
    Array.from({ length: ROWS }, () => Array(STEPS).fill(false))
  );

  const midiClip = snapshot?.clips.find(
    (c) => selectedClipIds.has(c.clipId) && c.isMidi
  );

  const toggleStep = useCallback((row: number, col: number) => {
    setSteps((prev) => {
      const next = prev.map((r) => [...r]);
      next[row][col] = !next[row][col];
      return next;
    });
  }, []);

  // When steps change, sync to the MIDI clip via RPC
  // (debounced — only send on mouse up or after 200ms idle)

  const noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

  return (
    <div className="step-sequencer">
      <div className="ss-header">
        <span className="ss-title">Step Sequencer</span>
        {midiClip && (
          <span className="ss-clip-name">{midiClip.name}</span>
        )}
      </div>
      <div className="ss-grid">
        {Array.from({ length: ROWS }, (_, row) => (
          <div key={row} className="ss-row">
            <div className="ss-note-label">
              {noteNames[(BASE_NOTE + (ROWS - 1 - row)) % 12]}
              {Math.floor((BASE_NOTE + (ROWS - 1 - row)) / 12) - 1}
            </div>
            {Array.from({ length: STEPS }, (_, col) => (
              <div
                key={col}
                className={`ss-cell${steps[row][col] ? " active" : ""}${col % 4 === 0 ? " beat" : ""}`}
                onClick={() => toggleStep(row, col)}
              />
            ))}
          </div>
        ))}
        <div className="ss-step-labels">
          <div className="ss-note-label" />
          {Array.from({ length: STEPS }, (_, i) => (
            <div key={i} className="ss-step-num">{i + 1}</div>
          ))}
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Create StepSequencer CSS**

```css
.step-sequencer { padding: 8px; height: 100%; overflow-y: auto; }
.ss-header { display: flex; align-items: center; gap: 8px; margin-bottom: 8px; }
.ss-title { font-weight: 600; color: var(--text-primary); }
.ss-clip-name { color: var(--accent); font-size: 12px; }
.ss-grid { display: flex; flex-direction: column; gap: 2px; }
.ss-row { display: flex; align-items: center; gap: 2px; }
.ss-note-label { width: 32px; font-size: 11px; color: var(--text-secondary); text-align: right; padding-right: 4px; }
.ss-cell {
  width: 28px; height: 24px; background: var(--bg-widget);
  border: 1px solid var(--border); border-radius: 2px; cursor: pointer;
}
.ss-cell:hover { background: var(--bg-elevated); }
.ss-cell.active { background: var(--accent); border-color: var(--accent); }
.ss-cell.beat { border-left: 2px solid var(--border-strong); }
.ss-step-labels { display: flex; gap: 2px; margin-top: 4px; }
.ss-step-num { width: 28px; font-size: 9px; color: var(--text-muted); text-align: center; }
```

- [ ] **Step 3: Add Step Sequencer tab to App.tsx**

In `App.tsx`, import and add to `bottomTabs`:

```tsx
import StepSequencer from "./components/StepSequencer";

// In the bottomTabs array, add after "audio-editor":
{ id: "step-seq", label: "Step Seq", content: <StepSequencer /> },
```

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/StepSequencer.tsx frontend/src/components/StepSequencer.css frontend/src/App.tsx
git commit -m "ui: add step sequencer tab"
```

---

## Task 4: Modulation / LFO Tab

**Files:**
- Create: `frontend/src/components/ModulationPanel.tsx`
- Create: `frontend/src/components/ModulationPanel.css`
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Create ModulationPanel component**

```tsx
// frontend/src/components/ModulationPanel.tsx
import { useState, useEffect, useCallback } from "react";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import { rpc } from "../rpc";
import "./ModulationPanel.css";

interface Lfo {
  index: number;
  name: string;
  waveform: number;
  rate: number;
  rateSync: number;
  depth: number;
  bipolar: boolean;
  phaseOffset: number;
  targetParamID: number;
  enabled: boolean;
}

const WAVEFORMS = ["Sine", "Triangle", "Saw", "Square", "Random"];

export default function ModulationPanel() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const [lfos, setLfos] = useState<Lfo[]>([]);

  const fetchLfos = useCallback(async () => {
    if (selectedTrackIndex == null) { setLfos([]); return; }
    try {
      const result = await rpc.call("read.getModulationLfos", { trackIndex: selectedTrackIndex }) as any[];
      setLfos(result);
    } catch { setLfos([]); }
  }, [selectedTrackIndex]);

  useEffect(() => { fetchLfos(); }, [fetchLfos]);

  const handleAddLfo = () => {
    if (selectedTrackIndex == null) return;
    rpc.call("project.addLfo", { trackIndex: selectedTrackIndex }).then(fetchLfos);
  };

  const handleRemoveLfo = (lfoIndex: number) => {
    if (selectedTrackIndex == null) return;
    rpc.call("project.removeLfo", { trackIndex: selectedTrackIndex, lfoIndex }).then(fetchLfos);
  };

  const handleSetParam = (lfoIndex: number, paramName: string, value: number | boolean) => {
    if (selectedTrackIndex == null) return;
    rpc.call("project.setLfoParam", {
      trackIndex: selectedTrackIndex,
      lfoIndex,
      paramName,
      value: typeof value === "boolean" ? (value ? 1 : 0) : value,
    }).then(fetchLfos);
  };

  const trackName = snapshot?.tracks[selectedTrackIndex ?? -1]?.name ?? "No track selected";

  return (
    <div className="modulation-panel">
      <div className="mod-header">
        <span className="mod-title">Modulation — {trackName}</span>
        <button className="mod-add-btn" onClick={handleAddLfo}>+ Add LFO</button>
      </div>
      {lfos.length === 0 && (
        <div className="mod-empty">No LFOs. Click "+ Add LFO" to create one.</div>
      )}
      {lfos.map((lfo) => (
        <div key={lfo.index} className="mod-lfo-card">
          <div className="mod-lfo-header">
            <span className="mod-lfo-name">{lfo.name || `LFO ${lfo.index + 1}`}</span>
            <button className="mod-remove-btn" onClick={() => handleRemoveLfo(lfo.index)}>×</button>
          </div>
          <div className="mod-lfo-controls">
            <label>
              Wave
              <select
                value={lfo.waveform}
                onChange={(e) => handleSetParam(lfo.index, "waveform", Number(e.target.value))}
              >
                {WAVEFORMS.map((w, i) => (
                  <option key={i} value={i}>{w}</option>
                ))}
              </select>
            </label>
            <label>
              Rate
              <input
                type="range" min="0.1" max="20" step="0.1"
                value={lfo.rate}
                onChange={(e) => handleSetParam(lfo.index, "rate", Number(e.target.value))}
              />
              <span>{lfo.rate.toFixed(1)} Hz</span>
            </label>
            <label>
              Depth
              <input
                type="range" min="0" max="1" step="0.01"
                value={lfo.depth}
                onChange={(e) => handleSetParam(lfo.index, "depth", Number(e.target.value))}
              />
              <span>{Math.round(lfo.depth * 100)}%</span>
            </label>
            <label>
              Phase
              <input
                type="range" min="0" max="360" step="1"
                value={lfo.phaseOffset}
                onChange={(e) => handleSetParam(lfo.index, "phaseOffset", Number(e.target.value))}
              />
              <span>{Math.round(lfo.phaseOffset)}°</span>
            </label>
            <label className="mod-checkbox">
              <input
                type="checkbox"
                checked={lfo.bipolar}
                onChange={(e) => handleSetParam(lfo.index, "bipolar", e.target.checked)}
              />
              Bipolar
            </label>
            <label className="mod-checkbox">
              <input
                type="checkbox"
                checked={lfo.enabled}
                onChange={(e) => handleSetParam(lfo.index, "enabled", e.target.checked)}
              />
              Enabled
            </label>
          </div>
        </div>
      ))}
    </div>
  );
}
```

- [ ] **Step 2: Create ModulationPanel CSS**

```css
.modulation-panel { padding: 8px; height: 100%; overflow-y: auto; }
.mod-header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 12px; }
.mod-title { font-weight: 600; color: var(--text-primary); }
.mod-add-btn { background: var(--accent); color: #fff; border: none; border-radius: 4px; padding: 4px 12px; cursor: pointer; font-size: 12px; }
.mod-empty { color: var(--text-muted); font-size: 13px; }
.mod-lfo-card {
  background: var(--bg-widget); border: 1px solid var(--border);
  border-radius: 6px; padding: 10px; margin-bottom: 8px;
}
.mod-lfo-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
.mod-lfo-name { font-weight: 600; color: var(--accent); }
.mod-remove-btn { background: none; border: none; color: var(--danger); font-size: 18px; cursor: pointer; }
.mod-lfo-controls { display: flex; flex-wrap: wrap; gap: 12px; }
.mod-lfo-controls label { display: flex; flex-direction: column; gap: 2px; font-size: 11px; color: var(--text-secondary); }
.mod-lfo-controls select, .mod-lfo-controls input[type="range"] { width: 120px; }
.mod-checkbox { flex-direction: row !important; align-items: center; gap: 4px !important; }
```

- [ ] **Step 3: Add Modulation tab to App.tsx**

```tsx
import ModulationPanel from "./components/ModulationPanel";

// In bottomTabs array:
{ id: "modulation", label: "Modulation", content: <ModulationPanel /> },
```

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/ModulationPanel.tsx frontend/src/components/ModulationPanel.css frontend/src/App.tsx
git commit -m "ui: add modulation/LFO tab"
```

---

## Task 5: Clip Context Menu — Normalize, Reverse, Slice, Region Clipboard

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Add context menu items**

In `TimelineMinimal.tsx`, find the context menu rendering and add new items:

```tsx
// Add to the context menu, after existing items:
{selectedClipIds.size === 1 && (
  <>
    <div className="ctx-separator" />
    <button className="ctx-item" onClick={() => {
      const clipId = [...selectedClipIds][0];
      rpc.call("project.sliceClipAtPlayhead", { clipId });
    }}>
      Slice at Playhead
    </button>
    <button className="ctx-item" onClick={() => {
      const clipId = [...selectedClipIds][0];
      rpc.call("project.sliceClipAtTransients", { clipId });
    }}>
      Slice at Transients
    </button>
    <div className="ctx-separator" />
    <button className="ctx-item" onClick={() => {
      const clipId = [...selectedClipIds][0];
      rpc.call("project.copyAudioClipRegion", { clipId, regionStart: 0, regionEnd: 9999 });
    }}>
      Copy Region
    </button>
    <button className="ctx-item" onClick={() => {
      const clipId = [...selectedClipIds][0];
      rpc.call("project.cutAudioClipRegion", { clipId, regionStart: 0, regionEnd: 9999 });
    }}>
      Cut Region
    </button>
    <button className="ctx-item" onClick={() => {
      const clipId = [...selectedClipIds][0];
      rpc.call("project.pasteAudioClipRegion", { clipId, pasteTime: transport.currentTimeSeconds });
    }}>
      Paste Region
    </button>
  </>
)}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "ui: add slice, region clipboard to clip context menu"
```

---

## Task 6: Marker Editing (Rename, Delete)

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`
- Modify: `frontend/src/store/markerStore.ts`

- [ ] **Step 1: Add marker context menu**

In `TimelineMinimal.tsx`, add right-click handling on markers in the ruler:

```tsx
// In the ruler area, add onContextMenu for markers:
const handleMarkerContextMenu = (e: React.MouseEvent, markerIndex: number) => {
  e.preventDefault();
  e.stopPropagation();
  setContextMenu({
    x: e.clientX,
    y: e.clientY,
    type: "marker",
    markerIndex,
  });
};

// In context menu rendering, add marker case:
{contextMenu?.type === "marker" && (
  <>
    <button className="ctx-item" onClick={() => {
      const name = prompt("Marker name:");
      if (name != null) {
        rpc.call("project.setMarkerName", { index: contextMenu.markerIndex, name });
      }
      setContextMenu(null);
    }}>
      Rename Marker
    </button>
    <button className="ctx-item ctx-danger" onClick={() => {
      rpc.call("project.removeMarker", { index: contextMenu.markerIndex });
      setContextMenu(null);
    }}>
      Delete Marker
    </button>
  </>
)}
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "ui: add marker rename and delete context menu"
```

---

## Task 7: MIDI CC Lane in Piano Roll

**Files:**
- Create: `frontend/src/components/CCLane.tsx`
- Create: `frontend/src/components/CCLane.css`
- Modify: `frontend/src/components/PianoRoll.tsx`

- [ ] **Step 1: Create CCLane component**

```tsx
// frontend/src/components/CCLane.tsx
import { useRef, useCallback, useEffect, useState } from "react";
import { rpc } from "../rpc";
import "./CCLane.css";

interface CcPoint {
  beat: number;
  value: number;
}

interface CCLaneProps {
  clipId: number;
  controllerNumber: number;
  width: number;
  pixelsPerBeat: number;
  scrollX: number;
}

export default function CCLane({ clipId, controllerNumber, width, pixelsPerBeat, scrollX }: CCLaneProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [points, setPoints] = useState<CcPoint[]>([]);

  useEffect(() => {
    // Fetch CC points for this clip/controller
    // (Backend doesn't have a read method for CC yet — use notes as proxy or add one)
  }, [clipId, controllerNumber]);

  const handleCanvasClick = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left + scrollX;
    const beat = x / pixelsPerBeat;
    const value = Math.round(127 * (1 - (e.clientY - rect.top) / rect.height));
    rpc.call("project.addCcPoint", {
      clipId,
      controllerNumber,
      beat,
      value: Math.max(0, Math.min(127, value)),
    });
  }, [clipId, controllerNumber, pixelsPerBeat, scrollX]);

  return (
    <div className="cc-lane">
      <div className="cc-label">CC{controllerNumber}</div>
      <canvas
        ref={canvasRef}
        width={width}
        height={60}
        className="cc-canvas"
        onClick={handleCanvasClick}
      />
    </div>
  );
}
```

- [ ] **Step 2: Create CCLane CSS**

```css
.cc-lane { display: flex; align-items: center; gap: 4px; }
.cc-label { font-size: 10px; color: var(--text-muted); width: 36px; text-align: right; }
.cc-canvas { background: var(--bg-widget); border: 1px solid var(--border); border-radius: 2px; cursor: crosshair; }
```

- [ ] **Step 3: Add CC Lane to PianoRoll**

In `PianoRoll.tsx`, add a CC lane selector and the CCLane component below the velocity lane:

```tsx
import CCLane from "./CCLane";

// Add state:
const [ccController, setCcController] = useState(1); // CC1 = Mod Wheel

// Add below VelocityLaneWidget:
<div className="pr-cc-row">
  <select value={ccController} onChange={(e) => setCcController(Number(e.target.value))}>
    {Array.from({ length: 128 }, (_, i) => (
      <option key={i} value={i}>CC{i}</option>
    ))}
  </select>
  {activeClipId && (
    <CCLane
      clipId={activeClipId}
      controllerNumber={ccController}
      width={gridWidth}
      pixelsPerBeat={pixelsPerBeat}
      scrollX={scrollX}
    />
  )}
</div>
```

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/CCLane.tsx frontend/src/components/CCLane.css frontend/src/components/PianoRoll.tsx
git commit -m "ui: add MIDI CC lane to piano roll"
```

---

## Task 8: Chord Stamps in Piano Roll

**Files:**
- Modify: `frontend/src/components/PianoRoll.tsx`
- Modify: `frontend/src/components/NoteGrid.tsx`

- [ ] **Step 1: Add chord stamp state and UI to PianoRoll**

```tsx
// In PianoRoll.tsx, add state:
const [chordEnabled, setChordEnabled] = useState(false);
const [chordType, setChordType] = useState("major");

const CHORD_SHAPES: Record<string, number[]> = {
  major: [0, 4, 7],
  minor: [0, 3, 7],
  diminished: [0, 3, 6],
  augmented: [0, 4, 8],
  maj7: [0, 4, 7, 11],
  min7: [0, 3, 7, 10],
  dom7: [0, 4, 7, 10],
  sus2: [0, 2, 7],
  sus4: [0, 5, 7],
};

// Add to the toolbar area:
<label className="pr-chord-toggle">
  <input
    type="checkbox"
    checked={chordEnabled}
    onChange={(e) => setChordEnabled(e.target.checked)}
  />
  Chord
</label>
{chordEnabled && (
  <select value={chordType} onChange={(e) => setChordType(e.target.value)}>
    {Object.keys(CHORD_SHAPES).map((name) => (
      <option key={name} value={name}>{name}</option>
    ))}
  </select>
)}
```

- [ ] **Step 2: Pass chord shapes to NoteGrid**

In `NoteGrid.tsx`, accept a `chordShape` prop. On click, if chord is enabled, create multiple notes at the clicked pitch + chord intervals:

```tsx
// Add to NoteGridProps:
chordShape?: number[];

// In the click handler, after creating the base note:
if (chordShape && chordShape.length > 0) {
  for (const interval of chordShape.slice(1)) {
    // Create additional notes at pitch + interval
    rpc.call("project.addNote", {
      clipId,
      pitch: pitch + interval,
      velocity,
      startBeat: beat,
      durationBeats: duration,
    });
  }
}
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/PianoRoll.tsx frontend/src/components/NoteGrid.tsx
git commit -m "ui: add chord stamps to piano roll"
```

---

## Task 9: Plugin Manager Dialog

**Files:**
- Create: `frontend/src/components/PluginManagerDialog.tsx`
- Create: `frontend/src/components/PluginManagerDialog.css`
- Modify: `frontend/src/components/TransportBar.tsx` (add menu item)

- [ ] **Step 1: Create PluginManagerDialog**

```tsx
// frontend/src/components/PluginManagerDialog.tsx
import { useState, useEffect, useCallback } from "react";
import { rpc } from "../rpc";
import "./PluginManagerDialog.css";

interface PluginInfo {
  name: string;
  format: string;
  manufacturer: string;
  fileOrIdentifier: string;
  isInstrument: boolean;
}

interface Props {
  onClose: () => void;
}

export default function PluginManagerDialog({ onClose }: Props) {
  const [plugins, setPlugins] = useState<PluginInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [filter, setFilter] = useState("");
  const [blacklisted, setBlacklisted] = useState<Set<string>>(new Set());

  const fetchPlugins = useCallback(async () => {
    try {
      const result = await rpc.call("plugin.getPlugins") as PluginInfo[];
      setPlugins(result);
      // Check blacklist status for each
      const bl = new Set<string>();
      for (const p of result) {
        const isBl = await rpc.call("plugin.isBlacklisted", { pluginID: p.fileOrIdentifier }) as boolean;
        if (isBl) bl.add(p.fileOrIdentifier);
      }
      setBlacklisted(bl);
    } catch (e) { console.error(e); }
  }, []);

  useEffect(() => { fetchPlugins(); }, [fetchPlugins]);

  const handleScan = async () => {
    setLoading(true);
    try { await rpc.call("plugin.scanAll"); } catch (e) { console.error(e); }
    setLoading(false);
    fetchPlugins();
  };

  const handleToggleBlacklist = async (pluginID: string) => {
    if (blacklisted.has(pluginID)) {
      await rpc.call("plugin.unblacklistPlugin", { pluginID });
    } else {
      await rpc.call("plugin.blacklistPlugin", { pluginID });
    }
    fetchPlugins();
  };

  const filtered = plugins.filter((p) =>
    p.name.toLowerCase().includes(filter.toLowerCase()) ||
    p.manufacturer.toLowerCase().includes(filter.toLowerCase())
  );

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="plugin-manager" onClick={(e) => e.stopPropagation()}>
        <div className="pm-header">
          <h2>Plugin Manager</h2>
          <button className="pm-close" onClick={onClose}>×</button>
        </div>
        <div className="pm-toolbar">
          <input
            className="pm-filter"
            placeholder="Filter plugins..."
            value={filter}
            onChange={(e) => setFilter(e.target.value)}
          />
          <button className="pm-scan-btn" onClick={handleScan} disabled={loading}>
            {loading ? "Scanning..." : "Rescan"}
          </button>
        </div>
        <div className="pm-list">
          {filtered.map((p) => (
            <div key={p.fileOrIdentifier} className={`pm-item${blacklisted.has(p.fileOrIdentifier) ? " blacklisted" : ""}`}>
              <div className="pm-item-info">
                <span className="pm-item-name">{p.name}</span>
                <span className="pm-item-meta">{p.manufacturer} — {p.format}</span>
              </div>
              <button
                className="pm-bl-btn"
                onClick={() => handleToggleBlacklist(p.fileOrIdentifier)}
              >
                {blacklisted.has(p.fileOrIdentifier) ? "Unblacklist" : "Blacklist"}
              </button>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Create PluginManagerDialog CSS**

```css
.modal-overlay { position: fixed; inset: 0; background: rgba(0,0,0,0.6); display: flex; align-items: center; justify-content: center; z-index: 1000; }
.plugin-manager { background: var(--bg-panel); border: 1px solid var(--border); border-radius: 8px; width: 600px; max-height: 80vh; display: flex; flex-direction: column; }
.pm-header { display: flex; justify-content: space-between; align-items: center; padding: 12px 16px; border-bottom: 1px solid var(--border); }
.pm-header h2 { margin: 0; font-size: 16px; color: var(--text-primary); }
.pm-close { background: none; border: none; color: var(--text-secondary); font-size: 20px; cursor: pointer; }
.pm-toolbar { display: flex; gap: 8px; padding: 8px 16px; }
.pm-filter { flex: 1; background: var(--bg-widget); border: 1px solid var(--border); border-radius: 4px; padding: 6px 8px; color: var(--text-primary); font-size: 13px; }
.pm-scan-btn { background: var(--accent); color: #fff; border: none; border-radius: 4px; padding: 6px 16px; cursor: pointer; }
.pm-list { flex: 1; overflow-y: auto; padding: 8px 16px; }
.pm-item { display: flex; justify-content: space-between; align-items: center; padding: 8px; border-radius: 4px; }
.pm-item:hover { background: var(--bg-widget); }
.pm-item.blacklisted { opacity: 0.5; }
.pm-item-name { color: var(--text-primary); font-size: 13px; }
.pm-item-meta { color: var(--text-muted); font-size: 11px; }
.pm-item-info { display: flex; flex-direction: column; gap: 2px; }
.pm-bl-btn { background: var(--bg-widget); border: 1px solid var(--border); border-radius: 4px; padding: 4px 8px; font-size: 11px; cursor: pointer; }
```

- [ ] **Step 3: Add Plugin Manager to TransportBar menu**

In `TransportBar.tsx`, add a Tools dropdown or button that opens the dialog:

```tsx
import PluginManagerDialog from "./PluginManagerDialog";

// Add state:
const [showPluginManager, setShowPluginManager] = useState(false);

// Add button in transport area:
<button className="tb-btn" onClick={() => setShowPluginManager(true)} title="Plugin Manager">
  🎛️
</button>

// Add dialog at end of JSX:
{showPluginManager && <PluginManagerDialog onClose={() => setShowPluginManager(false)} />}
```

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/PluginManagerDialog.tsx frontend/src/components/PluginManagerDialog.css frontend/src/components/TransportBar.tsx
git commit -m "ui: add plugin manager dialog"
```

---

## Task 10: Preferences Dialog

**Files:**
- Create: `frontend/src/components/PreferencesDialog.tsx`
- Create: `frontend/src/components/PreferencesDialog.css`
- Modify: `frontend/src/components/TransportBar.tsx` (add menu item)

- [ ] **Step 1: Create PreferencesDialog**

```tsx
// frontend/src/components/PreferencesDialog.tsx
import { useState, useEffect, useCallback } from "react";
import { rpc } from "../rpc";
import "./PreferencesDialog.css";

interface Props {
  onClose: () => void;
}

export default function PreferencesDialog({ onClose }: Props) {
  const [midiDevices, setMidiDevices] = useState<string[]>([]);
  const [activeDevice, setActiveDevice] = useState<string>("");
  const [sampleRate, setSampleRate] = useState(44100);
  const [bufferSize, setBufferSize] = useState(512);

  useEffect(() => {
    rpc.call("midi.getAvailableDevices").then((d: any) => setMidiDevices(d)).catch(() => {});
  }, []);

  const handleOpenDevice = async (device: string) => {
    await rpc.call("midi.openDevice", { identifier: device });
    setActiveDevice(device);
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="preferences-dialog" onClick={(e) => e.stopPropagation()}>
        <div className="pref-header">
          <h2>Preferences</h2>
          <button className="pref-close" onClick={onClose}>×</button>
        </div>
        <div className="pref-body">
          <section className="pref-section">
            <h3>MIDI</h3>
            <label>
              Input Device
              <select
                value={activeDevice}
                onChange={(e) => handleOpenDevice(e.target.value)}
              >
                <option value="">None</option>
                {midiDevices.map((d) => (
                  <option key={d} value={d}>{d}</option>
                ))}
              </select>
            </label>
          </section>
          <section className="pref-section">
            <h3>Audio</h3>
            <label>
              Sample Rate
              <select value={sampleRate} onChange={(e) => setSampleRate(Number(e.target.value))}>
                <option value={44100}>44100 Hz</option>
                <option value={48000}>48000 Hz</option>
                <option value={96000}>96000 Hz</option>
              </select>
            </label>
            <label>
              Buffer Size
              <select value={bufferSize} onChange={(e) => setBufferSize(Number(e.target.value))}>
                <option value={128}>128</option>
                <option value={256}>256</option>
                <option value={512}>512</option>
                <option value={1024}>1024</option>
                <option value={2048}>2048</option>
              </select>
            </label>
          </section>
          <section className="pref-section">
            <h3>MCP Server</h3>
            <p className="pref-note">MCP server settings are configured via the C++ backend.</p>
          </section>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Create PreferencesDialog CSS**

```css
.preferences-dialog { background: var(--bg-panel); border: 1px solid var(--border); border-radius: 8px; width: 480px; max-height: 80vh; display: flex; flex-direction: column; }
.pref-header { display: flex; justify-content: space-between; align-items: center; padding: 12px 16px; border-bottom: 1px solid var(--border); }
.pref-header h2 { margin: 0; font-size: 16px; color: var(--text-primary); }
.pref-close { background: none; border: none; color: var(--text-secondary); font-size: 20px; cursor: pointer; }
.pref-body { padding: 16px; overflow-y: auto; }
.pref-section { margin-bottom: 20px; }
.pref-section h3 { margin: 0 0 8px; font-size: 14px; color: var(--accent); }
.pref-section label { display: flex; flex-direction: column; gap: 4px; font-size: 12px; color: var(--text-secondary); margin-bottom: 8px; }
.pref-section select { background: var(--bg-widget); border: 1px solid var(--border); border-radius: 4px; padding: 6px; color: var(--text-primary); }
.pref-note { font-size: 12px; color: var(--text-muted); }
```

- [ ] **Step 3: Add Preferences to TransportBar**

```tsx
import PreferencesDialog from "./PreferencesDialog";

const [showPrefs, setShowPrefs] = useState(false);

// Add button:
<button className="tb-btn" onClick={() => setShowPrefs(true)} title="Preferences">⚙</button>

// Add dialog:
{showPrefs && <PreferencesDialog onClose={() => setShowPrefs(false)} />}
```

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/PreferencesDialog.tsx frontend/src/components/PreferencesDialog.css frontend/src/components/TransportBar.tsx
git commit -m "ui: add preferences dialog"
```

---

## Task 11: Export Dialog — Bit Depth

**Files:**
- Modify: `frontend/src/components/ExportDialog.tsx`

- [ ] **Step 1: Add bit depth selector**

In `ExportDialog.tsx`, add a bit depth state and select:

```tsx
const [bitDepth, setBitDepth] = useState(24);

// Add to the form, after format select:
<label>
  Bit Depth
  <select value={bitDepth} onChange={(e) => setBitDepth(Number(e.target.value))}>
    <option value={16}>16-bit</option>
    <option value={24}>24-bit</option>
    <option value={32}>32-bit float</option>
  </select>
</label>

// Pass to export call:
rpc.call("project.exportAudio", { filePath: outputPath, format, bitDepth });
```

- [ ] **Step 2: Commit**

```bash
git add frontend/src/components/ExportDialog.tsx
git commit -m "ui: add bit depth selector to export dialog"
```

---

## Task 12: Timeline — Zoom to Fit Selection, Drag-and-Drop Import

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Add Zoom to Fit Selection**

In `TimelineMinimal.tsx`, add a keyboard shortcut handler for Shift+F:

```tsx
// In the keydown handler:
if (e.key === "F" && e.shiftKey) {
  // Zoom to fit selection
  const selectedClips = snapshot?.clips.filter((c) => selectedClipIds.has(c.clipId));
  if (selectedClips && selectedClips.length > 0) {
    const minStart = Math.min(...selectedClips.map((c) => c.startBeat));
    const maxEnd = Math.max(...selectedClips.map((c) => c.startBeat + c.durationBeats));
    const range = maxEnd - minStart;
    if (range > 0) {
      const newPps = (containerWidth * 0.8) / range;
      setPixelsPerBeat(Math.max(10, Math.min(200, newPps)));
      setScrollX(minStart * pixelsPerBeat - containerWidth * 0.1);
    }
  }
  e.preventDefault();
}
```

- [ ] **Step 2: Add file drag-and-drop import**

In `TimelineMinimal.tsx`, add drop handling on the timeline container:

```tsx
const handleDrop = useCallback((e: React.DragEvent) => {
  e.preventDefault();
  const files = Array.from(e.dataTransfer.files);
  const audioExts = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
  const midiExts = [".mid", ".midi"];

  for (const file of files) {
    const ext = "." + file.name.split(".").pop()?.toLowerCase();
    if (audioExts.includes(ext)) {
      rpc.call("project.addAudioClip", {
        trackIndex: 0, // TODO: determine track from drop Y position
        start: transport.currentTimeSeconds * (transport.bpm / 60),
        duration: 4,
        sourceFile: file.path,
        name: file.name,
      });
    } else if (midiExts.includes(ext)) {
      // Use MIDI import
      rpc.call("project.importMidi", { filePath: file.path, trackIndex: 0 });
    }
  }
  useProjectStore.getState().syncSnapshot(rpc);
}, [transport]);

// On the timeline container div:
<div className="timeline-container" onDragOver={(e) => e.preventDefault()} onDrop={handleDrop}>
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "ui: add zoom-to-fit-selection and file drag-and-drop import"
```

---

## Task 13: Startup Dialog

**Files:**
- Create: `frontend/src/components/StartupDialog.tsx`
- Create: `frontend/src/components/StartupDialog.css`
- Modify: `frontend/src/main.tsx`

- [ ] **Step 1: Create StartupDialog**

```tsx
// frontend/src/components/StartupDialog.tsx
import { useState, useEffect } from "react";
import { rpc } from "../rpc";
import "./StartupDialog.css";

interface Props {
  onClose: () => void;
}

export default function StartupDialog({ onClose }: Props) {
  const [recentProjects, setRecentProjects] = useState<string[]>([]);

  useEffect(() => {
    const stored = localStorage.getItem("hdaw_recent_projects");
    if (stored) setRecentProjects(JSON.parse(stored));
  }, []);

  const handleNew = async () => {
    await rpc.call("project.newProject");
    onClose();
  };

  const handleOpen = async (path?: string) => {
    if (!path) {
      // Would use Electron file dialog — for now prompt
      path = prompt("Project file path:");
    }
    if (path) {
      await rpc.call("project.loadProject", { filePath: path });
      onClose();
    }
  };

  const handleOpenRecent = async (path: string) => {
    await rpc.call("project.loadProject", { filePath: path });
    onClose();
  };

  return (
    <div className="modal-overlay">
      <div className="startup-dialog">
        <h1>HDAW</h1>
        <p className="startup-version">v0.9.1</p>
        <div className="startup-actions">
          <button className="startup-btn primary" onClick={handleNew}>New Project</button>
          <button className="startup-btn" onClick={() => handleOpen()}>Open Project...</button>
        </div>
        {recentProjects.length > 0 && (
          <div className="startup-recent">
            <h3>Recent Projects</h3>
            {recentProjects.slice(0, 8).map((p) => (
              <button key={p} className="startup-recent-item" onClick={() => handleOpenRecent(p)}>
                {p.split(/[\\/]/).pop()}
              </button>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}
```

- [ ] **Step 2: Create StartupDialog CSS**

```css
.startup-dialog { background: var(--bg-panel); border: 1px solid var(--border); border-radius: 12px; padding: 32px; width: 400px; text-align: center; }
.startup-dialog h1 { margin: 0; font-size: 28px; color: var(--accent); }
.startup-version { color: var(--text-muted); font-size: 13px; margin: 4px 0 24px; }
.startup-actions { display: flex; flex-direction: column; gap: 8px; margin-bottom: 20px; }
.startup-btn { background: var(--bg-widget); border: 1px solid var(--border); border-radius: 6px; padding: 10px 16px; color: var(--text-primary); font-size: 14px; cursor: pointer; }
.startup-btn:hover { background: var(--bg-elevated); }
.startup-btn.primary { background: var(--accent); color: #fff; border-color: var(--accent); }
.startup-btn.primary:hover { background: color-mix(in srgb, var(--accent), #fff 15%); }
.startup-recent { text-align: left; }
.startup-recent h3 { font-size: 12px; color: var(--text-muted); margin: 0 0 8px; }
.startup-recent-item { display: block; width: 100%; background: none; border: none; padding: 6px 8px; color: var(--text-secondary); font-size: 12px; text-align: left; cursor: pointer; border-radius: 4px; }
.startup-recent-item:hover { background: var(--bg-widget); color: var(--text-primary); }
```

- [ ] **Step 3: Show StartupDialog in main.tsx**

In `frontend/src/main.tsx`, add state to show the dialog on first load:

```tsx
import StartupDialog from "./components/StartupDialog";

// Add state:
const [showStartup, setShowStartup] = useState(true);

// Render:
{showStartup && <StartupDialog onClose={() => setShowStartup(false)} />}
```

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/StartupDialog.tsx frontend/src/components/StartupDialog.css frontend/src/main.tsx
git commit -m "ui: add startup dialog"
```

---

## Task 14: Status Bar

**Files:**
- Create: `frontend/src/components/StatusBar.tsx`
- Create: `frontend/src/components/StatusBar.css`
- Modify: `frontend/src/App.tsx`

- [ ] **Step 1: Create StatusBar**

```tsx
// frontend/src/components/StatusBar.tsx
import { useTransportStore } from "../store/transportStore";
import { useProjectStore } from "../store/projectStore";
import { useUiStore } from "../store/uiStore";
import "./StatusBar.css";

export default function StatusBar() {
  const transport = useTransportStore((s) => s.transport);
  const snapshot = useProjectStore((s) => s.snapshot);
  const selectedTrackIndex = useUiStore((s) => s.selectedTrackIndex);
  const selectedClipIds = useUiStore((s) => s.selectedClipIds);

  const selectedTrack = selectedTrackIndex != null ? snapshot?.tracks[selectedTrackIndex] : null;

  return (
    <div className="status-bar">
      <span className="sb-field">♩ {transport.bpm.toFixed(1)}</span>
      <span className="sb-field">{transport.sampleRate} Hz</span>
      {selectedTrack && (
        <span className="sb-field">Track: {selectedTrack.name}</span>
      )}
      <span className="sb-field">{selectedClipIds.size} selected</span>
      {transport.isRecording && <span className="sb-field sb-rec">● REC</span>}
    </div>
  );
}
```

- [ ] **Step 2: Create StatusBar CSS**

```css
.status-bar {
  display: flex; align-items: center; gap: 16px;
  padding: 2px 12px; background: var(--bg-panel);
  border-top: 1px solid var(--border); font-size: 11px; color: var(--text-muted);
}
.sb-field { white-space: nowrap; }
.sb-rec { color: var(--danger); font-weight: 600; }
```

- [ ] **Step 3: Add StatusBar to App.tsx**

```tsx
import StatusBar from "./components/StatusBar";

// Add at the end of the app-shell div, before closing </div>:
<StatusBar />
```

- [ ] **Step 4: Update App.css grid**

In `App.css`, add a row for the status bar in the grid layout.

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/StatusBar.tsx frontend/src/components/StatusBar.css frontend/src/App.tsx frontend/src/App.css
git commit -m "ui: add status bar"
```

---

## Task 15: Keyboard Shortcuts — Global

**Files:**
- Create: `frontend/src/hooks/useKeyboardShortcuts.ts`
- Modify: `frontend/src/main.tsx`

- [ ] **Step 1: Create global keyboard shortcuts hook**

```tsx
// frontend/src/hooks/useKeyboardShortcuts.ts
import { useEffect } from "react";
import { rpc } from "../rpc";
import { useUiStore } from "../store/uiStore";
import { useProjectStore } from "../store/projectStore";

export function useKeyboardShortcuts() {
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT") return;

      const ctrl = e.ctrlKey || e.metaKey;
      const shift = e.shiftKey;

      // Transport
      if (e.code === "Space" && !ctrl) {
        e.preventDefault();
        const playing = useTransportStore.getState().transport.isPlaying;
        rpc.call(playing ? "transport.pause" : "transport.play");
      }
      if (e.code === "Space" && shift) {
        e.preventDefault();
        rpc.call("transport.stop");
      }
      if (e.code === "KeyR" && !ctrl) {
        e.preventDefault();
        rpc.call("transport.record");
      }
      if (e.code === "Home") {
        e.preventDefault();
        rpc.call("transport.rewind");
      }

      // Undo/Redo
      if (ctrl && e.key === "z" && !shift) {
        e.preventDefault();
        rpc.call("project.undo").then(() => {
          useProjectStore.getState().syncSnapshot(rpc);
        });
      }
      if (ctrl && e.key === "z" && shift) {
        e.preventDefault();
        rpc.call("project.redo").then(() => {
          useProjectStore.getState().syncSnapshot(rpc);
        });
      }
      if (ctrl && e.key === "Z") {
        e.preventDefault();
        rpc.call("project.redo").then(() => {
          useProjectStore.getState().syncSnapshot(rpc);
        });
      }

      // Project
      if (ctrl && e.key === "n") { e.preventDefault(); rpc.call("project.newProject"); }
      if (ctrl && e.key === "s" && !shift) { e.preventDefault(); /* save */ }
      if (ctrl && e.key === "S") { e.preventDefault(); /* save as */ }
      if (ctrl && e.key === "l") { e.preventDefault(); rpc.call("transport.toggleLoop"); }

      // Track
      if (ctrl && shift && e.key === "T") { e.preventDefault(); rpc.call("project.addTrack"); }
    };

    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, []);
}
```

- [ ] **Step 2: Wire into main.tsx**

```tsx
import { useKeyboardShortcuts } from "./hooks/useKeyboardShortcuts";

// Inside App component:
useKeyboardShortcuts();
```

- [ ] **Step 3: Commit**

```bash
git add frontend/src/hooks/useKeyboardShortcuts.ts frontend/src/main.tsx
git commit -m "ui: add global keyboard shortcuts"
```

---

## Execution Order

| Order | Task | Dependencies | Estimated Effort |
|-------|------|-------------|-----------------|
| 1 | Task 1: Transport Bar Controls | None | Small |
| 2 | Task 2: Track Header Enhancements | None | Small |
| 3 | Task 14: Status Bar | None | Small |
| 4 | Task 15: Keyboard Shortcuts | None | Medium |
| 5 | Task 3: Step Sequencer | None | Medium |
| 6 | Task 4: Modulation/LFO Tab | None | Medium |
| 7 | Task 5: Clip Context Menu | None | Small |
| 8 | Task 6: Marker Editing | None | Small |
| 9 | Task 7: MIDI CC Lane | None | Medium |
| 10 | Task 8: Chord Stamps | Task 7 | Small |
| 11 | Task 9: Plugin Manager | None | Medium |
| 12 | Task 10: Preferences Dialog | None | Medium |
| 13 | Task 11: Export Bit Depth | None | Small |
| 14 | Task 12: Zoom/Import | None | Small |
| 15 | Task 13: Startup Dialog | None | Small |

Tasks 1–6, 11, 12, 14, 15 are independent and can be parallelized.
Tasks 7 and 8 are sequential (CC Lane before Chord Stamps).
