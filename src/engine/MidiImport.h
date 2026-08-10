#pragma once
#include <QString>
#include <vector>
#include "../engine/AudioEngine.h"

namespace HDAW
{
    // Import MIDI tracks from a .mid file.
    // trackIdx >= 0: place all MIDI tracks as clips on that track (legacy).
    // trackIdx == -1: create a new HDAW track per MIDI track (default).
    // Returns clip IDs of imported clips (empty on failure).
    std::vector<int> importMidiFile(AudioEngine& engine, const QString& path, int trackIdx = -1);
}
