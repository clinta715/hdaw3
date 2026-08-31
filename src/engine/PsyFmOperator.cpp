#include "PsyFmOperator.h"

namespace HDAW {

void PsyFmOperator::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    indexEnv_.setSampleRate (sampleRate);
    phase_ = 0.0f;
    lastOutput_ = 0.0f;
    currentEnvValue_ = 0.0f;
}

void PsyFmOperator::setEnvelopeParams (const juce::ADSR::Parameters& p)
{
    indexEnv_.setParameters (p);
}

void PsyFmOperator::noteOn()
{
    indexEnv_.noteOn();
    phase_ = 0.0f;
    lastOutput_ = 0.0f;
}

void PsyFmOperator::noteOff()
{
    indexEnv_.noteOff();
}

bool PsyFmOperator::isActive() const
{
    return indexEnv_.isActive();
}

void PsyFmOperator::setBlockParams (float ratio, float feedbackAmount, float baseFreqHz)
{
    currentRatio_ = ratio;
    feedbackAmount_ = feedbackAmount;
    baseFreq_ = baseFreqHz;
}

void PsyFmOperator::renderBlock (float* outBuffer, const float* modInputBuffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Sample-accurate envelope — owns the amplitude/index contour
        float envValue = indexEnv_.getNextSample();
        currentEnvValue_ = envValue;

        float externalMod = modInputBuffer != nullptr ? modInputBuffer[i] : 0.0f;
        float selfFeedback = feedbackAmount_ * lastOutput_;

        // Phase modulation (avoids frequency-domain aliasing of pure FM at high indices)
        float phaseIncrement = juce::MathConstants<float>::twoPi
                             * currentRatio_ * baseFreq_
                             / static_cast<float> (sampleRate_);
        phase_ += phaseIncrement;
        if (phase_ > juce::MathConstants<float>::twoPi)
            phase_ -= juce::MathConstants<float>::twoPi;

        float sample = std::sin (phase_ + externalMod + selfFeedback) * envValue;
        outBuffer[i] = sample;
        lastOutput_ = sample;
    }
}

} // namespace HDAW
