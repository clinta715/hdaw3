#pragma once
#include "BusProcessorBase.h"
#include "../common/BufferCheck.h"
#include <algorithm>
#include <atomic>

namespace HDAW {

class MasterBusProcessor : public BusProcessorBase
{
public:
    MasterBusProcessor()
        : BusProcessorBase("Master Bus",
                           juce::AudioChannelSet::stereo(),
                           juce::AudioChannelSet::stereo())
    {
    }

    ~MasterBusProcessor() override = default;

    // Thread-safe: called from the message/command thread (listener or
    // rebuild); the audio thread only reads the atomic.
    void setGain(float newGain) { gain.store(std::max(0.0f, newGain), std::memory_order_relaxed); }
    float getGain() const { return gain.load(std::memory_order_relaxed); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        scratchBuffer.setSize(2, samplesPerBlock);
        meter.setComputeLufs(true);
        meter.prepare(sampleRate, samplesPerBlock);
        gainSmooth.reset(sampleRate, 0.02);
        gainSmooth.setCurrentAndTargetValue(gain.load(std::memory_order_relaxed));
    }

    void releaseResources() override
    {
        scratchBuffer.setSize(1, 1);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin(2, buffer.getNumChannels());

        scratchBuffer.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            scratchBuffer.addFrom(ch, 0, buffer, ch, 0, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom(ch, 0, scratchBuffer, ch, 0, numSamples);

        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        // Realtime-safe gain: atomic written off-thread, smoothed on the
        // audio thread (ClipSourceProcessor.h:429 idiom). No alloc/lock.
        // getNextValue() returns the current value when not smoothing.
        gainSmooth.setTargetValue(gain.load(std::memory_order_relaxed));
        for (int s = 0; s < numSamples; ++s)
        {
            const float g = gainSmooth.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, s, buffer.getSample(ch, s) * g);
        }

        // Meter reads POST-gain.
        meter.update(buffer);

        HDAW::BufferCheck::checkBuffer(buffer, getSampleRate(), 0);
    }

private:
    juce::AudioBuffer<float> scratchBuffer;
    std::atomic<float> gain{1.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmooth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterBusProcessor)
};

} // namespace HDAW
