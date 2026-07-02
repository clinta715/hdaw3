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
#include "../model/ProjectModel.h"
#include <map>

namespace HDAW {

class RoutingManager
{
public:
    RoutingManager(juce::AudioProcessorGraph& graph, ProjectModel& model,
                   juce::AudioFormatManager& fm, HDAW::TransportManager& tm,
                   HDAW::PluginManager* pm = nullptr);
    ~RoutingManager();

    void rebuildFromValueTree();
    void addTrack(int trackIndex, juce::ValueTree trackTree);
    void removeTrack(int trackIndex);
    void addBus(int busID, juce::ValueTree busTree);
    void removeBus(int busID);
    void addSend(int trackIndex, const juce::ValueTree& sendTree);
    void removeSend(int trackIndex, int sendIndex);

    void updateClipParam(int trackIndex, int clipIndex, int paramID, float value);
    void switchClipTake(int trackIndex, int clipIndex, const juce::String& sourceFile);
    void rebuildTrackFX(int trackIndex);
    void setTrackMidiChannel(int trackIndex, int channel);

    MasterBusProcessor* getMasterBus() { return masterBus; }
    HDAW::Track* getTrackNode(int trackIndex) const;
    FxBusProcessor* getFxBus(int busID) const;
    int getNumTracks() const { return static_cast<int>(trackProcessors.size()); }
    void setPlaybackInfo(double sr, int bs) { sampleRate = sr; blockSize = bs; }
    void setInputMonitoring(int trackIndex, bool enabled);

private:
    void connectTrackToBus(int trackIndex, int busID);
    void connectBusToParent(int busID);
    void rebuildClipsForTrack(int trackIndex, juce::ValueTree trackTree);
    void removeSendsForTrack(int trackIndex);
    void removeClipsForTrack(int trackIndex);

    juce::AudioProcessorGraph& graph;
    ProjectModel& projectModel;
    juce::AudioFormatManager& formatManager;
    HDAW::TransportManager& transportManager;
    HDAW::PluginManager* pluginManager = nullptr;

    double sampleRate = 44100.0;
    int blockSize = 512;

    MasterBusProcessor* masterBus = nullptr;
    juce::AudioProcessorGraph::Node::Ptr masterNode;

    std::map<int, juce::AudioProcessorGraph::Node::Ptr>  busNodes;
    std::map<int, GroupBusProcessor*>  groupBuses;
    std::map<int, FxBusProcessor*>  fxBusProcessors;

    std::map<int, juce::AudioProcessorGraph::Node::Ptr>  trackNodes;
    std::map<int, HDAW::Track*>  trackProcessors;

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

    struct MonitorConnection {
        juce::AudioProcessorGraph::Connection connections[2];
        bool connected = false;
    };
    std::map<int, MonitorConnection> monitorConnections;
};

} // namespace HDAW
