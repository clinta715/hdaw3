#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class PassthroughPlugin : public juce::AudioProcessor
{
public:
    PassthroughPlugin()
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {}

    const juce::String getName() const override { return "PassthroughTest"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    void getStateInformation(juce::MemoryBlock& destData) override
    {
        const char marker[] = "PASSTHROUGH_STATE_MARKER";
        destData.setSize(sizeof(marker));
        std::memcpy(destData.getData(), marker, sizeof(marker));
    }

    void setStateInformation(const void* data, int sizeInBytes) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PassthroughPlugin)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PassthroughPlugin();
}
