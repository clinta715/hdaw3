# Gain Envelope & Clip Slicing — Design Spec

**Date:** 2026-07-15
**Status:** Approved for implementation

---

## Overview

Two related audio clip editing features:

1. **Per-clip gain envelope** — Draw volume automation directly on the waveform (Ableton-style clip envelopes)
2. **Clip slicing** — Split clips at playhead or detected transients (spectral flux)

Both features operate on audio clips in the timeline and integrate with existing automation/undo infrastructure.

---

## 1. Gain Envelope (Per-Clip Volume Automation)

### 1.1 Architecture

**Reuse existing automation infrastructure:**
- Extend `AutomationManager` / `AutomationLaneWidget` to support a new `gainEnvelope` lane type
- New automation paramID for clip gain (distinct from track volume)
- Envelope evaluated at audio thread via `AutomationManager::getValueAtTime()` in `ClipSourceProcessor::processBlock`

**ValueTree structure:**
```text
CLIP
  AUTOMATION_LIST
    GAIN_ENVELOPE (type="gainEnvelope", enabled=true, paramID=<reserved>)
      GAIN_ENVELOPE_POINT (time=0.5, gain=0.0, shape="linear")
      GAIN_ENVELOPE_POINT (time=1.2, gain=0.5, shape="linear")
      ...
```

### 1.2 Audio Thread Integration

In `ClipSourceProcessor::processBlock`:
```cpp
// Existing clip gain
float g = gainSmooth.getNextValue();

// NEW: multiply by envelope value at current playhead time
if (auto* am = getGainEnvelopeAutomation()) {
    double envVal = am->getValueAtTime(currentTimeSeconds);
    if (envVal >= 0.0) g *= static_cast<float>(envVal);
}
```
- Envelope value range: `0.0` to `1.0` (multiplicative with clip gain)
- Linear interpolation between points
- No allocation on audio thread (uses existing lock-free automation cache)

### 1.3 UI/Interaction (AudioWaveformWidget)

**Overlay rendering:**
- Envelope curve drawn in accent color (cyan) on waveform
- Node handles at each envelope point (small circles)
- Fade in/out triangles remain visible underneath

**Mouse interaction:**
| Action | Result |
|--------|--------|
| Click empty waveform area | Add node at (time, gain) |
| Drag node | Move time & gain |
| Right-click node | Delete node |
| Hover node | Tooltip: "Time: 1.23s, Gain: -6.0 dB" |
| Double-click node | Dialog for exact time/gain entry |
| Ctrl+drag vertical | Constrain to gain only |
| Ctrl+drag horizontal | Constrain to time only |

**Keyboard:**
- `Delete` / `Backspace` — delete selected node
- `Escape` — clear selection

### 1.4 Automation Lane (Future)

- Dedicated `AutomationLaneWidget` tab for gain envelope
- Mirrors track automation UI (add/remove points, copy/paste, selection)
- Can be shown/hidden via track header automation toggle

---

## 2. Clip Slicing

### 2.1 Operations

| Shortcut | Action |
|----------|--------|
| `S` | Slice selected clip(s) at playhead |
| `Shift+S` | Slice selected clip(s) at transients |

### 2.2 Slice at Playhead (S)

**Logic:**
1. Get clips under playhead (selected, or hit-test at playhead)
2. For each clip:
   - If playhead within clip bounds (with 1ms tolerance):
     - Compute split time `t = playhead - clip.startTime`
     - Create two clips:
       - Clip A: `startTime` unchanged, `duration = t`, `offset` unchanged
       - Clip B: `startTime = clip.startTime + t`, `duration = clip.duration - t`, `offset = clip.offset + t`
     - Copy all properties (gain, fade, stretch, loop, automation)
     - Remove original, insert A then B at correct positions
3. Single undo transaction

### 2.3 Slice at Transients (Shift+S)

**Transient Detection (Spectral Flux):**
```cpp
// Off-thread worker (similar to StretchRenderer)
struct TransientDetector {
    void detect(const juce::String& sourceFile, double sampleRate,
                std::function<void(const std::vector<double>&)> onComplete);
};
```
- Load full audio via `AudioFormatReader` into `AudioBuffer<float>`
- STFT: 2048 FFT, 512 hop, Hann window
- Spectral flux = L2 norm of magnitude difference between consecutive frames
- Adaptive threshold: local mean (500ms window) + 1.5 × local std
- Peak picking with 50ms minimum separation
- Output: sample positions relative to file start

**Slicing Logic:**
1. Run transient detection on source file (off-thread, shows progress)
2. On completion, filter transients within clip's `offset`..`offset+duration`
3. Convert to clip-relative times
4. Create slices at each transient (same property copy as playhead slice)
5. Remove original, insert all slices
6. Single undo transaction

### 2.4 Edge Cases & Polish

- **Tiny slices**: Merge adjacent slices < 10ms into previous slice
- **No transients found**: Show toast "No transients detected in clip"
- **Playhead outside clips**: No-op (or slice selected clip at nearest edge?)
- **Multi-clip selection**: Slice all selected clips at playhead
- **Undo**: Single transaction — `Ctrl+Z` restores original clip(s)
- **Stretch cache**: New clips get new `clipID`; stretch re-rendered if needed

---

## 3. Implementation Plan Summary

### Phase 1: Gain Envelope Core
1. Reserve paramID for clip gain envelope (≥ 200 to avoid conflicts)
2. Add `GAIN_ENVELOPE` / `GAIN_ENVELOPE_POINT` IDs
3. Extend `AutomationManager` to support clip-level gain lane
4. Wire `ClipSourceProcessor` to read envelope value
5. Add envelope rendering to `AudioWaveformWidget::paintEvent`

### Phase 2: Gain Envelope Interaction
1. Add mouse handling for node add/drag/delete in `AudioWaveformWidget`
2. Connect to `ProjectCommands::setGainEnvelopePoint` / `removeGainEnvelopePoint`
3. Add tooltips, double-click editing, Ctrl-constraints

### Phase 3: Clip Slicing — Playhead
1. Add `ProjectCommands::sliceClipAtTime(clipId, time)`
2. Implement slicing logic in `ProjectModel` / `AudioEngine`
3. Add keyboard shortcut `S` in `TimelineView` / `TimelineInteraction`
4. Hook up undo transaction

### Phase 4: Clip Slicing — Transients
1. Create `TransientDetector` worker (off-thread, like `StretchRenderer`)
2. Spectral flux implementation (JUCE FFT + custom peak picking)
3. Add `ProjectCommands::sliceClipAtTransients(clipId)`
4. Keyboard shortcut `Shift+S`
5. Progress indicator during analysis

---

## 4. Testing

**Gain Envelope:**
- Unit: envelope interpolation, boundary clamping
- Integration: audio thread reads correct value during playback
- UI: node add/drag/delete, tooltip, constraints

**Slicing:**
- Unit: slice point calculation, property copying
- Integration: timeline updates, undo/redo, stretch cache
- Edge: tiny slices, empty detection, multi-clip

---

## 5. Out of Scope (Future)

- Per-track gain envelope (shared across clips)
- Formant-preserving pitch shift on slices
- Auto-slice on import (drum loop → individual hits)
- Region-based slicing (select region → slice)
- MIDI clip slicing (different feature)

---

## 6. Files to Modify

| File | Changes |
|------|---------|
| `src/model/IDs.h` | New `GAIN_ENVELOPE`, `GAIN_ENVELOPE_POINT` identifiers |
| `src/engine/AutomationManager.h/cpp` | Clip gain envelope support |
| `src/engine/ClipSourceProcessor.h` | Read envelope in `processBlock` |
| `src/ui/AudioWaveformWidget.h/cpp` | Envelope overlay + interaction |
| `src/ui/AudioClipEditorWidget.h/cpp` | Show/hide envelope toggle |
| `src/common/ProjectCommands.h/cpp` | `setGainEnvelopePoint`, `removeGainEnvelopePoint`, `sliceClipAtTime`, `sliceClipAtTransients` |
| `src/engine/AudioEngine.h/cpp` | Slicing implementation |
| `src/engine/TransientDetector.h/cpp` | New: spectral flux detector |
| `src/ui/TimelineView.cpp` | Keyboard shortcuts `S` / `Shift+S` |
| `src/ui/TimelineInteraction.cpp` | Hit-test for clip under playhead |
| `CMakeLists.txt` | Add `TransientDetector.cpp` |

---

## 7. Acceptance Criteria

- [ ] Gain envelope nodes editable on waveform (add/drag/delete)
- [ ] Envelope audible during playback (multiplies with clip gain)
- [ ] Envelope saved/loaded with project
- [ ] Undo/redo works for envelope edits
- [ ] `S` slices clip at playhead (single & multi-select)
- [ ] `Shift+S` detects transients and slices (progress shown)
- [ ] Slices preserve all clip properties
- [ ] Undo/redo works for slicing
- [ ] All existing tests pass + new unit tests

---

*Approved for implementation. Next step: invoke `writing-plans` skill to create detailed implementation plan.*