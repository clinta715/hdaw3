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
#include <map>

namespace HDAW {

class DecodedSoundPool;
class SendProcessor;
class FxBusProcessor;

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
    void setFxSlotPsyFmMatrix(int slotIndex, const juce::String& encoded);
    void setFxSlotPsyFmSweepRate(int slotIndex, float hz);

    void setAutomationTrees(const juce::ValueTree& automationList);
    AutomationManager& getAutomation(int index) { return *automationManagers[index]; }
    int getNumAutomations() const { return static_cast<int>(automationManagers.size()); }
    int getNumModulations() const { return modulationManager ? modulationManager->getNumSources() : 0; }
    // Target paramID of the live modulation source at index (see ModulationManager::getSourceParamID); -1 when absent. Live-processor probe for the rebuild-restore test discipline (Gate 1/10).
    int getModulationSourceParamID(int index) const { return modulationManager ? modulationManager->getSourceParamID(index) : -1; }

    void setPluginManager(PluginManager* pm) { pluginManager = pm; }
    PluginManager* getPluginManager() const { return pluginManager; }

    void setDecodedSoundPool(HDAW::DecodedSoundPool* p) { decodedPool = p; }

    void rebuildModulation(const juce::ValueTree& modulationListTree);

    // ── Automation handle registration (contract, mirrored in the
    // RoutingManager.cpp addTrack/addSend/removeSend hooks) ──
    // RoutingManager registers on EVERY rebuild (and on incremental
    // createSend/removeSend): sendHandles[sendIndex] = the LIVE SendProcessor
    // behind pid 2000 + sendIndex; busRegistry = RoutingManager's live
    // FxBusProcessor map behind pid 3000 + busID*8 + paramIndex. Writers take
    // stateLock; processBlock's three decode sites (automation record, lane
    // apply, LFO) read these under their existing stateLock.tryEnter()
    // sections. Behavior: an unregistered index, a null slot, or a registry
    // miss is a SILENT NO-OP — a pid can never crash and never decodes into
    // another pid range (Gates 2/9).
    void registerSendProcessor(int sendIndex, SendProcessor* send);
    void setBusRegistry(const std::map<int, FxBusProcessor*>* registry);

    // Live-processor probes for tests/readback (unlocked; call after a drained
    // rebuild).
    int getNumRegisteredSends() const { return static_cast<int>(sendHandles.size()); }
    SendProcessor* getRegisteredSend(int sendIndex) const
    {
        if (sendIndex < 0 || sendIndex >= static_cast<int>(sendHandles.size())) return nullptr;
        return sendHandles[static_cast<size_t>(sendIndex)];
    }
    const std::map<int, FxBusProcessor*>* getBusRegistry() const { return busRegistry; }

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
    HDAW::DecodedSoundPool* decodedPool = nullptr;

    ProjectModel* projectModel = nullptr;
    int trackIndex = -1;

    // Registered by RoutingManager (contract: see registerSendProcessor) —
    // indexed by sendIndex; nullptr slots are unregistered/no-op pids.
    std::vector<SendProcessor*> sendHandles;
    const std::map<int, FxBusProcessor*>* busRegistry = nullptr;

    // Pid decode helpers for the three automation sites in processBlock
    // (record / lane-apply / LFO). Callers hold stateLock (every site runs
    // inside a stateLock.tryEnter() section). Bounds/null-safe: an index past
    // sendHandles.size() or a busID missing from busRegistry returns nullptr
    // and the site no-ops — arithmetic bounds live here so no site can leak
    // into another pid range (Gates 2/9).
    SendProcessor* sendForPid(int sendIndex) const;
    FxBusProcessor* busForPid(int busID) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Track)
};

} // namespace HDAW
