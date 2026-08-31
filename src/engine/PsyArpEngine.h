#pragma once
// PsyArpEngine — self-contained psytrance arpeggiator synth processor.
// Inspired by Astral Projection / early Goa-into-psy lineage arps.
// Architecture: arp sequencer → oscillator → resonant filter (slow sweep)
//               → [phaser] → ping-pong delay → reverb
// The filter sweep clock is DECOUPLED from the note clock — this is the
// defining characteristic of the style.
//
// Plan: docs/plans/Astral Projection and Arpeggiation.md

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>
#include <set>

class PsyArpEngine
{
public:
    // ========================================================================
    // Parameter enums
    // ========================================================================
    enum class OscShape { Saw = 0, Square, SuperSaw, NumShapes };
    enum class PatternShape { UpDown = 0, Asymmetric332, Random, NumShapes };
    enum class FilterMode { LowPass = 0, NumModes };

    static constexpr int kMaxHeldNotes = 128;
    static constexpr int kMaxArpSeqLen = 64;  // max pattern length (octaves * notes)

    PsyArpEngine() = default;
    ~PsyArpEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // ========================================================================
    // Lock-free param setters (message thread → audio thread via atomics)
    // ========================================================================

    // Oscillator
    void setOscShape(int v) noexcept { oscShape_.store(v, std::memory_order_relaxed); }
    void setOscUnisonVoices(int v) noexcept { oscUnisonVoices_.store(v, std::memory_order_relaxed); }
    void setOscDetuneCents(float v) noexcept { oscDetuneCents_.store(v, std::memory_order_relaxed); }

    // Arp engine
    void setPatternShape(int v) noexcept { patternShape_.store(v, std::memory_order_relaxed); }
    void setOctaveRange(int v) noexcept { octaveRange_.store(v, std::memory_order_relaxed); }
    void setBarsPerMotifLoop(float v) noexcept { barsPerMotifLoop_.store(v, std::memory_order_relaxed); }

    // Filter (slow sweep)
    void setFilterCutoffHz(float v) noexcept { filterCutoffHz_.store(v, std::memory_order_relaxed); }
    void setFilterResonance(float v) noexcept { filterResonance_.store(v, std::memory_order_relaxed); }
    void setFilterSweepBars(float v) noexcept { filterSweepBars_.store(v, std::memory_order_relaxed); }

    // Delay
    void setDelayTimeBeats(float v) noexcept { delayTimeBeats_.store(v, std::memory_order_relaxed); }
    void setDelayFeedback(float v) noexcept { delayFeedback_.store(v, std::memory_order_relaxed); }
    void setDelayPingPongWidth(float v) noexcept { delayPingPongWidth_.store(v, std::memory_order_relaxed); }
    void setDelayWetLevel(float v) noexcept { delayWetLevel_.store(v, std::memory_order_relaxed); }

    // Reverb
    void setReverbSizeSec(float v) noexcept { reverbSizeSec_.store(v, std::memory_order_relaxed); }
    void setReverbWetOnDry(float v) noexcept { reverbWetOnDry_.store(v, std::memory_order_relaxed); }
    void setReverbWetOnDelay(float v) noexcept { reverbWetOnDelay_.store(v, std::memory_order_relaxed); }

    // Phaser
    void setPhaserEnabled(bool v) noexcept { phaserEnabled_.store(v, std::memory_order_relaxed); }
    void setPhaserRateHz(float v) noexcept { phaserRateHz_.store(v, std::memory_order_relaxed); }
    void setPhaserDepth(float v) noexcept { phaserDepth_.store(v, std::memory_order_relaxed); }

    // Output
    void setOutputLevel(float v) noexcept { outputLevel_.store(v, std::memory_order_relaxed); }

    // ========================================================================
    // Inspection
    // ========================================================================
    int activeNoteCount() const noexcept;

private:
    // ========================================================================
    // Arp sequencer state
    // ========================================================================
    struct ArpState
    {
        std::set<int> heldNotes;           // currently held MIDI notes
        std::vector<int> sequence;         // generated arp pattern (MIDI pitches)
        int seqIndex = 0;                  // current position in sequence
        double currentStepBeat = 0.0;      // beat position of current step
        int currentNote = -1;              // sounding note (-1 = none)
        double noteOffBeat = 0.0;          // beat when current note should release
        int channel = 1;                   // MIDI channel
        double lastBeat = 0.0;             // last transport beat (for step advancement)
        int motifCount = 0;                // bars elapsed in current motif loop
    };

    // ========================================================================
    // Per-voice oscillator state (unison voices)
    // ========================================================================
    struct OscVoice
    {
        float phase = 0.0f;
        float detuneRatio = 1.0f;
    };

    // ========================================================================
    // Delay line state (ping-pong)
    // ========================================================================
    struct DelayState
    {
        std::vector<float> bufferL;
        std::vector<float> bufferR;
        int writePosL = 0;
        int writePosR = 0;
        int delaySamples = 0;
    };

    // ========================================================================
    // Phaser state (simplified 4-stage)
    // ========================================================================
    struct PhaserState
    {
        float stage[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float lfoPhase = 0.0f;
    };

    // ========================================================================
    // Reverb state (simple algorithmic)
    // ========================================================================
    struct ReverbState
    {
        // Allpass + comb filters (Schroeder-style)
        std::vector<float> combBuffer[4];
        int combPos[4] = { 0, 0, 0, 0 };
        std::vector<float> allpassBuffer[2];
        int allpassPos[2] = { 0, 0 };
        float mixL = 0.0f;
        float mixR = 0.0f;
    };

    // ========================================================================
    // Internal methods
    // ========================================================================
    void rebuildArpSequence();
    float generateOscillator(float phase, OscShape shape) const;
    float processSVF(float input, float cutoff, float resonance, float* state);
    void processDelay(float& inL, float& inR, float& outL, float& outR);
    void processReverb(float inL, float inR, float& outL, float& outR);
    float processPhaser(float input, PhaserState& state, float lfoPhase);

    // ========================================================================
    // Atomic parameters
    // ========================================================================
    std::atomic<int>   oscShape_{ 0 };           // Saw
    std::atomic<int>   oscUnisonVoices_{ 2 };
    std::atomic<float> oscDetuneCents_{ 8.0f };

    std::atomic<int>   patternShape_{ 0 };       // Asymmetric332
    std::atomic<int>   octaveRange_{ 3 };
    std::atomic<float> barsPerMotifLoop_{ 2.0f };

    std::atomic<float> filterCutoffHz_{ 600.0f };
    std::atomic<float> filterResonance_{ 7.0f };
    std::atomic<float> filterSweepBars_{ 4.0f };

    std::atomic<float> delayTimeBeats_{ 0.375f };
    std::atomic<float> delayFeedback_{ 0.55f };
    std::atomic<float> delayPingPongWidth_{ 1.0f };
    std::atomic<float> delayWetLevel_{ 0.4f };

    std::atomic<float> reverbSizeSec_{ 3.5f };
    std::atomic<float> reverbWetOnDry_{ 0.1f };
    std::atomic<float> reverbWetOnDelay_{ 0.6f };

    std::atomic<bool>  phaserEnabled_{ true };
    std::atomic<float> phaserRateHz_{ 0.15f };
    std::atomic<float> phaserDepth_{ 0.3f };

    std::atomic<float> outputLevel_{ 0.4f };

    // ========================================================================
    // Internal state (not atomic — audio-thread only)
    // ========================================================================
    double sampleRate_ = 44100.0;
    double bpm_ = 120.0;

    ArpState arp_;
    OscVoice oscVoices_[4] = {};        // max 4 unison voices

    // Filter state (2-pole SVF per filter)
    float filterState_[2] = { 0.0f, 0.0f };
    float filterSweepPhase_ = 0.0f;     // slow LFO phase for filter sweep

    DelayState delay_;
    PhaserState phaser_;
    ReverbState reverb_;
};
