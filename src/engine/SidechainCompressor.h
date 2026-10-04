#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SidechainCompressor — juce::dsp::Compressor<float> plus an external-envelope
// option for track-FX-slot sidechain (v1).
//
// The stock members, constructor, setters, prepare/reset, update() and the
// per-sample gain math are copied FAITHFULLY from
// build/_deps/juce-src/modules/juce_dsp/widgets/juce_Compressor.{h,cpp}
// (JUCE 8.0.0 FetchContent copy in this repo). The envelope filter is the
// STOCK juce::dsp::BallisticsFilter<float>. When setSidechainSource() is
// never called (or cleared with nullptr), process() is SAMPLE-IDENTICAL to
// stock juce::dsp::Compressor<float> — pinned by the
// Sidechain.WrapperMatchesStockCompressor test (EXEQ, 0 delta).
//
// With a sidechain source installed, each sample's envelope input is the
// level-gained sidechain frame for that channel (channels of a stereo
// sidechain are MONO-SUMMED so both detector states see the same signal);
// the gain computer then applies the identical threshold/ratio/knee math to
// the MAIN input sample. Past the frames the bus published this block, the
// envelope falls back to the main input (stock behavior), so a short bus
// block can never desync the two paths.
// ─────────────────────────────────────────────────────────────────────────────

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace HDAW {

class SidechainCompressor
{
public:
    SidechainCompressor() { update(); }

    void setThreshold (float newThreshold) { thresholddB = newThreshold; update(); }
    void setRatio (float newRatio)
    {
        jassert (newRatio >= 1.0f);
        ratio = newRatio;
        update();
    }
    void setAttack (float newAttack)  { attackTime = newAttack;  update(); }
    void setRelease (float newRelease) { releaseTime = newRelease; update(); }

    // Per-block sidechain injection, called by TrackFXSlot::process BEFORE
    // process(). Stores the TWO channel pointers BY VALUE (never the caller's
    // block-local array, which dies before process() runs — that dangling read
    // made the duck nondeterministic). nullptr/0 clears. The pointed-to storage
    // must outlive process() (it is the SidechainBus's fixed arrays).
    void setSidechainSource (const float* const* channels, int numChannels, int numFrames)
    {
        sidechainCh[0] = (channels != nullptr && numChannels >= 1) ? channels[0] : nullptr;
        sidechainCh[1] = (channels != nullptr && numChannels >= 2) ? channels[1] : nullptr;
        sidechainNumChannels = numChannels;
        sidechainNumFrames = numFrames;
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        jassert (spec.sampleRate > 0);
        jassert (spec.numChannels > 0);

        sampleRate = spec.sampleRate;
        envelopeFilter.prepare (spec);
        update();
        reset();
    }

    void reset() { envelopeFilter.reset(); }

    template <typename ProcessContext>
    void process (const ProcessContext& context) noexcept
    {
        const auto& inputBlock = context.getInputBlock();
        auto& outputBlock      = context.getOutputBlock();
        const auto numChannels = outputBlock.getNumChannels();
        const auto numSamples  = outputBlock.getNumSamples();

        jassert (inputBlock.getNumChannels() == numChannels);
        jassert (inputBlock.getNumSamples()  == numSamples);

        if (context.isBypassed)
        {
            outputBlock.copyFrom (inputBlock);
            return;
        }

        const bool useSidechain = (sidechainCh[0] != nullptr)
            && (sidechainNumChannels < 2 || sidechainCh[1] != nullptr)
            && sidechainNumFrames > 0;
        const int  scChannels   = useSidechain ? sidechainNumChannels : 0;
        const float scNorm      = (scChannels > 1) ? (1.0f / (float) scChannels) : 1.0f;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* inputSamples  = inputBlock .getChannelPointer (channel);
            auto* outputSamples = outputBlock.getChannelPointer (channel);

            int scRemaining = sidechainNumFrames;

            for (size_t i = 0; i < numSamples; ++i)
            {
                float envelopeInput = inputSamples[i];

                if (useSidechain && scRemaining > 0)
                {
                    // Sidechain frames: envelope fed externally, channels
                    // MONO-SUMMED so every detector state sees the same signal.
                    float scSample = 0.0f;
                    for (int c = 0; c < scChannels; ++c)
                        scSample += sidechainCh[c][i];
                    envelopeInput = scSample * scNorm;
                    --scRemaining;
                }

                auto env = envelopeFilter.processSample ((int) channel, envelopeInput);
                outputSamples[i] = gainComputer (env, inputSamples[i]);
            }
        }
    }

    // Stock-compatible single-sample path (envelope from the input itself).
    float processSample (int channel, float inputValue)
    {
        auto env = envelopeFilter.processSample (channel, inputValue);
        return gainComputer (env, inputValue);
    }

private:
    // VCA — byte-identical to juce::dsp::Compressor<float>::processSample.
    float gainComputer (float env, float inputValue) noexcept
    {
        auto gain = (env < threshold)
            ? 1.0f
            : std::pow (env * thresholdInverse, ratioInverse - 1.0f);
        return gain * inputValue;
    }

    void update()
    {
        threshold = juce::Decibels::decibelsToGain (thresholddB, -200.0f);
        thresholdInverse = 1.0f / threshold;
        ratioInverse     = 1.0f / ratio;

        envelopeFilter.setAttackTime (attackTime);
        envelopeFilter.setReleaseTime (releaseTime);
    }

    //==========================================================================
    float threshold = 0.0f, thresholdInverse = 0.0f, ratioInverse = 0.0f;
    juce::dsp::BallisticsFilter<float> envelopeFilter;

    double sampleRate = 44100.0;
    float thresholddB = 0.0f, ratio = 1.0f, attackTime = 1.0f, releaseTime = 100.0f;

    const float* sidechainCh[2] = { nullptr, nullptr };
    int sidechainNumChannels = 0;
    int sidechainNumFrames = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidechainCompressor)
};

} // namespace HDAW
