# Polish & Gaps — Phase Design

## Overview

Address four critical gaps in the HDAW application: automation not applied to audio, waveform thumbnails not rendered, VU meters not painted, and pan interaction not wired. All four are self-contained fixes touching separate subsystems.

## Execution Order

Engine-first, then UI:

1. Automation wiring (audio engine)
2. Waveform thumbnail rendering (timeline)
3. VU meter fixes (track headers + mixer)
4. Pan drag interaction (mixer strips)

## 1. Automation Wiring

### Problem
`AutomationManager` stores automation points and interpolates with `getValueAtTime()`, but `Track::processBlock()` never calls it. Automation data exists in ValueTree and the editor works, but has no effect on audio.

### Design

**Real-time reading:** During `processBlock`, `Track` queries `automationManager->getValueAtTime(transportSampleTime / sampleRate)` for each automated parameter.

**Playback vs. stopped:**
- **Playing:** automation value overrides the parameter (standard DAW behavior — faders follow automation)
- **Stopped:** UI fader value (delivered via SPSC bridge) controls the parameter directly

**Transport time access:** `Track` gets current playback time via `getPlayHead()->getPosition()->getTimeInSeconds()`.

**Data sync:** `AutomationManager::rebuildCache()` is called when automation nodes change in the ValueTree — ensure the track listens for automation changes and rebuilds.

**Per-parameter routing:** Store a mapping of paramID → automation curve index in `Track`. During playback, if automation data exists for a parameter, override the SPSC-delivered value with the interpolated automation value.

### Files changed
- `src/engine/Track.h` — add automation point cache, parameter→curve mapping
- `src/engine/Track.cpp` — call `getValueAtTime()` in `processBlock`, override params during playback
- `src/engine/AutomationManager.h` — expose `updateCacheFromValueTree()` non-destructively (already has `rebuildCache()`)

## 2. Waveform Thumbnail Rendering

### Problem
`AudioClipItem` loads a `juce::AudioThumbnail` but `paintContent()` draws placeholder sine waves instead of the actual waveform. Thumbnail data exists but is never painted.

### Design

**Replace placeholder painting:** In `AudioClipItem::paintContent()`, call `audioThumbnail->drawChannels()` onto the `QPainter` context.

**Coordinate mapping:** Clip pixel width maps to thumbnail time range (clip start → clip end). Compute a `juce::Rectangle<int>` from the QGraphicsItem rect and pass to `drawChannels()`.

**Thumbnail lifecycle:** The `AudioThumbnail` is created in the constructor via `ProjectPool::getThumbnailCache()`. Register as a `ChangeListener` on the thumbnail; when it finishes loading asynchronously, call `update()` on the QGraphicsItem to trigger repaint.

**Fallback:** If thumbnail isn't loaded yet, draw a simple filled rect with the clip name centered — graceful degradation.

### Files changed
- `src/ui/AudioClipItem.h` — inherit `ChangeListener`, add `changeListenerCallback()`
- `src/ui/AudioClipItem.cpp` — replace sine wave code with `drawChannels()` call

## 3. VU Meter Fixes

### Problem
Two VU displays are non-functional:
- `TrackHeaderWidget::updateVU()` reads levels then discards them (`static_cast<void>`)
- `MixerWidget` master VU widget is a plain `QWidget` with no paint override

### Design

**TrackHeader VU:**
- Store smoothed left/right levels as member variables from `updateVU()`
- Call `update()` to trigger repaint
- In `paintEvent()`, draw logarithmic VU bars matching the existing `VUMeter` style (green/yellow/red color zones, fast-attack/slow-decay envelope)

**Mixer master VU:**
- Replace the plain master VU `QWidget` with an instance of the existing `VUMeter` class
- Wire it to the master bus `LevelMeter`

### Files changed
- `src/ui/TrackHeaderWidget.h` — add level storage, `paintEvent()` override
- `src/ui/TrackHeaderWidget.cpp` — implement paint, fix `updateVU()`
- `src/ui/MixerWidget.cpp` — replace master VU widget with `VUMeter`

## 4. Pan Drag Interaction

### Problem
`MixerStripWidget` declares `setPan()` and `draggingPan` but has no mouse event handling for the pan area. Only volume fader works.

### Design

**Hit area:** Define a pan control region (thin strip below the VU meter area).

**Mouse events:**
- `mousePressEvent`: check if click is in pan region → set `draggingPan = true`, record initial pan
- `mouseMoveEvent`: map horizontal position to -1.0 to 1.0, call `setPan()`, push update via SPSC bridge
- `mouseReleaseEvent`: clear `draggingPan`

**Visual feedback:** Draw a small circle/knob indicator at the current pan position within the pan region.

### Files changed
- `src/ui/MixerStripWidget.h` — add pan region geometry, mouse event overrides
- `src/ui/MixerStripWidget.cpp` — implement mouse handling and pan indicator paint

## Testing

All four fixes are verifiable by running the application and checking:
1. Automation: toggle a track to "playing", automate volume, hear the change
2. Thumbnails: import an audio file, see the waveform in the timeline clip
3. VU: play audio, observe track header and master VU bars moving
4. Pan: drag the pan control in a mixer strip, hear the stereo image shift
