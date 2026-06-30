#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ProxyCommon.h"
#include "ProxyPipe.h"
#include "ProxySharedMemory.h"
#include "ProxyProcessManager.h"
#include <atomic>
#include <memory>

namespace proxy {

class PluginProxySlot : public juce::AudioPluginInstance {
public:
    PluginProxySlot(ProxyProcessManager& mgr, uint32_t slotId,
                    const juce::String& pluginName);
    ~PluginProxySlot() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midiMessages) override;
    void reset() override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    const juce::String getName() const override;
    void fillInPluginDescription(juce::PluginDescription& desc) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }

    bool isCrashed() const { return crashed.load(); }
    void onChildCrashed();

private:
    ProxyProcessManager& processManager;
    uint32_t slotId;
    juce::String pluginDisplayName;

    std::atomic<bool> crashed{false};

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int numChannels = 2;
};

} // namespace proxy
