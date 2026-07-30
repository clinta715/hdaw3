#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"

void AudioEngineCommands::setClipScene(int clipId, int sceneIndex)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();

    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c) {
            auto clip = clipList.getChild(c);
            if (static_cast<int>(clip.getProperty(IDs::clipID, -1)) == clipId) {
                clip.setProperty(IDs::sceneIndex, sceneIndex, &um);
                return;
            }
        }
    }
}

int AudioEngineCommands::createSessionClip(int trackIndex, int sceneIndex, bool isMidi)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return -1;

    auto track = trackList.getChild(trackIndex);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid()) {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        track.addChild(clipList, -1, nullptr);
    }

    juce::ValueTree clip;
    if (isMidi) {
        clip = ProjectModel::createMidiClipEmpty("Session Clip", 0.0, 4.0);
    } else {
        clip = ProjectModel::createAudioClip("Session Clip", 0.0, 4.0, "");
    }
    int newId = model.allocateClipID();
    clip.setProperty(IDs::clipID, newId, nullptr);
    clip.setProperty(IDs::sceneIndex, sceneIndex, nullptr);
    clip.setProperty(IDs::looping, true, nullptr);  // session clips loop by default

    clipList.addChild(clip, -1, &um);
    return newId;
}
