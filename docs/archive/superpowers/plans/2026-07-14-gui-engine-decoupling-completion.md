# GUI-Engine Decoupling Completion Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) for syntax tracking.

**Goal:** Complete the GUI-engine decoupling by (1) migrating all direct `UndoManager` access in UI event handlers to the `ProjectCommands` interface, and (2) removing the last `#include "../engine/AudioEngine.h"` from a UI header (`MainWindow.h`).

**Architecture:** The `src/common/` interfaces (`ProjectCommands`, `TransportCommands`, `AudioGraphCommands`, `ReadModel`) already exist and are used by most widgets. The remaining debt is ~14 UI files that bypass the command interface to call `engine.getProjectModel().getUndoManager()` directly. We migrate them to use `projectCmds->beginTransaction()` + the existing command methods. Then we forward-declare `AudioEngine` in `MainWindow.h` and move the include to the `.cpp`.

**Tech Stack:** C++20, Qt 6, JUCE 8, CMake

---

### Task 1: Add `endTransaction()` to `ProjectCommands` and implement it

**Files:**
- Modify: `src/common/ProjectCommands.h:124`
- Modify: `src/engine/AudioEngineCommands.h:114`
- Modify: `src/engine/AudioEngineCommands.cpp:847-850`

- [ ] **Step 1: Add `endTransaction()` to the interface**

In `src/common/ProjectCommands.h`, add `endTransaction()` right after `beginTransaction`:

```cpp
    // Transaction lifecycle (wraps UndoManager::beginNewTransaction / endTransaction)
    virtual void beginTransaction(const std::string& name) = 0;
    virtual void endTransaction() = 0;
```

- [ ] **Step 2: Add `endTransaction()` to the concrete implementation header**

In `src/engine/AudioEngineCommands.h`, add after line 114:

```cpp
    void endTransaction() override;
```

- [ ] **Step 3: Implement `endTransaction()` in the .cpp**

In `src/engine/AudioEngineCommands.cpp`, after the `beginTransaction` implementation (line 850), add:

```cpp
void AudioEngineCommands::endTransaction()
{
    // UndoManager's beginNewTransaction implicitly ends the previous one.
    // Calling beginNewTransaction with an empty name is the standard way
    // to close the current transaction without starting a new named one.
    engine_.getProjectModel().getUndoManager().beginNewTransaction({});
}
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile, no errors.

---

### Task 2: Migrate `TrackHeaderWidget.cpp` — 15 call sites

**Files:**
- Modify: `src/ui/TrackHeaderWidget.cpp` (lines 167, 303, 317, 600, 628, 636, 646, 661, 667, 674, 725, 878, 896, 970, 982)

**Pattern:** Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->beginTransaction(...)` + `projectCmds->setXxx(...)`.

- [ ] **Step 1: Migrate `setTrackHeight` (line 167)**

Replace:
```cpp
void TrackHeaderWidget::setTrackHeight(int index, double height)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (index >= 0 && index < trackList.getNumChildren())
    {
        trackList.getChild(index).setProperty(IDs::trackHeight,
            (std::max)(40.0, height), &engine.getProjectModel().getUndoManager());
        layoutRects();
        update();
    }
}
```
With:
```cpp
void TrackHeaderWidget::setTrackHeight(int index, double height)
{
    if (index >= 0)
    {
        projectCmds->setTrackHeight(index, static_cast<int>((std::max)(40.0, height)));
        layoutRects();
        update();
    }
}
```

- [ ] **Step 2: Migrate `commitVolume` (line 296-308)**

Replace:
```cpp
void TrackHeaderWidget::commitVolume(int trackIndex, float vol)
{
    if (trackIndex < 0) return;
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < trackList.getNumChildren())
    {
        auto tree = trackList.getChild(trackIndex);
        tree.setProperty(IDs::volume, vol, &engine.getProjectModel().getUndoManager());
        ParamUpdate update{ trackIndex, 1, vol };
        engine.getBridge().pushUpdate(update);
    }
}
```
With:
```cpp
void TrackHeaderWidget::commitVolume(int trackIndex, float vol)
{
    if (trackIndex < 0) return;
    projectCmds->setTrackVolume(trackIndex, vol);
    ParamUpdate update{ trackIndex, 1, vol };
    engine.getBridge().pushUpdate(update);
}
```

- [ ] **Step 3: Migrate `commitPan` (line 310-319)**

Replace:
```cpp
void TrackHeaderWidget::commitPan(int trackIndex, float pan)
{
    if (trackIndex < 0) return;
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < trackList.getNumChildren())
    {
        auto tree = trackList.getChild(trackIndex);
        tree.setProperty(IDs::pan, pan, &engine.getProjectModel().getUndoManager());
    }
}
```
With:
```cpp
void TrackHeaderWidget::commitPan(int trackIndex, float pan)
{
    if (trackIndex < 0) return;
    projectCmds->setTrackPan(trackIndex, pan);
}
```

- [ ] **Step 4: Migrate mute/solo/arm/inputMonitor toggles (lines ~600-674)**

These are in `TrackHeaderWidget::mousePressEvent` or similar. Each follows the pattern:
```cpp
tree.setProperty(IDs::muted, !muted, &engine.getProjectModel().getUndoManager());
```
Replace each with the corresponding `projectCmds->setTrackMuted(...)`, `projectCmds->setTrackSoloed(...)`, etc.

- [ ] **Step 5: Migrate context-menu edits (lines ~725, 878, 896, 970, 982)**

These are rename, color change, and other context-menu actions. Replace each `setProperty(..., &um)` with the corresponding `projectCmds->setTrackName(...)`, `projectCmds->setTrackColor(...)`, etc.

- [ ] **Step 6: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 3: Migrate `TimelineView.cpp` — ~20 call sites

**Files:**
- Modify: `src/ui/TimelineView.cpp` (lines 115, 197, 406, 439, 500, 657, 682, 695, 730, 740, 793, 797-799, 810, 830, 835, 956, 962, 969, 975-978)

**Pattern:** Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->beginTransaction(...)` + `projectCmds->setXxx(...)`.

- [ ] **Step 1: Migrate `setUndoManager` on interaction (line 115)**

Replace:
```cpp
interaction->setUndoManager(&engine.getProjectModel().getUndoManager());
```
With:
```cpp
interaction->setUndoManager(&engine.getProjectModel().getUndoManager());
```
(Keep this one — `TimelineInteraction` needs the raw pointer for its own internal operations. This is a design choice: the interaction handles fine-grained drag operations and needs direct UndoManager access for coalescing. Document this as an accepted coupling.)

- [ ] **Step 2: Migrate `cutSelectedClips` (line 402-410)**

Replace:
```cpp
void TimelineView::cutSelectedClips()
{
    if (interaction == nullptr) return;
    auto& um = engine.getProjectModel().getUndoManager();
    um.beginNewTransaction("Cut clips");
    copySelectedClips();
    interaction->deleteSelectedClips();
}
```
With:
```cpp
void TimelineView::cutSelectedClips()
{
    if (interaction == nullptr) return;
    projectCmds->beginTransaction("Cut clips");
    copySelectedClips();
    interaction->deleteSelectedClips();
}
```

- [ ] **Step 3: Migrate remaining ~18 call sites in `TimelineView.cpp`**

Each follows one of these patterns:
- `engine.getProjectModel().getUndoManager().beginNewTransaction("...")` → `projectCmds->beginTransaction("...")`
- `engine.getProjectModel().getUndoManager().undo()` → `projectCmds->undo()`
- `engine.getProjectModel().getUndoManager().redo()` → `projectCmds->redo()`
- `engine.getProjectModel().getUndoManager()` passed as `&um` to `setProperty` → use the corresponding `projectCmds->setXxx()` method instead

Key sites to migrate (lines 406, 439, 500, 657, 682, 695, 730, 740, 793, 797-799, 810, 830, 835, 956, 962, 969, 975-978).

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 4: Migrate `ModulationWidget.cpp` — ~11 call sites

**Files:**
- Modify: `src/ui/ModulationWidget.cpp` (lines 254, 270, 288, 309-316)

**Pattern:** Replace `model.getUndoManager()` with `projectCmds->beginTransaction(...)` + `projectCmds->setXxx(...)`.

- [ ] **Step 1: Migrate `onAddLFO` (lines 241-269)**

Replace direct `model.getUndoManager()` calls with `projectCmds->beginTransaction(...)` + `projectCmds->setXxx(...)`. Since `ModulationWidget` doesn't have `projectCmds` yet, add the member and initialize it from the engine in the constructor.

- [ ] **Step 2: Migrate remaining ~8 call sites (lines 270, 288, 309-316)**

Each follows the same pattern: `model.getUndoManager()` → `projectCmds->beginTransaction(...)` + `projectCmds->setXxx(...)`.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 5: Migrate `NoteGridWidget.cpp` — ~6 call sites

**Files:**
- Modify: `src/ui/NoteGridWidget.cpp` (lines 267, 335-336, 352, 381-382)

**Pattern:** Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->beginTransaction(...)` + `projectCmds->setNoteXxx(...)`.

- [ ] **Step 1: Migrate note drag/move/resize handlers**

Each handler follows the pattern:
```cpp
auto& um = engine.getProjectModel().getUndoManager();
um.beginNewTransaction("Edit note");
// ... setProperty(IDs::noteStart, ..., &um) ...
```
Replace with:
```cpp
projectCmds->beginTransaction("Edit note");
projectCmds->setNoteStart(noteId, newStart);
projectCmds->setNotePitch(noteId, newPitch);
// etc.
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 5: Migrate `TimelineInteraction.cpp` — ~4 call sites

**Files:**
- Modify: `src/ui/TimelineInteraction.cpp` (lines 114, 419, 428, 514)

**Pattern:** Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->beginTransaction(...)`.

- [ ] **Step 1: Migrate clip creation/delete handlers**

Replace `engine.getProjectModel().getUndoManager().beginNewTransaction("Edit clip")` with `projectCmds->beginTransaction("Edit clip")`.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 6: Migrate `AutomationLaneWidget.cpp` — ~2 call sites

**Files:**
- Modify: `src/ui/AutomationLaneWidget.cpp` (lines 487, 490)

- [ ] **Step 1: Migrate add-automation-lane handler**

Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->addAutomationLane(...)`.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 7: Migrate `MixerStripWidget.cpp` — ~1 call site

**Files:**
- Modify: `src/ui/MixerStripWidget.cpp` (line 390)

- [ ] **Step 1: Migrate context-menu action**

Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->setXxx(...)`.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 8: Migrate `StepEditorWidget.cpp` — ~2 call sites

**Files:**
- Modify: `src/ui/StepEditorWidget.cpp` (lines 142, 161)

- [ ] **Step 1: Migrate note edit handlers**

Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->beginTransaction(...)` + `projectCmds->setNoteXxx(...)`.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 9: Migrate `TimeRuler.cpp` — ~3 call sites

**Files:**
- Modify: `src/ui/TimeRuler.cpp` (lines 309, 312-313, 315)

- [ ] **Step 1: Migrate marker context-menu handler**

Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->addMarker(...)` / `projectCmds->setLoopStart(...)` / `projectCmds->setLoopEnd(...)`.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 10: Migrate `VelocityLaneWidget.cpp` — ~3 call sites

**Files:**
- Modify: `src/ui/VelocityLaneWidget.cpp` (lines 43, 142, 162)

- [ ] **Step 1: Migrate velocity edit handlers**

Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->beginTransaction(...)` + `projectCmds->setNoteVelocity(...)`.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 11: Migrate `PhraseGeneratorDialog.cpp` — ~2 call sites

**Files:**
- Modify: `src/ui/PhraseGeneratorDialog.cpp` (lines 483, 486)

- [ ] **Step 1: Migrate clip-creation handler**

Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->beginTransaction(...)` + `projectCmds->addMidiClip(...)` / `projectCmds->addNote(...)`.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 12: Migrate `PianoRollWidget.cpp` — ~1 call site

**Files:**
- Modify: `src/ui/PianoRollWidget.cpp` (line 244)

- [ ] **Step 1: Migrate UndoManager pass-through to PianoRollModel**

Replace `engine.getProjectModel().getUndoManager()` with `projectCmds->beginTransaction(...)` + `projectCmds->setNoteXxx(...)`.

Note: `PianoRollModel` has its own `juce::UndoManager* undoManager` member and calls `beginNewTransaction()` directly in 7 places (lines 47, 62, 118, 147, 158, 175, 221) for operations like transpose, quantize, humanize. These are deeper — `PianoRollModel` is a model class, not a widget. The plan should either:
- (a) Give `PianoRollModel` a `ProjectCommands*` pointer and have it call `projectCmds->beginTransaction(...)` + `projectCmds->setNoteXxx(...)`, or
- (b) Accept this as a deeper coupling that requires a larger refactor (PianoRollModel is a shared model, not a widget).

Recommendation: Option (a) — give `PianoRollModel` a `ProjectCommands*` pointer. This is consistent with the pattern used by all widgets.

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile.

---

### Task 13: Forward-declare `AudioEngine` in `MainWindow.h`

**Files:**
- Modify: `src/ui/MainWindow.h` (lines 13-15)
- Modify: `src/ui/MainWindow.cpp` (add include)

- [ ] **Step 1: Replace include with forward declaration**

In `src/ui/MainWindow.h`, replace lines 13-15:
```cpp
// Follow-up: forward-declare AudioEngine once UI headers no longer
// transitively include AudioEngine.h (all UI headers pull it in today).
#include "../engine/AudioEngine.h"
```
With:
```cpp
class AudioEngine;
```

- [ ] **Step 2: Add the include to `MainWindow.cpp`**

In `src/ui/MainWindow.cpp`, add `#include "../engine/AudioEngine.h"` at the top (it likely already has it — verify and add if missing).

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug`
Expected: Clean compile. If any widget header transitively depends on `AudioEngine.h` through `MainWindow.h`, the build will fail — fix those by adding the include to the `.cpp` file.

---

### Task 13: Final verification

- [ ] **Step 1: Run full build**

Run: `cmake --build build --config Debug`
Expected: Clean compile with zero errors.

- [ ] **Step 2: Run tests**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: All tests pass.

- [ ] **Step 3: Verify no remaining direct UndoManager access in UI files**

Run: `rg "getUndoManager\(\)" src/ui/`
Expected: Zero results (all direct UndoManager access has been migrated to `ProjectCommands`).

- [ ] **Step 4: Verify no engine includes in UI headers**

Run: `rg "#include.*\.\./engine/" src/ui/ --include "*.h"`
Expected: Zero results (all engine includes are in `.cpp` files only).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "decouple: complete GUI-engine decoupling — migrate undo access to ProjectCommands, forward-declare AudioEngine in MainWindow.h"
```
