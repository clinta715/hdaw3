#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"

namespace {

// The audio engine tracks the active scene in SessionManager's atomic (reads on the
// audio thread), while the frontend learns it from the SESSION_STATE ValueTree property
// via the read.snapshot field `launchedScene` (ReadModelImpl.cpp). Both must stay in
// sync, so every session launch/stop that touches the atomic also writes the property
// (with a null undo manager — scene-launch is transient play state, not editable content).
void setSessionLaunchedScene(AudioEngine& engine, int sceneIndex)
{
    auto sessionState = engine.getProjectModel().getTree().getChildWithName(IDs::SESSION_STATE);
    if (sessionState.isValid())
        sessionState.setProperty(IDs::launchedScene, sceneIndex, nullptr);
}

} // namespace

void AudioEngineCommands::setClipScene(int clipId, int sceneIndex)
{
    auto& model = engine_.getProjectModel();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;
    clip.setProperty(IDs::sceneIndex, sceneIndex, &model.getUndoManager());
}

int AudioEngineCommands::createSessionClip(int trackIndex, int sceneIndex, bool isMidi)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;
    if (sceneIndex < 0) return -1;

    auto track = trackList.getChild(trackIndex);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        track.addChild(clipList, -1, nullptr);
    }

    juce::ValueTree clip;
    if (isMidi) {
        clip = model.createMidiClipEmpty("Session Clip", 0.0, 4.0);
    } else {
        clip = model.createAudioClip("Session Clip", 0.0, 4.0, "");
    }
    int newId = model.allocateClipID();
    clip.setProperty(IDs::clipID, newId, nullptr);
    clip.setProperty(IDs::sceneIndex, sceneIndex, nullptr);
    clip.setProperty(IDs::looping, true, nullptr);

    clipList.addChild(clip, -1, &um);
    return newId;
}

void AudioEngineCommands::launchScene(int sceneIndex)
{
    engine_.getSessionManager().launchScene(sceneIndex);
    setSessionLaunchedScene(engine_, sceneIndex);
}

void AudioEngineCommands::stopAllSessionClips()
{
    engine_.getSessionManager().stopAll();
    setSessionLaunchedScene(engine_, -1);
}
