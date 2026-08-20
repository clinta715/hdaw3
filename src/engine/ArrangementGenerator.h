#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "engine/Generative.h"

namespace HDAW
{

// ── Arrangement generation ──
// Deterministic, multi-track groove arranger inspired by teknoir: plans song
// sections, drives every instrument from a per-bar energy curve, and coordinates
// parts (bass ducks the kick, hats avoid the clap). Pure function of its params.

enum class TrackRole
{
    Kick = 0,
    ClosedHat,
    OpenHat,
    Clap,
    Snare,
    Bass,
    Lead,
    Chords
};

const char* trackRoleName (TrackRole role);

enum class Section
{
    Intro = 0,
    BuildUp,
    MainA,
    MainB,
    Drop,
    Break,
    Outro
};

const char* sectionName (Section s);

struct ArrangementNote
{
    double startBeat = 0.0;
    int noteNumber = 0;
    int velocity = 0;
    double durationBeats = 0.25;
};

struct ArrangementPart
{
    TrackRole role = TrackRole::Kick;
    std::string name;
    int trackType = 0;                 // hint for project integration (0 = MIDI)
    std::vector<ArrangementNote> notes;
};

struct ArrangementParams
{
    int bars = 32;
    double bpm = 120.0;
    int scaleRoot = 0;                 // 0..11
    int scaleMode = 1;                 // PhraseGenerator scale index (1 = Minor/Aeolian)
    uint64_t seed = 0;                 // 0 = non-deterministic; else reproducible
    int style = 0;                     // 0 = Techno (archetype/density flavour)
    double complexity = 0.5;           // 0..1
    double swingPercent = 50.0;        // 50 = straight

    bool enableKick = true;
    bool enableClosedHat = true;
    bool enableOpenHat = true;
    bool enableClap = true;
    bool enableSnare = false;
    bool enableBass = true;
    bool enableLead = false;
    bool enableChords = false;

    std::map<std::string, int> targetTrackIds;
};

struct Arrangement
{
    std::vector<ArrangementPart> parts;
    std::vector<std::pair<int, Section>> sectionMap;  // (startBar, section)
    std::vector<int> energyByBar;                     // 0..3 per bar
    uint64_t resolvedSeed = 0;                        // actual seed used
};

Arrangement generateArrangement (const ArrangementParams& params);

// Exposed for tests / introspection.
std::vector<std::pair<int, Section>> buildSectionMap (int bars);
std::vector<int> energyCurveFromSections (int bars, const std::vector<std::pair<int, Section>>& sectionMap);

} // namespace HDAW
