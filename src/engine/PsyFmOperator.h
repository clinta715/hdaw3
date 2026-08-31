#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace HDAW {

/// Single FM operator with sample-accurate amplitude/index envelope.
/// Receives block-rate ratio and feedback from the voice's modulation matrix pass.
/// The envelope runs inside renderBlock() at sample rate for accurate contour shape.
class PsyFmOperator
{
public:
    PsyFmOperator() = default;

    void prepare (double sampleRate);
    void setEnvelopeParams (const juce::ADSR::Parameters& p);
    void noteOn();
    void noteOff();
    bool isActive() const;

    /// Block-constant params, set once per block before renderBlock() runs.
    void setBlockParams (float ratio, float feedbackAmount, float baseFreqHz);

    /// Per-sample render into outBuffer.
    /// @param modInputBuffer  Phase modulation from upstream operator (nullptr = none)
    void renderBlock (float* outBuffer, const float* modInputBuffer, int numSamples);

    /// Current envelope value (for analysis/visualization).
    float getCurrentEnvValue() const { return currentEnvValue_; }

private:
    double sampleRate_ = 44100.0;
    juce::ADSR indexEnv_;
    float phase_ = 0.0f;
    float lastOutput_ = 0.0f;
    float currentRatio_ = 1.0f;
    float feedbackAmount_ = 0.0f;
    float baseFreq_ = 220.0f;
    float currentEnvValue_ = 0.0f;
};

} // namespace HDAW
