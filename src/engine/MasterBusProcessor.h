#pragma once
#include "BusProcessorBase.h"

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

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        scratchBuffer.setSize(2, samplesPerBlock);
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

        meter.update(buffer);
    }

private:
    juce::AudioBuffer<float> scratchBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterBusProcessor)
};

} // namespace HDAW
