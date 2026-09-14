#include "PsyFmOperator.h"

namespace HDAW {

void PsyFmOperator::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    indexEnv_.setSampleRate (sampleRate);
    phase_ = 0.0f;
    lastOutput_ = 0.0f;
    currentEnvValue_ = 0.0f;
    noteOnUnsampled_ = false;
    pendingNoteOff_ = false;
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
    noteOnUnsampled_ = true;
    pendingNoteOff_ = false;
}

void PsyFmOperator::noteOff()
{
    if (noteOnUnsampled_)
    {
        // Same-block kill: the ADSR envelope has not advanced yet
        // (envelopeVal == 0), so a plain noteOff computes releaseRate = 0
        // and the voice would sit silent in State::release forever. Defer
        // the release until after this block's keydown phase has sounded.
        pendingNoteOff_ = true;
        return;
    }
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

    // The keydown phase has sounded — a deferred same-block noteOff takes
    // effect now, releasing from the envelope level the note actually
    // reached instead of its (unstarted) level 0.
    if (numSamples > 0 && noteOnUnsampled_)
        noteOnUnsampled_ = false;
    if (numSamples > 0 && pendingNoteOff_)
    {
        indexEnv_.noteOff();
        pendingNoteOff_ = false;
    }
}

} // namespace HDAW
