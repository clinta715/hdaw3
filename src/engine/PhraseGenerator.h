#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <string>
#include <utility>

class PhraseGenerator
{
public:
    // ── Scale modes ──
    struct ScaleMode {
        int index;
        const char* name;
        std::vector<int> intervals;
    };
    static const std::vector<ScaleMode>& getScaleModes();
    static std::vector<int> buildScalePitches(int rootNote, int scaleModeIndex,
                                               int lowNote = 36, int highNote = 96);

    // ── Chord types ──
    struct ChordType {
        int index;
        const char* name;
        std::vector<int> intervals; // semitones from root
    };
    static const std::vector<ChordType>& getChordTypes();
    static const char* chordTypeName(int chordTypeIndex);

    // ── Progression patterns ──
    // Each element: (degree 0-6, chordTypeIndex)
    struct ProgressionPattern {
        int index;
        const char* name;
        std::vector<std::pair<int,int>> chords;
    };
    static const std::vector<ProgressionPattern>& getProgressionPatterns();

    // ── Preset phrase styles ──
    enum Style {
        Standard = 0,
        Arpeggio,
        BassLine,
        ChordStab,
        Pad,
        Lead,
        RandomWalk,
        Buildup
    };
    static const char* styleName(Style s);

    // ── Shared params ──
    struct BaseParams {
        int scaleRoot = 0;
        int scaleMode = 0;
        int lowNote = 48;
        int highNote = 84;
        int minVelocity = 60;
        int maxVelocity = 110;
    };

    struct PhraseParams : BaseParams {
        Style style = Standard;
        double lengthBeats = 4.0;
        int density = 8;
        double noteDuration = 0.5;
    };

    struct ChordParams : BaseParams {
        int chordType = 0;        // index into getChordTypes()
        int voicing = 0;          // 0=close, 1=open, 2=spread
        int inversion = 0;        // 0=root, 1=1st, 2=2nd, 3=3rd
        bool arpeggiate = false;
        double arpeggioRate = 0.125; // beat spacing between arp notes
        double durationBeats = 2.0;  // per-chord note length
    };

    struct ProgressionParams : BaseParams {
        int patternIndex = 0;       // index into getProgressionPatterns()
        int chordTypeOverride = -1; // -1 = use pattern's default per degree
        bool arpeggiate = false;
        double arpeggioRate = 0.125;
        double durationBeats = 2.0;
        double beatsPerChord = 4.0;
    };

    struct GeneratedNote {
        double startBeat;
        int noteNumber;
        int velocity;
        double durationBeats;
    };

    // ── Generation ──
    static std::vector<GeneratedNote> generatePhrase(const PhraseParams& params);
    static std::vector<GeneratedNote> generateChord(int rootNote, const ChordParams& params);
    static std::vector<GeneratedNote> generateProgression(const ProgressionParams& params);

    // ── Utility ──
    static const char* noteName(int noteNumber);
    static const char* modeName(int scaleModeIndex);

private:
    static int randomInt(int min, int max);
    static double randomDouble(double min, double max);
    static std::vector<int> diatonicRoots(int scaleRoot, int scaleModeIndex, int octave = 4);
};
