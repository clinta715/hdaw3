#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <cstring>
#include "msfa/dx7note.h"
#include "msfa/controllers.h"
#include "msfa/lfo.h"

class FmSynthEngine
{
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kPatchSize = 156;

    FmSynthEngine() = default;
    ~FmSynthEngine();

    void prepare(double sampleRate, int maxBlockSize);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    // Lock-free param setters (message thread -> audio thread via atomics)
    void setAlgorithm(int v) noexcept;
    void setFeedback(int v) noexcept;
    void setOutputLevel(float v) noexcept;
    void setMonoMode(bool v) noexcept;
    void setOpLevel(int op, float v) noexcept;
    void setOpCoarse(int op, int v) noexcept;
    void setOpFine(int op, int v) noexcept;
    void setOpDetune(int op, int v) noexcept;
    void setLfoRate(float v) noexcept;
    void setLfoDelay(float v) noexcept;
    void setLfoPitchDepth(float v) noexcept;
    void setLfoAmpDepth(float v) noexcept;
    void setLfoWaveform(int v) noexcept;

    // Load a raw DX7 patch (156 bytes). Message thread only.
    void loadPatch(const uint8_t patch[kPatchSize]);

    // Inspection
    int activeVoiceCount() const noexcept;
    const Controllers& getControllers() const { return controllers_; }

private:
    struct Voice
    {
        Dx7Note* note = nullptr;
        int midiNote = -1;
        int channel = 0;
        int velocity = 0;
        bool keydown = false;
        bool sustained = false;
        bool live = false;
        int32_t keydownSeq = 0;
    };

    Voice voices_[kMaxVoices];
    int currentNote_ = 0;
    int32_t nextKeydownSeq_ = 0;
    bool sustain_ = false;

    Lfo lfo_;
    Controllers controllers_;
    FmCore engineMsfa_;
    uint8_t patchData_[kPatchSize]{};

    static constexpr int kBlockSize = 64;
    float extra_buf_[kBlockSize] = {};
    int extra_buf_size_ = 0;

    double sampleRate_ = 44100.0;

    // Atomic mirrors
    std::atomic<int>   algorithmAtom_{ 0 };
    std::atomic<int>   feedbackAtom_{ 5 };
    std::atomic<float> outputLevelAtom_{ 0.8f };
    std::atomic<bool>  monoModeAtom_{ false };
    std::atomic<float> opLevelAtom_[6]{ 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f };
    std::atomic<int>   opCoarseAtom_[6]{};
    std::atomic<int>   opFineAtom_[6]{};
    std::atomic<int>   opDetuneAtom_[6]{ 7,7,7,7,7,7 };
    std::atomic<float> lfoRateAtom_{ 0.5f };
    std::atomic<float> lfoDelayAtom_{ 0.0f };
    std::atomic<float> lfoPitchDepthAtom_{ 0.0f };
    std::atomic<float> lfoAmpDepthAtom_{ 0.0f };
    std::atomic<int>   lfoWaveformAtom_{ 0 };
    std::atomic<bool>  paramsDirty_{ false };

    void applyPendingParams();
    void computeBlock(int32_t lfoVal, int32_t lfoDelay, float* dest, int count);
    void handleNoteOn(int note, float vel, int channel);
    void handleNoteOff(int note, int channel);
    Voice* allocateVoice();
    void noteOn(int channel, int pitch, int velocity);
    void noteOff(int channel, int pitch);
    void allNotesOff();
};
