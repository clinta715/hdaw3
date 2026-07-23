// AudioEngineCommands_Helpers.h
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <utility>

namespace HDAW {

// Returns {undoManager, track} or {um, {}} if out of range.
inline std::pair<juce::UndoManager*, juce::ValueTree> getTrack(
    juce::ValueTree trackList, int trackIndex, juce::UndoManager& um)
{
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return { &um, {} };
    return { &um, trackList.getChild(trackIndex) };
}

// Get or create a child list under parent.
inline juce::ValueTree getOrCreateChild(
    juce::ValueTree parent, const juce::Identifier& childId, juce::UndoManager& um)
{
    auto child = parent.getChildWithName(childId);
    if (!child.isValid())
    {
        child = juce::ValueTree(childId);
        parent.addChild(child, -1, &um);
    }
    return child;
}

// Set a property on a ValueTree with undo.
template<typename T>
inline void setProp(juce::ValueTree tree, const juce::Identifier& prop, T value, juce::UndoManager* um)
{
    if (tree.isValid())
        tree.setProperty(prop, value, um);
}

} // namespace HDAW
