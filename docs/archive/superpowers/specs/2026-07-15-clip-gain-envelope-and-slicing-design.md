# Clip Gain Envelope & Slicing — Design Spec

**Date:** 2026-07-15  
**Version:** 0.1  
**Status:** Draft

---

## Overview

Two related features for the audio clip editor:

1. **Per-Clip Gain Envelope** — Draw volume automation directly on clips using existing automation lane infrastructure
2. **Clip Slicing** — Split clips at playhead, selection, or transients (non-destructive)

Both features operate on audio clips only and integrate with the existing undo/redo, automation, and ValueTree model.

---

## Feature A: Per-Clip Gain Envelope

### Goal
Allow users to draw gain/volume automation on individual audio clips, similar to Ableton's clip envelopes, using the existing automation system.

### Architecture

**Reuse existing automation infrastructure:**
- Clips already have `AUTOMATION_LIST` child in ValueTree
- `AutomationManager` reads automation at sample-accurate positions
- `AutomationLaneWidget` provides the UI for drawing/editing
- SPSC bridge already has paramID 10 = clip gain

**Data Flow:**
```
User draws in AutomationLaneWidget
    → AutomationManager writes points to ValueTree (CLIP/AUTOMATION_LIST)
    → AudioEngine valueTreePropertyChanged listener
    → SPSCBridge pushes ParamUpdate(paramID=10, value) to audio thread
    → ClipSourceProcessor::processBlock reads gain via AutomationManager::getValueAtTime()
    → Applied as multiplier to clip's base gain
```

### ValueTree Structure (Existing)
```
CLIP
  └── AUTOMATION_LIST
        └── AUTOMATION (paramID=10, enabled=true)
              └── AUTOMATION_POINT_LIST
                    └── POINT (time, value, shape)
```

### Components to Modify

| Component | Change |
|-----------|--------|
| `ClipSourceProcessor` | Add `AutomationManager*` member; read gain automation in `processBlock` |
| `AudioEngine` | Attach `AutomationManager` to clip processors during `rebuildClipsForTrack` |
| `AutomationLaneWidget` | Add "Clip Gain" to add-lane menu when audio clip selected |
| `ProjectCommands` | `addClipAutomationLane(clipId, paramID=10)` helper |
| `ReadModel` | `getClipAutomationLanes(clipId)` for UI population |

### ClipSourceProcessor Changes

```cpp
// In ClipSourceProcessor.h
class ClipSourceProcessor : public juce::AudioProcessor
{
    // ...
    void setAutomationManager(AutomationManager* am) { automationManager = am; }
    
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // ... existing code ...
        
        // Read gain automation at current playhead time
        if (automationManager && playhead)
        {
            double timeSec = playhead->getPosition()->getTimeInSeconds().orFallback(0.0);
            double autoGain = automationManager->getValueAtTime(timeSec);
            if (autoGain >= 0.0)
            {
                // Multiply with base gain (already smoothed via gainSmooth)
                gainSmooth.setTargetValue(static_cast<float>(autoGain) * baseGain.load());
            }
        }
        // ... apply gain/envelope ...
    }
    
private:
    AutomationManager* automationManager = nullptr;
    std::atomic<float> baseGain{ 1.0f };  // from setGain()
};
```

### Automation Lane UI

- When audio clip selected in timeline, bottom panel shows AutomationLaneWidget
- "+" button menu includes: **Track Volume, Track Pan, Track Mute, Clip Gain, [FX params...]**
- "Clip Gain" creates automation lane with `paramID=10` on the clip's `AUTOMATION_LIST`
- Lane color: distinct from track lanes (e.g., teal vs blue)

### Parameter Mapping

| Parameter | paramID | Range | Notes |
|-----------|---------|-------|-------|
| Clip Gain | 10 | 0.0 – 2.0 (linear) | Multiplier on base gain; 1.0 = unity |

---

## Feature C: Clip Slicing (Non-Destructive)

### Goal
Split audio clips into multiple slices at playhead position, selected region, or detected transients. Original clip preserved.

### Slice Operations

| Operation | Trigger | Behavior |
|-----------|---------|----------|
| Slice at Playhead | `Ctrl+Shift+S` / Context Menu | Split clip at transport position |
| Slice Selection | Region selected in waveform + Context Menu | Split at selection start/end |
| Slice at Transients | Context Menu "Slice at Transients" | Auto-detect onsets, slice at each |

### Behavior

**Non-destructive:**
- Original clip remains at same timeline position with all properties intact
- New clips inserted sequentially after original on same track
- Each slice references same `sourceFile` with adjusted `offset` and `duration`
- Slices inherit: gain, fades, stretch settings, looping, color
- All operations wrapped in single undo transaction

**Example:**
```
Before:  [ Clip A: offset=0, dur=4s ]──────────
After slice at 1.5s:
         [ Clip A: offset=0,     dur=1.5s ]──
         [ Clip A-1: offset=1.5, dur=2.5s ]──
```

### Transient Detection Algorithm

**Simple onset detection (v1):**
1. Load source audio into float buffer (reuse `ProjectPool` reader)
2. Compute RMS in 5ms windows (hop 2.5ms)
3. Detect peaks where RMS > threshold × local median
4. Minimum spacing: 50ms between onsets
5. Return onset times in seconds relative to file start

**Implementation:** New `AudioAnalysis` module in `src/engine/`:
```cpp
namespace HDAW {
    struct TransientDetector {
        static std::vector<double> detectTransients(
            const juce::String& sourceFile,
            juce::AudioFormatManager& formatManager,
            double threshold = 1.5,      // multiplier on local median
            double minSpacingSec = 0.05  // 50ms
        );
    };
}
```

### Components to Add/Modify

| Component | Change |
|-----------|--------|
| `ProjectCommands` | `sliceClipAtTime(clipId, timeSec)`, `sliceClipAtTransients(clipId)` |
| `AudioAnalysis.{h,cpp}` | New: transient detection |
| `TimelineInteraction` | Handle `Ctrl+Shift+S`; context menu items |
| `AudioWaveformWidget` | Region selection already emits `regionSelected` — connect to slice |
| `AudioClipEditorWidget` | Add "Slice at Playhead/Transients" buttons to control bar |

### Slice Implementation (ProjectCommands)

```cpp
void ProjectCommands::sliceClipAtTime(int clipId, double timeSec)
{
    auto clip = findClipById(clipId);
    if (!clip.isValid()) return;
    
    double clipStart = clip.getProperty(IDs::startTime);
    double clipDur   = clip.getProperty(IDs::duration);
    double clipOffset = clip.getProperty(IDs::offset);
    
    double relTime = timeSec - clipStart;  // relative to clip start
    if (relTime <= 0 || relTime >= clipDur) return;
    
    auto track = ProjectModel::getTrackOfClip(clip);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    int clipIndex = clipList.indexOf(clip);
    
    // Original clip: shorten duration
    clip.setProperty(IDs::duration, relTime, undoManager);
    
    // New slice clip
    auto newClip = clip.createCopy();
    newClip.setProperty(IDs::offset, clipOffset + relTime, undoManager);
    newClip.setProperty(IDs::duration, clipDur - relTime, undoManager);
    newClip.setProperty(IDs::name, clip.getProperty(IDs::name) + "-slice", undoManager);
    newClip.setProperty(IDs::clipID, generateClipId(), undoManager);
    
    clipList.addChild(newClip, clipIndex + 1, undoManager);
    engine.getMainProcessor()->rebuildRoutingGraph();
}
```

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Shift+S` | Slice at Playhead |
| Context Menu → Slice at Playhead | Same |
| Context Menu → Slice Selection | Split at region bounds |
| Context Menu → Slice at Transients | Auto-slice |

---

## Integration Points

### Undo/Redo
- All slice operations use `UndoManager` via `ProjectCommands`
- Automation lane edits already use `AutomationManager` → `ValueTree` → `UndoManager`

### Routing Graph Rebuild
- `sliceClipAtTime` calls `rebuildRoutingGraph()` after modifying clip list
- Automation lane addition triggers `rebuildAutomationCache()` on track

### MCP Tools (Future)
- `slice_clip_at_time`, `slice_clip_at_transients` tools
- `add_clip_automation_lane` tool

---

## Testing

### Unit Tests
- `AudioAnalysis_transient_test.cpp` — detectTransients on known signals
- `ProjectCommands_slice_test.cpp` — slice at time, verify clip properties
- `ClipSourceProcessor_automation_test.cpp` — gain automation read on audio thread

### Integration Tests
- Slice → undo → redo preserves state
- Automation lane on clip → play → verify gain modulation
- Multiple slices → each has correct offset/duration

---

## Future Extensions (Out of Scope)

- Gain envelope drawn directly on waveform (overlay)
- Per-slice fade handles in timeline
- Slice to new track / sampler
- Spectral transient detection (MFCC-based)
- MIDI clip slicing (by note)

---

## Acceptance Criteria

1. **Gain Envelope:** User can add "Clip Gain" automation lane, draw points, hear volume modulation on playback
2. **Slice at Playhead:** `Ctrl+Shift+S` splits clip at transport position; original shortened clip-15 0 transport; original + new clip both play correctly
3. **Slice at Transients:** Detects onsets on drum loop; creates slices at each hit; slices align to transients
4. **Non-destructive:** Original clip unchanged; undo restores exact previous state
5. **Performance:** No audio thread allocations; transient detection runs off-thread