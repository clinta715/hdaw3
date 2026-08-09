#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "LevelMeter.h"
#include "TrackFXSlot.h"
#include "MidiFx.h"
#include "AutomationManager.h"
#include "ModulationManager.h"
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
    float getVolume() const { return volumeGain.getTargetValue(); }
    float getPan() const { return panPosition.getTargetValue(); }

    void restoreMixerState(float volume, float pan, bool muted)
    {
        volumeGain.setCurrentAndTargetValue(volume);
        panPosition.setCurrentAndTargetValue(pan);
        isMuted.store(muted);
    }

    void rebuildFXChain(const juce::ValueTree& fxChainTree);
    int getNumFXSlots() const { return static_cast<int>(fxChain.size()); }
    std::vector<std::unique_ptr<TrackFXSlot>>& getFXChain() { return fxChain; }
    void toggleFXEditor(int slotIndex);

    void rebuildMidiFXChain(const juce::ValueTree& midiFxChainTree);
    int getNumMidiFxSlots() const { return static_cast<int>(midiFxChain.size()); }
    const std::vector<std::unique_ptr<MidiFxSlot>>& getMidiFxChain() const { return midiFxChain; }

    // FX chain mutation (used by the MCP server's add_fx/remove_fx/set_fx_bypass tools).
    // The in-memory chain and the track's FX_CHAIN ValueTree stay in sync.
    // `pos` < 0 means "append". Returns the new slot's index.
    int  addFXSlotAt(const std::string& type, int pos = -1);
    // Sets the pluginID and (best-effort) pluginFormat on a "plugin"-type slot, then
    // triggers a chain rebuild so the plugin is loaded.
    void setFXSlotPluginID(int slotIndex, const std::string& pluginID);
    void removeFXSlot(int slotIndex);
    void setFXBypassed(int slotIndex, bool bypassed);

    // Internal (non-plugin) FX param setter. Must hold stateLock: the pump
    // thread's graph bake (Track::prepareToPlay → TrackFXSlot::prepare)
    // recreates the slot's DSP objects under stateLock, so a concurrent
    // setInternalParam would write into the object being destroyed.
    void setFxSlotInternalParam(int slotIndex, int paramIndex, float value);

    void setAutomationTrees(const juce::ValueTree& automationList);
    AutomationManager& getAutomation(int index) { return *automationManagers[index]; }
    int getNumAutomations() const { return static_cast<int>(automationManagers.size()); }

    void setPluginManager(PluginManager* pm) { pluginManager = pm; }
    PluginManager* getPluginManager() const { return pluginManager; }

    void rebuildModulation(const juce::ValueTree& modulationListTree);

    // Back-pointer to the project model + the track's index. Set once at track
    // creation by RoutingManager::addTrack. Used by the FX-mutation methods so
    // they can locate and modify the track's FX_CHAIN subtree in the model.
    void setProjectContext(ProjectModel* model, int idx)
    {
        projectModel = model;
        trackIndex = idx;
    }

    LevelMeter& getMeter() { return meter; }

    // AudioProcessor boilerplate
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Track"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    void updateLatency();
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
    std::atomic<bool> pendingReset{ false };

    juce::SpinLock stateLock;
    std::vector<std::unique_ptr<TrackFXSlot>> fxChain;
    std::vector<std::unique_ptr<MidiFxSlot>> midiFxChain;
    juce::dsp::ProcessSpec fxSpec;

    std::vector<std::unique_ptr<AutomationManager>> automationManagers;
    std::unique_ptr<ModulationManager> modulationManager;

    PluginManager* pluginManager = nullptr;

    ProjectModel* projectModel = nullptr;
    int trackIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Track)
};

} // namespace HDAW
