#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"

// ─── ProjectCommands — Track operations ───────────────────────────

int AudioEngineCommands::addTrack(const std::string& name, int color, int parentBus)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    auto track = createTrackValueTree(name, color, parentBus);
    int idx = trackList.getNumChildren();
    trackList.addChild(track, idx, &um);
    return idx;
}

void AudioEngineCommands::removeTrack(int trackIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.removeChild(trackIndex, &um);
}

void AudioEngineCommands::moveTrack(int trackIndex, int newIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return;
    if (newIndex < 0 || newIndex >= trackList.getNumChildren()) return;
    if (trackIndex == newIndex) return;
    auto track = trackList.getChild(trackIndex);
    trackList.removeChild(trackIndex, &um);
    if (newIndex > trackIndex) --newIndex;
    trackList.addChild(track, newIndex, &um);
}

void AudioEngineCommands::setTrackName(int trackIndex, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::name, juce::String(name), &um);
}

void AudioEngineCommands::setTrackColor(int trackIndex, int color)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::color, color, &um);
}

void AudioEngineCommands::setTrackVolume(int trackIndex, float volume)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::volume, static_cast<double>(volume), &um);
}

void AudioEngineCommands::setTrackPan(int trackIndex, float pan)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::pan, static_cast<double>(pan), &um);
}

void AudioEngineCommands::setTrackMuted(int trackIndex, bool muted)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::isMuted, muted, &um);
}

void AudioEngineCommands::setTrackSoloed(int trackIndex, bool soloed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::isSoloed, soloed, &um);
}

void AudioEngineCommands::setTrackArmed(int trackIndex, bool armed)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::isArm, armed, &um);
}

void AudioEngineCommands::setTrackInputMonitor(int trackIndex, bool monitor)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::inputMonitor, monitor, &um);
}

void AudioEngineCommands::setTrackHeight(int trackIndex, int height)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::trackHeight, static_cast<double>(height), &um);
}

void AudioEngineCommands::setTrackMidiChannel(int trackIndex, int channel)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::midiChannel, channel, &um);
}

int AudioEngineCommands::duplicateTrack(int trackIndex)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;
    auto source = trackList.getChild(trackIndex);
    auto copy = source.createCopy();

    auto origName = copy.getProperty(IDs::name).toString();
    if (!origName.endsWith(" copy"))
        copy.setProperty(IDs::name, origName + " copy", &um);

    auto clipList = copy.getChildWithName(IDs::CLIP_LIST);
    for (int c = 0; c < clipList.getNumChildren(); ++c)
    {
        auto clip = clipList.getChild(c);
        clip.setProperty(IDs::clipID, model.allocateClipID(), nullptr);

        auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
        for (int n = 0; n < noteList.getNumChildren(); ++n)
        {
            auto note = noteList.getChild(n);
            note.setProperty(IDs::noteID, model.allocateNoteID(), nullptr);
        }
    }

    int newIdx = trackList.getNumChildren();
    trackList.addChild(copy, newIdx, &um);
    return newIdx;
}
