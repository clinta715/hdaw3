#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace juce { class AudioFormatManager; }

namespace HDAW {

struct LoopAnalysis {
    bool ok = false;
    double bpm = 0.0;
    double confidence = 0.0;
    double beatInterval = 0.0;
    double downbeatOffset = 0.0;      // source seconds from file start to grid origin (beat 0)
    int bars = 0;
    int beatsPerBar = 4;
    double loopSpanSourceSeconds = 0.0; // source seconds [gridOrigin, gridOrigin + bars*beatsPerBar*beatInterval]
    double leadingSlack = 0.0;        // source seconds file start -> first onset
    double trailingSlack = 0.0;       // source seconds last onset -> loop end
    std::vector<double> onsetTimes;   // source seconds
};

class LoopAnalyzer {
public:
    static LoopAnalysis analyze(const juce::String& sourceFile,
                                juce::AudioFormatManager& formatManager,
                                double maxSeconds = 60.0, double sensitivity = 0.5);
    static LoopAnalysis analyzeBuffer(const juce::AudioBuffer<float>& mono,
                                      double sampleRate, double sensitivity = 0.5);
};

} // namespace HDAW