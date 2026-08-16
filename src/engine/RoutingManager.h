#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Track.h"
#include "BusProcessorBase.h"
#include "MasterBusProcessor.h"
#include "GroupBusProcessor.h"
#include "FxBusProcessor.h"
#include "SendProcessor.h"
#include "ClipSourceProcessor.h"
#include "MidiClipProcessor.h"
#include "StretchCache.h"
#include "../model/ProjectModel.h"
#include <map>
#include <unordered_map>

namespace HDAW {

class DecodedSoundPool;
class StreamingSoundPool;

class RoutingManager
{
public:
    RoutingManager(juce::AudioProcessorGraph& graph, ProjectModel& model,
                   juce::AudioFormatManager& fm, HDAW::TransportManager& tm,
                   HDAW::PluginManager* pm = nullptr,
                   StretchCache* stretchCache = nullptr,
                   HDAW::DecodedSoundPool* decodedPool = nullptr,
                   HDAW::StreamingSoundPool* streamPool = nullptr);
    ~RoutingManager();

    void rebuildFromValueTree();
    void prebuildTracks();
    // Re-establish the master-bus → audio-output connections. Must be called
    // AFTER AudioProcessorGraph::prepareToPlay, which is when the audioOutput
    // IO node's input-channel count is negotiated with the host bus layout.
    // Connections added before prepareToPlay are silently rejected (the IO
    // node reports 0 channels until its bus layout is reconciled).
    void reconnectMasterToOutput();
    static bool isFolderTrack(const juce::ValueTree& trackTree);
    void addTrack(int trackIndex, juce::ValueTree trackTree);
    void removeTrack(int trackIndex);
    void addBus(int busID, juce::ValueTree busTree);
    void removeBus(int busID);
    // `sendIndex` is the per-track send position (0-based). Each send must
    // use a distinct index; the previous implementation hardcoded 0, which
    // clobbered the map entry and orphaned the earlier SendProcessor node
    // for any track with more than one send.
    void addSend(int trackIndex, int sendIndex, const juce::ValueTree& sendTree);
    void removeSend(int trackIndex, int sendIndex);
    void setSendLevel(int trackIndex, int sendIndex, float level);
    void setSendMode(int trackIndex, int sendIndex, bool isPreFader);
    void setSendBypassed(int trackIndex, int sendIndex, bool bypassed);

    void updateClipParam(int trackIndex, int clipIndex, int paramID, float value);
    void switchClipTake(int trackIndex, int clipIndex, const juce::String& sourceFile);
    // Incremental clip-add path (spike scope): adds a single clip node to the
    // live graph without tearing it down. The clip must already be appended at
    // the end of the track's CLIP_LIST (clipIndex == last position) so the
    // existing (trackIndex, clipIndex) map keys stay valid.
    void addClip(int trackIndex, int clipIndex, const juce::ValueTree& clipTree);
    // Incremental clip-removal path (Task 2): removes a single clip node + its
    // live edges from the graph without tearing it down, erases the identity
    // map entries, and re-applies crossfades to the remaining audio siblings
    // via the shared helpers. The clip must already be removed from the track's
    // CLIP_LIST before this call (the crossfade recompute reads back from the
    // model tree), and the removed clip should be the LAST child so the
    // (trackIndex, clipIndex) keys of the remaining clips stay valid.
    // LOCKING CONTRACT (all incremental clip mutations): callers must hold
    // graphLock and, when not on the JUCE message thread, park the pump via
    // MessageManagerLock for the duration — exactly mirroring
    // MainAudioProcessor::rebuildRoutingGraph. RoutingManager does NOT take
    // these locks itself.
    void removeClip(int trackIndex, int clipIndex);
    // Incremental placement-change path (Task 2): re-reads the clip's
    // startTime/duration/offset/gain/fadeIn/fadeOut/looping/muted from the clip
    // ValueTree, re-pushes them to the live processor, and re-applies
    // crossfades to the moved clip + overlapping audio siblings. Move-within-
    // track only; cross-track moves and middle inserts route to full rebuild.
    // Same locking contract as removeClip.
    void updateClipPlacement(int trackIndex, int clipIndex);
    void rebuildTrackFX(int trackIndex);
    void rebuildMidiTrackFX(int trackIndex);
    void setTrackMidiChannel(int trackIndex, int channel);
    void rebuildMidiClipCache(juce::ValueTree clipTree);

    MasterBusProcessor* getMasterBus() { return masterBus; }
    HDAW::Track* getTrackNode(int trackIndex) const;
    FxBusProcessor* getFxBus(int busID) const;
    int getNumTracks() const { return static_cast<int>(trackProcessors.size()); }
    void setPlaybackInfo(double sr, int bs) { sampleRate = sr; blockSize = bs; }
    void setInputMonitoring(int trackIndex, bool enabled);

    bool loadingPhase = false;

    const std::map<std::pair<int, int>, ClipSourceProcessor*>& getAudioClipSources() const { return audioClipSources; }
    const std::map<std::pair<int, int>, MidiClipProcessor*>& getMidiClipSources() const { return midiClipSources; }

    // Propagates non-realtime (export) mode to every live clip source. Called
    // from the export render thread before rendering begins; the streamer
    // switches to synchronous refill and joins its background reader.
    void setClipSourcesNonRealtime(bool nr);

private:
    std::unique_ptr<HDAW::Track> buildTrackProcessor(int trackIndex, juce::ValueTree trackTree);
    void connectTrackToBus(int trackIndex, int busID);
    void connectBusToParent(int busID);
    void rebuildClipsForTrack(int trackIndex, juce::ValueTree trackTree);
    using CrossfadeMap = std::unordered_map<int, std::vector<ClipSourceProcessor::GainPoint>>;
    CrossfadeMap computeTrackCrossfades(const juce::ValueTree& trackTree);
    std::vector<ClipSourceProcessor::GainPoint> computeMergedEnvelopeForClip(
        const juce::ValueTree& clipTree, const CrossfadeMap& crossfadeMap);
    void buildClipNode(int trackIndex, int clipIndex, const juce::ValueTree& clipTree,
                       const CrossfadeMap& crossfadeMap);
    void removeSendsForTrack(int trackIndex);
    void removeClipsForTrack(int trackIndex);

    juce::AudioProcessorGraph& graph;
    ProjectModel& projectModel;
    juce::AudioFormatManager& formatManager;
    HDAW::TransportManager& transportManager;
    HDAW::PluginManager* pluginManager = nullptr;
    StretchCache* stretchCache = nullptr;
    HDAW::DecodedSoundPool* decodedPool = nullptr;
    HDAW::StreamingSoundPool* streamPool = nullptr;

    double sampleRate = 44100.0;
    int blockSize = 512;

    MasterBusProcessor* masterBus = nullptr;
    juce::AudioProcessorGraph::Node::Ptr masterNode;

    std::map<int, juce::AudioProcessorGraph::Node::Ptr>  busNodes;
    std::map<int, GroupBusProcessor*>  groupBuses;
    std::map<int, FxBusProcessor*>  fxBusProcessors;

    std::map<int, juce::AudioProcessorGraph::Node::Ptr>  trackNodes;
    std::map<int, HDAW::Track*>  trackProcessors;
    std::map<int, std::unique_ptr<HDAW::Track>> prebuiltTracks;

    std::map<std::pair<int, int>, juce::AudioProcessorGraph::Node::Ptr> audioClipNodes;
    std::map<std::pair<int, int>, ClipSourceProcessor*> audioClipSources;
    std::map<std::pair<int, int>, juce::AudioProcessorGraph::Node::Ptr> midiClipNodes;
    std::map<std::pair<int, int>, MidiClipProcessor*> midiClipSources;

    struct SendConnection {
        juce::AudioProcessorGraph::Node::Ptr node;
        SendProcessor* processor = nullptr;
    };
    std::map<std::pair<int, int>, SendConnection> sendConnections;

    juce::AudioProcessorGraph::Node::Ptr ioNode;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr midiInputNode;

    struct MonitorConnection {
        juce::AudioProcessorGraph::Connection connections[2];
        bool connected = false;
    };
    std::map<int, MonitorConnection> monitorConnections;
};

} // namespace HDAW
