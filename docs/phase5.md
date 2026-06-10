# Phase 5: Lint Sweep — v0.2.1

## Summary

Mechanical lint fixes across 21+ files. No behavioral changes. Version bumped
0.2.0 → 0.2.1.

## L-1: Unprotected `std::min`/`std::max` — Windows macro safety

Added extra parentheses so `std::min(a, b)` becomes `(std::min)(a, b)`.  This
prevents the Windows `min`/`max` macros from expanding into standard-library
calls, which would break compilation on MSVC with `<windows.h>` in scope.

**56 occurrences** in 21 files:

`AudioClipEditorWidget.cpp` · `AudioWaveformWidget.cpp` · `AutomationLaneWidget.cpp`
`CCLaneWidget.cpp` · `CLAPPluginInstance.cpp` · `ClipItem.cpp` · `ExportManager.cpp`
`LoopMarker.cpp` · `MainWindow.cpp` · `MidiClipItem.cpp` · `MidiClipProcessor.h`
`MixerStripWidget.cpp` · `NoteGridWidget.cpp` · `PianoRollRuler.cpp`
`PianoRollWidget.cpp` · `ProjectPoolBrowser.cpp` · `TimeRuler.cpp`
`TimelineInteraction.cpp` · `TimelineScene.cpp` · `TimelineView.cpp`
`TrackHeaderWidget.cpp` · `VUMeter.cpp` · `VelocityLaneWidget.cpp`

## L-2: Non-const `auto&` in range-for — COW detach risk

Changed 14 range-for loops from `auto&` to `const auto&` to prevent accidental
deep copy (COW detach) on Qt shared containers. Three loops intentionally kept
as `auto&`:

- TrackHeaderWidget: lines 30, 83 — mutate struct members.
- PianoRollModel.h: line 72 — `removeSelectedNotes` modifies via loop.

**Files**: `TimelineScene.cpp` (2) · `AutomationLaneWidget.cpp` (2) ·
`CLAPPluginInstance.cpp` (1) · `Track.cpp` (7) · `PluginManager.cpp` (4) ·
`CLAPPluginFormat.cpp` (1) · `NoteGridWidget.cpp` (1) · `FXSlotRow.cpp` (2).

## L-3: `QFile::open()` return not checked

Added error fallback to stderr in `DebugLog.h`:

```cpp
if (!f.open(...))
    fputs("HDAW: Failed to open debug log file\n", stderr);
```

## L-4: `QDateTime::currentDateTimeUtc()` in `DebugLog.h`

Replaced `currentDateTime()` (local time) with `currentDateTimeUtc()` for
deterministic log timestamps regardless of local time zone / DST.

## Excluded from this phase

| Category | Reason |
|----------|--------|
| 52 `get`-prefix getters (API-5) | Too invasive — changes every call site. |
| 10 direct brace inits (VAR-3) | Purely cosmetic. |
| 5 unscoped enums (ENM-2) | Requires call-site prefix updates; defer to a dedicated pass. |
| 1 integer timeout (TMO-1) | Low impact, single occurrence. |
