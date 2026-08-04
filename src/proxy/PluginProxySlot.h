#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ProxyCommon.h"
#include "ProxyPipe.h"
#include "ProxySharedMemory.h"
#include "ProxyProcessManager.h"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace proxy {

// When true, PluginProxySlot::processBlock spin-waits for the child process
// to produce output instead of clearing the buffer on empty output ring.
// Set by ExportManager during offline render (the render loop runs at CPU
// speed with no real-time pacing; the child needs time to process each block).
inline std::atomic<bool> s_renderMode{ false };
inline void setRenderMode(bool enabled) { s_renderMode.store(enabled, std::memory_order_relaxed); }

class PluginProxySlot : public juce::AudioPluginInstance,
                         private juce::Timer {
public:
    PluginProxySlot(ProxyProcessManager& mgr, uint32_t slotId,
                    const juce::String& pluginName,
                    const juce::String& pluginPath = {});
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
    bool restartAfterCrash();
    void migrateToNewSlot(uint32_t newSlotId, std::shared_ptr<ShmRegion> newShm);

    ProxyProcessManager& getProcessManager() { return processManager; }
    uint32_t getSlotId() const { return slotId; }

    void saveStateToTemp();
    bool restoreStateFromTemp();

    static juce::MemoryBlock loadStateForOldSlotId(uint32_t oldSlotId);
    static void clearStateForSlotId(uint32_t slotId);

    using CrashNotifyFn = std::function<void(uint32_t, const juce::String&, const juce::String&)>;
    void setCrashRecoveryNotifier(CrashNotifyFn fn) { crashRecoveryNotifier = std::move(fn); }

    using RespawnRequestFn = std::function<void(uint32_t)>;
    void setRespawnRequestFn(RespawnRequestFn fn) { respawnRequestFn = std::move(fn); }
    void requestRespawn() { if (respawnRequestFn) respawnRequestFn(slotId); }

    using EditorClosedCallback = std::function<void()>;
    void setEditorClosedCallback(EditorClosedCallback cb) { editorClosedCb = std::move(cb); }
    void startEditorWatcher();

private:
    ProxyProcessManager& processManager;
    uint32_t slotId;
    juce::String pluginDisplayName;
    juce::String pluginPathForRecovery;

    std::atomic<bool> crashed{false};
    std::atomic<bool> childAlive{true};
    std::shared_ptr<ShmRegion> shmHandle;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int numChannels = 2;

    void timerCallback() override;

    EditorClosedCallback editorClosedCb;
    std::thread editorWatcherThread;

    CrashNotifyFn crashRecoveryNotifier;
    RespawnRequestFn respawnRequestFn;

    void waitForEditorClosed();
};

} // namespace proxy
