#include "SessionManager.h"

namespace HDAW {

SessionManager::SessionManager(TransportManager& transport, ProjectModel& model)
    : transport(transport), model(model) {}

void SessionManager::launchScene(int sceneIndex)
{
    launchedScene.store(sceneIndex);
}

void SessionManager::stopScene(int /*sceneIndex*/)
{
    if (launchedScene.load() >= 0)
        launchedScene.store(-1);
}

void SessionManager::stopAll()
{
    launchedScene.store(-1);
}

std::vector<SessionManager::SessionClipState> SessionManager::getClipStates() const
{
    std::vector<SessionClipState> states;
    auto trackList = model.getTrackListTree();
    int activeScene = launchedScene.load();

    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c) {
            auto clip = clipList.getChild(c);
            int si = static_cast<int>(clip.getProperty(IDs::sceneIndex, -1));
            if (si < 0) continue;  // not a session clip
            SessionClipState state;
            state.clipId = static_cast<int>(clip.getProperty(IDs::clipID, -1));
            state.sceneIndex = si;
            state.isPlaying = (si == activeScene);
            state.isLaunched = false;
            states.push_back(state);
        }
    }
    return states;
}

} // namespace HDAW
