#pragma once
#include <string>
#include <vector>

struct TrackSnapshot {
    int index = 0;
    std::string name;
    int color = 0;
    double volume = 1.0;
    double pan = 0.0;
    bool muted = false;
    bool soloed = false;
    bool armed = false;
    bool inputMonitor = false;
    double height = 80.0;
    int midiChannel = 1;
    int clipCount = 0;
};

struct ClipSnapshot {
    int clipId = 0;
    int trackIndex = 0;
    std::string name;
    std::string sourceFile;
    double startBeat = 0.0;
    double durationBeats = 0.0;
    double offset = 0.0;
    double gain = 1.0;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    bool looping = false;
    bool isMidi = false;
    // Timestretch (audio clips only; zeroed for MIDI).
    double sourceBpm = 0.0;      // 0 = unknown
    int stretchMode = 0;         // 0=Off, 1=TempoMatch, 2=ManualRatio
    double stretchRatio = 1.0;   // time ratio vs original source
    double sourceDuration = 0.0; // original source length in seconds

    // Gain envelope
    struct GainEnvelopePoint { double time; double gain; };
    std::vector<GainEnvelopePoint> gainEnvelope;
};

struct NoteSnapshot {
    int noteId = 0;
    int pitch = 0;
    int velocity = 0;
    double startBeat = 0.0;
    double durationBeats = 0.0;
};

struct TransportSnapshot {
    double bpm = 120.0;
    bool isPlaying = false;
    bool isLooping = false;
    bool isRecording = false;
    double loopStart = 0.0;
    double loopEnd = 8.0;
    double currentTimeSeconds = 0.0;
    double sampleRate = 0.0;
};

struct ProjectSnapshot {
    std::string name;
    TransportSnapshot transport;
    std::vector<TrackSnapshot> tracks;
    std::vector<ClipSnapshot> clips;
    int scaleRoot = 0;
    int scaleMode = 0;
};

struct FxSlotSnapshot {
    int slotIndex = 0;
    std::string fxType;
    std::string pluginId;
    std::string pluginName;
    bool bypassed = false;
    int paramCount = 0;
};

struct AutomationLaneSnapshot {
    int laneIndex = 0;
    std::string name;
    int paramID = 0;
    bool enabled = false;
};

struct AutomationPointSnapshot {
    double time = 0.0;
    float value = 0.0f;
};

struct MarkerSnapshot {
    int index = 0;
    double time = 0.0;
    std::string name;
    int color = 0;
};

struct TempoPointSnapshot {
    double timeSeconds = 0.0;
    double bpm = 120.0;
};

struct AutomatableParamSnapshot {
    int slotIndex = 0;
    int paramIndex = 0;
    std::string name;
    bool automatable = false;
};

struct MeterSnapshot {
    float leftLevel = 0.0f;
    float rightLevel = 0.0f;
};

class ReadModel {
public:
    virtual ~ReadModel() = default;

    virtual ProjectSnapshot snapshot() const = 0;
    virtual int getTrackCount() const = 0;
    virtual TrackSnapshot getTrack(int index) const = 0;
    virtual ClipSnapshot getClip(int clipId) const = 0;
    virtual std::vector<NoteSnapshot> getNotes(int clipId) const = 0;
    virtual std::vector<ClipSnapshot::GainEnvelopePoint> getClipGainEnvelope(int clipId) const = 0;
    virtual TransportSnapshot getTransport() const = 0;
    virtual int getScaleRoot() const = 0;
    virtual int getScaleMode() const = 0;

    virtual std::vector<FxSlotSnapshot> getFxSlots(int trackIndex) const = 0;
    virtual std::vector<AutomationLaneSnapshot> getAutomationLanes(int trackIndex) const = 0;
    virtual std::vector<AutomationPointSnapshot> getAutomationPoints(int trackIndex,
        const std::string& laneName) const = 0;
    virtual std::vector<MarkerSnapshot> getMarkers() const = 0;
    virtual std::vector<TempoPointSnapshot> getTempoPoints() const = 0;
    virtual std::vector<AutomatableParamSnapshot> getAutomatableParams(int trackIndex) const = 0;
    virtual MeterSnapshot getTrackMeter(int trackIndex) const = 0;
    virtual MeterSnapshot getMasterMeter() const = 0;
    virtual bool isDirty() const = 0;
};
