# MidiAnalyzer Design Spec

## Goal

Build a C++ `MidiAnalyzer` class that reads existing MIDI files and extracts three products: a musical fingerprint (statistical summary), reusable bar-aligned patterns and sub-bar motifs, and PhraseGenerator-compatible style parameters. This enables a "learn from examples" pipeline — analyze a MIDI file, store the result as a PatternPreset, and regenerate similar music via the PhraseGenerator.

## Architecture

Single monolithic class: `MidiAnalyzer` in `src/engine/MidiAnalyzer.h/.cpp`.

```
MidiFile → MidiAnalyzer::analyze(path) → MidiAnalysisResult
                                              ├── fingerprint (stats)
                                              ├── patterns[]  (bar-aligned + motifs)
                                              └── styleParams (PhraseParams-compatible)
```

Uses JUCE's `MidiFile`/`MidiMessageSequence` for parsing (same approach as `MidiImport.cpp`). Integrates with existing `PhraseGenerator` style enums and `PatternLibrary` JSON format.

## Output Structures

### MidiFingerprint

```cpp
struct MidiFingerprint {
    // Rhythm
    double avgNoteDensity;         // notes per beat
    double rhythmComplexity;       // 0-1, how far from straight grid
    double syncopationScore;       // 0-1, off-beat emphasis
    double swingAmount;            // 0-1, timing shuffle

    // Pitch
    int pitchRange;                // semitones spanned
    int rootNote;                  // detected root (MIDI note)
    int scaleType;                 // 0=unknown, 1=major, 2=minor, etc.
    double chromaticism;           // 0-1, how many non-scale tones

    // Velocity
    double avgVelocity;            // 0-1 normalized
    double velocityRange;          // 0-1
    double velocityDynamicRange;   // std dev

    // Timing
    double quantizationStrength;   // 0-1, how close to grid
    double avgNoteDuration;        // beats

    // Structure
    double barCount;
    int voiceCount;
    double avgPolyphony;
};
```

### MidiPattern

```cpp
struct MidiPattern {
    juce::String name;
    int startBar;
    int lengthBars;
    int trackIndex;
    struct Note {
        double startBeat;    // relative to pattern start
        int pitch;
        int velocity;
        double durationBeats;
    };
    std::vector<Note> notes;
    double frequency;        // how often this motif repeats
    bool isMotif;            // true=sub-bar repeating, false=bar-aligned segment
};
```

### MidiAnalysisResult

```cpp
struct MidiAnalysisResult {
    MidiFingerprint fingerprint;
    std::vector<MidiPattern> patterns;

    // Style mapping
    int guessedStyle;        // PhraseGenerator::Style enum value
    float styleConfidence;   // 0-1
    juce::String paramsJson;      // PhraseParams as JSON
    juce::String styleParamsJson; // style-specific params as JSON

    // Metadata
    juce::String fileName;
    double sourceBpm;
    int timeSignatureNum;
    int timeSignatureDen;
    int trackCount;
};
```

## Analysis Pipeline

### Step 1: Parse

- `MidiFile::readFrom()` to parse the MIDI file
- Extract tempo from track 0 (default 120 BPM if absent)
- Extract time signature from track 0
- Convert all tick positions to beats using detected BPM and ticks-per-quarter-note

### Step 2: Per-Track Analysis

For each MIDI track with note data:

- **Note density**: count of notes / total beats
- **Velocity stats**: min, max, mean, std dev (normalized 0-1)
- **Pitch stats**: min, max, range, pitch histogram (12 bins)
- **Rhythm grid detection**: autocorrelation of note onset times to find dominant interval (quarter, eighth, sixteenth, triplet)
- **Syncopation**: ratio of notes falling off the detected grid, weighted by velocity accent
- **Swing estimation**: for eighth-note patterns, compare timing of first vs second eighth in each beat — avg offset reveals swing ratio
- **Quantization strength**: avg deviation of note onsets from nearest grid position, normalized to 0-1
- **Polyphony**: count of simultaneous notes at each time point, averaged

### Step 3: Scale Detection

- Build pitch histogram across all tracks (12 bins, weighted by duration and velocity)
- Match against the 13 scale modes defined in `PhraseGenerator.cpp` (Major through Chromatic)
- Use interval vector comparison: for each candidate scale, count how many played pitches fall on scale tones vs chromatic
- Best match = scale with highest on-scale ratio
- Root note = most frequent pitch class weighted by duration

### Step 4: Bar Segmentation

- Using detected time signature (default 4/4), compute beats-per-bar
- Split notes into bar-aligned windows
- For each bar: hash the note pattern (sorted pitch + onset positions)
- Deduplicate: identical bars share a pattern entry
- Store unique patterns with frequency count

### Step 5: Motif Extraction (Sub-Bar)

- Sliding window of 0.5 to 2 bar lengths (in beats)
- For each window position: hash the note sequence (relative pitches and timing, normalized to window length)
- Use a hash map to count occurrences of each hash
- Motifs that appear ≥ 2 times are extracted
- Merge overlapping motif candidates (keep longest, highest frequency)
- Name motifs by their dominant pitch class and rhythm character

### Step 6: Style Classification (Rule-Based)

Heuristic rules mapping fingerprint → PhraseGenerator style:

| Condition | Style | Confidence |
|-----------|-------|------------|
| avgNoteDensity > 8 AND pitchRange < 12 AND avgNoteDuration < 0.25 | TrapHiHat | 0.8 |
| avgNoteDensity > 6 AND pitchRange < 12 AND rhythmComplexity > 0.6 | DrillBass | 0.7 |
| avgNoteDensity < 3 AND pitchRange > 24 AND avgNoteDuration > 2.0 | Pad | 0.8 |
| avgNoteDensity < 4 AND avgPolyphony > 2.5 | ChordStab | 0.7 |
| pitchRange > 24 AND avgNoteDensity < 5 AND syncopationScore > 0.4 | Lead | 0.6 |
| walking motion (consecutive intervals ≤ 3 semitones) AND swingAmount > 0.3 | WalkingBass | 0.8 |
| swingAmount > 0.4 AND avgPolyphony > 1.5 | SwingComping | 0.7 |
| pitchRange < 7 AND avgNoteDensity > 4 | ScalarRun | 0.7 |
| polyrhythm detected (2+ dominant intervals) | AdditiveRhythm | 0.6 |
| low density + wide pitch + long duration + chromaticism > 0.3 | Aleatoric | 0.6 |
| high chromaticism + complex rhythm | Counterpoint | 0.6 |
| ... (fallthrough) | Standard | 0.4 |

### Step 7: Parameter Extraction

Map fingerprint to PhraseParams JSON:

- `scaleRoot` = detected rootNote
- `scaleMode` = detected scaleType (mapped to PhraseGenerator enum)
- `lowNote` = min pitch across all tracks
- `highNote` = max pitch across all tracks
- `minVelocity` = min velocity * 127
- `maxVelocity` = max velocity * 127
- `lengthBeats` = beats per bar (from time sig)
- `density` = notes per bar
- `noteDuration` = median note duration
- `seed` = 0 (non-deterministic by default)

Style-specific params derived from the relevant analysis:
- SwingComping: `swingPercent` = swingAmount * 100
- TrapHiHat: `rollSpeed` = avgNoteDensity / 4, `swing` = swingAmount
- DrillBass: `slideIntensity` = syncopationScore, `glideDuration` = avgNoteDuration * 0.3
- etc.

## Integration Points

### PatternLibrary

`MidiAnalysisResult.paramsJson` and `styleParamsJson` are directly compatible with `PatternPreset` fields. The analyzer output can be saved via:
```cpp
PatternPreset preset;
preset.name = "Analyzed: " + result.fileName;
preset.style = PhraseGenerator::getStyleName(result.guessedStyle);
preset.paramsJson = result.paramsJson;
preset.styleParamsJson = result.styleParamsJson;
// ... fill category, tags from fingerprint
patternLibrary.save(preset);
```

### PhraseGenerator

The extracted params can be loaded directly:
```cpp
auto params = PhraseGenerator::paramsFromJson(result.paramsJson);
auto notes = generator.generatePhrase(params);
```

### MCP / RPC

Expose as `composition.analyzeMidiFile(path)` RPC method returning the full result as JSON. This enables MCP tools for analysis.

## Files to Create/Modify

| File | Action |
|------|--------|
| `src/engine/MidiAnalyzer.h` | **New** — class + result structs |
| `src/engine/MidiAnalyzer.cpp` | **New** — implementation (~600-800 lines) |
| `CMakeLists.txt` | **Modify** — add MidiAnalyzer.cpp to sources |
| `src/engine/Router_Composition.cpp` | **Modify** — add RPC handler |
| `src/engine/AudioEngineCommands_Composition.cpp` | **Modify** — add command |
| `tests/unit/engine/midi_analyzer_test.cpp` | **New** — gtest suite |

## Dependencies

- JUCE `MidiFile`, `MidiMessage`, `MidiMessageSequence` (already used in MidiImport)
- `PhraseGenerator` style enum and param structs (read-only)
- `PatternLibrary` PatternPreset format (read-only)
- `Generative.h` utilities (optional, for hash functions)

## Testing Strategy

- Unit tests for each analysis step independently
- Test with known MIDI files (scales, drum patterns, chord progressions) where expected output is predictable
- Determinism test: same file → same result
- Edge cases: empty tracks, single note, no notes, extreme velocities, 3/4 vs 4/4 time signatures
- Style classification accuracy: test with files that clearly match each style
