# Handoff Prompt: Continue Refactoring Oversized Files

## Context

I'm refactoring the three largest files in the HDAW codebase. **Task 1 is complete** (RouterHelpers extracted). Tasks 2-12 remain. The full plan is at `docs/superpowers/plans/2026-08-10-refactor-oversized-files.md`.

**Project:** HDAW — a JUCE 8 desktop DAW with React 19 frontend, C++ engine, JSON-RPC 2.0 over WebSocket.

**Working directory:** `D:\pdf\roo projects\hdaw3`

**Build command:** `cmake --build build --config Debug` (outputs to `build/Debug/`)
**Test command:** `build/Debug/hdaw_tests.exe` (filter with `--gtest_filter="*pattern*"`)
**Frontend build:** `cd frontend && npm run build`
**Frontend tests:** `cd frontend && npm test`

## What's Done (Task 1)

- Created `src/frontend/router/RouterHelpers.h` — 13 param-extraction helpers in `namespace frontend::router_helpers`
- Modified `src/frontend/FrontendRouter.cpp` — replaced anonymous namespace helpers with `#include "router/RouterHelpers.h"` + `using namespace frontend::router_helpers;` (placed ABOVE `namespace frontend {`)
- Build passes, 672/672 tests pass

## Current File State

`src/frontend/FrontendRouter.cpp` — 1,445 lines. Dispatch functions and their line ranges:

| Line | Function | Lines |
|------|----------|-------|
| 46 | `dispatchProject` | 46-575 (~530 lines) |
| 576 | `dispatchSession` | 576-584 (~9 lines) |
| 585 | `dispatchTransport` | 585-604 (~20 lines) |
| 605 | `dispatchAudioGraph` | 605-615 (~11 lines) |
| 616 | `dispatchRead` | 616-693 (~78 lines) |
| 694 | `dispatchPlugin` | 694-761 (~68 lines) |
| 762 | `dispatchPluginParam` | 762-812 (~51 lines) |
| 813 | `dispatchMidi` | 813-823 (~11 lines) |
| 824 | `dispatchAudio` | 824-1025 (~202 lines) |
| 1026 | `dispatchExport` | 1026-1147 (~122 lines) |
| 1148 | `dispatchPreview` | 1148-1194 (~47 lines) |
| 1195 | `dispatchComposition` | 1195-1396 (~202 lines) |
| 1397 | `} // namespace (anonymous)` | |
| 1401 | `dispatch` (top-level) | 1401-1445 (~45 lines) |

## Remaining Tasks

### Task 2: Extract Router_Project.cpp (HIGHEST PRIORITY — biggest win)

**Create:** `src/frontend/router/Router_Project.h` + `src/frontend/router/Router_Project.cpp`
**Move:** `dispatchProject` (lines 46-575) from `FrontendRouter.cpp` to `Router_Project.cpp`
**Update:** `FrontendRouter.cpp` to `#include "router/Router_Project.h"` and remove the function
**Update:** `CMakeLists.txt` — add `src/frontend/router/Router_Project.cpp` to the source list

The header should declare:
```cpp
namespace frontend {
class ProjectCommands;
DispatchResult dispatchProject(ProjectCommands& cmds, const QString& subMethod, const QJsonValue& params);
}
```

The `.cpp` needs includes: `Router_Project.h`, `RouterHelpers.h`, `src/common/ProjectCommands.h`, `src/engine/PhraseGenerator.h`, `src/engine/ArrangementGenerator.h`, `src/engine/EnvelopeGenerator.h`, Qt headers, `<algorithm>`. Use `using namespace frontend::router_helpers;`.

**Build + test after.**

### Task 3: Extract remaining Router domain files

Same pattern as Task 2 for each sub-dispatcher. Each gets a `.h` + `.cpp` pair in `src/frontend/router/`:

| File | Function | Lines | Key includes |
|------|----------|-------|-------------|
| `Router_Session.cpp` | `dispatchSession` | 576-584 | `ProjectCommands.h` |
| `Router_Transport.cpp` | `dispatchTransport` | 585-604 | `TransportCommands.h` |
| `Router_AudioGraph.cpp` | `dispatchAudioGraph` | 605-615 | `AudioGraphCommands.h` |
| `Router_Read.cpp` | `dispatchRead` | 616-693 | `ReadModel.h` |
| `Router_Plugin.cpp` | `dispatchPlugin` + `dispatchPluginParam` | 694-812 | `PluginService.h`, `PluginParamService.h` |
| `Router_Midi.cpp` | `dispatchMidi` | 813-823 | `MidiService.h` |
| `Router_Audio.cpp` | `dispatchAudio` | 824-1025 | `AudioEngine.h`, `MainAudioProcessor.h`, `PluginManager.h`, `SettingsKeys.h` |
| `Router_Export.cpp` | `dispatchExport` | 1026-1147 | `AudioEngine.h`, `ExportManager.h` |
| `Router_Preview.cpp` | `dispatchPreview` | 1148-1194 | `AudioEngine.h`, `ProjectPool.h` |
| `Router_Composition.cpp` | `dispatchComposition` | 1195-1396 | `AudioEngine.h`, `PhraseGenerator.h`, `ArrangementGenerator.h` |

**All new `.cpp` files must be added to `CMakeLists.txt`.**

After all extractions, `FrontendRouter.cpp` should be ~100 lines: includes + `dispatch()` top-level router only.

### Tasks 4-8: NoteGrid.tsx refactoring (916 lines)

`frontend/src/components/NoteGrid.tsx` — monolithic piano roll component.

**Task 4:** Create `frontend/src/components/NoteGrid/noteGridTypes.ts` and `noteGridConstants.ts`. Extract interfaces (`Props`, `NoteDragState`, `NoteResizeState`, `ContextMenuState`, `MarqueeState`) and constants (`KEY_HEIGHT`, `TOTAL_KEY_AREA`, `DRAG_THRESHOLD`, `clamp`, `noteClipboard`).

**Task 5:** Create `frontend/src/components/NoteGrid/useNoteGridDrag.ts`. Extract `handleMouseMove` (lines 179-206), `handleMouseUp` (lines 208-273), and the window listener `useEffect` (lines 279-295).

**Task 6:** Create `frontend/src/components/NoteGrid/useNoteGridInteractions.ts`. Extract `handleDoubleClick` (356-413), `deleteSelected` (415-445), `transposeSelected` (447-480), `quantizeSelected` (482-516), `humanizeSelected` (518-563), `copySelected` (565-568), `cutSelected` (570-573), `pasteAtScroll` (575-609), `selectAll` (611-613), `handleKeyDown` (615-669).

**Task 7:** Create `frontend/src/components/NoteGrid/useNoteGridMarquee.ts`. Extract `intersectNotes` (127-140), `handleMarqueeStart` (301-354).

**Task 8:** Move `NoteGrid.tsx` into `NoteGrid/NoteGrid.tsx`, create `NoteGrid/index.ts` re-export. `PianoRoll.tsx` imports `"./NoteGrid"` which resolves to the directory's `index.ts` automatically.

**Frontend build + test after each task:** `cd frontend && npm run build && npm test`

### Tasks 9-12: TimelineMinimal.tsx refactoring (911 lines)

`frontend/src/components/TimelineMinimal.tsx` — timeline component with 6 custom hooks already extracted.

**Task 9:** Create `frontend/src/components/TimelineMinimal/useTimelineRuler.ts`. Extract `isScrubbing` state (line 183), `scrubRef` (184), `beatToSec` (186), `handleRulerMouseDown` (188-233).

**Task 10:** Create `frontend/src/components/TimelineMinimal/useTimelineDrop.ts`. Extract `handleDrop` (236-339, ~104 lines of drag-and-drop import logic).

**Task 11:** Create `frontend/src/components/TimelineMinimal/useTimelineKeyboard.ts`. Extract the `handler` function from the `useEffect` at lines 483-595 (~112 lines of keyboard shortcuts).

**Task 12:** Move `TimelineMinimal.tsx` into `TimelineMinimal/TimelineMinimal.tsx`, create `TimelineMinimal/index.ts` re-export. `App.tsx` imports `"./components/TimelineMinimal"` which resolves automatically.

**Frontend build + test after each task.**

## Pitfall Gates to Watch

- **Gate 5 (Stale closures):** Window-level event handlers use ref mirrors. New hooks must accept refs, not stale closures.
- **Gate 3 (Audio-thread safety):** FrontendRouter changes are message-thread only — N/A.
- **Gate 8 (CSS):** No CSS changes — N/A.
- **UTF-8 encoding:** The edit tool corrupted UTF-8 chars in Task 1. After any edit to `FrontendRouter.cpp`, verify `≥`, `●`, `—` are intact (not `â‰¥`, `â—`, `â€"`).

## Execution Approach

Use **subagent-driven development**: dispatch a fresh subagent per task, review between tasks. The plan file at `docs/superpowers/plans/2026-08-10-refactor-oversized-files.md` has detailed step-by-step instructions for each task.

After all 12 tasks, verify:
- `FrontendRouter.cpp` < 120 lines
- `NoteGrid.tsx` < 250 lines  
- `TimelineMinimal.tsx` < 400 lines
- `cmake --build build --config Debug` succeeds
- `cd frontend && npm run build && npm test` succeeds
