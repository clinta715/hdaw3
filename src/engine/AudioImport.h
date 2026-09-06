#pragma once
#include <QString>
#include "../engine/AudioEngine.h"

namespace juce { class AudioFormatReader; }

namespace HDAW
{
    double readBpmFromMetadata(juce::AudioFormatReader* reader);
    // Imports an audio file onto the given track (trackIdx < 0 = first track).
    // Returns the new clipId, or -1 on failure. startTimeSec >= 0 pins the
    // clip start; startTimeSec < 0 appends after the last clip on the track.
    // alignToGrid runs LoopAnalyzer on the file and writes grid-aligned
    // sourceBpm/offset/duration/stretchMode/stretchRatio when a grid is found
    // (the final stretchRatio write triggers the routing rebuild); otherwise
    // it falls back to silence-trim + metadata/aubio BPM.
    int importAudioFile(AudioEngine& engine, const QString& path, int trackIdx = -1,
                        double startTimeSec = -1.0, bool alignToGrid = true);
    bool normalizeAudioFile(AudioEngine& engine, const QString& sourcePath, QString& outPath);
    bool reverseAudioFile(AudioEngine& engine, const QString& sourcePath, QString& outPath);

    struct SilenceBounds {
        double leadingSeconds = 0.0;
        double trailingSeconds = 0.0;
    };

    SilenceBounds detectSilenceBounds(juce::AudioFormatReader& reader, float threshold = 0.001f);
}
