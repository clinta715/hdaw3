#pragma once
#include <QString>
#include "../engine/AudioEngine.h"

namespace juce { class AudioFormatReader; }

namespace HDAW
{
    double readBpmFromMetadata(juce::AudioFormatReader* reader);
    bool importAudioFile(AudioEngine& engine, const QString& path, int trackIdx = -1);
    bool normalizeAudioFile(AudioEngine& engine, const QString& sourcePath, QString& outPath);
    bool reverseAudioFile(AudioEngine& engine, const QString& sourcePath, QString& outPath);
}
