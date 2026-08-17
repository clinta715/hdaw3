#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "MainAudioProcessor.h"
#include "SPSCBridge.h"
#include "TransportManager.h"
#include "ProjectPool.h"
#include "PluginManager.h"
#include "MidiInputManager.h"
#include "StretchCache.h"
#include "SessionManager.h"
#include "AudioPreviewPlayer.h"
#include "FileLibraryManager.h"
#include "AudioEngineCommands.h"
#include "ReadModelImpl.h"
#include "PluginServiceImpl.h"
#include "PluginParamServiceImpl.h"
#include "MidiServiceImpl.h"
#include "../model/ProjectModel.h"
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class AudioEngine : private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine();

    void initialize();
    void shutdown();

    MainAudioProcessor* getMainProcessor() const { return mainProcessor.get(); }
    ProjectModel& getProjectModel() { return projectModel; }
    SPSCBridge& getBridge() { return spscBridge; }
    HDAW::TransportManager& getTransportManager() { return transportManager; }
    HDAW::ProjectPool& getProjectPool() { return projectPool; }
    HDAW::PluginManager& getPluginManager() { return pluginManager; }
    HDAW::MidiInputManager& getMidiInputManager() { return midiInputManager; }
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    HDAW::StretchCache& getStretchCache() { return stretchCache; }
    HDAW::AudioPreviewPlayer& getPreviewPlayer() { return *previewPlayer; }
    HDAW::SessionManager& getSessionManager() { return sessionManager; }
    HDAW::FileLibraryManager& getFileLibraryManager() { return fileLibraryManager; }

    // Command interfaces (returning references for polymorphic use)
    ProjectCommands& getProjectCommands();
    TransportCommands& getTransportCommands();
    AudioGraphCommands& getAudioGraphCommands();

    // Concrete command object. Sampler-specific control (setSamplerMode,
    // setSamplerSliceMode, detectSamplerSlices, triggerSamplerSlice) lives on
    // AudioEngineCommands but is not yet part of the ProjectCommands interface;
    // the sampler RPC router / MCP tools reach it through this accessor.
    AudioEngineCommands& getAudioEngineCommands() { return *commands; }

    // Service interfaces
    PluginService& getPluginService() { return *pluginService; }
    PluginParamService& getPluginParamService() { return *paramService; }
    MidiService& getMidiService() { return *midiService; }

    // Read-only model snapshot
    ReadModel& getReadModel();

    // Facade methods
    int getTrackCount() const { return mainProcessor ? mainProcessor->getNumTracks() : 0; }
    float getTrackVolume(int trackIndex) const;
    void setTrackVolume(int trackIndex, float volume);
    float getTrackPan(int trackIndex) const;
    void setTrackPan(int trackIndex, float pan);
    bool isTrackMuted(int trackIndex) const;
    void setTrackMuted(int trackIndex, bool muted);
    bool isTrackArmed(int trackIndex) const;
    void setTrackArmed(int trackIndex, bool armed);
    juce::String getTrackName(int trackIndex) const;

    // Typed read: returns FX-slot program list as data (engine keeps graph reach internal).
    struct FxProgramListEntry { int index = 0; std::string name; };
    std::vector<FxProgramListEntry> getFxProgramList(int trackIndex, int slotIndex) const;

    // Typed read: waveform peak pairs (min/max) for an audio clip, computed from
    // the actual source file via the pool reader. Returns data; keeps the pool
    // reach internal. `ok=false` carries an error message and RPC-style code.
    struct WavePeaks
    {
        std::vector<double> peaks;
        double sampleRate = 0;
        int64_t numSamples = 0;
        bool ok = false;
        std::string error;
        int errorCode = 0;
    };
    WavePeaks getWaveformPeaks(int clipId, int numBins);

    // MIDI CC automation recording. When armed, incoming CC messages during
    // playback are dispatched to the registered callback (on the main thread)
    // so the UI can record them into the current clip's CC list.
    using MidiCcCallback = std::function<void(int channel, int controllerNumber, int value)>;
    void setMidiCcRecordArmed(bool armed) { midiCcRecordArmed.store(armed); }
    bool isMidiCcRecordArmed() const { return midiCcRecordArmed.load(); }
    void setMidiCcCallback(MidiCcCallback cb) { midiCcCallback = std::move(cb); }

    // Record an incoming CC event into the MIDI clip under the playhead on an
    // armed track whose MIDI channel matches. Called on the main thread.
    void recordMidiCc(int channel, int controllerNumber, int value);

    // MIDI note recording. When armed, incoming notes during playback are
    // captured into a MIDI clip on the armed track whose channel matches.
    void setMidiNoteRecordArmed(bool armed);
    bool isMidiNoteRecordArmed() const { return midiNoteRecordArmed.load(); }
    void recordMidiNoteEvent(int channel, int noteNumber, int velocity, bool isNoteOn, int64_t sample);

    // Deterministic drain for the coalesced async routing rebuild.
    //
    // Clip/track add/remove listeners call triggerAsyncUpdate(); the message
    // pump thread later dispatches handleAsyncUpdate() -> rebuildRoutingGraph().
    // Callers on background threads (tests, tooling) that need the graph
    // SETTLED before touching live processors should call this instead of
    // sleeping: it marshals AsyncUpdater::handleUpdateNowIfNeeded() onto the
    // message thread (the flush is documented main-thread-only), where the
    // rebuild takes its already-serialized no-park path. Delivery is
    // exactly-once (shouldDeliver atomic exchange) vs the pump's own
    // dispatch, and a no-op when nothing is pending. Blocks until the
    // rebuild COMPLETES.
    //
    // Caller contract: any thread EXCEPT the message thread, and must NOT
    // hold a MessageManagerLock (callFunctionOnMessageThread jasserts).
    // Calling on the message thread flushes inline (allowed, harmless).
    void drainPendingRoutingRebuild();

    // Incremental-routing mode (Task 3): ON when HDAW_FORCE_INCREMENTAL_ROUTING
    // was non-zero/non-'f' at engine startup (read once in initialize()).
    // Exposed so tests can prove the flag plumbing (T3-G4).
    bool isIncrementalRoutingEnabled() const { return incrementalEnabled_; }

    // Test seams (read-only; cumulative counters are settled once
    // drainPendingRoutingRebuild() returns, because the drain is exactly-once).
    // Prove the drain took the incremental branch (ops applied under one
    // graphLock hold, T3-G3) vs the full-rebuild fallback (T3-G2) and that the
    // queue emptied. No-ops when the flag is OFF, so a zero-change regression
    // is visible as a zero delta on both counters.
    uint64_t debugIncrementalOpsApplied() const { return incrementalOpsApplied_; }
    uint64_t debugFullRebuilds() const { return fullRebuilds_; }
    int debugPendingClipOpCount() const
    {
        std::lock_guard<std::mutex> lock(pendingOpsMutex_);
        return static_cast<int>(pendingClipOps_.size());
    }
    bool debugForceFullRebuildFlag() const
    {
        std::lock_guard<std::mutex> lock(pendingOpsMutex_);
        return forceFullRebuild_;
    }

private:
    // ValueTree::Listener overrides
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichItWasRemoved) override;

    // Coalesces routing-graph rebuilds triggered by clip/track add/remove.
    // Batch clip operations (duplicateClips, moveClips, slicing) mutate many
    // clips at once; rebuilding the whole AudioProcessorGraph on every single
    // add/remove made those operations O(N) rebuilds × O(project) each — a
    // super-linear cliff that stalled the engine (RPC timeouts → black screen).
    // AsyncUpdater merges any number of triggerAsyncUpdate() calls within one
    // message-loop tick into a single handleAsyncUpdate() rebuild.
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void rebuildTempoMap();
    void pushEffectiveMuteState();

    // Task 3 — incremental clip-mutation queue (behind HDAW_FORCE_INCREMENTAL_ROUTING).
    // The ValueTree listeners capture coalesced clip ops at mutation time; the
    // pump thread's handleAsyncUpdate applies them to the live graph under one
    // graphLock hold instead of tearing down the whole graph. Identity
    // (trackIndex/clipIndex) is captured at listener time and stays stable
    // because only append-adds / last-position removes are incremental-safe;
    // any structural event sets forceFullRebuild_ so the drain falls back to a
    // full rebuildRoutingGraph (T3-G1/G2/G3).
    struct PendingClipOp
    {
        enum class Op { Add, Remove, Place };
        Op op = Op::Place;
        int trackIndex = -1;
        int clipIndex = -1;
        int clipID = -1;
    };
    void enqueueClipOp(PendingClipOp::Op op, int trackIndex, int clipIndex,
                       int clipID, bool isStructural);
    void drainPendingClipOps();
    std::vector<PendingClipOp> pendingClipOps_;
    mutable std::mutex pendingOpsMutex_;
    bool forceFullRebuild_ = false;
    bool incrementalEnabled_ = true;
    // Test seams (see debug* getters above).
    uint64_t incrementalOpsApplied_ = 0;
    uint64_t fullRebuilds_ = 0;

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer processorPlayer;
    // PluginManager is declared BEFORE mainProcessor so destruction runs in
    // reverse: the graph (and its PluginProxySlots) is destroyed first, then
    // the PluginManager whose ProxyProcessManager those slots reference. A
    // reversed order would destroy the proxy registry (and terminate the
    // children) while the proxies were still alive.
    HDAW::PluginManager pluginManager;
    std::unique_ptr<MainAudioProcessor> mainProcessor;
    ProjectModel projectModel;
    SPSCBridge spscBridge;
    HDAW::TransportManager transportManager;
    // Declared AFTER mainProcessor, so at destruction the pool dies first
    // (reverse member order) while the graph teardown is still running. Safe:
    // consumers hold shared_ptr copies of decodes (ClipSourceProcessor::decoded_,
    // TrackFXSlot pooled sampler state), so the DecodedSound data outlives the
    // pool's map, and raw DecodedSoundPool* members are never dereferenced
    // during destruction. Do not reorder without revisiting this contract.
    HDAW::ProjectPool projectPool;
    HDAW::MidiInputManager midiInputManager;
    HDAW::StretchCache stretchCache;
    HDAW::SessionManager sessionManager;
    HDAW::FileLibraryManager fileLibraryManager;
    std::unique_ptr<HDAW::AudioPreviewPlayer> previewPlayer;

    std::atomic<bool> midiCcRecordArmed{ false };
    MidiCcCallback midiCcCallback;

    std::atomic<bool> midiNoteRecordArmed{ false };
    struct MidiNoteRecClip { int clipId = -1; int trackIndex = -1; int64_t startSample = 0; int64_t maxEndSample = 0; };
    std::vector<MidiNoteRecClip> midiNoteRecClips;
    std::map<int, std::map<int, std::pair<int64_t, int>>> midiPendingNotes;

    void flushPendingMidiNote(int trackIndex, int noteNumber, int64_t endSample);
    void flushAllPendingMidiNotes(int64_t endSample);
    int ensureMidiRecClip(int trackIndex, int64_t startSample);
    void finalizeMidiRecClips();
    bool isPropagating_ = false;
    bool removingGhosts_ = false;
    std::unique_ptr<AudioEngineCommands> commands;
    std::unique_ptr<ReadModelImpl> readModel;
    std::unique_ptr<PluginService> pluginService;
    std::unique_ptr<PluginParamService> paramService;
    std::unique_ptr<MidiService> midiService;
};
