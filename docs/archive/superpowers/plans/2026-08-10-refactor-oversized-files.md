# Refactor Oversized Files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Break the three most dangerously large files into focused, maintainable modules while preserving all existing behavior and tests.

**Architecture:** Each refactor follows the same pattern — extract cohesive groups of code into new files, then re-import them in the original. No behavior changes; pure structural decomposition. Each task is independently completable and testable.

**Tech Stack:** C++ (FrontendRouter), React/TypeScript (NoteGrid, TimelineMinimal)

---

## File Structure (before → after)

### FrontendRouter.cpp (1,567 lines → ~100 lines dispatch + 12 domain files)

```
src/frontend/
  FrontendRouter.h          (unchanged — single free function decl)
  FrontendRouter.cpp         (shrinks to: helpers + top-level dispatch + sub-dispatch calls)
  router/
    RouterHelpers.h          (extract: requireInt, requireDouble, optString, etc.)
    Router_Project.cpp       (extract: dispatchProject — tracks, clips, notes, FX, automation, etc.)
    Router_Transport.cpp     (extract: dispatchTransport)
    Router_AudioGraph.cpp    (extract: dispatchAudioGraph)
    Router_Read.cpp          (extract: dispatchRead — snapshot, getTrack, getClip, etc.)
    Router_Plugin.cpp        (extract: dispatchPlugin + dispatchPluginParam)
    Router_Audio.cpp         (extract: dispatchAudio — device enumeration, setup)
    Router_Midi.cpp          (extract: dispatchMidi)
    Router_Export.cpp        (extract: dispatchExport)
    Router_Preview.cpp       (extract: dispatchPreview)
    Router_Composition.cpp   (extract: dispatchComposition)
    Router_Session.cpp       (extract: dispatchSession)
```

### NoteGrid.tsx (916 lines → ~200 lines component + 5 extracted modules)

```
frontend/src/components/
  NoteGrid.tsx               (shrinks to: state setup, render, hook wiring)
  NoteGrid/
    noteGridTypes.ts         (extract: NoteDragState, NoteResizeState, ContextMenuState, MarqueeState, Props)
    noteGridConstants.ts     (extract: KEY_HEIGHT, TOTAL_KEY_AREA, DRAG_THRESHOLD, clamp, noteClipboard)
    useNoteGridDrag.ts       (extract: handleMouseMove, handleMouseUp, window listener effect)
    useNoteGridInteractions.ts (extract: handleDoubleClick, deleteSelected, transposeSelected, quantizeSelected, humanizeSelected, copy/cut/paste, handleKeyDown)
    useNoteGridMarquee.ts    (extract: handleMarqueeStart, intersectNotes, marquee state)
```

### TimelineMinimal.tsx (911 lines → ~350 lines component + 3 extracted modules)

```
frontend/src/components/
  TimelineMinimal.tsx        (shrinks to: state, render, hook wiring)
  TimelineMinimal/
    useTimelineDrop.ts       (extract: handleDrop — 100 lines of drag-and-drop import logic)
    useTimelineKeyboard.ts   (extract: handler function — keyboard shortcuts, ~110 lines)
    useTimelineRuler.ts      (extract: ruler click-to-seek, drag-scrub, beatToSec)
```

---

## Pitfall Gates Triggered

- **Gate 5 (Stale closures):** All three files use window-level event listeners with ref mirrors. Extraction must preserve the ref indirection pattern — new hooks must accept refs, not stale closures.
- **Gate 8 (CSS design tokens):** No CSS changes planned — N/A.
- **Gate 2 (Unimplemented paths):** Pure structural refactor — no new code paths, just reorganization. Existing tests verify all paths work.
- **Gate 3 (Audio-thread safety):** FrontendRouter changes are message-thread only. No audio-thread impact.

## Dependency Map

- **Blast radius:** FrontendRouter is called by `FrontendServer::handleMessage` (single caller). NoteGrid is called by `PianoRoll` (single caller). TimelineMinimal is called by `App.tsx` (single caller). All three have narrow inbound fan-in.
- **Downstream:** FrontendRouter dispatches to `AudioEngine` command objects — no change to those interfaces. NoteGrid/TimelineMinimal call RPC methods — no change to the RPC layer.
- **Projections affected:** None — structural refactor only, no data flow changes.
- **SPSC paths touched:** None.

---

## Task 1: Extract RouterHelpers from FrontendRouter.cpp

**Files:**
- Create: `src/frontend/router/RouterHelpers.h`
- Modify: `src/frontend/FrontendRouter.cpp` (remove helpers, add include)

- [ ] **Step 1: Create `src/frontend/router/` directory**

```bash
mkdir -p src/frontend/router
```

- [ ] **Step 2: Create `src/frontend/router/RouterHelpers.h`**

Move lines 42-164 from `FrontendRouter.cpp` (the anonymous namespace helpers: `requireInt`, `requireDouble`, `requireFloat`, `requireBool`, `requireString`, `optInt`, `optDouble`, `optFloat`, `optBool`, `optString`, `paramsObject`, `parseShape`, `toDoubleVector`) into a new header. Wrap in `namespace frontend::router_helpers`. Keep the `#include` set that these helpers need (`QJsonValue`, `QJsonObject`, `QJsonArray`, `EnvelopeGenerator.h`).

```cpp
#pragma once
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <optional>
#include "src/engine/EnvelopeGenerator.h"

namespace frontend::router_helpers {

// ... all helper functions verbatim from FrontendRouter.cpp lines 44-164 ...

} // namespace frontend::router_helpers
```

- [ ] **Step 3: Update FrontendRouter.cpp to use the new header**

Replace lines 42-164 with:
```cpp
#include "router/RouterHelpers.h"
using namespace frontend::router_helpers;
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build --config Debug
```

Expected: Build succeeds, no linker errors.

- [ ] **Step 5: Run tests**

```bash
build/Debug/hdaw_tests.exe
```

Expected: All tests pass (pure structural change).

---

## Task 2: Extract Router_Project.cpp (the biggest win)

**Files:**
- Create: `src/frontend/router/Router_Project.cpp`
- Create: `src/frontend/router/Router_Project.h`
- Modify: `src/frontend/FrontendRouter.cpp` (remove dispatchProject, call extracted version)

- [ ] **Step 1: Create `src/frontend/router/Router_Project.h`**

```cpp
#pragma once
#include "src/frontend/FrontendRpc.h"

class ProjectCommands;
class QJsonValue;
class QString;

namespace frontend {
DispatchResult dispatchProject(ProjectCommands& cmds, const QString& subMethod,
                               const QJsonValue& params);
}
```

- [ ] **Step 2: Create `src/frontend/router/Router_Project.cpp`**

Move lines 168-696 from `FrontendRouter.cpp` (the entire `dispatchProject` function) into this file. Include `Router_Project.h`, `RouterHelpers.h`, and all necessary command headers. The function signature changes from static to exported.

- [ ] **Step 3: Update FrontendRouter.cpp**

Remove lines 168-696. Add `#include "router/Router_Project.h"`. The top-level `dispatch()` function's `"project"` case now calls `dispatchProject(...)` which is declared in the header.

- [ ] **Step 4: Add `Router_Project.cpp` to CMakeLists.txt**

Find the source list in `CMakeLists.txt` that includes `FrontendRouter.cpp` and add `src/frontend/router/Router_Project.cpp`.

- [ ] **Step 5: Build and verify**

```bash
cmake --build build --config Debug
```

Expected: Build succeeds.

- [ ] **Step 6: Run tests**

```bash
build/Debug/hdaw_tests.exe --gtest_filter="*Rpc*:*Mcp*:*Frontend*"
```

Expected: All RPC/MCP/frontend tests pass.

---

## Task 3: Extract remaining Router domain files

Apply the same pattern as Task 2 for each sub-dispatcher. Each is a separate step that can be done independently.

**Files (all new):**
- `src/frontend/router/Router_Transport.cpp` + `.h` (lines 707-725, ~19 lines)
- `src/frontend/router/Router_AudioGraph.cpp` + `.h` (lines 727-736, ~10 lines)
- `src/frontend/router/Router_Read.cpp` + `.h` (lines 738-814, ~77 lines)
- `src/frontend/router/Router_Plugin.cpp` + `.h` (lines 816-933, ~118 lines)
- `src/frontend/router/Router_Audio.cpp` + `.h` (lines 946-1140, ~195 lines)
- `src/frontend/router/Router_Midi.cpp` + `.h` (lines 935-944, ~10 lines)
- `src/frontend/router/Router_Export.cpp` + `.h` (lines 1148-1268, ~121 lines)
- `src/frontend/router/Router_Preview.cpp` + `.h` (lines 1270-1315, ~46 lines)
- `src/frontend/router/Router_Composition.cpp` + `.h` (lines 1317-1517, ~201 lines)
- `src/frontend/router/Router_Session.cpp` + `.h` (lines 698-705, ~8 lines)

- [ ] **Step 1: Create each `.h` + `.h` pair following the Task 2 pattern**
- [ ] **Step 2: Update FrontendRouter.cpp to include and call the extracted dispatchers**
- [ ] **Step 3: Add all new `.cpp` files to CMakeLists.txt**
- [ ] **Step 4: Build: `cmake --build build --config Debug`**
- [ ] **Step 5: Run full test suite: `build/Debug/hdaw_tests.exe`**
- [ ] **Step 6: Verify FrontendRouter.cpp is now ~100 lines (helpers + top-level dispatch only)**

---

## Task 4: Extract NoteGrid types and constants

**Files:**
- Create: `frontend/src/components/NoteGrid/noteGridTypes.ts`
- Create: `frontend/src/components/NoteGrid/noteGridConstants.ts`
- Modify: `frontend/src/components/NoteGrid.tsx` (import from new files)

- [ ] **Step 1: Create `frontend/src/components/NoteGrid/` directory**

```bash
mkdir -p frontend/src/components/NoteGrid
```

- [ ] **Step 2: Create `noteGridTypes.ts`**

Move from `NoteGrid.tsx`:
- `Props` interface (lines 11-24)
- `NoteDragState` interface (lines 26-36)
- `NoteResizeState` interface (lines 38-43)
- `ContextMenuState` interface (lines 45-49)
- `MarqueeState` interface (lines 51-58)

```typescript
import { NoteSnapshot } from '../../rpc/types';
import { RpcClient } from '../../rpc/client';

export interface Props { /* ... */ }
export interface NoteDragState { /* ... */ }
export interface NoteResizeState { /* ... */ }
export interface ContextMenuState { /* ... */ }
export interface MarqueeState { /* ... */ }
```

- [ ] **Step 3: Create `noteGridConstants.ts`**

Move from `NoteGrid.tsx`:
- `KEY_HEIGHT`, `TOTAL_KEY_AREA`, `DRAG_THRESHOLD` (lines 60-62)
- `clamp` function (lines 64-66)
- `noteClipboard` array (line 68)

```typescript
export const KEY_HEIGHT = 8;
export const TOTAL_KEY_AREA = 128 * KEY_HEIGHT;
export const DRAG_THRESHOLD = 4;
export function clamp(v: number, lo: number, hi: number) { return Math.max(lo, Math.min(hi, v)); }
export const noteClipboard: { beat: number; pitch: number; duration: number; velocity: number }[] = [];
```

- [ ] **Step 4: Update NoteGrid.tsx imports**

Replace the inline type/constant definitions with:
```typescript
import { Props, NoteDragState, NoteResizeState, ContextMenuState, MarqueeState } from './NoteGrid/noteGridTypes';
import { KEY_HEIGHT, TOTAL_KEY_AREA, DRAG_THRESHOLD, clamp, noteClipboard } from './NoteGrid/noteGridConstants';
```

- [ ] **Step 5: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Task 5: Extract useNoteGridDrag hook

**Files:**
- Create: `frontend/src/components/NoteGrid/useNoteGridDrag.ts`
- Modify: `frontend/src/components/NoteGrid.tsx` (move handlers out, call hook)

- [ ] **Step 1: Create `useNoteGridDrag.ts`**

Extract from `NoteGrid.tsx`:
- `handleMouseMove` callback (lines 179-206)
- `handleMouseUp` callback (lines 208-273)
- The window listener `useEffect` (lines 279-295)

The hook accepts refs and state setters as parameters (to avoid stale closures):

```typescript
import { useCallback, useEffect, RefObject } from 'react';
import { NoteDragState, NoteResizeState } from './noteGridTypes';
import { clamp } from './noteGridConstants';
import { useProjectStore } from '../../store/projectStore';
import { RpcClient } from '../../rpc/client';
import { NoteSnapshot } from '../../rpc/types';

interface UseNoteGridDragOpts {
  dragRef: RefObject<NoteDragState | null>;
  resizeRef: RefObject<NoteResizeState | null>;
  ppbRef: RefObject<number>;
  notesRef: RefObject<NoteSnapshot[]>;
  selectedRef: RefObject<Set<number>>;
  setDragState: (s: NoteDragState | null) => void;
  setResizeState: (s: NoteResizeState | null) => void;
  rpc: RpcClient;
  clipId: number | null;
}

export function useNoteGridDrag(opts: UseNoteGridDragOpts) {
  const { dragRef, resizeRef, ppbRef, notesRef, selectedRef,
          setDragState, setResizeState, rpc, clipId } = opts;

  const handleMouseMove = useCallback((e: MouseEvent) => { /* ... */ }, []);
  const handleMouseUp = useCallback((e: MouseEvent) => { /* ... */ }, [rpc, clipId]);

  useEffect(() => {
    if (!dragRef.current && !resizeRef.current) return;
    const onMove = (e: MouseEvent) => handleMouseMove(e);
    const onUp = (e: MouseEvent) => handleMouseUp(e);
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    return () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
    };
  }, [handleMouseMove, handleMouseUp]);

  return { handleMouseMove, handleMouseUp };
}
```

- [ ] **Step 2: Update NoteGrid.tsx**

Replace the extracted callbacks and useEffect with:
```typescript
const { handleMouseMove, handleMouseUp } = useNoteGridDrag({
  dragRef, resizeRef, ppbRef, notesRef, selectedRef,
  setDragState, setResizeState, rpc, clipId
});
```

- [ ] **Step 3: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Task 6: Extract useNoteGridInteractions hook

**Files:**
- Create: `frontend/src/components/NoteGrid/useNoteGridInteractions.ts`
- Modify: `frontend/src/components/NoteGrid.tsx`

- [ ] **Step 1: Create `useNoteGridInteractions.ts`**

Extract from `NoteGrid.tsx`:
- `handleDoubleClick` (lines 356-413)
- `deleteSelected` (lines 415-445)
- `transposeSelected` (lines 447-480)
- `quantizeSelected` (lines 482-516)
- `humanizeSelected` (lines 518-563)
- `copySelected` (lines 565-568)
- `cutSelected` (lines 570-573)
- `pasteAtScroll` (lines 575-609)
- `selectAll` (lines 611-613)
- `handleKeyDown` (lines 615-669)

The hook takes refs, state, rpc, and store access as parameters. Returns all the handlers for wiring into the component.

- [ ] **Step 2: Update NoteGrid.tsx**

Replace all extracted callbacks with the hook call:
```typescript
const {
  handleDoubleClick, deleteSelected, transposeSelected,
  quantizeSelected, humanizeSelected, copySelected, cutSelected,
  pasteAtScroll, selectAll, handleKeyDown
} = useNoteGridInteractions({ /* opts */ });
```

- [ ] **Step 3: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Task 7: Extract useNoteGridMarquee hook

**Files:**
- Create: `frontend/src/components/NoteGrid/useNoteGridMarquee.ts`
- Modify: `frontend/src/components/NoteGrid.tsx`

- [ ] **Step 1: Create `useNoteGridMarquee.ts`**

Extract from `NoteGrid.tsx`:
- `intersectNotes` (lines 127-140)
- `handleMarqueeStart` (lines 301-354)
- Marquee state (`marquee`, `marqueeJustCompleted`)

- [ ] **Step 2: Update NoteGrid.tsx**

Replace with hook call.

- [ ] **Step 3: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Task 8: Move NoteGrid.tsx into NoteGrid/ directory

**Files:**
- Move: `frontend/src/components/NoteGrid.tsx` → `frontend/src/components/NoteGrid/NoteGrid.tsx`
- Create: `frontend/src/components/NoteGrid/index.ts` (re-export)
- Modify: `frontend/src/components/PianoRoll.tsx` (update import path)

- [ ] **Step 1: Move the file**

```bash
mv frontend/src/components/NoteGrid.tsx frontend/src/components/NoteGrid/NoteGrid.tsx
```

- [ ] **Step 2: Create `frontend/src/components/NoteGrid/index.ts`**

```typescript
export { default } from './NoteGrid';
```

- [ ] **Step 3: Update PianoRoll.tsx import**

```typescript
// Old:
import NoteGrid from "./NoteGrid";
// New:
import NoteGrid from "./NoteGrid";
// (Same import path works because NoteGrid/index.ts re-exports)
```

Actually, since PianoRoll imports `"./NoteGrid"` and the directory has an `index.ts`, the import resolves automatically. No change needed in PianoRoll.

- [ ] **Step 4: Update internal imports in NoteGrid.tsx**

Fix relative paths inside the moved file (e.g., `../../rpc/types` → `../../rpc/types`, `../../store/projectStore` → `../../store/projectStore`). The `./NoteGrid/` subdirectory imports use `./` prefix.

- [ ] **Step 5: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Task 9: Extract useTimelineRuler hook

**Files:**
- Create: `frontend/src/components/TimelineMinimal/useTimelineRuler.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create `useTimelineRuler.ts`**

Extract from `TimelineMinimal.tsx`:
- `isScrubbing` state (line 183)
- `scrubRef` (line 184)
- `beatToSec` callback (line 186)
- `handleRulerMouseDown` callback (lines 188-233)

```typescript
import { useState, useRef, useCallback } from 'react';
import { useTransportStore } from '../../store/transportStore';
import { rpc } from '../../rpc';
import { snap } from '../snapUtils';

interface UseTimelineRulerOpts {
  pps: number;
}

export function useTimelineRuler(opts: UseTimelineRulerOpts) {
  const { pps } = opts;
  const [isScrubbing, setIsScrubbing] = useState(false);
  const scrubRef = useRef(false);

  const beatToSec = useCallback((beat: number) => { /* ... */ }, []);

  const handleRulerMouseDown = useCallback((e: React.MouseEvent, rulerRef: React.RefObject<HTMLDivElement | null>) => { /* ... */ }, [pps, beatToSec]);

  return { isScrubbing, handleRulerMouseDown };
}
```

- [ ] **Step 2: Update TimelineMinimal.tsx**

Replace extracted state/callbacks with hook call.

- [ ] **Step 3: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Task 10: Extract useTimelineDrop hook

**Files:**
- Create: `frontend/src/components/TimelineMinimal/useTimelineDrop.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create `useTimelineDrop.ts`**

Extract `handleDrop` (lines 236-339, ~104 lines). This is the file drag-and-drop import handler with two branches (internal file-browser drag and external OS file drop). It needs access to `rpc`, `useProjectStore`, `snap`, `nextTempId`, and layout calculations.

```typescript
import { useCallback } from 'react';
import { rpc } from '../../rpc';
import { useProjectStore, nextTempId } from '../../store/projectStore';
import { snap } from '../snapUtils';
import { buildRowLayout } from '../../utils/rowLayout';

interface UseTimelineDropOpts {
  pps: number;
  layout: ReturnType<typeof buildRowLayout>;
  maxEnd: number;
}

export function useTimelineDrop(opts: UseTimelineDropOpts) {
  const { pps, layout, maxEnd } = opts;

  const handleDrop = useCallback((e: React.DragEvent) => { /* ... */ }, [pps, layout, maxEnd]);

  return { handleDrop };
}
```

- [ ] **Step 2: Update TimelineMinimal.tsx**

Replace `handleDrop` with hook call.

- [ ] **Step 3: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Task 11: Extract useTimelineKeyboard hook

**Files:**
- Create: `frontend/src/components/TimelineMinimal/useTimelineKeyboard.ts`
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Create `useTimelineKeyboard.ts`**

Extract the `handler` function from the `useEffect` at lines 483-595 (~112 lines). This is the keyboard shortcuts handler (Delete, Ctrl+D, Ctrl+M, Ctrl+C/X/V, Escape, Shift+F, Space).

```typescript
import { useEffect, useCallback } from 'react';
import { useProjectStore } from '../../store/projectStore';
import { useTransportStore } from '../../store/transportStore';
import { useUiStore } from '../../store/uiStore';
import { rpc } from '../../rpc';

interface UseTimelineKeyboardOpts {
  handleDuplicateClip: () => void;
  pasteClipboard: () => void;
}

export function useTimelineKeyboard(opts: UseTimelineKeyboardOpts) {
  const { handleDuplicateClip, pasteClipboard } = opts;

  useEffect(() => {
    const handler = (e: KeyboardEvent) => { /* ... */ };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [handleDuplicateClip, pasteClipboard]);
}
```

- [ ] **Step 2: Update TimelineMinimal.tsx**

Replace the `useEffect` block (lines 483-595) with:
```typescript
useTimelineKeyboard({ handleDuplicateClip, pasteClipboard });
```

- [ ] **Step 3: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Task 12: Move TimelineMinimal.tsx into TimelineMinimal/ directory

**Files:**
- Move: `frontend/src/components/TimelineMinimal.tsx` → `frontend/src/components/TimelineMinimal/TimelineMinimal.tsx`
- Create: `frontend/src/components/TimelineMinimal/index.ts`
- Modify: `frontend/src/App.tsx` (update import path)

- [ ] **Step 1: Move the file**

```bash
mv frontend/src/components/TimelineMinimal.tsx frontend/src/components/TimelineMinimal/TimelineMinimal.tsx
```

- [ ] **Step 2: Create `frontend/src/components/TimelineMinimal/index.ts`**

```typescript
export { default } from './TimelineMinimal';
```

- [ ] **Step 3: Update App.tsx import**

Since `TimelineMinimal/index.ts` re-exports, the existing `import TimelineMinimal from "./components/TimelineMinimal"` resolves automatically. No change needed.

- [ ] **Step 4: Fix internal imports in moved file**

Update relative paths for hooks and utilities (add `../` prefix for sibling imports).

- [ ] **Step 5: Run frontend build + tests**

```bash
cd frontend && npm run build && npm test
```

Expected: Build succeeds, all tests pass.

---

## Final Verification

- [ ] **Step 1: Full C++ build**

```bash
cmake --build build --config Debug
```

- [ ] **Step 2: Full C++ test suite**

```bash
build/Debug/hdaw_tests.exe
```

- [ ] **Step 3: Frontend build + unit tests**

```bash
cd frontend && npm run build && npm test
```

- [ ] **Step 4: Frontend E2E tests**

```bash
cd frontend && npm run test:e2e
```

- [ ] **Step 5: Verify line counts**

```bash
wc -l src/frontend/FrontendRouter.cpp
wc -l frontend/src/components/NoteGrid/NoteGrid.tsx
wc -l frontend/src/components/TimelineMinimal/TimelineMinimal.tsx
```

Expected:
- `FrontendRouter.cpp` < 120 lines
- `NoteGrid.tsx` < 250 lines
- `TimelineMinimal.tsx` < 400 lines
