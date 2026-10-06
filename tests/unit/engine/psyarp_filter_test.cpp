// PsyArpEngine multimode filter + Pulse/Noise osc shapes (slice A of
// docs/plans/2026-10-05-internal-synth-expansion.md), re-pinned for slice C
// (PolyBLEP oscillator anti-aliasing).
//
// Slice A was ADDITIVE: new params 21..23 APPENDED to the psyarp def table and
// Osc Shape's max only WIDENED (2 -> 4), proven by the back-compat hashes.
//
// Slice C (2026-10-05) is NON-ADDITIVE: the naive Saw/Square/Pulse/SuperSaw
// waveforms were replaced IN PLACE with PolyBLEP-band-limited versions (no
// opt-out param — naive aliasing is a defect, not a feature). Those hashes
// therefore MOVED, and the values below are re-pinned to the AA render: the
// tests now pin the slice-C output (any further DSP edit on this path moves
// them again). Their remaining value is as a regression lock on the AA render,
// not as A-slice back-compat.
#include <gtest/gtest.h>

#include "engine/PsyArpEngine.h"
#include "engine/TrackFXSlot.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace HDAW;

namespace
{
constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 512;

// FNV-1a over the raw float bits of every sample in the rendered buffer.
uint32_t hashSamples (const std::vector<float>& x)
{
    uint32_t h = 2166136261u;
    for (float v : x)
    {
        uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof (bits));
        h ^= bits;
        h *= 16777619u;
    }
    return h;
}

double peakOf (const std::vector<float>& x)
{
    double p = 0.0;
    for (float v : x)
        p = std::max (p, (double) std::fabs (v));
    return p;
}

bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (! std::isfinite (v)) return false;
    return true;
}

// Number of samples that differ AT ALL between two equal-length renders.
size_t countDifferent (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size());
    size_t d = 0;
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) ++d;
    return d;
}

// Peak absolute sample difference (a proper "these renders are audibly
// different" measure, not just "not bit-equal").
double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size());
    double m = 0.0;
    for (size_t i = 0; i < n; ++i)
        m = std::max (m, (double) std::fabs ((double) a[i] - (double) b[i]));
    return m;
}

// Energy left after a CASCADE of 3 one-pole lowpasses at `fc` (an 18 dB/oct
// low-band probe — steep enough that high-frequency content does not leak
// through and masquerade as low-band energy). HP well above the note
// fundamental must leave much less than LP.
double lowBandEnergy (const std::vector<float>& x, double fc)
{
    const double a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * fc / kSampleRate);
    double y0 = 0.0, y1 = 0.0, y2 = 0.0, e = 0.0;
    for (float v : x)
    {
        y0 += a * ((double) v - y0);
        y1 += a * (y0 - y1);
        y2 += a * (y1 - y2);
        e += y2 * y2;
    }
    return e;
}

struct RenderCfg
{
    int   oscShape    = 0;      // Saw
    int   uniVoices   = 1;
    float detuneCents = 0.0f;
    float cutoff      = 1000.0f;
    float resonance   = 4.0f;
    float sweepBars   = 16.0f;  // slow: the cutoff sits near `cutoff`
    int   filterMode  = 0;      // LP
    float pulseWidth  = 0.5f;
    float noiseLevel  = 0.0f;
    int   blocks      = 48;
    std::vector<int> notes = { 48 };
};

// Renders channel 0 through the full engine with FX disabled, so the oscillator
// and filter are the only things shaping the signal.
std::vector<float> renderEngine (const RenderCfg& cfg)
{
    PsyArpEngine engine;
    engine.setOscShape (cfg.oscShape);
    engine.setOscUnisonVoices (cfg.uniVoices);
    engine.setOscDetuneCents (cfg.detuneCents);
    engine.setFilterCutoffHz (cfg.cutoff);
    engine.setFilterResonance (cfg.resonance);
    engine.setFilterSweepBars (cfg.sweepBars);
    engine.setFilterMode (cfg.filterMode);
    engine.setPulseWidth (cfg.pulseWidth);
    engine.setNoiseLevel (cfg.noiseLevel);
    // FX off: isolate osc + filter.
    engine.setDelayWetLevel (0.0f);
    engine.setReverbWetOnDry (0.0f);
    engine.setReverbWetOnDelay (0.0f);
    engine.setPhaserEnabled (false);
    engine.setOutputLevel (1.0f);
    engine.prepare (kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;

    for (int b = 0; b < cfg.blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : cfg.notes)
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);

        engine.render (buffer, midi);
        const auto* p = buffer.getReadPointer (0);
        out.insert (out.end(), p, p + kBlockSize);
    }
    return out;
}

// Deterministic default-patch scenario: engine at ALL defaults, one held chord
// from block 0, 64 blocks through the full chain (arp -> osc -> filter sweep ->
// phaser -> delay -> reverb). Both channels collected.
std::vector<float> renderDefaultScenario (PsyArpEngine& engine)
{
    engine.prepare (kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;

    for (int b = 0; b < 64; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : { 36, 43, 48 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);

        engine.render (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            out.insert (out.end(), p, p + kBlockSize);
        }
    }
    return out;
}

std::vector<float> renderDefaultScenarioSlot (TrackFXSlot& slot)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;
    slot.prepare (spec);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;

    for (int b = 0; b < 64; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : { 36, 43, 48 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);

        buffer.clear();
        slot.process (buffer, midi);
        midi.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* p = buffer.getReadPointer (ch);
            out.insert (out.end(), p, p + kBlockSize);
        }
    }
    return out;
}

// The slot-native FX-off mixer (keeps the filter/arp deterministic).
void configureSlotFxOff (TrackFXSlot& slot)
{
    slot.setInternalParam (1, 1.0f);     // 1 unison voice
    slot.setInternalParam (12, 0.0f);    // delay wet off
    slot.setInternalParam (14, 0.0f);    // reverb wet on dry off
    slot.setInternalParam (15, 0.0f);    // reverb wet on delay off
    slot.setInternalParam (16, 0.0f);    // phaser off
    slot.setInternalParam (19, 1.0f);    // unity out
}

// Renders a prepared slot's channel 0, ~48 blocks of a held chord.
std::vector<float> renderSlot (TrackFXSlot& slot, const std::vector<int>& notes,
                               int blocks = 48)
{
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : notes)
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        buffer.clear();
        slot.process (buffer, midi);
        midi.clear();
        const auto* p = buffer.getReadPointer (0);
        out.insert (out.end(), p, p + kBlockSize);
    }
    return out;
}
} // namespace

// ============================================================================
// Back-compat: a default patch (no new param_N) renders bit-identically
// ============================================================================

TEST (PsyArpBackCompat, EngineDefaultPatchRendersIdenticalToPreChange)
{
    // Re-pinned for slice C (PolyBLEP, NON-ADDITIVE): the naive saw/square
    // default path is gone, so 0x236ff2cd (pre-A, and A's own pin) is no
    // longer the AA render. This value is the AA render's FNV-1a hash,
    // measured 2026-10-05 after the PolyBLEP change.
    constexpr uint32_t kGoldenHash = 0xc2fde655u;

    PsyArpEngine engine;
    const auto out = renderDefaultScenario (engine);

    ASSERT_TRUE (allFinite (out));
    ASSERT_GT (peakOf (out), 0.01) << "scenario must be audibly non-silent";

    const uint32_t h = hashSamples (out);
    EXPECT_EQ (h, kGoldenHash) << "engine default render moved; computed 0x"
                               << std::hex << h;
}

TEST (PsyArpBackCompat, SlotDefaultPatchRendersIdenticalToPreChange)
{
    // Re-pinned for slice C: 0x04d484a5 was the naive (A) slot render; this is
    // the AA render through the same FX-slot protocol.
    constexpr uint32_t kGoldenHash = 0x16766ba1u;

    TrackFXSlot slot ("psyarp");   // all params at their table defaults
    const auto out = renderDefaultScenarioSlot (slot);

    ASSERT_TRUE (allFinite (out));
    ASSERT_GT (peakOf (out), 0.01);

    const uint32_t h = hashSamples (out);
    EXPECT_EQ (h, kGoldenHash) << "slot default render moved; computed 0x"
                               << std::hex << h;
}

TEST (PsyArpBackCompat, ExistingProjectSlotWithoutNewParamsRendersIdentical)
{
    // An EXISTING project's slot tree carries only the params it ever wrote —
    // no param_21/22/23. Loading it leaves the new params at their defaults.
    // Slice C is NON-ADDITIVE, so this now pins the AA render (0x16766ba1),
    // not the pre-change naive 0x04d484a5.
    constexpr uint32_t kGoldenHash = 0x16766ba1u;

    juce::ValueTree tree (juce::Identifier ("FX_SLOT"));
    tree.setProperty (juce::Identifier ("fxType"), juce::String ("psyarp"), nullptr);
    // A couple of pre-change params present, the rest absent (defaults).
    tree.setProperty (juce::Identifier ("param_0"), 0.0, nullptr);   // Osc Shape
    tree.setProperty (juce::Identifier ("param_6"), 600.0, nullptr); // Filter Cutoff

    TrackFXSlot slot ("psyarp");
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;
    slot.prepare (spec);
    slot.loadParamsFromTree (tree);   // no param_21/22/23 -> defaults stay

    const auto vals = slot.getInternalParamValues();
    ASSERT_EQ (vals.size(), 24u);
    EXPECT_FLOAT_EQ (vals[(size_t) 21], 0.0f);   // Filter Type default (LP)
    EXPECT_FLOAT_EQ (vals[(size_t) 22], 0.5f);   // Pulse Width default
    EXPECT_FLOAT_EQ (vals[(size_t) 23], 0.0f);   // Noise Level default (off)

    const auto out = renderDefaultScenarioSlot (slot);
    ASSERT_TRUE (allFinite (out));
    ASSERT_GT (peakOf (out), 0.01);
    EXPECT_EQ (hashSamples (out), kGoldenHash)
        << "an existing project with no new param_N must render as before";
}

// ============================================================================
// Filter modes: HP/BP change the render vs LP, and HP really high-passes
// ============================================================================

TEST (PsyArpFilter, HighpassAndBandpassDifferFromLowpass)
{
    RenderCfg base;
    base.oscShape = 0;          // Saw: full spectrum, so the mode is audible
    base.cutoff = 300.0f;       // above the held note's fundamental (C3 ~131 Hz)
    base.resonance = 4.0f;      // k = 1.5, well damped
    base.notes = { 48 };

    RenderCfg lp = base; lp.filterMode = 0;
    RenderCfg hp = base; hp.filterMode = 1;
    RenderCfg bp = base; bp.filterMode = 2;

    const auto outLp = renderEngine (lp);
    const auto outHp = renderEngine (hp);
    const auto outBp = renderEngine (bp);

    for (const auto* p : { &outLp, &outHp, &outBp })
    {
        ASSERT_TRUE (allFinite (*p));
        ASSERT_GT (peakOf (*p), 0.01) << "every filter mode must render non-silent";
    }

    EXPECT_GT (countDifferent (outLp, outHp), outLp.size() / 4)
        << "HP must change the render vs LP";
    EXPECT_GT (maxAbsDiff (outLp, outHp), 1.0e-2);
    EXPECT_GT (countDifferent (outLp, outBp), outLp.size() / 4)
        << "BP must change the render vs LP";
    EXPECT_GT (maxAbsDiff (outLp, outBp), 1.0e-2);
    EXPECT_GT (countDifferent (outHp, outBp), outHp.size() / 4)
        << "HP and BP are distinct modes";

    // Direction check (not just "bytes differ"): the highpass must leave far
    // less energy below 150 Hz than the lowpass at a 300 Hz cutoff.
    const double lo = lowBandEnergy (outLp, 150.0);
    const double hi = lowBandEnergy (outHp, 150.0);
    EXPECT_LT (hi, 0.5 * lo) << "HP (300 Hz) must reject the low band LP keeps";
}

// ============================================================================
// Osc shapes: Pulse + Noise render non-silent and differ from Saw
// ============================================================================

TEST (PsyArpOsc, PulseAndNoiseRenderNonSilentAndDifferFromSaw)
{
    RenderCfg saw;
    saw.oscShape = 0;
    saw.cutoff = 12000.0f;      // open filter: hear the shape itself
    RenderCfg pulse = saw;  pulse.oscShape = 3;
    RenderCfg noise = saw;  noise.oscShape = 4;

    const auto outSaw   = renderEngine (saw);
    const auto outPulse = renderEngine (pulse);
    const auto outNoise = renderEngine (noise);

    ASSERT_GT (peakOf (outSaw), 0.05);
    ASSERT_TRUE (allFinite (outPulse));
    ASSERT_TRUE (allFinite (outNoise));
    ASSERT_GT (peakOf (outPulse), 0.05) << "Pulse must render non-silent";
    ASSERT_GT (peakOf (outNoise), 0.05) << "Noise must render non-silent";

    // Pulse (duty 0.5) and Noise both differ from Saw across the buffer.
    EXPECT_GT (countDifferent (outSaw, outPulse), outSaw.size() / 4);
    EXPECT_GT (maxAbsDiff (outSaw, outPulse), 1.0e-2);
    EXPECT_GT (countDifferent (outSaw, outNoise), outSaw.size() / 4);
    EXPECT_GT (maxAbsDiff (outSaw, outNoise), 1.0e-2);
    EXPECT_GT (countDifferent (outPulse, outNoise), outPulse.size() / 4);
}

TEST (PsyArpOsc, PulseWidthChangesThePulse)
{
    RenderCfg narrow;
    narrow.oscShape = 3;
    narrow.cutoff = 12000.0f;
    narrow.pulseWidth = 0.1f;
    RenderCfg wide = narrow;
    wide.pulseWidth = 0.9f;

    const auto a = renderEngine (narrow);
    const auto b = renderEngine (wide);

    ASSERT_GT (peakOf (a), 0.05);
    ASSERT_GT (peakOf (b), 0.05);
    EXPECT_GT (countDifferent (a, b), a.size() / 4)
        << "Pulse Width must actually change the pulse duty";
    EXPECT_GT (maxAbsDiff (a, b), 1.0e-2);
}

// ============================================================================
// Noise headroom: the noise-augmented sum is bounded and finite (lesson 45)
// ============================================================================

TEST (PsyArpNoise, NoiseAugmentedMultiVoiceSumStaysBoundedAndFinite)
{
    // Worst case: 4 unison voices of saw (sum up to 1.0) + full white noise +
    // unity output level, FX off. The memoryless soft ceiling must keep the
    // oscillator sum bounded and everything finite.
    RenderCfg worst;
    worst.oscShape = 0;
    worst.uniVoices = 4;
    worst.detuneCents = 12.0f;
    worst.cutoff = 18000.0f;
    worst.resonance = 4.0f;
    worst.noiseLevel = 1.0f;
    worst.notes = { 36, 43, 48 };
    worst.blocks = 64;

    const auto out = renderEngine (worst);
    ASSERT_TRUE (allFinite (out));
    EXPECT_GT (peakOf (out), 0.05);
    // Soft-ceiling asymptote is 1.5 pre-filter; with the (nearly) open filter
    // this stays well under 2.0 — no runaway sum.
    EXPECT_LT (peakOf (out), 2.0);
}

// ============================================================================
// Param surface: new defs, clamping, round-trip, and BOTH wiring sites
// ============================================================================

TEST (PsyArpParams, NewParamDefsAndClamp)
{
    TrackFXSlot slot ("psyarp");
    const auto defs = slot.getInternalParamDefs();
    ASSERT_EQ ((int) defs.size(), 24);

    // Osc Shape's max widened 2 -> 4; defaults and lower rows unchanged.
    EXPECT_EQ (defs[0].name, juce::String ("Osc Shape"));
    EXPECT_NEAR (defs[0].defaultValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[0].minValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[0].maxValue, 4.0f, 1e-5f);

    // Appended rows 21..23, verbatim table values.
    EXPECT_EQ (defs[21].index, 21);
    EXPECT_EQ (defs[21].name, juce::String ("Filter Type"));
    EXPECT_NEAR (defs[21].defaultValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[21].minValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[21].maxValue, 2.0f, 1e-5f);
    EXPECT_EQ (defs[22].name, juce::String ("Pulse Width"));
    EXPECT_NEAR (defs[22].defaultValue, 0.5f, 1e-5f);
    EXPECT_NEAR (defs[22].minValue, 0.05f, 1e-5f);
    EXPECT_NEAR (defs[22].maxValue, 0.95f, 1e-5f);
    EXPECT_EQ (defs[23].name, juce::String ("Noise Level"));
    EXPECT_NEAR (defs[23].defaultValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[23].minValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[23].maxValue, 1.0f, 1e-5f);

    // Every new-param write clamps to its def range (lesson 23).
    slot.setInternalParam (0, 5.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 0], 4.0f, 1e-5f);
    slot.setInternalParam (21, 9.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 21], 2.0f, 1e-5f);
    slot.setInternalParam (21, -4.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 21], 0.0f, 1e-5f);
    slot.setInternalParam (22, 0.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 22], 0.05f, 1e-5f);
    slot.setInternalParam (22, 5.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 22], 0.95f, 1e-5f);
    slot.setInternalParam (23, 3.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 23], 1.0f, 1e-5f);
    slot.setInternalParam (23, -1.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 23], 0.0f, 1e-5f);
}

TEST (PsyArpParams, NewParamsRoundTripAndSelectNoiseShape)
{
    TrackFXSlot slot ("psyarp");
    configureSlotFxOff (slot);

    // Real-unit round-trip through the FX-slot param path.
    slot.setInternalParam (21, 2.0f);
    slot.setInternalParam (22, 0.8f);
    slot.setInternalParam (23, 0.6f);
    slot.setInternalParam (0, 4.0f);        // Noise shape: accepted (max 4)
    const auto vals = slot.getInternalParamValues();
    EXPECT_NEAR (vals[(size_t) 21], 2.0f, 1e-5f);
    EXPECT_NEAR (vals[(size_t) 22], 0.8f, 1e-5f);
    EXPECT_NEAR (vals[(size_t) 23], 0.6f, 1e-5f);
    EXPECT_NEAR (vals[(size_t) 0], 4.0f, 1e-5f);

    // The Noise shape + Noise Level actually reach the DSP: the render is
    // finite, non-silent, and differs from the same slot's Saw render.
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;
    slot.prepare (spec);
    const auto noiseOut = renderSlot (slot, { 36, 43, 48 });
    ASSERT_TRUE (allFinite (noiseOut));
    ASSERT_GT (peakOf (noiseOut), 0.05);

    TrackFXSlot sawSlot ("psyarp");
    configureSlotFxOff (sawSlot);
    sawSlot.prepare (spec);
    const auto sawOut = renderSlot (sawSlot, { 36, 43, 48 });

    EXPECT_GT (countDifferent (noiseOut, sawOut), noiseOut.size() / 4);
}

TEST (PsyArpParams, FilterModeWiredAtPrepareAndLive)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;

    // (1) PREPARE path: param 21 written BEFORE prepare must shape the render.
    TrackFXSlot lpSlot ("psyarp");
    configureSlotFxOff (lpSlot);
    lpSlot.setInternalParam (0, 0.0f);      // Saw
    lpSlot.setInternalParam (6, 300.0f);
    lpSlot.setInternalParam (8, 16.0f);
    lpSlot.setInternalParam (21, 0.0f);     // LP
    lpSlot.prepare (spec);

    TrackFXSlot hpSlot ("psyarp");
    configureSlotFxOff (hpSlot);
    hpSlot.setInternalParam (0, 0.0f);
    hpSlot.setInternalParam (6, 300.0f);
    hpSlot.setInternalParam (8, 16.0f);
    hpSlot.setInternalParam (21, 1.0f);     // HP
    hpSlot.prepare (spec);

    const auto lpOut = renderSlot (lpSlot, { 48 });
    const auto hpOut = renderSlot (hpSlot, { 48 });
    ASSERT_GT (peakOf (lpOut), 0.05);
    ASSERT_GT (peakOf (hpOut), 0.05);
    EXPECT_GT (maxAbsDiff (lpOut, hpOut), 1.0e-2)
        << "param 21 written before prepare must reach the DSP";
    EXPECT_LT (lowBandEnergy (hpOut, 150.0), 0.5 * lowBandEnergy (lpOut, 150.0));

    // (2) LIVE path: switching param 21 mid-render changes the ongoing output.
    TrackFXSlot live ("psyarp");
    configureSlotFxOff (live);
    live.setInternalParam (0, 0.0f);
    live.setInternalParam (6, 300.0f);
    live.setInternalParam (8, 16.0f);
    live.prepare (spec);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> early, late;
    for (int b = 0; b < 96; ++b)
    {
        midi.clear();
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        buffer.clear();
        live.process (buffer, midi);
        midi.clear();
        const auto* p = buffer.getReadPointer (0);
        if (b < 44) early.insert (early.end(), p, p + kBlockSize);
        else if (b >= 52) late.insert (late.end(), p, p + kBlockSize);

        if (b == 48) live.setInternalParam (21, 1.0f);   // live LP -> HP
    }
    ASSERT_GT (peakOf (early), 0.05);
    ASSERT_GT (peakOf (late), 0.05);
    EXPECT_GT (maxAbsDiff (early, late), 1.0e-2)
        << "live setInternalParam(21) must change the render";
    EXPECT_LT (lowBandEnergy (late, 150.0), 0.5 * lowBandEnergy (early, 150.0));
}
