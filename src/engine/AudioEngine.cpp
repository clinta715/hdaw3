#include "AudioEngine.h"

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
    mainProcessor->setTransportManager(&transportManager);

    // Initialize plugin manager — load cache + background scan
    pluginManager.loadCache();
    pluginManager.scanAll();

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
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::PROJECT))
    {
        if (property == IDs::tempo)
        {
            transportManager.setBPM(treeWhosePropertyHasChanged.getProperty(IDs::tempo));
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::TRACK))
    {
        if (property == IDs::volume || property == IDs::pan)
        {
            float value = treeWhosePropertyHasChanged.getProperty(property);
            int paramID = (property == IDs::volume) ? 1 : 2;

            auto trackList = projectModel.getTrackListTree();
            for (int i = 0; i < trackList.getNumChildren(); ++i)
            {
                if (trackList.getChild(i) == treeWhosePropertyHasChanged)
                {
                    ParamUpdate update{ i, paramID, value };
                    spscBridge.pushUpdate(update);
                    break;
                }
            }
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::CLIP))
    {
        if (property == IDs::gain || property == IDs::fadeIn || property == IDs::fadeOut)
        {
            float value = treeWhosePropertyHasChanged.getProperty(property);
            int paramID = (property == IDs::gain) ? 10 :
                          (property == IDs::fadeIn) ? 11 : 12;

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
}
