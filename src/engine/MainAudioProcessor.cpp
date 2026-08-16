#include "MainAudioProcessor.h"
#include "ClipSourceProcessor.h"
#include <exception>

namespace
{

// Runs fn on the JUCE message thread, blocking the caller until it completes
// (and rethrowing any exception the callback produced on the calling thread —
// same marshaling shape as PluginHost::runLifecycleOnMessageThread).
//
// Why this exists — the rebuildTrackFX/rebuildMidiTrackFX race: those entry
// points are called from command/RPC/undo/test threads and used to mutate the
// live Track directly, while the pump thread's async rebuildRoutingGraph
// (handleAsyncUpdate) destroys the whole RoutingManager — and every Track in
// it — via `routingManager = std::move(fresh)`. That overlap is a
// use-after-free (crash inside Track::rebuildFXChain).
//
// Message-thread execution closes the window from every direction:
//  * The async rebuild is itself a message callback, and the pump delivers
//    messages one at a time, so it cannot interleave with our marshaled
//    callback.
//  * Any other thread's direct rebuildRoutingGraph parks the pump
//    (MessageManagerLock precedes graphLock). JUCE's park posts a
//    BlockingMessage that suspends the message thread inside the holder, so
//    (a) the park cannot be acquired while our callback runs (its acquire
//    message is undelivered until we return to the loop), and (b) while
//    another thread holds the park, our callback simply waits and then runs
//    against the post-swap manager — which is why callers must re-check the
//    routingManager pointer INSIDE the callback, not before marshaling.
//
// Why NOT the Gate-12 pump park instead: parking would suspend the message
// thread while Track::rebuildFXChain instantiates plugins in-process —
// AudioPluginFormat::createInstanceFromDescription dispatches to the message
// thread, which the park suspends → self-deadlock (lesson 18, the exact trap
// documented at RoutingManager::prebuildTracks, RoutingManager.cpp:138-157).
// On the message thread that dispatch executes inline.
//
// callFunctionOnMessageThread jasserts if the caller holds a
// MessageManagerLock; no call path into the FX rebuilds holds one. It executes
// the callback inline when already on the message thread, but we check
// explicitly so the null-MessageManager fallback (no pump yet) also runs
// inline, preserving the old direct-call behavior.
template <typename Fn>
void runOnMessageThread(Fn&& fn)
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread())
    {
        fn();
        return;
    }

    struct CallContext
    {
        Fn* fn;
        std::exception_ptr exception;
    } ctx{ &fn, nullptr };

    mm->callFunctionOnMessageThread(
        [](void* userData) -> void*
        {
            auto& c = *static_cast<CallContext*>(userData);
            try { (*c.fn)(); }
            catch (...) { c.exception = std::current_exception(); }
            return nullptr;
        },
        &ctx);

    if (ctx.exception != nullptr)
        std::rethrow_exception(ctx.exception);
}

} // namespace

MainAudioProcessor::MainAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    audioRecorder = std::make_unique<HDAW::AudioRecorder>();
}

MainAudioProcessor::~MainAudioProcessor() = default;

bool MainAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Require at least one output bus (speaker feed). Match input/output
    // channel counts on the main buses so the graph can route symmetrically.
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn = layouts.getMainInputChannelSet();
    if (mainOut.isDisabled()) return false;
    if (!mainIn.isDisabled() && mainIn.size() != mainOut.size()) return false;
    return true;
}

void MainAudioProcessor::setTransportManager(HDAW::TransportManager* tm)
{
    transportManager = tm;
    if (transportManager != nullptr)
        internalPlayHead = std::make_unique<HDAW::InternalPlayHead>(*transportManager);
}

void MainAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (projectModel == nullptr || transportManager == nullptr || formatManager == nullptr) return;

    transportManager->setSampleRate(sampleRate);
    metronome.prepareToPlay(sampleRate);

    // Rebuild arranger chain data for audio-thread consumption
    transportManager->rebuildArrangerChainData(projectModel->getTree());

    // Re-sync loop bounds with the actual sample rate. The initial sync in
    // AudioEngine::initialize() uses the default 44100 Hz, but the audio
    // device may use a different rate (e.g. 48000). Without this re-sync
    // the loop wraps at the wrong position.
    {
        auto transportTree = projectModel->getTransportTree();
        double ls = transportTree.getProperty(IDs::loopStart);
        double le = transportTree.getProperty(IDs::loopEnd);
        transportManager->setLoopStartSample(static_cast<int64_t>(ls * sampleRate));
        transportManager->setLoopEndSample(static_cast<int64_t>(le * sampleRate));
        juce::Logger::writeToLog("MainAudioProcessor::prepareToPlay loop bounds re-synced: "
            "sampleRate=" + juce::String(sampleRate)
            + " loopStart=" + juce::String(ls) + "s (" + juce::String(static_cast<int64_t>(ls * sampleRate)) + " samples)"
            + " loopEnd=" + juce::String(le) + "s (" + juce::String(static_cast<int64_t>(le * sampleRate)) + " samples)");
    }

    // Propagate this processor's host-negotiated bus layout to the graph.
    // The graph's audioOutputNode reads its input-channel count from the
    // graph's own output bus; without this, the IO node reports 0 channels
    // and every master→IO addConnection is silently rejected (no audio
    // reaches the speaker buffer even though the master meter moves).
    graph.setBusesLayout(getBusesLayout());

    routingManager = std::make_unique<HDAW::RoutingManager>(
        graph, *projectModel, *formatManager, *transportManager, pluginManager, stretchCache, decodedPool, streamingPool);
    routingManager->setPlaybackInfo(sampleRate, samplesPerBlock);
    routingManager->rebuildFromValueTree();

    graph.setPlayHead(internalPlayHead.get());
    graph.prepareToPlay(sampleRate, samplesPerBlock);

    // Re-establish output-bound connections after prepareToPlay. Even with
    // the bus layout set above, JUCE finalizes IO-node channel negotiation
    // during prepareToPlay, so connections are re-added here to be safe.
    routingManager->reconnectMasterToOutput();

    // Compute project end sample for auto-stop (same logic as rebuildRoutingGraph).
    {
        double sr = sampleRate;
        if (sr <= 0) sr = 44100.0;
        int64_t maxEnd = 0;
        for (auto& kv : routingManager->getAudioClipSources())
        {
            auto* clip = kv.second;
            if (clip->isLooping()) continue;
            double endSec = clip->getStartTime() + clip->getDuration();
            int64_t endSample = static_cast<int64_t>(endSec * sr);
            if (endSample > maxEnd) maxEnd = endSample;
        }
        for (auto& kv : routingManager->getMidiClipSources())
        {
            auto* clip = kv.second;
            double endSec = clip->getStartTime() + clip->getDuration();
            int64_t endSample = static_cast<int64_t>(endSec * sr);
            if (endSample > maxEnd) maxEnd = endSample;
        }
        transportManager->setProjectEndSample(maxEnd);
    }
}

void MainAudioProcessor::releaseResources()
{
    audioRecorder->stopRecording();
    graph.releaseResources();
    graph.clear();
    routingManager = nullptr;
}

void MainAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // NOTE: this is the audio thread. Per AGENTS.md "Realtime / Audio-Thread
    // Safety", do NOT add HDAW_LOG, juce::String/QString construction, locks,
    // disk I/O, or buffer.getMagnitude() scans here — a previous diagnostic
    // pass added all of the above and caused input lag + dropouts. The
    // silent-output bug that instrumentation was tracking is fixed by the
    // bus-layout propagation in prepareToPlay(); see AGENTS.md
    // "AudioProcessorGraph bus layout must be propagated".

    setPlayHead(internalPlayHead.get());

    // SPSC param updates must always drain even when stopped, so UI knobs
    // don't back up.
    bool clipTimingChanged = false;
    if (spscBridge != nullptr)
    {
        spscBridge->popUpdates([this, &clipTimingChanged](const ParamUpdate& update) {
            if (update.clipIndex >= 0)
            {
                if (routingManager != nullptr)
                    routingManager->updateClipParam(update.trackIndex, update.clipIndex, update.paramID, update.value);
                // Params 13 (startTime), 14 (duration), 16 (looping) affect
                // the project end position — flag for recompute.
                if (update.paramID >= 13 && update.paramID <= 16)
                    clipTimingChanged = true;
                return;
            }
            auto* track = routingManager ? routingManager->getTrackNode(update.trackIndex) : nullptr;
            if (track != nullptr)
            {
                if (update.paramID == 1)
                    track->setVolume(update.value);
                else if (update.paramID == 2)
                    track->setPan(update.value);
                else if (update.paramID == 3)
                    track->setMuted(update.value > 0.5f);
            }
        });
    }

    // Recompute project end if clip timing or looping changed via SPSC.
    if (clipTimingChanged && routingManager != nullptr && transportManager != nullptr)
    {
        double sr = getSampleRate();
        if (sr <= 0) sr = 44100.0;
        int64_t maxEnd = 0;
        for (auto& kv : routingManager->getAudioClipSources())
        {
            auto* clip = kv.second;
            if (clip->isLooping()) continue;
            double endSec = clip->getStartTime() + clip->getDuration();
            int64_t endSample = static_cast<int64_t>(endSec * sr);
            if (endSample > maxEnd) maxEnd = endSample;
        }
        for (auto& kv : routingManager->getMidiClipSources())
        {
            auto* clip = kv.second;
            double endSec = clip->getStartTime() + clip->getDuration();
            int64_t endSample = static_cast<int64_t>(endSec * sr);
            if (endSample > maxEnd) maxEnd = endSample;
        }
        transportManager->setProjectEndSample(maxEnd);
    }

    // Silence the entire graph when transport is stopped and not recording
    // or counting in.  Without this, clip sources at position 0 read the
    // same block of samples every callback (position never advances),
    // producing an audible buzz at the buffer-rate frequency.
    if (transportManager != nullptr
        && !transportManager->isPlayingNow()
        && !transportManager->isRecordingNow()
        && !countInActive.load())
    {
        buffer.clear();
        midiMessages.clear();
        transportManager->advance(buffer.getNumSamples());
        return;
    }

    if (countInActive.load() && transportManager)
    {
        if (transportManager->getCurrentSample() >= pendingRecordStartSample)
        {
            countInActive.store(false);
            metronome.setEnabled(wasMetronomeOn);
            recordStartPending.store(true);
        }
    }

    if (transportManager && transportManager->isRecordingNow())
    {
        if (transportManager->isPunchEnabled() && transportManager->isLoopingNow())
        {
            int64_t current = transportManager->getCurrentSample();
            int64_t loopStart = transportManager->getLoopStartSample();
            int64_t loopEnd = transportManager->getLoopEndSample();
            if (current >= loopStart && current < loopEnd)
                audioRecorder->processBlock(buffer);
        }
        else
        {
            audioRecorder->processBlock(buffer);
        }
    }

    if (graphLock.tryEnter())
    {
        graphRebuildPending.store(false, std::memory_order_release);
        if (midiLock.tryEnter())
        {
            midiMessages.addEvents(pendingMidi, 0, -1, 0);
            pendingMidi.clear();
            midiLock.exit();
        }
        graph.processBlock(buffer, midiMessages);
        graphLock.exit();
    }
    else
    {
        buffer.clear();
        midiMessages.clear();
    }

    if (transportManager != nullptr)
    {
        metronome.processBlock(buffer, *transportManager);
        transportManager->advance(buffer.getNumSamples());

        if (transportManager->isPunchEnabled() && transportManager->isRecordingNow())
        {
            int64_t current = transportManager->getCurrentSample();
            int64_t loopEnd = transportManager->getLoopEndSample();
            if (current >= loopEnd)
                transportManager->requestPunchOut();
        }
    }
}

bool MainAudioProcessor::startRecording()
{
    if (transportManager == nullptr) return false;

    if (countInEnabled)
    {
        double bpm = transportManager->getBpmAtTime(
            static_cast<double>(transportManager->getCurrentSample()) / transportManager->getSampleRate());
        double sr = transportManager->getSampleRate();
        double countInSec = static_cast<double>(countInBars) * 4.0 * 60.0 / bpm;
        pendingRecordStartSample = transportManager->getCurrentSample()
            + static_cast<int64_t>(countInSec * sr);
        countInActive.store(true);
        wasMetronomeOn = metronome.isEnabled();
        metronome.setEnabled(true);
return true;
}

    return beginActualRecording();
}

void MainAudioProcessor::updateClipGainEnvelope(int clipId, const std::vector<HDAW::ClipSourceProcessor::GainPoint>& points)
{
    auto* routingManager = getRoutingManager();
    if (!routingManager) return;
    
    for (auto& kv : routingManager->getAudioClipSources())
    {
        if (kv.second->getClipID() == clipId)
        {
            kv.second->setGainEnvelopePoints(points);
            break;
        }
    }
}

bool MainAudioProcessor::beginActualRecording()
{
    if (transportManager == nullptr) return false;

    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    auto recDir = appData.getChildFile("HDAW").getChildFile("recordings");
    recDir.createDirectory();

    auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    auto recFile = recDir.getChildFile("rec_" + timestamp + ".wav");

    double sr = getSampleRate();
    int numChannels = getTotalNumInputChannels();

    if (!audioRecorder->startRecording(recFile, sr, numChannels))
        return false;

    recordingStartSample = transportManager->getCurrentSample();
    transportManager->setRecording(true);
    return true;
}

void MainAudioProcessor::stopRecording()
{
    if (countInActive.load())
    {
        countInActive.store(false);
        metronome.setEnabled(wasMetronomeOn);
        transportManager->setPlaying(false);
        return;
    }

    if (!audioRecorder->isRecording()) return;

    auto recordedFile = audioRecorder->stopRecording();
    transportManager->setRecording(false);

    if (projectModel != nullptr && recordedFile.existsAsFile())
    {
        auto trackList = projectModel->getTrackListTree();
        auto& um = projectModel->getUndoManager();

        double sr = transportManager ? transportManager->getSampleRate() : getSampleRate();
        double startTimeSec = static_cast<double>(recordingStartSample) / sr;

        double recDuration = 4.0;
        if (formatManager != nullptr)
        {
            if (auto reader = formatManager->createReaderFor(recordedFile))
            {
                recDuration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
                delete reader;
            }
        }

        int targetTrack = -1;
        for (int i = 0; i < trackList.getNumChildren(); ++i)
        {
            if (trackList.getChild(i).getProperty(IDs::isArm))
            {
                targetTrack = i;
                break;
            }
        }
        if (targetTrack < 0 && trackList.getNumChildren() > 0)
            targetTrack = 0;

        if (targetTrack >= 0)
        {
            auto trackTree = trackList.getChild(targetTrack);
            auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
            if (!clipList.isValid())
            {
                clipList = juce::ValueTree(IDs::CLIP_LIST);
                trackTree.addChild(clipList, -1, &um);
            }

            double recEnd = startTimeSec + recDuration;
            int overlapClip = -1;
            for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
            {
                auto c = clipList.getChild(ci);
                if (c.getProperty(IDs::clipType).toString() != "audio") continue;
                double cStart = c.getProperty(IDs::startTime);
                double cDur = c.getProperty(IDs::duration);
                if (startTimeSec < cStart + cDur && recEnd > cStart)
                {
                    overlapClip = ci;
                    break;
                }
            }

            if (overlapClip >= 0)
            {
                auto clipTree = clipList.getChild(overlapClip);
                auto takeList = clipTree.getChildWithName(IDs::TAKE_LIST);
                if (!takeList.isValid())
                {
                    takeList = juce::ValueTree(IDs::TAKE_LIST);
                    clipTree.addChild(takeList, -1, &um);

                    auto origSource = clipTree.getProperty(IDs::sourceFile).toString();
                    auto origTake = juce::ValueTree(IDs::TAKE);
                    origTake.setProperty(IDs::sourceFile, origSource, nullptr);
                    origTake.setProperty(IDs::name, "Take 1", nullptr);
                    takeList.addChild(origTake, -1, nullptr);
                }

                auto newTake = juce::ValueTree(IDs::TAKE);
                newTake.setProperty(IDs::sourceFile, recordedFile.getFullPathName(), nullptr);
                newTake.setProperty(IDs::name, "Take " + juce::String(takeList.getNumChildren() + 1), nullptr);
                takeList.addChild(newTake, -1, &um);
                clipTree.setProperty(IDs::activeTake, takeList.getNumChildren() - 1, &um);

                rebuildRoutingGraph();
            }
            else
            {
                auto clip = ProjectModel::createAudioClip(
                    "Recording", startTimeSec, recDuration, recordedFile.getFullPathName());
                clipList.addChild(clip, -1, &um);
                rebuildRoutingGraph();
            }
        }
    }
}

bool MainAudioProcessor::isRecording() const
{
    return audioRecorder && audioRecorder->isRecording();
}

HDAW::Track* MainAudioProcessor::getTrack(int index) const
{
    return routingManager ? routingManager->getTrackNode(index) : nullptr;
}

void MainAudioProcessor::toggleFXEditor(int trackIndex, int slotIndex)
{
    // Marshaled to the message thread — see the runOnMessageThread rationale
    // block above (live-Track destruction race; also fixes editor affinity).
    runOnMessageThread([this, trackIndex, slotIndex]
    {
        if (routingManager != nullptr)
        {
            auto* track = routingManager->getTrackNode(trackIndex);
            if (track != nullptr)
                track->toggleFXEditor(slotIndex);
        }
    });
}

void MainAudioProcessor::rebuildTrackFX(int trackIndex)
{
    // Marshaled to the message thread (see runOnMessageThread above); the
    // routingManager check must live inside the callback — the manager can be
    // swapped by a rebuild that completes while we waited for the pump.
    runOnMessageThread([this, trackIndex]
    {
        if (routingManager != nullptr)
            routingManager->rebuildTrackFX(trackIndex);
    });
}

void MainAudioProcessor::rebuildMidiTrackFX(int trackIndex)
{
    runOnMessageThread([this, trackIndex]
    {
        if (routingManager != nullptr)
            routingManager->rebuildMidiTrackFX(trackIndex);
    });
}

void MainAudioProcessor::rebuildMidiClipCache(juce::ValueTree clipTree)
{
    // Marshaled to the message thread — see the runOnMessageThread rationale
    // block above (live MIDI-clip cache invalidation vs graph-rebuild destruction).
    runOnMessageThread([this, clipTree]
    {
        if (routingManager != nullptr)
            routingManager->rebuildMidiClipCache(clipTree);
    });
}

void MainAudioProcessor::rebuildModulation(int trackIndex)
{
    // Marshaled to the message thread — see the runOnMessageThread rationale
    // block above (live-Track destruction race).
    runOnMessageThread([this, trackIndex]
    {
        if (routingManager == nullptr) return;
        auto* track = routingManager->getTrackNode(trackIndex);
        if (track == nullptr) return;
        if (projectModel == nullptr) return;
        auto trackList = projectModel->getTrackListTree();
        if (trackIndex >= trackList.getNumChildren()) return;
        auto trackTree = trackList.getChild(trackIndex);
        auto modList = trackTree.getChildWithName(IDs::MODULATION_LIST);
        track->rebuildModulation(modList);
    });
}

void MainAudioProcessor::rebuildAutomationCache(int trackIndex)
{
    // Marshaled to the message thread — see the runOnMessageThread rationale
    // block above (live-Track destruction race).
    runOnMessageThread([this, trackIndex]
    {
        if (routingManager == nullptr) return;
        auto* track = routingManager->getTrackNode(trackIndex);
        if (track == nullptr) return;

        if (projectModel != nullptr)
        {
            auto trackList = projectModel->getTrackListTree();
            auto trackTree = trackList.getChild(trackIndex);
            track->setAutomationTrees(trackTree.getChildWithName(IDs::AUTOMATION_LIST));
        }
        else
        {
            for (int i = 0; i < track->getNumAutomations(); ++i)
                track->getAutomation(i).rebuildCache();
        }
    });
}

void MainAudioProcessor::rebuildRoutingGraph(bool loading)
{
    if (routingManager != nullptr && projectModel != nullptr)
    {
        if (decodedPool != nullptr)
            decodedPool->pruneUnreferenced();
        if (streamingPool != nullptr)
            streamingPool->pruneUnreferenced();

        // The JUCE message pump thread (MessagePumpThread) concurrently
        // dispatches AudioProcessorGraph's internal async rebuild messages
        // (Pimpl::handleAsyncUpdate iterates the live node list). A rebuild
        // here clears + re-adds every node on the calling thread; if that
        // thread is not the message thread, park the pump for the duration
        // so a queued graph-internal message cannot iterate nodes that this
        // rebuild frees mid-flight (access violation). On the message thread
        // (engine handleAsyncUpdate dispatched by the pump) dispatch and
        // mutation are already serialized, and MessageManagerLock would
        // self-deadlock, so skip it.
        // Two-phase rebuild: the replacement RoutingManager (and, when plugins
        // run in-process, its plugin FX instances) is built BEFORE parking the
        // pump. JUCE plugin instantiation dispatches to the message thread
        // (AudioPluginFormat::createInstanceFromDescription); with the pump
        // parked that dispatch never runs and the rebuild deadlocks. Isolated
        // mode spawns child processes instead and needs no message thread, so
        // it keeps the single-phase path.
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        const bool needsPark = (mm != nullptr && !mm->isThisTheMessageThread());

        auto fresh = std::make_unique<HDAW::RoutingManager>(
            graph, *projectModel, *formatManager, *transportManager, pluginManager, stretchCache, decodedPool, streamingPool);
        fresh->loadingPhase = loading;
        if (needsPark && pluginManager != nullptr && !pluginManager->isolationEnabled)
            fresh->prebuildTracks();

        std::optional<juce::MessageManagerLock> pumpPark;
        if (needsPark)
            pumpPark.emplace();

        graphRebuildPending.store(true, std::memory_order_release);
        graphLock.enter();
        graph.clear();
        graph.setBusesLayout(getBusesLayout());
        routingManager = std::move(fresh);
        routingManager->rebuildFromValueTree();
        graph.setPlayHead(internalPlayHead.get());
        if (getSampleRate() > 0)
        {
            graph.prepareToPlay(getSampleRate(), getBlockSize());
            routingManager->reconnectMasterToOutput();
        }
        graphLock.exit();
        graphRebuildPending.store(false, std::memory_order_release);

        // Release the pump park BEFORE re-baking (lesson 18): the bake may touch
        // node processors; plugin work must not run while the pump is parked.
        pumpPark.reset();

        // Fix A (lesson 21): force a render-sequence re-bake so the sequence
        // baked during the PREVIOUS playback (pinning old Node::Ptr -> Tracks ->
        // FX slots -> plugin proxies -> child processes) is released.
        // graph.clear() alone leaves it pinned until the next processBlock
        // re-bake, so a load with stopped transport leaked the whole previous
        // graph's plugin children. Outside graphLock + outside the park.
        // Mirrors the drain-path precedent (AudioEngine.cpp:1491).
        if (routingManager != nullptr)
            routingManager->rebuildGraph();

        // Close the release handshake (lesson 21): the bake installs the NEW
        // sequence in the graph's mainThreadState, but the OLD sequence stays
        // pinned in audioThreadState until the audio thread's next
        // graph.processBlock swaps it (RenderSequenceExchange). With stopped
        // transport this processor's processBlock early-outs before
        // graph.processBlock, so the swap never happens and the old sequence
        // keeps the previous graph's plugin children alive indefinitely.
        // Drive one graph.processBlock on a scratch buffer to force the swap;
        // the graph's internal 500 ms timer then frees the old sequence (now in
        // mainThreadState), destroying the stale proxies and their children.
        //
        // Ordering: off the message thread graph.rebuild() only QUEUES the bake
        // (a message on the pump), so the drive must run on the message thread
        // AFTER it — runOnMessageThread posts behind the queued bake (FIFO) and
        // runs inline when already on the message thread (where the bake above
        // was synchronous). The drive takes graphLock: the real audio thread
        // tryEnter()s it in processBlock and skips on contention (silence), so
        // the two can never interleave (same discipline as respawn, lesson 14).
        // No audio-thread work is added: this runs on the rebuild caller's
        // thread / the message thread only.
        if (routingManager != nullptr)
        {
            runOnMessageThread([this]()
            {
                graphLock.enter();
                const int numChannels = juce::jmax(1, getTotalNumOutputChannels());
                const int numSamples = getBlockSize() > 0 ? getBlockSize() : 512;
                juce::AudioBuffer<float> scratch(numChannels, numSamples);
                scratch.clear();
                juce::MidiBuffer midi;
                graph.processBlock(scratch, midi);
                graphLock.exit();
            });
        }

        recomputeProjectEndSample();
    }
}

void MainAudioProcessor::recomputeProjectEndSample()
{
    // Compute project end: the latest sample position across all clips.
    // The transport auto-stops when it reaches this position (unless looping).
    // Extracted from rebuildRoutingGraph so the incremental routing drain can
    // refresh it after a batched clip mutation without a full rebuild.
    if (routingManager == nullptr || transportManager == nullptr)
        return;
    double sr = getSampleRate();
    if (sr <= 0) sr = 44100.0;
    int64_t maxEnd = 0;
    for (auto& kv : routingManager->getAudioClipSources())
    {
        auto* clip = kv.second;
        if (clip->isLooping()) continue;   // looping clips don't bound the project
        double endSec = clip->getStartTime() + clip->getDuration();
        int64_t endSample = static_cast<int64_t>(endSec * sr);
        if (endSample > maxEnd) maxEnd = endSample;
    }
    for (auto& kv : routingManager->getMidiClipSources())
    {
        auto* clip = kv.second;
        double endSec = clip->getStartTime() + clip->getDuration();
        int64_t endSample = static_cast<int64_t>(endSec * sr);
        if (endSample > maxEnd) maxEnd = endSample;
    }
    transportManager->setProjectEndSample(maxEnd);
}

HDAW::LevelMeter& MainAudioProcessor::getMasterMeter()
{
    if (routingManager != nullptr && routingManager->getMasterBus() != nullptr)
        return routingManager->getMasterBus()->getMeter();
    static HDAW::LevelMeter fallbackMeter;
    return fallbackMeter;
}

void MainAudioProcessor::addExternalMidiMessage(const juce::MidiMessage& msg)
{
    const juce::SpinLock::ScopedLockType sl(midiLock);
    pendingMidi.addEvent(msg, 0);
}
