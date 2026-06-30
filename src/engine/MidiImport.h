#pragma once
#include <QString>
#include "../engine/AudioEngine.h"

namespace HDAW
{
    bool importMidiFile(AudioEngine& engine, const QString& path, int trackIdx = -1);
}
