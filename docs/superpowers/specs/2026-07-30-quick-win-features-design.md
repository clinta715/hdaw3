# Quick-Win Features Design

**Date:** 2026-07-30
**Status:** Approved
**Scope:** 4 independent quick-win features to close DAW feature parity gaps

---

## Overview

Four independent features that fill visible gaps in HDAW's feature set. Each is
a focused addition to an existing system — no new subsystems or architectural
changes required. Implemented sequentially in the order listed.

1. **Loudness metering** (RMS + LUFS momentary) — Phase 9 Mixing
2. **Modulation FX** (Chorus + Flanger + Phaser) — Phase 5 FX
3. **Punch in/out** (using loop region) — Phase 3 Audio
4. **Automation modes** (Read/Write/Touch/Latch) — Phase 6 Automation

---

## Feature 1: Loudness Metering (RMS + LUFS Momentary)

### Current state

`LevelMeter` (in `src/engine/LevelMeter.h`) tracks peak L/R levels using
`buffer.getMagnitude()` per channel. Values are stored as atomic floats and
read at 30 Hz by `FrontendServer::onMeterTimer()`, which broadcasts
`notify.meters` to the frontend. The frontend renders vertical bars in
`MixerStrip.tsx`.

### Design

**Engine — `LevelMeter.h`:**
- Add RMS accumulator: exponential moving average with ~300ms time constant
  (`alpha = 1 - exp(-1 / (sampleRate * 0.3))`)
- Add LUFS momentary: K-weighted filter (two biquads — high-shelf + high-pass
  per ITU-R BS.1770) + mean square over 400ms sliding window + `10 * log10`
  conversion
- Both computed in `update(const AudioBuffer<float>& buffer)` alongside existing peak

**Engine — `ReadModel.h`:**
- `MeterSnapshot` gains: `rmsLeft`, `rmsRight`, `lufsMomentary` (all float)

**Engine — `FrontendServer.cpp`:**
- `onMeterTimer()` includes new fields in `notify.meters` payload

**Frontend — `types.ts`:**
- `MeterLevels` gains: `rmsL`, `rmsR`, `lufs`

**Frontend — `meterStore.ts`:**
- Store new fields from `notify.meters`

**Frontend — `MixerStrip.tsx`:**
- Render RMS bar (thinner, semi-transparent) behind existing peak bar
- Render LUFS numeric readout below the meter (e.g., "-14.2 LUFS")

**No new RPCs.** Uses existing `notify.meters` push at 30 Hz.

### Files

| File | Change |
|------|--------|
| `src/engine/LevelMeter.h` | Add RMS + LUFS computation |
| `src/common/ReadModel.h` | Extend `MeterSnapshot` |
| `src/engine/ReadModelImpl.cpp` | Read new meter fields |
| `src/frontend/FrontendServer.cpp` | Include new fields in meter push |
| `src/frontend/FrontendRpc.h` | Serialize new meter fields |
| `frontend/src/rpc/types.ts` | Extend `MeterLevels` |
| `frontend/src/store/meterStore.ts` | Store new fields |
| `frontend/src/components/MixerStrip.tsx` | RMS bar + LUFS readout |

---

## Feature 2: Modulation FX (Chorus + Flanger + Phaser)

### Current state

`TrackFXSlot` supports internal FX types: EQ, Compressor, Reverb, Delay.
Each has a fixed param set defined in `getParamDefsForType()`. JUCE's
`juce_dsp` module provides `Chorus<float>` (handles chorus and flanger via
parameter configs) and `Phaser<float>`.

### Design

**Engine — `TrackFXSlot.h`:**
- Add `Chorus`, `Flanger`, `Phaser` to `ActiveType` enum
- Add DSP members: `std::unique_ptr<juce::dsp::Chorus<float>>` for
  chorus/flanger, `std::unique_ptr<juce::dsp::Phaser<float>>` for phaser
- `getParamDefsForType()` — define params:
  - **Chorus:** Rate (0.1–5 Hz), Depth (0–1), Centre Delay (1–50ms), Feedback (-1–1), Mix (0–1)
  - **Flanger:** Rate (0.1–5 Hz), Depth (0–1), Centre Delay (1–5ms), Feedback (-1–1), Mix (0–1)
  - **Phaser:** Rate (0.1–5 Hz), Depth (0–1), Centre Frequency (20–20000Hz), Feedback (-1–1), Mix (0–1)
- `prepare()` — create and prepare DSP instances
- `process()` — apply DSP to buffer
- `applyInternalParamToDsp()` — push param values to DSP

**Engine — `AudioEngineCommands_Fx.cpp`:**
- Type mapping: int 4=chorus, 5=flanger, 6=phaser
- String mapping: "chorus", "flanger", "phaser"

**Frontend — `FXChainPanel.tsx`:**
- Add chorus/flanger/phaser to FX type selector dropdown

**MCP:** `audioGraph.addFxSlot` with `type: "chorus"` / `"flanger"` / `"phaser"`

### Files

| File | Change |
|------|--------|
| `src/engine/TrackFXSlot.h` | Add types, DSP members, param defs |
| `src/engine/TrackFXSlot.cpp` | Implement prepare/process for new types |
| `src/engine/AudioEngineCommands_Fx.cpp` | Type mapping |
| `frontend/src/components/FXChain.tsx` | Add type options |

---

## Feature 3: Punch In/Out (Loop Region)

### Current state

Recording starts immediately when `startRecording()` is called (after optional
count-in) and stops on `stopRecording()`. No concept of punch boundaries.
The loop region is already displayed on the ruler with draggable markers.

### Design

**Engine — `TransportManager.h`:**
- Add `std::atomic<bool> punchEnabled{false}`
- Add `setPunchEnabled(bool)`, `isPunchEnabled()`

**Engine — `TransportCommands.h`:**
- Add `virtual void setPunchEnabled(bool) = 0`
- Add `virtual bool isPunchEnabled() const = 0`

**Engine — `MainAudioProcessor.cpp`:**
- In `processBlock()`, when recording with punch enabled:
  - If position < loop start: don't call `audioRecorder->processBlock()` (play normally)
  - If position >= loop start AND position < loop end: record normally
  - If position >= loop end: auto-call `stopRecording()` (punch out)

**Engine — `TransportSnapshot` (ReadModel.h):**
- Add `punchEnabled` field

**Frontend — `TransportBar.tsx`:**
- Add punch toggle button (P)
- Visual indicator when punch is active

**Frontend — `types.ts`:**
- Add `punchEnabled` to `TransportSnapshot`

**MCP:** `transport.set_punch_enabled { enabled: bool }`

### Files

| File | Change |
|------|--------|
| `src/engine/TransportManager.h` | Add punch state |
| `src/common/TransportCommands.h` | Add punch commands |
| `src/engine/AudioEngineCommands_Transport.cpp` | Implement commands |
| `src/engine/MainAudioProcessor.cpp` | Punch-in/out logic in processBlock |
| `src/common/ReadModel.h` | Add punchEnabled to TransportSnapshot |
| `src/engine/ReadModelImpl.cpp` | Read punchEnabled |
| `src/frontend/FrontendRpc.h` | Serialize punchEnabled |
| `src/frontend/FrontendRouter.cpp` | Dispatch setPunchEnabled RPC |
| `frontend/src/rpc/types.ts` | Add punchEnabled |
| `frontend/src/components/TransportBar.tsx` | Punch toggle button |
| `src/mcp/McpTools_Transport.cpp` | MCP tool |

---

## Feature 4: Automation Modes (Read/Write/Touch/Latch)

### Current state

Automation is read-only during playback. `AutomationManager` stores points
and interpolates values at time. Per-lane `automationEnabled` flag controls
whether playback is active. No recording of automation from knob movements.

### Design

**Engine — `AutomationManager.h`:**
- Add `enum class Mode { Read, Write, Touch, Latch }`
- Add `Mode mode = Mode::Read` member
- Add `bool isTouching = false` flag (for Touch mode)
- Add `setMode(Mode m)`, `getMode()`, `setTouching(bool)`
- Add `recordPoint(double time, float value)` — appends point during write

**Engine — `Track.h` / `Track.cpp`:**
- In `processBlock()`, when automation mode is Write/Touch/Latch:
  - Check if currently writing (Write = always, Touch = when touching, Latch = after first touch)
  - If writing: read current parameter value and record as automation point
  - Use `timeSec` for point timestamp
  - Batch points (don't write every sample — write when value changes significantly or every ~10ms)

**Engine — `AudioEngineCommands_Automation.cpp`:**
- Add `setAutomationMode(int trackIndex, const std::string& laneName, const std::string& mode)`
- Add `notifyAutomationTouch(int trackIndex, int paramID, bool touching)`

**Engine — `AutomationLaneSnapshot` (ReadModel.h):**
- Add `std::string mode = "read"` field

**Frontend — `types.ts`:**
- Add `mode: string` to `AutomationLaneSnapshot`

**Frontend — `AutomationPanel.tsx`:**
- Add R/W/T/L mode selector buttons per lane
- Active mode highlighted with accent color

**Frontend — knob touch detection:**
- When user starts dragging a fader/knob during playback, call
  `project.notifyAutomationTouch(trackIndex, paramID, true)`
- When user releases, call `project.notifyAutomationTouch(trackIndex, paramID, false)`
- This enables Touch mode recording

**MCP:** `project.set_automation_mode { trackIndex, laneName, mode }`

### Files

| File | Change |
|------|--------|
| `src/engine/AutomationManager.h` | Add Mode enum, mode member, recordPoint |
| `src/engine/AutomationManager.cpp` | Implement mode logic |
| `src/engine/Track.h` | Add automation write path |
| `src/engine/Track.cpp` | Write automation in processBlock |
| `src/common/ReadModel.h` | Add mode to AutomationLaneSnapshot |
| `src/engine/ReadModelImpl.cpp` | Read mode field |
| `src/frontend/FrontendRpc.h` | Serialize mode field |
| `src/common/ProjectCommands.h` | Add setAutomationMode, notifyAutomationTouch |
| `src/engine/AudioEngineCommands.h` | Add overrides |
| `src/engine/AudioEngineCommands_Automation.cpp` | Implement commands |
| `src/frontend/FrontendRouter.cpp` | Dispatch RPCs |
| `frontend/src/rpc/types.ts` | Add mode to AutomationLaneSnapshot |
| `frontend/src/components/AutomationPanel.tsx` | Mode selector UI |
| `src/mcp/McpTools_Project.cpp` | MCP tool |

---

## Implementation Order

1. **Loudness metering** — smallest scope, no new RPCs, extends existing system
2. **Modulation FX** — follows existing FX pattern exactly, new DSP types
3. **Punch in/out** — transport extension, moderate scope
4. **Automation modes** — largest scope (new recording path + UI touch detection)
