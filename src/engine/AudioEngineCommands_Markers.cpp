#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"

// ─── ProjectCommands — Markers ────────────────────────────────────

int AudioEngineCommands::addMarker(const std::string& name, double time, int color)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto projectTree = engine_.getProjectModel().getTree();
    auto markerList = projectTree.getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid())
    {
        markerList = juce::ValueTree(IDs::MARKER_LIST);
        projectTree.addChild(markerList, -1, &um);
    }
    juce::ValueTree marker(IDs::MARKER);
    marker.setProperty(IDs::markerName, juce::String(name), &um);
    marker.setProperty(IDs::markerTime, time, &um);
    marker.setProperty(IDs::markerColor, color, &um);
    int idx = markerList.getNumChildren();
    markerList.addChild(marker, -1, &um);
    return idx;
}

void AudioEngineCommands::removeMarker(int index)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto projectTree = engine_.getProjectModel().getTree();
    auto markerList = projectTree.getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid()) return;
    if (index >= 0 && index < markerList.getNumChildren())
        markerList.removeChild(index, &um);
}

void AudioEngineCommands::setMarkerName(int index, const std::string& name)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto projectTree = engine_.getProjectModel().getTree();
    auto markerList = projectTree.getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid()) return;
    if (index >= 0 && index < markerList.getNumChildren())
        markerList.getChild(index).setProperty(IDs::markerName, juce::String(name), &um);
}

void AudioEngineCommands::setMarkerTime(int index, double time)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto projectTree = engine_.getProjectModel().getTree();
    auto markerList = projectTree.getChildWithName(IDs::MARKER_LIST);
    if (!markerList.isValid()) return;
    if (index >= 0 && index < markerList.getNumChildren())
        markerList.getChild(index).setProperty(IDs::markerTime, time, &um);
}
