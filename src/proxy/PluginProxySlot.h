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
// Thread-local: only the export render thread should be affected; the live
// audio callback must not spin-wait.
inline thread_local bool tls_renderMode{ false };
inline thread_local std::thread::id tls_renderThreadId{};
inline void setRenderMode(bool enabled) {
    tls_renderMode = enabled;
    tls_renderThreadId = enabled ? std::this_thread::get_id() : std::thread::id{};
}
inline bool isRenderMode() { return tls_renderMode; }
inline bool isRenderThread() { return tls_renderMode && std::this_thread::get_id() == tls_renderThreadId; }

// Cancel flag for interrupting spin-waits during export. Set by
// ExportManager::cancel() so the spin-waits can bail out immediately
// instead of waiting for the 200ms deadline.
inline std::atomic<bool> s_renderCancelRequested{ false };
inline void setRenderCancelRequested(bool v) { s_renderCancelRequested.store(v, std::memory_order_relaxed); }
inline bool isRenderCancelRequested() { return s_renderCancelRequested.load(std::memory_order_relaxed); }

// AudioProcessorParameter backed by the isolated-plugin param bridge. Reads a
// parent-local atomic cache for getValue; setValue updates the cache and marks
// a staging slot dirty so processBlock can flush it into the shm paramSet ring
// (lock-free — invoked from the audio thread by TrackFXSlot::applyAutomation).
// AudioPluginInstance requires parameters to be HostedAudioProcessorParameter
// subclasses, so we derive from that and implement getParameterID().
class ProxiedParameter : public juce::HostedAudioProcessorParameter
{
public:
    ProxiedParameter(uint32_t idx, const juce::String& name,
                     float defaultValue, bool automatable, class PluginProxySlot& owner)
        : HostedAudioProcessorParameter(), index(idx), nameStr(name),
          defaultValue_(defaultValue), automatable_(automatable), ownerSlot(owner)
    {
        cache.store(defaultValue, std::memory_order_relaxed);
    }

    juce::String getParameterID() const override { return "proxy_param_" + juce::String(index); }
    float getValue() const override;
    void setValue(float newValue) override;
    float getDefaultValue() const override { return defaultValue_; }
    juce::String getName(int maxLen) const override;
    juce::String getLabel() const override { return {}; }
    int getNumSteps() const override { return 101; }
    bool isAutomatable() const override { return automatable_; }
    juce::String getText(float, int) const override { return {}; }
    float getValueForText(const juce::String&) const override { return 0.f; }

    void setCache(float v) noexcept { cache.store(v, std::memory_order_relaxed); }
    float loadCache() const noexcept { return cache.load(std::memory_order_relaxed); }
    uint32_t paramIndex() const noexcept { return index; }

private:
    uint32_t index;
    juce::String nameStr;
    float defaultValue_;
    bool automatable_;
    std::atomic<float> cache{ 0.f };
    class PluginProxySlot& ownerSlot;
};

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

    // Reported channel layout of the hosted plugin (written by the child into
    // the shm header after load). Drives the PREPARE width and the proxy's
    // reported bus layout so multi-port plugins get their full channel count.
    // Reported channel layout of the hosted plugin (written by the child into
    // the shm header after load). Drives the PREPARE width so multi-port
    // plugins get their full channel count in the child. NOT an override —
    // juce::AudioProcessor::getTotalNum*Channels() is non-virtual in JUCE 8.
    int getReportedNumInputChannels() const;
    int getReportedNumOutputChannels() const;

    // The width the child must process (from the hosted plugin's summed
    // channel count). Set by the engine before prepareToPlay; sent to the
    // child in the PREPARE message.
    void setNumChannels(int channels) { numChannels = channels; }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    const juce::String getName() const override;
    void fillInPluginDescription(juce::PluginDescription& desc) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    int getNumPrograms() override { return numProgramsCached_; }
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
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

    // Message-thread hook for the parent to forward staged param values into
    // the shm paramSet ring on the next audio block.
    void stageParam(uint32_t index, float value);

    // Pull pending child->parent param notifications off the local atomic
    // queue and forward them to this AudioProcessor's listeners via the
    // matching ProxiedParameter::sendValueChangedMessageToListeners.
    void drainParamNotifications();

    void saveStateToTemp();
    bool restoreStateFromTemp();

    static juce::MemoryBlock loadStateForOldSlotId(uint32_t oldSlotId);
    static void clearStateForSlotId(uint32_t slotId);

    using CrashNotifyFn = std::function<void(uint32_t, const juce::String&, const juce::String&)>;
    void setCrashRecoveryNotifier(CrashNotifyFn fn) { crashRecoveryNotifier = std::move(fn); }

    using RespawnRequestFn = std::function<void(uint32_t)>;
    void setRespawnRequestFn(RespawnRequestFn fn) { respawnRequestFn = std::move(fn); }
    void requestRespawn() { if (respawnRequestFn) respawnRequestFn(slotId); }

    // Destruction notifier — fired at the END of ~PluginProxySlot (after all
    // shm/process cleanup). The PluginManager uses it to erase this slot from
    // liveProxySlots and cancel any pending CrashRecovery entry, so a respawn
    // scheduled for a now-destroyed proxy can never dereference a freed
    // pointer. Null-guarded: the proxy can be destroyed during shutdown
    // without a callback set.
    using SlotDestroyedFn = std::function<void(uint32_t)>;
    void setSlotDestroyedFn(SlotDestroyedFn fn) { slotDestroyedFn = std::move(fn); }

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
    int reportedNumInputs = 0;
    int reportedNumOutputs = 0;

    void fetchParamMetadata();
    void timerCallback() override;

    EditorClosedCallback editorClosedCb;
    std::thread editorWatcherThread;

    CrashNotifyFn crashRecoveryNotifier;
    RespawnRequestFn respawnRequestFn;
    SlotDestroyedFn slotDestroyedFn;

    void waitForEditorClosed();

    // Param bridge state. stagedParams_/paramDirty_ are written by stageParam
    // (any thread) and flushed by processBlock (single audio-thread writer).
    // Built once during construction (null pipe ⇒ empty). Atomics are not
    // copy/movable, so use a plain dynamic array via unique_ptr.
    std::unique_ptr<std::atomic<float>[]> stagedParams_;
    std::unique_ptr<std::atomic<uint32_t>[]> paramDirty_;
    uint32_t paramCacheSize_ = 0;
    int numProgramsCached_ = 1;

    // Parent-local bounded SPSC queue bridging child param notifications from
    // the paramNotify shm ring (consumed by processBlock on the audio thread)
    // to drainParamNotifications (message thread), which forwards them to
    // AudioProcessorListener callbacks.
    static constexpr uint32_t kNotifyQueueCap = 1024;
    struct NotifyEntry { int index; float value; };
    NotifyEntry notifyQueue_[kNotifyQueueCap];
    std::atomic<uint32_t> notifyQWrite{0};
    std::atomic<uint32_t> notifyQRead{0};
};

} // namespace proxy
