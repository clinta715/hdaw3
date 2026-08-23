#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <cstdint>

namespace HDAW {

struct MidiFingerprint {
    // Rhythm
    double avgNoteDensity = 0.0;       // notes per beat
    double rhythmComplexity = 0.0;     // 0-1, deviation from grid
    double syncopationScore = 0.0;     // 0-1, off-beat emphasis
    double swingAmount = 0.0;          // 0-1, timing shuffle

    // Pitch
    int pitchRange = 0;                // semitones spanned
    int rootNote = 60;                 // detected root (MIDI note)
    int scaleType = 0;                 // index into PhraseGenerator scale modes, -1=unknown
    double chromaticism = 0.0;         // 0-1, ratio of non-scale tones

    // Velocity
    double avgVelocity = 0.0;          // 0-1 normalized
    double velocityRange = 0.0;        // 0-1
    double velocityDynamicRange = 0.0; // std dev

    // Timing
    double quantizationStrength = 0.0; // 0-1, how close to grid
    double avgNoteDuration = 0.0;      // beats

    // Structure
    double barCount = 0.0;
    int voiceCount = 0;
    double avgPolyphony = 0.0;
};

struct MidiPattern {
    struct Note {
        double startBeat = 0.0;   // relative to pattern start
        int pitch = 0;
        int velocity = 0;        // 0-127
        double durationBeats = 0.0;
    };

    juce::String name;
    int startBar = 0;
    int lengthBars = 1;
    int trackIndex = 0;
    std::vector<Note> notes;
    double frequency = 0.0;      // how often this pattern repeats
    bool isMotif = false;        // true=sub-bar, false=bar-aligned
};

struct MidiAnalysisResult {
    MidiFingerprint fingerprint;
    std::vector<MidiPattern> patterns;

    int guessedStyle = 0;               // PhraseGenerator::Style enum value
    float styleConfidence = 0.0f;       // 0-1
    juce::String paramsJson;            // PhraseParams as JSON
    juce::String styleParamsJson;       // style-specific params as JSON

    juce::String fileName;
    double sourceBpm = 120.0;
    int timeSignatureNum = 4;
    int timeSignatureDen = 4;
    int trackCount = 0;
};

class MidiAnalyzer {
public:
    // Analyze a MIDI file on disk. Returns empty result on failure.
    static MidiAnalysisResult analyze(const juce::File& midiFile);

    // Analyze from pre-parsed MidiFile (for testing / in-memory use).
    static MidiAnalysisResult analyzeMidiFile(const juce::MidiFile& midiFile,
                                              const juce::String& fileName = {});

    // Convert analysis result to a PatternPreset-compatible JSON pair.
    // Returns {paramsJson, styleParamsJson}.
    static std::pair<juce::String, juce::String> toPatternJson(
        const MidiAnalysisResult& result);

    // Classify a fingerprint into a PhraseGenerator style index.
    static int classifyStyle(const MidiFingerprint& fp);

private:
    struct TrackAnalysis {
        struct Note {
            double startBeat;
            int pitch;
            int velocity;   // 0-127
            double durationBeats;
        };
        std::vector<Note> notes;
        double totalBeats = 0.0;
    };

    static TrackAnalysis analyzeTrack(const juce::MidiMessageSequence& track,
                                      double ticksPerQuarter);
    static MidiFingerprint computeFingerprint(const std::vector<TrackAnalysis>& tracks,
                                              double beatsPerBar);
    static std::vector<MidiPattern> extractBarPatterns(const std::vector<TrackAnalysis>& tracks,
                                                       double beatsPerBar);
    static std::vector<MidiPattern> extractMotifs(const std::vector<TrackAnalysis>& tracks,
                                                   double beatsPerBar);
    static std::pair<juce::String, juce::String> extractParams(
        const MidiFingerprint& fp, int style, double beatsPerBar);

    static int detectScale(const std::vector<TrackAnalysis>& tracks, int& rootNote);
    static double detectSwing(const std::vector<TrackAnalysis>& tracks, double beatDivision);
    static double detectGridInterval(const std::vector<TrackAnalysis>& tracks);
};

} // namespace HDAW
