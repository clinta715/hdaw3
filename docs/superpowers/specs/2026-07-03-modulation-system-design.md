# Modulation System — Design Spec

**Date:** 2026-07-03  
**Status:** Draft  
**Version:** 0.1  
**Applies to:** HDAW v0.5+

## Overview

Add per-track LFO-based modulation to HDAW. Each track gets a modulation
rack where the user can add LFO sources, each targeting one automatable
parameter (volume, pan, and future FX parameters). LFO output is summed
with the base parameter value in the per-sample audio loop.

## Data Model

### New ValueTree IDs (in `src/model/ProjectModel.h`)

```
MODULATION_LIST  — child of TRACK
MODULATION       — each LFO source
MOD_TARGET       — future: target assignment for matrix mode (reserved)
```

### New property identifiers

```
waveform         — int  0=sine 1=triangle 2=saw 3=square 4=s&h
rate             — double  frequency in Hz or beat-division multiplier
rateSync         — bool  true=beat-synced, false=free Hz
depth            — double  0.0–1.0 modulation depth
bipolar          — bool  false=unipolar 0→+1, true=bipolar -1→+1
phaseOffset      — double  0–360 degrees initial phase
targetParamID    — int  which parameter to modulate (1=volume, 2=pan, …)
targetClipIndex  — int  -1 = track-level, >=0 = clip-level
```

### ValueTree structure per track

```
TRACK
  ├── MODULATION_LIST
  │     ├── MODULATION { id="lfo_1", type="lfo", name="LFO 1",
  │     │                enabled=true, waveform=0, rate=1.0, rateSync=true,
  │     │                depth=0.3, bipolar=false, phaseOffset=0,
  │     │                targetParamID=1, targetClipIndex=-1 }
  │     └── MODULATION { ... }
  ├── FX_CHAIN
  ├── AUTOMATION_LIST
  └── CLIP_LIST
```

Each `MODULATION` node represents one LFO targeting exactly one parameter.
Multi-target matrix mode is reserved for a future v2 via a `MOD_TARGET`
child list.

## Engine

### src/engine/ModulationSource.h — LFO implementation

Base class for all modulation sources:

```cpp
class ModulationSource {
public:
    virtual ~ModulationSource() = default;
    virtual void prepare(double sampleRate) = 0;
    virtual float processSample(int64_t samplePos, double bpm,
                                double ppqPos, double sampleRate) = 0;
};
```

`LFOModulationSource` implements this with:

- **Waveform lookup** — sine via `std::sin`, triangle/saw/square via
  phase-accumulator math, S&H via a noise source updated per-cycle
- **Phase accumulator** — advanced per-sample by `rate / sampleRate`
  (when free) or `rate * bpm / 60.0 / sampleRate` (when beat-synced)
- **All parameters as atomics** — `waveform`, `rate`, `depth`, `bipolar`,
  `rateSync`, `phaseOffset`, `targetParamID`, `enabled` are all
  `std::atomic<T>` so the audio thread reads them lock-free. The UI
  thread writes them.
- **`processSample` logic**:
  1. If `!enabled` → return 0
  2. Advance phase: `phase += rate / sampleRate` (free)
     or `phase += (rate * bpm / 60.0) / sampleRate` (synced,
     where `rate` = cycles per beat, e.g. 1.0 = quarter note,
     4.0 = sixteenth note)
  3. Wrap phase at 1.0
  4. Lookup waveform value at `(phase + phaseOffset/360.0) % 1.0`
  5. Apply bipolar/unipolar scaling
  6. Return `value * depth`

### src/engine/ModulationManager.h — per-track container

```cpp
class ModulationManager {
public:
    void prepare(double sampleRate);
    void rebuild(const juce::ValueTree& modulationListTree,
                 double sampleRate);
    float getModulation(int paramID, int64_t samplePos,
                        double bpm, double ppqPos, double sampleRate);

private:
    struct ModEntry {
        std::unique_ptr<ModulationSource> source;
    };
    std::vector<ModEntry> entries;
    std::atomic<bool> needsRebuild{false};
    double sampleRate = 44100.0;
};
```

- `rebuild()` is called from the UI thread (under `stateLock`)
  when the ValueTree changes. It reconstructs the `entries` vector
  and sets `needsRebuild = false`.
- `getModulation()` is called from the audio thread per sample.
  It iterates the `entries` vector, calls `processSample()` on
  sources matching `paramID`, and returns the sum of their outputs.

### Track integration (src/engine/Track.h/.cpp)

```cpp
// Track.h — new members
std::unique_ptr<ModulationManager> modulationManager;

// Track.h — new methods
void rebuildModulation(const juce::ValueTree& modulationListTree);

// Track.cpp - prepareToPlay
void Track::prepareToPlay(...) {
    ...
    if (modulationManager)
        modulationManager->prepare(getSampleRate());
}

// Track.cpp - processBlock (per-sample loop change)
for (int sample = 0; sample < numSamples; ++sample) {
    float baseGain = volumeGain.getNextValue();
    float basePan  = panPosition.getNextValue();

    int64_t sampleOffset = transportManager.getCurrentSample() + sample;
    float modGain = modulationManager
        ? modulationManager->getModulation(1, sampleOffset, bpm, ppqPos, sr)
        : 0.0f;
    float modPan  = modulationManager
        ? modulationManager->getModulation(2, sampleOffset, bpm, ppqPos, sr)
        : 0.0f;

    float leftGain  = (baseGain + modGain) * (1.0f - (basePan + modPan));
    float rightGain = (baseGain + modGain) * (basePan + modPan);
    ...
}
```

The `TransportManager` already exposes `currentSample` (atomic) and
`getBPM()`, `samplesToPpq()`, so the per-loop modulation has everything
it needs to compute beat-synced LFO rates.

### RoutingManager integration

`RoutingManager::rebuildTrackFX` already calls `track->rebuildFXChain()`.
Add a parallel call to `track->rebuildModulation()` so that FX chain
and modulation state are both rebuilt when the track's ValueTree changes.

## UI

### src/ui/ModulationWidget.h/.cpp — new bottom-panel tab

A `QWidget` showing:

**Top toolbar:** `[+ Add LFO]` button, track name label.

**LFO list:** A vertical list of LFO panels, one per `MODULATION` entry
for the currently selected track. Each panel has:

| Control | Type | Behaviour |
|---------|------|-----------|
| Waveform selector | 5 `QPushButton` button group | Sine, Triangle, Saw, Square, S&H icons |
| Rate | `QDoubleSpinBox` (Hz) or `QComboBox` (beat divisions) | Switches based on sync toggle |
| Sync toggle | `QPushButton` | Toggles Hz ↔ beat-synced |
| Depth | `QSlider` (horizontal, 0–100%) | Real-time modulation depth |
| Bipolar | `QPushButton` toggle | Unipolar ↔ bipolar |
| Phase offset | `QDoubleSpinBox` 0–360° | Phase offset in degrees |
| Target param | `QComboBox` | Populated with available param names (Volume, Pan, …) |
| Bypass | `QPushButton` toggle | Enables/disables the LFO |
| Remove | `QPushButton` «×» | Removes the LFO and its MODULATION tree |
| Waveform preview | Custom `paintEvent` strip (~200×30px) | One cycle of the selected waveform |

### MainWindow integration

- Add `ModulationWidget` to `setupBottomPanel()` as tab index 6
  (after Step Sequencer).
- Connect `TrackHeaderWidget::trackSelectionChanged` to
  `ModulationWidget::loadTrack(int trackIndex)`.
- Connect each LFO control change to write to the corresponding
  property on the `MODULATION` ValueTree node.
- ValueTree property changes trigger `AudioEngine::valueTreePropertyChanged`
  which already calls `rebuildTrackFX` — extend this to also call
  `track->rebuildModulation()` so the audio thread picks up new LFO
  state.

### Track selection tracking

The `ModulationWidget` stores the current `trackIndex` and listens for
`trackSelectionChanged`. When the selection changes, it reads the
`MODULATION_LIST` child from that track's ValueTree and rebuilds the
widget's LFO panel list. When no track is selected, it shows an empty
state.

## Thread safety

| Operation | Thread | Protection |
|-----------|--------|------------|
| LFO parameter write | UI | `std::atomic<T>::store()` |
| LFO parameter read | Audio | `std::atomic<T>::load()` |
| LFO add/remove | UI | ValueTree mutation + `needsRebuild` flag |
| `rebuildModulation()` | UI (via VTS listener) | `stateLock` (same lock Track::processBlock uses) |
| `getModulation()` | Audio per-sample | Lock-free (reads atomics only) |
| Phase accumulator write | Audio only | Not shared — per-source private member |

## File changes summary

| File | Change |
|------|--------|
| `src/model/ProjectModel.h` | +9 IDs, +1 property identifier |
| `src/engine/ModulationSource.h` | **New** — `ModulationSource` base + `LFOModulationSource` |
| `src/engine/ModulationManager.h` | **New** — per-track container |
| `src/engine/Track.h` | +1 member, +1 method declaration |
| `src/engine/Track.cpp` | +prepare/rebuild wiring, modify per-sample loop |
| `src/engine/RoutingManager.cpp` | +rebuildModulation call |
| `src/ui/ModulationWidget.h` | **New** — header |
| `src/ui/ModulationWidget.cpp` | **New** — implementation |
| `src/ui/MainWindow.h` | +1 member, +1 forward decl |
| `src/ui/MainWindow.cpp` | +tab creation, +signal wiring |
| `src/CMakeLists.txt` | +4 new source entries |

## Out of scope (v1)

- Multi-target modulation matrix (one LFO → many params)
- Envelope follower, step sequencer, or other modulation source types
- MIDI-learn for modulation depth/rate
- Modulation of FX plugin parameters
- Clip-level modulation targets
- Global modulation rack (not per-track)
- Automation of modulation parameters (modulating the modulator)
- Waveform preview animation at LFO rate
- LFO retrigger on note/play start
