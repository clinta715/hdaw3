#pragma once
#include <cstdint>

namespace HDAW {

inline float deterministicChance(uint64_t seed, int noteIndex, int loopCount) {
    uint64_t h = seed;
    h ^= static_cast<uint64_t>(noteIndex) * 0x9e3779b97f4a7c15ULL;
    h ^= static_cast<uint64_t>(loopCount) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    h = h ^ (h >> 31);
    return static_cast<float>(h & 0xFFFFFFFF) / 4294967296.0f;
}

inline bool chanceCheck(float chance, uint64_t seed, int noteIndex, int loopCount) {
    if (chance >= 1.0f) return true;
    if (chance <= 0.0f) return false;
    return deterministicChance(seed, noteIndex, loopCount) < chance;
}

inline bool occurrenceCheck(int occurrence, int loopCount, int cycleSize) {
    if (occurrence == 0) return true;
    int cyclePos = loopCount % cycleSize;
    return (occurrence & (1 << cyclePos)) != 0;
}

inline bool recurrenceCheck(int recurrence, bool previousPlayed) {
    if (recurrence == 0) return true;
    if (recurrence == 1) return previousPlayed;
    if (recurrence == 2) return !previousPlayed;
    return true;
}

} // namespace HDAW