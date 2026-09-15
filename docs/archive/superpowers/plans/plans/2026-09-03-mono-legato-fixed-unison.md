# Mono/Legato + Fixed Unison Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **MANDATORY:** invoke the `hdaw-guard` skill before any code change.

**Goal:** Extend the existing `sub_synth` internal FX with mono/legato note handling, portamento glide, and a fixed 2-voice unison stack.

**Architecture:** Keep the feature inside `SubtractiveSynthEngine` and the existing internal-FX restore path. The engine stays monophonic at the note-allocation level, but each rendered note is duplicated into a small fixed detune stack so the sound thickens without adding a user-facing unison control. `TrackFXSlot` exposes only the controllable musical parameters that matter here: a legato toggle and portamento time. The existing internal-parameter UI path in `FXChain` will pick those up automatically, so no frontend component work is needed.

**Tech Stack:** C++17 + JUCE 8, gtest, existing HDAW internal-FX/ValueTree path.

---

## File Structure

### Create
| File | Responsibility |
|------|---------------|
| `docs/superpowers/plans/2026-09-03-mono-legato-fixed-unison.md` | This implementation plan |

### Modify
| File | Change |
|------|--------|
| `src/engine/SubtractiveSynthEngine.h` | Add legato/portamento setters, note-state fields, and test-only frequency accessors |
| `src/engine/SubtractiveSynthEngine.cpp` | Implement mono/legato note allocation, glide, and fixed two-voice detune rendering |
| `src/engine/TrackFXSlot.h` | Add `Legato` and `Portamento` param defs for `sub_synth` and wire them to the engine |
| `src/engine/TrackFXSlot.cpp` | Restore legato/portamento from the slot tree on rebuild |
| `tests/unit/engine/subtractive_synth_test.cpp` | Core engine tests for mono retrigger, legato retrigger, and glide |
| `tests/unit/engine/track_fx_rebuild_race_test.cpp` | Rebuild/state-restore regression for the new params |
| `CMakeLists.txt` | Ensure any new/updated engine source is listed |
| `tests/CMakeLists.txt` | Ensure updated test file is listed |

---

## Success Gates

- [ ] `cmake --build build --config Debug` succeeds.
- [ ] `build/Debug/hdaw_tests.exe --gtest_filter=SubtractiveSynth*` passes.
- [ ] `build/Debug/hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynth*` passes.
- [ ] `sub_synth` still appears in `list_fx_params`, with `Legato` and `Portamento` present and restored after rebuild.

---

## Task 1: Engine Note Modes and Glide

**Files:**
- Modify: `src/engine/SubtractiveSynthEngine.h`
- Modify: `src/engine/SubtractiveSynthEngine.cpp`
- Test: `tests/unit/engine/subtractive_synth_test.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/unit/engine/subtractive_synth_test.cpp
#include <gtest/gtest.h>
#include <cmath>
#include "engine/SubtractiveSynthEngine.h"

TEST(SubtractiveSynthEngine, MonoRetriggerResetsCurrentVoice)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setLegatoEnabled(false);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    juce::AudioBuffer<float> buffer(1, 64);
    buffer.clear();
    engine.render(buffer, midi);

    const float before = engine.currentFrequencyForTest();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.9f), 0);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_NEAR(engine.currentFrequencyForTest(), 523.25f, 1.0f);
    EXPECT_NE(before, engine.currentFrequencyForTest());
}

TEST(SubtractiveSynthEngine, LegatoRetriggerKeepsEnvelopeAlive)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setLegatoEnabled(true);
    engine.setPortamentoSeconds(0.05f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    juce::AudioBuffer<float> buffer(1, 64);
    buffer.clear();
    engine.render(buffer, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.9f), 0);
    buffer.clear();
    engine.render(buffer, midi);

    EXPECT_GT(engine.currentFrequencyForTest(), 0.0f);
    EXPECT_LT(engine.currentFrequencyForTest(), 523.25f);
}

TEST(SubtractiveSynthEngine, PortamentoMovesFrequencyTowardTarget)
{
    SubtractiveSynthEngine engine;
    engine.prepare(44100.0, 512);
    engine.setLegatoEnabled(true);
    engine.setPortamentoSeconds(0.10f);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
    juce::AudioBuffer<float> buffer(1, 32);
    buffer.clear();
    engine.render(buffer, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    buffer.clear();
    engine.render(buffer, midi);

    const float stepped = engine.currentFrequencyForTest();
    EXPECT_GT(stepped, 130.8f);
    EXPECT_LT(stepped, 261.7f);
    EXPECT_NEAR(engine.targetFrequencyForTest(), 261.63f, 1.0f);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=SubtractiveSynth*`

Expected: compile or assertion failures because `setLegatoEnabled`, `setPortamentoSeconds`, and the test accessors do not exist yet.

- [ ] **Step 3: Implement the minimal engine changes**

```cpp
// src/engine/SubtractiveSynthEngine.h
void setLegatoEnabled(bool enabled) noexcept;
void setPortamentoSeconds(float value) noexcept;
float currentFrequencyForTest() const noexcept;
float targetFrequencyForTest() const noexcept;

std::atomic<bool> legatoEnabled_ { false };
std::atomic<float> portamentoSeconds_ { 0.0f };
float currentFrequency_ = 0.0f;
float targetFrequency_ = 0.0f;
float glideIncrement_ = 0.0f;
static constexpr float kFixedUnisonDetuneCents = 6.0f;
```

```cpp
// src/engine/SubtractiveSynthEngine.cpp
void SubtractiveSynthEngine::setLegatoEnabled(bool enabled) noexcept
{
    legatoEnabled_.store(enabled, std::memory_order_relaxed);
}

void SubtractiveSynthEngine::setPortamentoSeconds(float value) noexcept
{
    portamentoSeconds_.store(std::max(0.0f, value), std::memory_order_relaxed);
}
```

Implement the note path so:
- mono retrigger resets the voice and envelope
- legato retrigger keeps the envelope running and only retargets pitch
- glide walks `currentFrequency_` toward `targetFrequency_` over successive samples
- each rendered note is duplicated into two fixed detuned lanes and summed before the filter

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --config Debug`

Then: `build\Debug\hdaw_tests.exe --gtest_filter=SubtractiveSynth*`

Expected: the mono/legato and glide tests pass, and the output remains non-zero with the fixed unison stack enabled.

- [ ] **Step 5: Commit**

```bash
git add src/engine/SubtractiveSynthEngine.h src/engine/SubtractiveSynthEngine.cpp tests/unit/engine/subtractive_synth_test.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add mono legato and glide to sub synth"
```

---

## Task 2: Slot Param Restore

**Files:**
- Modify: `src/engine/TrackFXSlot.h`
- Modify: `src/engine/TrackFXSlot.cpp`
- Modify: `tests/unit/engine/track_fx_rebuild_race_test.cpp`

- [ ] **Step 1: Write the failing integration test**

```cpp
TEST(TrackFxRebuildRace, SubSynthLegatoAndPortamentoRestore)
{
    AudioEngine engine;
    engine.initialize();

    auto& cmds = engine.getProjectCommands();
    cmds.addFxSlot(0, "sub_synth", 0, "");
    cmds.setFxSlotParam(0, 0, 15, 1.0f); // Legato = on
    cmds.setFxSlotParam(0, 0, 16, 0.12f); // Portamento = 120 ms if normalized-to-real mapping is used

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    track->rebuildFXChain();

    auto fxSlots = engine.getReadModel().getFxSlots(0);
    ASSERT_FALSE(fxSlots.empty());
    EXPECT_EQ(fxSlots[0].fxType, "sub_synth");

    auto params = engine.getReadModel().getInternalFxParams(0, 0);
    ASSERT_GE(params.size(), 17u);
    EXPECT_EQ(params[15].value, 1.0f);
    EXPECT_NEAR(params[16].value, 0.12f, 1e-6f);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynthLegatoAndPortamentoRestore`

Expected: slot params are missing until the engine restore path is updated.

- [ ] **Step 3: Add the slot parameters and restore path**

```cpp
// src/engine/TrackFXSlot.h
if (type == "sub_synth")
    return {
        { 0, "Osc1 Wave", 0.0f, 0.0f, 3.0f },
        { 1, "Osc1 Level", 0.6f, 0.0f, 1.0f },
        { 2, "Osc2 Wave", 1.0f, 0.0f, 3.0f },
        { 3, "Osc2 Level", 0.4f, 0.0f, 1.0f },
        { 4, "Osc2 Detune", 8.0f, 0.0f, 30.0f },
        { 5, "Sub Level", 0.35f, 0.0f, 1.0f },
        { 6, "Sub Octave", -1.0f, -2.0f, -1.0f },
        { 7, "Cutoff", 1800.0f, 20.0f, 20000.0f },
        { 8, "Resonance", 0.15f, 0.0f, 0.99f },
        { 9, "Drive", 0.0f, 0.0f, 1.0f },
        { 10, "Attack", 0.01f, 0.001f, 5.0f },
        { 11, "Decay", 0.18f, 0.001f, 5.0f },
        { 12, "Sustain", 0.65f, 0.0f, 1.0f },
        { 13, "Release", 0.18f, 0.001f, 5.0f },
        { 14, "Output Level", 0.8f, 0.0f, 1.0f },
        { 15, "Legato", 0.0f, 0.0f, 1.0f },
        { 16, "Portamento", 0.0f, 0.0f, 1.0f },
    };
```

Wire the new params in `setInternalParam()` and `loadParamsFromTree()` so they call the new engine setters.

- [ ] **Step 4: Run the test to verify it passes**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynthLegatoAndPortamentoRestore`

Expected: legato and portamento survive `rebuildFXChain()` and are visible in the read model.

- [ ] **Step 5: Commit**

```bash
git add src/engine/TrackFXSlot.h src/engine/TrackFXSlot.cpp tests/unit/engine/track_fx_rebuild_race_test.cpp
git commit -m "feat: restore sub synth legato and portamento params"
```

---

## Task 3: Final Verification

**Files:**
- None

- [ ] **Step 1: Run the targeted synth tests**

Run:
`build\Debug\hdaw_tests.exe --gtest_filter=SubtractiveSynth*`
`build\Debug\hdaw_tests.exe --gtest_filter=TrackFxRebuildRace.SubSynth*`

Expected: both targeted suites pass.

- [ ] **Step 2: Run a full debug build**

Run: `cmake --build build --config Debug`

Expected: build succeeds with the freshly compiled binary.

- [ ] **Step 3: Confirm no extra frontend or MCP work is required**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=FXChain.*`

Expected: no frontend code changes were needed because the existing internal-FX param panel already renders `sub_synth` params through `list_fx_params`.
