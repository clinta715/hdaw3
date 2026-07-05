#include "MainAudioProcessor.h"

MainAudioProcessor::MainAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    audioRecorder = std::make_unique<HDAW::AudioRecorder>();
}

MainAudioProcessor::~MainAudioProcessor() = default;

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

    routingManager = std::make_unique<HDAW::RoutingManager>(
        graph, *projectModel, *formatManager, *transportManager, pluginManager);
    routingManager->setPlaybackInfo(sampleRate, samplesPerBlock);
    routingManager->rebuildFromValueTree();

    graph.setPlayHead(internalPlayHead.get());
    graph.prepareToPlay(sampleRate, samplesPerBlock);
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

    setPlayHead(internalPlayHead.get());

    if (countInActive.load() && transportManager)
    {
        if (transportManager->getCurrentSample() >= pendingRecordStartSample)
        {
            countInActive.store(false);
            metronome.setEnabled(wasMetronomeOn);
            beginActualRecording();
        }
    }

    if (transportManager && transportManager->isRecordingNow())
        audioRecorder->processBlock(buffer);

    if (spscBridge != nullptr)
    {
        spscBridge->popUpdates([this](const ParamUpdate& update) {
            if (update.clipIndex >= 0)
            {
                if (routingManager != nullptr)
                    routingManager->updateClipParam(update.trackIndex, update.clipIndex, update.paramID, update.value);
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

    if (graphLock.tryEnter())
    {
        graphRebuildPending.store(false, std::memory_order_release);
        {
            const juce::ScopedLock sl(midiLock);
            midiMessages.addEvents(pendingMidi, 0, -1, 0);
            pendingMidi.clear();
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
    if (routingManager != nullptr)
    {
        auto* track = routingManager->getTrackNode(trackIndex);
        if (track != nullptr)
            track->toggleFXEditor(slotIndex);
    }
}

void MainAudioProcessor::rebuildTrackFX(int trackIndex)
{
    if (routingManager != nullptr)
        routingManager->rebuildTrackFX(trackIndex);
}

void MainAudioProcessor::rebuildModulation(int trackIndex)
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
}

void MainAudioProcessor::rebuildAutomationCache(int trackIndex)
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
}

void MainAudioProcessor::rebuildRoutingGraph()
{
    if (routingManager != nullptr && projectModel != nullptr)
    {
        graphRebuildPending.store(true, std::memory_order_release);
        graphLock.enter();
        graph.clear();
        routingManager = std::make_unique<HDAW::RoutingManager>(
            graph, *projectModel, *formatManager, *transportManager, pluginManager);
        routingManager->rebuildFromValueTree();
        graph.setPlayHead(internalPlayHead.get());
        if (getSampleRate() > 0)
            graph.prepareToPlay(getSampleRate(), getBlockSize());
        graphLock.exit();
        graphRebuildPending.store(false, std::memory_order_release);
    }
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
    const juce::ScopedLock sl(midiLock);
    pendingMidi.addEvent(msg, 0);
}
