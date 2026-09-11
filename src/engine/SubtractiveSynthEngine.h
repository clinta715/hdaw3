#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

class SubtractiveSynthEngine
{
public:
    SubtractiveSynthEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    void setOsc1Wave(int value) noexcept;
    void setOsc1Level(float value) noexcept;
    void setOsc2Wave(int value) noexcept;
    void setOsc2Level(float value) noexcept;
    void setOsc2DetuneCents(float value) noexcept;
    void setSubLevel(float value) noexcept;
    void setSubOctave(int value) noexcept;
    void setCutoffHz(float value) noexcept;
    void setResonance(float value) noexcept;
    void setDrive(float value) noexcept;
    void setAttackSeconds(float value) noexcept;
    void setDecaySeconds(float value) noexcept;
    void setSustain(float value) noexcept;
    void setReleaseSeconds(float value) noexcept;
    void setOutputLevel(float value) noexcept;
    void setLegato(bool value) noexcept;
    void setPortamentoSeconds(float value) noexcept;
    void setFilterType(int value) noexcept;
    void setFilterEnvAmount(float value) noexcept;
    void setFilterAttackSeconds(float value) noexcept;
    void setFilterDecaySeconds(float value) noexcept;
    void setFilterSustain(float value) noexcept;
    void setFilterReleaseSeconds(float value) noexcept;
    void setPitchBendRange(float value) noexcept;
    // Virus-emulation upgrade 1 (param 25): osc2 -> osc1 FM amount, 0..1,
    // default 0 (off, bit-identical). Phase-modulates osc1 by the current
    // osc2 output; FM=1.0 deviates osc1 phase by +/-kFmPhaseDepth cycles.
    void setOsc2FmAmount(float value) noexcept;
    // Virus-emulation upgrade 2 (param 26): 24 dB lowpass slope, default
    // false (12 dB, bit-identical). Applies to lowpass mode only; other
    // filter types keep the 12 dB path.
    void setFilterSlope24(bool value) noexcept;
    // Internal Virus-style modulation LFO/mod matrix (params 27..32). All
    // destination amounts default to zero so the shipped sound is unchanged.
    void setModLfoWave(int value) noexcept;
    void setModLfoRateHz(float value) noexcept;
    void setModLfoCutoffAmount(float semitones) noexcept;
    void setModLfoPitchAmountCents(float cents) noexcept;
    void setModLfoAmpAmount(float value) noexcept;
    void setModLfoFmAmount(float value) noexcept;
    // Polyphony mode (param 24). Mono is the default and bit-identical to the
    // pre-polyphony engine; poly allocates up to kMaxPolyVoices voices with
    // per-voice filters. Switching modes clears sound (no orphan voices).
    void setPolyphony(bool value) noexcept;

    int activeNoteCount() const noexcept;

    // Debug accessors for poly-voice state (tests only; not realtime API).
    int   polyVoiceNoteForTest(int index) const noexcept { return polyVoices_[(size_t) index].note; }
    float polyVoiceEnvForTest(int index) const noexcept { return polyVoices_[(size_t) index].envelope; }
    bool  polyVoiceReleasingForTest(int index) const noexcept { return polyVoices_[(size_t) index].releasing; }

    float currentFrequencyForTest() const noexcept;
    float targetFrequencyForTest() const noexcept;
    float envelopeForTest() const noexcept;
    int currentNoteForTest() const noexcept;
    int filterTypeForTest() const noexcept;
    float filterEnvForTest() const noexcept;
    float pitchBendRatioForTest() const noexcept;
    float osc2FmAmountForTest() const noexcept { return osc2FmAmount_.load(std::memory_order_relaxed); }
    bool filterSlope24ForTest() const noexcept { return filterSlope24_.load(std::memory_order_relaxed); }
    float modLfoPhaseForTest() const noexcept { return modLfoPhase_; }
    float modLfoCutoffAmountForTest() const noexcept { return modLfoCutoffAmount_.load(std::memory_order_relaxed); }
    float modLfoFmAmountForTest() const noexcept { return modLfoFmAmount_.load(std::memory_order_relaxed); }

private:
    enum class Waveform
    {
        Sine = 0,
        Saw = 1,
        Square = 2,
        Triangle = 3
    };

    struct Voice
    {
        bool active = false;
        bool releasing = false;
        int note = -1;
        int velocity = 0;
        std::array<float, 2> osc1Phase{};
        std::array<float, 2> osc2Phase{};
        std::array<float, 2> subPhase{};
        float envelope = 0.0f;
        float filterEnv = 0.0f;
        bool filterEnvReleasing = false;
        bool sustainHold = false;
        float bendRatio = 1.0f;
        float currentHz = 0.0f;
        float targetHz = 0.0f;
        int glideSamplesRemaining = 0;
    };

    static constexpr int kMaxHeldNotes = 16;
    static constexpr int kMaxPolyVoices = 8;
    // Osc1 phase deviation (cycles) per unit osc2 output at FM=1.0.
    // +/-1 cycle is full hard-FM character (Virus-style harshness).
    static constexpr float kFmPhaseDepth = 1.0f;
    static constexpr float kUnisonDetuneCents = 7.0f;

    double sampleRate_ = 44100.0;
    Voice voice_;
    std::array<int, kMaxHeldNotes> heldNotes_{};
    int heldNoteCount_ = 0;

    // ── Polyphony bank (param 24) ──
    // Separate from the mono path so existing mono behavior is bit-identical:
    // voice_[0] semantics stay exactly as shipped. Each poly voice owns its
    // filter pair (the cutoff/filter-env depend on the voice's own envelope).
    std::array<Voice, kMaxPolyVoices> polyVoices_{};
    std::atomic<bool> poly_ { false };

    // Per-sample voice DSP shared by both modes (oscillators → drive →
    // per-voice filter env cutoff → filters → pitch/glide → envelopes →
    // env*velocity). outputLevel is applied OUTSIDE (once per output sample).
    float renderVoiceSampleCore(Voice& v,
                                juce::dsp::StateVariableTPTFilter<float>& filter,
                                juce::dsp::StateVariableTPTFilter<float>& filterHp,
                                juce::dsp::StateVariableTPTFilter<float>& filterLp2,
                                float& lastResonance,
                                float modLfo) noexcept;
    void polyNoteOn(int note, int velocity) noexcept;
    void polyNoteOff(int note) noexcept;
    void resetPolyVoice(int index, int note, int velocity) noexcept;
    int  allocPolyVoice() noexcept;
    void advancePitch(Voice& v) noexcept;
    void advanceEnvelope(Voice& v) noexcept;

    std::atomic<int> osc1Wave_ { 0 };
    std::atomic<float> osc1Level_ { 0.6f };
    std::atomic<int> osc2Wave_ { 1 };
    std::atomic<float> osc2Level_ { 0.4f };
    std::atomic<float> osc2DetuneCents_ { 8.0f };
    std::atomic<float> subLevel_ { 0.35f };
    std::atomic<int> subOctave_ { -1 };
    std::atomic<float> cutoffHz_ { 1800.0f };
    std::atomic<float> resonance_ { 0.15f };
    std::atomic<float> drive_ { 0.0f };
    std::atomic<float> attackSeconds_ { 0.01f };
    std::atomic<float> decaySeconds_ { 0.18f };
    std::atomic<float> sustain_ { 0.65f };
    std::atomic<float> releaseSeconds_ { 0.18f };
    std::atomic<float> outputLevel_ { 0.8f };
    std::atomic<bool> legato_ { false };
    std::atomic<float> portamentoSeconds_ { 0.0f };
    std::atomic<int> filterType_ { 0 };
    std::atomic<float> filterEnvAmount_ { 24.0f };
    std::atomic<float> filterEnvAttack_ { 0.01f };
    std::atomic<float> filterEnvDecay_ { 0.30f };
    std::atomic<float> filterEnvSustain_ { 0.70f };
    std::atomic<float> filterEnvRelease_ { 0.30f };
    std::atomic<float> pitchBendRange_ { 2.0f };
    std::atomic<float> osc2FmAmount_ { 0.0f };
    std::atomic<bool> filterSlope24_ { false };
    std::atomic<int> modLfoWave_ { 0 };
    std::atomic<float> modLfoRateHz_ { 0.5f };
    std::atomic<float> modLfoCutoffAmount_ { 0.0f };
    std::atomic<float> modLfoPitchAmountCents_ { 0.0f };
    std::atomic<float> modLfoAmpAmount_ { 0.0f };
    std::atomic<float> modLfoFmAmount_ { 0.0f };
    float modLfoPhase_ = 0.0f;

    juce::dsp::StateVariableTPTFilter<float> filter_;
    juce::dsp::StateVariableTPTFilter<float> filterHp_;
    // Second LP stage for the 24 dB slope option (param 26). Preallocated in
    // prepare(); unused (state still reset) in the default 12 dB path.
    juce::dsp::StateVariableTPTFilter<float> filterLp2_;
    bool sustainPedal_ = false;
    float lastFilterResonance_ = -1.0f;

    // Poly bank filters + per-voice resonance caches (SVF stores its own
    // cutoff per instance; only the resonance is cached off the param read).
    std::array<juce::dsp::StateVariableTPTFilter<float>, kMaxPolyVoices> polyFilter_ {};
    std::array<juce::dsp::StateVariableTPTFilter<float>, kMaxPolyVoices> polyFilterHp_ {};
    std::array<juce::dsp::StateVariableTPTFilter<float>, kMaxPolyVoices> polyFilterLp2_ {};
    std::array<float, kMaxPolyVoices> lastPolyResonance_ {};

    static int clampWave(int value) noexcept;
    static int clampSubOctave(int value) noexcept;
    static int clampFilterType(int value) noexcept;
    static float clampUnit(float value) noexcept;
    static float clampPositive(float value, float fallback) noexcept;
    static float midiNoteToHz(int note) noexcept;
    static float phaseToSample(Waveform wave, float phase) noexcept;
    void resetVoice(int note, int velocity) noexcept;
    void retargetVoice(int note, int velocity, bool resetEnvelope) noexcept;
    void noteOn(int note, int velocity) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;
    void releaseCurrentVoice() noexcept;
    float renderVoiceSample(float modLfo) noexcept;   // mono: core(voice_) * outputLevel
    float renderOutputSample() noexcept;  // mode-aware: mono voice or poly sum
    float nextModLfoSample() noexcept;
    void updateHeldNote(int note, bool pressed) noexcept;
};
