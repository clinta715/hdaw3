#pragma once
#include "../model/ProjectModel.h"
#include "TransportManager.h"
#include <vector>
#include <atomic>

namespace HDAW {

class SessionManager {
public:
    SessionManager(TransportManager& transport, ProjectModel& model);

    void launchScene(int sceneIndex);
    void stopScene(int sceneIndex);
    void stopAll();

    struct SessionClipState {
        int clipId = -1;
        int sceneIndex = -1;
        bool isPlaying = false;
        bool isLaunched = false;
    };
    std::vector<SessionClipState> getClipStates() const;

    int getLaunchedScene() const { return launchedScene.load(); }
    void setLaunchedScene(int scene) { launchedScene.store(scene); }

private:
    TransportManager& transport;
    ProjectModel& model;
    std::atomic<int> launchedScene{ -1 };
};

} // namespace HDAW
