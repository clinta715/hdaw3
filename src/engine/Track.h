#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "LevelMeter.h"
#include "TrackFXSlot.h"
#include "AutomationManager.h"
#include "PluginManager.h"
#include "../model/ProjectModel.h"
#include <vector>
#include <memory>

namespace HDAW {

class Track : public juce::AudioProcessor
{
public:
    Track();
    ~Track() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void setVolume(float newVolume);
    void setPan(float newPan);
    void setMuted(bool shouldMute) { isMuted.store(shouldMute); }
    bool getMuted() const { return isMuted.load(); }

    void rebuildFXChain(const juce::ValueTree& fxChainTree);
    int getNumFXSlots() const { return static_cast<int>(fxChain.size()); }
    std::vector<std::unique_ptr<TrackFXSlot>>& getFXChain() { return fxChain; }
    void toggleFXEditor(int slotIndex);

    void setAutomationTrees(const juce::ValueTree& automationList);
    AutomationManager& getAutomation(int index) { return *automationManagers[index]; }
    int getNumAutomations() const { return static_cast<int>(automationManagers.size()); }

    void setPluginManager(PluginManager* pm) { pluginManager = pm; }
    PluginManager* getPluginManager() const { return pluginManager; }

    LevelMeter& getMeter() { return meter; }

    // AudioProcessor boilerplate
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Track"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    LevelMeter meter;
    juce::LinearSmoothedValue<float> volumeGain;
    juce::LinearSmoothedValue<float> panPosition;
    std::atomic<bool> isMuted{ false };

    std::vector<std::unique_ptr<TrackFXSlot>> fxChain;
    juce::dsp::ProcessSpec fxSpec;

    std::vector<std::unique_ptr<AutomationManager>> automationManagers;

    PluginManager* pluginManager = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Track)
};

} // namespace HDAW
