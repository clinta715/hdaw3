#pragma once

class AudioGraphCommands
{
public:
    virtual ~AudioGraphCommands() = default;

    virtual void rebuildRoutingGraph() = 0;
    virtual void rebuildTrackFX(int trackIndex) = 0;
    virtual void rebuildAutomationCache(int trackIndex) = 0;
    virtual void rebuildModulation(int trackIndex) = 0;
    virtual void toggleFXEditor(int trackIndex, int slotIndex) = 0;
};
