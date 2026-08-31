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
    DECLARE_ID(muted)

    // MIDI note properties
    DECLARE_ID(noteNumber)
    DECLARE_ID(velocity)
    DECLARE_ID(startBeat)
    DECLARE_ID(durationBeats)
    DECLARE_ID(chance)
    DECLARE_ID(repeatCount)
    DECLARE_ID(repeatRate)
    DECLARE_ID(repeatCurve)
    DECLARE_ID(occurrence)
    DECLARE_ID(recurrence)

    // Per-note expressions
    DECLARE_ID(noteGain)
    DECLARE_ID(notePan)
    DECLARE_ID(notePitch)
    DECLARE_ID(noteTimbre)
    DECLARE_ID(notePressure)

    // CC lane
    DECLARE_ID(CC_LIST)
    DECLARE_ID(CC_POINT)
    DECLARE_ID(controllerNumber)
    DECLARE_ID(beat)
    DECLARE_ID(value)
    DECLARE_ID(ccID)

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

    // Arranger Regions (named timeline sections)
    DECLARE_ID(ARRANGER_LIST)
    DECLARE_ID(ARRANGER_REGION)
    DECLARE_ID(regionID)
    DECLARE_ID(regionName)

    // Arranger Chains (playback order)
    DECLARE_ID(ARRANGER_CHAIN_LIST)
    DECLARE_ID(ARRANGER_CHAIN)
    DECLARE_ID(chainID)
    DECLARE_ID(chainName)
    DECLARE_ID(isActive)
    DECLARE_ID(CHAIN_ENTRY)

    // Arranger transport state
    DECLARE_ID(arrangerEnabled)
    DECLARE_ID(arrangerChainPosition)
    DECLARE_ID(arrangerRepeatIndex)

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
    DECLARE_ID(keyRangeLow)
    DECLARE_ID(keyRangeHigh)

    // Per-track MIDI FX chain
    DECLARE_ID(MIDI_FX_CHAIN)
    DECLARE_ID(MIDI_FX_SLOT)
    DECLARE_ID(arpRate)
    DECLARE_ID(arpPattern)
    DECLARE_ID(arpOctaves)
    DECLARE_ID(arpGate)
    DECLARE_ID(velFactor)
    DECLARE_ID(chordType)
    DECLARE_ID(scaleType)
    DECLARE_ID(lengthFactor)

    // Transpose
    DECLARE_ID(semitones)
    // Key Filter
    DECLARE_ID(keyFilterRoot)
    DECLARE_ID(keyFilterScale)
    // Multi-Note
    DECLARE_ID(multiNoteIntervals)
    // Velocity Curve
    // (curveType already declared under Automation — reused here)
    DECLARE_ID(curveAmount)
    // Note Chance
    DECLARE_ID(noteChance)
    // MIDI Delay
    DECLARE_ID(delayBeats)
    DECLARE_ID(delayFeedback)
    DECLARE_ID(delayMix)
    // Humanize
    DECLARE_ID(humanizeTiming)
    DECLARE_ID(humanizeVelocity)
    DECLARE_ID(humanizePitch)
    // Strum
    DECLARE_ID(strumTime)
    DECLARE_ID(strumDirection)

    // Automation
    DECLARE_ID(AUTOMATION_LIST)
    DECLARE_ID(AUTOMATION)
    DECLARE_ID(POINT_LIST)
    DECLARE_ID(POINT)
    DECLARE_ID(curveType)
    DECLARE_ID(automationEnabled)
    DECLARE_ID(automationMode)

    // Gain Envelope (per-clip volume automation)
    DECLARE_ID(GAIN_ENVELOPE)
    DECLARE_ID(GAIN_ENVELOPE_POINT)
    DECLARE_ID(pointTime)
    DECLARE_ID(pointGain)
    DECLARE_ID(paramID)

    // Track UI state
    DECLARE_ID(trackHeight)
    DECLARE_ID(trackType)
    DECLARE_ID(childIds)
    DECLARE_ID(parentId)
    DECLARE_ID(isCollapsed)
    DECLARE_ID(isHidden)
    DECLARE_ID(sceneIndex)

    // Session state
    DECLARE_ID(SESSION_STATE)
    DECLARE_ID(launchedScene)
    DECLARE_ID(sceneCount)

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

    // Master bus (root property; restored on routing-graph rebuild)
    DECLARE_ID(masterGain)

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

    // Ghost clips
    DECLARE_ID(ghostSourceId) // clipID of the source clip; -1 = not a ghost
    DECLARE_ID(isGhost)       // 0/1 bool: is this a ghost copy?
    DECLARE_ID(seed)           // uint64 seed for deterministic operators

    // Project file metadata (root ValueTree properties; serialized via toXmlString)
    DECLARE_ID(createdWithApp) // app version that first created this project (provenance; never overwritten)
    DECLARE_ID(savedWithApp)   // app version that last saved this file
    DECLARE_ID(formatVersion)  // schema/format version int (migration hook)
    DECLARE_ID(createdAt)      // ISO-8601 timestamp of first creation
    DECLARE_ID(lastSavedAt)    // ISO-8601 timestamp of last save
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

    // Master-bus gain (linear, >= 0). Root property; defaults to 1.0.
    float getMasterGain() const;

    juce::UndoManager& getUndoManager() { return undoManager; }
    bool isDirty() const { return dirty; }
    void markAsSaved() { dirty = false; }

    // Per-instance id counters: a second ProjectModel (e.g. ExportManager's
    // render-local model) must never reset the live project's counters, or
    // freshly minted ids collide with existing clips/notes in the tree.
    int allocateClipID();
    void resetClipIDCounter();
    int allocateNoteID();
    void resetNoteIDCounter();
    int allocateCcID();
    juce::ValueTree createAudioClip(juce::String name, double start, double dur, juce::String file);
    juce::ValueTree createMidiClipEmpty(juce::String name, double start, double dur);
    juce::ValueTree createMidiNote(int note, float vel, double start, double dur);
    static juce::ValueTree getTrackOfClip(const juce::ValueTree& clip);
    // Returns a color from a curated rotating palette so each track (and thus
    // its clips) gets a distinct, stable color without clashing.
    static juce::uint32 trackColorForIndex(int index);
    static juce::ValueTree createTrackAutomationList();
    void scanAndSyncClipIDs();
    void scanAndSyncNoteIDs();

    void createDefaultProject();

    struct GainEnvelopePoint { double time; double gain; };

    static juce::ValueTree ensureGainEnvelope(juce::ValueTree clip, juce::UndoManager* um = nullptr);
    static juce::ValueTree addGainEnvelopePoint(juce::ValueTree envelope, double time, double gain, juce::UndoManager* um);
    static std::vector<GainEnvelopePoint> getGainEnvelopePoints(const juce::ValueTree& envelope);
    static void removeGainEnvelopePoint(juce::ValueTree envelope, int index, juce::UndoManager* um);
    static void clearGainEnvelope(juce::ValueTree envelope, juce::UndoManager* um);

    // Slicing (instance method: mints fresh clip ids from this model's counter)
    std::vector<juce::ValueTree> sliceClipAtTimes(juce::ValueTree clip, const std::vector<double>& times, juce::UndoManager* um);

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

    // Per-instance id counters (see the public allocator notes above).
    std::atomic<int> nextClipID_{1};
    std::atomic<int> nextNoteID_{1};
    std::atomic<int> nextCcID_{1};

    juce::ValueTree projectTree;
    juce::UndoManager undoManager;
    bool dirty = false;
    HDAW::PluginManager* pluginManager_ = nullptr;
};
