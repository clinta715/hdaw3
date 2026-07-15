#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <string>
#include <atomic>

namespace HDAW { class PluginManager; }

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
    DECLARE_ID(inputMonitor)
    DECLARE_ID(midiChannel)

    // Clip properties
    DECLARE_ID(clipID)
    DECLARE_ID(noteID)
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

    // Markers (named navigation points)
    DECLARE_ID(MARKER_LIST)
    DECLARE_ID(MARKER)
    DECLARE_ID(markerTime)
    DECLARE_ID(markerName)
    DECLARE_ID(markerColor)

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

    // Gain Envelope (per-clip volume automation)
    DECLARE_ID(GAIN_ENVELOPE)
    DECLARE_ID(GAIN_ENVELOPE_POINT)
    DECLARE_ID(pointTime)
    DECLARE_ID(pointGain)
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

    // Time signature
    DECLARE_ID(timeSigNumerator)
    DECLARE_ID(timeSigDenominator)

    // Take management
    DECLARE_ID(TAKE_LIST)
    DECLARE_ID(TAKE)
    DECLARE_ID(activeTake)

    // Project scale
    DECLARE_ID(scaleRoot)
    DECLARE_ID(scaleMode)
    DECLARE_ID(SCALE_INFO)

    // Modulation
    DECLARE_ID(MODULATION_LIST)
    DECLARE_ID(MODULATION)
    DECLARE_ID(waveform)
    DECLARE_ID(rate)
    DECLARE_ID(rateSync)
    DECLARE_ID(depth)
    DECLARE_ID(bipolar)
    DECLARE_ID(phaseOffset)
    DECLARE_ID(targetParamID)
    DECLARE_ID(targetClipIndex)
    DECLARE_ID(enabled)

    // Audio clip timestretch
    DECLARE_ID(sourceBpm)      // musical tempo of the source file; 0 = unknown
    DECLARE_ID(stretchMode)    // 0=Off, 1=TempoMatch, 2=ManualRatio
    DECLARE_ID(stretchRatio)   // time ratio vs original source (targetDuration/sourceDuration)
    DECLARE_ID(sourceDuration) // original source length in seconds (cached at import)
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
    static int allocateNoteID();
    static void resetNoteIDCounter();
    static juce::ValueTree createAudioClip(juce::String name, double start, double dur, juce::String file);
    static juce::ValueTree createMidiClipEmpty(juce::String name, double start, double dur);
    static juce::ValueTree createMidiNote(int note, float vel, double start, double dur);
    static juce::ValueTree getTrackOfClip(const juce::ValueTree& clip);
    // Returns a color from a curated rotating palette so each track (and thus
    // its clips) gets a distinct, stable color without clashing.
    static juce::uint32 trackColorForIndex(int index);
    static juce::ValueTree createTrackAutomationList();
    void scanAndSyncClipIDs();
    void scanAndSyncNoteIDs();

    void createDefaultProject();

    struct GainEnvelopePoint { double time; double gain; };

    static juce::ValueTree ensureGainEnvelope(juce::ValueTree clip);
    static juce::ValueTree addGainEnvelopePoint(juce::ValueTree envelope, double time, double gain, juce::UndoManager* um);
    static std::vector<GainEnvelopePoint> getGainEnvelopePoints(const juce::ValueTree& envelope);
    static void removeGainEnvelopePoint(juce::ValueTree envelope, int index, juce::UndoManager* um);
    static void clearGainEnvelope(juce::ValueTree envelope, juce::UndoManager* um);

    // Slicing
    static std::vector<juce::ValueTree> sliceClipAtTimes(juce::ValueTree clip, const std::vector<double>& times, juce::UndoManager* um);

    // Wire the engine's PluginManager so addFxSlot can resolve plugin formats.
    // Pass nullptr to clear. The pointer is not owned.
    void setPluginManager(HDAW::PluginManager* pm) { pluginManager_ = pm; }

    // Add a new FX slot to a track. `type` is the FX type
    // ("eq"/"compressor"/"reverb"/"delay") or "plugin". `pluginID` is required
    // when type == "plugin" and is used to look up the plugin's format via the
    // project's PluginManager. `pos` < 0 means append. Returns the new slot
    // index, or -1 on error.
    int addFxSlot(int trackIdx, const std::string& type, int pos = -1,
                  const std::string& pluginID = {});

    // Look up the format for a plugin ID via the project's PluginManager.
    // Returns the matching pluginFormatName, or an empty string if the manager
    // is unset or the plugin is not in the cache.
    std::string resolvePluginFormat(const std::string& pluginID) const;

private:
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { dirty = true; }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { dirty = true; }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { dirty = true; }
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override { dirty = true; }
    void valueTreeParentChanged(juce::ValueTree&) override {}

    static inline std::atomic<int> nextNoteID{1};

    juce::ValueTree projectTree;
    juce::UndoManager undoManager;
    bool dirty = false;
    HDAW::PluginManager* pluginManager_ = nullptr;
};
