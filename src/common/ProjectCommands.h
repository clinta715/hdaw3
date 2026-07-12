#pragma once
#include <cstdint>
#include <string>

class ProjectCommands
{
public:
    virtual ~ProjectCommands() = default;

    // Track operations
    virtual int addTrack(const std::string& name, int color = -1, int parentBus = -1) = 0;
    virtual void removeTrack(int trackIndex) = 0;
    virtual void moveTrack(int trackIndex, int newIndex) = 0;
    virtual void setTrackName(int trackIndex, const std::string& name) = 0;
    virtual void setTrackColor(int trackIndex, int color) = 0;
    virtual void setTrackVolume(int trackIndex, float volume) = 0;
    virtual void setTrackPan(int trackIndex, float pan) = 0;
    virtual void setTrackMuted(int trackIndex, bool muted) = 0;
    virtual void setTrackSoloed(int trackIndex, bool soloed) = 0;
    virtual void setTrackArmed(int trackIndex, bool armed) = 0;
    virtual void setTrackInputMonitor(int trackIndex, bool monitor) = 0;
    virtual void setTrackHeight(int trackIndex, int height) = 0;
    virtual void setTrackMidiChannel(int trackIndex, int channel) = 0;

    // Clip operations
    virtual int addAudioClip(int trackIndex, double start, double duration,
                             const std::string& sourceFile, const std::string& name) = 0;
    virtual int addMidiClip(int trackIndex, double start, double duration,
                            const std::string& name) = 0;
    virtual void removeClip(int clipId) = 0;
    virtual void moveClip(int clipId, int newTrackIndex, double newStart) = 0;
    virtual void setClipStart(int clipId, double start) = 0;
    virtual void setClipDuration(int clipId, double duration) = 0;
    virtual void setClipGain(int clipId, float gain) = 0;
    virtual void setClipFadeIn(int clipId, double fadeIn) = 0;
    virtual void setClipFadeOut(int clipId, double fadeOut) = 0;
    virtual void setClipOffset(int clipId, double offset) = 0;
    virtual void setClipLooping(int clipId, bool looping) = 0;
    virtual int duplicateClip(int clipId) = 0;

    // MIDI note operations
    virtual int addNote(int clipId, int pitch, int velocity,
                        double startBeat, double durationBeats) = 0;
    virtual void removeNote(int noteId) = 0;
    virtual void setNotePitch(int noteId, int pitch) = 0;
    virtual void setNoteVelocity(int noteId, int velocity) = 0;
    virtual void setNoteStart(int noteId, double startBeat) = 0;
    virtual void setNoteDuration(int noteId, double durationBeats) = 0;
    virtual void clearNotes(int clipId) = 0;

    // FX operations
    virtual void addFxSlot(int trackIndex, int type, int position = -1,
                           const std::string& pluginId = "") = 0;
    virtual void removeFxSlot(int trackIndex, int slotIndex) = 0;
    virtual void setFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed) = 0;
    virtual void setFxSlotParam(int trackIndex, int slotIndex, int paramIndex,
                                float value) = 0;
    virtual void reorderFxSlots(int trackIndex, int fromSlot, int toSlot) = 0;

    // Automation
    virtual void addAutomationLane(int trackIndex, const std::string& laneName) = 0;
    virtual void removeAutomationLane(int trackIndex, const std::string& laneName) = 0;
    virtual void addAutomationPoint(int trackIndex, const std::string& lane,
                                     double time, float value) = 0;
    virtual void removeAutomationPoint(int trackIndex, const std::string& lane,
                                        double time) = 0;
    virtual void setAutomationEnabled(int trackIndex, const std::string& lane,
                                       bool enabled) = 0;

    // Transport properties
    virtual void setTempo(double bpm) = 0;
    virtual void setLoopStart(double beat) = 0;
    virtual void setLoopEnd(double beat) = 0;
    virtual void setLooping(bool looping) = 0;
    virtual void setMetronomeEnabled(bool enabled) = 0;

    // Markers
    virtual int addMarker(const std::string& name, double time, int color = 0xFF59e0c4) = 0;
    virtual void removeMarker(int index) = 0;
    virtual void setMarkerName(int index, const std::string& name) = 0;
    virtual void setMarkerTime(int index, double time) = 0;

    // Undo/redo
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;

    // Project lifecycle
    virtual void newProject() = 0;
    virtual bool saveProject(const std::string& filePath) = 0;
    virtual bool loadProject(const std::string& filePath) = 0;

    // Scale
    virtual void setScaleRoot(int root) = 0;
    virtual void setScaleMode(int mode) = 0;
};
