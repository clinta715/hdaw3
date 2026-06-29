#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace IDs {
    #define DECLARE_ID(name) const juce::Identifier name { #name };
    DECLARE_ID(PROJECT)
    DECLARE_ID(TRANSPORT)
    DECLARE_ID(TRACK_LIST)
    DECLARE_ID(TRACK)
    DECLARE_ID(CLIP_LIST)
    DECLARE_ID(CLIP)
    DECLARE_ID(MIDI_NOTE_LIST)
    DECLARE_ID(MIDI_NOTE)

    // Properties
    DECLARE_ID(name)
    DECLARE_ID(tempo)
    DECLARE_ID(position)
    DECLARE_ID(isPlaying)
    DECLARE_ID(volume)
    DECLARE_ID(pan)
    DECLARE_ID(isMuted)
    DECLARE_ID(isSoloed)
    DECLARE_ID(isArm)

    // Clip properties
    DECLARE_ID(clipID)
    DECLARE_ID(color)
    DECLARE_ID(startTime)
    DECLARE_ID(duration)
    DECLARE_ID(offset)
    DECLARE_ID(clipType)
    DECLARE_ID(sourceFile)
    DECLARE_ID(gain)
    DECLARE_ID(fadeIn)
    DECLARE_ID(fadeOut)
    DECLARE_ID(looping)

    // MIDI note properties
    DECLARE_ID(noteNumber)
    DECLARE_ID(velocity)
    DECLARE_ID(startBeat)
    DECLARE_ID(durationBeats)

    // CC lane
    DECLARE_ID(CC_LIST)
    DECLARE_ID(CC_POINT)
    DECLARE_ID(controllerNumber)
    DECLARE_ID(beat)
    DECLARE_ID(value)

    // Transport / loop
    DECLARE_ID(loopStart)
    DECLARE_ID(loopEnd)
    DECLARE_ID(isLooping)
    DECLARE_ID(metronomeEnabled)

    // Routing
    DECLARE_ID(ROUTING_GRAPH)
    DECLARE_ID(BUS_LIST)
    DECLARE_ID(BUS)
    DECLARE_ID(SEND_LIST)
    DECLARE_ID(SEND)

    DECLARE_ID(busType)
    DECLARE_ID(busTarget)
    DECLARE_ID(fxType)
    DECLARE_ID(sendLevel)
    DECLARE_ID(sendMode)
    DECLARE_ID(sendTarget)
    DECLARE_ID(parentBus)
    DECLARE_ID(busID)

    // Per-track FX chain
    DECLARE_ID(FX_CHAIN)
    DECLARE_ID(FX_SLOT)
    DECLARE_ID(slotIndex)
    DECLARE_ID(bypassed)

    // Automation
    DECLARE_ID(AUTOMATION_LIST)
    DECLARE_ID(AUTOMATION)
    DECLARE_ID(POINT_LIST)
    DECLARE_ID(POINT)
    DECLARE_ID(curveType)
    DECLARE_ID(automationEnabled)
    DECLARE_ID(paramID)

    // Track UI state
    DECLARE_ID(trackHeight)

    // Plugin hosting
    DECLARE_ID(pluginID)
    DECLARE_ID(pluginFormat)
    DECLARE_ID(pluginState)
    DECLARE_ID(pluginPath)

    // Tempo track
    DECLARE_ID(TEMPO_POINT_LIST)
    DECLARE_ID(TEMPO_POINT)

    // Project scale
    DECLARE_ID(scaleRoot)
    DECLARE_ID(scaleMode)
    DECLARE_ID(SCALE_INFO)
    #undef DECLARE_ID
}

class ProjectModel : private juce::ValueTree::Listener
{
public:
    ProjectModel();
    ~ProjectModel();

    juce::ValueTree& getTree() { return projectTree; }
    juce::ValueTree getTransportTree() { return projectTree.getChildWithName(IDs::TRANSPORT); }
    juce::ValueTree getTransportTree() const { return projectTree.getChildWithName(IDs::TRANSPORT); }
    juce::ValueTree getTrackListTree() { return projectTree.getChildWithName(IDs::TRACK_LIST); }
    juce::ValueTree getTrackListTree() const { return projectTree.getChildWithName(IDs::TRACK_LIST); }
    juce::ValueTree getBusListTree() { return projectTree.getChildWithName(IDs::ROUTING_GRAPH).getChildWithName(IDs::BUS_LIST); }

    juce::ValueTree getScaleInfoTree();
    juce::ValueTree getScaleInfoTree() const;
    int getScaleRoot() const;
    int getScaleMode() const;
    void setScaleRoot(int root);
    void setScaleMode(int mode);

    juce::UndoManager& getUndoManager() { return undoManager; }
    bool isDirty() const { return dirty; }
    void markAsSaved() { dirty = false; }

    static int allocateClipID();
    static void resetClipIDCounter();
    // Returns a color from a curated rotating palette so each track (and thus
    // its clips) gets a distinct, stable color without clashing.
    static juce::uint32 trackColorForIndex(int index);
    void scanAndSyncClipIDs();

    void createDefaultProject();

private:
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { dirty = true; }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { dirty = true; }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { dirty = true; }
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override { dirty = true; }
    void valueTreeParentChanged(juce::ValueTree&) override {}

    juce::ValueTree projectTree;
    juce::UndoManager undoManager;
    bool dirty = false;
};
