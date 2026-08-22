#include "PhraseGenerator.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
// Per-call RNG context: each generate* installs a seeded SplitMix64 so output is
// reproducible for a given seed. Thread-local + RAII save/restore makes it
// thread-safe and reentrant (generateProgression nests generateChord calls).
HDAW::SplitMix64*& rngSlot()
{
    thread_local HDAW::SplitMix64* slot = nullptr;
    return slot;
}

HDAW::SplitMix64& legacyRng()
{
    static HDAW::SplitMix64 gen([] {
        std::random_device d;
        uint64_t s = (static_cast<uint64_t>(d()) << 32) ^ d();
        return s ? s : static_cast<uint64_t>(0x9E3779B97F4A7C15ULL);
    }());
    return gen;
}

HDAW::SplitMix64& current()
{
    auto* s = rngSlot();
    return s ? *s : legacyRng();
}

struct ScopedRng
{
    HDAW::SplitMix64* prev;
    explicit ScopedRng(HDAW::SplitMix64& r) : prev(rngSlot()) { rngSlot() = &r; }
    ~ScopedRng() { rngSlot() = prev; }
};
}

HDAW::SplitMix64 PhraseGenerator::makeRng(uint64_t seed)
{
    if (seed != 0)
        return HDAW::SplitMix64(seed);
    std::random_device dev;
    uint64_t s = (static_cast<uint64_t>(dev()) << 32) ^ static_cast<uint64_t>(dev());
    if (s == 0) s = 0x9E3779B97F4A7C15ULL;
    return HDAW::SplitMix64(s);
}

int PhraseGenerator::randomInt(int min, int max)
{
    if (min >= max) return min;
    return current().nextInt(min, max);
}

double PhraseGenerator::randomDouble(double min, double max)
{
    if (min >= max) return min;
    return min + current().nextFloat() * (max - min);
}

// ── Scale modes ──

const std::vector<PhraseGenerator::ScaleMode>& PhraseGenerator::getScaleModes()
{
    static const std::vector<ScaleMode> modes = {
        { 0,  "Major (Ionian)",      {0, 2, 4, 5, 7, 9, 11} },
        { 1,  "Minor (Aeolian)",     {0, 2, 3, 5, 7, 8, 10} },
        { 2,  "Dorian",              {0, 2, 3, 5, 7, 9, 10} },
        { 3,  "Phrygian",            {0, 1, 3, 5, 7, 8, 10} },
        { 4,  "Lydian",              {0, 2, 4, 6, 7, 9, 11} },
        { 5,  "Mixolydian",          {0, 2, 4, 5, 7, 9, 10} },
        { 6,  "Locrian",             {0, 1, 3, 5, 6, 8, 10} },
        { 7,  "Harmonic Minor",      {0, 2, 3, 5, 7, 8, 11} },
        { 8,  "Melodic Minor",       {0, 2, 3, 5, 7, 9, 11} },
        { 9,  "Pentatonic Major",    {0, 2, 4, 7, 9} },
        { 10, "Pentatonic Minor",    {0, 3, 5, 7, 10} },
        { 11, "Blues",               {0, 3, 5, 6, 7, 10} },
        { 12, "Chromatic",           {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11} },
    };
    return modes;
}

const char* PhraseGenerator::modeName(int scaleModeIndex)
{
    const auto& modes = getScaleModes();
    for (const auto& m : modes)
        if (m.index == scaleModeIndex)
            return m.name;
    return "Major (Ionian)";
}

const char* PhraseGenerator::styleName(Style s)
{
    switch (s) {
        case Arpeggio:        return "Arpeggio";
        case BassLine:        return "Bass Line";
        case ChordStab:       return "Chord Stab";
        case Pad:             return "Pad";
        case Lead:            return "Lead";
        case RandomWalk:      return "Random Walk";
        case Buildup:         return "Buildup";
        case Euclidean:       return "Euclidean";
        case Percussion:      return "Percussion";
        case TrapHiHat:       return "Trap Hi-Hat";
        case DrillBass:       return "Drill Bass";
        case Counterpoint:    return "Counterpoint";
        case WalkingBass:     return "Walking Bass";
        case SwingComping:    return "Swing Comping";
        case MarkovMelody:    return "Markov Melody";
        case EvolvingTexture: return "Evolving Texture";
        case Aleatoric:       return "Aleatoric";
        case ScalarRun:       return "Scalar Run";
        case ChordToneSeq:    return "Chord Tone Seq";
        case CallResponse:    return "Call & Response";
        case PhaseShift:      return "Phase Shift";
        case AdditiveRhythm:  return "Additive Rhythm";
        case MinimalistLoop:  return "Minimalist Loop";
        case Layered:         return "Layered";
        default:              return "Standard";
    }
}

// ── Chord types ──

const std::vector<PhraseGenerator::ChordType>& PhraseGenerator::getChordTypes()
{
    static const std::vector<ChordType> types = {
        { 0,  "Major",        {0, 4, 7} },
        { 1,  "Minor",        {0, 3, 7} },
        { 2,  "Diminished",   {0, 3, 6} },
        { 3,  "Augmented",    {0, 4, 8} },
        { 4,  "Sus2",         {0, 2, 7} },
        { 5,  "Sus4",         {0, 5, 7} },
        { 6,  "Dominant 7",   {0, 4, 7, 10} },
        { 7,  "Major 7",      {0, 4, 7, 11} },
        { 8,  "Minor 7",      {0, 3, 7, 10} },
        { 9,  "Diminished 7", {0, 3, 6, 9} },
        { 10, "Half-dim 7",   {0, 3, 6, 10} },
        { 11, "Min Maj 7",    {0, 3, 7, 11} },
        { 12, "Dominant 9",   {0, 4, 7, 10, 14} },
        { 13, "Minor 9",      {0, 3, 7, 10, 14} },
        { 14, "Major 9",      {0, 4, 7, 11, 14} },
        { 15, "Power Chord",  {0, 7} },
        { 16, "6th",          {0, 4, 7, 9} },
        { 17, "Minor 6th",    {0, 3, 7, 9} },
    };
    return types;
}

const char* PhraseGenerator::chordTypeName(int chordTypeIndex)
{
    const auto& types = getChordTypes();
    for (const auto& t : types)
        if (t.index == chordTypeIndex)
            return t.name;
    return "Major";
}

// ── Progression patterns ──
// Each pair: (scale_degree 0-6, chordTypeIndex)
// degree maps through the scale's root notes

const std::vector<PhraseGenerator::ProgressionPattern>& PhraseGenerator::getProgressionPatterns()
{
    static const std::vector<ProgressionPattern> patterns = {
        { 0,  "I – IV – V – I",
            {{0, 0}, {3, 0}, {4, 0}, {0, 0}} },
        { 1,  "I – V – vi – IV (Pop)",
            {{0, 0}, {4, 0}, {5, 1}, {3, 0}} },
        { 2,  "ii – V – I (Jazz)",
            {{1, 1}, {4, 6}, {0, 0}} },
        { 3,  "I – vi – IV – V (50s)",
            {{0, 0}, {5, 1}, {3, 0}, {4, 0}} },
        { 4,  "i – VII – VI – V (Minor)",
            {{0, 1}, {6, 0}, {5, 0}, {4, 0}} },
        { 5,  "i – iv – v – i (Minor)",
            {{0, 1}, {3, 1}, {4, 1}, {0, 1}} },
        { 6,  "I – IV – vi – V",
            {{0, 0}, {3, 0}, {5, 1}, {4, 0}} },
        { 7,  "I – iii – IV – V",
            {{0, 0}, {2, 1}, {3, 0}, {4, 0}} },
        { 8,  "vi – IV – I – V (Sad Pop)",
            {{5, 1}, {3, 0}, {0, 0}, {4, 0}} },
        { 9,  "I – II – IV – I (Lydian)",
            {{0, 0}, {1, 0}, {3, 0}, {0, 0}} },
        { 10, "Andalusian (i – VII – VI – V)",
            {{0, 1}, {6, 0}, {5, 0}, {4, 0}} },
        { 11, "12-Bar Blues",
            {{0, 6}, {0, 6}, {0, 6}, {0, 6},
             {3, 6}, {3, 6}, {0, 6}, {0, 6},
             {4, 6}, {3, 6}, {0, 6}, {4, 6}} },
    };
    return patterns;
}

// ── Utility ──

const char* PhraseGenerator::noteName(int noteNumber)
{
    static const char* names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int octave = (noteNumber / 12) - 1;
    static thread_local char buf[8];
    snprintf(buf, sizeof(buf), "%s%d", names[noteNumber % 12], octave);
    return buf;
}

// ── Core helpers ──

std::vector<int> PhraseGenerator::buildScalePitches(int rootNote, int scaleModeIndex,
                                                     int lowNote, int highNote)
{
    const auto& modes = getScaleModes();
    const std::vector<int>* intervals = nullptr;
    for (const auto& m : modes)
    {
        if (m.index == scaleModeIndex)
        {
            intervals = &m.intervals;
            break;
        }
    }
    if (intervals == nullptr || intervals->empty())
        return {};

    std::vector<int> pitches;
    for (int n = lowNote; n <= highNote; ++n)
    {
        int semitone = (n - rootNote) % 12;
        if (semitone < 0) semitone += 12;
        for (int iv : *intervals)
        {
            if (semitone == iv)
            {
                pitches.push_back(n);
                break;
            }
        }
    }
    return pitches;
}

// Returns the MIDI note numbers of the 7 diatonic roots in a given scale.
// octave selects which octave to place them.
std::vector<int> PhraseGenerator::diatonicRoots(int scaleRoot, int scaleModeIndex, int octave)
{
    const auto& modes = getScaleModes();
    const std::vector<int>* intervals = nullptr;
    for (const auto& m : modes)
    {
        if (m.index == scaleModeIndex)
        {
            intervals = &m.intervals;
            break;
        }
    }
    if (intervals == nullptr || intervals->empty())
        return {};

    std::vector<int> roots;
    int base = scaleRoot + (octave + 1) * 12;
    for (size_t i = 0; i < intervals->size(); ++i)
        roots.push_back(base + (*intervals)[i]);
    return roots;
}

// ── Style-based phrase generation ──

std::vector<PhraseGenerator::GeneratedNote> PhraseGenerator::generatePhrase(const PhraseParams& params)
{
    auto pitches = buildScalePitches(params.scaleRoot, params.scaleMode,
                                     params.lowNote, params.highNote);
    if (pitches.empty())
    {
        for (int n = params.lowNote; n <= params.highNote; ++n)
            pitches.push_back(n);
    }
    if (pitches.empty())
        return {};

    auto rng = makeRng(params.seed);
    ScopedRng scope(rng);

    std::vector<GeneratedNote> result;
    int numNotes = (std::max)(1, params.density);

    switch (params.style)
    {
    case Arpeggio:
    {
        // Ascending/descending through scale notes, short durations
        double beatStep = params.lengthBeats / static_cast<double>(numNotes);
        int pitchIdx = 0;
        for (int i = 0; i < numNotes; ++i)
        {
            GeneratedNote n;
            n.startBeat = static_cast<double>(i) * beatStep;
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatStep * 0.8;

            int idx = randomInt(pitchIdx, pitchIdx + 2);
            n.noteNumber = pitches[idx % pitches.size()];
            pitchIdx = (pitchIdx + 1) % (static_cast<int>(pitches.size()) - 2);
            result.push_back(n);
        }
        break;
    }

    case BassLine:
    {
        // Low octave, root/fifth heavy, simpler rhythm
        double beatStep = params.lengthBeats / static_cast<double>(numNotes);
        for (int i = 0; i < numNotes; ++i)
        {
            GeneratedNote n;
            n.startBeat = static_cast<double>(i) * beatStep;
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatStep * 0.9;

            // Bass: prefer root, fifth, octave
            double r = randomDouble(0.0, 1.0);
            if (r < 0.45 && pitches.size() > 0)
                n.noteNumber = pitches[0]; // root
            else if (r < 0.7 && pitches.size() > 4)
                n.noteNumber = pitches[4];   // fifth
            else
                n.noteNumber = pitches[randomInt(0, static_cast<int>(pitches.size()) - 1)];

            // Shift down an octave if above lowNote
            while (n.noteNumber - 12 >= params.lowNote)
                n.noteNumber -= 12;

            result.push_back(n);
        }
        break;
    }

    case ChordStab:
    {
        // All notes on beat 0 (or spread slightly), short, chord tones
        auto intervals = getChordTypes()[0].intervals; // major triad
        double base = pitches.empty() ? 60.0 : static_cast<double>(pitches[0]);
        for (size_t i = 0; i < intervals.size(); ++i)
        {
            GeneratedNote n;
            n.startBeat = randomDouble(0.0, 0.05); // slight offset
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = 0.25;

            int note = static_cast<int>(base) + intervals[i];
            if (note >= params.lowNote && note <= params.highNote)
            {
                n.noteNumber = note;
                result.push_back(n);
            }
        }
        break;
    }

    case Pad:
    {
        // Long notes, few of them, wide spread
        int padNotes = (std::min)(numNotes, 8);
        // Choose a few wide-spread scale notes
        std::vector<int> used;
        for (int i = 0; i < padNotes; ++i)
        {
            GeneratedNote n;
            n.startBeat = randomDouble(0.0, params.lengthBeats * 0.3);
            n.velocity = randomInt(params.minVelocity, params.maxVelocity - 20);
            n.durationBeats = params.lengthBeats * randomDouble(0.6, 1.0);

            int idx;
            do {
                idx = randomInt(0, static_cast<int>(pitches.size()) - 1);
            } while (std::find(used.begin(), used.end(), idx) != used.end() && used.size() < pitches.size());
            used.push_back(idx);
            n.noteNumber = pitches[idx];
            result.push_back(n);
        }
        break;
    }

    case Lead:
    {
        // Single-note line, varied rhythm, medium range
        double beatStep = params.lengthBeats / static_cast<double>(numNotes);
        int currentIdx = randomInt(0, static_cast<int>(pitches.size()) - 1);
        for (int i = 0; i < numNotes; ++i)
        {
            GeneratedNote n;
            n.startBeat = static_cast<double>(i) * beatStep * randomDouble(0.7, 1.3);
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatStep * randomDouble(0.3, 0.9);

            // Stepwise motion with occasional leaps
            int step = randomDouble(0, 1) < 0.7 ? randomInt(-2, 2) : randomInt(-5, 5);
            if (step == 0) step = randomInt(-1, 1);
            if (step == 0) step = 1;

            currentIdx = (currentIdx + step + static_cast<int>(pitches.size())) % static_cast<int>(pitches.size());
            if (currentIdx < 0) currentIdx = 0;
            if (currentIdx >= static_cast<int>(pitches.size())) currentIdx = static_cast<int>(pitches.size()) - 1;

            n.noteNumber = pitches[currentIdx];
            result.push_back(n);
        }
        break;
    }

    case RandomWalk:
    {
        // Pentatonic-biased random walk
        double beatStep = params.lengthBeats / static_cast<double>(numNotes);
        int currentIdx = randomInt(0, static_cast<int>(pitches.size()) - 1);
        for (int i = 0; i < numNotes; ++i)
        {
            GeneratedNote n;
            n.startBeat = static_cast<double>(i) * beatStep;
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatStep * randomDouble(0.4, 1.0);

            int step = randomInt(-1, 1);
            if (step == 0) step = (randomDouble(0, 1) < 0.5) ? -1 : 1;
            currentIdx += step;
            currentIdx = (std::max)(0, (std::min)(static_cast<int>(pitches.size()) - 1, currentIdx));
            n.noteNumber = pitches[currentIdx];
            result.push_back(n);
        }
        break;
    }

    case Buildup:
    {
        // Density + pitch rises over time
        double beatStep = params.lengthBeats / static_cast<double>(numNotes);
        int pitchOffset = 0;
        for (int i = 0; i < numNotes; ++i)
        {
            GeneratedNote n;
            n.startBeat = static_cast<double>(i) * beatStep;
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            // Shorter as it builds
            double progress = static_cast<double>(i) / static_cast<double>(numNotes);
            n.durationBeats = beatStep * (1.0 - progress * 0.5);

            // Pitch rises
            double r = randomDouble(0.0, 1.0);
            int idx;
            if (r < 0.4)
                idx = (pitchOffset) % static_cast<int>(pitches.size());
            else if (r < 0.7)
                idx = (pitchOffset + 1) % static_cast<int>(pitches.size());
            else
                idx = randomInt(0, static_cast<int>(pitches.size()) - 1);

            n.noteNumber = pitches[idx];
            pitchOffset = (pitchOffset + (i % 2 == 0 ? 1 : 0)) % static_cast<int>(pitches.size());
            result.push_back(n);
        }
        break;
    }

    case Euclidean:
    {
        // Euclidean rhythm: k = density hits evenly spread over a 16th-note grid,
        // pitches walked with Markov motion through the in-range scale tones.
        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const int k = std::clamp(params.density, 1, totalSteps);
        const auto onsets = HDAW::euclideanSteps(k, totalSteps);
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);
        int idx = static_cast<int>(pitches.size()) / 2;
        for (size_t oi = 0; oi < onsets.size(); ++oi)
        {
            GeneratedNote n;
            n.startBeat = onsets[oi] * beatPerStep;
            n.noteNumber = pitches[std::clamp(idx, 0, static_cast<int>(pitches.size()) - 1)];
            n.velocity = (oi == 0) ? params.maxVelocity
                                   : randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = params.noteDuration > 0.0 ? params.noteDuration : beatPerStep * 0.9;
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);
            idx = HDAW::nextMarkovDegree(rng, idx, static_cast<int>(pitches.size()));
        }
        break;
    }

    case Percussion:
    {
        struct PercVoice { int pitch; int hits; int rotation; };
        std::vector<PercVoice> percVoices = {
            {36, 4, 0},
            {42, 4, 0},
            {38, 2, 0},
        };
        const int effLow = (std::min)(params.lowNote, 36);
        const int effHigh = (std::max)(params.highNote, 42);
        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);
        for (const auto& v : percVoices)
        {
            if (v.pitch < effLow || v.pitch > effHigh)
                continue;
            int k = std::clamp(v.hits, 1, totalSteps);
            auto onsets = HDAW::euclideanSteps(k, totalSteps, v.rotation);
            for (size_t oi = 0; oi < onsets.size(); ++oi)
            {
                GeneratedNote n;
                n.startBeat = onsets[oi] * beatPerStep;
                n.noteNumber = v.pitch;
                n.velocity = (oi == 0) ? params.maxVelocity
                                       : randomInt(params.minVelocity, params.maxVelocity);
                n.durationBeats = params.noteDuration > 0.0 ? params.noteDuration : beatPerStep * 0.5;
                result.push_back(n);
            }
        }
        break;
    }

    case TrapHiHat:
    {
        // 32nd-note grid with ratchet bursts
        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);
        const int rollDensity = (std::max)(1, params.trapHiHat.rollDensity);
        const double velocityDecay = std::clamp(params.trapHiHat.velocityDecay, 0.1, 1.0);
        const double ratchetChance = std::clamp(params.trapHiHat.ratchetChance, 0.0, 1.0);

        // Place regular hits based on density (every N steps)
        int hitInterval = (std::max)(1, totalSteps / rollDensity);
        for (int step = 0; step < totalSteps; step += hitInterval)
        {
            GeneratedNote n;
            n.startBeat = step * beatPerStep;
            n.noteNumber = pitches.empty() ? 42 : pitches[pitches.size() / 2];
            n.velocity = params.maxVelocity;
            n.durationBeats = beatPerStep * 0.5;
            result.push_back(n);

            // Optional ratchet burst (2-8 rapid repeats)
            if (randomDouble(0.0, 1.0) < ratchetChance)
            {
                int ratchetCount = randomInt(2, 8);
                double ratchetBeatLen = beatPerStep / static_cast<double>(ratchetCount + 1);
                for (int r = 1; r <= ratchetCount; ++r)
                {
                    GeneratedNote rn;
                    rn.startBeat = n.startBeat + r * ratchetBeatLen;
                    rn.noteNumber = n.noteNumber;
                    rn.velocity = static_cast<int>(params.maxVelocity * std::pow(velocityDecay, static_cast<double>(r)));
                    rn.velocity = (std::max)(params.minVelocity, rn.velocity);
                    rn.durationBeats = ratchetBeatLen * 0.5;
                    result.push_back(rn);
                }
            }
        }
        break;
    }

    case DrillBass:
    {
        // Displaced 808 patterns, root/fifth heavy, low register
        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);
        const double displacement = std::clamp(params.drillBass.displacement, 0.0, 1.0);
        const bool sustainTail = params.drillBass.sustainTail;

        // Find root and fifth in scale pitches
        int rootPitch = pitches.empty() ? params.lowNote : pitches[0];
        int fifthPitch = rootPitch + 7;
        // Clamp fifth to range
        if (fifthPitch > params.highNote)
            fifthPitch = rootPitch - 5;

        // Regular placement: every 4 steps with displacement offset
        int displacementSteps = static_cast<int>(std::lround(displacement * 4.0));
        for (int step = displacementSteps; step < totalSteps; step += 4)
        {
            GeneratedNote n;
            n.startBeat = step * beatPerStep;
            n.velocity = params.maxVelocity;
            n.durationBeats = sustainTail ? beatPerStep * 3.5 : beatPerStep * 1.5;

            // Pitch preference: root 45%, fifth 25%, random scale 30%
            double r = randomDouble(0.0, 1.0);
            if (r < 0.45)
                n.noteNumber = rootPitch;
            else if (r < 0.70)
                n.noteNumber = fifthPitch;
            else
                n.noteNumber = pitches[randomInt(0, static_cast<int>(pitches.size()) - 1)];

            // Force into low range
            while (n.noteNumber - 12 >= params.lowNote && n.noteNumber - 12 > 0)
                n.noteNumber -= 12;

            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);
        }
        break;
    }

    case WalkingBass:
    {
        // Stepwise root-5th approach-note bass on quarter-note grid
        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);
        const bool useApproach = params.walkingBass.approachNotes;
        const double ghostProb = std::clamp(params.walkingBass.ghostNotes, 0.0, 1.0);
        const double chromaticism = std::clamp(params.walkingBass.chromaticism, 0.0, 1.0);

        int rootPitch = pitches.empty() ? params.lowNote : pitches[0];
        int fifthPitch = rootPitch + 7;

        // Targets on downbeats (every 4 steps)
        std::vector<int> targets;
        for (int i = 0; i < totalSteps; i += 4)
        {
            double r = randomDouble(0.0, 1.0);
            int target = (r < 0.6) ? rootPitch : fifthPitch;
            // Clamp to range
            while (target > params.highNote) target -= 12;
            while (target < params.lowNote) target += 12;
            targets.push_back(target);
        }

        int pitchIdx = 0;
        for (int step = 0; step < totalSteps; ++step)
        {
            bool isDownbeat = (step % 4 == 0);
            int targetIdx = step / 4;

            if (isDownbeat)
            {
                // Downbeat: play the target
                GeneratedNote n;
                n.startBeat = step * beatPerStep;
                n.noteNumber = (targetIdx < static_cast<int>(targets.size())) ? targets[targetIdx] : rootPitch;
                n.velocity = params.maxVelocity;
                n.durationBeats = beatPerStep * 3.5;
                result.push_back(n);
                pitchIdx = n.noteNumber;
            }
            else if (useApproach)
            {
                // Approach note: step toward next downbeat target
                int nextTarget = ((targetIdx + 1) < static_cast<int>(targets.size())) ? targets[targetIdx + 1] : rootPitch;
                int diff = nextTarget - pitchIdx;
                int step_dir = (diff > 0) ? 1 : -1;

                // Chromatic approach or scalar approach
                int approach;
                if (randomDouble(0.0, 1.0) < chromaticism)
                    approach = pitchIdx + step_dir; // chromatic
                else
                {
                    // Scalar approach: find nearest scale pitch in direction
                    approach = pitchIdx + step_dir;
                    // Snap to nearest scale pitch
                    int bestDist = 999;
                    for (int p : pitches)
                    {
                        int dist = std::abs(p - approach);
                        if (dist < bestDist)
                        {
                            bestDist = dist;
                            approach = p;
                        }
                    }
                }

                GeneratedNote n;
                n.startBeat = step * beatPerStep;
                n.noteNumber = approach;
                n.velocity = params.maxVelocity - 15;
                n.durationBeats = beatPerStep * 0.8;
                pitchIdx = approach;
                result.push_back(n);
            }

            // Optional ghost note
            if (!isDownbeat && useApproach && randomDouble(0.0, 1.0) < ghostProb)
            {
                GeneratedNote gn;
                gn.startBeat = step * beatPerStep + beatPerStep * 0.5;
                gn.noteNumber = pitchIdx;
                gn.velocity = params.minVelocity;
                gn.durationBeats = beatPerStep * 0.3;
                if (gn.startBeat + gn.durationBeats <= params.lengthBeats)
                    result.push_back(gn);
            }
        }
        break;
    }

    case Counterpoint:
    {
        // Multi-voice species counterpoint
        const int voiceCount = std::clamp(params.counterpoint.voiceCount, 1, 4);
        const int species = std::clamp(params.counterpoint.species, 1, 5);
        const int intervalConstraint = std::clamp(params.counterpoint.intervalConstraint, 0, 3);

        // Species determines note density: 1=whole, 2=half, 3=quarter, 4=eighth, 5=mixed
        int notesPerBar;
        switch (species) {
            case 1: notesPerBar = 1; break;
            case 2: notesPerBar = 2; break;
            case 3: notesPerBar = 4; break;
            case 4: notesPerBar = 8; break;
            default: notesPerBar = 4; break;
        }

        const int totalNotes = static_cast<int>(std::lround(params.lengthBeats * notesPerBar / 4.0));
        const double beatPerNote = params.lengthBeats / static_cast<double>((std::max)(1, totalNotes));

        // Build each voice
        for (int voice = 0; voice < voiceCount; ++voice)
        {
            int octaveOffset = voice * 12; // voices offset by octaves
            int currentIdx = static_cast<int>(pitches.size()) / 2 + voice;

            for (int i = 0; i < totalNotes; ++i)
            {
                GeneratedNote n;
                n.startBeat = i * beatPerNote;

                // Species 4+ can have variable rhythm
                if (species >= 4 && randomDouble(0.0, 1.0) < 0.3)
                {
                    // Some notes are longer (half notes in species 4)
                    n.durationBeats = beatPerNote * 2.0;
                }
                else
                {
                    n.durationBeats = beatPerNote * 0.9;
                }

                n.velocity = randomInt(params.minVelocity, params.maxVelocity);

                // Motion: prefer stepwise, occasional leaps
                int step;
                double motionRoll = randomDouble(0.0, 1.0);
                if (motionRoll < 0.7)
                    step = randomInt(-1, 1); // stepwise
                else if (motionRoll < 0.9)
                    step = randomInt(-3, 3); // small leap
                else
                    step = randomInt(-5, 5); // large leap

                currentIdx += step;
                currentIdx = (std::max)(0, (std::min)(static_cast<int>(pitches.size()) - 1, currentIdx));

                // Interval constraint: keep voices within interval limits
                int pitch = pitches[currentIdx] + octaveOffset;
                pitch = (std::max)(params.lowNote, (std::min)(params.highNote, pitch));
                n.noteNumber = pitch;

                result.push_back(n);
            }
        }
        break;
    }

    case SwingComping:
    {
        // Syncopated chord stabs with swing feel
        const int swingPercent = std::clamp(params.swingComping.swingPercent, 50, 80);
        const int compPattern = std::clamp(params.swingComping.compPattern, 0, 3);
        const int voicingSpread = std::clamp(params.swingComping.voicingSpread, 0, 3);

        // Comp patterns: which 16th-note positions have stabs (in a 4-beat bar)
        // Pattern 0: Charleston (beat 1, & of 2)
        // Pattern 1: Shifted (e of 1, beat 3)
        // Pattern 2: Sparse (beat 1, beat 3)
        // Pattern 3: Dense (multiple syncopations)
        std::vector<int> stabPositions;
        switch (compPattern) {
            case 0: // Charleston
                stabPositions = {0, 6};
                break;
            case 1: // Shifted
                stabPositions = {2, 8};
                break;
            case 2: // Sparse
                stabPositions = {0, 8};
                break;
            case 3: // Dense
                stabPositions = {0, 3, 6, 10};
                break;
            default:
                stabPositions = {0, 6};
                break;
        }

        // Build chord: root + third + fifth
        int rootPitch = pitches.empty() ? 60 : pitches[0];
        int thirdPitch = rootPitch + 4; // major third
        int fifthPitch = rootPitch + 7; // fifth

        // Adjust voicing spread
        if (voicingSpread == 0) // close
        {
            // keep as is
        }
        else if (voicingSpread == 1) // medium
        {
            thirdPitch += 12;
        }
        else if (voicingSpread == 2) // open
        {
            thirdPitch += 12;
            fifthPitch += 12;
        }
        else // wide
        {
            rootPitch -= 12;
            thirdPitch += 12;
            fifthPitch += 12;
        }

        // Clamp to range
        auto clampPitch = [&](int p) {
            while (p < params.lowNote) p += 12;
            while (p > params.highNote) p -= 12;
            return p;
        };
        rootPitch = clampPitch(rootPitch);
        thirdPitch = clampPitch(thirdPitch);
        fifthPitch = clampPitch(fifthPitch);

        int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);

        for (int pos : stabPositions)
        {
            if (pos >= totalSteps) continue;

            // Apply swing: delay off-beat 16ths
            int actualTick = pos;
            if (pos % 2 == 1) // off-beat
            {
                double swingOffset = (swingPercent - 50.0) / 50.0 * beatPerStep * 2.0;
                actualTick = static_cast<int>(std::lround(pos + swingOffset / beatPerStep));
            }

            if (actualTick >= totalSteps) continue;

            // Chord stab: all 3 notes at the same position
            int chordNotes[] = {rootPitch, thirdPitch, fifthPitch};
            for (int cn : chordNotes)
            {
                GeneratedNote n;
                n.startBeat = actualTick * beatPerStep;
                n.noteNumber = cn;
                n.velocity = randomInt(params.minVelocity, params.maxVelocity);
                n.durationBeats = beatPerStep * 1.5;
                if (n.startBeat + n.durationBeats > params.lengthBeats)
                    n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
                result.push_back(n);
            }
        }
        break;
    }

    case MarkovMelody:
    {
        const int grid = std::clamp(params.markovMelody.rhythmGrid, 4, 32);
        const int states = std::clamp(params.markovMelody.stateCount, 3, 12);
        const double beatPerStep = params.lengthBeats / static_cast<double>(grid);
        const int totalSteps = grid;
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        int currentIdx = pitchCount / 2;
        for (int step = 0; step < totalSteps; ++step)
        {
            GeneratedNote n;
            n.startBeat = step * beatPerStep;
            n.noteNumber = pitches[std::clamp(currentIdx, 0, pitchCount - 1)];
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatPerStep * randomDouble(0.6, 1.0);
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);

            currentIdx = HDAW::nextMarkovDegree(rng, currentIdx, (std::min)(states, pitchCount));
        }
        break;
    }

    case EvolvingTexture:
    {
        const int layers = std::clamp(params.evolvingTexture.layerCount, 1, 8);
        const double drift = std::clamp(params.evolvingTexture.driftSpeed, 0.0, 1.0);
        const double swell = std::clamp(params.evolvingTexture.densitySwell, 0.0, 1.0);
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        for (int layer = 0; layer < layers; ++layer)
        {
            double startFraction = static_cast<double>(layer) / static_cast<double>(layers);
            double layerStart = startFraction * params.lengthBeats * (1.0 - swell * 0.5);

            int pitchIdx = randomInt(0, pitchCount - 1);
            double pos = layerStart;

            while (pos < params.lengthBeats)
            {
                double noteLen = randomDouble(2.0, 6.0);
                if (pos + noteLen > params.lengthBeats)
                    noteLen = params.lengthBeats - pos;
                if (noteLen < 0.1) break;

                GeneratedNote n;
                n.startBeat = pos;
                n.noteNumber = pitches[std::clamp(pitchIdx, 0, pitchCount - 1)];
                n.velocity = randomInt(params.minVelocity, params.maxVelocity - 20);
                n.durationBeats = noteLen;
                result.push_back(n);

                pos += noteLen * randomDouble(0.8, 1.2);
                if (randomDouble(0.0, 1.0) < drift)
                    pitchIdx += randomInt(-1, 1);
                pitchIdx = (std::max)(0, (std::min)(pitchCount - 1, pitchIdx));
            }
        }
        break;
    }

    case Aleatoric:
    {
        const double tightness = std::clamp(params.aleatoric.constraintTightness, 0.0, 1.0);
        const double rhythmVar = std::clamp(params.aleatoric.rhythmVariety, 0.0, 1.0);
        const double restProb = std::clamp(params.aleatoric.restProbability, 0.0, 1.0);
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);

        int centerIdx = pitchCount / 2;
        for (int step = 0; step < totalSteps; ++step)
        {
            if (randomDouble(0.0, 1.0) < restProb)
                continue;

            double r = randomDouble(0.0, 1.0);
            if (r > rhythmVar)
                continue;

            GeneratedNote n;
            n.startBeat = step * beatPerStep + randomDouble(-beatPerStep * 0.2, beatPerStep * 0.2);
            if (n.startBeat < 0.0) n.startBeat = 0.0;

            double pitchRoll = randomDouble(0.0, 1.0);
            int pitchIdx;
            if (pitchRoll < tightness)
            {
                pitchIdx = centerIdx + randomInt(-1, 1);
            }
            else
            {
                pitchIdx = randomInt(0, pitchCount - 1);
            }
            pitchIdx = (std::max)(0, (std::min)(pitchCount - 1, pitchIdx));
            n.noteNumber = pitches[pitchIdx];

            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatPerStep * randomDouble(0.3, 1.0);
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);
        }
        break;
    }

    case ScalarRun:
    {
        const int dir = std::clamp(params.scalarRun.direction, 0, 2);
        const int octaves = std::clamp(params.scalarRun.octaveSpan, 1, 4);
        const int speed = std::clamp(params.scalarRun.runSpeed, 4, 32);
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        const int totalSteps = speed;
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);
        int scaleRange = pitchCount * octaves;

        int pos = 0;
        int direction = (dir == 1) ? -1 : 1;
        for (int step = 0; step < totalSteps; ++step)
        {
            int pitchIdx = pos % pitchCount;
            GeneratedNote n;
            n.startBeat = step * beatPerStep;
            n.noteNumber = pitches[std::clamp(pitchIdx, 0, pitchCount - 1)];
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatPerStep * 0.9;
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);

            pos += direction;
            if (dir == 2)
            {
                if (pos >= scaleRange || pos < 0)
                {
                    direction = -direction;
                    pos += direction * 2;
                }
            }
            else
            {
                if (pos >= scaleRange)
                    pos = 0;
                if (pos < 0)
                    pos = scaleRange - 1;
            }
        }
        break;
    }

    case ChordToneSeq:
    {
        const int approachType = std::clamp(params.chordToneSeq.approachType, 0, 3);
        const int shape = std::clamp(params.chordToneSeq.patternShape, 0, 3);
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        int chordTones[] = { 0, 2, 4, 6 };
        int numChordTones = 4;

        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);

        for (int step = 0; step < totalSteps; ++step)
        {
            int seqIdx;
            switch (shape)
            {
                case 0: seqIdx = step % numChordTones; break;
                case 1: seqIdx = numChordTones - 1 - (step % numChordTones); break;
                case 2:
                {
                    int cycle = step % (numChordTones * 2 - 2);
                    seqIdx = cycle < numChordTones ? cycle : (numChordTones * 2 - 2 - cycle);
                    break;
                }
                default: seqIdx = randomInt(0, numChordTones - 1); break;
            }

            int baseIdx = chordTones[seqIdx];
            int pitchIdx = baseIdx % pitchCount;

            if (approachType > 0 && step > 0 && randomDouble(0.0, 1.0) < 0.35)
            {
                GeneratedNote approach;
                approach.startBeat = (step - 1) * beatPerStep + beatPerStep * 0.5;
                switch (approachType)
                {
                    case 1:
                        approach.noteNumber = pitches[std::clamp(pitchIdx - 1, 0, pitchCount - 1)];
                        break;
                    case 2:
                        approach.noteNumber = pitches[std::clamp(pitchIdx, 0, pitchCount - 1)] + 1;
                        break;
                    case 3:
                        approach.noteNumber = pitches[std::clamp(pitchIdx + ((randomDouble(0, 1) < 0.5) ? -1 : 1), 0, pitchCount - 1)];
                        break;
                    default:
                        approach.noteNumber = pitches[pitchIdx];
                        break;
                }
                approach.velocity = params.minVelocity + 10;
                approach.durationBeats = beatPerStep * 0.4;
                if (approach.startBeat + approach.durationBeats <= params.lengthBeats)
                    result.push_back(approach);
            }

            GeneratedNote n;
            n.startBeat = step * beatPerStep;
            n.noteNumber = pitches[pitchIdx];
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatPerStep * 0.8;
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);
        }
        break;
    }

    case CallResponse:
    {
        const int phraseLen = std::clamp(params.callResponse.phraseLength, 1, 8);
        const double variation = std::clamp(params.callResponse.responseVariation, 0.0, 1.0);
        const double rest = std::clamp(params.callResponse.restBeats, 0.0, 4.0);
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        const double callEnd = static_cast<double>(phraseLen);
        const double responseStart = callEnd + rest;

        auto generatePhrase = [&](double startTime, double length) -> std::vector<GeneratedNote>
        {
            std::vector<GeneratedNote> phrase;
            const int notesInPhrase = (std::max)(2, static_cast<int>(std::lround(length * 2.0)));
            const double noteLen = length / static_cast<double>(notesInPhrase);
            int idx = randomInt(pitchCount / 3, pitchCount * 2 / 3);

            for (int i = 0; i < notesInPhrase; ++i)
            {
                GeneratedNote n;
                n.startBeat = startTime + i * noteLen + randomDouble(-noteLen * 0.1, noteLen * 0.1);
                if (n.startBeat < startTime) n.startBeat = startTime;

                idx += randomInt(-1, 1);
                idx = (std::max)(0, (std::min)(pitchCount - 1, idx));
                n.noteNumber = pitches[idx];
                n.velocity = randomInt(params.minVelocity, params.maxVelocity);
                n.durationBeats = noteLen * randomDouble(0.6, 1.0);
                if (n.startBeat + n.durationBeats > startTime + length)
                    n.durationBeats = (std::max)(0.05, startTime + length - n.startBeat);
                phrase.push_back(n);
            }
            return phrase;
        };

        auto call = generatePhrase(0.0, callEnd);
        result.insert(result.end(), call.begin(), call.end());

        auto response = generatePhrase(responseStart, callEnd);
        if (variation > 0.0)
        {
            for (auto& n : response)
            {
                if (randomDouble(0.0, 1.0) < variation)
                {
                    int shift = randomInt(-2, 2);
                    int newIdx = 0;
                    for (int p = 0; p < pitchCount; ++p)
                    {
                        if (pitches[p] == n.noteNumber) { newIdx = p; break; }
                    }
                    newIdx += shift;
                    newIdx = (std::max)(0, (std::min)(pitchCount - 1, newIdx));
                    n.noteNumber = pitches[newIdx];
                }
            }
        }
        result.insert(result.end(), response.begin(), response.end());
        break;
    }

    case PhaseShift:
    {
        const int voice1Grid = std::clamp(params.phaseShift.voice1Grid, 2, 16);
        const int voice2Grid = std::clamp(params.phaseShift.voice2Grid, 2, 16);
        const double phaseRate = std::clamp(params.phaseShift.phaseRate, 0.0, 1.0);
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);

        // Voice 1: euclidean pattern on voice1Grid subdivision
        int k1 = std::clamp(numNotes / 2, 1, voice1Grid);
        auto onsets1 = HDAW::euclideanSteps(k1, voice1Grid);
        // Voice 2: euclidean pattern on voice2Grid subdivision
        int k2 = std::clamp(numNotes / 2, 1, voice2Grid);
        auto onsets2 = HDAW::euclideanSteps(k2, voice2Grid);

        // Place voice 1 hits (root register)
        for (int step : onsets1)
        {
            int gridStep = static_cast<int>(std::lround(static_cast<double>(step) * static_cast<double>(totalSteps) / static_cast<double>(voice1Grid)));
            if (gridStep >= totalSteps) continue;
            GeneratedNote n;
            n.startBeat = gridStep * beatPerStep;
            n.noteNumber = pitches[0]; // root
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatPerStep * 0.8;
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);
        }

        // Place voice 2 hits (fifth register, shifted by phaseRate)
        int phaseOffset = static_cast<int>(std::lround(phaseRate * static_cast<double>(totalSteps)));
        for (int step : onsets2)
        {
            int gridStep = static_cast<int>(std::lround(static_cast<double>(step) * static_cast<double>(totalSteps) / static_cast<double>(voice2Grid)));
            gridStep = (gridStep + phaseOffset) % totalSteps;
            GeneratedNote n;
            n.startBeat = gridStep * beatPerStep;
            // Fifth above root, clamped to range
            int fifthIdx = (pitchCount > 4) ? 4 : (pitchCount - 1);
            n.noteNumber = pitches[fifthIdx]; // fifth
            n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            n.durationBeats = beatPerStep * 0.8;
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);
        }
        break;
    }

    case AdditiveRhythm:
    {
        const int subdivision = std::clamp(params.additiveRhythm.subdivision, 4, 32);
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        const double beatPerStep = params.lengthBeats / static_cast<double>(subdivision);

        // Parse grouping string: "3+3+2" -> [3, 3, 2]
        std::vector<int> groups;
        {
            juce::StringArray tokens = juce::StringArray::fromTokens(
                juce::String(params.additiveRhythm.grouping), "+", "");
            for (const auto& token : tokens)
            {
                int val = token.getIntValue();
                if (val > 0)
                    groups.push_back(val);
            }
        }
        if (groups.empty())
            groups = { 3, 3, 2 }; // fallback

        // Repeat the grouping pattern to fill the phrase
        int pos = 0;
        int groupIdx = 0;
        int groupOffset = 0; // position within current group
        while (pos < subdivision)
        {
            int currentGroupSize = groups[groupIdx % static_cast<int>(groups.size())];
            bool isFirstInGroup = (groupOffset == 0);

            GeneratedNote n;
            n.startBeat = pos * beatPerStep;
            n.noteNumber = pitches[randomInt(0, pitchCount - 1)];
            // Accent notes on group boundaries (higher velocity)
            n.velocity = isFirstInGroup
                ? params.maxVelocity
                : randomInt(params.minVelocity, params.maxVelocity - 20);
            n.durationBeats = beatPerStep * (isFirstInGroup ? 0.9 : 0.6);
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);

            ++pos;
            ++groupOffset;
            if (groupOffset >= currentGroupSize)
            {
                groupOffset = 0;
                ++groupIdx;
            }
        }
        break;
    }

    case MinimalistLoop:
    {
        const int cellLength = std::clamp(params.minimalistLoop.cellLength, 3, 12);
        const double mutationRate = std::clamp(params.minimalistLoop.mutationRate, 0.0, 1.0);
        const int pitchCount = static_cast<int>(pitches.size());
        if (pitchCount == 0) break;

        // Generate initial cell (3-12 notes with random pitches and durations)
        std::vector<GeneratedNote> cell(cellLength);
        for (int i = 0; i < cellLength; ++i)
        {
            cell[i].noteNumber = pitches[randomInt(0, pitchCount - 1)];
            cell[i].velocity = randomInt(params.minVelocity, params.maxVelocity);
            cell[i].durationBeats = randomDouble(0.2, 1.0);
        }

        // Calculate how many steps fit in the phrase
        const int totalSteps = (std::max)(1, static_cast<int>(std::lround(params.lengthBeats * 4.0)));
        const double beatPerStep = params.lengthBeats / static_cast<double>(totalSteps);

        // Repeat the cell to fill the phrase, mutating on each repetition
        int step = 0;
        while (step < totalSteps)
        {
            int cellIdx = step % cellLength;
            GeneratedNote n = cell[cellIdx];

            // Mutate notes with probability mutationRate
            if (randomDouble(0.0, 1.0) < mutationRate)
            {
                // Mutate pitch: shift by ±1 or ±2 scale steps
                int pitchIdx = 0;
                for (int p = 0; p < pitchCount; ++p)
                {
                    if (pitches[p] == n.noteNumber) { pitchIdx = p; break; }
                }
                int shift = randomInt(-2, 2);
                if (shift == 0) shift = 1;
                pitchIdx = (std::max)(0, (std::min)(pitchCount - 1, pitchIdx + shift));
                n.noteNumber = pitches[pitchIdx];
                n.velocity = randomInt(params.minVelocity, params.maxVelocity);
            }

            n.startBeat = step * beatPerStep;
            if (n.startBeat + n.durationBeats > params.lengthBeats)
                n.durationBeats = (std::max)(0.05, params.lengthBeats - n.startBeat);
            result.push_back(n);

            ++step;
        }
        break;
    }

    case Layered:
        break; // deferred to future iteration

    default: // Standard
    {
        double beatStep = params.lengthBeats / static_cast<double>(numNotes);
        std::vector<double> positions;
        for (int i = 0; i < numNotes; ++i)
        {
            double beat = static_cast<double>(i) * beatStep;
            if (i % 2 == 1)
                beat += randomDouble(-beatStep * 0.15, beatStep * 0.15);
            else
                beat += randomDouble(-beatStep * 0.05, beatStep * 0.05);
            if (beat < params.lengthBeats)
                positions.push_back((std::max)(0.0, beat));
        }

        if (numNotes > 4)
        {
            std::vector<double> filtered;
            for (double pos : positions)
            {
                if (randomDouble(0.0, 1.0) > 0.15)
                    filtered.push_back(pos);
            }
            if (filtered.size() >= 2)
                positions = filtered;
        }

        for (double pos : positions)
        {
            GeneratedNote note;
            note.startBeat = pos;
            note.velocity = randomInt(params.minVelocity, params.maxVelocity);
            note.durationBeats = params.noteDuration * randomDouble(0.5, 1.5);

            double r = randomDouble(0.0, 1.0);
            int pitchIdx;
            if (r < 0.3 && pitches.size() > 0)
                pitchIdx = 0;
            else if (r < 0.45 && pitches.size() > 4)
                pitchIdx = 4;
            else if (r < 0.55 && pitches.size() > 2)
                pitchIdx = 2;
            else
                pitchIdx = randomInt(0, static_cast<int>(pitches.size()) - 1);

            note.noteNumber = pitches[pitchIdx % pitches.size()];
            if (randomDouble(0.0, 1.0) < 0.1 && note.noteNumber + 12 <= params.highNote)
                note.noteNumber += 12;

            result.push_back(note);
        }
        break;
    }
    }

    std::sort(result.begin(), result.end(),
        [](const GeneratedNote& a, const GeneratedNote& b) { return a.startBeat < b.startBeat; });
    return result;
}

// ── Style params schema ──

std::vector<PhraseGenerator::ParamField> PhraseGenerator::getStyleParamsSchema(Style style)
{
    switch (style) {
    case TrapHiHat:
        return {
            {"rollDensity", "integer", 2, 16, 4, "Roll Density"},
            {"velocityDecay", "number", 0.0, 1.0, 0.7, "Velocity Decay"},
            {"ratchetChance", "number", 0.0, 1.0, 0.3, "Ratchet Chance"}
        };
    case DrillBass:
        return {
            {"glideDuration", "number", 0.01, 1.0, 0.15, "Glide Duration"},
            {"slideIntensity", "number", 0.0, 1.0, 0.8, "Slide Intensity"},
            {"sustainTail", "boolean", 0, 1, 1, "Sustain Tail"},
            {"displacement", "number", 0.0, 1.0, 0.5, "Displacement"}
        };
    case Counterpoint:
        return {
            {"voiceCount", "integer", 1, 4, 2, "Voice Count"},
            {"species", "integer", 1, 5, 2, "Species"},
            {"intervalConstraint", "integer", 0, 3, 1, "Interval Constraint"}
        };
    case WalkingBass:
        return {
            {"approachNotes", "boolean", 0, 1, 1, "Approach Notes"},
            {"ghostNotes", "number", 0.0, 1.0, 0.1, "Ghost Notes"},
            {"chromaticism", "number", 0.0, 1.0, 0.3, "Chromaticism"}
        };
    case SwingComping:
        return {
            {"swingPercent", "integer", 50, 80, 65, "Swing Percent"},
            {"compPattern", "integer", 0, 4, 0, "Comp Pattern"},
            {"voicingSpread", "integer", 0, 3, 1, "Voicing Spread"}
        };
    case MarkovMelody:
        return {
            {"rhythmGrid", "integer", 4, 32, 16, "Rhythm Grid"},
            {"stateCount", "integer", 3, 12, 7, "State Count"}
        };
    case EvolvingTexture:
        return {
            {"layerCount", "integer", 1, 8, 4, "Layer Count"},
            {"driftSpeed", "number", 0.0, 1.0, 0.5, "Drift Speed"},
            {"densitySwell", "number", 0.0, 1.0, 0.5, "Density Swell"}
        };
    case Aleatoric:
        return {
            {"constraintTightness", "number", 0.0, 1.0, 0.5, "Constraint Tightness"},
            {"rhythmVariety", "number", 0.0, 1.0, 0.7, "Rhythm Variety"},
            {"restProbability", "number", 0.0, 1.0, 0.2, "Rest Probability"}
        };
    case ScalarRun:
        return {
            {"direction", "integer", 0, 2, 0, "Direction (0=up, 1=down, 2=both)"},
            {"octaveSpan", "integer", 1, 4, 2, "Octave Span"},
            {"runSpeed", "integer", 4, 32, 16, "Run Speed (grid)"}
        };
    case ChordToneSeq:
        return {
            {"approachType", "integer", 0, 3, 1, "Approach Type"},
            {"patternShape", "integer", 0, 3, 0, "Pattern Shape"}
        };
    case CallResponse:
        return {
            {"phraseLength", "integer", 1, 8, 4, "Phrase Length (beats)"},
            {"responseVariation", "number", 0.0, 1.0, 0.5, "Response Variation"},
            {"restBeats", "number", 0.0, 4.0, 1.0, "Rest Between Phrases"}
        };
    case PhaseShift:
        return {
            {"voice1Grid", "integer", 2, 16, 8, "Voice 1 Grid"},
            {"voice2Grid", "integer", 2, 16, 6, "Voice 2 Grid"},
            {"phaseRate", "number", 0.0, 1.0, 0.3, "Phase Rate"}
        };
    case AdditiveRhythm:
        return {
            {"grouping", "string", 0, 0, 0, "Grouping (e.g. 3+3+2)"},
            {"subdivision", "integer", 4, 32, 8, "Subdivision"}
        };
    case MinimalistLoop:
        return {
            {"cellLength", "integer", 2, 16, 6, "Cell Length"},
            {"mutationRate", "number", 0.0, 1.0, 0.2, "Mutation Rate"},
            {"phaseOffset", "integer", 0, 15, 0, "Phase Offset"}
        };
    default:
        return {};
    }
}

// ── Chord generation ──

std::vector<PhraseGenerator::GeneratedNote> PhraseGenerator::generateChord(
    int rootNote, const ChordParams& params)
{
    auto rng = makeRng(params.seed);
    ScopedRng scope(rng);

    const auto& types = getChordTypes();
    const ChordType* ct = nullptr;
    for (const auto& t : types)
    {
        if (t.index == params.chordType)
        {
            ct = &t;
            break;
        }
    }
    if (!ct) return {};

    // Build the chord note set
    std::vector<int> chordNotes;
    for (size_t i = 0; i < ct->intervals.size(); ++i)
    {
        // Handle inversion: rotate intervals
        size_t invIdx = (i + static_cast<size_t>(params.inversion)) % ct->intervals.size();
        int rotatedInterval;
        if (params.inversion > 0 && invIdx < i)
            rotatedInterval = ct->intervals[invIdx] + 12; // up an octave
        else
            rotatedInterval = ct->intervals[invIdx];

        int note = rootNote + rotatedInterval;
        chordNotes.push_back(note);
    }

    // Apply voicing
    if (params.voicing == 1) // open: spread notes across octaves
    {
        for (size_t i = 1; i < chordNotes.size(); ++i)
            if (i % 2 == 1)
                chordNotes[i] -= 12;
    }
    else if (params.voicing == 2) // spread: wide distribution
    {
        for (size_t i = 1; i < chordNotes.size(); ++i)
            chordNotes[i] += (static_cast<int>(i) - 1) * 12;
    }

    // Clamp to range
    for (auto& n : chordNotes)
    {
        if (n < params.lowNote)
        {
            while (n < params.lowNote) n += 12;
        }
        if (n > params.highNote)
        {
            while (n > params.highNote) n -= 12;
        }
    }

    std::vector<GeneratedNote> result;
    if (params.arpeggiate)
    {
        double beat = 0.0;
        for (int note : chordNotes)
        {
            GeneratedNote gn;
            gn.startBeat = beat;
            gn.noteNumber = note;
            gn.velocity = randomInt(params.minVelocity, params.maxVelocity);
            gn.durationBeats = params.arpeggioRate * 1.5;
            result.push_back(gn);
            beat += params.arpeggioRate;
        }
    }
    else
    {
        for (int note : chordNotes)
        {
            GeneratedNote gn;
            gn.startBeat = randomDouble(0.0, 0.02); // slight spread
            gn.noteNumber = note;
            gn.velocity = randomInt(params.minVelocity, params.maxVelocity);
            gn.durationBeats = params.durationBeats;
            result.push_back(gn);
        }
    }

    return result;
}

// ── Progression generation ──

std::vector<PhraseGenerator::GeneratedNote> PhraseGenerator::generateProgression(
    const ProgressionParams& params)
{
    const auto& patterns = getProgressionPatterns();
    const ProgressionPattern* pat = nullptr;
    for (const auto& p : patterns)
    {
        if (p.index == params.patternIndex)
        {
            pat = &p;
            break;
        }
    }
    if (!pat || pat->chords.empty()) return {};

    auto roots = diatonicRoots(params.scaleRoot, params.scaleMode, 4);
    if (roots.size() < 7) return {};

    auto rng = makeRng(params.seed);
    ScopedRng scope(rng);

    std::vector<GeneratedNote> result;
    double beatPos = 0.0;

    for (const auto& [degree, defaultChordType] : pat->chords)
    {
        if (degree < 0 || degree >= static_cast<int>(roots.size()))
        {
            beatPos += params.beatsPerChord;
            continue;
        }

        int root = roots[degree];
        int chordIdx = (params.chordTypeOverride >= 0) ? params.chordTypeOverride : defaultChordType;

        ChordParams cp;
        cp.scaleRoot = params.scaleRoot;
        cp.scaleMode = params.scaleMode;
        cp.lowNote = params.lowNote;
        cp.highNote = params.highNote;
        cp.minVelocity = params.minVelocity;
        cp.maxVelocity = params.maxVelocity;
        cp.chordType = chordIdx;
        cp.voicing = 0;
        cp.inversion = 0;
        cp.arpeggiate = params.arpeggiate;
        cp.arpeggioRate = params.arpeggioRate;
        cp.durationBeats = params.durationBeats;
        cp.seed = rng.nextU64();

        auto notes = generateChord(root, cp);

        // Offset all notes to the current beat position
        for (auto& n : notes)
        {
            n.startBeat += beatPos;
            result.push_back(n);
        }

        beatPos += params.beatsPerChord;
    }

    return result;
}
