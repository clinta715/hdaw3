// DrumSynthEngine regression tests — the independently implemented (no
// third-party source vendored), per-sample, realtime-safe 11-voice TR-909-style
// analog drum kit.
//
// What this suite pins:
//   * the pinned param table (51 rows) as TrackFXSlot advertises it;
//   * every one of the 11 voices actually sounds and reports active;
//   * the pinned GM note map + Fixed-mode note-ignoring;
//   * the ClosedHat -> OpenHat choke and its no-discontinuity declick;
//   * bit-identical determinism across two fresh engines AND across a second
//     prepare()+render on the same engine (no wall-clock / pointer / counter
//     seeding anywhere in the render path);
//   * the pinned key-track pitch model;
//   * the TrackFXSlot integration seam (renders, consumes MIDI, clamps params);
//   * full-kit headroom: an all-11-voices unison hit stays under unity at
//     default levels (and never goes NaN/Inf when over-driven);
//   * Gate 1/10: a drum_synth slot restored by a full rebuildFXChain keeps its
//     type AND its params on the LIVE processor (a ReadModel-only assertion
//     would not prove the restored slot sounds right);
//   * isPreservedInstrumentFxType: a drum_synth slot survives applying a
//     reverb-only ChainPreset.
//
// Mirrors the psyarp_engine_test.cpp MSVC STL-assert router (a debug STL
// assert would otherwise pop a modal dialog and hang the runner) and the
// track_fx_rebuild_race_test.cpp live-processor rebuild pattern.

#include <gtest/gtest.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#endif

// ChainLibrary.h MUST precede the Qt-pulling AudioEngine.h so ChainPreset's
// `slots` member is parsed before Qt's `slots` keyword macro exists (same
// ordering dance as fx_chain_preset_test.cpp; the #undef below repairs later
// uses).
#include "engine/ChainLibrary.h"
#include "engine/DrumSynthEngine.h"
#include "engine/TrackFXSlot.h"
#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "model/ProjectModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Qt defines `slots` as a keyword macro (signals/slots); AudioEngine.h pulls Qt
// in, textually blanking ChainPreset::slots. This TU uses no Qt signals/slots
// keywords, so undef it (fx_chain_preset_test.cpp precedent).
#ifdef slots
#undef slots
#endif

// MSVC debug STL asserts (_STL_VERIFY -> _invalid_parameter) pop a MODAL
// dialog in a console test run by default, hanging CI until someone clicks.
// Route them to a handler that prints to stderr and aborts: a regression
// becomes a clean crash the test runner records instead of a silent hang.
#ifdef _MSC_VER
namespace
{
void hdawDrumTestInvalidParameterHandler (const wchar_t*, const wchar_t*,
                                          const wchar_t*, unsigned int, uintptr_t)
{
    std::fputs ("FATAL: CRT invalid parameter (MSVC STL debug assert) fired\n", stderr);
    std::abort();
}
struct HdawDrumAssertRouter
{
    HdawDrumAssertRouter()
    {
        _CrtSetReportMode (_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
        _set_invalid_parameter_handler (hdawDrumTestInvalidParameterHandler);
    }
};
const HdawDrumAssertRouter hdawDrumAssertRouter;
} // namespace
#endif

using namespace HDAW;

namespace
{
constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 512;

void addNoteOn (juce::MidiBuffer& midi, int note, int vel = 100, int sample = 0)
{
    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), sample);
}

// Peak magnitude over every channel; also asserts every sample is finite
// (a NaN/Inf in the render path would otherwise poison the peak comparison).
float peakOf (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer (ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            EXPECT_TRUE (std::isfinite (data[s]));
            peak = std::max (peak, std::abs (data[s]));
        }
    }
    return peak;
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer (ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            if (! std::isfinite (data[s]))
                return false;
    }
    return true;
}

void prepareSlot (TrackFXSlot& slot)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = kSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (kBlockSize);
    spec.numChannels      = 2;
    slot.prepare (spec);
}
} // namespace

// ---------------------------------------------------------------------------
// Param table (the pinned 51-row layout as TrackFXSlot advertises it)
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, ParamDefs)
{
    HDAW::TrackFXSlot slot ("drum_synth");
    const auto defs = slot.getInternalParamDefs();

    ASSERT_EQ (defs.size(), 51u);
    for (int i = 0; i < 51; ++i)
        EXPECT_EQ (defs[static_cast<size_t> (i)].index, i);

    // Row 0: Output Level.
    EXPECT_FLOAT_EQ (defs[0].defaultValue, 0.8f);
    EXPECT_FLOAT_EQ (defs[0].minValue,     0.0f);
    EXPECT_FLOAT_EQ (defs[0].maxValue,     1.5f);

    // Row 4: Voice (integer enum, 0..10).
    EXPECT_FLOAT_EQ (defs[4].defaultValue, 0.0f);
    EXPECT_FLOAT_EQ (defs[4].minValue,     0.0f);
    EXPECT_FLOAT_EQ (defs[4].maxValue,    10.0f);

    // Row 5: Note Map (0=Fixed, 1=GM).
    EXPECT_FLOAT_EQ (defs[5].defaultValue, 0.0f);
    EXPECT_FLOAT_EQ (defs[5].minValue,     0.0f);
    EXPECT_FLOAT_EQ (defs[5].maxValue,     1.0f);

    // Row 6: Key Track.
    EXPECT_FLOAT_EQ (defs[6].defaultValue, 1.0f);
    EXPECT_FLOAT_EQ (defs[6].minValue,     0.0f);
    EXPECT_FLOAT_EQ (defs[6].maxValue,     1.0f);

    // Row 7: Instrument 0 (Kick) Level.
    EXPECT_FLOAT_EQ (defs[7].defaultValue, 0.8f);
    EXPECT_FLOAT_EQ (defs[7].minValue,     0.0f);
    EXPECT_FLOAT_EQ (defs[7].maxValue,     1.5f);

    // Row 10: Instrument 0 (Kick) Tone  (base 7 + 0*4 + 3).
    EXPECT_FLOAT_EQ (defs[10].defaultValue, 0.5f);
    EXPECT_FLOAT_EQ (defs[10].minValue,     0.0f);
    EXPECT_FLOAT_EQ (defs[10].maxValue,     1.0f);

    // Row 50: Instrument 10 (Ride) Tone  (base 7 + 10*4 + 3).
    EXPECT_FLOAT_EQ (defs[50].defaultValue, 0.5f);
    EXPECT_FLOAT_EQ (defs[50].minValue,     0.0f);
    EXPECT_FLOAT_EQ (defs[50].maxValue,     1.0f);

    // The slot's public def table is the one applyFxChain validates against.
    EXPECT_EQ (HDAW::TrackFXSlot::getParamDefsForType ("drum_synth").size(), 51u);
}

// ---------------------------------------------------------------------------
// Every voice sounds
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, EachVoiceSounds)
{
    for (int inst = 0; inst < DrumSynthEngine::kNumInstruments; ++inst)
    {
        DrumSynthEngine eng;
        eng.setVoice (inst);
        eng.setNoteMap (0);
        eng.setOutputLevel (1.0f);
        eng.setInstrumentLevel (inst, 1.0f);
        eng.prepare (kSampleRate, kBlockSize);

        float peak = 0.0f;

        // Short first block (~2.9 ms) so "became active" is observed early in
        // the envelope, before even the shortest voice (Rim ~30 ms) could end.
        {
            juce::AudioBuffer<float> head (2, 128);
            juce::MidiBuffer midi;
            addNoteOn (midi, 45);
            eng.render (head, midi);
            EXPECT_TRUE (eng.instrumentVoiceActiveForTest (inst)) << "instrument " << inst;
            EXPECT_TRUE (allFinite (head)) << "instrument " << inst;
            peak = std::max (peak, peakOf (head));
        }

        // ...then a full block to capture the body of the hit.
        {
            juce::AudioBuffer<float> buffer (2, kBlockSize);
            juce::MidiBuffer midi;
            eng.render (buffer, midi);
            EXPECT_TRUE (allFinite (buffer)) << "instrument " << inst;
            peak = std::max (peak, peakOf (buffer));
        }

        EXPECT_GT (peak, 0.01f) << "instrument " << inst;
    }
}

// ---------------------------------------------------------------------------
// Note map
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, GmMap)
{
    struct NoteMapRow { int note; int expected; };
    const NoteMapRow rows[] = {
        { 35, DrumSynthEngine::Kick      }, { 36, DrumSynthEngine::Kick      },
        { 37, DrumSynthEngine::Rim       },
        { 38, DrumSynthEngine::Snare     }, { 40, DrumSynthEngine::Snare     },
        { 39, DrumSynthEngine::Clap      },
        { 41, DrumSynthEngine::TomLow    }, { 43, DrumSynthEngine::TomLow    },
        { 45, DrumSynthEngine::TomMid    }, { 47, DrumSynthEngine::TomMid    },
        { 48, DrumSynthEngine::TomHigh   }, { 50, DrumSynthEngine::TomHigh   },
        { 42, DrumSynthEngine::ClosedHat }, { 44, DrumSynthEngine::ClosedHat },
        { 46, DrumSynthEngine::OpenHat   },
        { 49, DrumSynthEngine::Crash     }, { 57, DrumSynthEngine::Crash     },
        { 51, DrumSynthEngine::Ride      }, { 59, DrumSynthEngine::Ride      },
    };

    for (const auto& r : rows)
        EXPECT_EQ (DrumSynthEngine::instrumentForNote (r.note, 5), r.expected)
            << "note " << r.note;

    // Unmapped notes fall back to the Voice param value.
    EXPECT_EQ (DrumSynthEngine::instrumentForNote (0, 7), 7);
    EXPECT_EQ (DrumSynthEngine::instrumentForNote (100, 7), 7);
}

TEST (DrumSynthEngineTest, FixedModeIgnoresNote)
{
    DrumSynthEngine eng;
    eng.setVoice (DrumSynthEngine::Ride);
    eng.setNoteMap (0);
    eng.prepare (kSampleRate, kBlockSize);

    auto trigger = [&eng] (int note)
    {
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        addNoteOn (midi, note);
        eng.render (buffer, midi);
    };

    trigger (36);
    EXPECT_EQ (eng.lastTriggerInstrumentForTest(), DrumSynthEngine::Ride);
    trigger (60);
    EXPECT_EQ (eng.lastTriggerInstrumentForTest(), DrumSynthEngine::Ride);
}

// ---------------------------------------------------------------------------
// ClosedHat -> OpenHat choke (classic 909) and declick
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, ClosedHatChokesOpenHatWithoutDiscontinuity)
{
    DrumSynthEngine eng;
    eng.setNoteMap (1);   // GM: note 46 -> OpenHat, note 42 -> ClosedHat
    eng.setOutputLevel (1.0f);
    eng.setInstrumentLevel (DrumSynthEngine::OpenHat,   1.0f);
    eng.setInstrumentLevel (DrumSynthEngine::ClosedHat, 1.0f);
    eng.prepare (kSampleRate, kBlockSize);

    float peak = 0.0f;

    // ~20 ms of OpenHat (2 blocks ~= 23 ms).
    {
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        addNoteOn (midi, 46);
        for (int b = 0; b < 2; ++b)
        {
            if (b > 0)
                midi.clear();
            eng.render (buffer, midi);
            peak = std::max (peak, peakOf (buffer));
            EXPECT_TRUE (allFinite (buffer));
        }
    }
    EXPECT_TRUE (eng.instrumentVoiceActiveForTest (DrumSynthEngine::OpenHat));

    // Choke: 1 block (~11.6 ms) is well past the 2 ms choke release.
    {
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        addNoteOn (midi, 42);
        eng.render (buffer, midi);
        peak = std::max (peak, peakOf (buffer));
        EXPECT_TRUE (allFinite (buffer));
    }

    EXPECT_FALSE (eng.instrumentVoiceActiveForTest (DrumSynthEngine::OpenHat));
    // No hard phase discontinuity / transient spike anywhere in the window.
    EXPECT_LT (peak, 1.5f);
}

// ---------------------------------------------------------------------------
// Determinism (hard requirement: bit-identical across runs)
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, DeterminismIsBitIdentical)
{
    auto renderOnce = [] (DrumSynthEngine& eng, juce::AudioBuffer<float>& out)
    {
        eng.prepare (kSampleRate, kBlockSize);
        juce::MidiBuffer midi;
        addNoteOn (midi, 45, 100, 0);
        addNoteOn (midi, 42, 100, 64);    // retrigger (declick tail slot)
        addNoteOn (midi, 38, 100, 128);
        eng.render (out, midi);
    };

    const size_t bytes = sizeof (float) * static_cast<size_t> (kBlockSize);

    // Two fresh engines -> bit-identical output.
    juce::AudioBuffer<float> a (2, kBlockSize), b (2, kBlockSize);
    DrumSynthEngine e1, e2;
    renderOnce (e1, a);
    renderOnce (e2, b);
    for (int ch = 0; ch < 2; ++ch)
        EXPECT_EQ (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), bytes), 0)
            << "channel " << ch;

    // A second prepare() + identical render on the SAME engine is also
    // bit-identical (prepare fully resets the voice/noise state).
    juce::AudioBuffer<float> c (2, kBlockSize);
    renderOnce (e1, c);
    for (int ch = 0; ch < 2; ++ch)
        EXPECT_EQ (std::memcmp (a.getReadPointer (ch), c.getReadPointer (ch), bytes), 0)
            << "channel " << ch;
}

// ---------------------------------------------------------------------------
// Key track pitch model
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, KeyTrackScalesKickPitch)
{
    DrumSynthEngine eng;
    eng.setVoice (DrumSynthEngine::Kick);
    eng.setNoteMap (0);
    eng.setKeyTrack (1.0f);
    eng.prepare (kSampleRate, kBlockSize);

    auto hzFor = [&eng] (int note)
    {
        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        addNoteOn (midi, note);
        eng.render (buffer, midi);
        return eng.lastTriggerHzForTest();
    };

    const float hz36 = hzFor (36);
    const float hz47 = hzFor (47);
    EXPECT_GT (hz47, hz36);
    const float expectedRatio = std::pow (2.0f, 11.0f / 12.0f);
    EXPECT_NEAR (hz47 / hz36, expectedRatio, expectedRatio * 0.05f);

    // KeyTrack = 0 makes the incoming note irrelevant to pitch.
    eng.setKeyTrack (0.0f);
    const float flat36 = hzFor (36);
    const float flat47 = hzFor (47);
    EXPECT_FLOAT_EQ (flat36, flat47);
}

// ---------------------------------------------------------------------------
// TrackFXSlot integration
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, SlotIntegrationRendersAndConsumesMidi)
{
    HDAW::TrackFXSlot slot ("drum_synth");
    prepareSlot (slot);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    addNoteOn (midi, 45);   // Fixed mode (default Voice 0 = Kick)

    slot.process (buffer, midi);

    EXPECT_GT (peakOf (buffer), 0.01f);
    EXPECT_TRUE (allFinite (buffer));
    // The instrument branch consumed the note (buffer replaced, MIDI cleared).
    EXPECT_TRUE (midi.isEmpty());
}

TEST (DrumSynthEngineTest, SlotParamClamp)
{
    HDAW::TrackFXSlot slot ("drum_synth");

    // Out-of-range writes clamp to the def range (lesson 23).
    slot.setInternalParam (0, 99.0f);   // Output Level -> 1.5
    slot.setInternalParam (4, 99.0f);   // Voice        -> 10.0

    const auto values = slot.getInternalParamValues();
    ASSERT_EQ (values.size(), 51u);
    EXPECT_FLOAT_EQ (values[0], 1.5f);
    EXPECT_FLOAT_EQ (values[4], 10.0f);
}

// ---------------------------------------------------------------------------
// Gate 1/10: rebuild restores the LIVE drum_synth slot and its params
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, RebuildRestoreKeepsDrumSynthOnLiveProcessor)
{
    AudioEngine engine;
    engine.initialize();

    auto& cmds = engine.getProjectCommands();
    cmds.addTrack ("Track");
    cmds.addFxSlot (0, "drum_synth", 0, "");
    cmds.setFxSlotParam (0, 0, 4, 6.0f);   // Voice = 6 (TomHigh)
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack (0);
    ASSERT_NE (track, nullptr);

    auto fxChainTree = engine.getProjectModel().getTrackListTree()
        .getChild (0)
        .getChildWithName (IDs::FX_CHAIN);
    ASSERT_TRUE (fxChainTree.isValid());

    track->rebuildFXChain (fxChainTree);

    // Assert on the LIVE processor, not the ReadModel: only the restored DSP
    // slot proves the chain sounds right (Gate 1/10).
    track = engine.getMainProcessor()->getTrack (0);
    ASSERT_NE (track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_FALSE (chain.empty());
    ASSERT_NE (chain[0], nullptr);
    EXPECT_EQ (chain[0]->getType(), "drum_synth");

    const auto values = chain[0]->getInternalParamValues();
    ASSERT_GE (values.size(), 5u);
    EXPECT_FLOAT_EQ (values[4], 6.0f);
}

// ---------------------------------------------------------------------------
// isPreservedInstrumentFxType: a drum_synth slot survives applying a preset
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, PreservedOnFxChainApply)
{
    AudioEngine engine;
    engine.initialize();
    engine.getProjectCommands().addTrack ("Track");
    engine.drainPendingRoutingRebuild();
    auto& commands = engine.getAudioEngineCommands();

    commands.addFxSlot (0, "drum_synth", 0, std::string());

    HDAW::ChainPreset preset;
    preset.name = "ReverbOnly";
    HDAW::ChainPreset::Slot r;
    r.fxType = "reverb";
    r.params = { { "param_0", 0.8 } };
    preset.slots = { r };

    juce::String error;
    ASSERT_TRUE (commands.applyFxChain (0, preset, &error)) << error.toStdString();

    engine.drainPendingRoutingRebuild();
    auto* track = engine.getMainProcessor()->getTrack (0);
    ASSERT_NE (track, nullptr);
    const auto& chain = track->getFXChain();
    ASSERT_EQ (chain.size(), 2u);
    EXPECT_EQ (chain[0]->getType(), "drum_synth");
    EXPECT_EQ (chain[1]->getType(), "reverb");
}

// ---------------------------------------------------------------------------
// Full-kit headroom: all 11 voices struck on ONE sample stay under unity
// ---------------------------------------------------------------------------

TEST (DrumSynthEngineTest, SimultaneousFullKitStaysUnderUnity)
{
    // The eleven voices are summed into ONE slot. Struck together on the same
    // sample with DEFAULT params (every Instrument Level 0.8, Output Level 0.8)
    // and velocity 127 the linear sum reaches ~3.558x unity — no linear trim
    // can bound that AND keep a single voice usable, so renderSample() passes
    // the sum through a memoryless SOFT CEILING (kitCeiling) whose knee sits
    // ABOVE one and two voices: a single default kick (raw sum ~0.79) and a
    // two-voice hit are bit-identical to the un-ceilinged sum, so normal
    // playing is untouched; only 3+ simultaneous voices engage the soft region,
    // which asymptotes to knee + span = 0.95. The ceiling sits BEFORE Output
    // Level, so a deliberate overdrive can still exceed unity.
    //
    // Expected peaks (default params, velocity 127):
    //   * single kick:     sum 0.788 (< knee 0.80) -> untouched -> x0.8 ~ 0.6304
    //   * 4-voice hit:     sum 0.948 (> knee) -> shaped 0.894 -> x0.8 ~ 0.715
    //   * 11-voice unison: sum 3.558 -> ~0.94999 -> x0.8 ~ 0.760
    //   * 11 voices all levels 1.5 + Output 1.5: sum 6.67 -> 0.95 -> x1.5 ~ 1.425
    //
    // The halving check below is the proof that the ceiling is transparent in
    // the normal range: it asserts the property (peak scales linearly with
    // Output Level), not a brittle absolute amplitude.
    {
        DrumSynthEngine eng;
        eng.setNoteMap (1);   // GM
        eng.prepare (kSampleRate, kBlockSize);

        const int gmKit[11] = { 36, 37, 38, 39, 41, 45, 48, 42, 46, 49, 51 };

        juce::MidiBuffer midi;
        for (int note : gmKit)
            addNoteOn (midi, note, 127, 0);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        eng.render (buffer, midi);

        const float peak = peakOf (buffer);   // also asserts every sample finite
        EXPECT_LT (peak, 1.0f);   // a full-kit hit must not clip
        EXPECT_GT (peak, 0.20f);  // ...and must still be a real, audible hit

        // Linearity below the knee: a single default kick (GM 36, velocity 127,
        // every Instrument Level 0.8) has a pre-ceiling sum below kCeilingKnee,
        // so kitCeiling() must pass it through untouched. Halving Output Level
        // must therefore halve the rendered peak EXACTLY — the property proof
        // that the ceiling is transparent across the normal playing range.
        eng.prepare (kSampleRate, kBlockSize);   // fresh voices, same engine

        juce::MidiBuffer kickMidi;
        addNoteOn (kickMidi, 36, 127, 0);

        juce::AudioBuffer<float> kickBuffer (2, kBlockSize);
        eng.render (kickBuffer, kickMidi);       // Output Level 0.8 (default)

        const float p1 = peakOf (kickBuffer);

        eng.setOutputLevel (0.4f);
        eng.prepare (kSampleRate, kBlockSize);   // fresh voices again

        juce::MidiBuffer kickMidi2;
        addNoteOn (kickMidi2, 36, 127, 0);

        juce::AudioBuffer<float> kickBuffer2 (2, kBlockSize);
        eng.render (kickBuffer2, kickMidi2);

        const float p2 = peakOf (kickBuffer2);

        EXPECT_NEAR (p1, 2.0f * p2, p1 * 0.01f);   // ceiling transparent here
        EXPECT_LT (p1, 0.80f);
        EXPECT_GT (p1, 0.20f);
    }

    // Same 11 voices with EVERY level maxed (Instrument Level 1.5, Output Level
    // 1.5) deliberately over-drives the sum. Over-driving is the user's choice,
    // so no < 1.0 assertion here — but it must not produce NaN/Inf garbage.
    {
        DrumSynthEngine eng;
        eng.setNoteMap (1);
        eng.setOutputLevel (1.5f);
        for (int inst = 0; inst < DrumSynthEngine::kNumInstruments; ++inst)
            eng.setInstrumentLevel (inst, 1.5f);
        eng.prepare (kSampleRate, kBlockSize);

        const int gmKit[11] = { 36, 37, 38, 39, 41, 45, 48, 42, 46, 49, 51 };

        juce::MidiBuffer midi;
        for (int note : gmKit)
            addNoteOn (midi, note, 127, 0);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        eng.render (buffer, midi);

        const float peak = peakOf (buffer);
        EXPECT_TRUE (std::isfinite (peak));
        EXPECT_TRUE (allFinite (buffer));
    }
}
