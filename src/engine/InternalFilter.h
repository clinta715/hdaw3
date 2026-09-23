#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace HDAW {

// The internal state-variable filter DSP, shared by the per-track FX slot
// (TrackFXSlot's ActiveType::Filter) and the fx return (FxBusProcessor's
// "filter" chain).
//
// Extracted from TrackFXSlot::ManualSVF (plan 2026-09-23, slice E) with the
// arithmetic copied VERBATIM: the per-channel integrator states, the TPT
// coefficient solve (g/k/Dinv) and the per-sample loop are the same code, so a
// track's internal filter renders what it rendered before. That solve was
// verified numerically against the analytic loop solve on 2026-09-09 — the
// previous hand-rolled variant omitted the damping term in v3, mis-derived v2
// (spurious ic1 feed-in, missing the a2*v3 term) and returned k*v2 for HP, so
// the filter never actually swept its cutoff (the AutomationPidRouting
// regression). Do not "improve" it.
//
// Why the bus needs it: a bus return could not be high-passed — the bus fxTypes
// were reverb/delay/eq/compressor and the eq is a single PEAK filter — so the
// classic dub move (rolling the lows off the delay return so the repeats do not
// muddy the bass) was inexpressible. The eq is still a peak filter; this class
// is what makes `fxType:"filter"` a real high-pass/low-pass/band-pass return.
//
// Threading: the three params are plain floats written by the command/message
// thread and read by the audio thread — the benign-tear class ManualSVF
// documented (a torn read can only pick an old or new coefficient set, both
// stable). Nothing here allocates or locks: prepare() only computes
// coefficients, processSample()/process() touch no memory but the block.
//
// Safety: every param write is clamped to the def (lesson 23) — Cutoff
// 20..20000 Hz, Mode 0..2, Resonance 0.1..10 — so a legacy or hand-edited
// project file cannot push a coefficient out of the numerically safe range.
// Mode is an int enum, so a fractional value is ROUNDED (the TrackFXSlot
// contract: reads report what the DSP actually runs) rather than falling
// through the mode switch to lowpass.
class InternalFilter
{
public:
    // Mode is an int enum: 0=lowpass, 1=highpass, 2=bandpass (the def range).
    enum Mode { Lowpass = 0, Highpass = 1, Bandpass = 2 };

    // Param indices. TrackFXSlot::getParamDefsForType("filter") builds its table
    // from paramDefs() below and common/BusFxDefs.h mirrors it, so track FX, bus
    // FX and both surfaces share one numbering.
    enum ParamIndex { Cutoff = 0, ModeParam = 1, Resonance = 2 };

    static constexpr int kNumParams = 3;

    struct ParamDef { const char* name; float def; float min; float max; };

    // The filter's names/ranges/defaults — ONE source of truth: TrackFXSlot
    // derives its InternalParamDef list from this (so the DSP's clamp and the
    // advertised surface range cannot drift apart), and the bus table
    // (common/BusFxDefs.h) is pinned to it by BusFxParam.DefTableMatchesTrackFxDefs.
    static const std::array<ParamDef, (size_t) kNumParams>& paramDefs()
    {
        static const std::array<ParamDef, (size_t) kNumParams> defs = { {
            { "Cutoff",    1000.0f,  20.0f, 20000.0f },
            { "Mode",         0.0f,   0.0f,     2.0f },
            { "Resonance",    0.7f,   0.1f,    10.0f },
        } };
        return defs;
    }

    static float clampParam(int index, float value)
    {
        if (index < 0 || index >= kNumParams) return value;
        const auto& d = paramDefs()[(size_t) index];
        return juce::jlimit(d.min, d.max, value);
    }

    InternalFilter()
    {
        cutoff = paramDefs()[(size_t) Cutoff].def;
        resonance = paramDefs()[(size_t) Resonance].def;
        type = (int) paramDefs()[(size_t) ModeParam].def;
        updateCoefficients();
    }

    ~InternalFilter() = default;

    // Sets the sample rate, clears the integrator states and recomputes the
    // coefficients (from whatever params are currently stored). Message-thread
    // only, like every prepare(); no allocation.
    void prepare(double sampleRate)
    {
        this->sampleRate = (sampleRate > 0.0) ? static_cast<float>(sampleRate) : 44100.0f;
        reset();
        updateCoefficients();
    }

    // Clears the per-channel integrator states. Audio-thread safe (plain writes).
    void reset()
    {
        ic1eqL = 0; ic2eqL = 0;
        ic1eqR = 0; ic2eqR = 0;
    }

    // Clamped to this class's defs at EVERY entry (lesson 23); Mode is rounded
    // to its enum. Any param change recomputes the coefficients (they depend on
    // all three: g on Cutoff, k on Resonance, the mode only selects which
    // integrator combination is returned), the same "reconfigure on ANY param
    // change" contract the slot and the EQ path use.
    void setParam(int index, float value)
    {
        if (index < 0 || index >= kNumParams) return;
        value = clampParam(index, value);
        switch (index)
        {
            case Cutoff:    cutoff = value; break;
            case ModeParam: type = juce::roundToInt(value); break;
            default:        resonance = value; break;
        }
        updateCoefficients();
    }

    float getParam(int index) const
    {
        if (index < 0 || index >= kNumParams) return 0.0f;
        switch (index)
        {
            case Cutoff:    return cutoff;
            case ModeParam: return static_cast<float>(type);
            default:        return resonance;
        }
    }

    // In-place per-sample TPT solve, per channel (0 = left state pair, any other
    // channel = the right pair — the slot's call site passes static_cast<int>(ch)
    // and a bus block is stereo). Verbatim from ManualSVF::processSample:
    //   loop: hp = x - k*bp - lp, trap: y = g*u + s, s' = 2y - s, solved
    //   instantaneously: bp = (g*x + ic1 - g*ic2) / D, D = 1 + g*k + g^2,
    //   lp = g*bp + ic2.
    float processSample(int channel, float input)
    {
        const float ic1 = (channel == 0) ? ic1eqL : ic1eqR;
        const float ic2 = (channel == 0) ? ic2eqL : ic2eqR;
        const float bp = (g * input + ic1 - g * ic2) * Dinv;
        const float lp = g * bp + ic2;
        if (channel == 0) { ic1eqL = 2.0f * bp - ic1; ic2eqL = 2.0f * lp - ic2; }
        else              { ic1eqR = 2.0f * bp - ic1; ic2eqR = 2.0f * lp - ic2; }
        switch (type)
        {
            case Highpass: return input - k * bp - lp; // highpass
            case Bandpass: return bp;                  // bandpass
            default:       return lp;                  // lowpass
        }
    }

    // The bus call site: the same per-sample solve over a whole block, in place.
    // No allocation, no lock, no juce::String.
    void process(juce::dsp::AudioBlock<float>& block)
    {
        const auto numChannels = block.getNumChannels();
        const auto numSamples = block.getNumSamples();
        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* channelData = block.getChannelPointer(ch);
            for (size_t s = 0; s < numSamples; ++s)
                channelData[s] = processSample(static_cast<int>(ch), channelData[s]);
        }
    }

private:
    // Correct TPT (trapezoidal) SVF coefficients — verbatim from ManualSVF: the
    // cutoff is folded just below Nyquist (0.49 * sampleRate) and the resonance
    // maps to the damping term k = 2/Q (floored at 0.1 so k can never blow up).
    void updateCoefficients()
    {
        g = std::tan(3.14159265f * std::min(cutoff, sampleRate * 0.49f) / sampleRate);
        k = 2.0f / std::max(0.1f, resonance);
        Dinv = 1.0f / (1.0f + g * k + g * g);
    }

    float ic1eqL = 0, ic2eqL = 0;  // left integrator states
    float ic1eqR = 0, ic2eqR = 0;  // right integrator states
    float g = 0, k = 1, Dinv = 1;
    float sampleRate = 44100;
    float cutoff = 1000;
    float resonance = 0.7;
    int type = 0; // 0=LP, 1=HP, 2=BP

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InternalFilter)
};

} // namespace HDAW
