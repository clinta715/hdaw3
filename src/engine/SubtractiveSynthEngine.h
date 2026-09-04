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

    int activeNoteCount() const noexcept;

    float currentFrequencyForTest() const noexcept;
    float targetFrequencyForTest() const noexcept;
    float envelopeForTest() const noexcept;
    int currentNoteForTest() const noexcept;
    int filterTypeForTest() const noexcept;
    float filterEnvForTest() const noexcept;
    float pitchBendRatioForTest() const noexcept;

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
    static constexpr float kUnisonDetuneCents = 7.0f;

    double sampleRate_ = 44100.0;
    Voice voice_;
    std::array<int, kMaxHeldNotes> heldNotes_{};
    int heldNoteCount_ = 0;

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

    juce::dsp::StateVariableTPTFilter<float> filter_;
    juce::dsp::StateVariableTPTFilter<float> filterHp_;
    bool sustainPedal_ = false;
    float lastFilterResonance_ = -1.0f;

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
    void advancePitch() noexcept;
    float renderVoiceSample() noexcept;
    void advanceEnvelope() noexcept;
    void updateHeldNote(int note, bool pressed) noexcept;
};
