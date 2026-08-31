#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>
#include <string>
#include <utility>
#include "engine/Generative.h"

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

    // Map a scale DEGREE (diatonic step, octave-wrapped) to an absolute MIDI
    // pitch for a root note + scale mode. Degree 0 is the root; degree N
    // (N = pitch classes in the scale) wraps up an octave; negative degrees
    // wrap below the root. octave shifts the whole result by ±12·octave
    // (octave is the SCIENTIFIC octave of the note, matching diatonicRoots:
    // degree 0 at octave 4 is rootMidi=60/root+0 → C4-style placement;
    // see the MCP scale_note tool for the canonical semantics). Returns -1 if
    // the scale mode index is unknown or the computed pitch falls outside
    // 0..127 — callers must treat -1 as invalid (same contract as scale_note).
    static int scaleDegreeToPitch(int rootMidi, int scaleModeIndex, int degree, int octave = 0);

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
        Buildup,
        Euclidean,
        Percussion,
        TrapHiHat,
        DrillBass,
        Counterpoint,
        WalkingBass,
        SwingComping,
        MarkovMelody,
        EvolvingTexture,
        Aleatoric,
        ScalarRun,
        ChordToneSeq,
        CallResponse,
        PhaseShift,
        AdditiveRhythm,
        MinimalistLoop,
        Layered,
        NumStyles
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
        uint64_t seed = 0;   // 0 = non-deterministic (legacy); else reproducible
    };

    struct TrapHiHatParams {
        int rollDensity = 4;
        double velocityDecay = 0.7;
        double ratchetChance = 0.3;
    };

    struct DrillBassParams {
        double glideDuration = 0.15;
        double slideIntensity = 0.8;
        bool sustainTail = true;
        double displacement = 0.5;
    };

    struct CounterpointParams {
        int voiceCount = 2;
        int species = 2;
        int intervalConstraint = 1;
    };

    struct WalkingBassParams {
        bool approachNotes = true;
        double ghostNotes = 0.1;
        double chromaticism = 0.3;
    };

    struct SwingCompingParams {
        int swingPercent = 65;
        int compPattern = 0;
        int voicingSpread = 1;
    };

    struct MarkovMelodyParams {
        int rhythmGrid = 16;
        int stateCount = 7;
    };

    struct EvolvingTextureParams {
        int layerCount = 4;
        double driftSpeed = 0.5;
        double densitySwell = 0.5;
    };

    struct AleatoricParams {
        double constraintTightness = 0.5;
        double rhythmVariety = 0.7;
        double restProbability = 0.2;
    };

    struct ScalarRunParams {
        int direction = 0;
        int octaveSpan = 2;
        int runSpeed = 16;
    };

    struct ChordToneSeqParams {
        int approachType = 1;
        int patternShape = 0;
    };

    struct CallResponseParams {
        int phraseLength = 4;
        double responseVariation = 0.5;
        double restBeats = 1.0;
    };

    struct PhaseShiftParams {
        int voice1Grid = 8;
        int voice2Grid = 6;
        double phaseRate = 0.3;
    };

    struct AdditiveRhythmParams {
        std::string grouping = "3+3+2";
        int subdivision = 8;
    };

    struct MinimalistLoopParams {
        int cellLength = 6;
        double mutationRate = 0.2;
        int phaseOffset = 0;
    };

    struct PhraseParams : BaseParams {
        Style style = Standard;
        double lengthBeats = 4.0;
        int density = 8;
        double noteDuration = 0.5;

        TrapHiHatParams trapHiHat;
        DrillBassParams drillBass;
        CounterpointParams counterpoint;
        WalkingBassParams walkingBass;
        SwingCompingParams swingComping;
        MarkovMelodyParams markovMelody;
        EvolvingTextureParams evolvingTexture;
        AleatoricParams aleatoric;
        ScalarRunParams scalarRun;
        ChordToneSeqParams chordToneSeq;
        CallResponseParams callResponse;
        PhaseShiftParams phaseShift;
        AdditiveRhythmParams additiveRhythm;
        MinimalistLoopParams minimalistLoop;
    };

    struct ParamField {
        std::string name;
        std::string type;
        double min = 0;
        double max = 0;
        double defaultVal = 0;
        std::string label;
    };
    static std::vector<ParamField> getStyleParamsSchema(Style style);

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
    static HDAW::SplitMix64 makeRng(uint64_t seed);
    static int randomInt(int min, int max);
    static double randomDouble(double min, double max);
    static std::vector<int> diatonicRoots(int scaleRoot, int scaleModeIndex, int octave = 4);
};
