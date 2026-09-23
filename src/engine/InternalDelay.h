#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>

namespace HDAW {

// The internal delay DSP, shared by the per-track FX slot (TrackFXSlot's
// ActiveType::Delay) and the fx return (FxBusProcessor's "delay" chain).
//
// Extracted from TrackFXSlot (plan 2026-09-22, slice C3) with the arithmetic
// copied VERBATIM: the per-sample pop/push/mix loop, the feedback/push ordering,
// the tempo-sync derivation and the 1e-4 s recompute gate are the same code, so
// a track's internal delay renders what it rendered before. Why the bus needs
// it: FxBusProcessor's previous bare juce::dsp::DelayLine was a single 100%-wet
// tap with no feedback path, so a dub return could not repeat.
//
// Threading: params and tempo are atomics — the command/message thread writes
// them and the audio thread reads them, the same benign-tear class TrackFXSlot
// documents for these objects (no new deferral model). Nothing here allocates
// except prepare(), which is message-thread only: the line's capacity is set
// BEFORE juce's prepare() (which is what allocates), and process() only reads
// and writes the preallocated ring.
//
// Safety: every param write is clamped to the def (lesson 23), and Feedback's
// def max is 0.99 — that clamp is what stops the recursive feedback running away
// to inf/NaN on a legacy or hand-edited project file (TrackFXSlot.h:719).
class InternalDelay
{
public:
    // Param indices. TrackFXSlot::getParamDefsForType("delay") builds its table
    // from paramDefs() below and common/BusFxDefs.h mirrors it, so track FX,
    // bus FX and both surfaces share one numbering.
    enum ParamIndex { DelayTime = 0, Feedback = 1, Mix = 2, SyncToTempo = 3, Division = 4 };
    static constexpr int kNumParams = 5;

    static constexpr float kMaxDelaySeconds = 5.0f;   // Delay Time def top == the line's capacity
    static constexpr float kMinDelaySeconds = 0.01f;  // Delay Time def bottom
    static constexpr float kMaxFeedback = 0.99f;      // runaway guard, see the class comment

    // Tempo-synced delay division beat fractions (P1-3), indexed by the
    // Division param (4): 0=1/8, 1=1/16, 2=1/32, 3=triplet-1/8 (2/3*1/8),
    // 4=dotted-1/8 (1.5*1/8), 5=dotted-1/16 (1.5*1/16), 6=1/4.
    static constexpr float kDelayDivisionBeats[7] = {
        0.125f, 0.0625f, 0.03125f, 0.08333f, 0.1875f, 0.09375f, 0.25f
    };

    struct ParamDef { const char* name; float def; float min; float max; };

    // The delay's names/ranges/defaults — ONE source of truth: TrackFXSlot
    // derives its InternalParamDef list from this (so the DSP's clamp and the
    // advertised surface range cannot drift apart), and the bus table
    // (common/BusFxDefs.h) is pinned to it by BusFxParam.DefTableMatchesTrackFxDefs.
    static const std::array<ParamDef, (size_t) kNumParams>& paramDefs()
    {
        static const std::array<ParamDef, (size_t) kNumParams> defs = { {
            { "Delay Time",  0.5f, kMinDelaySeconds, kMaxDelaySeconds },
            { "Feedback",    0.3f, 0.0f,             kMaxFeedback      },
            { "Mix",         0.5f, 0.0f,             1.0f              },
            { "SyncToTempo", 0.0f, 0.0f,             1.0f              },
            { "Division",    0.0f, 0.0f,             6.0f              },
        } };
        return defs;
    }

    static float clampParam(int index, float value)
    {
        if (index < 0 || index >= kNumParams) return value;
        const auto& d = paramDefs()[(size_t) index];
        return juce::jlimit(d.min, d.max, value);
    }

    InternalDelay()
    {
        for (size_t i = 0; i < (size_t) kNumParams; ++i)
            params[i].store(paramDefs()[i].def, std::memory_order_relaxed);
    }

    ~InternalDelay() = default;

    // Message thread only (allocates). Sized to the def's 5 s top BEFORE
    // juce's prepare(), so the documented Delay Time range is real at every
    // sample rate rather than silently clipped to a fixed capacity.
    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        line.setMaximumDelayInSamples(
            juce::jmax(4, (int) std::ceil((double) kMaxDelaySeconds * spec.sampleRate)));
        line.prepare(spec);
        sampleRate = spec.sampleRate;
        reset();
    }

    // Clears the line's contents and the derived-time cache (params/tempo are
    // kept: both callers re-push them after prepare). Audio-thread safe — this
    // only zeroes the ring allocated by prepare().
    void reset()
    {
        line.reset();
        lastDelayTime = -1.0f;
        lastDelaySamps = 1;
    }

    // True once prepare() ran with a real sample rate. Until then setParam()
    // only stores (the caller's prepare re-pushes every param, so a value
    // written before prepare is not lost).
    bool isPrepared() const noexcept { return sampleRate > 0.0; }

    // Clamped to this class's defs at EVERY entry (lesson 23). Note there is no
    // special case for a Delay Time write while SyncToTempo is on: deriveSeconds
    // ignores param 0 in sync mode, so storing it and re-deriving is a no-op —
    // the value resumes mattering the moment sync is turned off, exactly as
    // before the extraction.
    void setParam(int index, float value)
    {
        if (index < 0 || index >= kNumParams) return;
        params[(size_t) index].store(clampParam(index, value), std::memory_order_relaxed);
        applyDelayIfChanged();
    }

    float getParam(int index) const
    {
        if (index < 0 || index >= kNumParams) return 0.0f;
        return params[(size_t) index].load(std::memory_order_relaxed);
    }

    // Project tempo for the tempo-synced divisions. Atomic store only, safe
    // from the audio thread (Track::processBlock feeds tracks this way) and
    // accepted before prepare().
    void setTempo(double bpm)
    {
        tempoBpm.store(static_cast<float>(bpm), std::memory_order_relaxed);
        applyDelayIfChanged();
    }

    // In-place per-block process over a dsp::AudioBlock, per channel:
    //   in = x[s]; delayed = popSample(ch, delaySamps);
    //   pushSample(ch, in + delayed * fb); x[s] = in * dryMix + delayed * wetMix;
    // (TrackFXSlot's loop, copied unchanged — pop before push, fb into the
    // write, wet/dry mix on the read.) No allocation, no lock, no juce::String.
    void process(const juce::dsp::AudioBlock<float>& block)
    {
        if (! isPrepared()) return;

        applyDelayIfChanged();

        const float fb     = params[(size_t) Feedback].load(std::memory_order_relaxed);
        const float wetMix = params[(size_t) Mix].load(std::memory_order_relaxed);
        const float dryMix = 1.0f - wetMix;
        const int delaySamps = lastDelaySamps;

        const auto numChannels = block.getNumChannels();
        const auto numSamples  = block.getNumSamples();
        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* channelData = block.getChannelPointer(ch);
            for (size_t s = 0; s < numSamples; ++s)
            {
                const float in = channelData[s];
                const float delayed = line.popSample(static_cast<int>(ch), (float) delaySamps);
                line.pushSample(static_cast<int>(ch), in + delayed * fb);
                channelData[s] = in * dryMix + delayed * wetMix;
            }
        }
    }

private:
    // Effective delay seconds. Sync mode: kDelayDivisionBeats[division] *
    // 60 / bpm (bpm <= 0 falls back to 120), clamped to the Delay Time param
    // range. Sync off: the raw Delay Time param.
    float deriveSeconds() const
    {
        const float sync = params[(size_t) SyncToTempo].load(std::memory_order_relaxed);
        if (sync > 0.5f)
        {
            int division = juce::roundToInt(params[(size_t) Division].load(std::memory_order_relaxed));
            division = juce::jlimit(0, 6, division);
            double bpm = static_cast<double>(tempoBpm.load(std::memory_order_relaxed));
            if (bpm <= 0.0) bpm = 120.0;
            const double sec = static_cast<double>(kDelayDivisionBeats[division]) * 60.0 / bpm;
            return static_cast<float>(juce::jlimit(0.01, 5.0, sec));
        }
        return params[(size_t) DelayTime].load(std::memory_order_relaxed);
    }

    // The original slot's recompute gate: only touch the delay line when the
    // derived time actually moved (param, division or tempo change), never per
    // block, let alone per sample.
    void applyDelayIfChanged()
    {
        if (! isPrepared()) return;
        const float delayTime = deriveSeconds();
        if (std::fabs(delayTime - lastDelayTime) > 1e-4f)
        {
            lastDelayTime = delayTime;
            lastDelaySamps = juce::roundToInt(delayTime * sampleRate);
            lastDelaySamps = std::max(1, lastDelaySamps);
            line.setDelay((float) lastDelaySamps);
        }
    }

    juce::dsp::DelayLine<float> line;
    std::array<std::atomic<float>, (size_t) kNumParams> params;
    std::atomic<float> tempoBpm{ 120.0f };
    double sampleRate = 0.0;      // 0 == not prepared
    float lastDelayTime = -1.0f;  // derived seconds cache (recompute gate)
    int lastDelaySamps = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InternalDelay)
};

} // namespace HDAW
