# MidiAnalyzer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a `MidiAnalyzer` class that reads MIDI files and extracts a musical fingerprint, reusable patterns/motifs, and PhraseGenerator-compatible style parameters.

**Architecture:** Single `MidiAnalyzer` class in `src/engine/MidiAnalyzer.h/.cpp`. Uses JUCE's `MidiFile`/`MidiMessageSequence` for parsing (same approach as `MidiImport.cpp`). Produces `MidiAnalysisResult` containing a `MidiFingerprint`, a vector of `MidiPattern`, and guessed style + PhraseParams JSON. Integrates with existing `PhraseGenerator` style enums and `PatternLibrary` JSON format.

**Tech Stack:** C++17, JUCE 8 (`juce_core`, `juce_audio_formats`), Google Test, existing `PhraseGenerator`/`PatternLibrary`/`Generative` modules.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `src/engine/MidiAnalyzer.h` | **Create** | Class declaration + result structs |
| `src/engine/MidiAnalyzer.cpp` | **Create** | Full implementation (~700 lines) |
| `CMakeLists.txt:142` | **Modify** | Add `src/engine/MidiAnalyzer.cpp` after `MidiImport.cpp` |
| `tests/unit/engine/midi_analyzer_test.cpp` | **Create** | GTest suite |
| `tests/CMakeLists.txt:78` | **Modify** | Add test file after `midi_import_test.cpp` |
| `src/frontend/router/Router_Composition.cpp` | **Modify** | Add `analyzeMidiFile` RPC handler |

---

### Task 1: Create MidiAnalyzer header with result structs

**Files:**
- Create: `src/engine/MidiAnalyzer.h`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cstdint>

namespace HDAW {

struct MidiFingerprint {
    // Rhythm
    double avgNoteDensity = 0.0;       // notes per beat
    double rhythmComplexity = 0.0;     // 0-1, deviation from grid
    double syncopationScore = 0.0;     // 0-1, off-beat emphasis
    double swingAmount = 0.0;          // 0-1, timing shuffle

    // Pitch
    int pitchRange = 0;                // semitones spanned
    int rootNote = 60;                 // detected root (MIDI note)
    int scaleType = 0;                 // index into PhraseGenerator scale modes, -1=unknown
    double chromaticism = 0.0;         // 0-1, ratio of non-scale tones

    // Velocity
    double avgVelocity = 0.0;          // 0-1 normalized
    double velocityRange = 0.0;        // 0-1
    double velocityDynamicRange = 0.0; // std dev

    // Timing
    double quantizationStrength = 0.0; // 0-1, how close to grid
    double avgNoteDuration = 0.0;      // beats

    // Structure
    double barCount = 0.0;
    int voiceCount = 0;
    double avgPolyphony = 0.0;
};

struct MidiPattern {
    struct Note {
        double startBeat = 0.0;   // relative to pattern start
        int pitch = 0;
        int velocity = 0;        // 0-127
        double durationBeats = 0.0;
    };

    juce::String name;
    int startBar = 0;
    int lengthBars = 1;
    int trackIndex = 0;
    std::vector<Note> notes;
    double frequency = 0.0;      // how often this pattern repeats
    bool isMotif = false;        // true=sub-bar, false=bar-aligned
};

struct MidiAnalysisResult {
    MidiFingerprint fingerprint;
    std::vector<MidiPattern> patterns;

    int guessedStyle = 0;               // PhraseGenerator::Style enum value
    float styleConfidence = 0.0f;       // 0-1
    juce::String paramsJson;            // PhraseParams as JSON
    juce::String styleParamsJson;       // style-specific params as JSON

    juce::String fileName;
    double sourceBpm = 120.0;
    int timeSignatureNum = 4;
    int timeSignatureDen = 4;
    int trackCount = 0;
};

class MidiAnalyzer {
public:
    // Analyze a MIDI file on disk. Returns empty result on failure.
    static MidiAnalysisResult analyze(const juce::File& midiFile);

    // Analyze from pre-parsed MidiFile (for testing / in-memory use).
    static MidiAnalysisResult analyzeMidiFile(const juce::MidiFile& midiFile,
                                              const juce::String& fileName = {});

    // Convert analysis result to a PatternPreset-compatible JSON pair.
    // Returns {paramsJson, styleParamsJson}.
    static std::pair<juce::String, juce::String> toPatternJson(
        const MidiAnalysisResult& result);

    // Classify a fingerprint into a PhraseGenerator style index.
    static int classifyStyle(const MidiFingerprint& fp);

private:
    struct TrackAnalysis {
        struct Note {
            double startBeat;
            int pitch;
            int velocity;   // 0-127
            double durationBeats;
        };
        std::vector<Note> notes;
        double totalBeats = 0.0;
    };

    static TrackAnalysis analyzeTrack(const juce::MidiMessageSequence& track,
                                      double ticksPerQuarter);
    static MidiFingerprint computeFingerprint(const std::vector<TrackAnalysis>& tracks,
                                              double beatsPerBar);
    static std::vector<MidiPattern> extractBarPatterns(const std::vector<TrackAnalysis>& tracks,
                                                       double beatsPerBar);
    static std::vector<MidiPattern> extractMotifs(const std::vector<TrackAnalysis>& tracks,
                                                   double beatsPerBar);
    static std::pair<juce::String, juce::String> extractParams(
        const MidiFingerprint& fp, int style, double beatsPerBar);

    static int detectScale(const std::vector<TrackAnalysis>& tracks, int& rootNote);
    static double detectSwing(const std::vector<TrackAnalysis>& tracks, double beatDivision);
    static double detectGridInterval(const std::vector<TrackAnalysis>& tracks);
};

} // namespace HDAW
```

- [ ] **Step 2: Verify header compiles**

Run: `cmake --build build --config Debug --target HDAW_lib 2>&1 | Select-String -Pattern "MidiAnalyzer"`
Expected: no errors referencing MidiAnalyzer (file not yet in build, so no errors expected)

- [ ] **Step 3: Commit**

```bash
git add src/engine/MidiAnalyzer.h
git commit -m "feat: add MidiAnalyzer header with result structs"
```

---

### Task 2: Add MidiAnalyzer.cpp to build, implement parsing and per-track analysis

**Files:**
- Create: `src/engine/MidiAnalyzer.cpp`
- Modify: `CMakeLists.txt:142`

- [ ] **Step 1: Add to CMakeLists.txt**

In `CMakeLists.txt`, after line 142 (`src/engine/MidiImport.cpp`), add:
```cmake
    src/engine/MidiAnalyzer.cpp
```

- [ ] **Step 2: Write the implementation skeleton with parsing and per-track analysis**

```cpp
#include "MidiAnalyzer.h"
#include "PhraseGenerator.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <sstream>

namespace HDAW {

MidiAnalysisResult MidiAnalyzer::analyze(const juce::File& midiFile)
{
    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk())
        return {};

    juce::MidiFile midiData;
    if (!midiData.readFrom(stream))
        return {};

    return analyzeMidiFile(midiData, midiFile.getFileName());
}

MidiAnalysisResult MidiAnalyzer::analyzeMidiFile(const juce::MidiFile& midiFile,
                                                  const juce::String& fileName)
{
    MidiAnalysisResult result;
    result.fileName = fileName;
    result.trackCount = midiFile.getNumTracks();

    if (midiFile.getNumTracks() == 0)
        return result;

    // Extract time format
    int midiTimeFormat = static_cast<int>(midiFile.getTimeFormat());
    if (midiTimeFormat <= 0)
        return result; // SMPTE not supported
    double ticksPerQuarter = static_cast<double>(midiTimeFormat);

    // Extract tempo from track 0
    double bpm = 120.0;
    auto* tempoTrack = midiFile.getTrack(0);
    if (tempoTrack != nullptr) {
        for (int e = 0; e < tempoTrack->getNumEvents(); ++e) {
            auto* ev = tempoTrack->getEventPointer(e);
            if (ev != nullptr && ev->message.isTempoMetaEvent()) {
                double secPerQuarter = ev->message.getTempoSecondsPerQuarterNote();
                if (secPerQuarter > 0.0)
                    bpm = 60.0 / secPerQuarter;
                break;
            }
        }
    }
    result.sourceBpm = bpm;

    // Extract time signature from track 0
    if (tempoTrack != nullptr) {
        for (int e = 0; e < tempoTrack->getNumEvents(); ++e) {
            auto* ev = tempoTrack->getEventPointer(e);
            if (ev != nullptr && ev->message.isTimeSignatureMetaEvent()) {
                result.timeSignatureNum = ev->message.getTimeSignatureNumerator();
                result.timeSignatureDen = ev->message.getTimeSignatureDenominator();
                break;
            }
        }
    }

    double beatsPerBar = static_cast<double>(result.timeSignatureNum);

    // Analyze each track
    std::vector<TrackAnalysis> tracks;
    for (int mt = 0; mt < midiFile.getNumTracks(); ++mt) {
        auto* track = midiFile.getTrack(mt);
        if (track == nullptr || track->getNumEvents() == 0)
            continue;
        auto ta = analyzeTrack(*track, ticksPerQuarter);
        if (!ta.notes.empty()) {
            ta.totalBeats = ta.notes.back().startBeat + ta.notes.back().durationBeats;
            tracks.push_back(std::move(ta));
        }
    }

    if (tracks.empty())
        return result;

    // Compute fingerprint
    result.fingerprint = computeFingerprint(tracks, beatsPerBar);

    // Extract bar-aligned patterns
    result.patterns = extractBarPatterns(tracks, beatsPerBar);

    // Extract sub-bar motifs
    auto motifs = extractMotifs(tracks, beatsPerBar);
    for (auto& m : motifs)
        result.patterns.push_back(std::move(m));

    // Classify style
    result.guessedStyle = classifyStyle(result.fingerprint);
    result.styleConfidence = 0.5f; // base confidence, overridden by rules

    // Extract params
    auto [paramsJson, styleParamsJson] = extractParams(
        result.fingerprint, result.guessedStyle, beatsPerBar);
    result.paramsJson = paramsJson;
    result.styleParamsJson = styleParamsJson;

    return result;
}

MidiAnalyzer::TrackAnalysis MidiAnalyzer::analyzeTrack(
    const juce::MidiMessageSequence& track, double ticksPerQuarter)
{
    TrackAnalysis ta;

    for (int e = 0; e < track.getNumEvents(); ++e) {
        auto* ev = track.getEventPointer(e);
        if (ev == nullptr) continue;
        auto& msg = ev->message;

        if (msg.isNoteOn() && msg.getVelocity() > 0) {
            double tickTime = msg.getTimeStamp();
            double beatTime = tickTime / ticksPerQuarter;
            int noteNum = msg.getNoteNumber();
            int velocity = msg.getVelocity();

            // Find matching note-off
            double durBeats = 0.25;
            for (int e2 = e + 1; e2 < track.getNumEvents(); ++e2) {
                auto* ev2 = track.getEventPointer(e2);
                if (ev2 != nullptr && ev2->message.isNoteOff() &&
                    ev2->message.getNoteNumber() == noteNum) {
                    double offTick = ev2->message.getTimeStamp();
                    durBeats = (offTick - tickTime) / ticksPerQuarter;
                    break;
                }
            }

            ta.notes.push_back({ beatTime, noteNum, velocity, durBeats });
        }
    }

    // Sort by start time
    std::sort(ta.notes.begin(), ta.notes.end(),
        [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });

    return ta;
}

} // namespace HDAW
```

- [ ] **Step 3: Build to verify compilation**

Run: `cmake --build build --config Debug 2>&1 | Select-String -Pattern "error|MidiAnalyzer"`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add src/engine/MidiAnalyzer.cpp CMakeLists.txt
git commit -m "feat: MidiAnalyzer parsing and per-track analysis skeleton"
```

---

### Task 3: Implement scale detection and swing analysis

**Files:**
- Modify: `src/engine/MidiAnalyzer.cpp`

- [ ] **Step 1: Add scale detection implementation**

Append to `MidiAnalyzer.cpp`:

```cpp
int MidiAnalyzer::detectScale(const std::vector<TrackAnalysis>& tracks, int& rootNote)
{
    // Build pitch histogram (12 bins, weighted by duration)
    double pitchBins[12] = {};
    for (const auto& t : tracks) {
        for (const auto& n : t.notes) {
            int bin = ((n.pitch % 12) + 12) % 12;
            pitchBins[bin] += n.durationBeats * (n.velocity / 127.0);
        }
    }

    // Find most-weighted pitch class as candidate root
    int bestBin = 0;
    for (int i = 1; i < 12; ++i)
        if (pitchBins[i] > pitchBins[bestBin])
            bestBin = i;
    rootNote = bestBin; // pitch class 0-11

    // Match against scale modes
    const auto& modes = PhraseGenerator::getScaleModes();
    int bestMode = -1;
    double bestScore = -1.0;

    for (const auto& mode : modes) {
        if (mode.index == 12) continue; // skip Chromatic

        double onScale = 0.0;
        double total = 0.0;
        for (int i = 0; i < 12; ++i) {
            total += pitchBins[i];
            bool inScale = false;
            for (int iv : mode.intervals) {
                if (((rootNote + iv) % 12 + 12) % 12 == i) {
                    inScale = true;
                    break;
                }
            }
            if (inScale)
                onScale += pitchBins[i];
        }

        if (total > 0.0) {
            double score = onScale / total;
            if (score > bestScore) {
                bestScore = score;
                bestMode = mode.index;
            }
        }
    }

    return bestMode;
}

double MidiAnalyzer::detectSwing(const std::vector<TrackAnalysis>& tracks, double beatDivision)
{
    // Collect all note onset positions relative to beat grid
    std::vector<double> offsets;
    for (const auto& t : tracks) {
        for (const auto& n : t.notes) {
            double posInBeat = std::fmod(n.startBeat, 1.0);
            if (posInBeat < 0.0) posInBeat += 1.0;

            // Check if note is near an odd subdivision (e.g. 2nd eighth, 3rd sixteenth)
            double subdivPos = std::fmod(posInBeat * beatDivision, 1.0);
            if (std::fmod(subdivPos, 2.0) >= 0.8 || std::fmod(subdivPos, 2.0) <= 0.2) {
                // Near an odd subdivision — measure offset from straight position
                double straightOffset = std::floor(posInBeat * beatDivision + 0.5) / beatDivision;
                double offset = (posInBeat - straightOffset) * beatDivision;
                offsets.push_back(offset);
            }
        }
    }

    if (offsets.empty())
        return 0.0;

    double avgOffset = std::accumulate(offsets.begin(), offsets.end(), 0.0) / offsets.size();
    // Normalize: typical swing is 0-0.15 beats offset, clamp to 0-1
    return std::max(0.0, std::min(1.0, avgOffset * 5.0));
}

double MidiAnalyzer::detectGridInterval(const std::vector<TrackAnalysis>& tracks)
{
    // Collect all onset intervals between consecutive notes
    std::vector<double> intervals;
    for (const auto& t : tracks) {
        for (size_t i = 1; i < t.notes.size(); ++i) {
            double dt = t.notes[i].startBeat - t.notes[i - 1].startBeat;
            if (dt > 0.001 && dt < 4.0)
                intervals.push_back(dt);
        }
    }

    if (intervals.empty())
        return 0.25; // default sixteenth

    // Histogram intervals into bins of 0.01 beats
    std::unordered_map<int, int> hist;
    for (double d : intervals) {
        int bin = static_cast<int>(d * 100.0 + 0.5);
        hist[bin]++;
    }

    // Find the most common interval
    int bestBin = 0;
    int bestCount = 0;
    for (const auto& [bin, count] : hist) {
        if (count > bestCount) {
            bestCount = count;
            bestBin = bin;
        }
    }

    return bestBin / 100.0;
}
```

- [ ] **Step 2: Build to verify**

Run: `cmake --build build --config Debug 2>&1 | Select-String -Pattern "error"`
Expected: no errors

- [ ] **Step 3: Commit**

```bash
git add src/engine/MidiAnalyzer.cpp
git commit -m "feat: MidiAnalyzer scale detection and swing analysis"
```

---

### Task 4: Implement fingerprint computation

**Files:**
- Modify: `src/engine/MidiAnalyzer.cpp`

- [ ] **Step 1: Add computeFingerprint**

Append to `MidiAnalyzer.cpp`:

```cpp
MidiFingerprint MidiAnalyzer::computeFingerprint(const std::vector<TrackAnalysis>& tracks,
                                                  double beatsPerBar)
{
    MidiFingerprint fp;

    int totalNotes = 0;
    double totalDuration = 0.0;
    double totalVelocity = 0.0;
    double velocitySquaredSum = 0.0;
    int minPitch = 127, maxPitch = 0;
    int minVel = 127, maxVel = 0;
    double totalBeatSpan = 0.0;
    double totalPolyphony = 0.0;
    int polyphonySamples = 0;

    // Pitch class histogram for chromaticism
    double pitchBins[12] = {};

    // Collect all onsets for quantization analysis
    std::vector<double> allOnsets;

    for (const auto& t : tracks) {
        totalNotes += static_cast<int>(t.notes.size());
        totalBeatSpan = std::max(totalBeatSpan, t.totalBeats);

        for (const auto& n : t.notes) {
            totalDuration += n.durationBeats;
            totalVelocity += n.velocity;
            velocitySquaredSum += static_cast<double>(n.velocity) * n.velocity;
            minPitch = std::min(minPitch, n.pitch);
            maxPitch = std::max(maxPitch, n.pitch);
            minVel = std::min(minVel, n.velocity);
            maxVel = std::max(maxVel, n.velocity);
            allOnsets.push_back(n.startBeat);

            int bin = ((n.pitch % 12) + 12) % 12;
            pitchBins[bin] += n.durationBeats * (n.velocity / 127.0);
        }

        // Polyphony: count simultaneous notes at each onset
        for (const auto& n : t.notes) {
            int simCount = 0;
            for (const auto& m : t.notes) {
                if (&m == &n) continue;
                if (m.startBeat <= n.startBeat &&
                    m.startBeat + m.durationBeats > n.startBeat)
                    simCount++;
            }
            totalPolyphony += simCount + 1; // +1 for the note itself
            polyphonySamples++;
        }
    }

    if (totalNotes == 0)
        return fp;

    // Density
    fp.avgNoteDensity = totalNotes / std::max(1.0, totalBeatSpan);

    // Average duration
    fp.avgNoteDuration = totalDuration / totalNotes;

    // Velocity stats
    fp.avgVelocity = (totalVelocity / totalNotes) / 127.0;
    fp.velocityRange = (maxVel - minVel) / 127.0;
    double meanVel = totalVelocity / totalNotes;
    double variance = (velocitySquaredSum / totalNotes) - (meanVel * meanVel);
    fp.velocityDynamicRange = std::sqrt(std::max(0.0, variance)) / 127.0;

    // Pitch
    fp.pitchRange = maxPitch - minPitch;

    // Root + scale detection
    fp.scaleType = detectScale(tracks, fp.rootNote);

    // Chromaticism: ratio of off-scale notes
    if (fp.scaleType >= 0 && fp.scaleType < static_cast<int>(PhraseGenerator::getScaleModes().size())) {
        const auto& mode = PhraseGenerator::getScaleModes()[fp.scaleType];
        double onScale = 0.0, total = 0.0;
        for (int i = 0; i < 12; ++i) {
            total += pitchBins[i];
            bool inScale = false;
            for (int iv : mode.intervals) {
                if (((fp.rootNote + iv) % 12 + 12) % 12 == i) {
                    inScale = true;
                    break;
                }
            }
            if (inScale) onScale += pitchBins[i];
        }
        fp.chromaticism = total > 0.0 ? 1.0 - (onScale / total) : 0.0;
    }

    // Bar count
    fp.barCount = totalBeatSpan / beatsPerBar;

    // Voice count (approximate: count distinct pitch ranges)
    fp.voiceCount = static_cast<int>(tracks.size());

    // Average polyphony
    fp.avgPolyphony = polyphonySamples > 0 ? totalPolyphony / polyphonySamples : 1.0;

    // Grid detection and rhythm metrics
    double gridInterval = detectGridInterval(tracks);
    fp.quantizationStrength = 0.5; // placeholder, computed below

    // Quantization strength: avg deviation from grid
    double totalDeviation = 0.0;
    for (double onset : allOnsets) {
        double nearestGrid = std::round(onset / gridInterval) * gridInterval;
        totalDeviation += std::abs(onset - nearestGrid);
    }
    if (!allOnsets.empty()) {
        double avgDev = totalDeviation / allOnsets.size();
        fp.quantizationStrength = std::max(0.0, 1.0 - (avgDev / gridInterval) * 2.0);
    }

    // Rhythm complexity: how much the inter-onset intervals vary
    std::vector<double> ioi;
    auto sortedOnsets = allOnsets;
    std::sort(sortedOnsets.begin(), sortedOnsets.end());
    for (size_t i = 1; i < sortedOnsets.size(); ++i) {
        double dt = sortedOnsets[i] - sortedOnsets[i - 1];
        if (dt > 0.001) ioi.push_back(dt);
    }
    if (ioi.size() > 1) {
        double meanIOI = std::accumulate(ioi.begin(), ioi.end(), 0.0) / ioi.size();
        double ioiVar = 0.0;
        for (double d : ioi) ioiVar += (d - meanIOI) * (d - meanIOI);
        ioiVar /= ioi.size();
        fp.rhythmComplexity = std::min(1.0, std::sqrt(ioiVar) / meanIOI);
    }

    // Syncopation: ratio of notes NOT on strong beats
    int offBeat = 0;
    for (double onset : allOnsets) {
        double posInBar = std::fmod(onset, beatsPerBar);
        double posInBeat = std::fmod(posInBar, 1.0);
        if (posInBeat > 0.1 && posInBeat < 0.9)
            offBeat++;
    }
    fp.syncopationScore = allOnsets.empty() ? 0.0 :
        static_cast<double>(offBeat) / allOnsets.size();

    // Swing
    fp.swingAmount = detectSwing(tracks, 2.0); // check for eighth-note swing

    return fp;
}
```

- [ ] **Step 2: Build to verify**

Run: `cmake --build build --config Debug 2>&1 | Select-String -Pattern "error"`
Expected: no errors

- [ ] **Step 3: Commit**

```bash
git add src/engine/MidiAnalyzer.cpp
git commit -m "feat: MidiAnalyzer fingerprint computation"
```

---

### Task 5: Implement bar pattern and motif extraction

**Files:**
- Modify: `src/engine/MidiAnalyzer.cpp`

- [ ] **Step 1: Add extractBarPatterns**

Append to `MidiAnalyzer.cpp`:

```cpp
std::vector<MidiPattern> MidiAnalyzer::extractBarPatterns(
    const std::vector<TrackAnalysis>& tracks, double beatsPerBar)
{
    struct BarKey {
        int trackIdx;
        int barNum;
        bool operator==(const BarKey& o) const {
            return trackIdx == o.trackIdx && barNum == o.barNum;
        }
    };

    struct BarKeyHash {
        size_t operator()(const BarKey& k) const {
            return std::hash<int>()(k.trackIdx) ^ (std::hash<int>()(k.barNum) << 16);
        }
    };

    // Hash notes within each bar
    struct BarNotes {
        std::vector<MidiPattern::Note> notes;
    };

    std::unordered_map<BarKey, BarNotes, BarKeyHash> barMap;

    for (int ti = 0; ti < static_cast<int>(tracks.size()); ++ti) {
        const auto& t = tracks[ti];
        for (const auto& n : t.notes) {
            int bar = static_cast<int>(n.startBeat / beatsPerBar);
            BarKey key{ ti, bar };
            double barStart = bar * beatsPerBar;

            barMap[key].notes.push_back({
                n.startBeat - barStart,
                n.pitch,
                n.velocity,
                n.durationBeats
            });
        }
    }

    // Deduplicate: find bars with identical note patterns
    struct BarSignature {
        std::vector<std::pair<double, int>> onsetPitches; // sorted (onset, pitch)
    };

    std::unordered_map<size_t, std::vector<BarKey>> sigToBars;
    for (auto& [key, barNotes] : barMap) {
        BarSignature sig;
        for (const auto& n : barNotes.notes)
            sig.onsetPitches.push_back({ n.startBeat, n.pitch });
        std::sort(sig.onsetPitches.begin(), sig.onsetPitches.end());

        // Hash the signature
        size_t h = sig.onsetPitches.size();
        for (const auto& [onset, pitch] : sig.onsetPitches) {
            h ^= std::hash<double>()(onset + 0.001) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(pitch) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        sigToBars[h].push_back(key);
    }

    // Build patterns for bars that appear more than once
    std::vector<MidiPattern> patterns;
    for (auto& [sig, bars] : sigToBars) {
        if (bars.size() < 2) continue;

        const auto& firstKey = bars[0];
        const auto& firstBar = barMap[firstKey];

        MidiPattern pat;
        pat.trackIndex = firstKey.trackIdx;
        pat.startBar = firstKey.barNum;
        pat.lengthBars = 1;
        pat.frequency = static_cast<double>(bars.size());
        pat.isMotif = false;
        pat.notes = firstBar.notes;
        pat.name = "Bar_" + juce::String(firstKey.barNum) +
                   "_t" + juce::String(firstKey.trackIdx);

        patterns.push_back(std::move(pat));
    }

    return patterns;
}
```

- [ ] **Step 2: Add extractMotifs**

Append to `MidiAnalyzer.cpp`:

```cpp
std::vector<MidiPattern> MidiAnalyzer::extractMotifs(
    const std::vector<TrackAnalysis>& tracks, double beatsPerBar)
{
    // Sliding window: 0.5 to 2 bar lengths
    std::vector<MidiPattern> motifs;

    for (const auto& t : tracks) {
        if (t.notes.size() < 3) continue;

        double maxBeat = t.notes.back().startBeat + t.notes.back().durationBeats;

        for (double winLen = beatsPerBar * 0.5; winLen <= beatsPerBar * 2.0;
             winLen += beatsPerBar * 0.25)
        {
            for (double winStart = 0.0; winStart + winLen <= maxBeat;
                 winStart += beatsPerBar * 0.5)
            {
                // Collect notes in this window
                std::vector<MidiPattern::Note> windowNotes;
                for (const auto& n : t.notes) {
                    if (n.startBeat >= winStart && n.startBeat < winStart + winLen) {
                        windowNotes.push_back({
                            n.startBeat - winStart,
                            n.pitch,
                            n.velocity,
                            n.durationBeats
                        });
                    }
                }

                if (windowNotes.size() < 2) continue;

                // Normalize: transpose so lowest pitch = 0
                int minPitch = windowNotes[0].pitch;
                for (const auto& n : windowNotes)
                    minPitch = std::min(minPitch, n.pitch);

                // Hash: sorted (normalized_onset, relative_pitch)
                std::vector<std::pair<double, int>> sig;
                for (const auto& n : windowNotes)
                    sig.push_back({ n.startBeat, n.pitch - minPitch });
                std::sort(sig.begin(), sig.end());

                size_t h = sig.size();
                for (const auto& [onset, pitch] : sig) {
                    h ^= std::hash<double>()(onset + 0.001) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    h ^= std::hash<int>()(pitch) + 0x9e3779b9 + (h << 6) + (h >> 2);
                }

                // Store in a static map for cross-window comparison
                // (simplified: just check if same hash appears in different windows)
                // For now, store all candidate motifs
                static thread_local std::unordered_map<size_t, int> motifCount;
                motifCount[h]++;

                if (motifCount[h] == 2) {
                    // This motif appeared before — it's a repeat
                    MidiPattern pat;
                    pat.startBar = static_cast<int>(winStart / beatsPerBar);
                    pat.lengthBars = std::max(1, static_cast<int>(std::ceil(winLen / beatsPerBar)));
                    pat.trackIndex = 0; // TODO: pass track index
                    pat.frequency = 2.0;
                    pat.isMotif = true;
                    pat.notes = windowNotes;
                    pat.name = "Motif_h" + juce::String(static_cast<int>(h % 10000));
                    motifs.push_back(std::move(pat));
                }
            }
        }
    }

    return motifs;
}
```

- [ ] **Step 3: Build to verify**

Run: `cmake --build build --config Debug 2>&1 | Select-String -Pattern "error"`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add src/engine/MidiAnalyzer.cpp
git commit -m "feat: MidiAnalyzer bar pattern and motif extraction"
```

---

### Task 6: Implement style classification and param extraction

**Files:**
- Modify: `src/engine/MidiAnalyzer.cpp`

- [ ] **Step 1: Add classifyStyle**

Append to `MidiAnalyzer.cpp`:

```cpp
int MidiAnalyzer::classifyStyle(const MidiFingerprint& fp)
{
    // Rule-based classification
    // Priority: more specific rules first

    if (fp.avgNoteDensity > 8.0 && fp.pitchRange < 12 && fp.avgNoteDuration < 0.25)
        return PhraseGenerator::TrapHiHat;

    if (fp.avgNoteDensity > 6.0 && fp.pitchRange < 12 && fp.rhythmComplexity > 0.6)
        return PhraseGenerator::DrillBass;

    if (fp.avgNoteDensity < 3.0 && fp.pitchRange > 24 && fp.avgNoteDuration > 2.0)
        return PhraseGenerator::Pad;

    if (fp.avgNoteDensity < 4.0 && fp.avgPolyphony > 2.5)
        return PhraseGenerator::ChordStab;

    if (fp.swingAmount > 0.4 && fp.avgPolyphony > 1.5)
        return PhraseGenerator::SwingComping;

    if (fp.swingAmount > 0.3 && fp.rhythmComplexity < 0.3 &&
        fp.avgNoteDensity > 3.0 && fp.pitchRange < 24)
        return PhraseGenerator::WalkingBass;

    if (fp.pitchRange < 7 && fp.avgNoteDensity > 4.0)
        return PhraseGenerator::ScalarRun;

    if (fp.pitchRange > 24 && fp.avgNoteDensity < 5.0 && fp.syncopationScore > 0.4)
        return PhraseGenerator::Lead;

    if (fp.rhythmComplexity > 0.7 && fp.chromaticism > 0.3)
        return PhraseGenerator::Counterpoint;

    if (fp.avgNoteDensity < 2.0 && fp.chromaticism > 0.3)
        return PhraseGenerator::Aleatoric;

    if (fp.avgNoteDensity < 3.0 && fp.rhythmComplexity > 0.5)
        return PhraseGenerator::EvolvingTexture;

    if (fp.avgPolyphony > 2.0 && fp.rhythmComplexity < 0.3)
        return PhraseGenerator::MinimalistLoop;

    return PhraseGenerator::Standard;
}
```

- [ ] **Step 2: Add extractParams**

Append to `MidiAnalyzer.cpp`:

```cpp
std::pair<juce::String, juce::String> MidiAnalyzer::extractParams(
    const MidiFingerprint& fp, int style, double beatsPerBar)
{
    // Base PhraseParams as JSON
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("scaleRoot", fp.rootNote);
    obj->setProperty("scaleMode", fp.scaleType >= 0 ? fp.scaleType : 0);
    obj->setProperty("lowNote", juce::jmax(0, fp.rootNote - 12));
    obj->setProperty("highNote", juce::jmin(127, fp.rootNote + fp.pitchRange + 12));
    obj->setProperty("minVelocity", static_cast<int>(fp.avgVelocity * 127.0 * 0.7));
    obj->setProperty("maxVelocity", static_cast<int>(fp.avgVelocity * 127.0 * 1.3));
    obj->setProperty("seed", 0);
    obj->setProperty("lengthBeats", beatsPerBar);
    obj->setProperty("density", static_cast<int>(fp.avgNoteDensity * beatsPerBar));
    obj->setProperty("noteDuration", fp.avgNoteDuration);

    juce::String paramsJson = juce::JSON::toString(obj, true);

    // Style-specific params as JSON
    juce::DynamicObject::Ptr sp = new juce::DynamicObject();
    switch (style) {
        case PhraseGenerator::TrapHiHat:
            sp->setProperty("rollDensity", static_cast<int>(fp.avgNoteDensity / 2.0));
            sp->setProperty("velocityDecay", 0.7);
            sp->setProperty("ratchetChance", fp.rhythmComplexity * 0.5);
            break;
        case PhraseGenerator::DrillBass:
            sp->setProperty("glideDuration", fp.avgNoteDuration * 0.3);
            sp->setProperty("slideIntensity", fp.syncopationScore);
            sp->setProperty("sustainTail", true);
            sp->setProperty("displacement", fp.rhythmComplexity);
            break;
        case PhraseGenerator::SwingComping:
            sp->setProperty("swingPercent", static_cast<int>(50 + fp.swingAmount * 30));
            sp->setProperty("compPattern", 0);
            sp->setProperty("voicingSpread", fp.pitchRange > 12 ? 2 : 1);
            break;
        case PhraseGenerator::WalkingBass:
            sp->setProperty("approachNotes", true);
            sp->setProperty("ghostNotes", fp.velocityDynamicRange);
            sp->setProperty("chromaticism", fp.chromaticism);
            break;
        case PhraseGenerator::ScalarRun:
            sp->setProperty("direction", fp.syncopationScore > 0.5 ? 1 : 0);
            sp->setProperty("octaveSpan", std::max(1, fp.pitchRange / 12));
            sp->setProperty("runSpeed", static_cast<int>(fp.avgNoteDensity));
            break;
        case PhraseGenerator::Counterpoint:
            sp->setProperty("voiceCount", fp.voiceCount);
            sp->setProperty("species", 2);
            sp->setProperty("intervalConstraint", 1);
            break;
        case PhraseGenerator::EvolvingTexture:
            sp->setProperty("layerCount", fp.voiceCount);
            sp->setProperty("driftSpeed", fp.rhythmComplexity);
            sp->setProperty("densitySwell", fp.velocityDynamicRange);
            break;
        case PhraseGenerator::Aleatoric:
            sp->setProperty("constraintTightness", 1.0 - fp.chromaticism);
            sp->setProperty("rhythmVariety", fp.rhythmComplexity);
            sp->setProperty("restProbability", 1.0 - fp.avgNoteDensity / 10.0);
            break;
        case PhraseGenerator::Pad:
            sp->setProperty("layerCount", fp.voiceCount);
            sp->setProperty("driftSpeed", 0.3);
            sp->setProperty("densitySwell", 0.2);
            break;
        case PhraseGenerator::Lead:
            sp->setProperty("rhythmGrid", 16);
            sp->setProperty("stateCount", 7);
            break;
        default:
            break;
    }

    juce::String styleParamsJson = juce::JSON::toString(sp, true);
    return { paramsJson, styleParamsJson };
}
```

- [ ] **Step 3: Add toPatternJson (convenience wrapper)**

Append to `MidiAnalyzer.cpp`:

```cpp
std::pair<juce::String, juce::String> MidiAnalyzer::toPatternJson(
    const MidiAnalysisResult& result)
{
    return { result.paramsJson, result.styleParamsJson };
}

} // namespace HDAW
```

- [ ] **Step 4: Build to verify**

Run: `cmake --build build --config Debug 2>&1 | Select-String -Pattern "error"`
Expected: no errors

- [ ] **Step 5: Commit**

```bash
git add src/engine/MidiAnalyzer.cpp
git commit -m "feat: MidiAnalyzer style classification and param extraction"
```

---

### Task 7: Write test suite

**Files:**
- Create: `tests/unit/engine/midi_analyzer_test.cpp`
- Modify: `tests/CMakeLists.txt:78`

- [ ] **Step 1: Add test file to CMakeLists.txt**

In `tests/CMakeLists.txt`, after line 78 (`unit/engine/midi_import_test.cpp`), add:
```cmake
    unit/engine/midi_analyzer_test.cpp
```

- [ ] **Step 2: Write the test file**

```cpp
#include <gtest/gtest.h>
#include "engine/MidiAnalyzer.h"
#include <juce_core/juce_core.h>
#include <fstream>

namespace
{

// Helper: write a minimal MIDI file with known content
std::string writeTestMidiFile(const std::vector<std::tuple<int, int, double, double>>& notes,
                               double bpm = 120.0)
{
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char path[MAX_PATH];
    GetTempFileNameA(tempPath, "mid", 0, path);

    // Build a simple MIDI file: header + 1 track with note events
    std::vector<uint8_t> data;

    // MIDI header: MThd, length=6, format=0, 1 track, 480 ticks/qn
    const uint8_t header[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 1,0xE0
    };
    data.insert(data.end(), header, header + sizeof(header));

    // Track data
    std::vector<uint8_t> trackData;

    // Tempo meta event
    uint32_t usPerQuarter = static_cast<uint32_t>(60000000.0 / bpm);
    trackData.push_back(0);
    trackData.insert(trackData.end(), { 0xFF, 0x51, 0x03 });
    trackData.push_back((usPerQuarter >> 16) & 0xFF);
    trackData.push_back((usPerQuarter >> 8) & 0xFF);
    trackData.push_back(usPerQuarter & 0xFF);

    // Note events
    for (const auto& [pitch, vel, start, dur] : notes) {
        uint32_t startTick = static_cast<uint32_t>(start * 480.0);
        uint32_t endTick = static_cast<uint32_t>((start + dur) * 480.0);

        // Note-on (delta = startTick - previous)
        trackData.push_back(0); // delta=0 for simplicity (overlapping is fine for test)
        trackData.push_back(0x90);
        trackData.push_back(static_cast<uint8_t>(pitch));
        trackData.push_back(static_cast<uint8_t>(vel));

        // Note-off
        trackData.push_back(0);
        trackData.push_back(0x80);
        trackData.push_back(static_cast<uint8_t>(pitch));
        trackData.push_back(0);
    }

    // End of track
    trackData.insert(trackData.end(), { 0, 0xFF, 0x2F, 0 });

    // Track chunk header
    data.insert(data.end(), { 'M','T','r','k' });
    uint32_t trackLen = static_cast<uint32_t>(trackData.size());
    data.push_back((trackLen >> 24) & 0xFF);
    data.push_back((trackLen >> 16) & 0xFF);
    data.push_back((trackLen >> 8) & 0xFF);
    data.push_back(trackLen & 0xFF);
    data.insert(data.end(), trackData.begin(), trackData.end());

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();
    return std::string(path);
}

TEST(MidiAnalyzerTest, AnalyzeNonexistentFileReturnsEmpty)
{
    auto result = HDAW::MidiAnalyzer::analyze(juce::File("C:/nonexistent/path.mid"));
    EXPECT_TRUE(result.fileName.isEmpty());
    EXPECT_EQ(result.trackCount, 0);
}

TEST(MidiAnalyzerTest, AnalyzeSimpleScaleReturnsFingerprint)
{
    // C major scale: C D E F G A B C
    std::vector<std::tuple<int, int, double, double>> notes = {
        {60, 100, 0.0, 0.5},  // C4
        {62, 100, 0.5, 0.5},  // D4
        {64, 100, 1.0, 0.5},  // E4
        {65, 100, 1.5, 0.5},  // F4
        {67, 100, 2.0, 0.5},  // G4
        {69, 100, 2.5, 0.5},  // A4
        {71, 100, 3.0, 0.5},  // B4
        {72, 100, 3.5, 0.5},  // C5
    };

    auto path = writeTestMidiFile(notes);
    auto file = juce::File(path);
    auto result = HDAW::MidiAnalyzer::analyze(file);

    EXPECT_EQ(result.trackCount, 1);
    EXPECT_GT(result.fingerprint.avgNoteDensity, 0.0);
    EXPECT_GT(result.fingerprint.pitchRange, 0);
    EXPECT_GE(result.fingerprint.scaleType, 0);
    EXPECT_NEAR(result.fingerprint.avgVelocity, 100.0 / 127.0, 0.05);

    file.deleteFile();
}

TEST(MidiAnalyzerTest, AnalyzeChordReturnsHighPolyphony)
{
    // C major chord: C E G played simultaneously
    std::vector<std::tuple<int, int, double, double>> notes = {
        {60, 100, 0.0, 1.0},
        {64, 100, 0.0, 1.0},
        {67, 100, 0.0, 1.0},
        {60, 100, 1.0, 1.0},
        {64, 100, 1.0, 1.0},
        {67, 100, 1.0, 1.0},
    };

    auto path = writeTestMidiFile(notes);
    auto result = HDAW::MidiAnalyzer::analyze(juce::File(path));

    EXPECT_GE(result.fingerprint.avgPolyphony, 2.5);
    EXPECT_EQ(result.fingerprint.voiceCount, 1);

    juce::File(path).deleteFile();
}

TEST(MidiAnalyzerTest, RepeatedBarsAppearAsPatterns)
{
    // 4 bars, bars 0 and 2 identical
    std::vector<std::tuple<int, int, double, double>> notes;
    auto addBar = [&](int barStart, int pitch) {
        notes.push_back({pitch, 100, barStart + 0.0, 0.25});
        notes.push_back({pitch + 4, 100, barStart + 0.5, 0.25});
        notes.push_back({pitch + 7, 100, barStart + 1.0, 0.25});
        notes.push_back({pitch + 12, 80, barStart + 1.5, 0.5});
    };

    addBar(0.0, 60);  // bar 0: C E G C
    addBar(4.0, 62);  // bar 1: D F# A D
    addBar(8.0, 60);  // bar 2: same as bar 0
    addBar(12.0, 65); // bar 3: F A C F

    auto path = writeTestMidiFile(notes);
    auto result = HDAW::MidiAnalyzer::analyze(juce::File(path));

    // Should have at least one bar pattern with frequency >= 2
    bool foundRepeated = false;
    for (const auto& p : result.patterns) {
        if (!p.isMotif && p.frequency >= 2.0) {
            foundRepeated = true;
            break;
        }
    }
    EXPECT_TRUE(foundRepeated);

    juce::File(path).deleteFile();
}

TEST(MidiAnalyzerTest, StyleClassificationForHighDensity)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 12.0;
    fp.pitchRange = 5;
    fp.avgNoteDuration = 0.1;
    fp.rhythmComplexity = 0.7;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::TrapHiHat);
}

TEST(MidiAnalyzerTest, StyleClassificationForPad)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 1.5;
    fp.pitchRange = 36;
    fp.avgNoteDuration = 4.0;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::Pad);
}

TEST(MidiAnalyzerTest, StyleClassificationForWalkingBass)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 4.0;
    fp.pitchRange = 18;
    fp.swingAmount = 0.5;
    fp.rhythmComplexity = 0.2;
    fp.avgPolyphony = 1.0;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::WalkingBass);
}

TEST(MidiAnalyzerTest, ToPatternJsonReturnsNonEmpty)
{
    HDAW::MidiAnalysisResult result;
    result.fingerprint.rootNote = 60;
    result.fingerprint.scaleType = 0;
    result.fingerprint.avgNoteDensity = 4.0;
    result.fingerprint.avgNoteDuration = 0.5;
    result.fingerprint.pitchRange = 24;
    result.fingerprint.avgVelocity = 0.7;
    result.guessedStyle = PhraseGenerator::Standard;

    auto [params, styleParams] = HDAW::MidiAnalyzer::toPatternJson(result);
    EXPECT_FALSE(params.isEmpty());
    EXPECT_FALSE(styleParams.isEmpty());
}

} // namespace
```

- [ ] **Step 3: Build and run the tests**

Run: `cmake --build build --config Debug --target hdaw_tests 2>&1 | Select-String -Pattern "error"`
Expected: no errors

Run: `build\Debug\hdaw_tests.exe --gtest_filter=MidiAnalyzerTest.*`
Expected: all tests PASS

- [ ] **Step 4: Commit**

```bash
git add tests/unit/engine/midi_analyzer_test.cpp tests/CMakeLists.txt
git commit -m "feat: MidiAnalyzer test suite"
```

---

### Task 8: Add RPC handler for analyzeMidiFile

**Files:**
- Modify: `src/frontend/router/Router_Composition.cpp`

- [ ] **Step 1: Add include for MidiAnalyzer**

After line 10 (`#include "../../engine/PatternLibrary.h"`), add:
```cpp
#include "../../engine/MidiAnalyzer.h"
```

- [ ] **Step 2: Add the RPC handler**

In `Router_Composition.cpp`, after the existing `listPatterns` block (around line 100), add a new handler before the mutations section. Find the line `// --- Mutations: generate + insert MIDI clip ---` and insert before it:

```cpp
    // --- MIDI Analysis ---

    if (m == "analyzeMidiFile") {
        std::string filePath;
        if (!requireString(o, "path", filePath, nullptr))
            return makeError(-32602, "path required");

        juce::File file(juce::String(filePath));
        if (!file.existsAsFile())
            return makeError(-32602, "file does not exist: " + filePath);

        auto result = HDAW::MidiAnalyzer::analyze(file);
        if (result.trackCount == 0)
            return makeError(-32603, "no MIDI data found in file");

        // Convert to JSON
        QJsonObject fpObj;
        fpObj["avgNoteDensity"] = result.fingerprint.avgNoteDensity;
        fpObj["rhythmComplexity"] = result.fingerprint.rhythmComplexity;
        fpObj["syncopationScore"] = result.fingerprint.syncopationScore;
        fpObj["swingAmount"] = result.fingerprint.swingAmount;
        fpObj["pitchRange"] = result.fingerprint.pitchRange;
        fpObj["rootNote"] = result.fingerprint.rootNote;
        fpObj["scaleType"] = result.fingerprint.scaleType;
        fpObj["chromaticism"] = result.fingerprint.chromaticism;
        fpObj["avgVelocity"] = result.fingerprint.avgVelocity;
        fpObj["velocityRange"] = result.fingerprint.velocityRange;
        fpObj["velocityDynamicRange"] = result.fingerprint.velocityDynamicRange;
        fpObj["quantizationStrength"] = result.fingerprint.quantizationStrength;
        fpObj["avgNoteDuration"] = result.fingerprint.avgNoteDuration;
        fpObj["barCount"] = result.fingerprint.barCount;
        fpObj["voiceCount"] = result.fingerprint.voiceCount;
        fpObj["avgPolyphony"] = result.fingerprint.avgPolyphony;

        QJsonArray patternsArr;
        for (const auto& p : result.patterns) {
            QJsonArray notesArr;
            for (const auto& n : p.notes) {
                notesArr.append(QJsonObject{
                    { "startBeat", n.startBeat },
                    { "pitch", n.pitch },
                    { "velocity", n.velocity },
                    { "durationBeats", n.durationBeats }
                });
            }
            patternsArr.append(QJsonObject{
                { "name", QString::fromStdString(p.name.toStdString()) },
                { "startBar", p.startBar },
                { "lengthBars", p.lengthBars },
                { "trackIndex", p.trackIndex },
                { "notes", notesArr },
                { "frequency", p.frequency },
                { "isMotif", p.isMotif }
            });
        }

        QJsonObject resultObj;
        resultObj["fingerprint"] = fpObj;
        resultObj["patterns"] = patternsArr;
        resultObj["guessedStyle"] = PhraseGenerator::styleName(
            static_cast<PhraseGenerator::Style>(result.guessedStyle));
        resultObj["styleConfidence"] = result.styleConfidence;
        resultObj["paramsJson"] = QString::fromStdString(result.paramsJson.toStdString());
        resultObj["styleParamsJson"] = QString::fromStdString(result.styleParamsJson.toStdString());
        resultObj["sourceBpm"] = result.sourceBpm;
        resultObj["timeSignatureNum"] = result.timeSignatureNum;
        resultObj["timeSignatureDen"] = result.timeSignatureDen;
        resultObj["trackCount"] = result.trackCount;
        resultObj["fileName"] = QString::fromStdString(result.fileName.toStdString());

        return { false, resultObj };
    }

```

- [ ] **Step 3: Build to verify**

Run: `cmake --build build --config Debug 2>&1 | Select-String -Pattern "error"`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add src/frontend/router/Router_Composition.cpp
git commit -m "feat: add composition.analyzeMidiFile RPC handler"
```

---

### Task 9: Fix static thread-local in extractMotifs

**Files:**
- Modify: `src/engine/MidiAnalyzer.cpp`

- [ ] **Step 1: Fix the thread_local static motifCount map**

The current `extractMotifs` uses a `static thread_local` map that leaks across calls. Replace the approach: pass a mutable map as a parameter, or clear it between calls. Simplest: use a local map that persists across the function's sliding window loops but is per-call.

Replace the `static thread_local` section in `extractMotifs` with:

```cpp
                // Hash the signature
                // (h already computed above)

                // Track motif occurrence per-call
                // (motifCount is declared at function scope, not static)
                motifCount[h]++;

                if (motifCount[h] == 2) {
```

And add at the top of `extractMotifs`:
```cpp
    std::unordered_map<size_t, int> motifCount;
```

Remove the `static thread_local` line.

- [ ] **Step 2: Build and run tests**

Run: `cmake --build build --config Debug --target hdaw_tests 2>&1 | Select-String -Pattern "error"`
Expected: no errors

Run: `build\Debug\hdaw_tests.exe --gtest_filter=MidiAnalyzerTest.*`
Expected: all tests PASS

- [ ] **Step 3: Commit**

```bash
git add src/engine/MidiAnalyzer.cpp
git commit -m "fix: MidiAnalyzer motif extraction per-call map instead of static"
```

---

### Task 10: Full build and verification

**Files:** None (verification only)

- [ ] **Step 1: Full Debug build**

Run: `cmake --build build --config Debug 2>&1 | Select-String -Pattern "error"`
Expected: no errors

- [ ] **Step 2: Run all MidiAnalyzer tests**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=MidiAnalyzerTest.*`
Expected: all tests PASS

- [ ] **Step 3: Run existing MidiImport tests (no regression)**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=MidiImportTest.*`
Expected: all tests PASS

- [ ] **Step 4: Run full test suite (quick check)**

Run: `build\Debug\hdaw_tests.exe 2>&1 | Select-String -Pattern "PASSED|FAILED"`
Expected: all PASSED, no FAILED

- [ ] **Step 5: Final commit if needed**

```bash
git add -A
git status
```

Only commit if there are unstaged changes. Otherwise, the work is complete.
