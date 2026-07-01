#pragma once
#include <QString>
#include "../engine/AudioEngine.h"

namespace HDAW
{
    bool importAudioFile(AudioEngine& engine, const QString& path, int trackIdx = -1);
    bool normalizeAudioFile(AudioEngine& engine, const QString& sourcePath, QString& outPath);
    bool reverseAudioFile(AudioEngine& engine, const QString& sourcePath, QString& outPath);
}
