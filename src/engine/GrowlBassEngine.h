#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

class GrowlBassEngine
{
public:
    // ========================================================================
    // Parameter enums
    // ========================================================================
    enum class ModShape { Sine = 0, Triangle, Square, NumShapes };
    enum class ClipType { SoftTanh = 0, SoftAtan, Hard, Bitcrush, NumTypes };
    enum class FilterType { LowPass = 0, BandPass, NumTypes };

    static constexpr int kMaxVoices = 8;

    GrowlBassEngine() = default;
    ~GrowlBassEngine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // ========================================================================
    // Lock-free param setters (message thread -> audio thread via atomics)
    // ========================================================================

    // Oscillator / FM core
    void setFundamentalHz(float v) noexcept;
    void setModRatio(float v) noexcept;
    void setModDepth(float v) noexcept;
    void setModShape(int v) noexcept;

    // Distortion stage
    void setClipType(int v) noexcept;
    void setDriveDb(float v) noexcept;
    void setAsymmetry(float v) noexcept;
    void setBitcrushBits(int v) noexcept;

    // Filter
    void setFilterCutoffHz(float v) noexcept;
    void setFilterResonance(float v) noexcept;
    void setFilterEnvAmount(float v) noexcept;
    void setFilterType(int v) noexcept;

    // Envelope (per note)
    void setAttackMs(float v) noexcept;
    void setDecayMs(float v) noexcept;
    void setSustainLevel(float v) noexcept;
    void setReleaseMs(float v) noexcept;

    // Output
    void setOutputLevel(float v) noexcept;

    // ========================================================================
    // Mutation layer (bar-synced randomization)
    // ========================================================================
    void setUnisonEnabled(bool v) noexcept;
    void setUnisonVoices(int v) noexcept;
    void setUnisonDetuneCents(float v) noexcept;
    void setPerNoteRatioJitter(bool v) noexcept;
    void setRatioJitterAmount(float v) noexcept;
    void setFormantEnabled(bool v) noexcept;
    void setFormantMorph(float v) noexcept;
    void setSidechainDrive(bool v) noexcept;
    void setSidechainAmount(float v) noexcept;

    // ========================================================================
    // Inspection
    // ========================================================================
    int activeVoiceCount() const noexcept;
    float getOutputLevel() const noexcept;

private:
    // ========================================================================
    // Voice
    // ========================================================================
    struct Voice
    {
        bool active = false;
        bool noteOn = false;
        int midiNote = -1;
        int channel = 0;
        int velocity = 0;

        // Oscillator phase
        float carrierPhase = 0.0f;
        float modulatorPhase = 0.0f;

        // Envelope
        float envLevel = 0.0f;
        float envReleaseLevel = 0.0f;
        enum class EnvStage { Idle, Attack, Decay, Sustain, Release };
        EnvStage envStage = EnvStage::Idle;

        // Filter state (biquad coefficients + state for SVF)
        float filterState[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    Voice voices_[kMaxVoices];
    int currentNote_ = 0;
    double sampleRate_ = 44100.0;

    // ========================================================================
    // DSP helpers
    // ========================================================================
    float generateCarrier(float phase, ModShape shape);
    float generateModulator(float phase, ModShape shape);
    float waveshape(float input, ClipType type, float drive, float asymmetry, int bits);
    float processSVF(float input, float cutoff, float resonance, FilterType type, float* state);
    float processFormant(float input, float morph);

    Voice* allocateVoice();
    void noteOn(int channel, int pitch, int velocity);
    void noteOff(int channel, int pitch);
    void allNotesOff();
    void advanceEnvelope(Voice& voice, int numSamples);

    // ========================================================================
    // Atomic parameter mirrors (audio-thread reads, message-thread writes)
    // ========================================================================

    // Oscillator / FM core
    std::atomic<float> fundamentalHz_{ 55.0f };
    std::atomic<float> modRatio_{ 1.5f };
    std::atomic<float> modDepth_{ 0.6f };
    std::atomic<int>   modShape_{ 0 }; // ModShape::Sine

    // Distortion stage
    std::atomic<int>   clipType_{ 0 }; // ClipType::SoftTanh
    std::atomic<float> driveDb_{ 18.0f };
    std::atomic<float> asymmetry_{ 0.15f };
    std::atomic<int>   bitcrushBits_{ 8 };

    // Filter
    std::atomic<float> filterCutoffHz_{ 800.0f };
    std::atomic<float> filterResonance_{ 4.0f };
    std::atomic<float> filterEnvAmount_{ 0.7f };
    std::atomic<int>   filterType_{ 0 }; // FilterType::LowPass

    // Envelope
    std::atomic<float> attackMs_{ 2.0f };
    std::atomic<float> decayMs_{ 80.0f };
    std::atomic<float> sustainLevel_{ 0.7f };
    std::atomic<float> releaseMs_{ 40.0f };

    // Output
    std::atomic<float> outputLevel_{ 0.4f };

    // Mutators
    std::atomic<bool>  unisonEnabled_{ false };
    std::atomic<int>   unisonVoices_{ 2 };
    std::atomic<float> unisonDetuneCents_{ 12.0f };
    std::atomic<bool>  perNoteRatioJitter_{ false };
    std::atomic<float> ratioJitterAmount_{ 0.05f };
    std::atomic<bool>  formantEnabled_{ false };
    std::atomic<float> formantMorph_{ 0.0f };
    std::atomic<bool>  sidechainDrive_{ false };
    std::atomic<float> sidechainAmount_{ 0.5f };
};
