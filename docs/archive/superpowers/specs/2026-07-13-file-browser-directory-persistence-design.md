# File Browser Directory Persistence & Default Directories

**Date**: 2026-07-13
**Status**: Design (pre-implementation)

## Problem

1. The file browser tree (`ProjectPoolBrowser`) starts at the filesystem root every launch — there is no way to remember where the user last navigated.
2. File dialogs (Open/Save Project, Import Audio, Import MIDI) all share the same `kKeyLastProjectDir` QSettings key, with no way to set permanent defaults for each purpose.

## Solution

Add per-purpose default directory preferences and file-browser tree persistence.

### 1. Preferences — Default Directories Section

A new `QGroupBox` in `PreferencesDialog` with three path pickers. Each picker is a `QLineEdit` + "Browse..." button (opens `QFileDialog::getExistingDirectory`) + "Clear" button (resets to empty).

| Setting | QSettings Key | Dialog defaults for |
|---|---|---|
| Default project folder | `kKeyDefaultProjectDir` | Open Project, Save Project As |
| Default audio samples folder | `kKeyDefaultAudioDir` | Import Audio, file-browser tree initial dir |
| Default MIDI folder | `kKeyDefaultMidiDir` | Import MIDI |

A fourth key `kKeyLastBrowserDir` tracks the file-browser tree's current root (not shown in preferences, persisted automatically).

### 2. File Browser Tree Persistence

**`ProjectPoolBrowser`** constructor:
1. Read `kKeyLastBrowserDir` from QSettings
2. If non-empty → set tree root to that path
3. If empty → fall back to `kKeyDefaultAudioDir` from preferences
4. If still empty → filesystem root (current behavior)

On navigation (`QTreeView::clicked` or `QFileSystemModel::directoryLoaded`): save the current directory path to QSettings under `kKeyLastBrowserDir`.

On `ProjectPoolBrowser` destructor: save current root dir.

### 3. QFileDialog Fallback Chain

Each dialog call site changes from a single `kKeyLastProjectDir` lookup to:

```
lastUsedDir (current) → purpose-specific default from prefs → empty string
```

| Call site | Last-used key | Default key |
|---|---|---|
| Open Project | `kKeyLastProjectDir` | `kKeyDefaultProjectDir` |
| Save Project As | `kKeyLastProjectDir` | `kKeyDefaultProjectDir` |
| Import Audio | `kKeyLastProjectDir` | `kKeyDefaultAudioDir` |
| Import MIDI | `kKeyLastProjectDir` | `kKeyDefaultMidiDir` |

### 4. Files Modified

| File | Change |
|---|---|
| `src/ui/PreferencesDialog.h` | Add 4 key constants (`kKeyDefaultProjectDir`, `kKeyDefaultAudioDir`, `kKeyDefaultMidiDir`, `kKeyLastBrowserDir`) + 3 static accessors |
| `src/ui/PreferencesDialog.cpp` | Add "Default Directories" group box with 3 path pickers; save/load in `onApply()`/`loadSettings()` |
| `src/ui/ProjectPoolBrowser.h` | Add `saveBrowsedDir()` method, override destructor |
| `src/ui/ProjectPoolBrowser.cpp` | Constructor: restore last browsed dir; connect tree selection to save; destructor: save |
| `src/ui/MainWindow.cpp` | 4 QFileDialog call sites: add default-dir fallback |

### 5. Backward Compatibility

Existing `kKeyLastProjectDir` and `kKeyLastExportDir` keys are untouched. The new defaults are purely additive — users who do not set them see no change in behavior (the fallback chain falls through to empty string, same as before).
