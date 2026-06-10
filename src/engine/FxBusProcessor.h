#pragma once
#include "BusProcessorBase.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace HDAW {

class FxBusProcessor : public BusProcessorBase
{
public:
    FxBusProcessor(const juce::String& name = "FX",
                   const juce::String& fxType = "reverb")
        : BusProcessorBase(name,
                           juce::AudioChannelSet::stereo(),
                           juce::AudioChannelSet::stereo()),
          currentFxType(fxType)
    {
    }

    ~FxBusProcessor() override = default;

    void setFxType(const juce::String& type)
    {
        currentFxType = type;
        resetFxChain();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        scratchBuffer.setSize(2, samplesPerBlock);

        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 2;

        resetFxChain();
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
        int totalInputs = getTotalNumInputChannels();
        for (int ch = 0; ch < numChannels; ++ch)
            scratchBuffer.addFrom(ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> block(scratchBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        if (reverbEnabled)
            reverbProcess.process(context);
        if (delayEnabled)
            delayProcess.process(context);
        if (eqEnabled)
            eqProcess.process(context);
        if (compEnabled)
            compProcess.process(context);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom(ch, 0, scratchBuffer, ch, 0, numSamples);

        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        meter.update(buffer);
    }

private:
    void resetFxChain()
    {
        reverbEnabled = false;
        delayEnabled = false;
        eqEnabled = false;
        compEnabled = false;

        if (currentFxType == "reverb")
        {
            reverbEnabled = true;
            reverbProcess.reset();
            reverbProcess.prepare(spec);
            reverbProcess.setParameters({ 0.5f, 0.5f, 0.3f, 0.7f });
        }
        else if (currentFxType == "delay")
        {
            delayEnabled = true;
            delayProcess.reset();
            delayProcess.prepare(spec);
            delayProcess.setDelay(static_cast<int>(spec.sampleRate * 0.5));
        }
        else if (currentFxType == "eq")
        {
            eqEnabled = true;
            eqProcess.reset();
            eqProcess.prepare(spec);
            *eqProcess.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                spec.sampleRate, 1000.0f, 0.7f, 0.0f);
        }
        else if (currentFxType == "compressor")
        {
            compEnabled = true;
            compProcess.reset();
            compProcess.prepare(spec);
            compProcess.setThreshold(-20.0f);
            compProcess.setRatio(4.0f);
            compProcess.setAttack(5.0f);
            compProcess.setRelease(100.0f);
        }
    }

    juce::String currentFxType;
    juce::AudioBuffer<float> scratchBuffer;
    juce::dsp::ProcessSpec spec;

    std::atomic<bool> reverbEnabled{ false };
    std::atomic<bool> delayEnabled{ false };
    std::atomic<bool> eqEnabled{ false };
    std::atomic<bool> compEnabled{ false };

    juce::dsp::Reverb reverbProcess;
    juce::dsp::DelayLine<float> delayProcess{ 44100 };
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> eqProcess;
    juce::dsp::Compressor<float> compProcess;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBusProcessor)
};

} // namespace HDAW
