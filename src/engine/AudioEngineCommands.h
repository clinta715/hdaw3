#pragma once
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../model/ProjectModel.h"
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
    int addTrack(const std::string& name, int color, int parentBus, int trackType) override;
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
    void setTrackType(int trackIndex, int type) override;
    void setTrackCollapsed(int trackIndex, bool collapsed) override;
    void setTrackHidden(int trackIndex, bool hidden) override;
    void moveTrackIntoFolder(int trackIndex, int folderIndex) override;
    void moveTrackOutOfFolder(int trackIndex) override;

    // Send operations
    void setTrackSendLevel(int trackIndex, int sendIndex, float level) override;
    void setTrackSendMode(int trackIndex, int sendIndex, bool isPreFader) override;
    void setTrackSendBypassed(int trackIndex, int sendIndex, bool bypassed) override;

    // Session
    void setClipScene(int clipId, int sceneIndex) override;
    int createSessionClip(int trackIndex, int sceneIndex, bool isMidi) override;
    void launchScene(int sceneIndex) override;
    void stopAllSessionClips() override;

    // ProjectCommands — Clip operations
    int addAudioClip(int trackIndex, double start, double duration,
                     const std::string& sourceFile, const std::string& name) override;
    int addMidiClip(int trackIndex, double start, double duration,
                    const std::string& name) override;
    void removeClip(int clipId) override;
    void moveClip(int clipId, int newTrackIndex, double newStart) override;
    void moveClipWithOverlap(int clipId, int newTrackIndex, double newStart) override;
    void rippleDelete(double startBeat, double endBeat) override;
    void insertSilence(double startBeat, double endBeat) override;
    void duplicateRegion(double startBeat, double endBeat) override;
    void setClipStart(int clipId, double start) override;
    void setClipDuration(int clipId, double duration) override;
    void setClipGain(int clipId, float gain) override;
    void setClipFadeIn(int clipId, double fadeIn) override;
    void setClipFadeOut(int clipId, double fadeOut) override;
    void setClipOffset(int clipId, double offset) override;
    void setClipLooping(int clipId, bool looping) override;
    void setClipMuted(int clipId, bool muted) override;
    int duplicateClip(int clipId) override;
    int duplicateClipTo(int clipId, double newStart, int newTrackIndex) override;
    int createGhostClip(int sourceClipId, double newStart, int newTrackIndex) override;
    std::vector<int> paintClips(const std::vector<int>& sourceClipIds, double originBeat, double spacing, int targetTrackIndex, int count) override;
    std::vector<int> duplicateClips(const std::vector<int>& clipIds, const std::vector<double>& newStarts, const std::vector<int>& newTrackIndices) override;
    void moveClips(const std::vector<int>& clipIds, const std::vector<double>& newStarts, const std::vector<int>& newTrackIndices) override;
    void removeClips(const std::vector<int>& clipIds) override;
    std::vector<int> addClips(int trackIndex, const std::vector<double>& starts, const std::vector<double>& durations, const std::vector<std::string>& names, const std::vector<std::string>& sourceFiles = {}) override;

    // ProjectCommands — generative arrangement
    ArrangementResult generateArrangement(const HDAW::ArrangementParams& params) override;

    // ProjectCommands — audio clip timestretch
    void setClipSourceBpm(int clipId, double bpm) override;
    void setClipStretchMode(int clipId, int mode) override;
    void setClipStretchRatio(int clipId, double ratio) override;
    void tempoMatchClip(int clipId) override;
    void fitClipToLoop(int clipId) override;

    // ProjectCommands — Slicing
    void sliceClipAtTimes(int clipId, const std::vector<double>& times) override;
    void sliceClipAtTransients(int clipId) override;
    void sliceClipAtPlayhead(int clipId) override;
    void sliceClipsAtPlayhead(const std::vector<int>& clipIds) override;
    void sliceClipsAtTransients(const std::vector<int>& clipIds) override;

    // ProjectCommands — Region cut/copy/paste
    int copyAudioClipRegion(int clipId, double regionStart, double regionEnd) override;
    int cutAudioClipRegion(int clipId, double regionStart, double regionEnd) override;
    int pasteAudioClipRegion(int clipId, double pasteTime) override;

    // ProjectCommands — Gain Envelope
    void addGainEnvelopePoint(int clipId, double time, double gain) override;
    void moveGainEnvelopePoint(int clipId, int pointIndex, double time, double gain) override;
    void removeGainEnvelopePoint(int clipId, int pointIndex) override;
    void clearGainEnvelope(int clipId) override;
    void setClipGainEnvelope(int clipId,
                             const std::vector<std::pair<double, double>>& points) override;
    void notifyClipGainEnvelopeChanged(int clipId) override;

    // ProjectCommands — Envelope generation
    void generateAutomationEnvelope(int trackIndex, const std::string& lane,
                                     const HDAW::EnvelopeGenerator::Params& params) override;
    void generateClipGainEnvelope(int clipId,
                                   const HDAW::EnvelopeGenerator::Params& params) override;
    void generateClipCcLane(int clipId, int controllerNumber,
                             const HDAW::EnvelopeGenerator::Params& params) override;

    // ProjectCommands — Modulation (LFO)
    void addLfo(int trackIndex) override;
    void removeLfo(int trackIndex, int lfoIndex) override;
    void setLfoParam(int trackIndex, int lfoIndex,
                     const std::string& paramName, double value) override;

    // ProjectCommands — MIDI note operations
    int addNote(int clipId, int pitch, int velocity,
                double startBeat, double durationBeats) override;
    void removeNote(int noteId) override;
    void setNotePitch(int noteId, int pitch) override;
    void setNoteVelocity(int noteId, int velocity) override;
    void setNoteStart(int noteId, double startBeat) override;
    void setNoteDuration(int noteId, double durationBeats) override;
    void setNoteChance(int noteId, float chance) override;
    void setNoteRepeatCount(int noteId, int repeatCount) override;
    void setNoteRepeatRate(int noteId, float repeatRate) override;
    void setNoteRepeatCurve(int noteId, float repeatCurve) override;
    void setNoteOccurrence(int noteId, int occurrence) override;
    void setNoteRecurrence(int noteId, int recurrence) override;
    void setNoteGain(int noteId, float gain) override;
    void setNotePan(int noteId, float pan) override;
    void setNotePitchOffset(int noteId, float pitchOffset) override;
    void setNoteTimbre(int noteId, float timbre) override;
    void setNotePressure(int noteId, float pressure) override;
    void setNotesExpression(int noteId, float gain, float pan, float pitchOffset, float timbre, float pressure) override;
    void setClipSeed(int clipId, uint64_t seed) override;
    void setNotesOperator(int clipId, int noteId, float chance, int repeatCount, float repeatRate, float repeatCurve, int occurrence, int recurrence) override;
    void clearNotes(int clipId) override;
    int mergeClips(const std::vector<int>& clipIds) override;

    // ProjectCommands — FX operations
    void addFxSlot(int trackIndex, int type, int position,
                   const std::string& pluginId) override;
    void addFxSlot(int trackIndex, const std::string& type,
                   int position, const std::string& pluginId) override;
    void addMidiFxSlot(int trackIndex, const std::string& type, int position) override;
    void removeMidiFxSlot(int trackIndex, int slotIndex) override;
    void setMidiFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed) override;
    void setMidiFxSlotParam(int trackIndex, int slotIndex,
                            const std::string& paramName, double value) override;
    void removeFxSlot(int trackIndex, int slotIndex) override;
    void setFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed) override;
    void setFxSlotParam(int trackIndex, int slotIndex, int paramIndex,
                        float value) override;
    void reorderFxSlots(int trackIndex, int fromSlot, int toSlot) override;
    void respawnFxSlot(int trackIndex, int slotIndex) override;

    // ProjectCommands — Automation
    void addAutomationLane(int trackIndex, const std::string& laneName, int paramID = 0) override;
    void removeAutomationLane(int trackIndex, const std::string& laneName) override;
    void addAutomationPoint(int trackIndex, const std::string& lane,
                            double time, float value) override;
    void removeAutomationPoint(int trackIndex, const std::string& lane,
                               double time) override;
    void setAutomationEnabled(int trackIndex, const std::string& lane,
                              bool enabled) override;
    void setAutomationMode(int trackIndex, const std::string& laneName,
                           const std::string& mode) override;
    void notifyAutomationTouch(int trackIndex, int paramID, bool touching) override;

    // ProjectCommands — Transport properties
    void setTempo(double bpm) override;
    int addTempoPoint(double timeSeconds, double bpm) override;
    void removeTempoPoint(int index) override;
    void setTempoPointBpm(int index, double bpm) override;
    void setTempoPointTime(int index, double timeSeconds) override;
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
    void setCcPoint(int ccId, double beat, int value) override;
    void removeCcPoint(int ccId) override;
    void setCcRecordArmed(bool armed) override;
    void setMidiNoteRecordArmed(bool armed) override;

    // ProjectCommands — Undo/redo
    void undo() override;
    void redo() override;
    bool canUndo() const override;
    bool canRedo() const override;
    std::vector<std::string> getUndoDescriptions() const override;
    std::vector<std::string> getRedoDescriptions() const override;

    // ProjectCommands — Transaction lifecycle
    void beginTransaction(const std::string& name) override;
    void endTransaction() override;

    // ProjectCommands — Markers
    int addMarker(const std::string& name, double time, int color) override;
    void removeMarker(int index) override;
    void setMarkerName(int index, const std::string& name) override;
    void setMarkerTime(int index, double time) override;

    // ProjectCommands — Arranger Regions
    std::string addArrangerRegion(const std::string& name, double startTime, double duration, int color) override;
    void removeArrangerRegion(const std::string& regionID) override;
    void setArrangerRegionName(const std::string& regionID, const std::string& name) override;
    void setArrangerRegionBounds(const std::string& regionID, double startTime, double duration) override;
    void setArrangerRegionColor(const std::string& regionID, int color) override;

    // ProjectCommands — Arranger Chains
    std::string addArrangerChain(const std::string& name) override;
    void removeArrangerChain(const std::string& chainID) override;
    void setArrangerChainName(const std::string& chainID, const std::string& name) override;
    void setArrangerChainActive(const std::string& chainID) override;

    // ProjectCommands — Chain Entries
    int addChainEntry(const std::string& chainID, const std::string& regionID, int repeatCount) override;
    void removeChainEntry(const std::string& chainID, int entryIndex) override;
    void reorderChainEntry(const std::string& chainID, int fromIndex, int toIndex) override;
    void setChainEntryRepeat(const std::string& chainID, int entryIndex, int repeatCount) override;

    // ProjectCommands — Flatten
    void flattenArranger() override;

    // ProjectCommands — Project persistence
    void newProject() override;
    bool saveProject(const std::string& filePath) override;
    bool loadProject(const std::string& filePath) override;

    // ProjectCommands — Scale
    void setScaleRoot(int root) override;
    void setScaleMode(int mode) override;

    // ProjectCommands — Missing source-file relinking
    std::string findMissingClipSourceFile(int clipId, const std::string& searchDir) override;
    RelinkResult relinkAllMissingFiles(const std::string& searchDir) override;

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
    void setPunchEnabled(bool enabled) override;
    bool isPunchEnabled() const override;

    // AudioGraphCommands
    void rebuildRoutingGraph(bool loading = false) override;
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

    // Find CC point by ID in a clip's CC_LIST. Sets outClipId.
    juce::ValueTree findCcPointById(int ccId, int& outClipId) const;

    // Find the FX_SLOT child at slotIndex in a track's FX_CHAIN.
    juce::ValueTree findFxSlot(int trackIndex, int slotIndex) const;
    juce::ValueTree findMidiFxSlot(int trackIndex, int slotIndex) const;

    // Look up a plugin's display name from the plugin service by fileOrIdentifier.
    std::string resolvePluginName(const std::string& pluginId) const;

    // Find the AUTOMATION child by lane name in a track.
    juce::ValueTree findAutomationLane(int trackIndex, const std::string& lane) const;

    // Add a new track ValueTree to the project. Returns the new index.
    juce::ValueTree createTrackValueTree(const std::string& name, int color, int parentBus, int trackType = 0);

    // Gain envelope helpers
    std::vector<ProjectModel::GainEnvelopePoint> getGainEnvelopePoints(int clipId);

    // CC bulk writer helper
    void setClipCcPoints(int clipId, int controllerNumber,
                         const std::vector<std::pair<double, double>>& points);

    AudioEngine& engine_;
};
