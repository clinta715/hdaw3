#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "LevelMeter.h"

namespace HDAW {

class BusProcessorBase : public juce::AudioProcessor
{
public:
    BusProcessorBase(const juce::String& name,
                     const juce::AudioChannelSet& inputSet = juce::AudioChannelSet::stereo(),
                     const juce::AudioChannelSet& outputSet = juce::AudioChannelSet::stereo())
        : AudioProcessor(BusesProperties()
              .withInput("Input", inputSet, true)
              .withOutput("Output", outputSet, true)),
          busName(name)
    {
    }

    ~BusProcessorBase() override = default;

    const juce::String getName() const override { return busName; }

    LevelMeter& getMeter() { return meter; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

protected:
    LevelMeter meter;
    juce::String busName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BusProcessorBase)
};

} // namespace HDAW
