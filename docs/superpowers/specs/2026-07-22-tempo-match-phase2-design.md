# Tempo-Match Phase 2: BPM Detection, Manual Input, Bar Snapping

**Date:** 2026-07-22
**Status:** Approved
**Scope:** Group A — Import flow improvements (BPM detection, manual BPM dialog, bar-boundary snapping)

## Summary

Extend the existing tempo-match-on-import feature with automatic BPM detection
(via aubio), a manual BPM input dialog (Shift-import or detection failure), and
optional bar-boundary snapping. This ensures every imported audio file can be
tempo-matched, even without BPM metadata.

## Current State

Phase 1 (already implemented):
- `readBpmFromMetadata()` reads BPM from audio file tags (`AudioImport.cpp:10-27`)
- Auto tempo-match applies `stretchMode=1`, `stretchRatio=bpm/projectBpm`
  when preference is enabled (`AudioImport.cpp:99-113`)
- Manual `tempoMatchClip(clipId)` available from UI (`AudioEngineCommands.cpp:1543`)
- Preview tempo-match in file browser (`AudioPreviewPlayer`, `ProjectPoolBrowser`)

Phase 1 gap: if the file has no BPM metadata tag, no detection fallback exists.
The clip is imported without tempo-matching.

## Architecture

```
AudioImport::importAudioFile()
  ├── readBpmFromMetadata()          // existing: check file tags
  ├── if no metadata:
  │     └── BpmDetector::detect()    // new: aubio onset tracking → BPM
  ├── if Shift held OR no BPM found:
  │     └── BpmInputDialog            // new: manual entry dialog
  ├── if BPM available:
  │     ├── apply tempo-match         // existing logic
  │     └── if snapPreference:        // new: snap to bar
  │           └── snapToBarBoundary()
  └── finish import
```

## Components

### 1. BpmDetector (aubio wrapper)

**Files:** `src/engine/BpmDetector.h`, `src/engine/BpmDetector.cpp`

```cpp
class BpmDetector {
public:
    struct Result {
        double bpm = 0.0;
        double confidence = 0.0;  // 0.0–1.0 (informational; flow uses bpm > 0 as success)
    };

    // Detect BPM from interleaved float samples.
    // sampleRate must match the audio file's rate.
    // Reads up to maxSeconds (default 30s) of audio.
    static Result detect(const float* samples, int numSamples,
                         double sampleRate, double maxSeconds = 30.0);
};
```

Implementation details:
- Creates `aubio_tempo` with `buffer_size=1024`, `hop_size=512`
- Feeds audio in hop-sized chunks from the sample buffer
- Collects beat timestamps, computes inter-beat intervals
- Returns the dominant BPM (most frequent interval) and confidence score
- RAII cleanup of aubio objects

Detection runs on first 30 seconds of audio — fast (~100ms on typical files),
accurate for most material.

### 2. BpmInputDialog

**Files:** `src/ui/BpmInputDialog.h`, `src/ui/BpmInputDialog.cpp`

```cpp
class BpmInputDialog : public QDialog {
    Q_OBJECT
public:
    // detectedBpm: pre-filled if aubio found something (0.0 = no detection)
    // Returns 0.0 if user cancels, >0.0 if user confirms
    static double ask(QWidget* parent, double detectedBpm = 0.0);
};
```

Dialog contents:
- Title: "Enter BPM"
- `QDoubleSpinBox` range 20–300, step 0.1, decimals 1
- Pre-fills with `detectedBpm` if > 0
- OK / Cancel buttons
- Returns BPM value or 0.0 on cancel

### 3. Bar-snapping logic

**Files:** `src/engine/BarSnap.h`, `src/engine/BarSnap.cpp`

```cpp
namespace HDAW {
    // Snap a time position to the nearest bar boundary.
    // Uses project tempo map and beatsPerBar.
    double snapToBarBoundary(double timeSeconds, AudioEngine& engine);
}
```

Implementation:
- Get tempo at `timeSeconds` via `TransportManager::getBpmAtTime()`
- Get `beatsPerBar` from metronome (default 4)
- Compute seconds-per-bar: `(60.0 / bpm) * beatsPerBar`
- Compute bar position: `timeSeconds / secondsPerBar`
- Round to nearest integer, multiply back
- Return snapped time (or unchanged if < 1 sample shift)

### 4. Import flow integration

Updated `AudioImport::importAudioFile()` flow:

1. Read audio file, compute duration (existing)
2. `detectSilenceBounds()` (existing)
3. Create clip ValueTree (existing)
4. **BPM resolution (new):**
   - `bpm = readBpmFromMetadata(reader)` — try metadata first
   - If `bpm == 0`: `bpm = BpmDetector::detect(samples, n, sr).bpm` — aubio fallback
   - If `bpm == 0` OR Shift held: `bpm = BpmInputDialog::ask(parent, detectedBpm)`
   - If user cancels (returns 0): skip tempo-match entirely
5. Apply tempo-match (existing stretchMode/stretchRatio/duration logic)
6. **Bar snap (new):** If snap preference enabled AND `startTime > 0`:
   `clip.setProperty(IDs::startTime, snapToBarBoundary(startTime, engine))`
7. `rebuildRoutingGraph()` (existing)

Key: BPM resolution happens **before** the clip is added to the scene, so no
visual flicker. The clip appears already stretched and snapped.

### 5. Preferences

New checkbox in `PreferencesDialog`:

```
[√] Auto tempo-match imported audio     (existing)
[√] Snap imported clips to bar boundaries  (new)
```

- QSettings key: `snapToBarBoundaries`, default `true`
- Read via `PreferencesDialog::getSnapToBarBoundaries()`
- Lives in the Import group alongside existing auto-tempo-match checkbox

## Dependencies

- **aubio:** fetched via CMake `FetchContent` from `https://github.com/aubio/aubio`
  - Built as static library
  - Version pinned to latest stable tag
  - Only `aubio_tempo` and supporting functions needed

## Files Changed

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add FetchContent for aubio, link to HDAW_lib |
| `src/engine/BpmDetector.h` | New — aubio wrapper interface |
| `src/engine/BpmDetector.cpp` | New — aubio onset detection + BPM extraction |
| `src/engine/BarSnap.h` | New — bar-boundary snapping utility |
| `src/engine/BarSnap.cpp` | New — bar-boundary snapping implementation |
| `src/ui/BpmInputDialog.h` | New — manual BPM entry dialog |
| `src/ui/BpmInputDialog.cpp` | New — dialog implementation |
| `src/engine/AudioImport.cpp` | Updated — integrate detection, dialog, snapping |
| `src/ui/PreferencesDialog.h` | Updated — add snap checkbox + getter |
| `src/ui/PreferencesDialog.cpp` | Updated — add snap checkbox UI + settings |

## Testing

1. Import a WAV with BPM metadata tag → verify auto tempo-match applies (no detection needed)
2. Import a WAV without BPM metadata → verify aubio detects BPM, tempo-match applies
3. Import with Shift held → verify dialog appears with detected BPM pre-filled
4. Import with no detection + no Shift → verify dialog appears empty
5. Import with snap preference on → verify clip start snaps to bar boundary
6. Import with snap preference off → verify clip start is unmodified
7. Import a spoken-word file (no rhythmic content) → verify dialog appears
8. Verify existing `tempoMatchClip()` still works independently
9. Run `hdaw_tests` — no regressions
