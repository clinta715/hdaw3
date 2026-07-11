#include "AudioEngine.h"
#include <juce_events/juce_events.h>

AudioEngine::AudioEngine()
{
    mainProcessor = std::make_unique<MainAudioProcessor>();
    projectModel.getTree().addListener(this);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}
void AudioEngine::initialize()
{
    // Link bridge, project model, and format manager to processor
    mainProcessor->setBridge(&spscBridge);
    mainProcessor->setProjectModel(&projectModel);
    mainProcessor->setFormatManager(projectPool.getFormatManager());
    mainProcessor->setPluginManager(&pluginManager);

    // Setup transport
    transportManager.setBPM(projectModel.getTree().getProperty(IDs::tempo));
    rebuildTempoMap();

    // Sync loop state from ValueTree to TransportManager atomics.
    // The listener only fires on property changes, so we must push the
    // initial values here during setup to ensure advance() has valid
    // loopStartSample / loopEndSample from the start.
    {
        auto transportTree = projectModel.getTransportTree();
        transportManager.setLooping(transportTree.getProperty(IDs::isLooping));
        double sr = transportManager.getSampleRate();
        transportManager.setLoopStartSample(
            static_cast<int64_t>(static_cast<double>(transportTree.getProperty(IDs::loopStart)) * sr));
        transportManager.setLoopEndSample(
            static_cast<int64_t>(static_cast<double>(transportTree.getProperty(IDs::loopEnd)) * sr));
    }

    {
        auto transportTree = projectModel.getTransportTree();
        int tsNum = transportTree.getProperty(IDs::timeSigNumerator, 4);
        mainProcessor->getMetronome().setBeatsPerBar(tsNum > 0 ? tsNum : 4);
    }

    mainProcessor->setTransportManager(&transportManager);

    // Initialize plugin manager — load cache (scan happens asynchronously after UI starts)
    pluginManager.loadCache();

    // Initialize default audio device (2 in, 2 out)
    auto error = deviceManager.initialiseWithDefaultDevices(2, 2);
    
    if (error.isNotEmpty())
    {
        juce::Logger::writeToLog("AudioEngine::initialize Error: " + error);
    }

    // Connect processor to player (triggers prepareToPlay → RoutingManager rebuild)
    processorPlayer.setProcessor(mainProcessor.get());
    
    // Add player as audio callback
    deviceManager.addAudioCallback(&processorPlayer);

    // Wire MIDI input to processor
    midiInputManager.setNoteCallback([this](const juce::MidiMessage& msg) {
        // If CC recording is armed and the transport is playing, capture
        // controller events and dispatch them to the main thread. The audio
        // thread is never allowed to touch the ValueTree, so we route through
        // the message manager.
        if (msg.isController() && midiCcRecordArmed
            && transportManager.isPlayingNow() && midiCcCallback)
        {
            int controller = msg.getControllerNumber();
            int value = msg.getControllerValue();
            juce::MessageManager::callAsync([this, controller, value]() {
                if (midiCcCallback)
                    midiCcCallback(controller, value);
            });
        }
        mainProcessor->addExternalMidiMessage(msg);
    });

    commands = std::make_unique<AudioEngineCommands>(*this);
    readModel = std::make_unique<ReadModelImpl>(projectModel);
}

void AudioEngine::shutdown()
{
    projectModel.getTree().removeListener(this);
    deviceManager.removeAudioCallback(&processorPlayer);
    processorPlayer.setProcessor(nullptr);
}

float AudioEngine::getTrackVolume(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::volume);
    return 0.0f;
}

void AudioEngine::setTrackVolume(int trackIndex, float volume)
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
    {
        trackList.getChild(trackIndex).setProperty(IDs::volume, static_cast<double>(volume), &projectModel.getUndoManager());
    }
}

float AudioEngine::getTrackPan(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::pan);
    return 0.0f;
}

void AudioEngine::setTrackPan(int trackIndex, float pan)
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
    {
        trackList.getChild(trackIndex).setProperty(IDs::pan, static_cast<double>(pan), &projectModel.getUndoManager());
    }
}

bool AudioEngine::isTrackMuted(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::isMuted);
    return false;
}

void AudioEngine::setTrackMuted(int trackIndex, bool muted)
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
    {
        trackList.getChild(trackIndex).setProperty(IDs::isMuted, muted, &projectModel.getUndoManager());
        ParamUpdate update{ trackIndex, 3, muted ? 1.0f : 0.0f };
        spscBridge.pushUpdate(update);
    }
}

bool AudioEngine::isTrackArmed(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::isArm);
    return false;
}

void AudioEngine::setTrackArmed(int trackIndex, bool armed)
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::isArm, armed, &projectModel.getUndoManager());
}

juce::String AudioEngine::getTrackName(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::name).toString();
    return {};
}

void AudioEngine::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    if (treeWhosePropertyHasChanged.hasType(IDs::TRANSPORT))
    {
        if (property == IDs::isPlaying)
        {
            bool playing = treeWhosePropertyHasChanged.getProperty(IDs::isPlaying);
            transportManager.setPlaying(playing);
            juce::Logger::writeToLog("AudioEngine: Playback state changed to: " + juce::String(playing ? "Playing" : "Stopped"));
        }
        else if (property == IDs::position)
        {
            double pos = treeWhosePropertyHasChanged.getProperty(IDs::position);
            transportManager.setCurrentSample(static_cast<int64_t>(pos * transportManager.getSampleRate()));
        }
        else if (property == IDs::isLooping)
        {
            transportManager.setLooping(treeWhosePropertyHasChanged.getProperty(IDs::isLooping));
        }
        else if (property == IDs::loopStart)
        {
            double t = treeWhosePropertyHasChanged.getProperty(IDs::loopStart);
            transportManager.setLoopStartSample(static_cast<int64_t>(t * transportManager.getSampleRate()));
        }
        else if (property == IDs::loopEnd)
        {
            double t = treeWhosePropertyHasChanged.getProperty(IDs::loopEnd);
            transportManager.setLoopEndSample(static_cast<int64_t>(t * transportManager.getSampleRate()));
        }
        else if (property == IDs::metronomeEnabled)
        {
            mainProcessor->getMetronome().setEnabled(
                treeWhosePropertyHasChanged.getProperty(IDs::metronomeEnabled));
        }
        else if (property == IDs::timeSigNumerator)
        {
            int num = treeWhosePropertyHasChanged.getProperty(IDs::timeSigNumerator);
            mainProcessor->getMetronome().setBeatsPerBar(num > 0 ? num : 4);
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::PROJECT))
    {
        if (property == IDs::tempo)
        {
            transportManager.setBPM(treeWhosePropertyHasChanged.getProperty(IDs::tempo));
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::TEMPO_POINT))
    {
        rebuildTempoMap();
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::TRACK))
    {
        if (property == IDs::midiChannel)
        {
            int newChannel = treeWhosePropertyHasChanged.getProperty(IDs::midiChannel);
            auto trackList = projectModel.getTrackListTree();
            int tIdx = -1;
            for (int i = 0; i < trackList.getNumChildren(); ++i)
            {
                if (trackList.getChild(i) == treeWhosePropertyHasChanged)
                {
                    tIdx = i;
                    break;
                }
            }
            if (tIdx >= 0 && mainProcessor != nullptr)
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->setTrackMidiChannel(tIdx, newChannel);
            }
        }
        else if (property == IDs::volume || property == IDs::pan || property == IDs::isMuted)
        {
            float value = treeWhosePropertyHasChanged.getProperty(property);
            int paramID = (property == IDs::volume) ? 1
                        : (property == IDs::pan)     ? 2
                                                     : 3;

            auto trackList = projectModel.getTrackListTree();
            for (int i = 0; i < trackList.getNumChildren(); ++i)
            {
                if (trackList.getChild(i) == treeWhosePropertyHasChanged)
                {
                    ParamUpdate update{ i, paramID, value };
                    spscBridge.pushUpdate(update);

                    if (transportManager.isPlayingNow())
                    {
                        auto autoList = treeWhosePropertyHasChanged.getChildWithName(IDs::AUTOMATION_LIST);
                        for (int a = 0; a < autoList.getNumChildren(); ++a)
                        {
                            auto autoTree = autoList.getChild(a);
                            if (static_cast<int>(autoTree.getProperty(IDs::paramID)) == paramID)
                            {
                                double timeSec = static_cast<double>(transportManager.getCurrentSample())
                                    / transportManager.getSampleRate();
                                auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
                                if (!pointList.isValid())
                                {
                                    pointList = juce::ValueTree(IDs::POINT_LIST);
                                    autoTree.addChild(pointList, -1, nullptr);
                                }
                                juce::ValueTree pt(IDs::POINT);
                                pt.setProperty(IDs::startTime, timeSec, nullptr);
                                pt.setProperty(IDs::gain, static_cast<double>(value), nullptr);
                                pointList.addChild(pt, -1, nullptr);
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::CLIP))
    {
        if (property == IDs::gain    || property == IDs::fadeIn  || property == IDs::fadeOut ||
            property == IDs::startTime || property == IDs::duration ||
            property == IDs::offset   || property == IDs::looping)
        {
            float value = treeWhosePropertyHasChanged.getProperty(property);
            int paramID;
            if      (property == IDs::gain)      paramID = 10;
            else if (property == IDs::fadeIn)    paramID = 11;
            else if (property == IDs::fadeOut)   paramID = 12;
            else if (property == IDs::startTime) paramID = 13;
            else if (property == IDs::duration)  paramID = 14;
            else if (property == IDs::offset)    paramID = 15;
            else                                  paramID = 16; // looping

            // Find which track + clip index this belongs to
            auto trackList = projectModel.getTrackListTree();
            for (int t = 0; t < trackList.getNumChildren(); ++t)
            {
                auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                if (!clipList.isValid()) continue;

                for (int c = 0; c < clipList.getNumChildren(); ++c)
                {
                    if (clipList.getChild(c) == treeWhosePropertyHasChanged)
                    {
                        ParamUpdate update{ t, paramID, value, c };
                        spscBridge.pushUpdate(update);
                        break;
                    }
                }
            }
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::MIDI_NOTE))
    {
        auto noteList = treeWhosePropertyHasChanged.getParent();
        if (noteList.isValid() && noteList.hasType(IDs::MIDI_NOTE_LIST))
        {
            auto clipTree = noteList.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP) && mainProcessor != nullptr)
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);
            }
        }
    }
}

void AudioEngine::valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded)
{
    if (childWhichHasBeenAdded.hasType(IDs::TRANSPORT))
    {
        // A new transport node was added (e.g. after File→New).
        // Properties were set before addChild, so valueTreePropertyChanged
        // never fired for them. Push the initial state to the audio thread.
        transportManager.setLooping(childWhichHasBeenAdded.getProperty(IDs::isLooping));
        double sr = transportManager.getSampleRate();
        transportManager.setLoopStartSample(static_cast<int64_t>(
            static_cast<double>(childWhichHasBeenAdded.getProperty(IDs::loopStart)) * sr));
        transportManager.setLoopEndSample(static_cast<int64_t>(
            static_cast<double>(childWhichHasBeenAdded.getProperty(IDs::loopEnd)) * sr));
        transportManager.setPlaying(childWhichHasBeenAdded.getProperty(IDs::isPlaying));
    }
    if (parentTree.hasType(IDs::TEMPO_POINT_LIST) || parentTree.hasType(IDs::PROJECT))
        rebuildTempoMap();

    if (childWhichHasBeenAdded.hasType(IDs::MIDI_NOTE) && mainProcessor != nullptr)
    {
        if (parentTree.hasType(IDs::MIDI_NOTE_LIST))
        {
            auto clipTree = parentTree.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP))
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);
            }
        }
    }
}

void AudioEngine::valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichItWasRemoved)
{
    juce::ignoreUnused(indexFromWhichItWasRemoved);
    if (parentTree.hasType(IDs::TEMPO_POINT_LIST) || parentTree.hasType(IDs::PROJECT))
        rebuildTempoMap();

    if (childWhichHasBeenRemoved.hasType(IDs::MIDI_NOTE) && mainProcessor != nullptr)
    {
        if (parentTree.hasType(IDs::MIDI_NOTE_LIST))
        {
            auto clipTree = parentTree.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP))
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);
            }
        }
    }
}

void AudioEngine::rebuildTempoMap()
{
    auto tempoList = projectModel.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid())
        return;

    auto map = std::make_shared<std::vector<HDAW::TempoPoint>>();
    for (int i = 0; i < tempoList.getNumChildren(); ++i)
    {
        auto pt = tempoList.getChild(i);
        double t = pt.getProperty(IDs::startTime);
        double b = pt.getProperty(IDs::tempo);
        map->push_back({ t, b });
    }

    std::sort(map->begin(), map->end(),
              [](const HDAW::TempoPoint& a, const HDAW::TempoPoint& b) {
                  return a.timeInSeconds < b.timeInSeconds;
              });

    transportManager.setTempoMap(map);
}

ProjectCommands& AudioEngine::getProjectCommands()  { return *commands; }
TransportCommands& AudioEngine::getTransportCommands() { return *commands; }
AudioGraphCommands& AudioEngine::getAudioGraphCommands() { return *commands; }
ReadModel& AudioEngine::getReadModel() { return *readModel; }
