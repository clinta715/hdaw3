#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../model/ProjectModel.h"
#include <cmath>
#include <atomic>
#include <random>
#include <array>

namespace HDAW {

// Base class for future modulation source types (envelope follower, step seq, etc.)
class ModulationSource {
public:
    virtual ~ModulationSource() = default;
    virtual void prepare(double sampleRate) = 0;
    // Generic phase-driven sample hook for future source types that advance
    // from a precomputed phase. The LFO uses getNextValue(bpm, sr) instead
    // because it needs BPM for beat-synced rate; subclasses that don't can
    // override this. Default is a no-op so the base is usable as-is.
    virtual float processSample(double /*phase*/) { return 0.0f; }
};

enum class LfoWaveform : int {
    sine = 0,
    triangle,
    saw,
    square,
    sampleAndHold
};

class LFOModulationSource : public ModulationSource {
public:
    LFOModulationSource();

    void prepare(double sr) override;
    // processSample is intentionally NOT overridden: the LFO advances from
    // BPM/sr via getNextValue below, and inherits the base no-op for the
    // phase-driven hook.

    // Called from the per-sample loop. Returns the modulation offset
    // for this sample. Handles phase accumulation internally.
    float getNextValue(double bpm, double sampleRate);

    // Called from the audio thread — reads atomics
    float getDepth() const noexcept { return depth.load(std::memory_order_relaxed); }
    float getRate() const noexcept { return rate.load(std::memory_order_relaxed); }
    bool  isRateSynced() const noexcept { return rateSync.load(std::memory_order_relaxed); }
    LfoWaveform getWaveform() const noexcept { return static_cast<LfoWaveform>(waveform.load(std::memory_order_relaxed)); }
    bool  isBipolar() const noexcept { return bipolar.load(std::memory_order_relaxed); }
    float getPhaseOffset() const noexcept { return phaseOffset.load(std::memory_order_relaxed); }
    int   getTargetParamID() const noexcept { return targetParamID.load(std::memory_order_relaxed); }
    bool  isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }

    // Called from the UI thread — writes atomics
    void setDepth(float v) noexcept { depth.store(v, std::memory_order_relaxed); }
    void setRate(float v) noexcept { rate.store(v, std::memory_order_relaxed); }
    void setRateSync(bool v) noexcept { rateSync.store(v, std::memory_order_relaxed); }
    void setWaveform(LfoWaveform w) noexcept { waveform.store(static_cast<int>(w), std::memory_order_relaxed); }
    void setBipolar(bool v) noexcept { bipolar.store(v, std::memory_order_relaxed); }
    void setPhaseOffset(float v) noexcept { phaseOffset.store(v, std::memory_order_relaxed); }
    void setTargetParamID(int v) noexcept { targetParamID.store(v, std::memory_order_relaxed); }
    void setEnabled(bool v) noexcept { enabled.store(v, std::memory_order_relaxed); }

    // Sync state from a ValueTree MODULATION node
    void fromValueTree(const juce::ValueTree& tree);
    juce::ValueTree toValueTree(const juce::String& id) const;

private:
    float lookupWaveform(double normPhase, int wf);

    // Atomics — written by UI thread, read by audio thread
    std::atomic<float>  depth{0.0f};
    std::atomic<float>  rate{1.0f};
    std::atomic<int>    waveform{0};
    std::atomic<bool>   rateSync{true};
    std::atomic<bool>   bipolar{false};
    std::atomic<float>  phaseOffset{0.0f};
    std::atomic<int>    targetParamID{1};
    std::atomic<bool>   enabled{true};

    // Audio-thread only state
    double currentPhase = 0.0;
    double sampleRate = 44100.0;

    // S&H state. The noise table is filled once in prepare() (message thread),
    // never on the audio thread. The audio thread only indexes into it.
    static constexpr int shTableSize = 4096;
    std::array<float, shTableSize> shTable{};
    float lastShValue = 0.0f;
    double prevNormPhase = 0.0;   // for wrap detection (S&H re-roll)
};

// ── inline implementations ──

inline LFOModulationSource::LFOModulationSource()
{
    // Pre-fill the S&H noise table so a source is usable even before
    // prepare() runs. prepare() re-fills it (deterministic seed).
    std::mt19937 rng{ 42 };
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& v : shTable)
        v = dist(rng);
    lastShValue = shTable.front();
    prevNormPhase = 0.0;
}

inline void LFOModulationSource::prepare(double sr)
{
    sampleRate = sr;
    currentPhase = 0.0;
    prevNormPhase = 0.0;

    // Re-fill the noise table here (message thread, not audio thread).
    // No RNG runs on the audio thread afterwards — getNextValue only
    // indexes into shTable.
    std::mt19937 rng{ 42 };
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& v : shTable)
        v = dist(rng);
    lastShValue = shTable.front();
}

inline float LFOModulationSource::getNextValue(double bpm, double sr)
{
    if (!enabled.load(std::memory_order_relaxed))
        return 0.0f;

    double phaseStep;
    if (rateSync.load(std::memory_order_relaxed))
    {
        // `rate` is cycles per beat when synced. freq = rate * beatsPerSec,
        // so rate=1 → 1 cycle/beat (quarter-note period at 4/4), rate=4 →
        // 4 cycles/beat (sixteenth-note period). Higher rate = faster.
        double freq = rate.load(std::memory_order_relaxed) * (bpm / 60.0);
        phaseStep = freq / sr;
    }
    else
    {
        phaseStep = rate.load(std::memory_order_relaxed) / sr;
    }

    currentPhase += phaseStep;
    if (currentPhase >= 1.0)
        currentPhase -= std::floor(currentPhase);

    float phaseOff = phaseOffset.load(std::memory_order_relaxed);
    double normPhase = std::fmod(currentPhase + phaseOff / 360.0, 1.0);
    if (normPhase < 0.0) normPhase += 1.0;

    auto wf = waveform.load(std::memory_order_relaxed);
    float value;
    if (wf == 4)
    {
        // Sample & Hold: re-roll from the precomputed noise table when the
        // normalized phase wraps around a cycle boundary. No RNG runs on the
        // audio thread — only a table read. prevNormPhase tracks the previous
        // normalized phase; a wrap is detected when normPhase < prevNormPhase
        // (the phase wrapped from ~1.0 back to ~0.0 within this cycle window).
        if (normPhase < prevNormPhase)
        {
            // Index the table with a position derived from currentPhase so the
            // sequence advances across cycles. Use a hash of the integer cycle
            // count to spread successive samples across the table.
            int idx = static_cast<int>(currentPhase * shTableSize) % shTableSize;
            if (idx < 0) idx += shTableSize;
            lastShValue = shTable[static_cast<size_t>(idx)];
        }
        prevNormPhase = normPhase;
        value = lastShValue;
    }
    else
    {
        value = lookupWaveform(normPhase, wf);
    }
    float d = depth.load(std::memory_order_relaxed);

    if (!bipolar.load(std::memory_order_relaxed))
        value = value * 0.5f + 0.5f; // map [-1,1] to [0,1]

    return value * d;
}

inline float LFOModulationSource::lookupWaveform(double p, int wf)
{
    switch (wf)
    {
        case 0: // sine
            return static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * p));

        case 1: // triangle
            return static_cast<float>(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);

        case 2: // saw
            return static_cast<float>(2.0 * p - 1.0);

        case 3: // square
            return p < 0.5f ? 1.0f : -1.0f;

        // case 4 (sample & hold) is handled inline in getNextValue so it can
        // do RT-safe wrap detection against prevNormPhase without an RNG.

        default:
            return 0.0f;
    }
}

inline void LFOModulationSource::fromValueTree(const juce::ValueTree& tree)
{
    setWaveform(static_cast<LfoWaveform>(static_cast<int>(tree.getProperty(IDs::waveform, 0))));
    setRate(static_cast<float>(static_cast<double>(tree.getProperty(IDs::rate, 1.0))));
    setRateSync(tree.getProperty(IDs::rateSync, true));
    setDepth(static_cast<float>(static_cast<double>(tree.getProperty(IDs::depth, 0.0))));
    setBipolar(tree.getProperty(IDs::bipolar, false));
    setPhaseOffset(static_cast<float>(static_cast<double>(tree.getProperty(IDs::phaseOffset, 0.0))));
    setTargetParamID(tree.getProperty(IDs::targetParamID, 1));
    setEnabled(tree.getProperty(IDs::enabled, true));
}

inline juce::ValueTree LFOModulationSource::toValueTree(const juce::String& id) const
{
    auto tree = juce::ValueTree(IDs::MODULATION);
    tree.setProperty(IDs::name, "LFO", nullptr);
    tree.setProperty("id", id, nullptr);
    tree.setProperty("type", "lfo", nullptr);
    tree.setProperty(IDs::waveform, static_cast<int>(getWaveform()), nullptr);
    tree.setProperty(IDs::rate, static_cast<double>(getRate()), nullptr);
    tree.setProperty(IDs::rateSync, isRateSynced(), nullptr);
    tree.setProperty(IDs::depth, static_cast<double>(getDepth()), nullptr);
    tree.setProperty(IDs::bipolar, isBipolar(), nullptr);
    tree.setProperty(IDs::phaseOffset, static_cast<double>(getPhaseOffset()), nullptr);
    tree.setProperty(IDs::targetParamID, getTargetParamID(), nullptr);
    tree.setProperty(IDs::enabled, isEnabled(), nullptr);
    return tree;
}

} // namespace HDAW
