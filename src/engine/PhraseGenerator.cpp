#include "PhraseGenerator.h"
#include <random>
#include <algorithm>
#include <cmath>
#include <cstdlib>

static std::mt19937& rng()
{
    static std::mt19937 gen(std::random_device{}());
    return gen;
}

int PhraseGenerator::randomInt(int min, int max)
{
    if (min >= max) return min;
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng());
}

double PhraseGenerator::randomDouble(double min, double max)
{
    if (min >= max) return min;
    std::uniform_real_distribution<double> dist(min, max);
    return dist(rng());
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
        case Arpeggio:  return "Arpeggio";
        case BassLine:  return "Bass Line";
        case ChordStab: return "Chord Stab";
        case Pad:       return "Pad";
        case Lead:      return "Lead";
        case RandomWalk: return "Random Walk";
        case Buildup:   return "Buildup";
        default:        return "Standard";
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
        return {};

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

// ── Chord generation ──

std::vector<PhraseGenerator::GeneratedNote> PhraseGenerator::generateChord(
    int rootNote, const ChordParams& params)
{
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
