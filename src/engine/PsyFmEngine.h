#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <array>
#include <limits>
#include "PsyFmOperator.h"
#include "PsyFmModMatrix.h"
#include "InternalFilter.h"

namespace HDAW {

/// Psytrance-focused 6-operator FM engine with pluggable algorithm routing,
/// sample-accurate per-operator envelopes, and a modulation matrix for
/// routing LFOs/mod wheel/velocity to operator ratios and feedback.
///
/// Unlike FmSynthEngine (msfa DX7 core, 32 hardcoded algorithms), this engine
/// uses pluggable algorithm functions that define operator chaining/summing,
/// and a PsyFmModMatrix for dynamic modulation routing.
class PsyFmEngine
{
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kNumOperators = 6;

    /// Algorithm function signature: defines how operators chain/sum.
    using AlgorithmFn = std::function<void (PsyFmEngine&, int numSamples)>;

    PsyFmEngine() = default;
    ~PsyFmEngine() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // ── Algorithm ──
    void setAlgorithm (AlgorithmFn fn);

    // ── Base params (pre-modulation) ──
    void setBaseRatios (const float ratios[kNumOperators]);
    void setBaseFeedback (float fb);
    void setOpEnvelope (int opIndex, const juce::ADSR::Parameters& p);
    void setOutputLevel (float v) noexcept;

    // ── Post-carrier filter (slice B, docs/plans/2026-10-05-internal-synth-expansion.md) ──
    // A per-voice multimode TPT SVF (HDAW::InternalFilter) on each voice's
    // carrier output, before the voice-sum. Engine-local indices for the
    // appended TrackFXSlot params 33..37 (TrackFXSlot maps param_N -> N-33).
    // Ranges/defaults mirror the slot def table; every entry is CLAMPED there
    // (lesson 23), so a hand-edited project cannot push a coefficient out of
    // the numerically safe range. At the defaults (cutoff 20000, key-track 0,
    // env-amount 0) the filter is BYPASSED — a 20 kHz LP is not transparent,
    // so back-compat rides on an explicit skip, not on the filter math.
    enum FilterParamIndex
    {
        FilterCutoff = 0,   // 20..20000 Hz, def 20000 (neutral)
        FilterResonance,    // 0.1..10,      def 0.7
        FilterType,         // 0=LP, 1=HP, 2=BP, def 0 (int enum, rounded)
        FilterKeyTrack,     // 0..1,         def 0
        FilterEnvAmount,    // 0..1,         def 0
        kNumFilterParams
    };

    /// Audio-thread safe (plain atomic stores); clamped to the def range.
    void setFilterParam (int index, float value) noexcept;
    float getFilterParam (int index) const noexcept;

    /// True when a new filter param deviates from its neutral default and the
    /// per-voice filter is therefore processed (the back-compat bypass).
    bool isFilterEngaged() const noexcept;

    // ── Modulation matrix ──
    /// Thread-safe: may be called from the message/command thread while the
    /// audio thread renders. matrixLock_ serializes the swap against the
    /// render() matrix read (render skips the matrix pass on contention —
    /// base params remain valid). Direct mutation of getModMatrix() is NOT
    /// thread-safe; all writes must go through setModMatrix.
    void setModMatrix (PsyFmModMatrix matrix);
    PsyFmModMatrix& getModMatrix() { return matrix_; }
    PsyFmModSourcePool& getModSourcePool() { return sources_; }

    /// Read-only debug/inspection snapshot (message thread): copies the matrix
    /// routes, base params, and the source pool under matrixLock_ so a
    /// concurrent setModMatrix swap cannot race the read. The caller simulates
    /// apply() on the copies — no audio is rendered, nothing is mutated.
    /// (Direct getModMatrix() access is NOT thread-safe; debug tools must use
    /// this instead.) Render() uses TryLock+skip, so a brief ScopedLock hold
    /// here only ever costs one skipped matrix pass, never a stall.
    void snapshotModState (std::vector<PsyFmModRoute>& outRoutes,
                           float outBaseRatios[kNumOperators],
                           float& outBaseFeedback,
                           PsyFmModSourcePool& outPool);

    // ── Bar clock (called from MutatorConductor) ──
    void onBarBoundary (int barCounter);

    /// Base rate of the ratio-sweep LFO (PsyFmModSourcePool's documented
    /// default is 0.2 Hz). Stores the base AND resets the live pool rate to it,
    /// so onBarBoundary's rewind branch and the slot's persisted sweep rate
    /// share one value. Message thread, like the other preset setters.
    void setBaseRatioSweepRateHz (float hz) noexcept;

    // ── Inspection ──
    int activeVoiceCount() const noexcept;
    float getOpEgLevel (int op) const noexcept;

    // ── Helpers for algorithm functions ──
    float* getScratch (int opIndex);
    PsyFmOperator& op (int index);
    std::vector<float>& carrierMix();

private:
    struct Voice
    {
        PsyFmOperator operators[kNumOperators];
        int midiNote = -1;
        int channel = 0;
        bool keydown = false;
        bool live = false;
        // Polyphony scale captured when the key is released: the tail keeps
        // rendering at the normalization it had while held, so stacked
        // release tails no longer steal headroom from the newest voice
        // (see the voice-counter regression note in PsyFmEngine.cpp).
        float releaseScale = 1.0f;
    };

    void noteOn (int channel, int pitch, int velocity);
    void noteOff (int channel, int pitch);
    void allNotesOff();
    Voice* allocateVoice();

    double sampleRate_ = 44100.0;
    float baseFreqHz_ = 220.0f;
    float baseRatios_[kNumOperators] = { 1, 1, 1, 1, 1, 1 };
    float baseFeedback_ = 0.0f;

    Voice voices_[kMaxVoices];
    int currentNote_ = 0;

    AlgorithmFn algorithmFn_;
    PsyFmModSourcePool sources_;
    PsyFmModMatrix matrix_;

    // Guards matrix_ swaps (message thread) against the render() read
    // (audio thread). SpinLock, never held across allocation — Gate 3.
    juce::SpinLock matrixLock_;

    std::vector<std::vector<float>> scratchBuffers_;
    std::vector<float> carrierMixBuffer_;

    // Per-voice post-carrier filter (one instance per voice, prepared once in
    // prepare(); processSample() is allocation-free). The atomics are written
    // by the command/message thread and read by the audio thread — the benign
    // tear class InternalFilter documents.
    HDAW::InternalFilter voiceFilters_[kMaxVoices];
    std::atomic<float> filterCutoff_{ 20000.0f };
    std::atomic<float> filterResonance_{ 0.7f };
    std::atomic<int>   filterType_{ 0 };
    std::atomic<float> filterKeyTrack_{ 0.0f };
    std::atomic<float> filterEnvAmount_{ 0.0f };

    std::atomic<float> outputLevelAtom_{ 0.4f };
    std::atomic<float> opEgLevel_[kNumOperators]{};

    // Bar clock state. Audio-thread only: onBarBoundary runs from
    // Track::processBlock (through TrackFXSlot::setTransportBar) and
    // setBaseRatioSweepRateHz from the message thread under stateLock, so a
    // plain int/float is the same idiom as the pool's other fields.
    // lastBar_ starts at INT_MIN so the first call always runs (bar 0 included).
    int lastBar_ = std::numeric_limits<int>::min();
    float baseRatioSweepRateHz_ = 0.2f;   // pool's documented default
};

} // namespace HDAW
