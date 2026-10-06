// PsyFmEngine post-carrier multimode filter (slice B of
// docs/plans/2026-10-05-internal-synth-expansion.md).
//
// The change is ADDITIVE: params 33..37 are APPENDED to the psy_fm def table
// (no existing index or range moves). A project with no new param_N must render
// bit-identically, which the engine guarantees with an explicit bypass: a
// voice's per-voice filter is only PROCESSED when
//   cutoff < 19999 Hz || key-track != 0 || env-amount != 0
// (a 20 kHz LP is not transparent, so "default = neutral" cannot ride on the
// coefficient math). The back-compat tests pin the exact pre-change buffer
// (FNV-1a over the float bits) captured from the engine BEFORE this change;
// any DSP edit on the default path moves the hash.
//
// Slice C (PolyBLEP oscillator anti-aliasing, 2026-10-05) is NON-ADDITIVE but
// touches ONLY psyarp/sub_synth: psy_fm's operators are pure sine
// (`std::sin` in PsyFmOperator::renderBlock), which carries no step
// discontinuity, so nothing here changes and these hashes stay as pinned.
#include <gtest/gtest.h>

#include "engine/PsyFmAlgorithms.h"
#include "engine/PsyFmEngine.h"
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

double rmsOf (const std::vector<float>& x)
{
    if (x.empty()) return 0.0;
    double e = 0.0;
    for (float v : x) e += (double) v * (double) v;
    return std::sqrt (e / (double) x.size());
}

bool allFinite (const std::vector<float>& x)
{
    for (float v : x)
        if (! std::isfinite (v)) return false;
    return true;
}

size_t countDifferent (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size());
    size_t d = 0;
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) ++d;
    return d;
}

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size());
    double m = 0.0;
    for (size_t i = 0; i < n; ++i)
        m = std::max (m, (double) std::fabs ((double) a[i] - (double) b[i]));
    return m;
}

// One-pole probes. An 18 dB/oct cascade is steep enough that the other band
// does not leak through and masquerade as energy where there is none.
struct OnePoleLP
{
    double a = 0.0, y = 0.0;
    void setFc (double fc) { a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * fc / kSampleRate); }
    double process (double x) { y += a * (x - y); return y; }
};

struct OnePoleHP
{
    double a = 0.0, y = 0.0, xp = 0.0;
    void setFc (double fc) { a = std::exp (-2.0 * juce::MathConstants<double>::pi * fc / kSampleRate); }
    double process (double x) { y = a * (y + x - xp); xp = x; return y; }
};

double lowBandEnergy (const std::vector<float>& x, double fc)
{
    OnePoleLP s0, s1, s2;
    s0.setFc (fc); s1.setFc (fc); s2.setFc (fc);
    double e = 0.0;
    for (float v : x)
    {
        const double y0 = s0.process ((double) v);
        const double y1 = s1.process (y0);
        const double y2 = s2.process (y1);
        e += y2 * y2;
    }
    return e;
}

double highBandEnergy (const std::vector<float>& x, double fc)
{
    OnePoleHP s0, s1, s2;
    s0.setFc (fc); s1.setFc (fc); s2.setFc (fc);
    double e = 0.0;
    for (float v : x)
    {
        const double y0 = s0.process ((double) v);
        const double y1 = s1.process (y0);
        const double y2 = s2.process (y1);
        e += y2 * y2;
    }
    return e;
}

// ── Deterministic default-patch scenarios (back-compat pins) ────────────────

// Engine with NO explicit params at all (the raw engine defaults), one held
// chord from block 0, 64 blocks, both channels collected.
std::vector<float> renderDefaultScenarioEngine (PsyFmEngine& engine)
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

// The real wiring: TrackFXSlot("psy_fm") at ALL table defaults -> algorithm
// preset 0 (growlBass) + the default feedback-LFO matrix route, 64 blocks.
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

// ── Audibility scenario (engine-direct, channel 0, FX-free) ─────────────────

struct FmCfg
{
    int   algo       = 0;                          // 0=growlBass
    float ratios[6]  = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float attack     = 0.005f;
    float decay      = 0.30f;
    float sustain    = 0.70f;
    float release    = 0.20f;
    float output     = 1.0f;
    float cutoff     = 20000.0f;
    float resonance  = 0.7f;
    int   filterType = 0;                          // 0=LP
    float keyTrack   = 0.0f;
    float envAmount  = 0.0f;
    int   blocks     = 96;
    std::vector<int> notes = { 60 };
};

std::vector<float> renderEngine (const FmCfg& cfg)
{
    PsyFmEngine engine;
    switch (cfg.algo)
    {
        case 1: engine.setAlgorithm (acidLeadAlgorithm); break;
        case 2: engine.setAlgorithm (metallicPluckAlgorithm); break;
        case 3: engine.setAlgorithm (riserAlgorithm); break;
        default: engine.setAlgorithm (growlBassAlgorithm); break;
    }
    engine.setBaseRatios (cfg.ratios);
    for (int op = 0; op < 6; ++op)
        engine.setOpEnvelope (op, { cfg.attack, cfg.decay, cfg.sustain, cfg.release });
    engine.setOutputLevel (cfg.output);
    engine.setFilterParam (PsyFmEngine::FilterCutoff,    cfg.cutoff);
    engine.setFilterParam (PsyFmEngine::FilterResonance, cfg.resonance);
    engine.setFilterParam (PsyFmEngine::FilterType,      (float) cfg.filterType);
    engine.setFilterParam (PsyFmEngine::FilterKeyTrack,  cfg.keyTrack);
    engine.setFilterParam (PsyFmEngine::FilterEnvAmount, cfg.envAmount);
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
} // namespace

// ============================================================================
// Back-compat: a default patch (no new param_N) renders bit-identically
// ============================================================================

TEST (PsyFmBackCompat, EngineDefaultPatchRendersIdenticalToPreChange)
{
    // Captured from the engine's all-defaults path BEFORE this slice landed
    // (2026-10-05); pinned so any DSP edit on the neutral path is loud.
    constexpr uint32_t kGoldenHash = 0x87f32b95u;

    PsyFmEngine engine;
    const auto out = renderDefaultScenarioEngine (engine);

    ASSERT_TRUE (allFinite (out));
    ASSERT_GT (peakOf (out), 0.01) << "scenario must be audibly non-silent";

    const uint32_t h = hashSamples (out);
    EXPECT_EQ (h, kGoldenHash) << "engine default render moved; computed 0x"
                               << std::hex << h;
}

TEST (PsyFmBackCompat, SlotDefaultPatchRendersIdenticalToPreChange)
{
    constexpr uint32_t kGoldenHash = 0xba3c4b19u;

    TrackFXSlot slot ("psy_fm");   // all params at their table defaults
    const auto out = renderDefaultScenarioSlot (slot);

    ASSERT_TRUE (allFinite (out));
    ASSERT_GT (peakOf (out), 0.01);

    const uint32_t h = hashSamples (out);
    EXPECT_EQ (h, kGoldenHash) << "slot default render moved; computed 0x"
                               << std::hex << h;
}

TEST (PsyFmBackCompat, ExistingProjectSlotWithoutNewParamsRendersIdentical)
{
    // An EXISTING project's slot tree carries only the params it ever wrote —
    // no param_33..37. Loading it must leave the new params at their defaults
    // and render bit-identically to the pre-change engine.
    constexpr uint32_t kGoldenHash = 0xba3c4b19u;

    juce::ValueTree tree (juce::Identifier ("FX_SLOT"));
    tree.setProperty (juce::Identifier ("fxType"), juce::String ("psy_fm"), nullptr);
    // A couple of pre-change params present AT THEIR DEF DEFAULTS, the rest
    // absent — so this pins the SAME render as the all-defaults slot above
    // (proving the new params' absence is what keeps it identical).
    tree.setProperty (juce::Identifier ("param_6"), 0.0, nullptr);   // Feedback (def)
    tree.setProperty (juce::Identifier ("param_31"), 0.4, nullptr);  // Output Level (def)

    TrackFXSlot slot ("psy_fm");
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;
    slot.prepare (spec);
    slot.loadParamsFromTree (tree);   // no param_33..37 -> defaults stay

    const auto vals = slot.getInternalParamValues();
    ASSERT_EQ (vals.size(), 38u);
    EXPECT_FLOAT_EQ (vals[(size_t) 33], 20000.0f);   // Filter Cutoff (neutral)
    EXPECT_FLOAT_EQ (vals[(size_t) 34], 0.7f);       // Filter Resonance
    EXPECT_FLOAT_EQ (vals[(size_t) 35], 0.0f);       // Filter Type (LP)
    EXPECT_FLOAT_EQ (vals[(size_t) 36], 0.0f);       // Filter Key Track
    EXPECT_FLOAT_EQ (vals[(size_t) 37], 0.0f);       // Filter Env Amount

    const auto out = renderDefaultScenarioSlot (slot);
    ASSERT_TRUE (allFinite (out));
    ASSERT_GT (peakOf (out), 0.01);
    EXPECT_EQ (hashSamples (out), kGoldenHash)
        << "an existing project with no new param_N must render as before";
}

// ============================================================================
// Audibility: cutoff, key-track, env-amount
// ============================================================================

TEST (PsyFmFilter, LowCutoffAttenuatesTheHighBand)
{
    // Ratios that guarantee FM sidebands well above the 1500 Hz probe.
    FmCfg open;
    open.ratios[4] = 3.0f;      // op5 (modulator into op4)
    open.ratios[5] = 2.0f;      // op6 (feedback source)
    open.cutoff = 20000.0f;     // >= 19999 -> the filter is bypassed
    open.notes = { 60 };
    open.blocks = 48;

    FmCfg closed = open;
    closed.cutoff = 400.0f;     // lowpass well above the note fundamental
    closed.resonance = 0.7f;

    const auto outOpen   = renderEngine (open);
    const auto outClosed = renderEngine (closed);

    ASSERT_TRUE (allFinite (outOpen));
    ASSERT_TRUE (allFinite (outClosed));
    ASSERT_GT (peakOf (outOpen), 0.01);
    ASSERT_GT (peakOf (outClosed), 0.01) << "the filtered render must stay audible";

    EXPECT_GT (countDifferent (outOpen, outClosed), outOpen.size() / 4)
        << "an engaged cutoff must change the render";
    EXPECT_GT (maxAbsDiff (outOpen, outClosed), 1.0e-2);

    const double hiOpen   = highBandEnergy (outOpen, 1500.0);
    const double hiClosed = highBandEnergy (outClosed, 1500.0);
    EXPECT_GT (hiOpen, 0.0);
    EXPECT_LT (hiClosed, 0.3 * hiOpen)
        << "a 400 Hz lowpass must attenuate the >1500 Hz band vs the bypass";
}

TEST (PsyFmFilter, KeyTrackUpShiftsTheCutoffForAHighNote)
{
    FmCfg base;
    base.cutoff = 400.0f;
    base.ratios[4] = 3.0f;
    base.ratios[5] = 2.0f;
    base.blocks = 48;

    // Same HIGH note, key-track off vs on: eff = 400 * 2^((84-60)/12) = 1600 Hz.
    FmCfg hiOff = base; hiOff.notes = { 84 }; hiOff.keyTrack = 0.0f;
    FmCfg hiOn  = base; hiOn.notes  = { 84 }; hiOn.keyTrack  = 1.0f;

    const auto outHiOff = renderEngine (hiOff);
    const auto outHiOn  = renderEngine (hiOn);
    ASSERT_GT (peakOf (outHiOff), 0.01);
    ASSERT_GT (peakOf (outHiOn), 0.01);

    const double hiOffE = highBandEnergy (outHiOff, 1500.0);
    const double hiOnE  = highBandEnergy (outHiOn, 1500.0);
    EXPECT_GT (hiOnE, 2.0 * hiOffE)
        << "key-track must open the filter for a note two octaves up";

    // Same LOW note, key-track on lowers eff 400 -> 100 Hz: darker/quieter.
    FmCfg loOff = base; loOff.notes = { 36 }; loOff.keyTrack = 0.0f;
    FmCfg loOn  = base; loOn.notes  = { 36 }; loOn.keyTrack  = 1.0f;

    const auto outLoOff = renderEngine (loOff);
    const auto outLoOn  = renderEngine (loOn);
    ASSERT_GT (peakOf (outLoOff), 0.01);
    ASSERT_GT (peakOf (outLoOn), 0.01);

    EXPECT_LT (rmsOf (outLoOn), 0.8 * rmsOf (outLoOff))
        << "key-track must close the filter for a note two octaves down";
}

TEST (PsyFmFilter, EnvAmountOpensTheFilterOverTheNote)
{
    FmCfg base;
    base.cutoff = 250.0f;
    base.ratios[4] = 3.0f;
    base.ratios[5] = 2.0f;
    base.attack = 0.001f;
    base.decay = 0.30f;
    base.sustain = 0.20f;      // settles at 0.2 -> env-amount settles low
    base.blocks = 96;

    FmCfg off = base; off.envAmount = 0.0f;
    FmCfg on  = base; on.envAmount  = 1.0f;

    const auto outOff = renderEngine (off);
    const auto outOn  = renderEngine (on);
    ASSERT_GT (peakOf (outOff), 0.01);
    ASSERT_GT (peakOf (outOn), 0.01);

    // Early window ~ blocks 1..8 (11..105 ms): the amp envelope is high, so
    // env-amount must have ALSO opened the cutoff.
    const size_t earlyBegin = (size_t) kBlockSize * 1;
    const size_t earlyEnd   = (size_t) kBlockSize * 8;
    const std::vector<float> earlyOff (outOff.begin() + (long) earlyBegin, outOff.begin() + (long) earlyEnd);
    const std::vector<float> earlyOn  (outOn.begin()  + (long) earlyBegin, outOn.begin()  + (long) earlyEnd);

    const double earlyOffE = highBandEnergy (earlyOff, 1200.0);
    const double earlyOnE  = highBandEnergy (earlyOn, 1200.0);
    EXPECT_GT (earlyOnE, 2.0 * earlyOffE)
        << "env-amount must open the cutoff while the envelope is high";

    // Late window ~ blocks 72..95 (0.83..1.11 s): the envelope has decayed to
    // sustain, so the offset has collapsed back toward the 250 Hz base.
    const size_t lateBegin = (size_t) kBlockSize * 72;
    const size_t lateEnd   = (size_t) kBlockSize * 95;
    const std::vector<float> lateOn (outOn.begin() + (long) lateBegin, outOn.begin() + (long) lateEnd);

    EXPECT_LT (highBandEnergy (lateOn, 1200.0), 0.5 * earlyOnE)
        << "the env-driven cutoff offset must close over the note";
}

// ============================================================================
// Param surface: defs, clamping, round-trip, and BOTH wiring sites
// ============================================================================

TEST (PsyFmParams, NewParamDefsAndClamp)
{
    TrackFXSlot slot ("psy_fm");
    const auto defs = slot.getInternalParamDefs();
    ASSERT_EQ ((int) defs.size(), 38);

    EXPECT_EQ (defs[33].index, 33);
    EXPECT_EQ (defs[33].name, juce::String ("Filter Cutoff"));
    EXPECT_NEAR (defs[33].defaultValue, 20000.0f, 1e-5f);
    EXPECT_NEAR (defs[33].minValue, 20.0f, 1e-5f);
    EXPECT_NEAR (defs[33].maxValue, 20000.0f, 1e-5f);

    EXPECT_EQ (defs[34].name, juce::String ("Filter Resonance"));
    EXPECT_NEAR (defs[34].defaultValue, 0.7f, 1e-5f);
    EXPECT_NEAR (defs[34].minValue, 0.1f, 1e-5f);
    EXPECT_NEAR (defs[34].maxValue, 10.0f, 1e-5f);

    EXPECT_EQ (defs[35].name, juce::String ("Filter Type"));
    EXPECT_NEAR (defs[35].defaultValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[35].maxValue, 2.0f, 1e-5f);

    EXPECT_EQ (defs[36].name, juce::String ("Filter Key Track"));
    EXPECT_NEAR (defs[36].defaultValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[36].maxValue, 1.0f, 1e-5f);

    EXPECT_EQ (defs[37].name, juce::String ("Filter Env Amount"));
    EXPECT_NEAR (defs[37].defaultValue, 0.0f, 1e-5f);
    EXPECT_NEAR (defs[37].maxValue, 1.0f, 1e-5f);

    // Every new-param write clamps to its def range (lesson 23).
    slot.setInternalParam (33, 90000.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 33], 20000.0f, 1e-5f);
    slot.setInternalParam (33, 0.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 33], 20.0f, 1e-5f);
    slot.setInternalParam (34, 99.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 34], 10.0f, 1e-5f);
    slot.setInternalParam (34, 0.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 34], 0.1f, 1e-5f);
    slot.setInternalParam (35, 7.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 35], 2.0f, 1e-5f);
    slot.setInternalParam (35, -3.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 35], 0.0f, 1e-5f);
    slot.setInternalParam (36, 4.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 36], 1.0f, 1e-5f);
    slot.setInternalParam (37, -1.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 37], 0.0f, 1e-5f);
    slot.setInternalParam (37, 5.0f);
    EXPECT_NEAR (slot.getInternalParamValues()[(size_t) 37], 1.0f, 1e-5f);

    // The ENGINE clamps to the same def ranges (a bare engine has no slot).
    PsyFmEngine engine;
    engine.setFilterParam (PsyFmEngine::FilterCutoff, 1.0e9f);
    EXPECT_FLOAT_EQ (engine.getFilterParam (PsyFmEngine::FilterCutoff), 20000.0f);
    engine.setFilterParam (PsyFmEngine::FilterCutoff, -5.0f);
    EXPECT_FLOAT_EQ (engine.getFilterParam (PsyFmEngine::FilterCutoff), 20.0f);
    engine.setFilterParam (PsyFmEngine::FilterResonance, 1.0e9f);
    EXPECT_FLOAT_EQ (engine.getFilterParam (PsyFmEngine::FilterResonance), 10.0f);
    engine.setFilterParam (PsyFmEngine::FilterType, 9.0f);
    EXPECT_FLOAT_EQ (engine.getFilterParam (PsyFmEngine::FilterType), 2.0f);
    engine.setFilterParam (PsyFmEngine::FilterKeyTrack, -1.0f);
    EXPECT_FLOAT_EQ (engine.getFilterParam (PsyFmEngine::FilterKeyTrack), 0.0f);
    engine.setFilterParam (PsyFmEngine::FilterEnvAmount, 3.0f);
    EXPECT_FLOAT_EQ (engine.getFilterParam (PsyFmEngine::FilterEnvAmount), 1.0f);
    // Out-of-range index is a no-op, never a write.
    engine.setFilterParam (99, 1.0f);
    EXPECT_FLOAT_EQ (engine.getFilterParam (PsyFmEngine::FilterCutoff), 20.0f);
    EXPECT_FLOAT_EQ (engine.getFilterParam (99), 0.0f);

    // Defaults are the neutral bypass values.
    PsyFmEngine fresh;
    EXPECT_FLOAT_EQ (fresh.getFilterParam (PsyFmEngine::FilterCutoff), 20000.0f);
    EXPECT_FLOAT_EQ (fresh.getFilterParam (PsyFmEngine::FilterResonance), 0.7f);
    EXPECT_FLOAT_EQ (fresh.getFilterParam (PsyFmEngine::FilterType), 0.0f);
    EXPECT_FLOAT_EQ (fresh.getFilterParam (PsyFmEngine::FilterKeyTrack), 0.0f);
    EXPECT_FLOAT_EQ (fresh.getFilterParam (PsyFmEngine::FilterEnvAmount), 0.0f);
}

TEST (PsyFmParams, NewParamsRoundTripThroughTheSlot)
{
    TrackFXSlot slot ("psy_fm");
    slot.setInternalParam (33, 400.0f);
    slot.setInternalParam (34, 5.0f);
    slot.setInternalParam (35, 1.0f);
    slot.setInternalParam (36, 1.0f);
    slot.setInternalParam (37, 0.75f);

    const auto vals = slot.getInternalParamValues();
    ASSERT_EQ (vals.size(), 38u);
    EXPECT_FLOAT_EQ (vals[(size_t) 33], 400.0f);
    EXPECT_FLOAT_EQ (vals[(size_t) 34], 5.0f);
    EXPECT_FLOAT_EQ (vals[(size_t) 35], 1.0f);
    EXPECT_FLOAT_EQ (vals[(size_t) 36], 1.0f);
    EXPECT_FLOAT_EQ (vals[(size_t) 37], 0.75f);

    // The values reached the LIVE engine's atomics.
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;
    slot.prepare (spec);
    auto* engine = slot.psyFmEngine();
    ASSERT_NE (engine, nullptr);
    EXPECT_FLOAT_EQ (engine->getFilterParam (PsyFmEngine::FilterCutoff), 400.0f);
    EXPECT_FLOAT_EQ (engine->getFilterParam (PsyFmEngine::FilterResonance), 5.0f);
    EXPECT_FLOAT_EQ (engine->getFilterParam (PsyFmEngine::FilterType), 1.0f);
    EXPECT_FLOAT_EQ (engine->getFilterParam (PsyFmEngine::FilterKeyTrack), 1.0f);
    EXPECT_FLOAT_EQ (engine->getFilterParam (PsyFmEngine::FilterEnvAmount), 0.75f);

    // And survives a tree round-trip (param_N write + loadParamsFromTree).
    juce::ValueTree tree (juce::Identifier ("FX_SLOT"));
    tree.setProperty (juce::Identifier ("fxType"), juce::String ("psy_fm"), nullptr);
    tree.setProperty (juce::Identifier ("param_33"), 1200.0, nullptr);
    tree.setProperty (juce::Identifier ("param_35"), 2.0, nullptr);
    TrackFXSlot restored ("psy_fm");
    restored.prepare (spec);
    restored.loadParamsFromTree (tree);
    const auto rv = restored.getInternalParamValues();
    EXPECT_FLOAT_EQ (rv[(size_t) 33], 1200.0f);
    EXPECT_FLOAT_EQ (rv[(size_t) 35], 2.0f);
    EXPECT_FLOAT_EQ (rv[(size_t) 34], 0.7f);   // absent -> default
    ASSERT_NE (restored.psyFmEngine(), nullptr);
    EXPECT_FLOAT_EQ (restored.psyFmEngine()->getFilterParam (PsyFmEngine::FilterCutoff), 1200.0f);
    EXPECT_FLOAT_EQ (restored.psyFmEngine()->getFilterParam (PsyFmEngine::FilterType), 2.0f);
}

TEST (PsyFmParams, FilterWiredAtPrepareAndLive)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = (juce::uint32) kBlockSize;
    spec.numChannels = 2;

    // (1) PREPARE path: param 33 written BEFORE prepare must shape the render.
    TrackFXSlot openSlot ("psy_fm");
    openSlot.setInternalParam (4, 3.0f);       // op5 ratio: sidebands
    openSlot.setInternalParam (5, 2.0f);       // op6 ratio
    openSlot.setInternalParam (31, 1.0f);      // unity output
    openSlot.prepare (spec);

    TrackFXSlot filtSlot ("psy_fm");
    filtSlot.setInternalParam (4, 3.0f);
    filtSlot.setInternalParam (5, 2.0f);
    filtSlot.setInternalParam (31, 1.0f);
    filtSlot.setInternalParam (33, 400.0f);    // lowpass
    filtSlot.prepare (spec);

    auto renderSlot = [] (TrackFXSlot& s, const std::vector<int>& notes, int blocks)
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
            s.process (buffer, midi);
            midi.clear();
            const auto* p = buffer.getReadPointer (0);
            out.insert (out.end(), p, p + kBlockSize);
        }
        return out;
    };

    const auto openOut = renderSlot (openSlot, { 60 }, 48);
    const auto filtOut = renderSlot (filtSlot, { 60 }, 48);
    ASSERT_GT (peakOf (openOut), 0.01);
    ASSERT_GT (peakOf (filtOut), 0.01);
    EXPECT_GT (maxAbsDiff (openOut, filtOut), 1.0e-2)
        << "param 33 written before prepare must reach the DSP";
    EXPECT_LT (highBandEnergy (filtOut, 1500.0), 0.3 * highBandEnergy (openOut, 1500.0));

    // (2) LIVE path: switching param 33 mid-render changes the ongoing output.
    TrackFXSlot live ("psy_fm");
    live.setInternalParam (31, 1.0f);
    live.prepare (spec);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> early, late;
    for (int b = 0; b < 96; ++b)
    {
        midi.clear();
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        buffer.clear();
        live.process (buffer, midi);
        midi.clear();
        const auto* p = buffer.getReadPointer (0);
        if (b < 44) early.insert (early.end(), p, p + kBlockSize);
        else if (b >= 52) late.insert (late.end(), p, p + kBlockSize);

        if (b == 48) live.setInternalParam (33, 400.0f);   // live: engage LP
    }
    ASSERT_GT (peakOf (early), 0.01);
    ASSERT_GT (peakOf (late), 0.01);
    EXPECT_GT (maxAbsDiff (early, late), 1.0e-2)
        << "live setInternalParam(33) must change the render";
    EXPECT_LT (highBandEnergy (late, 1500.0), 0.3 * highBandEnergy (early, 1500.0));
}

// ============================================================================
// Direct filter-mode coverage on the voice carrier (engine level)
// ============================================================================

TEST (PsyFmFilter, HighpassAndBandpassDifferFromLowpass)
{
    FmCfg base;
    base.cutoff = 400.0f;
    base.ratios[4] = 3.0f;
    base.ratios[5] = 2.0f;
    base.notes = { 60 };
    base.blocks = 48;

    FmCfg lp = base; lp.filterType = 0;
    FmCfg hp = base; hp.filterType = 1;
    FmCfg bp = base; bp.filterType = 2;

    const auto outLp = renderEngine (lp);
    const auto outHp = renderEngine (hp);
    const auto outBp = renderEngine (bp);

    for (const auto* p : { &outLp, &outHp, &outBp })
    {
        ASSERT_TRUE (allFinite (*p));
        ASSERT_GT (peakOf (*p), 0.01) << "every filter mode must render non-silent";
    }

    EXPECT_GT (maxAbsDiff (outLp, outHp), 1.0e-2);
    EXPECT_GT (maxAbsDiff (outLp, outBp), 1.0e-2);
    EXPECT_GT (maxAbsDiff (outHp, outBp), 1.0e-2);

    // Direction check: at a 400 Hz cutoff the HP must leave far less energy
    // below the note fundamental than the LP does.
    EXPECT_LT (lowBandEnergy (outHp, 150.0), 0.5 * lowBandEnergy (outLp, 150.0));
}
