# Quick-Win Features Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement 4 independent quick-win features: loudness metering, modulation FX, punch in/out, and automation modes.

**Architecture:** Each feature extends an existing system — no new subsystems. Implemented sequentially: loudness metering first (smallest), automation modes last (largest).

**Tech Stack:** C++20 / JUCE 8 (engine), React 19 / TypeScript / Zustand (frontend), WebSocket JSON-RPC (bridge)

**Spec:** `docs/superpowers/specs/2026-07-30-quick-win-features-design.md`

---

## Feature 1: Loudness Metering (RMS + LUFS Momentary)

### Task 1.1: Extend LevelMeter with RMS + LUFS

**Files:**
- Modify: `src/engine/LevelMeter.h`
- Modify: `src/common/ReadModel.h` (MeterSnapshot)
- Modify: `src/engine/ReadModelImpl.cpp` (getTrackMeter/getMasterMeter)
- Modify: `src/frontend/FrontendServer.cpp` (onMeterTimer)
- Modify: `src/frontend/FrontendRpc.h` (toJson MeterSnapshot)
- Modify: `frontend/src/rpc/types.ts` (MeterLevels)
- Modify: `frontend/src/store/meterStore.ts`
- Modify: `frontend/src/components/MixerStrip.tsx`
- Test: `tests/unit/engine/meter_test.cpp` (NEW)

- [ ] **Step 1: Write failing test for RMS**

Create `tests/unit/engine/meter_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/LevelMeter.h"
#include <juce_audio_basics/juce_audio_basics.h>

TEST(LevelMeter, ComputesRmsAlongsidePeak)
{
    HDAW::LevelMeter meter;
    meter.prepare(44100.0);

    // Create a buffer with a known sine wave at 0.5 amplitude
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    for (int s = 0; s < 512; ++s) {
        float val = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * s / 44100.0f);
        buffer.setSample(0, s, val);
        buffer.setSample(1, s, val);
    }

    meter.update(buffer);

    float peakL = meter.getPeakLeft();
    float rmsL = meter.getRmsLeft();

    // Peak should be ~0.5
    EXPECT_GT(peakL, 0.4f);
    EXPECT_LT(peakL, 0.6f);

    // RMS of a sine wave at amplitude A is A/sqrt(2) ≈ 0.354
    EXPECT_GT(rmsL, 0.25f);
    EXPECT_LT(rmsL, 0.45f);

    // RMS should be less than peak
    EXPECT_LT(rmsL, peakL);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="LevelMeter.*"`
Expected: FAIL — `getRmsLeft` not found

- [ ] **Step 3: Add RMS to LevelMeter.h**

In `src/engine/LevelMeter.h`, add alongside existing peak members:

```cpp
    std::atomic<float> rmsLeft{0.0f};
    std::atomic<float> rmsRight{0.0f};
    float rmsCoeff{0.0f};  // smoothing coefficient

    void prepare(double sampleRate) {
        // ... existing code ...
        // RMS smoothing: ~300ms time constant
        rmsCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.3));
    }

    void update(const juce::AudioBuffer<float>& buffer) {
        // ... existing peak code ...

        // RMS: exponential moving average per channel
        float sumL = 0.0f, sumR = 0.0f;
        for (int s = 0; s < buffer.getNumSamples(); ++s) {
            float l = buffer.getSample(0, s);
            sumL += l * l;
            if (buffer.getNumChannels() > 1) {
                float r = buffer.getSample(1, s);
                sumR += r * r;
            }
        }
        float rmsL_new = std::sqrt(sumL / buffer.getNumSamples());
        float rmsR_new = std::sqrt(sumR / buffer.getNumSamples());

        float prevL = rmsLeft.load(std::memory_order_relaxed);
        float prevR = rmsRight.load(std::memory_order_relaxed);
        rmsLeft.store(prevL + rmsCoeff * (rmsL_new - prevL), std::memory_order_relaxed);
        rmsRight.store(prevR + rmsCoeff * (rmsR_new - prevR), std::memory_order_relaxed);
    }

    float getRmsLeft() const { return rmsLeft.load(std::memory_order_relaxed); }
    float getRmsRight() const { return rmsRight.load(std::memory_order_relaxed); }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="LevelMeter.*"`
Expected: PASS

- [ ] **Step 5: Extend MeterSnapshot in ReadModel.h**

Add to `MeterSnapshot`:

```cpp
    float rmsLeftLevel = 0.0f;
    float rmsRightLevel = 0.0f;
    float lufsMomentary = -70.0f;  // silence default
```

- [ ] **Step 6: Update ReadModelImpl.cpp meter functions**

In `getTrackMeter()` and `getMasterMeter()`, add:

```cpp
    ms.rmsLeftLevel = meter.getRmsLeft();
    ms.rmsRightLevel = meter.getRmsRight();
    // LUFS placeholder — will add K-weighting in next step
```

- [ ] **Step 7: Add LUFS momentary to LevelMeter**

Add K-weighting filter coefficients and 400ms sliding window:

```cpp
    // K-weighting (ITU-R BS.1770) — two biquad stages
    struct KWeightFilter {
        float b0, b1, b2, a1, a2;
        float x1=0, x2=0, y1=0, y2=0;
        float process(float x) {
            float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2=x1; x1=x; y2=y1; y1=y;
            return y;
        }
    };
    KWeightFilter kwHighshelf, kwHighpass;
    bool kwInitialized = false;

    // Mean-square accumulator for 400ms window
    static constexpr int LUFS_WINDOW_MS = 400;
    std::vector<float> lufsBuffer;  // ring buffer of squared samples
    int lufsWriteIdx = 0;
    int lufsWindowSize = 0;
    double lufsSum = 0.0;

    void prepareLufs(double sampleRate) {
        lufsWindowSize = static_cast<int>(sampleRate * LUFS_WINDOW_MS / 1000.0);
        lufsBuffer.resize(lufsWindowSize, 0.0f);
        lufsWriteIdx = 0;
        lufsSum = 0.0;

        // K-weighting coefficients (48kHz — adjust if needed)
        // High-shelf: +4 dB at 1500 Hz
        kwHighshelf = {1.53512485958697f, -2.69169618940638f, 1.19839281085285f,
                       -1.69065929318241f, 0.73248077421585f};
        // High-pass: ITU-R BS.1770-4 pre-filter
        kwHighpass = {1.0f, -2.0f, 1.0f,
                      -1.99004745483398f, 0.99007225036621f};
        kwInitialized = true;
    }

    float computeLufs() {
        if (!kwInitialized || lufsWindowSize == 0) return -70.0f;
        double meanSquare = lufsSum / lufsWindowSize;
        if (meanSquare < 1e-20) return -70.0f;
        return static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare));
    }
```

In `update()`, after RMS, add LUFS processing:

```cpp
        // LUFS: K-weight + mean square over 400ms window
        if (kwInitialized) {
            for (int s = 0; s < buffer.getNumSamples(); ++s) {
                float l = kwHighshelf.process(buffer.getSample(0, s));
                l = kwHighpass.process(l);
                float sq = l * l;
                if (buffer.getNumChannels() > 1) {
                    float r = kwHighshelf.process(buffer.getSample(1, s));
                    r = kwHighpass.process(r);
                    sq = 0.5f * (sq + r * r);
                }
                // Ring buffer: subtract old, add new
                lufsSum -= lufsBuffer[lufsWriteIdx];
                lufsBuffer[lufsWriteIdx] = sq;
                lufsSum += sq;
                lufsWriteIdx = (lufsWriteIdx + 1) % lufsWindowSize;
            }
        }
```

Add `getLufsMomentary()` accessor:

```cpp
    float getLufsMomentary() { return computeLufs(); }
```

- [ ] **Step 8: Update FrontendRpc.h and FrontendServer.cpp**

In `FrontendRpc.h`, in `toJson(const MeterSnapshot& m)`:

```cpp
        { "rmsL",  static_cast<double>(m.rmsLeftLevel) },
        { "rmsR",  static_cast<double>(m.rmsRightLevel) },
        { "lufs",  static_cast<double>(m.lufsMomentary) },
```

In `FrontendServer.cpp` `onMeterTimer()`, include new fields in meter payload.

- [ ] **Step 9: Update frontend types and store**

In `frontend/src/rpc/types.ts`, add to `MeterLevels`:

```typescript
  rmsL: number;
  rmsR: number;
  lufs: number;
```

In `frontend/src/store/meterStore.ts`, include new fields in the update.

- [ ] **Step 10: Update MixerStrip.tsx**

Add RMS bar behind peak bar and LUFS readout:

```tsx
<div className="meter-track">
  <div className="meter-rms" style={{ height: `${Math.min(100, meter.rmsL * 100)}%` }} />
  <div className={cls} style={{ height: `${pct}%` }} />
</div>
<span className="meter-lufs">{meter.lufs > -70 ? meter.lufs.toFixed(1) : "---"}</span>
```

Add CSS for `.meter-rms` (thinner, semi-transparent) and `.meter-lufs` (small text).

- [ ] **Step 11: Build and run all tests**

Run: `cmake --build build --config Debug`
Run: `build\Debug\hdaw_tests.exe --gtest_filter="LevelMeter.*"`
Run: `cd frontend && npm test`
Expected: All pass

- [ ] **Step 12: Commit**

```bash
git add src/engine/LevelMeter.h src/common/ReadModel.h src/engine/ReadModelImpl.cpp \
  src/frontend/FrontendServer.cpp src/frontend/FrontendRpc.h \
  frontend/src/rpc/types.ts frontend/src/store/meterStore.ts \
  frontend/src/components/MixerStrip.tsx tests/unit/engine/meter_test.cpp
git commit -m "feat(metering): add RMS + LUFS momentary to level meters"
```

---

## Feature 2: Modulation FX (Chorus + Flanger + Phaser)

### Task 2.1: Add Chorus FX type

**Files:**
- Modify: `src/engine/TrackFXSlot.h` (ActiveType enum, DSP members, param defs)
- Modify: `src/engine/TrackFXSlot.cpp` (prepare/process)
- Modify: `src/engine/AudioEngineCommands_Fx.cpp` (type mapping)
- Modify: `frontend/src/components/FXChain.tsx` (type options)
- Test: `tests/unit/engine/modulation_fx_test.cpp` (NEW)

- [ ] **Step 1: Write failing test**

Create `tests/unit/engine/modulation_fx_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"

TEST(ModulationFx, AddChorusToTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "chorus");

    auto snap = engine.getReadModel().snapshot();
    auto lanes = engine.getReadModel().getFxSlots(0);
    ASSERT_GE(lanes.size(), 1u);
    EXPECT_EQ(lanes.back().fxType, "chorus");
}

TEST(ModulationFx, AddFlangerToTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "flanger");

    auto lanes = engine.getReadModel().getFxSlots(0);
    ASSERT_GE(lanes.size(), 1u);
    EXPECT_EQ(lanes.back().fxType, "flanger");
}

TEST(ModulationFx, AddPhaserToTrack)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "phaser");

    auto lanes = engine.getReadModel().getFxSlots(0);
    ASSERT_GE(lanes.size(), 1u);
    EXPECT_EQ(lanes.back().fxType, "phaser");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="ModulationFx.*"`
Expected: FAIL — "chorus" not recognized as FX type

- [ ] **Step 3: Add types to TrackFXSlot.h**

In `src/engine/TrackFXSlot.h`, add to `ActiveType` enum:

```cpp
    Chorus, Flanger, Phaser
```

Add DSP members:

```cpp
    std::unique_ptr<juce::dsp::Chorus<float>> chorusDsp;
    std::unique_ptr<juce::dsp::Phaser<float>> phaserDsp;
```

Add to `getParamDefsForType()`:

```cpp
    case Chorus:
        return {{"Rate", 1.5f, 0.1f, 5.0f},
                {"Depth", 0.25f, 0.0f, 1.0f},
                {"Centre Delay", 7.0f, 1.0f, 50.0f},
                {"Feedback", 0.0f, -1.0f, 1.0f},
                {"Mix", 0.5f, 0.0f, 1.0f}};
    case Flanger:
        return {{"Rate", 0.5f, 0.1f, 5.0f},
                {"Depth", 0.5f, 0.0f, 1.0f},
                {"Centre Delay", 3.0f, 1.0f, 5.0f},
                {"Feedback", 0.5f, -1.0f, 1.0f},
                {"Mix", 0.5f, 0.0f, 1.0f}};
    case Phaser:
        return {{"Rate", 0.5f, 0.1f, 5.0f},
                {"Depth", 0.5f, 0.0f, 1.0f},
                {"Centre Frequency", 1000.0f, 20.0f, 20000.0f},
                {"Feedback", 0.0f, -1.0f, 1.0f},
                {"Mix", 0.5f, 0.0f, 1.0f}};
```

- [ ] **Step 4: Implement prepare/process in TrackFXSlot.cpp**

In `prepare()`:

```cpp
    case Chorus:
    case Flanger:
        chorusDsp = std::make_unique<juce::dsp::Chorus<float>>();
        chorusDsp->prepare(spec);
        break;
    case Phaser:
        phaserDsp = std::make_unique<juce::dsp::Phaser<float>>();
        phaserDsp->prepare(spec);
        break;
```

In `process()`:

```cpp
    case Chorus:
    case Flanger:
        if (chorusDsp) chorusDsp->process(juce::dsp::ProcessContextReplacing<float>(block));
        break;
    case Phaser:
        if (phaserDsp) phaserDsp->process(juce::dsp::ProcessContextReplacing<float>(block));
        break;
```

In `applyInternalParamToDsp()`:

```cpp
    if (activeType == Chorus || activeType == Flanger) {
        if (chorusDsp) {
            if (name == "Rate") chorusDsp->setRate(value);
            else if (name == "Depth") chorusDsp->setDepth(value);
            else if (name == "Centre Delay") chorusDsp->setCentreDelay(value);
            else if (name == "Feedback") chorusDsp->setFeedback(value);
            else if (name == "Mix") chorusDsp->setMix(value);
        }
    }
    if (activeType == Phaser) {
        if (phaserDsp) {
            if (name == "Rate") phaserDsp->setRate(value);
            else if (name == "Depth") phaserDsp->setDepth(value);
            else if (name == "Centre Frequency") phaserDsp->setCentreFrequency(value);
            else if (name == "Feedback") phaserDsp->setFeedback(value);
            else if (name == "Mix") phaserDsp->setMix(value);
        }
    }
```

- [ ] **Step 5: Add type mapping in AudioEngineCommands_Fx.cpp**

In `addFxSlot(int trackIndex, const std::string& type, ...)`, add:

```cpp
    else if (type == "chorus")   actualType = 4;
    else if (type == "flanger")  actualType = 5;
    else if (type == "phaser")   actualType = 6;
```

In the int-based overload, add cases 4/5/6 mapping to Chorus/Flanger/Phaser.

- [ ] **Step 6: Add to frontend FX type selector**

In `frontend/src/components/FXChain.tsx`, add to the FX type dropdown:

```tsx
<option value="chorus">Chorus</option>
<option value="flanger">Flanger</option>
<option value="phaser">Phaser</option>
```

- [ ] **Step 7: Build and run tests**

Run: `cmake --build build --config Debug`
Run: `build\Debug\hdaw_tests.exe --gtest_filter="ModulationFx.*"`
Expected: All 3 tests PASS

- [ ] **Step 8: Commit**

```bash
git add src/engine/TrackFXSlot.h src/engine/TrackFXSlot.cpp \
  src/engine/AudioEngineCommands_Fx.cpp frontend/src/components/FXChain.tsx \
  tests/unit/engine/modulation_fx_test.cpp
git commit -m "feat(fx): add chorus, flanger, phaser modulation effects"
```

---

## Feature 3: Punch In/Out (Loop Region)

### Task 3.1: Add punch enabled state to transport

**Files:**
- Modify: `src/engine/TransportManager.h`
- Modify: `src/common/TransportCommands.h`
- Modify: `src/engine/AudioEngineCommands_Transport.cpp`
- Modify: `src/engine/MainAudioProcessor.cpp`
- Modify: `src/common/ReadModel.h`
- Modify: `src/engine/ReadModelImpl.cpp`
- Modify: `src/frontend/FrontendRpc.h`
- Modify: `src/frontend/FrontendRouter.cpp`
- Modify: `frontend/src/rpc/types.ts`
- Modify: `frontend/src/components/TransportBar.tsx`
- Test: `tests/unit/engine/punch_test.cpp` (NEW)

- [ ] **Step 1: Write failing test**

Create `tests/unit/engine/punch_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"

TEST(PunchInOut, PunchDisabledByDefault)
{
    AudioEngine engine;
    engine.initialize();

    auto snap = engine.getReadModel().snapshot().transport;
    EXPECT_FALSE(snap.punchEnabled);
}

TEST(PunchInOut, SetPunchEnabled)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setPunchEnabled(true);
    EXPECT_TRUE(engine.getTransportManager().isPunchEnabled());

    cmds.setPunchEnabled(false);
    EXPECT_FALSE(engine.getTransportManager().isPunchEnabled());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="PunchInOut.*"`
Expected: FAIL — `setPunchEnabled` not found

- [ ] **Step 3: Add punch state to TransportManager.h**

In `src/engine/TransportManager.h`, add:

```cpp
    std::atomic<bool> punchEnabled{false};

    void setPunchEnabled(bool enabled) { punchEnabled.store(enabled); }
    bool isPunchEnabled() const { return punchEnabled.load(); }
```

- [ ] **Step 4: Add to TransportCommands.h**

```cpp
    virtual void setPunchEnabled(bool enabled) = 0;
    virtual bool isPunchEnabled() const = 0;
```

- [ ] **Step 5: Implement in AudioEngineCommands_Transport.cpp**

```cpp
void AudioEngineCommands::setPunchEnabled(bool enabled)
{
    engine_.getTransportManager().setPunchEnabled(enabled);
}

bool AudioEngineCommands::isPunchEnabled() const
{
    return engine_.getTransportManager().isPunchEnabled();
}
```

- [ ] **Step 6: Add punch logic to MainAudioProcessor.cpp**

In `processBlock()`, modify the recording section. Before calling `audioRecorder->processBlock(buffer)`:

```cpp
    if (transportManager.isPunchEnabled() && transportManager.isLooping()) {
        int64_t current = transportManager.getCurrentSample();
        int64_t loopStart = transportManager.getLoopStartSample();
        int64_t loopEnd = transportManager.getLoopEndSample();

        if (current < loopStart || current >= loopEnd) {
            // Outside punch range — don't record audio
            // (but still let the audio graph play)
        } else {
            audioRecorder->processBlock(buffer);
        }
    } else {
        audioRecorder->processBlock(buffer);
    }
```

Also: when punch is enabled and transport reaches loop end while recording, auto-stop recording.

- [ ] **Step 7: Extend TransportSnapshot**

In `ReadModel.h`, add to `TransportSnapshot`:

```cpp
    bool punchEnabled = false;
```

Update `ReadModelImpl.cpp` to read it, `FrontendRpc.h` to serialize it.

- [ ] **Step 8: Add RPC dispatch**

In `FrontendRouter.cpp`:

```cpp
    if (m == "setPunchEnabled") { bool b; if (!requireBool(o, "enabled", b, nullptr)) return makeError(-32602, "enabled required"); c.setPunchEnabled(b); return { false, QJsonValue::Null }; }
```

- [ ] **Step 9: Update frontend**

In `types.ts`, add `punchEnabled` to `TransportSnapshot`.

In `TransportBar.tsx`, add punch toggle button:

```tsx
const punchEnabled = useTransportStore((s) => s.punchEnabled);
<button
  className={`tb-btn${punchEnabled ? " tb-btn--active" : ""}`}
  onClick={() => rpc.call("setPunchEnabled", { enabled: !punchEnabled })}
  title="Punch In/Out (uses loop region)"
>P</button>
```

- [ ] **Step 10: Build and run tests**

Run: `cmake --build build --config Debug`
Run: `build\Debug\hdaw_tests.exe --gtest_filter="PunchInOut.*"`
Expected: PASS

- [ ] **Step 11: Commit**

```bash
git add src/engine/TransportManager.h src/common/TransportCommands.h \
  src/engine/AudioEngineCommands_Transport.cpp src/engine/MainAudioProcessor.cpp \
  src/common/ReadModel.h src/engine/ReadModelImpl.cpp \
  src/frontend/FrontendRpc.h src/frontend/FrontendRouter.cpp \
  frontend/src/rpc/types.ts frontend/src/components/TransportBar.tsx \
  tests/unit/engine/punch_test.cpp
git commit -m "feat(transport): add punch in/out using loop region boundaries"
```

---

## Feature 4: Automation Modes (Read/Write/Touch/Latch)

### Task 4.1: Add automation mode to AutomationManager

**Files:**
- Modify: `src/engine/AutomationManager.h`
- Modify: `src/engine/AutomationManager.cpp`
- Modify: `src/engine/Track.cpp` (write path in processBlock)
- Modify: `src/common/ReadModel.h`
- Modify: `src/engine/ReadModelImpl.cpp`
- Modify: `src/frontend/FrontendRpc.h`
- Modify: `src/common/ProjectCommands.h`
- Modify: `src/engine/AudioEngineCommands.h`
- Modify: `src/engine/AudioEngineCommands_Automation.cpp`
- Modify: `src/frontend/FrontendRouter.cpp`
- Modify: `frontend/src/rpc/types.ts`
- Modify: `frontend/src/components/AutomationPanel.tsx`
- Test: `tests/unit/engine/automation_mode_test.cpp` (NEW)

- [ ] **Step 1: Write failing test**

Create `tests/unit/engine/automation_mode_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"

TEST(AutomationMode, DefaultModeIsRead)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "Volume");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    ASSERT_GE(lanes.size(), 1u);
    // Find the Volume lane
    bool found = false;
    for (const auto& l : lanes) {
        if (l.name == "Volume") {
            EXPECT_EQ(l.mode, "read");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(AutomationMode, SetAutomationMode)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addAutomationLane(0, "Volume");
    cmds.setAutomationMode(0, "Volume", "write");

    auto lanes = engine.getReadModel().getAutomationLanes(0);
    for (const auto& l : lanes) {
        if (l.name == "Volume") {
            EXPECT_EQ(l.mode, "write");
        }
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="AutomationMode.*"`
Expected: FAIL — `setAutomationMode` not found

- [ ] **Step 3: Add Mode enum to AutomationManager.h**

```cpp
    enum class Mode { Read, Write, Touch, Latch };
    Mode mode = Mode::Read;
    bool isTouching = false;
    bool hasTouched = false;  // for Latch: latches after first touch

    void setMode(Mode m) { mode = m; isTouching = false; hasTouched = false; }
    Mode getMode() const { return mode; }
    void setTouching(bool t) {
        isTouching = t;
        if (t) hasTouched = true;
    }

    bool shouldWrite() const {
        switch (mode) {
            case Mode::Write: return true;
            case Mode::Touch: return isTouching;
            case Mode::Latch: return hasTouched;
            case Mode::Read:
            default: return false;
        }
    }
```

- [ ] **Step 4: Add write path in Track.cpp**

In `Track::processBlock()`, in the automation loop, after reading the automation value:

```cpp
    // Automation write path
    if (am.shouldWrite() && transport.isPlaying()) {
        double timeSec = transport.getCurrentTimeSeconds();
        float currentVal = getCurrentParameterValue(am.getParamID());
        am.recordPoint(timeSec, currentVal);
    }
```

Add `recordPoint()` to `AutomationManager`:

```cpp
    void recordPoint(double time, float value) {
        // Append point (deduplicate near-identical times)
        auto& pts = points;
        if (!pts.empty() && std::abs(pts.back().time - time) < 0.01) {
            pts.back().value = value;  // update existing
        } else {
            pts.push_back({time, value});
        }
    }
```

- [ ] **Step 5: Add mode to AutomationLaneSnapshot**

In `ReadModel.h`:

```cpp
    std::string mode = "read";
```

Update `ReadModelImpl.cpp` to read mode from ValueTree.

- [ ] **Step 6: Add commands**

In `ProjectCommands.h`:

```cpp
    virtual void setAutomationMode(int trackIndex, const std::string& laneName, const std::string& mode) = 0;
    virtual void notifyAutomationTouch(int trackIndex, int paramID, bool touching) = 0;
```

Implement in `AudioEngineCommands_Automation.cpp`:

```cpp
void AudioEngineCommands::setAutomationMode(int trackIndex, const std::string& laneName, const std::string& mode)
{
    // Find the automation lane and set mode
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    auto track = trackList.getChild(trackIndex);
    auto autoList = track.getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid()) return;
    for (int i = 0; i < autoList.getNumChildren(); ++i) {
        auto lane = autoList.getChild(i);
        if (lane.getProperty(IDs::name, "").toString().toStdString() == laneName) {
            lane.setProperty(IDs::automationMode, juce::String(mode), &um);
            return;
        }
    }
}

void AudioEngineCommands::notifyAutomationTouch(int trackIndex, int paramID, bool touching)
{
    // Find the automation manager for this param and set touching flag
    // This is called from the frontend when user starts/stops dragging a knob
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    // The Track object manages AutomationManagers — need to forward to it
    // For now, store in a SPSC queue or direct call if on message thread
}
```

- [ ] **Step 7: Add ValueTree ID**

In `ProjectModel.h`:

```cpp
    DECLARE_ID(automationMode)
```

- [ ] **Step 8: Add RPC dispatches**

In `FrontendRouter.cpp`:

```cpp
    if (m == "setAutomationMode") { int ti; std::string ln, md; if (!requireInt(o, "trackIndex", ti, nullptr) || !requireString(o, "laneName", ln, nullptr) || !requireString(o, "mode", md, nullptr)) return makeError(-32602, "trackIndex, laneName, mode required"); c.setAutomationMode(ti, ln, md); return { false, QJsonValue::Null }; }
    if (m == "notifyAutomationTouch") { int ti, pid; bool t; if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "paramID", pid, nullptr) || !requireBool(o, "touching", t, nullptr)) return makeError(-32602, "trackIndex, paramID, touching required"); c.notifyAutomationTouch(ti, pid, t); return { false, QJsonValue::Null }; }
```

- [ ] **Step 9: Update frontend**

In `types.ts`, add `mode` to `AutomationLaneSnapshot`:

```typescript
  mode?: string;  // "read" | "write" | "touch" | "latch"
```

In `AutomationPanel.tsx`, add mode selector buttons per lane:

```tsx
<div className="ap-mode-btns">
  {(["read", "write", "touch", "latch"] as const).map(m => (
    <button
      key={m}
      className={`ap-mode-btn${lane.mode === m ? " ap-mode-btn--active" : ""}`}
      onClick={() => rpc.call("setAutomationMode", { trackIndex, laneName: lane.name, mode: m })}
    >
      {m[0].toUpperCase()}
    </button>
  ))}
</div>
```

- [ ] **Step 10: Build and run tests**

Run: `cmake --build build --config Debug`
Run: `build\Debug\hdaw_tests.exe --gtest_filter="AutomationMode.*"`
Expected: PASS

- [ ] **Step 11: Commit**

```bash
git add src/engine/AutomationManager.h src/engine/AutomationManager.cpp \
  src/engine/Track.cpp src/common/ReadModel.h src/engine/ReadModelImpl.cpp \
  src/frontend/FrontendRpc.h src/common/ProjectCommands.h \
  src/engine/AudioEngineCommands.h src/engine/AudioEngineCommands_Automation.cpp \
  src/frontend/FrontendRouter.cpp src/model/ProjectModel.h \
  frontend/src/rpc/types.ts frontend/src/components/AutomationPanel.tsx \
  tests/unit/engine/automation_mode_test.cpp
git commit -m "feat(automation): add Read/Write/Touch/Latch modes with knob touch detection"
```

---

## Verification Checklist

After all features:

- [ ] `cmake --build build --config Debug` — builds cleanly
- [ ] `build\Debug\hdaw_tests.exe` — all tests pass
- [ ] `cd frontend && npm test` — all frontend tests pass
- [ ] `cd frontend && npm run build` — TypeScript compiles
- [ ] Manual: verify LUFS readout in mixer strips
- [ ] Manual: add chorus/flanger/phaser to FX chain, verify audio processing
- [ ] Manual: enable punch, record with loop region, verify recording starts/stops at boundaries
- [ ] Manual: set automation to Write mode, play back, verify points are recorded
