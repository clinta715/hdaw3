#include "RoutingManager.h"
#include "CrossfadeEngine.h"
#include <unordered_map>

namespace HDAW {

namespace {

// Whether two gain-envelope point sets represent the same envelope. `a` may be
// unsorted (the merge helper only sorts when crossfade points are present);
// the processor stores the sorted canonical form, so sort a copy first.
bool sameEnvelopePoints(std::vector<ClipSourceProcessor::GainPoint> a,
                        const std::vector<ClipSourceProcessor::GainPoint>& b)
{
    std::sort(a.begin(), a.end(),
        [](const auto& x, const auto& y) { return x.time < y.time; });
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::abs(a[i].time - b[i].time) >= 1e-6 || a[i].gain != b[i].gain)
            return false;
    }
    return true;
}

} // namespace

RoutingManager::RoutingManager(juce::AudioProcessorGraph& g, ProjectModel& model,
                               juce::AudioFormatManager& fm, HDAW::TransportManager& tm,
                               HDAW::PluginManager* pm, StretchCache* sc,
                               HDAW::DecodedSoundPool* pool,
                               HDAW::StreamingSoundPool* streamPool)
    : graph(g), projectModel(model), formatManager(fm), transportManager(tm),
      pluginManager(pm), stretchCache(sc), decodedPool(pool), streamPool(streamPool)
{
}

RoutingManager::~RoutingManager()
{
    trackNodes.clear();
    trackProcessors.clear();
    prebuiltTracks.clear();
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
    midiInputNode = nullptr;
}

bool RoutingManager::isFolderTrack(const juce::ValueTree& trackTree)
{
    if (!trackTree.isValid()) return false;
    int type = trackTree.getProperty(IDs::trackType, 0);
    return type == 2; // 2 = folder
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
    midiInputNode = nullptr;

    ioNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    inputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));

    midiInputNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    auto masterProc = std::make_unique<MasterBusProcessor>();
    masterNode = graph.addNode(std::move(masterProc));
    masterBus = static_cast<MasterBusProcessor*>(masterNode->getProcessor());
    // Gate 1/10 restore: a fresh MasterBusProcessor starts at unity; re-apply
    // the persisted gain so rebuilds (clip edits, load, export) keep it.
    masterBus->setGain(projectModel.getMasterGain());

    // Master-bus → audio-output connections are established by
    // reconnectMasterToOutput(), which must run after graph.prepareToPlay()
    // (see MainAudioProcessor::prepareToPlay). The IO node reports 0 input
    // channels until the graph negotiates its bus layout, so attempting the
    // connection here would be silently rejected.

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
        if (isFolderTrack(trackTree)) continue; // Folders are visual-only, no audio routing
        addTrack(t, trackTree);
    }

    HDAW_LOG("RoutingDiag", "rebuildFromValueTree: tracks=" + juce::String(static_cast<int>(trackNodes.size()))
        + " midiClips=" + juce::String(static_cast<int>(midiClipNodes.size()))
        + " audioClips=" + juce::String(static_cast<int>(audioClipNodes.size()))
        + " buses=" + juce::String(static_cast<int>(busNodes.size()))
        + " sends=" + juce::String(static_cast<int>(sendConnections.size())));

    if (!prebuiltTracks.empty())
    {
        HDAW_LOG("RoutingDiag", "rebuildFromValueTree: " + juce::String(static_cast<int>(prebuiltTracks.size())) + " prebuilt tracks NOT adopted");
        prebuiltTracks.clear();
    }
}

void RoutingManager::rebuildGraph()
{
    graph.rebuild();
}

std::unique_ptr<HDAW::Track> RoutingManager::buildTrackProcessor(int trackIndex, juce::ValueTree trackTree)
{
    auto newTrack = std::make_unique<HDAW::Track>();
    newTrack->setPluginManager(pluginManager);
    newTrack->setDecodedSoundPool(decodedPool);
    newTrack->setProjectContext(&projectModel, trackIndex);
    newTrack->prepareToPlay(sampleRate, blockSize);
    float trackVol = trackTree.getProperty(IDs::volume, 1.0);
    float trackPan = trackTree.getProperty(IDs::pan, 0.0);
    bool trackMuted = trackTree.getProperty(IDs::isMuted, false);
    bool trackSoloed = trackTree.getProperty(IDs::isSoloed, false);

    bool anySoloed = false;
    auto allTracks = projectModel.getTrackListTree();
    for (int i = 0; i < allTracks.getNumChildren(); ++i)
    {
        if (static_cast<bool>(allTracks.getChild(i).getProperty(IDs::isSoloed, false)))
        {
            anySoloed = true;
            break;
        }
    }
    bool effectiveMute = trackMuted || (anySoloed && !trackSoloed);
    newTrack->restoreMixerState(trackVol, trackPan, effectiveMute);
    return newTrack;
}

void RoutingManager::prebuildTracks()
{
    // Phase-0 of the two-phase rebuild: construct Track processors and their
    // plugin FX instances while the JUCE message pump still runs. JUCE
    // plugin instantiation dispatches to the message thread; once
    // MainAudioProcessor::rebuildRoutingGraph parks the pump, creating a
    // plugin instance deadlocks. Only used when plugins run in-process.
    prebuiltTracks.clear();
    auto trackList = projectModel.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto trackTree = trackList.getChild(t);
        if (isFolderTrack(trackTree)) continue;
        auto track = buildTrackProcessor(t, trackTree);
        auto fxChainTree = trackTree.getChildWithName(IDs::FX_CHAIN);
        if (fxChainTree.isValid())
            track->rebuildFXChain(fxChainTree);
        prebuiltTracks[t] = std::move(track);
    }
}

void RoutingManager::reconnectMasterToOutput()
{
    if (masterNode == nullptr || ioNode == nullptr) return;

    // Re-establish master→IO connections after the graph's bus layout has
    // been set and prepareToPlay has run. Use the IO node's negotiated
    // input-channel count as the loop bound.
    auto* master = masterNode->getProcessor();
    int numOut = master->getTotalNumOutputChannels();
    int ioInChannels = ioNode->getProcessor()->getTotalNumInputChannels();
    int channelsToConnect = (ioInChannels > 0) ? juce::jmin(numOut, ioInChannels) : numOut;

    for (int ch = 0; ch < channelsToConnect; ++ch)
    {
        auto conn = juce::AudioProcessorGraph::Connection{
            { masterNode->nodeID, ch }, { ioNode->nodeID, ch } };
        graph.removeConnection(conn);
        graph.addConnection(conn);
    }
}

void RoutingManager::addTrack(int trackIndex, juce::ValueTree trackTree)
{
    if (isFolderTrack(trackTree)) return; // Folders are visual-only

    bool wasPrebuilt = false;
    std::unique_ptr<HDAW::Track> newTrack;
    auto preIt = prebuiltTracks.find(trackIndex);
    if (preIt != prebuiltTracks.end())
    {
        // Built in prebuildTracks() before the message pump was parked:
        // plugin instantiation needs a live message thread (JUCE hops to it),
        // which is unavailable once the rebuild park is held.
        newTrack = std::move(preIt->second);
        prebuiltTracks.erase(preIt);
        wasPrebuilt = true;
    }
    else
    {
        newTrack = buildTrackProcessor(trackIndex, trackTree);
    }
    auto node = graph.addNode(std::move(newTrack));
    trackProcessors[trackIndex] = static_cast<HDAW::Track*>(node->getProcessor());
    trackNodes[trackIndex] = node;

    int parentBus = trackTree.getProperty(IDs::parentBus);
    connectTrackToBus(trackIndex, parentBus);

    // Connect MIDI input → track so external MIDI keyboard triggers instruments
    if (midiInputNode != nullptr)
    {
        graph.addConnection({ { midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                              { node->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });
    }

    auto sendList = trackTree.getChildWithName(IDs::SEND_LIST);
    if (sendList.isValid())
    {
        for (int s = 0; s < sendList.getNumChildren(); ++s)
        {
            auto sendTree = sendList.getChild(s);
            addSend(trackIndex, s, sendTree);
        }
    }

    trackProcessors[trackIndex]->setAutomationTrees(
        trackTree.getChildWithName(IDs::AUTOMATION_LIST));

    // Restore the LFO/modulation sources from the tree on every rebuild -
    // a fresh Track starts with an empty ModulationManager. Without this, a
    // full rebuildRoutingGraph() silently drops every LFO (Gate 1/10: the
    // live processor must match the tree; the ReadModel alone is not enough).
    trackProcessors[trackIndex]->rebuildModulation(
        trackTree.getChildWithName(IDs::MODULATION_LIST));

    // Build the in-memory FX chain from the project model so the saved
    // plugin state (base64 in IDs::pluginState) is reapplied. This is
    // what makes plugin state save/load actually work end-to-end: a
    // project reload walks the same code path as the initial graph
    // build, so the plugin state bytes get decoded and pushed into the
    // newly instantiated VST3/CLAP instance.
    auto fxChainTree = trackTree.getChildWithName(IDs::FX_CHAIN);
    if (fxChainTree.isValid() && !wasPrebuilt)
        trackProcessors[trackIndex]->rebuildFXChain(fxChainTree);

    auto midiFxChainTree = trackTree.getChildWithName(IDs::MIDI_FX_CHAIN);
    if (midiFxChainTree.isValid())
        trackProcessors[trackIndex]->rebuildMidiFXChain(midiFxChainTree);

    rebuildClipsForTrack(trackIndex, trackTree);

    // A routing-graph rebuild (graph.clear) destroys the physical
    // inputNode→track monitor connections. Re-establish them here from the
    // track's persisted inputMonitor intent (the ValueTree is the single
    // source of truth — the in-memory monitorConnections map is wiped when
    // rebuildRoutingGraph creates a fresh RoutingManager). This keeps input
    // monitoring alive across clip/move/FX edits.
    if (static_cast<bool>(trackTree.getProperty(IDs::inputMonitor, false)))
        setInputMonitoring(trackIndex, true);
}

void RoutingManager::removeTrack(int trackIndex)
{
    removeSendsForTrack(trackIndex);
    removeClipsForTrack(trackIndex);
    auto nodeIt = trackNodes.find(trackIndex);
    if (nodeIt != trackNodes.end())
    {
        graph.removeNode(nodeIt->second.get());
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
            graph.removeNode(it->second.node.get());
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
            graph.removeNode(audioIt->second.get());
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
            graph.removeNode(midiIt->second.get());
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
        graph.removeNode(busIt->second.get());
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

void RoutingManager::addSend(int trackIndex, int sendIndex, const juce::ValueTree& sendTree)
{
    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt == trackNodes.end()) return;

    int sendTarget = sendTree.getProperty(IDs::sendTarget);
    float sendLevel = sendTree.getProperty(IDs::sendLevel);
    juce::String sendMode = sendTree.getProperty(IDs::sendMode).toString();

    auto fxIt = busNodes.find(sendTarget);
    if (fxIt == busNodes.end()) return;

    auto sendProc = std::make_unique<SendProcessor>();
    sendProc->setSendLevel(sendLevel);
    sendProc->setSendMode(sendMode == "pre");
    sendProc->setBypassed(sendTree.getProperty(IDs::bypassed, false));

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
        graph.removeNode(it->second.node.get());
        sendConnections.erase(it);
    }
}

void RoutingManager::setSendLevel(int trackIndex, int sendIndex, float level)
{
    auto it = sendConnections.find({trackIndex, sendIndex});
    if (it != sendConnections.end() && it->second.processor != nullptr)
        it->second.processor->setSendLevel(level);
}

void RoutingManager::setSendMode(int trackIndex, int sendIndex, bool isPreFader)
{
    auto it = sendConnections.find({trackIndex, sendIndex});
    if (it != sendConnections.end() && it->second.processor != nullptr)
        it->second.processor->setSendMode(isPreFader);
}

void RoutingManager::setSendBypassed(int trackIndex, int sendIndex, bool bypassed)
{
    auto it = sendConnections.find({trackIndex, sendIndex});
    if (it != sendConnections.end() && it->second.processor != nullptr)
        it->second.processor->setBypassed(bypassed);
}

void RoutingManager::rebuildClipsForTrack(int trackIndex, juce::ValueTree trackTree,
                                          juce::AudioProcessorGraph::UpdateKind updateKind)
{
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return;

    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt == trackNodes.end()) return;

    auto crossfadeMap = computeTrackCrossfades(trackTree);

    for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
        buildClipNode(trackIndex, ci, clipList.getChild(ci), crossfadeMap, updateKind);
}

RoutingManager::CrossfadeMap RoutingManager::computeTrackCrossfades(const juce::ValueTree& trackTree)
{
    CrossfadeMap crossfadeMap;
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
        return crossfadeMap;

    // Pre-compute crossfade envelope points for all audio clips on this track.
    // Crossfades are ephemeral (not stored in the project) — they're recomputed
    // on every graph rebuild so they always reflect the current clip layout.
    std::vector<CrossfadeEngine::ClipInfo> trackClips;
    for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
    {
        auto ct = clipList.getChild(ci);
        if (ct.getProperty(IDs::clipType, "audio").toString() != "audio")
            continue;
        trackClips.push_back({
            static_cast<int>(ct.getProperty(IDs::clipID, 0)),
            static_cast<double>(ct.getProperty(IDs::startTime, 0.0)),
            static_cast<double>(ct.getProperty(IDs::duration, 0.0)),
            static_cast<double>(ct.getProperty(IDs::fadeIn, 0.0)),
            static_cast<double>(ct.getProperty(IDs::fadeOut, 0.0)),
        });
    }
    std::sort(trackClips.begin(), trackClips.end(),
        [](const auto& a, const auto& b) { return a.startSec < b.startSec; });

    auto crossfades = CrossfadeEngine::computeCrossfades(trackClips, 0.01);
    for (const auto& cf : crossfades)
    {
        if (cf.points.empty()) continue;
        std::vector<ClipSourceProcessor::GainPoint> gpts;
        gpts.reserve(cf.points.size());
        for (const auto& p : cf.points)
            gpts.push_back({ p.time, p.gain });
        crossfadeMap[cf.clipId] = std::move(gpts);
    }
    return crossfadeMap;
}

std::vector<ClipSourceProcessor::GainPoint> RoutingManager::computeMergedEnvelopeForClip(
    const juce::ValueTree& clipTree, const CrossfadeMap& crossfadeMap)
{
    std::vector<ClipSourceProcessor::GainPoint> gpts;
    auto envTree = clipTree.getChildWithName(IDs::GAIN_ENVELOPE);
    if (envTree.isValid())
    {
        auto envPoints = ProjectModel::getGainEnvelopePoints(envTree);
        gpts.reserve(envPoints.size() + 4);
        for (const auto& p : envPoints)
            gpts.push_back({ p.time, p.gain });
    }
    // Merge crossfade points if this clip has any. Crossfade points are
    // ephemeral (computed above, not stored in the project).
    int clipId = static_cast<int>(clipTree.getProperty(IDs::clipID, -1));
    auto cfIt = crossfadeMap.find(clipId);
    if (cfIt != crossfadeMap.end())
    {
        for (const auto& cp : cfIt->second)
            gpts.push_back({ cp.time, cp.gain });
        // Sort by time, then deduplicate (keep last value at each time).
        std::sort(gpts.begin(), gpts.end(),
            [](const auto& a, const auto& b) { return a.time < b.time; });
        auto last = std::unique(gpts.begin(), gpts.end(),
            [](const auto& a, const auto& b) {
                return std::abs(a.time - b.time) < 1e-6;
            });
        gpts.erase(last, gpts.end());
    }
    return gpts;
}

void RoutingManager::buildClipNode(int trackIndex, int clipIndex,
                                   const juce::ValueTree& clipTree,
                                   const CrossfadeMap& crossfadeMap,
                                   juce::AudioProcessorGraph::UpdateKind updateKind)
{
    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt == trackNodes.end()) return;

    juce::String clipType = clipTree.getProperty(IDs::clipType).toString();

    if (clipType == "audio")
    {
        auto clipProc = std::make_unique<ClipSourceProcessor>(transportManager, formatManager, decodedPool, streamPool);

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
        clipProc->setMuted(clipTree.getProperty(IDs::muted, false));

        // Push the per-clip gain envelope to the freshly-built processor,
        // merged with any crossfade points for this clip. Crossfade points
        // are ephemeral (computed above, not stored in the project).
        auto merged = computeMergedEnvelopeForClip(clipTree, crossfadeMap);
        if (!merged.empty())
            clipProc->setGainEnvelopePoints(merged);

        // Resolve stretch intent from the ValueTree. clipID lets the
        // processor be identified by StretchCache; stretchRatio keys
        // the cache lookup. If a matching rendered entry is ready,
        // adopt it now so the realtime path reads the stretched audio
        // from the first block. The processor retains its pooled preload
        // (decoded_ from DecodedSoundPool) as a fallback (activeBuffer=0).
        int cid = static_cast<int>(clipTree.getProperty(IDs::clipID, -1));
        clipProc->setClipID(cid);
        int stretchMode = static_cast<int>(clipTree.getProperty(IDs::stretchMode, 0));
        double ratio = static_cast<double>(clipTree.getProperty(IDs::stretchRatio, 1.0));
        if (stretchMode != 0 && std::abs(ratio - 1.0) > 1e-4 && stretchCache != nullptr)
        {
            clipProc->setStretchRatio(ratio);
            if (sampleRate > 0.0)
            {
                if (const auto* entry = stretchCache->lookup(cid, ratio, sampleRate))
                {
                    clipProc->adoptStretchedBuffer(
                        entry->data[0].get(), entry->data[1].get(),
                        entry->length, entry->channels);
                }
                else if (!loadingPhase)
                {
                    // Cache miss: request a render. When it completes,
                    // AudioEngine triggers rebuildRoutingGraph, which
                    // rebuilds this clip and adopts the buffer.
                    // Skip during loadingPhase to avoid cascading rebuilds.
                    stretchCache->requestRender(cid, sourcePath, ratio,
                                                sampleRate, formatManager);
                }
            }
        }

        auto node = graph.addNode(std::move(clipProc), std::nullopt, updateKind);
        audioClipNodes[{trackIndex, clipIndex}] = node;
        audioClipSources[{trackIndex, clipIndex}] =
            static_cast<ClipSourceProcessor*>(node->getProcessor());

        // Connect clip source → track input
        graph.addConnection({ { node->nodeID, 0 }, { trackIt->second->nodeID, 0 } }, updateKind);
        graph.addConnection({ { node->nodeID, 1 }, { trackIt->second->nodeID, 1 } }, updateKind);
    }
    else if (clipType == "midi")
    {
        auto clipProc = std::make_unique<MidiClipProcessor>(transportManager);
        clipProc->setClipTree(clipTree);
        clipProc->setStartTime(clipTree.getProperty(IDs::startTime));
        clipProc->setDuration(clipTree.getProperty(IDs::duration));
        clipProc->setGain(clipTree.getProperty(IDs::gain));
        clipProc->setMuted(clipTree.getProperty(IDs::muted, false));
        // Apply the track's MIDI channel to the new clip processor.
        // The track's midiChannel defaults to 1; the user can change
        // it via the track header.
        int trackChannel = 1;
        auto trackList = projectModel.getTrackListTree();
        if (trackIndex < trackList.getNumChildren())
            trackChannel = trackList.getChild(trackIndex).getProperty(IDs::midiChannel, 1);
        clipProc->setMidiChannel(trackChannel);

        auto node = graph.addNode(std::move(clipProc), std::nullopt, updateKind);
        midiClipNodes[{trackIndex, clipIndex}] = node;
        midiClipSources[{trackIndex, clipIndex}] =
            static_cast<MidiClipProcessor*>(node->getProcessor());

        // Connect MIDI clip output → track MIDI input.
        // JUCE's AudioProcessorGraph requires explicit MIDI connections
        // just like audio connections — MIDI does NOT flow automatically.
        bool midiConn = graph.addConnection({ { node->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                              { trackIt->second->nodeID, juce::AudioProcessorGraph::midiChannelIndex } }, updateKind);

        // Also connect stereo audio so JUCE's graph includes this node in
        // its processing order. Without an audio path to the output, the
        // graph skips the node entirely and processBlock is never called.
        // The audio buffer is silence (cleared in processBlock) — this is
        // just a topology requirement.
        bool a0 = graph.addConnection({ { node->nodeID, 0 }, { trackIt->second->nodeID, 0 } }, updateKind);
        bool a1 = graph.addConnection({ { node->nodeID, 1 }, { trackIt->second->nodeID, 1 } }, updateKind);

        if (getenv("HDAW_AUDIO_THREAD_DIAG") != nullptr)
            HDAW_LOG("MidiClipConn", "midiConn=" + juce::String(midiConn ? 1 : 0)
                + " a0=" + juce::String(a0 ? 1 : 0)
                + " a1=" + juce::String(a1 ? 1 : 0)
                + " clipOuts=" + juce::String(node->getProcessor()->getTotalNumOutputChannels())
                + " trackIns=" + juce::String(trackIt->second->getProcessor()->getTotalNumInputChannels()));
    }
}

void RoutingManager::addClip(int trackIndex, int clipIndex, const juce::ValueTree& clipTree,
                             juce::AudioProcessorGraph::UpdateKind updateKind)
{
    // Incremental counterpart of the rebuild loop: adds ONE clip node to the
    // live graph (no graph.clear / prepareToPlay / reconnect). The shared
    // helpers keep construction and crossfade math identical to a full
    // rebuild. Spike constraint: the new clip must be appended at the end of
    // the track's CLIP_LIST (clipIndex == last position); middle inserts must
    // shift the sibling (trackIndex, clipIndex) keys — Task 3 work alongside
    // remove/move.
    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt == trackNodes.end()) return;

    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= trackList.getNumChildren()) return;
    auto trackTree = trackList.getChild(trackIndex);

    // Recompute crossfades over ALL clips on the track (including the new one)
    // so the new clip crossfades against siblings exactly as a full rebuild
    // would.
    auto crossfadeMap = computeTrackCrossfades(trackTree);

    buildClipNode(trackIndex, clipIndex, clipTree, crossfadeMap, updateKind);

    // Re-push merged envelopes to sibling audio clips whose crossfade points
    // changed (the new clip may overlap them). Envelope re-push is the only
    // sibling mutation an incremental add needs — the sibling nodes stay live.
    // Skipped when unchanged, so an unaffected sibling's processor state is
    // preserved bit-for-bit.
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return;
    for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
    {
        if (ci == clipIndex) continue;
        auto sibling = clipList.getChild(ci);
        if (sibling.getProperty(IDs::clipType, "audio").toString() != "audio") continue;
        auto sIt = audioClipSources.find({trackIndex, ci});
        if (sIt == audioClipSources.end()) continue;
        auto merged = computeMergedEnvelopeForClip(sibling, crossfadeMap);
        if (!sameEnvelopePoints(merged, sIt->second->getGainEnvelopePoints()))
            sIt->second->setGainEnvelopePoints(merged);
    }
}

void RoutingManager::removeClip(int trackIndex, int clipIndex,
                                juce::AudioProcessorGraph::UpdateKind updateKind)
{
    // Incremental counterpart of the removeClipsForTrack loop: removes ONE clip
    // node + its live edges from the graph (no graph.clear / prepareToPlay /
    // reconnect). The clip must already be removed from the track's CLIP_LIST
    // (crossfades are recomputed back from the model tree), and should be the
    // LAST child so the (trackIndex, clipIndex) keys of the remaining clips
    // stay valid. Callers hold graphLock (+ pump-park off the message thread).
    const std::pair<int, int> key{trackIndex, clipIndex};

    juce::AudioProcessorGraph::Node::Ptr node;
    bool isMidi = false;
    auto audioIt = audioClipNodes.find(key);
    if (audioIt != audioClipNodes.end())
    {
        node = audioIt->second;
    }
    else
    {
        auto midiIt = midiClipNodes.find(key);
        if (midiIt == midiClipNodes.end())
            return; // no-op: identity key absent
        node = midiIt->second;
        isMidi = true;
    }

    // Drop the live edges first (audio clip→track: two connections; midi
    // clip→track: the MIDI edge + the two audio stubs added by buildClipNode),
    // then the node. removeNode also disconnects internally, but removing the
    // edges explicitly keeps the teardown symmetric with the construction.
    auto trackIt = trackNodes.find(trackIndex);
    if (trackIt != trackNodes.end() && node != nullptr)
    {
        graph.removeConnection({ { node->nodeID, 0 }, { trackIt->second->nodeID, 0 } }, updateKind);
        graph.removeConnection({ { node->nodeID, 1 }, { trackIt->second->nodeID, 1 } }, updateKind);
        if (isMidi)
        {
            graph.removeConnection({ { node->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                                     { trackIt->second->nodeID, juce::AudioProcessorGraph::midiChannelIndex } }, updateKind);
        }
    }
    if (node != nullptr)
        graph.removeNode(node.get(), updateKind);

    // Erase the identity-map entries.
    if (isMidi)
    {
        midiClipNodes.erase(key);
        midiClipSources.erase(key);
    }
    else
    {
        audioClipNodes.erase(key);
        audioClipSources.erase(key);
    }

    // Recompute crossfades over the REMAINING clips and re-push merged
    // envelopes to the surviving audio siblings whose points changed (the
    // removed clip may have overlapped them). Skipped when unchanged, so an
    // unaffected sibling's processor state is preserved bit-for-bit.
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= trackList.getNumChildren()) return;
    auto trackTree = trackList.getChild(trackIndex);
    auto crossfadeMap = computeTrackCrossfades(trackTree);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) return;
    for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
    {
        auto sibling = clipList.getChild(ci);
        if (sibling.getProperty(IDs::clipType, "audio").toString() != "audio") continue;
        auto sIt = audioClipSources.find({trackIndex, ci});
        if (sIt == audioClipSources.end()) continue;
        auto merged = computeMergedEnvelopeForClip(sibling, crossfadeMap);
        if (!sameEnvelopePoints(merged, sIt->second->getGainEnvelopePoints()))
            sIt->second->setGainEnvelopePoints(merged);
    }
}

void RoutingManager::updateClipPlacement(int trackIndex, int clipIndex,
                                         juce::AudioProcessorGraph::UpdateKind updateKind)
{
    (void)updateKind; // No graph mutations in this path — only processor property pushes
    // Incremental placement-change path: re-read the clip's placement
    // properties from the ValueTree and re-push them to the live processor,
    // then recompute the track crossfades and re-apply merged envelopes to the
    // moved clip + overlapping siblings. This closes the gap where the SPSC
    // clip-param path (AudioEngine) pushes properties but never recomputes
    // crossfades on a placement change. Move-within-track only; structural
    // changes (cross-track move, middle insert) route to full rebuild.
    // Callers hold graphLock (+ pump-park off the message thread).
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= trackList.getNumChildren()) return;
    auto trackTree = trackList.getChild(trackIndex);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid() || clipIndex >= clipList.getNumChildren()) return;
    auto clipTree = clipList.getChild(clipIndex);
    if (!clipTree.isValid()) return;

    const std::pair<int, int> key{trackIndex, clipIndex};
    juce::String clipType = clipTree.getProperty(IDs::clipType).toString();
    if (clipType == "audio")
    {
        auto audioIt = audioClipSources.find(key);
        if (audioIt == audioClipSources.end()) return;
        auto* clip = audioIt->second;
        clip->setStartTime(clipTree.getProperty(IDs::startTime));
        clip->setDuration(clipTree.getProperty(IDs::duration));
        clip->setOffset(clipTree.getProperty(IDs::offset));
        clip->setGain(clipTree.getProperty(IDs::gain));
        clip->setFadeIn(clipTree.getProperty(IDs::fadeIn));
        clip->setFadeOut(clipTree.getProperty(IDs::fadeOut));
        clip->setLooping(clipTree.getProperty(IDs::looping));
        clip->setMuted(clipTree.getProperty(IDs::muted, false));
    }
    else if (clipType == "midi")
    {
        auto midiIt = midiClipSources.find(key);
        if (midiIt == midiClipSources.end()) return;
        auto* clip = midiIt->second;
        clip->setStartTime(clipTree.getProperty(IDs::startTime));
        clip->setDuration(clipTree.getProperty(IDs::duration));
        clip->setGain(clipTree.getProperty(IDs::gain));
        clip->setMuted(clipTree.getProperty(IDs::muted, false));
    }
    else
    {
        return;
    }

    // Recompute the track crossfades over ALL clips and re-push merged
    // envelopes to the moved clip + every audio sibling whose points changed.
    auto crossfadeMap = computeTrackCrossfades(trackTree);
    for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
    {
        auto c = clipList.getChild(ci);
        if (c.getProperty(IDs::clipType, "audio").toString() != "audio") continue;
        auto sIt = audioClipSources.find({trackIndex, ci});
        if (sIt == audioClipSources.end()) continue;
        auto merged = computeMergedEnvelopeForClip(c, crossfadeMap);
        if (!sameEnvelopePoints(merged, sIt->second->getGainEnvelopePoints()))
            sIt->second->setGainEnvelopePoints(merged);
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
            case 17: clip->setMuted(value > 0.5f);                      break;
        }
        return;
    }

    auto midiIt = midiClipSources.find({trackIndex, clipIndex});
    if (midiIt != midiClipSources.end())
    {
        auto* clip = midiIt->second;
        if (paramID == 10)
            clip->setGain(value);
        else if (paramID == 17)
            clip->setMuted(value > 0.5f);
    }
}

void RoutingManager::switchClipTake(int trackIndex, int clipIndex, const juce::String& sourceFile)
{
    auto audioIt = audioClipSources.find({trackIndex, clipIndex});
    if (audioIt != audioClipSources.end())
        audioIt->second->switchToSourceFile(sourceFile);
}

void RoutingManager::setClipSourcesNonRealtime(bool nr)
{
    for (auto& kv : audioClipSources)
        kv.second->setNonRealtimeFlag(nr);
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
    trackIt->second->rebuildMidiFXChain(trackTree.getChildWithName(IDs::MIDI_FX_CHAIN));
    auto modulationListTree = trackTree.getChildWithName(IDs::MODULATION_LIST);
    trackIt->second->rebuildModulation(modulationListTree);
}

void RoutingManager::rebuildMidiTrackFX(int trackIndex)
{
    auto trackIt = trackProcessors.find(trackIndex);
    if (trackIt == trackProcessors.end()) return;

    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= trackList.getNumChildren()) return;

    auto trackTree = trackList.getChild(trackIndex);
    trackIt->second->rebuildMidiFXChain(trackTree.getChildWithName(IDs::MIDI_FX_CHAIN));
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

void RoutingManager::rebuildMidiClipCache(juce::ValueTree clipTree)
{
    for (auto& kv : midiClipSources)
    {
        if (kv.second != nullptr && kv.second->getClipTree() == clipTree)
            kv.second->setClipTree(clipTree);
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
