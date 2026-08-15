# Plugin Editor Windows — Phase Design

## Overview
Add native VST3 editor windows for loaded plugins. Editors are JUCE-managed floating windows (standard DAW pattern).

## Execution Order
1. Add editor management to TrackFXSlot (createEditor/closeEditor)
2. Add "Edit" button to FXSlotRow for plugin slots
3. Clean up editors on chain rebuild

---

## 1. TrackFXSlot Editor Methods
- `void showEditor()` — calls `pluginInstance->createEditor()`, sets visible, stores pointer
- `void closeEditor()` — hides and deletes editor
- `bool isEditorOpen() const`

## 2. FXSlotRow UI
- "Edit" button appears after bypass/move buttons when slot type is "plugin"
- On click: calls `showEditor()` on the corresponding slot
- Button text toggles between "Edit" and "Close" based on editor state

## 3. Cleanup
- `Track::rebuildFXChain()` calls `closeEditor()` on all slots before clearing
- Editor destroyed when slot is removed or chain reordered

---

## Files Changed
- `src/engine/TrackFXSlot.h` — add editor methods
- `src/ui/FXSlotRow.h` — add edit button, editor state
- `src/ui/FXSlotRow.cpp` — implement edit button + wiring
- `src/engine/Track.cpp` — close editors before rebuildFXChain
