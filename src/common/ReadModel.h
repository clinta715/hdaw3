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
    double currentSample = 0.0;
    double sampleRate = 44100.0;
};

struct ProjectSnapshot {
    std::string name;
    TransportSnapshot transport;
    std::vector<TrackSnapshot> tracks;
    std::vector<ClipSnapshot> clips;
    int scaleRoot = 0;
    int scaleMode = 0;
};

class ReadModel {
public:
    virtual ~ReadModel() = default;

    virtual ProjectSnapshot snapshot() const = 0;
    virtual int getTrackCount() const = 0;
    virtual TrackSnapshot getTrack(int index) const = 0;
    virtual ClipSnapshot getClip(int clipId) const = 0;
    virtual std::vector<NoteSnapshot> getNotes(int clipId) const = 0;
    virtual TransportSnapshot getTransport() const = 0;
    virtual int getScaleRoot() const = 0;
    virtual int getScaleMode() const = 0;
};
