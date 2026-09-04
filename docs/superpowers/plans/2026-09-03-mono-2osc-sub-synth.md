# Monophonic 2Osc/Sub Synth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **MANDATORY:** invoke the `hdaw-guard` skill before any code change.

**Goal:** Ship a minimal monophonic 2osc + sub subtractive synth as a new internal FX slot in HDAW.

**Architecture:** Build one realtime-safe voice engine with two main oscillators, a fixed sub oscillator, one filter, and one amp envelope. Wire it into the existing internal-FX path as a new `sub_synth` slot type so it behaves like `fm_synth` and `sampler` do today: state lives in the slot ValueTree, `TrackFXSlot` restores it on rebuild, and the audio thread only reads atomics and prebuilt DSP state. Use the found projects as references for shape and control layout only: `Synple` and `Fiore` for dual-osc subtractive structure, `Generator-Synth` for the simple JUCE processor flow, and the permissive MIT/BSD synths with sub oscillators for parameter ideas.

**Tech Stack:** C++17 + JUCE 8, gtest, Qt JSON MCP, React 19 + TypeScript + Zustand.

---

## File Structure

### Create
| File | Responsibility |
|------|---------------|
| `src/engine/SubtractiveSynthEngine.h` | Engine API, parameter atomics, note state, render entry point |
| `src/engine/SubtractiveSynthEngine.cpp` | Oscillator mix, sub oscillator, filter, amp envelope, MIDI handling |
| `tests/unit/engine/subtractive_synth_test.cpp` | Core engine unit tests for note on/off, audio output, and parameter clamping |

### Modify
| File | Change |
|------|--------|
| `src/engine/TrackFXSlot.h` | Add `ActiveType::SubSynth`, `std::unique_ptr<SubtractiveSynthEngine> subSynth`, and `getParamDefsForType("sub_synth")` |
| `src/engine/TrackFXSlot.cpp` | Add `sub_synth` branches in `prepare()`, `process()`, `reset()`, `loadParamsFromTree()`, and `setInternalParam()` |
| `src/engine/AudioEngineCommands_Fx.cpp` | Allow `addFxSlot(..., "sub_synth", ...)` from project commands |
| `src/mcp/McpTools_FxSlot.cpp` | Add `sub_synth` to the `add_fx` enum and internal-FX validation text |
| `frontend/src/components/FXChain.tsx` | Add `Sub Synth` to the internal FX menu |
| `frontend/src/components/FXChain.test.tsx` | Assert the menu renders `Sub Synth` and clicks map to `fxType: "sub_synth"` |
| `tests/unit/engine/track_fx_rebuild_race_test.cpp` | Add a rebuild/state-restore regression for `sub_synth` |
| `tests/CMakeLists.txt` | Register the new engine test file |
| `CMakeLists.txt` | Add the new engine source file |

---

## Success Gates

- [ ] `cmake --build build --config Debug` succeeds.
- [ ] `build/Debug/hdaw_tests.exe --gtest_filter=SubtractiveSynth*` passes.
- [ ] `build/Debug/hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.*` passes.
- [ ] `cd frontend; npm test -- FXChain` passes.
- [ ] `cd frontend; npm run build` succeeds.
- [ ] `project.addFxSlot` accepts `sub_synth`, and `list_fx_params` / `set_internal_fx_param` work on a `sub_synth` slot.

---

## Task 1: Engine Core

**Files:**
- Create: `src/engine/SubtractiveSynthEngine.h`
- Create: `src/engine/SubtractiveSynthEngine.cpp`
- Create: `tests/unit/engine/subtractive_synth_test.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/unit/engine/subtractive_synth_test.cpp
#include <gtest/gtest.h>
#include <cmath>
#include "engine/SubtractiveSynthEngine.h"

TEST(SubtractiveSynthEngine, NoteOnProducesOutput)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);

    juce::AudioBuffer<float> buffer(1, 256);
    buffer.clear();
    engine.render(buffer, midi);

    float peak = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        peak = std::max(peak, std::abs(buffer.getSample(0, i)));
    EXPECT_GT(peak, 0.0f);
}

TEST(SubtractiveSynthEngine, NoteOffClearsVoice)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    juce::AudioBuffer<float> buffer(1, 128);
    buffer.clear();
    engine.render(buffer, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    for (int i = 0; i < 64; ++i)
    {
        buffer.clear();
        engine.render(buffer, midi);
        midi.clear();
    }

    EXPECT_EQ(engine.activeNoteCount(), 0);
}

TEST(SubtractiveSynthEngine, ParameterSettersClampAndDoNotCrash)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setOsc1Wave(9);
    engine.setOsc2Wave(-4);
    engine.setOsc2DetuneCents(9999.0f);
    engine.setSubLevel(2.0f);
    engine.setCutoffHz(999999.0f);
    engine.setResonance(99.0f);
    engine.setDrive(4.0f);
    engine.setAttackSeconds(-1.0f);
    engine.setReleaseSeconds(-5.0f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buffer(1, 64);
    buffer.clear();
    EXPECT_NO_THROW(engine.render(buffer, midi));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=SubtractiveSynth*`

Expected: compile failure first, then red tests until the engine exists.

- [ ] **Step 3: Write the minimal implementation**

```cpp
// src/engine/SubtractiveSynthEngine.h
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

class SubtractiveSynthEngine
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setOsc1Wave(int value) noexcept;
    void setOsc2Wave(int value) noexcept;
    void setOsc1Level(float value) noexcept;
    void setOsc2Level(float value) noexcept;
    void setOsc2DetuneCents(float value) noexcept;
    void setSubLevel(float value) noexcept;
    void setSubOctave(int value) noexcept;
    void setCutoffHz(float value) noexcept;
    void setResonance(float value) noexcept;
    void setDrive(float value) noexcept;
    void setAttackSeconds(float value) noexcept;
    void setDecaySeconds(float value) noexcept;
    void setSustain(float value) noexcept;
    void setReleaseSeconds(float value) noexcept;
    void setOutputLevel(float value) noexcept;

    int activeNoteCount() const noexcept;

private:
    enum class Wave { Sine = 0, Saw = 1, Square = 2, Triangle = 3 };
    struct VoiceState { bool active = false; int note = -1; float phase1 = 0.0f; float phase2 = 0.0f; float phaseSub = 0.0f; juce::ADSR adsr; };

    void startVoice(int note, float velocity) noexcept;
    void stopVoice(int note) noexcept;
    float nextSample(VoiceState& voice) noexcept;
    float oscSample(Wave wave, float phase) noexcept;

    double sampleRate_ = 44100.0;
    std::array<VoiceState, 1> voices_;
    juce::dsp::StateVariableTPTFilter<float> filter_;
    juce::ADSR::Parameters adsrParams_;
    std::atomic<int> osc1Wave_ { 0 }, osc2Wave_ { 0 }, subOctave_ { -1 };
    std::atomic<float> osc1Level_ { 0.5f }, osc2Level_ { 0.5f }, osc2DetuneCents_ { 8.0f }, subLevel_ { 0.5f };
    std::atomic<float> cutoffHz_ { 1200.0f }, resonance_ { 0.7f }, drive_ { 0.0f }, outputLevel_ { 0.8f };
    std::atomic<float> attackSeconds_ { 0.01f }, decaySeconds_ { 0.2f }, sustain_ { 0.7f }, releaseSeconds_ { 0.2f };
};
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --config Debug`

Then: `build\Debug\hdaw_tests.exe --gtest_filter=SubtractiveSynth*`

Expected: pass with non-zero output, note-off release, and safe parameter clamping.

- [ ] **Step 5: Commit**

```bash
git add src/engine/SubtractiveSynthEngine.h src/engine/SubtractiveSynthEngine.cpp tests/unit/engine/subtractive_synth_test.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add monophonic 2osc sub synth engine"
```

---

## Task 2: Track Slot Wiring

**Files:**
- Modify: `src/engine/TrackFXSlot.h`
- Modify: `src/engine/TrackFXSlot.cpp`
- Modify: `src/engine/AudioEngineCommands_Fx.cpp`
- Modify: `tests/unit/engine/track_fx_rebuild_race_test.cpp`

- [ ] **Step 1: Write the failing integration test**

```cpp
TEST(TrackFxRebuildRace, SubSynthSlotSurvivesRebuildAndRenders)
{
    AudioEngine engine;
    engine.initialize();

    auto& cmds = engine.getProjectCommands();
    cmds.addFxSlot(0, "sub_synth", 0, "");

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);

    cmds.setFxSlotParam(0, 0, 0, 2.0f);   // osc1 wave clamp
    cmds.setFxSlotParam(0, 0, 9, 1200.0f); // cutoff

    track->rebuildFXChain();

    auto fxSlots = engine.getReadModel().getFxSlots(0);
    ASSERT_FALSE(fxSlots.empty());
    EXPECT_EQ(fxSlots[0].fxType, "sub_synth");

    auto params = engine.getReadModel().getInternalFxParams(0, 0);
    ASSERT_FALSE(params.empty());
    EXPECT_EQ(params[7].value, 1200.0f);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynth*`

Expected: slot type missing, wrong param defs, or no engine branch.

- [ ] **Step 3: Add the slot type and restore path**

```cpp
// src/engine/TrackFXSlot.h
if (type == "sub_synth")
    return {
        { 0, "Osc1 Wave", 0.0f, 0.0f, 3.0f },
        { 1, "Osc1 Level", 0.5f, 0.0f, 1.0f },
        { 2, "Osc2 Wave", 1.0f, 0.0f, 3.0f },
        { 3, "Osc2 Level", 0.5f, 0.0f, 1.0f },
        { 4, "Osc2 Detune", 8.0f, 0.0f, 30.0f },
        { 5, "Sub Level", 0.5f, 0.0f, 1.0f },
        { 6, "Sub Octave", -1.0f, -2.0f, -1.0f },
        { 7, "Cutoff", 1200.0f, 20.0f, 20000.0f },
        { 8, "Resonance", 0.7f, 0.1f, 10.0f },
        { 9, "Drive", 0.0f, 0.0f, 1.0f },
        { 10, "Attack", 0.01f, 0.001f, 5.0f },
        { 11, "Decay", 0.2f, 0.001f, 5.0f },
        { 12, "Sustain", 0.7f, 0.0f, 1.0f },
        { 13, "Release", 0.2f, 0.001f, 5.0f },
        { 14, "Output Level", 0.8f, 0.0f, 1.0f },
    };
```

```cpp
// src/engine/TrackFXSlot.cpp
if (activeType == ActiveType::SubSynth && subSynth)
{
    subSynth->prepare(sampleRate, maxBlockSize);
    loadParamsFromTree(slotTree);
}
```

```cpp
// src/engine/AudioEngineCommands_Fx.cpp
if (type == "sub_synth")
{
    slot.setProperty(IDs::fxType, juce::String("sub_synth"), &um);
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynth*`

Expected: slot survives rebuild, params stay attached, and the live track renders.

- [ ] **Step 5: Commit**

```bash
git add src/engine/TrackFXSlot.h src/engine/TrackFXSlot.cpp src/engine/AudioEngineCommands_Fx.cpp tests/unit/engine/track_fx_rebuild_race_test.cpp
git commit -m "feat: wire sub synth into track fx chain"
```

---

## Task 3: MCP and Frontend Exposure

**Files:**
- Modify: `src/mcp/McpTools_FxSlot.cpp`
- Modify: `frontend/src/components/FXChain.tsx`
- Modify: `frontend/src/components/FXChain.test.tsx`

- [ ] **Step 1: Write the failing frontend test**

```tsx
it("shows Sub Synth in the internal FX menu", async () => {
  useUiStore.setState({ selectedTrackIndex: 0 });
  mockedCall.mockResolvedValue([]);
  const user = userEvent.setup();
  render(<FXChain />);
  await flushRead();

  await user.click(screen.getByText("+ Add FX"));
  expect(screen.getByText("Sub Synth")).toBeInTheDocument();
  await user.click(screen.getByText("Sub Synth"));

  expect(mockedCall).toHaveBeenCalledWith("project.addFxSlot", expect.objectContaining({
    trackIndex: 0,
    fxType: "sub_synth",
  }));
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd frontend; npm test -- FXChain`

Expected: missing menu item or wrong `fxType`.

- [ ] **Step 3: Add the menu and MCP enum entry**

```ts
// frontend/src/components/FXChain.tsx
const INTERNAL_FX = [
  { label: "EQ", fxType: "eq" },
  { label: "Compressor", fxType: "compressor" },
  { label: "Reverb", fxType: "reverb" },
  { label: "Delay", fxType: "delay" },
  { label: "Chorus", fxType: "chorus" },
  { label: "Flanger", fxType: "flanger" },
  { label: "Phaser", fxType: "phaser" },
  { label: "Filter", fxType: "filter" },
  { label: "Saturator", fxType: "saturator" },
  { label: "Sampler", fxType: "sampler" },
  { label: "FM Synth", fxType: "fm_synth" },
  { label: "Growl Bass", fxType: "growl_bass" },
  { label: "PsyArp", fxType: "psyarp" },
  { label: "PsyFm", fxType: "psy_fm" },
  { label: "Sub Synth", fxType: "sub_synth" },
];
```

```cpp
// src/mcp/McpTools_FxSlot.cpp
{"fxType", QJsonObject{{"type","string"},
  {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger","phaser","filter","saturator","sampler","fm_synth","growl_bass","psyarp","psy_fm","sub_synth"}}}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd frontend; npm test -- FXChain`

Expected: `Sub Synth` appears in the internal menu and maps to `project.addFxSlot`.

- [ ] **Step 5: Commit**

```bash
git add src/mcp/McpTools_FxSlot.cpp frontend/src/components/FXChain.tsx frontend/src/components/FXChain.test.tsx
git commit -m "feat: expose sub synth in mcp and fx chain ui"
```

---

## Task 4: End-to-End Verification

**Files:**
- Modify: `tests/unit/engine/track_fx_rebuild_race_test.cpp`
- Modify: `frontend/src/components/FXChain.test.tsx`

- [ ] **Step 1: Add a project-level smoke check**

```cpp
TEST(SubtractiveSynthIntegration, SaveLoadPreservesSlotTypeAndParams)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "sub_synth", 0, "");
    cmds.setFxSlotParam(0, 0, 0, 2.0f);
    cmds.setFxSlotParam(0, 0, 7, 1800.0f);

    juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("sub_synth_roundtrip.hdaw");
    ASSERT_TRUE(cmds.saveProject(tmp.getFullPathName().toStdString()));
    ASSERT_TRUE(cmds.loadProject(tmp.getFullPathName().toStdString()));

    auto fxSlots = engine.getReadModel().getFxSlots(0);
    ASSERT_FALSE(fxSlots.empty());
    EXPECT_EQ(fxSlots[0].fxType, "sub_synth");
    auto params = engine.getReadModel().getInternalFxParams(0, 0);
    ASSERT_FALSE(params.empty());
    EXPECT_EQ(params[0].value, 2.0f);
    EXPECT_EQ(params[7].value, 1800.0f);
}
```

- [ ] **Step 2: Run the smoke checks**

Run:
`build\Debug\hdaw_tests.exe --gtest_filter=SubtractiveSynthIntegration.*`
`cd frontend; npm test -- FXChain`
`cd frontend; npm run build`

Expected: slot type survives save/load, UI still exposes the slot, and the frontend build stays green.

- [ ] **Step 3: Final validation pass**

Run:
`build\Debug\hdaw_tests.exe --gtest_filter=SubtractiveSynth*`
`build\Debug\hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.*`
`cd frontend; npm test`

Expected: all synth-specific and FX-chain tests pass together.
