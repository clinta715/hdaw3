#pragma once
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include <juce_data_structures/juce_data_structures.h>

class AudioEngine;

class AudioEngineCommands : public ProjectCommands,
                            public TransportCommands,
                            public AudioGraphCommands
{
public:
    explicit AudioEngineCommands(AudioEngine& engine);
    ~AudioEngineCommands() override;

    // ProjectCommands — Track operations
    int addTrack(const std::string& name, int color, int parentBus) override;
    void removeTrack(int trackIndex) override;
    void moveTrack(int trackIndex, int newIndex) override;
    void setTrackName(int trackIndex, const std::string& name) override;
    void setTrackColor(int trackIndex, int color) override;
    void setTrackVolume(int trackIndex, float volume) override;
    void setTrackPan(int trackIndex, float pan) override;
    void setTrackMuted(int trackIndex, bool muted) override;
    void setTrackSoloed(int trackIndex, bool soloed) override;
    void setTrackArmed(int trackIndex, bool armed) override;
    void setTrackInputMonitor(int trackIndex, bool monitor) override;
    void setTrackHeight(int trackIndex, int height) override;
    void setTrackMidiChannel(int trackIndex, int channel) override;

    // ProjectCommands — Clip operations
    int addAudioClip(int trackIndex, double start, double duration,
                     const std::string& sourceFile, const std::string& name) override;
    int addMidiClip(int trackIndex, double start, double duration,
                    const std::string& name) override;
    void removeClip(int clipId) override;
    void moveClip(int clipId, int newTrackIndex, double newStart) override;
    void setClipStart(int clipId, double start) override;
    void setClipDuration(int clipId, double duration) override;
    void setClipGain(int clipId, float gain) override;
    void setClipFadeIn(int clipId, double fadeIn) override;
    void setClipFadeOut(int clipId, double fadeOut) override;
    void setClipOffset(int clipId, double offset) override;
    void setClipLooping(int clipId, bool looping) override;
    int duplicateClip(int clipId) override;

    // ProjectCommands — audio clip timestretch
    void setClipSourceBpm(int clipId, double bpm) override;
    void setClipStretchMode(int clipId, int mode) override;
    void setClipStretchRatio(int clipId, double ratio) override;
    void tempoMatchClip(int clipId) override;
    void fitClipToLoop(int clipId) override;

    // ProjectCommands — Gain Envelope
    void addGainEnvelopePoint(int clipId, double time, double gain) override;
    void moveGainEnvelopePoint(int clipId, int pointIndex, double time, double gain) override;
    void removeGainEnvelopePoint(int clipId, int pointIndex) override;
    void clearGainEnvelope(int clipId) override;

    // ProjectCommands — MIDI note operations
    int addNote(int clipId, int pitch, int velocity,
                double startBeat, double durationBeats) override;
    void removeNote(int noteId) override;
    void setNotePitch(int noteId, int pitch) override;
    void setNoteVelocity(int noteId, int velocity) override;
    void setNoteStart(int noteId, double startBeat) override;
    void setNoteDuration(int noteId, double durationBeats) override;
    void clearNotes(int clipId) override;

    // ProjectCommands — FX operations
    void addFxSlot(int trackIndex, int type, int position,
                   const std::string& pluginId) override;
    void removeFxSlot(int trackIndex, int slotIndex) override;
    void setFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed) override;
    void setFxSlotParam(int trackIndex, int slotIndex, int paramIndex,
                        float value) override;
    void reorderFxSlots(int trackIndex, int fromSlot, int toSlot) override;

    // ProjectCommands — Automation
    void addAutomationLane(int trackIndex, const std::string& laneName) override;
    void removeAutomationLane(int trackIndex, const std::string& laneName) override;
    void addAutomationPoint(int trackIndex, const std::string& lane,
                            double time, float value) override;
    void removeAutomationPoint(int trackIndex, const std::string& lane,
                               double time) override;
    void setAutomationEnabled(int trackIndex, const std::string& lane,
                              bool enabled) override;

    // ProjectCommands — Transport properties
    void setTempo(double bpm) override;
    void setLoopStart(double beat) override;
    void setLoopEnd(double beat) override;
    void setLooping(bool looping) override;
    void setMetronomeEnabled(bool enabled) override;
    void setTimeSignature(int numerator, int denominator) override;

    // ProjectCommands — Track operations advanced
    int duplicateTrack(int trackIndex) override;

    // ProjectCommands — FX advanced
    void setFxSlotPlugin(int trackIndex, int slotIndex, const std::string& fxType,
        const std::string& pluginID, const std::string& pluginFormat,
        const std::string& pluginPath) override;

        // ProjectCommands — Automation point mutation
    void setAutomationPointValue(int trackIndex, const std::string& lane,
        double time, float value) override;

    // ProjectCommands — MIDI CC
    void addCcPoint(int clipId, int controllerNumber, double beat, int value) override;

    // ProjectCommands — Undo/redo
    void undo() override;
    void redo() override;
    bool canUndo() const override;
    bool canRedo() const override;

    // ProjectCommands — Transaction lifecycle
    void beginTransaction(const std::string& name) override;
    void endTransaction() override;

    // ProjectCommands — Markers
    int addMarker(const std::string& name, double time, int color) override;
    void removeMarker(int index) override;
    void setMarkerName(int index, const std::string& name) override;
    void setMarkerTime(int index, double time) override;

    // ProjectCommands — Project persistence
    void newProject() override;
    bool saveProject(const std::string& filePath) override;
    bool loadProject(const std::string& filePath) override;

    // ProjectCommands — Scale
    void setScaleRoot(int root) override;
    void setScaleMode(int mode) override;

    // TransportCommands
    void play() override;
    void stop() override;
    void pause() override;
    void rewind() override;
    void toggleLoop() override;
    void seekToSample(int64_t sample) override;
    void seekToSeconds(double seconds) override;
    void startRecording() override;
    void stopRecording() override;
    bool isRecording() const override;

    // AudioGraphCommands
    void rebuildRoutingGraph() override;
    void rebuildTrackFX(int trackIndex) override;
    void rebuildAutomationCache(int trackIndex) override;
    void rebuildModulation(int trackIndex) override;
    void toggleFXEditor(int trackIndex, int slotIndex) override;
    void switchClipTake(int clipId) override;

    // Modulation (LFO) — concrete-class mutation methods. These mutate the
    // track's MODULATION_LIST ValueTree via the UndoManager but take a
    // juce::ValueTree argument, so they live on the concrete class rather
    // than the JUCE-free abstract ProjectCommands interface.
    int addModulation(int trackIndex, const juce::ValueTree& modulationTree);
    void removeModulation(int trackIndex, int lfoIndex);
    void setModulationProperty(int trackIndex, int lfoIndex,
                               const std::string& propertyID, float value);

private:
    // Find clip by ID across all tracks. Sets outTrackIndex to the
    // parent track index. Returns a valid ValueTree on success.
    juce::ValueTree findClipById(int clipId, int& outTrackIndex) const;

    // Find note by ID in a clip's MIDI_NOTE_LIST. Sets outClipId.
    // Returns a valid ValueTree on success.
    juce::ValueTree findNoteById(int noteId, int& outClipId) const;

    // Find the FX_SLOT child at slotIndex in a track's FX_CHAIN.
    juce::ValueTree findFxSlot(int trackIndex, int slotIndex) const;

    // Find the AUTOMATION child by lane name in a track.
    juce::ValueTree findAutomationLane(int trackIndex, const std::string& lane) const;

    // Add a new track ValueTree to the project. Returns the new index.
    juce::ValueTree createTrackValueTree(const std::string& name, int color, int parentBus);

    // Gain envelope helpers
    std::vector<ProjectModel::GainEnvelopePoint> getGainEnvelopePoints(int clipId);
    void notifyClipGainEnvelopeChanged(int clipId);

    AudioEngine& engine_;
};
