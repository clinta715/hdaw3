# Codebase Refactoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce code duplication, split monolithic files into domain-focused modules, and extract reusable utilities across the HDAW codebase.

**Architecture:** Phase 1 extracts helpers and utilities from the largest files. Phase 2 splits `AudioEngineCommands.cpp` into domain-specific translation units. Phase 3 extracts hooks from `TimelineMinimal.tsx`. Phase 4 splits `McpTools.cpp` and `MainWindow.cpp`. Each phase is independently committable and testable.

**Tech Stack:** C++17, JUCE 8, React 19, TypeScript, Zustand, Vite

---

## Phase 1: Extract Helpers and Utilities (Low Risk)

### Task 1: Create AudioEngineCommands Helpers

**Files:**
- Create: `src/engine/AudioEngineCommands_Helpers.h`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create helpers header with shared utilities**

```cpp
// AudioEngineCommands_Helpers.h
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <utility>

namespace HDAW {

// Returns {undoManager, track} or {um, {}} if out of range.
inline std::pair<juce::UndoManager*, juce::ValueTree> getTrack(
    juce::ValueTree trackList, int trackIndex, juce::UndoManager& um)
{
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return { &um, {} };
    return { &um, trackList.getChild(trackIndex) };
}

// Get or create a child list under parent.
inline juce::ValueTree getOrCreateChild(
    juce::ValueTree parent, const juce::Identifier& childId, juce::UndoManager& um)
{
    auto child = parent.getChildWithName(childId);
    if (!child.isValid())
    {
        child = juce::ValueTree(childId);
        parent.addChild(child, -1, &um);
    }
    return child;
}

// Set a property on a ValueTree with undo.
template<typename T>
inline void setProp(juce::ValueTree tree, const juce::Identifier& prop, T value, juce::UndoManager* um)
{
    if (tree.isValid())
        tree.setProperty(prop, value, um);
}

} // namespace HDAW
```

- [ ] **Step 2: Include helpers header in AudioEngineCommands.cpp**

Add at top of `src/engine/AudioEngineCommands.cpp`:
```cpp
#include "AudioEngineCommands_Helpers.h"
```

- [ ] **Step 3: Run build to verify no regressions**

Run: `cmake --build build --config Debug`
Expected: Build succeeds with no errors

- [ ] **Step 4: Commit**

```bash
git add src/engine/AudioEngineCommands_Helpers.h src/engine/AudioEngineCommands.cpp
git commit -m "refactor: add AudioEngineCommands helper utilities"
```

---

### Task 2: Create Frontend Timeline Utilities

**Files:**
- Create: `frontend/src/utils/timelineUtils.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create timeline utilities module**

```typescript
// frontend/src/utils/timelineUtils.ts

export const AUDIO_EXTENSIONS = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
export const MIDI_EXTENSIONS = [".mid", ".midi"];

export function isAudioFile(name: string): boolean {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return AUDIO_EXTENSIONS.includes(ext);
}

export function isMidiFile(name: string): boolean {
  const ext = "." + name.split(".").pop()?.toLowerCase();
  return MIDI_EXTENSIONS.includes(ext);
}

export function clientXToBeat(
  clientX: number,
  containerEl: HTMLElement,
  pps: number
): number {
  const rect = containerEl.getBoundingClientRect();
  const scroll = containerEl.scrollLeft;
  return Math.max(0, (clientX - rect.left + scroll) / pps);
}

import { useUiStore } from "../store/uiStore";
import { snapToGrid } from "../components/snapUtils";

export function snapBeat(beat: number): number {
  const { snapEnabled, snapDivision } = useUiStore.getState();
  return snapEnabled ? snapToGrid(beat, snapDivision) : beat;
}

import { useProjectStore } from "../store/projectStore";
import { rpc } from "../rpc";

export async function syncAfterMutation(): Promise<void> {
  await useProjectStore.getState().syncDirtyFlag(rpc);
  await useProjectStore.getState().syncSnapshot(rpc);
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/utils/timelineUtils.ts
git commit -m "refactor: add timeline utility functions"
```

---

### Task 3: Create Frontend Timeline Constants

**Files:**
- Create: `frontend/src/utils/timelineConstants.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create timeline constants module**

```typescript
// frontend/src/utils/timelineConstants.ts

export const TRACK_HEIGHT = 64;
export const RULER_HEIGHT = 32;
export const DEFAULT_PPS = 80; // pixels per second
export const MIN_PPS = 20;
export const MAX_PPS = 400;

export interface DragState {
  clipId: number;
  startTrackIndex: number;
  startBeat: number;
  offsetX: number;
  offsetY: number;
  mouseX: number;
  mouseY: number;
  isDuplicate?: boolean;
  isGhostClone?: boolean;
  paintRepeat?: boolean;
  paintOriginBeat: number;
  paintSpacing: number;
  paintedClipIds: number[];
}

export interface TrimState {
  clipId: number;
  side: "left" | "right";
  initialStartBeat: number;
  initialDuration: number;
  currentStartBeat: number;
  currentDuration: number;
}

export interface FadeDrag {
  clipId: number;
  side: "in" | "out";
  initialValue: number;
}

export function computeRubberBandSelection(
  x1: number, y1: number, x2: number, y2: number,
  clips: Array<{ clipId: number; trackIndex: number; startBeat: number; durationBeats: number }>,
  pps: number
): Set<number> {
  const selected = new Set<number>();
  const minBeat = Math.min(x1, x2) / pps;
  const maxBeat = Math.max(x1, x2) / pps;
  const minTrack = Math.min(y1, y2) / TRACK_HEIGHT;
  const maxTrack = Math.max(y1, y2) / TRACK_HEIGHT;

  for (const clip of clips) {
    const clipEnd = clip.startBeat + clip.durationBeats;
    if (clip.startBeat <= maxBeat && clipEnd >= minBeat &&
        clip.trackIndex >= minTrack && clip.trackIndex <= maxTrack) {
      selected.add(clip.clipId);
    }
  }
  return selected;
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/utils/timelineConstants.ts
git commit -m "refactor: add timeline constants and types"
```

---

## Phase 2: Split AudioEngineCommands.cpp (Low Risk)

### Task 4: Split Clip Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_Clips.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create Clips domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_Clips.cpp`:
- `addAudioClip`
- `addMidiClip`
- `removeClip`
- `moveClip`
- `moveClipWithOverlap`
- `setClipStart`
- `setClipDuration`
- `setClipGain`
- `setClipFadeIn`
- `setClipFadeOut`
- `setClipOffset`
- `setClipLooping`
- `setClipMuted`
- `duplicateClip`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_Clips.cpp
git commit -m "refactor: extract clip operations to separate file"
```

---

### Task 5: Split Transport Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_Transport.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create Transport domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_Transport.cpp`:
- `setTempo`
- `addTempoPoint`
- `removeTempoPoint`
- `setTempoPointBpm`
- `setTempoPointTime`
- `setLoopStart`
- `setLoopEnd`
- `setLooping`
- `setMetronomeEnabled`
- `setTimeSignature`
- `play`
- `stop`
- `pause`
- `rewind`
- `toggleLoop`
- `seekToSeconds`
- `record`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
#include "../engine/TransportManager.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_Transport.cpp
git commit -m "refactor: extract transport operations to separate file"
```

---

### Task 6: Split FX Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_Fx.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create FX domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_Fx.cpp`:
- `addFxSlot` (both overloads)
- `removeFxSlot`
- `setFxSlotBypassed`
- `setFxSlotParam`
- `reorderFxSlots`
- `setFxSlotPlugin`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
#include "../engine/PluginManager.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_Fx.cpp
git commit -m "refactor: extract FX operations to separate file"
```

---

### Task 7: Split Timestretch Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_Timestretch.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create Timestretch domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_Timestretch.cpp`:
- `setClipSourceBpm`
- `setClipStretchMode`
- `setClipStretchRatio`
- `tempoMatchClip`
- `fitClipToLoop`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
#include "../engine/StretchCache.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_Timestretch.cpp
git commit -m "refactor: extract timestretch operations to separate file"
```

---

### Task 8: Split Slicing Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_Slicing.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create Slicing domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_Slicing.cpp`:
- `sliceClipAtTimes`
- `sliceClipAtTransients`
- `sliceClipAtPlayhead`
- `copyAudioClipRegion`
- `cutAudioClipRegion`
- `pasteAudioClipRegion`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioImport.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_Slicing.cpp
git commit -m "refactor: extract slicing operations to separate file"
```

---

### Task 9: Split Gain Envelope Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_GainEnvelope.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create GainEnvelope domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_GainEnvelope.cpp`:
- `addGainEnvelopePoint`
- `moveGainEnvelopePoint`
- `removeGainEnvelopePoint`
- `clearGainEnvelope`
- `setClipGainEnvelope`
- `notifyClipGainEnvelopeChanged`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_GainEnvelope.cpp
git commit -m "refactor: extract gain envelope operations to separate file"
```

---

### Task 10: Split MIDI Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_Midi.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create MIDI domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_Midi.cpp`:
- `addNote`
- `removeNote`
- `setNotePitch`
- `setNoteVelocity`
- `setNoteStart`
- `setNoteDuration`
- `clearNotes`
- `addCcPoint`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_Midi.cpp
git commit -m "refactor: extract MIDI operations to separate file"
```

---

### Task 11: Split Automation Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_Automation.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create Automation domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_Automation.cpp`:
- `addAutomationLane`
- `removeAutomationLane`
- `addAutomationPoint`
- `removeAutomationPoint`
- `setAutomationEnabled`
- `setAutomationPointValue`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_Automation.cpp
git commit -m "refactor: extract automation operations to separate file"
```

---

### Task 12: Split Ghost/Paint Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_GhostPaint.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create GhostPaint domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_GhostPaint.cpp`:
- `createGhostClip`
- `paintClips`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_GhostPaint.cpp
git commit -m "refactor: extract ghost/paint operations to separate file"
```

---

### Task 13: Split Markers and Remaining Operations

**Files:**
- Create: `src/engine/AudioEngineCommands_Markers.cpp`
- Modify: `src/engine/AudioEngineCommands.cpp`

- [ ] **Step 1: Create Markers domain file**

Move the following methods from `AudioEngineCommands.cpp` to `AudioEngineCommands_Markers.cpp`:
- `addMarker`
- `removeMarker`
- `setMarkerName`
- `setMarkerTime`

Also move remaining methods:
- `duplicateTrack`
- `undo`
- `redo`
- `canUndo`
- `canRedo`
- `beginTransaction`
- `endTransaction`
- `newProject`
- `saveProject`
- `loadProject`
- `setScaleRoot`
- `setScaleMode`
- `findMissingClipSourceFile`
- `relinkAllMissingFiles`

The new file should include:
```cpp
#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/engine/AudioEngineCommands_Markers.cpp
git commit -m "refactor: extract markers and remaining operations"
```

---

## Phase 3: Extract TimelineMinimal Hooks (Medium Risk)

### Task 14: Extract useTimelineDrag Hook

**Files:**
- Create: `frontend/src/hooks/useTimelineDrag.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create the drag hook**

Move the following from `TimelineMinimal.tsx` to `useTimelineDrag.ts`:
- `DragState` interface (from timelineConstants.ts)
- `handleClipMouseDown` function
- `handleMouseMove` function
- `handleMouseUp` function
- `dragSelectedIdsRef`
- `dragCursor`, `dragPreviewStyle`, `dragPreviewClip` computed values
- `paintTiles` computed value

The hook should accept parameters:
```typescript
interface UseTimelineDragParams {
  clips: ClipSnapshot[];
  pps: number;
  TRACK_HEIGHT: number;
  updateDrag: (state: Partial<DragState>) => void;
  rpc: RpcClient;
}

interface UseTimelineDragReturn {
  handleClipMouseDown: (e: React.MouseEvent, clipId: number, trackIndex: number, startBeat: number, forcePaintRepeat?: boolean) => void;
  handleMouseMove: (e: globalThis.MouseEvent) => void;
  handleMouseUp: () => void;
  dragSelectedIdsRef: React.MutableRefObject<Set<number>>;
  dragCursor: string;
  dragPreviewStyle: React.CSSProperties | null;
  dragPreviewClip: ClipSnapshot | null;
  paintTiles: Array<{ left: number; width: number; top: number }>;
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/hooks/useTimelineDrag.ts
git commit -m "refactor: extract useTimelineDrag hook"
```

---

### Task 15: Extract useTimelineTrim Hook

**Files:**
- Create: `frontend/src/hooks/useTimelineTrim.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create the trim hook**

Move the following from `TimelineMinimal.tsx` to `useTimelineTrim.ts`:
- `TrimState` interface (from timelineConstants.ts)
- `handleTrimStart` function
- `trimState`, `trimRef`
- `updateTrim` function
- `isTrimming` computed value

The hook should accept parameters:
```typescript
interface UseTimelineTrimParams {
  clips: ClipSnapshot[];
  pps: number;
  rpc: RpcClient;
}

interface UseTimelineTrimReturn {
  handleTrimStart: (e: React.MouseEvent, clip: ClipSnapshot, side: "left" | "right") => void;
  isTrimming: boolean;
  trimState: TrimState | null;
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/hooks/useTimelineTrim.ts
git commit -m "refactor: extract useTimelineTrim hook"
```

---

### Task 16: Extract useTimelineFade Hook

**Files:**
- Create: `frontend/src/hooks/useTimelineFade.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create the fade hook**

Move the following from `TimelineMinimal.tsx` to `useTimelineFade.ts`:
- `FadeDrag` interface (from timelineConstants.ts)
- `handleFadeStart` function
- `fadeDrag`, `fadeDragRef`

The hook should accept parameters:
```typescript
interface UseTimelineFadeParams {
  clips: ClipSnapshot[];
  pps: number;
  rpc: RpcClient;
}

interface UseTimelineFadeReturn {
  handleFadeStart: (e: React.MouseEvent, clip: ClipSnapshot, side: "in" | "out") => void;
  fadeDrag: FadeDrag | null;
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/hooks/useTimelineFade.ts
git commit -m "refactor: extract useTimelineFade hook"
```

---

### Task 17: Extract useTimelineLoopDrag Hook

**Files:**
- Create: `frontend/src/hooks/useTimelineLoopDrag.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create the loop drag hook**

Move the following from `TimelineMinimal.tsx` to `useTimelineLoopDrag.ts`:
- `startLoopDrag` function
- `loopDrag`, `dragBeat`, `dragBeatRef`

The hook should accept parameters:
```typescript
interface UseTimelineLoopDragParams {
  pps: number;
  transport: TransportSnapshot;
  rpc: RpcClient;
}

interface UseTimelineLoopDragReturn {
  startLoopDrag: (which: "start" | "end") => (e: React.MouseEvent) => void;
  dispLoopStart: number;
  dispLoopEnd: number;
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/hooks/useTimelineLoopDrag.ts
git commit -m "refactor: extract useTimelineLoopDrag hook"
```

---

### Task 18: Extract useTimelineRubberBand Hook

**Files:**
- Create: `frontend/src/hooks/useTimelineRubberBand.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create the rubber band hook**

Move the following from `TimelineMinimal.tsx` to `useTimelineRubberBand.ts`:
- `handleRubberBandStart` function
- `rubberBand`, `rubberBandRef`, `rubberBandJustCompleted`
- Import `computeRubberBandSelection` from timelineConstants.ts

The hook should accept parameters:
```typescript
interface UseTimelineRubberBandParams {
  clips: ClipSnapshot[];
  pps: number;
  TRACK_HEIGHT: number;
  selectedClipIds: Set<number>;
}

interface UseTimelineRubberBandReturn {
  handleRubberBandStart: (e: React.MouseEvent) => void;
  rubberBand: { x1: number; y1: number; x2: number; y2: number } | null;
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/hooks/useTimelineRubberBand.ts
git commit -m "refactor: extract useTimelineRubberBand hook"
```

---

### Task 19: Extract useTimelineZoom Hook

**Files:**
- Create: `frontend/src/hooks/useTimelineZoom.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create the zoom hook**

Move the following from `TimelineMinimal.tsx` to `useTimelineZoom.ts`:
- `pps` state
- `zoomIn`, `zoomOut`, `zoomFit` functions
- `onWheel` handler

The hook should accept parameters:
```typescript
interface UseTimelineZoomParams {
  maxEnd: number;
}

interface UseTimelineZoomReturn {
  pps: number;
  zoomIn: () => void;
  zoomOut: () => void;
  zoomFit: () => void;
  onWheel: (e: React.WheelEvent) => void;
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/hooks/useTimelineZoom.ts
git commit -m "refactor: extract useTimelineZoom hook"
```

---

### Task 20: Extract TimelineContextMenu Component

**Files:**
- Create: `frontend/src/components/TimelineContextMenu.tsx`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create the context menu component**

Move the following from `TimelineMinimal.tsx` to `TimelineContextMenu.tsx`:
- Clip context menu JSX (lines ~1277-1367)
- Empty area context menu JSX (lines ~1432-1467)
- Associated handler callbacks (delete, duplicate, split, copy, cut, paste)

The component should accept props:
```typescript
interface TimelineContextMenuProps {
  contextMenu: { x: number; y: number; type: string; clip?: ClipSnapshot; markerIndex?: number } | null;
  emptyContextMenu: { x: number; y: number } | null;
  clips: ClipSnapshot[];
  markers: MarkerSnapshot[];
  selectedClipIds: Set<number>;
  transport: TransportSnapshot;
  onClose: () => void;
  onDeleteClip: () => void;
  onDuplicateClip: () => void;
  onSplitClip: () => void;
}
```

- [ ] **Step 2: Run TypeScript check**

Run: `cd frontend && npx tsc --noEmit`
Expected: No errors

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/TimelineContextMenu.tsx
git commit -m "refactor: extract TimelineContextMenu component"
```

---

## Phase 4: Split McpTools.cpp (Low Risk)

### Task 21: Split McpTools into Domain Files

**Files:**
- Create: `src/mcp/McpTools_Project.cpp`
- Create: `src/mcp/McpTools_Transport.cpp`
- Create: `src/mcp/McpTools_Audio.cpp`
- Modify: `src/mcp/McpTools.cpp`

- [ ] **Step 1: Create Project tools file**

Move the following tool registrations from `McpTools.cpp` to `McpTools_Project.cpp`:
- `get_project`
- `get_tracks`
- `get_clip`
- `get_notes`
- `add_track`
- `remove_track`
- `add_clip`
- `remove_clip`
- `set_clip`
- `duplicate_clip`
- `set_track`
- `add_note`
- `remove_note`
- `set_note`

The new file should include:
```cpp
#include "McpTools.h"
#include "../model/ProjectModel.h"
```

- [ ] **Step 2: Create Transport tools file**

Move the following tool registrations from `McpTools.cpp` to `McpTools_Transport.cpp`:
- `get_transport`
- `set_tempo`
- `play`
- `stop`
- `pause`
- `rewind`
- `seek_to`
- `toggle_loop`
- `set_metronome`

The new file should include:
```cpp
#include "McpTools.h"
#include "../engine/TransportManager.h"
```

- [ ] **Step 3: Create Audio tools file**

Move the following tool registrations from `McpTools.cpp` to `McpTools_Audio.cpp`:
- `get_fx_slots`
- `add_fx_slot`
- `remove_fx_slot`
- `set_fx_slot`
- `get_automation_lanes`
- `add_automation_lane`
- `add_automation_point`
- `remove_automation_point`

The new file should include:
```cpp
#include "McpTools.h"
#include "../engine/AudioEngine.h"
```

- [ ] **Step 4: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 5: Commit**

```bash
git add src/mcp/McpTools_Project.cpp src/mcp/McpTools_Transport.cpp src/mcp/McpTools_Audio.cpp
git commit -m "refactor: split McpTools into domain files"
```

---

## Phase 5: Split MainWindow.cpp (Low Risk)

### Task 22: Extract MainWindowMenus

**Files:**
- Create: `src/ui/MainWindowMenus.cpp`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Create menus file**

Move the following from `MainWindow.cpp` to `MainWindowMenus.cpp`:
- Menu bar construction code
- File menu setup
- Edit menu setup
- Transport menu setup
- View menu setup
- Help menu setup

The new file should include:
```cpp
#include "MainWindow.h"
#include "../common/DebugLog.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/ui/MainWindowMenus.cpp
git commit -m "refactor: extract MainWindow menu construction"
```

---

### Task 23: Extract MainWindowConnections

**Files:**
- Create: `src/ui/MainWindowConnections.cpp`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Create connections file**

Move the following from `MainWindow.cpp` to `MainWindowConnections.cpp`:
- Signal/slot connections
- ValueTree listener registrations
- Timer callbacks
- Event filter installations

The new file should include:
```cpp
#include "MainWindow.h"
#include "../engine/AudioEngine.h"
```

- [ ] **Step 2: Run build to verify**

Run: `cmake --build build --config Debug`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/ui/MainWindowConnections.cpp
git commit -m "refactor: extract MainWindow signal/slot connections"
```

---

## Verification

### Task 24: Full Build and Test

- [ ] **Step 1: Clean build**

Run: `cmake --build build --config Debug --clean-first`
Expected: Build succeeds with no errors

- [ ] **Step 2: Run tests**

Run: `build/Debug/hdaw_tests.exe`
Expected: All tests pass

- [ ] **Step 3: Verify no duplicate symbols**

Run: `nm -C build/Debug/HDAW.exe | grep "AudioEngineCommands" | wc -l`
Expected: Each method appears exactly once

- [ ] **Step 4: Commit final state**

```bash
git add -A
git commit -m "refactor: complete codebase restructuring - split monolithic files into domain modules"
```

---

## Summary

| Phase | Files Created | Files Modified | Lines Moved |
|-------|---------------|----------------|-------------|
| Phase 1 | 2 | 2 | ~100 |
| Phase 2 | 10 | 1 | ~1,500 |
| Phase 3 | 7 | 1 | ~800 |
| Phase 4 | 3 | 1 | ~600 |
| Phase 5 | 2 | 1 | ~400 |
| **Total** | **24** | **6** | **~3,400** |

**Expected outcomes:**
- `AudioEngineCommands.cpp`: 2,076 → ~200 lines (helpers only)
- `TimelineMinimal.tsx`: 1,377 → ~300 lines (state + JSX)
- `McpTools.cpp`: 1,209 → ~200 lines (registry only)
- `MainWindow.cpp`: 1,128 → ~500 lines (core setup only)
