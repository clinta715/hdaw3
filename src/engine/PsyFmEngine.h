#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <array>
#include "PsyFmOperator.h"
#include "PsyFmModMatrix.h"

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

    // ── Modulation matrix ──
    /// Thread-safe: may be called from the message/command thread while the
    /// audio thread renders. matrixLock_ serializes the swap against the
    /// render() matrix read (render skips the matrix pass on contention —
    /// base params remain valid). Direct mutation of getModMatrix() is NOT
    /// thread-safe; all writes must go through setModMatrix.
    void setModMatrix (PsyFmModMatrix matrix);
    PsyFmModMatrix& getModMatrix() { return matrix_; }
    PsyFmModSourcePool& getModSourcePool() { return sources_; }

    // ── Bar clock (called from MutatorConductor) ──
    void onBarBoundary (int barCounter);

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

    std::atomic<float> outputLevelAtom_{ 0.4f };
    std::atomic<float> opEgLevel_[kNumOperators]{};
};

} // namespace HDAW
