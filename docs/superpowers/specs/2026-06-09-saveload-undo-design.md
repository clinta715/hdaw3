# Save/Load & Undo — Phase Design

## Overview

Add project file save/load and undo/redo to HDAW. The `juce::ValueTree` already supports both natively — this phase wires them in.

## Execution Order

1. UndoManager integration (ProjectModel + wire all call sites)
2. ProjectSerializer (save/load/createNew)
3. Menu bar + file dialogs (MainWindow)
4. Graph rebuild on load

---

## 1. UndoManager

### ProjectModel changes
- Owns `juce::UndoManager undoManager`
- Exposed via `UndoManager& getUndoManager()`
- `createDefaultProject()` clears history: `undoManager.clearUndoHistory()`

### Wire all ValueTree mutations
Every `setProperty()`, `addChild()`, `removeChild()` that passes `nullptr` needs to pass `&undoManager` instead.

Key call sites:
- `AudioEngine::valueTreePropertyChanged()` — transport, volume/pan, clip params
- `TrackHeaderWidget` — mute/solo toggle, volume/pan commit
- `MixerWidget` — volume/mute/solo via signals
- `MixerStripWidget` — pan via signal
- `FXSlotRow` — fxType, bypassed, plugin properties
- `FXChainWidget` — add/remove/move slots
- `AutomationLaneWidget` — automation points
- `NoteGridWidget` — MIDI note add/remove/edit
- `TimelineInteraction` — clip move/trim/fade
- `ProjectPoolBrowser` — clip import
- `PianoRollModel` — note add/remove

For grouped edits (e.g., slider drag), call `undoManager.beginNewTransaction()` before the change.

### Dirty state
`undoManager.hasChangedSinceSaved()` returns true when there are unsaved changes. This drives the "unsaved changes" dialog and window title indicator.

---

## 2. ProjectSerializer

### New class
`src/engine/ProjectSerializer.h` / `.cpp` — static methods or owned by `AudioEngine`.

### Methods
- `static bool save(ProjectModel& model, const juce::File& file)` — serialize ValueTree to XML, write to file
- `static bool load(ProjectModel& model, const juce::File& file)` — read file, parse XML, replace ValueTree
- `static void createNew(ProjectModel& model)` — reset ValueTree to default project

### Save internals
```cpp
auto xml = model.getTree().toXmlString();
file.replaceWithText(xml);
model.getUndoManager().save(); // reset dirty flag
```

### Load internals
```cpp
auto xml = file.loadFileAsString();
auto newTree = juce::ValueTree::fromXml(xml);
if (newTree.isValid()) {
    model.getTree().copyPropertiesFrom(newTree, &model.getUndoManager());
    // clear and copy children
    model.getTree().removeAllChildren(&model.getUndoManager());
    for (auto& child : newTree)
        model.getTree().addChild(child.createCopy(), -1, &model.getUndoManager());
    model.getUndoManager().clearUndoHistory();
}
```

### File extension
`.hdaw`

---

## 3. UI — Menu Bar & Dialogs

### MainWindow changes
- `QMenuBar*` added via `menuBar()`
- **File menu:** New (Ctrl+N), Open (Ctrl+O), Save (Ctrl+S), Save As (Ctrl+Shift+S), separator, Exit
- **Edit menu:** Undo (Ctrl+Z), Redo (Ctrl+Shift+Z)
- Menu actions connected to slots on MainWindow

### File dialogs
- New: `ProjectSerializer::createNew(engine.getProjectModel())` + rebuild all
- Open: `QFileDialog::getOpenFileName()` with `*.hdaw` filter → call `load()`
- Save: if `currentFilePath` is set, save directly; else Save As
- Save As: `QFileDialog::getSaveFileName()` with `*.hdaw` filter

### Unsaved changes
Before New/Open/Exit, check `undoManager.hasChangedSinceSaved()`. If true, show `QMessageBox::question("Save changes?")` with Save/Discard/Cancel.

### Window title
`"HDAW - Untitled"` or `"HDAW - filename.hdaw"`

### Graph rebuild on load
After `ProjectSerializer::load()`, call:
- `engine.getMainProcessor()->rebuildRoutingGraph()`
- `timelineView->rebuild()` (or equivalent scene rebuild)
- `mixerWidget->rebuild()`
- Other widgets as needed

This triggers the engine to reconstruct the audio graph from the new ValueTree, and UI to resync.

---

## 4. Files Changed

### New
- `src/engine/ProjectSerializer.h`
- `src/engine/ProjectSerializer.cpp`

### Changed
- `src/model/ProjectModel.h` — add UndoManager member, getter
- `src/model/ProjectModel.cpp` — clear history in createDefaultProject
- `src/engine/AudioEngine.h` — expose ProjectSerializer (optional)
- `src/ui/MainWindow.h` — add menu bar members, currentFilePath, slots
- `src/ui/MainWindow.cpp` — implement all menu actions, rebuild triggers
- ~15 files across UI and engine — change `nullptr` → `&undoManager` in ValueTree calls

### CMakeLists.txt
- Add `ProjectSerializer.cpp`

---

## 5. Testing

1. **Undo/Redo:** Make a change (move clip, edit MIDI note, change volume), press Ctrl+Z, verify undo, Ctrl+Shift+Z, verify redo
2. **Save/Load:** Save project, close, reopen, verify all state is restored
3. **Unsaved changes:** Make a change, try to close, verify dialog appears
4. **New project:** Create new, verify blank project with default state
5. **Graph rebuild:** Load project, verify audio plays correctly
