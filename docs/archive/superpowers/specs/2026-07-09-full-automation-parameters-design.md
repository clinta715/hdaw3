# Full Automation Parameter Coverage

**Date**: 2026-07-09  
**Status**: Draft  
**Version**: 0.5.0 candidate  

## Problem

The track automation editor only exposes Volume as an automatable parameter. Pan and Mute are supported by the engine but have no default automation lane. Plugin FX parameters have no automation infrastructure at all.

## Solution

Two-part implementation:

- **Part A**: Add default Pan (paramID 2) and Mute (paramID 3) automation lanes alongside Volume at track creation. Fix mute dispatch in the audio-thread automation loop. (~4 files, ~50 lines)
- **Part B**: Infrastructure for plugin FX parameter automation — a param registry in `TrackFXSlot`, compound paramID encoding, on-demand lane creation via the AutomationLaneWidget UI, and audio-thread dispatch. (~6 files, ~400 lines)

---

## Part A — Track-Level Default Lanes

### 1. Shared helper for default automation lanes

`ProjectModel.cpp` refactors `createAutomationList()` → `createTrackAutomationList()` that builds three `AUTOMATION` children:

| Lane | paramID | Default value | Range | Stored property |
|------|---------|---------------|-------|-----------------|
| Volume | 1 | 1.0 (0 dB) | 0.0–1.0 | `IDs::volume` |
| Pan | 2 | 0.5 (center) | 0.0–1.0 → maps to -1..+1 | `IDs::pan` |
| Mute | 3 | 0 (unmuted) | 0 or 1 | `IDs::isMuted` |

The three default tracks in `createDefaultProject()` call this helper, as does `MainWindow::onAddTrack()` and the MCP `add_track` tool.

### 2. Fix mute dispatch in Track::processBlock

The automation dispatch loop (lines 217–229) currently only handles paramID 1 (volume) and 2 (pan). Add paramID 3 (mute):

```cpp
if (pid == 1)
    volumeGain.setTargetValue(static_cast<float>(value));
else if (pid == 2)
    panPosition.setTargetValue(static_cast<float>(value * 2.0f - 1.0f));
else if (pid == 3)
    isMuted.store(value >= 0.5f);
```

### 3. Fix AudioEngine recording for mute

`AudioEngine::valueTreePropertyChanged` (line 255) excludes `IDs::isMuted` from on-the-fly automation recording with `property != IDs::isMuted`. Remove this exclusion so mute changes during playback record automation points. The binary nature (0 or 1) is fine — it records a point at the transition time.

### 4. Files changed

| File | Change |
|------|--------|
| `src/model/ProjectModel.cpp` | Refactor `createAutomationList()` → 3-lane helper |
| `src/ui/MainWindow.cpp` | `onAddTrack()` uses shared 3-lane helper |
| `src/engine/Track.cpp` | Add pid==3 dispatch in `processBlock` |
| `src/engine/AudioEngine.cpp` | Remove `property != IDs::isMuted` exclusion |
| `src/mcp/McpTools.cpp` | `add_track` creates 3 lanes |

---

## Part B — Plugin FX Parameter Automation

### 1. Compound paramID scheme

```
paramID = 100 + slotIndex * 100 + paramIndex
```

- Track-level: 1 (Volume), 2 (Pan), 3 (Mute) — unchanged
- Slot 0: 100–199 (param 0 → 100, param 1 → 101, …)
- Slot 1: 200–299
- Slot N: `100 + N*100` – `100 + N*100 + 99`

This encodes slot index and param index into a single integer that fits the existing `paramID` paradigm. Decode:

```
slotIndex   = (paramID - 100) / 100
paramIndex  = (paramID - 100) % 100
```

### 2. TrackFXSlot param registry + atomic cache

Add to `TrackFXSlot.h`:

```cpp
struct ParamInfo {
    juce::String name;
    int index;
};

const std::vector<ParamInfo>& getAutomatableParams() const;
void setAutomationParam(int paramIndex, float normalizedValue);
void applyAutomation();
```

- `getAutomatableParams()` iterates `pluginInstance->getParameters()` and builds `{name, index}` vector. Cached; rebuilt when the plugin instance changes.
- `std::vector<std::atomic<float>> paramValues` — sized to `getParameters().size()`, initialized to the current parameter value on plugin load.
- `setAutomationParam(paramIndex, val)` — `paramValues[paramIndex].store(val, relaxed)`.
- `applyAutomation()` — at the top of `TrackFXSlot::process()`, iterate `paramValues` and call `pluginInstance->getParameters()[i]->setValue(v)`. `setValue()` (without `NotifyingHost`) is realtime-safe per JUCE contract. The loop is bounded by `paramValues.size()` (typically < 64 params).

The built-in FX types (EQ/Compressor/Reverb/Delay) have no `AudioProcessorParameter`s and return an empty list.

### 3. Dispatch in Track::processBlock

After the existing pid 1/2/3 dispatch in the automation loop:

```cpp
else if (pid >= 100)
{
    int si = (pid - 100) / 100;
    int pi = (pid - 100) % 100;
    if (si < static_cast<int>(fxChain.size()) && fxChain[si])
        fxChain[si]->setAutomationParam(pi, static_cast<float>(value));
}
```

In the FX chain loop (lines 236–242), call `slot->applyAutomation()` before `slot->process()`:

```cpp
for (const auto& slot : fxChain) {
    if (slot) {
        slot->applyAutomation();   // ← new line
        slot->process(buffer, midiMessages);
    }
}
```

This ensures plugin parameter values reflect the automation curve at each block boundary.

### 4. Automation lane creation UI

`AutomationLaneWidget` gets an "Add Lane" button (a `+` button beside the paramCombo). Clicking it opens a `QMenu` with:

```
─ Track Parameters ─
  Volume
  Pan
  Mute
─ Slot 1: <plugin name> ─
  <param 0 name>
  <param 1 name>
  ...
─ Slot 2: ... ─
```

Building the menu requires enumerating:
- Track-level: "Volume" (1), "Pan" (2), "Mute" (3)
- Per FX slot: `slot->getAutomatableParams()`, skipping current slots that are "none" or bypassed, and skipping params already added as lanes.

Selecting an item calls a new method `addAutomationLane(name, paramID)` that creates an `AUTOMATION` child in the track's `AUTOMATION_LIST` with:
- `name`: display name
- `paramID`: the compound ID
- `curveType`: "linear"
- `automationEnabled`: false
- `POINT_LIST`: one default point at time=0, value=0.5 (midpoint)

A "Remove Lane" button (or right-click context menu on the combobox) removes the current lane with confirmation.

**Rebuilding the cache after add/remove**: After mutating `AUTOMATION_LIST`, call `engine.getMainProcessor()->rebuildAutomationCache(trackIndex)` and `refreshParamCombo()`.

### 5. Serialization

No special handling needed. Plugin parameter automation lanes are `AUTOMATION` children of `AUTOMATION_LIST` stored via ValueTree. On project load:
1. `Track::setAutomationTrees` creates `AutomationManager` instances for all lanes
2. The `paramID` values (100+) survive load/save
3. On first playback, `processBlock` dispatches pid ≥ 100 to the correct slot/param

**Edge case**: If a plugin is removed or replaced after automation lanes were created, the lanes remain in the ValueTree but their paramID targets no valid slot/param. The dispatch in `processBlock` bounds-checks `si < fxChain.size()` and `pi < slot->getAutomatableParams().size()` and silently skips out-of-range lanes. The user can remove stale lanes from the Add Lane menu (which skips them).

### 6. Files changed

| File | Change |
|------|--------|
| `src/engine/TrackFXSlot.h` | `ParamInfo`, `getAutomatableParams()`, `setAutomationParam()`, `applyAutomation()`, `paramValues` vector |
| `src/engine/Track.h` | Public access to fxChain for automation dispatch |
| `src/engine/Track.cpp` | dispatch pid≥100 in automation loop, `applyAutomation()` before `process()` |
| `src/ui/AutomationLaneWidget.h` | Add Lane / Remove Lane buttons, `addAutomationLane()` |
| `src/ui/AutomationLaneWidget.cpp` | Menu building, lane creation, removal |
| `src/model/ProjectModel.h` | (no new IDs needed — reuses existing AUTOMATION/AUTOMATION_LIST/paramID) |

---

## Scope / Non-goals

- **Built-in FX params**: The JUCE built-in FX (EQ, Compressor, Reverb, Delay) are not `AudioProcessor` subclasses — they use `juce::dsp` modules that don't expose `AudioProcessorParameter`. They have no automatable parameters in this pass. Future work could wrap them, but that's deferred.
- **MIDI CC automation**: This is a separate system using `CC_LIST`/`CC_POINT` in the clip model, not track-level automation lanes. Not in scope.
- **Modulation**: The `ModulationManager` is a separate system (LFO/envelope modulation). Not in scope.
- **Solo automation**: Solo is a monitoring function, not an audio-path parameter. Not included.

## Testing

### Part A tests
1. Create new project → each track has Volume, Pan, Mute in the automation lane combo box
2. Add new track via toolbar → same 3 lanes present
3. Draw a pan automation curve → playback pans correctly
4. Draw a mute automation curve → track mutes/unmutes at curve transitions
5. Save + reload project → lanes and points persist
6. Record-enable, play, change volume/pan/mute → points recorded during playback

### Part B tests
1. Add a plugin (VST3 or CLAP) to a track FX slot → plugin params appear in the "Add Lane" menu
2. Create an automation lane for a plugin parameter → param values change during playback
3. Multiple plugin slots → params from all slots appear, namespaced by slot
4. Remove a plugin → stale lanes silently skipped (no crash)
5. Save + reload → plugin param lanes persist
6. Add lane via MCP tool → works alongside UI-created lanes
