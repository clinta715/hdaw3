// ReeseBassEngine regression tests — the new detuned-supersaw reese / neuro
// bass with a tempo-synced wobble LFO.
//
// What this suite pins:
//   * the FROZEN 40-row param table (kNumParams, every def inside [min,max],
//     name lookup, and clampParam's below/above clamping);
//   * the default patch actually sounds (peak > 0.05) and stays finite;
//   * bit-identical determinism across two fresh engines AND across two
//     prepare()+render() passes on the SAME engine (no wall-clock, pointer,
//     counter or juce::Random seeding anywhere in the render path);
//   * lesson 38: every named param knob CHANGES the rendered buffer (the
//     observable effect, not the parse);
//   * inert-by-default gates: Sync Ratio does nothing while Sync Amount is 0;
//   * lesson 45: the all-params-maxed patch stays finite and under 1.5 peak;
//   * velocity is audible through Velocity->Drive and provably inert at 0;
//   * Mono Legato (the default) keeps ONE sounding voice for overlapping
//     notes, while Mono Legato = 0 gives the two poly note slots;
//   * render() consumes (clears) the MidiBuffer it is handed.

#include <gtest/gtest.h>

#include "engine/ReeseBassEngine.h"

// ChainLibrary.h MUST precede the Qt-pulling AudioEngine.h so ChainPreset's
// `slots` member is parsed before Qt's `slots` keyword macro exists (same
// ordering dance as fx_chain_preset_test.cpp / drum_synth_test.cpp; the
// #undef below repairs later uses).
#include "engine/ChainLibrary.h"
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
#include <cstring>
#include <string>
#include <vector>

// Qt defines `slots` as a keyword macro (signals/slots); AudioEngine.h pulls Qt
// in, textually blanking ChainPreset::slots. This TU uses no Qt signals/slots
// keywords, so undef it (fx_chain_preset_test.cpp precedent).
#ifdef slots
#undef slots
#endif

namespace
{

constexpr double kSr = 44100.0;
constexpr int kBlock = 256;
constexpr int kLongBlocks = 64;   // 16384 samples: long enough for the wobble LFO

struct Event
{
    int block;
    juce::MidiMessage msg;
};

juce::MidiMessage noteOnMsg(int note, int velocity)
{
    return juce::MidiMessage::noteOn(1, note, (juce::uint8) velocity);
}

juce::MidiMessage noteOffMsg(int note)
{
    return juce::MidiMessage::noteOff(1, note);
}

// Renders `blocks` blocks of kBlock samples, injecting `events` at the start
// of the block they name. Returns the whole concatenated render.
juce::AudioBuffer<float> renderWithEvents(ReeseBassEngine& engine, int blocks,
                                          const std::vector<Event>& events,
                                          int numChannels = 2)
{
    juce::AudioBuffer<float> out(numChannels, blocks * kBlock);
    out.clear();

    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> block(numChannels, kBlock);
        block.clear();

        juce::MidiBuffer midi;
        for (const auto& e : events)
            if (e.block == b)
                midi.addEvent(e.msg, 0);

        engine.render(block, midi);
        EXPECT_TRUE(midi.isEmpty()) << "render() must consume the MidiBuffer";

        for (int ch = 0; ch < numChannels; ++ch)
            out.copyFrom(ch, b * kBlock, block, ch, 0, kBlock);
    }

    return out;
}

float peakOf(const juce::AudioBuffer<float>& b)
{
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        peak = std::max(peak, b.getMagnitude(ch, 0, b.getNumSamples()));
    return peak;
}

bool allFinite(const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* d = b.getReadPointer(ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite(d[i]))
                return false;
    }
    return true;
}

// memcmp over channel 0 (every test that uses this compares stereo renders
// whose two channels move together).
bool bitIdentical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp(a.getReadPointer(ch), b.getReadPointer(ch),
                        sizeof(float) * (size_t) a.getNumSamples()) != 0)
            return false;

    return true;
}

// A fresh, prepared engine rendering one held note 36 for kLongBlocks.
juce::AudioBuffer<float> defaultRender(int paramIndex = -1, float value = 0.0f)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);
    if (paramIndex >= 0)
        engine.setParam(paramIndex, value);

    return renderWithEvents(engine, kLongBlocks, { { 0, noteOnMsg(36, 127) } });
}

} // namespace

// ============================================================================
// 1. Param table
// ============================================================================

TEST(ReeseBassEngineTest, ParamTableIsFrozenAndConsistent)
{
    EXPECT_EQ(ReeseBassEngine::kNumParams, 40);
    EXPECT_EQ(ReeseBassEngine::kMaxVoices, 7);

    const auto& defs = ReeseBassEngine::paramDefs();
    EXPECT_EQ((int) defs.size(), 40);

    for (int i = 0; i < ReeseBassEngine::kNumParams; ++i)
    {
        const auto& d = defs[(size_t) i];
        EXPECT_NE(d.name, nullptr) << "row " << i;
        EXPECT_GE(d.def, d.min) << d.name;
        EXPECT_LE(d.def, d.max) << d.name;
        EXPECT_LT(d.min, d.max) << d.name;
        // The name lookup round-trips (the device map and TrackFXSlot address
        // params by these names).
        EXPECT_EQ(ReeseBassEngine::paramIndexForName(d.name), i) << d.name;
    }

    // The header's exact first/last rows (a reorder would silently repoint
    // every saved project's values).
    EXPECT_STREQ(defs[0].name, "Voice Count");
    EXPECT_FLOAT_EQ(defs[0].def, 7.0f);
    EXPECT_FLOAT_EQ(defs[0].min, 1.0f);
    EXPECT_FLOAT_EQ(defs[0].max, 7.0f);
    EXPECT_STREQ(defs[39].name, "Velocity->Drive");
    EXPECT_FLOAT_EQ(defs[39].def, 0.3f);

    EXPECT_EQ(ReeseBassEngine::paramIndexForName("Voice Count"), 0);
    EXPECT_EQ(ReeseBassEngine::paramIndexForName("Velocity->Drive"), 39);
    EXPECT_EQ(ReeseBassEngine::paramIndexForName("Nope"), -1);
    EXPECT_EQ(ReeseBassEngine::paramIndexForName(nullptr), -1);

    // clampParam clamps into the ROW's range and ignores a bad index.
    EXPECT_FLOAT_EQ(ReeseBassEngine::clampParam(1, -5.0f), 0.0f);
    EXPECT_FLOAT_EQ(ReeseBassEngine::clampParam(1, 1.0e9f), 100.0f);
    EXPECT_FLOAT_EQ(ReeseBassEngine::clampParam(16, 0.0f), 0.1f);
    EXPECT_FLOAT_EQ(ReeseBassEngine::clampParam(16, 1000.0f), 20.0f);
    EXPECT_FLOAT_EQ(ReeseBassEngine::clampParam(-1, 3.5f), 3.5f);
    EXPECT_FLOAT_EQ(ReeseBassEngine::clampParam(999, 3.5f), 3.5f);
}

TEST(ReeseBassEngineTest, DefaultsMatchTheTableAndSetParamClamps)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);

    const auto& defs = ReeseBassEngine::paramDefs();
    for (int i = 0; i < ReeseBassEngine::kNumParams; ++i)
        EXPECT_FLOAT_EQ(engine.getParam(i), defs[(size_t) i].def) << defs[(size_t) i].name;

    // setParam clamps, and rounds the eight integer-valued rows.
    engine.setParam(1, 1.0e9f);
    EXPECT_FLOAT_EQ(engine.getParam(1), 100.0f);
    engine.setParam(1, -100.0f);
    EXPECT_FLOAT_EQ(engine.getParam(1), 0.0f);

    engine.setParam(0, 1.4f);
    EXPECT_FLOAT_EQ(engine.getParam(0), 1.0f);
    engine.setParam(13, 2.7f);
    EXPECT_FLOAT_EQ(engine.getParam(13), 3.0f);
    engine.setParam(6, -1.6f);
    EXPECT_FLOAT_EQ(engine.getParam(6), -2.0f);

    // An out-of-range index is ignored (no clamp, no store, no crash).
    engine.setParam(-1, 42.0f);
    engine.setParam(ReeseBassEngine::kNumParams, 42.0f);
    EXPECT_FLOAT_EQ(engine.getParam(-1), 0.0f);
    EXPECT_FLOAT_EQ(engine.getParam(999), 0.0f);
}

// ============================================================================
// 2. Audible + finite
// ============================================================================

TEST(ReeseBassEngineTest, DefaultsAreAudibleAndFinite)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);

    const auto buf = renderWithEvents(engine, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    EXPECT_GT(peakOf(buf), 0.05f);
    EXPECT_TRUE(allFinite(buf));
    EXPECT_EQ(engine.activeVoiceCount(), 1);

    // The sounding slot reports the note-36 pitch.
    EXPECT_NEAR(engine.currentHzForTest(0), 440.0f * std::exp2((36.0f - 69.0f) / 12.0f), 1.0f);
    EXPECT_FLOAT_EQ(engine.currentHzForTest(1), 0.0f);
    EXPECT_FLOAT_EQ(engine.currentHzForTest(-1), 0.0f);
}

TEST(ReeseBassEngineTest, NoteOffReleasesThenGoesIdle)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);

    renderWithEvents(engine, 4, { { 0, noteOnMsg(36, 127) }, { 2, noteOffMsg(36) } });
    EXPECT_EQ(engine.activeVoiceCount(), 1);        // releasing slot still sounds

    // Amp Release is 0.08 s default; 64 empty blocks is far past it.
    renderWithEvents(engine, 64, {});
    EXPECT_EQ(engine.activeVoiceCount(), 0);

    // ...and the tail is silent, not a stuck DC offset.
    const auto tail = renderWithEvents(engine, 8, {});
    EXPECT_FLOAT_EQ(peakOf(tail), 0.0f);
}

// ============================================================================
// 3. Determinism
// ============================================================================

TEST(ReeseBassEngineTest, TwoFreshEnginesAreBitIdentical)
{
    const auto a = defaultRender();
    const auto b = defaultRender();

    EXPECT_TRUE(bitIdentical(a, b));
    EXPECT_GT(peakOf(a), 0.05f);   // guard: we compared a non-silent render
}

TEST(ReeseBassEngineTest, PrepareResetsToABitIdenticalState)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);
    const auto first = renderWithEvents(engine, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    engine.prepare(kSr, kBlock);
    const auto second = renderWithEvents(engine, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    EXPECT_TRUE(bitIdentical(first, second));

    // prepare(sr, 0) is the deferred-reset entry point; it must be the same
    // full reset (and must never shrink anything).
    engine.prepare(kSr, 0);
    const auto third = renderWithEvents(engine, kLongBlocks, { { 0, noteOnMsg(36, 127) } });
    EXPECT_TRUE(bitIdentical(first, third));
}

// ============================================================================
// 4. Param effect (lesson 38: assert the observable effect, not the parse)
// ============================================================================

TEST(ReeseBassEngineTest, EachKnobChangesTheRender)
{
    const auto base = defaultRender();
    ASSERT_GT(peakOf(base), 0.05f);

    struct Case { int index; float value; const char* name; };
    const Case cases[] = {
        {  1,  80.0f,  "Detune Cents" },
        {  3,   1.0f,  "Osc Shape" },
        {  5,   0.9f,  "Sub Level" },
        {  9,   0.8f,  "Comb Amount" },
        { 12,  40.0f,  "Drive dB" },
        { 15, 200.0f,  "Filter Cutoff" },
        { 32,   1.0f,  "LFO Cutoff Amt" },
    };

    for (const auto& c : cases)
    {
        const auto variant = defaultRender(c.index, c.value);
        EXPECT_TRUE(allFinite(variant)) << c.name;
        EXPECT_FALSE(bitIdentical(base, variant))
            << c.name << " must change the rendered buffer";
    }
}

TEST(ReeseBassEngineTest, OutputLevelScalesThePeakLinearlyBelowTheCeiling)
{
    const auto quiet = defaultRender(28, 0.35f);
    const auto loud  = defaultRender(28, 0.70f);

    const float pq = peakOf(quiet);
    const float pl = peakOf(loud);

    ASSERT_GT(pq, 0.01f);
    // The soft ceiling sits BEFORE Output Level, so below the knee the level
    // param is a pure gain: doubling it doubles the peak.
    EXPECT_NEAR(pl / pq, 2.0f, 0.2f);
}

// ============================================================================
// 5. Inert-by-default gates
// ============================================================================

TEST(ReeseBassEngineTest, SyncRatioIsInertWithoutSyncAmount)
{
    ReeseBassEngine base;
    base.prepare(kSr, kBlock);
    base.setParam(7, 0.0f);     // Sync Amount = 0 (also the default)
    const auto a = renderWithEvents(base, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    ReeseBassEngine variant;
    variant.prepare(kSr, kBlock);
    variant.setParam(7, 0.0f);
    variant.setParam(8, 4.0f);  // Sync Ratio = 4 — must do nothing without amount
    const auto b = renderWithEvents(variant, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    EXPECT_TRUE(bitIdentical(a, b));

    // ...and with a non-zero amount the ratio DOES change the render (so the
    // gate is really gating, not just dropping the whole sync stage).
    ReeseBassEngine synced;
    synced.prepare(kSr, kBlock);
    synced.setParam(7, 1.0f);
    synced.setParam(8, 4.0f);
    const auto c = renderWithEvents(synced, kLongBlocks, { { 0, noteOnMsg(36, 127) } });
    EXPECT_FALSE(bitIdentical(a, c));
}

TEST(ReeseBassEngineTest, CombAmountZeroIsABitExactBypass)
{
    // With Comb Amount at its 0 default the whole comb stage is skipped, so
    // Comb Frequency / Feedback cannot touch the render.
    ReeseBassEngine base;
    base.prepare(kSr, kBlock);
    const auto a = renderWithEvents(base, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    ReeseBassEngine variant;
    variant.prepare(kSr, kBlock);
    variant.setParam(10, 20.0f);   // Comb Frequency
    variant.setParam(11, 0.95f);   // Comb Feedback
    const auto b = renderWithEvents(variant, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    EXPECT_TRUE(bitIdentical(a, b));
}

TEST(ReeseBassEngineTest, LfoAmountsAtZeroAreABitExactBypass)
{
    ReeseBassEngine base;
    base.prepare(kSr, kBlock);
    const auto a = renderWithEvents(base, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    ReeseBassEngine variant;
    variant.prepare(kSr, kBlock);
    variant.setParam(29, 3.0f);    // LFO Shape = SawDown
    variant.setParam(30, 0.0625f); // fastest rate
    variant.setParam(31, 0.0f);    // free-running Hz
    variant.setParam(35, 0.7f);    // LFO Phase
    const auto b = renderWithEvents(variant, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    EXPECT_TRUE(bitIdentical(a, b));
}

// ============================================================================
// 6. Headroom (lesson 45)
// ============================================================================

TEST(ReeseBassEngineTest, MaxedParamsStayFiniteAndUnderTheCeiling)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);

    engine.setParam(0, 7.0f);    // Voice Count (already 7)
    engine.setParam(12, 40.0f);  // Drive dB max
    engine.setParam(28, 1.0f);   // Output Level max
    engine.setParam(5, 1.0f);    // Sub Level max
    engine.setParam(9, 1.0f);    // Comb Amount max
    engine.setParam(11, 0.95f);  // Comb Feedback max
    engine.setParam(1, 100.0f);  // Detune max
    engine.setParam(2, 1.0f);    // Stereo Spread max
    engine.setParam(16, 20.0f);  // Filter Res max
    engine.setParam(39, 1.0f);   // Velocity->Drive max

    const auto buf = renderWithEvents(engine, kLongBlocks, { { 0, noteOnMsg(36, 127) } });

    EXPECT_TRUE(allFinite(buf));
    // The per-channel soft ceiling asymptotes to knee + span = 1.5 and Output
    // Level is 1.0, so the peak can never exceed the documented 1.5 bound.
    EXPECT_LE(peakOf(buf), 1.5f);
    EXPECT_GT(peakOf(buf), 0.05f);
}

// ============================================================================
// 7. Velocity is audible
// ============================================================================

TEST(ReeseBassEngineTest, VelocityIsAudibleThroughVelocityDrive)
{
    auto renderVelocity = [](float velocityDrive, int velocity)
    {
        ReeseBassEngine engine;
        engine.prepare(kSr, kBlock);
        engine.setParam(39, velocityDrive);
        return renderWithEvents(engine, kLongBlocks, { { 0, noteOnMsg(36, velocity) } });
    };

    const auto soft127 = renderVelocity(1.0f, 127);
    const auto soft20  = renderVelocity(1.0f, 20);
    EXPECT_FALSE(bitIdentical(soft127, soft20));

    // With Velocity->Drive at 0 velocity must be provably inert: the two
    // renders are byte-identical.
    const auto flat127 = renderVelocity(0.0f, 127);
    const auto flat20  = renderVelocity(0.0f, 20);
    ASSERT_GT(peakOf(flat127), 0.05f);
    EXPECT_TRUE(bitIdentical(flat127, flat20));

    // ...and the two velocity-drive settings themselves differ, i.e. the
    // velocity path really is engaged above.
    EXPECT_FALSE(bitIdentical(flat127, soft127));
}

// ============================================================================
// 8. Mono legato (the default) vs poly
// ============================================================================

TEST(ReeseBassEngineTest, MonoLegatoKeepsOneVoiceForOverlappingNotes)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);

    renderWithEvents(engine, 4, { { 0, noteOnMsg(36, 127) } });
    EXPECT_EQ(engine.activeVoiceCount(), 1);

    // Second key while the first is still held: legato retargets the SAME slot
    // (and the envelopes keep running — this is a mono lead, not a retrigger).
    renderWithEvents(engine, 4, { { 0, noteOnMsg(43, 127) } });
    EXPECT_EQ(engine.activeVoiceCount(), 1);
    EXPECT_NEAR(engine.currentHzForTest(0), 440.0f * std::exp2((43.0f - 69.0f) / 12.0f), 1.0f);

    // Releasing the newest key falls back to the key still held, not silence.
    renderWithEvents(engine, 4, { { 0, noteOffMsg(43) } });
    EXPECT_EQ(engine.activeVoiceCount(), 1);
    EXPECT_NEAR(engine.currentHzForTest(0), 440.0f * std::exp2((36.0f - 69.0f) / 12.0f), 1.0f);

    // Releasing the last key releases the slot.
    renderWithEvents(engine, 64, { { 0, noteOffMsg(36) } });
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST(ReeseBassEngineTest, PolyModeUsesTwoNoteSlots)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);
    engine.setParam(37, 0.0f);   // Mono Legato = 0 -> poly

    renderWithEvents(engine, 4, { { 0, noteOnMsg(36, 127) } });
    EXPECT_EQ(engine.activeVoiceCount(), 1);

    renderWithEvents(engine, 4, { { 0, noteOnMsg(43, 127) } });
    EXPECT_EQ(engine.activeVoiceCount(), 2);

    // A third overlapping note steals the quietest slot: the count stays at
    // the documented two.
    renderWithEvents(engine, 4, { { 0, noteOnMsg(48, 127) } });
    EXPECT_EQ(engine.activeVoiceCount(), 2);
}

// ============================================================================
// 9. MIDI consumption + glide / pitch bend plumbing
// ============================================================================

TEST(ReeseBassEngineTest, RenderConsumesMidi)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);

    juce::AudioBuffer<float> buffer(2, kBlock);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(noteOnMsg(36, 127), 0);
    midi.addEvent(noteOffMsg(36), 128);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 9000), 200);

    ASSERT_FALSE(midi.isEmpty());
    engine.render(buffer, midi);
    EXPECT_TRUE(midi.isEmpty());
}

TEST(ReeseBassEngineTest, GlideReachesTheTargetAndZeroGlideSnaps)
{
    // Glide 0 (the default) snaps the sounding pitch on the note-on sample.
    ReeseBassEngine snap;
    snap.prepare(kSr, kBlock);
    renderWithEvents(snap, 2, { { 0, noteOnMsg(36, 127) } });
    EXPECT_NEAR(snap.currentHzForTest(0),
                440.0f * std::exp2((36.0f - 69.0f) / 12.0f), 0.01f);

    // Glide = 1 s: after ~0.5 s the linear integrator is halfway, after 1 s it
    // has arrived.
    ReeseBassEngine glide;
    glide.prepare(kSr, kBlock);
    glide.setParam(36, 1.0f);
    glide.setParam(25, 5.0f);   // slow amp decay so the slot is still sounding
    glide.setParam(27, 5.0f);
    renderWithEvents(glide, 4, { { 0, noteOnMsg(36, 127) } });
    const float hz36 = glide.currentHzForTest(0);

    const int halfBlocks = (int) (0.5 * kSr / kBlock);
    const int fullBlocks = (int) (1.0 * kSr / kBlock) + 4;

    renderWithEvents(glide, halfBlocks, { { 0, noteOnMsg(43, 127) } });
    const float hzMid = glide.currentHzForTest(0);

    const float hz43 = 440.0f * std::exp2((43.0f - 69.0f) / 12.0f);
    EXPECT_GT(hzMid, hz36);
    EXPECT_LT(hzMid, hz43);
    EXPECT_GT(hzMid - hz36, 0.25f * (hz43 - hz36));
    EXPECT_LT(hzMid - hz36, 0.75f * (hz43 - hz36));

    renderWithEvents(glide, fullBlocks, {});
    EXPECT_NEAR(glide.currentHzForTest(0), hz43, 0.05f);
}

TEST(ReeseBassEngineTest, PitchBendAndAllNotesOff)
{
    ReeseBassEngine engine;
    engine.prepare(kSr, kBlock);

    renderWithEvents(engine, 4, { { 0, noteOnMsg(36, 127) } });
    const float hz0 = engine.currentHzForTest(0);

    // Pitch Bend Range = 12 semitones; full-up bend doubles the frequency.
    engine.setParam(38, 12.0f);
    renderWithEvents(engine, 4, { { 0, juce::MidiMessage::pitchWheel(1, 16383) } });
    EXPECT_NEAR(engine.currentHzForTest(0), hz0 * 2.0f, 0.1f);

    // All-notes-off kills the sounding slot immediately.
    renderWithEvents(engine, 4, { { 0, juce::MidiMessage::allNotesOff(1) } });
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST(ReeseBassEngineTest, RendersIntoMonoAndWideBuffers)
{
    for (int channels : { 1, 2, 4 })
    {
        ReeseBassEngine engine;
        engine.prepare(kSr, kBlock);

        const auto buf = renderWithEvents(engine, 8, { { 0, noteOnMsg(36, 127) } }, channels);
        EXPECT_EQ(buf.getNumChannels(), channels);
        EXPECT_GT(peakOf(buf), 0.05f) << "channels=" << channels;
        EXPECT_TRUE(allFinite(buf)) << "channels=" << channels;

        if (channels > 2)
        {
            // Every channel past stereo carries channel 0's signal.
            EXPECT_EQ(std::memcmp(buf.getReadPointer(0), buf.getReadPointer(3),
                                  sizeof(float) * (size_t) buf.getNumSamples()), 0);
        }
    }
}

TEST(ReeseBassEngineTest, TempoChangesTheSyncedWobble)
{
    auto renderAt = [](double bpm)
    {
        ReeseBassEngine engine;
        engine.prepare(kSr, kBlock);
        engine.setTempo(bpm);
        engine.setParam(32, 1.0f);   // LFO Cutoff Amt engages the wobble
        return renderWithEvents(engine, kLongBlocks, { { 0, noteOnMsg(36, 127) } });
    };

    const auto slow = renderAt(80.0);
    const auto fast = renderAt(160.0);

    EXPECT_TRUE(allFinite(slow));
    EXPECT_TRUE(allFinite(fast));
    EXPECT_FALSE(bitIdentical(slow, fast));
}

// ============================================================================
// 10. TrackFXSlot integration — the first-class `reese_bass` fxType
// ============================================================================

namespace
{

void prepareSlot(HDAW::TrackFXSlot& slot)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = kSr;
    spec.maximumBlockSize = static_cast<juce::uint32>(kBlock);
    spec.numChannels      = 2;
    slot.prepare(spec);
}

// Renders `blocks` blocks through a prepared slot, injecting `events` at the
// start of the block they name. Returns the whole concatenated render.
juce::AudioBuffer<float> renderSlot(HDAW::TrackFXSlot& slot, int blocks,
                                    const std::vector<Event>& events)
{
    juce::AudioBuffer<float> out(2, blocks * kBlock);
    out.clear();

    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> block(2, kBlock);
        block.clear();

        juce::MidiBuffer midi;
        for (const auto& e : events)
            if (e.block == b)
                midi.addEvent(e.msg, 0);

        slot.process(block, midi);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, b * kBlock, block, ch, 0, kBlock);
    }

    return out;
}

} // namespace

// SlotIntegration: a `reese_bass` TrackFXSlot prepares, keeps its type, renders
// the held note non-silently and consumes the MIDI it is handed.
TEST(ReeseBassEngineTest, SlotIntegrationRendersAndConsumesMidi)
{
    HDAW::TrackFXSlot slot("reese_bass");
    EXPECT_EQ(slot.getType(), "reese_bass");
    EXPECT_EQ(slot.getInternalParamDefs().size(), 40u);

    prepareSlot(slot);
    ASSERT_NE(slot.reeseBassEngine(), nullptr) << "prepare() must build the engine";

    juce::AudioBuffer<float> buffer(2, kBlock);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(noteOnMsg(36, 127), 0);

    slot.process(buffer, midi);

    EXPECT_GT(peakOf(buffer), 0.01f);
    EXPECT_TRUE(allFinite(buffer));
    EXPECT_TRUE(midi.isEmpty()) << "the instrument branch consumes the note";
}

// The FULL 40-param contract through the slot: the advertised def table matches
// ReeseBassEngine::paramDefs() name-for-name/range-for-range, and a write at the
// row's max then min lands on the LIVE processor (Gate 1/10 discipline — assert
// on the engine, not the tree).
TEST(ReeseBassEngineTest, SlotParamDefsMatchEngineAndWritesReachTheLiveProcessor)
{
    HDAW::TrackFXSlot slot("reese_bass");
    prepareSlot(slot);

    auto* eng = slot.reeseBassEngine();
    ASSERT_NE(eng, nullptr);

    const auto defs = slot.getInternalParamDefs();
    ASSERT_EQ(defs.size(), 40u);
    for (int i = 0; i < ReeseBassEngine::kNumParams; ++i)
    {
        const auto& d = ReeseBassEngine::paramDefs()[(size_t) i];
        const auto& s = defs[(size_t) i];
        EXPECT_EQ(s.index, i);
        EXPECT_EQ(s.name.toStdString(), std::string(d.name)) << "row " << i;
        EXPECT_FLOAT_EQ(s.defaultValue, d.def) << d.name;
        EXPECT_FLOAT_EQ(s.minValue, d.min) << d.name;
        EXPECT_FLOAT_EQ(s.maxValue, d.max) << d.name;
    }

    // Every row: a write at max then min (a) clamps into the row range on the
    // slot, and (b) reaches the LIVE engine's readback.
    for (int i = 0; i < ReeseBassEngine::kNumParams; ++i)
    {
        const auto& d = ReeseBassEngine::paramDefs()[(size_t) i];
        slot.setInternalParam(i, d.max);
        EXPECT_FLOAT_EQ(eng->getParam(i), d.max) << d.name << " @max";
        slot.setInternalParam(i, d.min);
        EXPECT_FLOAT_EQ(eng->getParam(i), d.min) << d.name << " @min";
    }

    // An out-of-range write clamps to the row's range on BOTH the tree readback
    // and the live engine.
    slot.setInternalParam(1, 1.0e9f);   // Detune Cents [0,100]
    const auto values = slot.getInternalParamValues();
    ASSERT_EQ(values.size(), 40u);
    EXPECT_FLOAT_EQ(values[1], 100.0f);
    EXPECT_FLOAT_EQ(eng->getParam(1), 100.0f);
}

// Tempo reaches the LIVE engine through TrackFXSlot::setTempo — the wobble is
// tempo-synced, so the project tempo MUST land every block.
TEST(ReeseBassEngineTest, SlotTempoReachesTheSyncedWobble)
{
    auto renderAt = [](double bpm)
    {
        HDAW::TrackFXSlot slot("reese_bass");
        prepareSlot(slot);
        slot.setInternalParam(32, 1.0f);   // LFO Cutoff Amt engages the wobble
        slot.setTempo(bpm);
        return renderSlot(slot, kLongBlocks, { { 0, noteOnMsg(36, 127) } });
    };

    const auto slow = renderAt(80.0);
    const auto fast = renderAt(160.0);
    EXPECT_TRUE(allFinite(slow));
    EXPECT_TRUE(allFinite(fast));
    EXPECT_FALSE(bitIdentical(slow, fast))
        << "project tempo must reach the slot's tempo-synced wobble LFO";

    // With every LFO amount at 0 the same two tempos must be IDENTICAL — the
    // modulation, not some other tempo path, is what moved.
    auto renderInert = [](double bpm)
    {
        HDAW::TrackFXSlot slot("reese_bass");
        prepareSlot(slot);
        slot.setInternalParam(32, 0.0f);   // LFO Cutoff Amt
        slot.setInternalParam(33, 0.0f);   // LFO Pitch Amt
        slot.setInternalParam(34, 0.0f);   // LFO Drive Amt
        slot.setTempo(bpm);
        return renderSlot(slot, kLongBlocks, { { 0, noteOnMsg(36, 127) } });
    };

    const auto inertSlow = renderInert(80.0);
    const auto inertFast = renderInert(160.0);
    EXPECT_TRUE(bitIdentical(inertSlow, inertFast))
        << "no LFO amount -> the tempo is inert";
}

// ---------------------------------------------------------------------------
// Gate 1/10: a full rebuildrestores the LIVE reese_bass slot and its params
// (drum_synth_test.cpp precedent)
// ---------------------------------------------------------------------------

TEST(ReeseBassEngineTest, RebuildRestoreKeepsReeseBassOnLiveProcessor)
{
    AudioEngine engine;
    engine.initialize();

    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Track");
    cmds.addFxSlot(0, "reese_bass", 0, "");
    cmds.setFxSlotParam(0, 0, 3, 2.0f);   // Osc Shape = 2 (Triangle)
    cmds.setFxSlotParam(0, 0, 1, 80.0f);  // Detune Cents
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);

    auto fxChainTree = engine.getProjectModel().getTrackListTree()
        .getChild(0)
        .getChildWithName(IDs::FX_CHAIN);
    ASSERT_TRUE(fxChainTree.isValid());

    track->rebuildFXChain(fxChainTree);

    // Assert on the LIVE processor, not the ReadModel: only the restored DSP
    // slot (and its live engine) proves the chain sounds right (Gate 1/10).
    track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_FALSE(chain.empty());
    ASSERT_NE(chain[0], nullptr);
    EXPECT_EQ(chain[0]->getType(), "reese_bass");

    const auto values = chain[0]->getInternalParamValues();
    ASSERT_EQ(values.size(), 40u);
    EXPECT_FLOAT_EQ(values[3], 2.0f);
    EXPECT_FLOAT_EQ(values[1], 80.0f);

    auto* eng = chain[0]->reeseBassEngine();
    ASSERT_NE(eng, nullptr) << "the rebuilt slot must own a LIVE engine";
    EXPECT_FLOAT_EQ(eng->getParam(3), 2.0f);
    EXPECT_FLOAT_EQ(eng->getParam(1), 80.0f);
}

// ---------------------------------------------------------------------------
// isPreservedInstrumentFxType: a reese_bass slot survives applying a preset
// (drum_synth_test.cpp precedent)
// ---------------------------------------------------------------------------

TEST(ReeseBassEngineTest, PreservedOnFxChainApply)
{
    AudioEngine engine;
    engine.initialize();
    engine.getProjectCommands().addTrack("Track");
    engine.drainPendingRoutingRebuild();
    auto& commands = engine.getAudioEngineCommands();

    commands.addFxSlot(0, "reese_bass", 0, std::string());

    HDAW::ChainPreset preset;
    preset.name = "ReverbOnly";
    HDAW::ChainPreset::Slot r;
    r.fxType = "reverb";
    r.params = { { "param_0", 0.8 } };
    preset.slots = { r };

    juce::String error;
    ASSERT_TRUE(commands.applyFxChain(0, preset, &error)) << error.toStdString();

    engine.drainPendingRoutingRebuild();
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    const auto& chain = track->getFXChain();
    ASSERT_EQ(chain.size(), 2u);
    EXPECT_EQ(chain[0]->getType(), "reese_bass");
    EXPECT_EQ(chain[1]->getType(), "reverb");
}

