#include "RoutingManager.h"

namespace HDAW {

RoutingManager::RoutingManager(juce::AudioProcessorGraph& g, ProjectModel& model,
                               juce::AudioFormatManager& fm, HDAW::TransportManager& tm,
                               HDAW::PluginManager* pm)
    : graph(g), projectModel(model), formatManager(fm), transportManager(tm), pluginManager(pm)
{
}

RoutingManager::~RoutingManager()
{
    trackNodes.clear();
    trackProcessors.clear();
    busNodes.clear();
    groupBuses.clear();
    fxBusProcessors.clear();
    sendConnections.clear();
    audioClipNodes.clear();
    audioClipSources.clear();
    midiClipNodes.clear();
    midiClipSources.clear();
    masterNode = nullptr;
    masterBus = nullptr;
    ioNode = nullptr;
}

void RoutingManager::rebuildFromValueTree()
{
    graph.clear();

    trackNodes.clear();
    trackProcessors.clear();
    busNodes.clear();
    groupBuses.clear();
    fxBusProcessors.clear();
    sendConnections.clear();
    audioClipNodes.clear();
    audioClipSources.clear();
    midiClipNodes.clear();
    midiClipSources.clear();
    masterBus = nullptr;
    masterNode = nullptr;
    ioNode = nullptr;

    ioNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    inputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));

    auto masterProc = std::make_unique<MasterBusProcessor>();
    masterNode = graph.addNode(std::move(masterProc));
    masterBus = static_cast<MasterBusProcessor*>(masterNode->getProcessor());

    auto* master = masterNode->getProcessor();
    int numOut = master->getTotalNumOutputChannels();
    for (int ch = 0; ch < numOut; ++ch)
        graph.addConnection({ { masterNode->nodeID, ch }, { ioNode->nodeID, ch } });

    auto busList = projectModel.getBusListTree();
    for (int i = 0; i < busList.getNumChildren(); ++i)
    {
        auto busTree = busList.getChild(i);
        int busID = busTree.getProperty(IDs::busID);
        juce::String busType = busTree.getProperty(IDs::busType).toString();
        if (busType == "master") continue;
        addBus(busID, busTree);
    }

    auto trackList = projectModel.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto trackTree = trackList.getChild(t);
        addTrack(t, trackTree);
    }
}

void RoutingManager::addTrack(int trackIndex, juce::ValueTree trackTree)
{
    auto newTrack = std::make_unique<HDAW::Track>();
    newTrack->setPluginManager(pluginManager);
    newTrack->setProjectContext(&projectModel, trackIndex);
    newTrack->prepareToPlay(sampleRate, blockSize);
    auto node = graph.addNode(std::move(newTrack));
    trackProcessors[trackIndex] = static_cast<HDAW::Track*>(node->getProcessor());
    trackNodes[trackIndex] = node;

    int parentBus = trackTree.getProperty(IDs::parentBus);
    connectTrackToBus(trackIndex, parentBus);

    auto sendList = trackTree.getChildWithName(IDs::SEND_LIST);
    if (sendList.isValid())
    {
        for (int s = 0; s < sendList.getNumChildren(); ++s)
        {
            auto sendTree = sendList.getChild(s);
            addSend(trackIndex, sendTree);
        }
    }

    trackProcessors[trackIndex]->setAutomationTrees(
        trackTree.getChildWithName(IDs::AUTOMATION_LIST));

    // Build the in-memory FX chain from the project model so the saved
    // plugin state (base64 in IDs::pluginState) is reapplied. This is
    // what makes plugin state save/load actually work end-to-end: a
    // project reload walks the same code path as the initial graph
    // build, so the plugin state bytes get decoded and pushed into the
    // newly instantiated VST3/CLAP instance.
    auto fxChainTree = trackTree.getChildWithName(IDs::FX_CHAIN);
    if (fxChainTree.isValid())
        trackProcessors[trackIndex]->rebuildFXChain(fxChainTree);

    rebuildClipsForTrack(trackIndex, trackTree);
}

void RoutingManager::removeTrack(int trackIndex)
{
    removeSendsForTrack(trackIndex);
    removeClipsForTrack(trackIndex);
    auto nodeIt = trackNodes.find(trackIndex);
    if (nodeIt != trackNodes.end())
    {
        graph.removeNode(nodeIt->second);
        trackNodes.erase(nodeIt);
    }
    trackProcessors.erase(trackIndex);
}

void RoutingManager::removeSendsForTrack(int trackIndex)
{
    for (auto it = sendConnections.begin(); it != sendConnections.end();)
    {
        if (it->first.first == trackIndex)
        {
            graph.removeNode(it->second.node);
            it = sendConnections.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void RoutingManager::removeClipsForTrack(int trackIndex)
{
    for (int ci = 0;; ++ci)
    {
        auto audioIt = audioClipNodes.find({trackIndex, ci});
        if (audioIt != audioClipNodes.end())
        {
            graph.removeNode(audioIt->second);
            audioClipNodes.erase(audioIt);
            audioClipSources.erase({trackIndex, ci});
        }
        else
        {
            break;
        }
    }

    for (int ci = 0;; ++ci)
    {
        auto midiIt = midiClipNodes.find({trackIndex, ci});
        if (midiIt != midiClipNodes.end())
        {
            graph.removeNode(midiIt->second);
            midiClipNodes.erase(midiIt);
            midiClipSources.erase({trackIndex, ci});
        }
        else
        {
            break;
        }
    }
}

void RoutingManager::addBus(int busID, juce::ValueTree busTree)
{
    juce::String busType = busTree.getProperty(IDs::busType).toString();
    juce::String busName = busTree.getProperty(IDs::name).toString();

    if (busType == "group")
    {
        auto node = graph.addNode(std::make_unique<GroupBusProcessor>(busName));
        groupBuses[busID] = static_cast<GroupBusProcessor*>(node->getProcessor());
        busNodes[busID] = node;
        connectBusToParent(busID);
    }
    else if (busType == "fx")
    {
        juce::String fxType = busTree.getProperty(IDs::fxType).toString();
        auto node = graph.addNode(std::make_unique<FxBusProcessor>(busName, fxType));
        fxBusProcessors[busID] = static_cast<FxBusProcessor*>(node->getProcessor());
        busNodes[busID] = node;
        connectBusToParent(busID);
    }
}

void RoutingManager::removeBus(int busID)
{
    auto busIt = busNodes.find(busID);
    if (busIt != busNodes.end())
    {
        graph.removeNode(busIt->second);
        busNodes.erase(busIt);
    }
    groupBuses.erase(busID);
    fxBusProcessors.erase(busID);
}

void RoutingManager::connectBusToParent(int busID)
{
    auto it = busNodes.find(busID);
    if (it == busNodes.end()) return;

    auto busList = projectModel.getBusListTree();
    int parentID = 0;
    for (int i = 0; i < busList.getNumChildren(); ++i)
    {
        auto tree = busList.getChild(i);
        if (static_cast<int>(tree.getProperty(IDs::busID)) == busID)
        {
            parentID = tree.getProperty(IDs::busTarget);
            break;
        }
    }

    juce::AudioProcessorGraph::Node::Ptr parentNode;
    if (parentID == 0)
        parentNode = masterNode;
    else
        parentNode = busNodes[parentID];

    if (parentNode == nullptr)
        parentNode = masterNode;

    auto* proc = it->second->getProcessor();
    int numOutChannels = proc->getTotalNumOutputChannels();
    for (int ch = 0; ch < numOutChannels; ++ch)
        graph.addConnection({ { it->second->nodeID, ch }, { parentNode->nodeID, ch } });
}

void RoutingManager::connectTrackToBus(int trackIndex, int busID)
{
    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt == trackNodes.end()) return;

    juce::AudioProcessorGraph::Node::Ptr targetNode;
    if (busID == 0)
        targetNode = masterNode;
    else
        targetNode = busNodes[busID];

    if (targetNode == nullptr)
        targetNode = masterNode;

    graph.addConnection({ { trackIt->second->nodeID, 0 }, { targetNode->nodeID, 0 } });
    graph.addConnection({ { trackIt->second->nodeID, 1 }, { targetNode->nodeID, 1 } });
}

void RoutingManager::addSend(int trackIndex, const juce::ValueTree& sendTree)
{
    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt == trackNodes.end()) return;

    int sendTarget = sendTree.getProperty(IDs::sendTarget);
    float sendLevel = sendTree.getProperty(IDs::sendLevel);
    juce::String sendMode = sendTree.getProperty(IDs::sendMode).toString();

    auto fxIt = busNodes.find(sendTarget);
    if (fxIt == busNodes.end()) return;

    int sendIndex = 0;

    auto sendProc = std::make_unique<SendProcessor>();
    sendProc->setSendLevel(sendLevel);
    sendProc->setSendMode(sendMode == "pre");

    auto sendNode = graph.addNode(std::move(sendProc));
    sendConnections[{trackIndex, sendIndex}] = {
        sendNode,
        static_cast<SendProcessor*>(sendNode->getProcessor())
    };

    graph.addConnection({ { trackIt->second->nodeID, 0 }, { sendNode->nodeID, 0 } });
    graph.addConnection({ { trackIt->second->nodeID, 1 }, { sendNode->nodeID, 1 } });
    graph.addConnection({ { sendNode->nodeID, 0 }, { fxIt->second->nodeID, 0 } });
    graph.addConnection({ { sendNode->nodeID, 1 }, { fxIt->second->nodeID, 1 } });
}

void RoutingManager::removeSend(int trackIndex, int sendIndex)
{
    auto it = sendConnections.find({trackIndex, sendIndex});
    if (it != sendConnections.end())
    {
        graph.removeNode(it->second.node);
        sendConnections.erase(it);
    }
}

void RoutingManager::rebuildClipsForTrack(int trackIndex, juce::ValueTree trackTree)
{
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return;

    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt == trackNodes.end()) return;

    for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
    {
        auto clipTree = clipList.getChild(ci);
        juce::String clipType = clipTree.getProperty(IDs::clipType).toString();

        if (clipType == "audio")
        {
            auto clipProc = std::make_unique<ClipSourceProcessor>(transportManager, formatManager);

            juce::String sourcePath = clipTree.getProperty(IDs::sourceFile).toString();
            auto takeList = clipTree.getChildWithName(IDs::TAKE_LIST);
            if (takeList.isValid() && takeList.getNumChildren() > 0)
            {
                int activeIdx = static_cast<int>(clipTree.getProperty(IDs::activeTake, 0));
                activeIdx = juce::jlimit(0, takeList.getNumChildren() - 1, activeIdx);
                sourcePath = takeList.getChild(activeIdx).getProperty(IDs::sourceFile).toString();
            }

            clipProc->setSourceFile(sourcePath);
            clipProc->setStartTime(clipTree.getProperty(IDs::startTime));
            clipProc->setDuration(clipTree.getProperty(IDs::duration));
            clipProc->setOffset(clipTree.getProperty(IDs::offset));
            clipProc->setGain(clipTree.getProperty(IDs::gain));
            clipProc->setFadeIn(clipTree.getProperty(IDs::fadeIn));
            clipProc->setFadeOut(clipTree.getProperty(IDs::fadeOut));
            clipProc->setLooping(clipTree.getProperty(IDs::looping));

            auto node = graph.addNode(std::move(clipProc));
            audioClipNodes[{trackIndex, ci}] = node;
            audioClipSources[{trackIndex, ci}] =
                static_cast<ClipSourceProcessor*>(node->getProcessor());

            // Connect clip source → track input
            graph.addConnection({ { node->nodeID, 0 }, { trackIt->second->nodeID, 0 } });
            graph.addConnection({ { node->nodeID, 1 }, { trackIt->second->nodeID, 1 } });
        }
        else if (clipType == "midi")
        {
            auto clipProc = std::make_unique<MidiClipProcessor>(transportManager);
            clipProc->setClipTree(clipTree);
            clipProc->setStartTime(clipTree.getProperty(IDs::startTime));
            clipProc->setDuration(clipTree.getProperty(IDs::duration));
            clipProc->setGain(clipTree.getProperty(IDs::gain));
            // Apply the track's MIDI channel to the new clip processor.
            // The track's midiChannel defaults to 1; the user can change
            // it via the track header.
            int trackChannel = trackTree.getProperty(IDs::midiChannel, 1);
            clipProc->setMidiChannel(trackChannel);

            auto node = graph.addNode(std::move(clipProc));
            midiClipNodes[{trackIndex, ci}] = node;
            midiClipSources[{trackIndex, ci}] =
                static_cast<MidiClipProcessor*>(node->getProcessor());

            // MIDI flows through the graph's MIDI buffer — no audio connections needed
            // The Track accepts MIDI, so MIDI messages flow through automatically
        }
    }
}

void RoutingManager::updateClipParam(int trackIndex, int clipIndex, int paramID, float value)
{
    auto audioIt = audioClipSources.find({trackIndex, clipIndex});
    if (audioIt != audioClipSources.end())
    {
        auto* clip = audioIt->second;
        switch (paramID)
        {
            case 10: clip->setGain(value);                              break;
            case 11: clip->setFadeIn(value);                            break;
            case 12: clip->setFadeOut(value);                           break;
            case 13: clip->setStartTime(static_cast<double>(value));    break;
            case 14: clip->setDuration(static_cast<double>(value));     break;
            case 15: clip->setOffset(static_cast<double>(value));       break;
            case 16: clip->setLooping(value > 0.5f);                    break;
        }
        return;
    }

    auto midiIt = midiClipSources.find({trackIndex, clipIndex});
    if (midiIt != midiClipSources.end())
    {
        auto* clip = midiIt->second;
        if (paramID == 10)
            clip->setGain(value);
    }
}

void RoutingManager::switchClipTake(int trackIndex, int clipIndex, const juce::String& sourceFile)
{
    auto audioIt = audioClipSources.find({trackIndex, clipIndex});
    if (audioIt != audioClipSources.end())
        audioIt->second->switchToSourceFile(sourceFile);
}

void RoutingManager::rebuildTrackFX(int trackIndex)
{
    auto trackIt = trackProcessors.find(trackIndex);
    if (trackIt == trackProcessors.end()) return;

    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(trackIndex);
    auto fxChainTree = trackTree.getChildWithName(IDs::FX_CHAIN);

    trackIt->second->rebuildFXChain(fxChainTree);
}

HDAW::Track* RoutingManager::getTrackNode(int trackIndex) const
{
    auto it = trackProcessors.find(trackIndex);
    return (it != trackProcessors.end()) ? it->second : nullptr;
}

FxBusProcessor* RoutingManager::getFxBus(int busID) const
{
    auto it = fxBusProcessors.find(busID);
    return (it != fxBusProcessors.end()) ? it->second : nullptr;
}

void RoutingManager::setTrackMidiChannel(int trackIndex, int channel)
{
    // Update every MIDI clip processor on this track. The clip processors
    // are stored in midiClipSources keyed by (trackIndex, clipIndex).
    for (auto& kv : midiClipSources)
    {
        if (kv.first.first == trackIndex && kv.second != nullptr)
            kv.second->setMidiChannel(channel);
    }
}

void RoutingManager::setInputMonitoring(int trackIndex, bool enabled)
{
    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt == trackNodes.end() || inputNode == nullptr) return;

    auto& mc = monitorConnections[trackIndex];

    if (enabled && !mc.connected)
    {
        mc.connections[0] = { { inputNode->nodeID, 0 }, { trackIt->second->nodeID, 0 } };
        mc.connections[1] = { { inputNode->nodeID, 1 }, { trackIt->second->nodeID, 1 } };
        graph.addConnection(mc.connections[0]);
        graph.addConnection(mc.connections[1]);
        mc.connected = true;
    }
    else if (!enabled && mc.connected)
    {
        graph.removeConnection(mc.connections[0]);
        graph.removeConnection(mc.connections[1]);
        mc.connected = false;
    }
}

} // namespace HDAW
