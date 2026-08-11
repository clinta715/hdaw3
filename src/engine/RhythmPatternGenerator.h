#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ── Rhythmic pattern generation ──
// Deterministic drum-pattern builder: two independent euclidean pulses
// (the classic polyrhythm engine — e.g. 4-over-3) plus an optional
// rhythm-DSL voice (Strudel subset: 'x' '-' '[..]xN' 'E(k,n[,rot])',
// expanded via Generative::expandToDivision). Pure function of its
// params: identical params → identical notes. No RNG, no seed.
class RhythmPatternGenerator
{
public:
    struct Params
    {
        int grid = 16;           // steps per bar (16 = 16th notes)
        int bars = 1;            // loop length in bars; pulses span grid*bars steps

        int pulseA = 4;          // hits of pulse A across the loop (0 = disabled)
        int pulseB = 3;          // hits of pulse B across the loop (0 = disabled)
        int rotationA = 1;       // rotate pulse A hits by this many steps
        int rotationB = 1;
        int pitchA = 36;         // C2 kick
        int pitchB = 42;         // F#2 closed hat
        int velocityA = 112;
        int velocityB = 96;

        std::string dsl;         // optional DSL voice, e.g. "E(3,8,1) [x-]x2"
        int dslPitch = 39;       // C#2 clap
        int dslVelocity = 104;
    };

    struct Note
    {
        double startBeat;
        int pitch;
        int velocity;
        double durationBeats;
    };

    // Pulse A (highest priority) > pulse B > DSL voice; simultaneous hits
    // on one step resolve by priority. Output sorted by startBeat.
    // Throws std::invalid_argument on a malformed DSL string.
    static std::vector<Note> generate(const Params& params);
};
