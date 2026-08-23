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

    int midiTimeFormat = static_cast<int>(midiFile.getTimeFormat());
    if (midiTimeFormat <= 0)
        return result;
    double ticksPerQuarter = static_cast<double>(midiTimeFormat);

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

    if (tempoTrack != nullptr) {
        for (int e = 0; e < tempoTrack->getNumEvents(); ++e) {
            auto* ev = tempoTrack->getEventPointer(e);
            if (ev != nullptr && ev->message.isTimeSignatureMetaEvent()) {
                ev->message.getTimeSignatureInfo(result.timeSignatureNum, result.timeSignatureDen);
                break;
            }
        }
    }

    double beatsPerBar = static_cast<double>(result.timeSignatureNum);

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

    result.fingerprint = computeFingerprint(tracks, beatsPerBar);
    result.patterns = extractBarPatterns(tracks, beatsPerBar);
    auto motifs = extractMotifs(tracks, beatsPerBar);
    for (auto& m : motifs)
        result.patterns.push_back(std::move(m));

    result.guessedStyle = classifyStyle(result.fingerprint);
    result.styleConfidence = 0.5f;

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

    std::sort(ta.notes.begin(), ta.notes.end(),
        [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });

    return ta;
}

int MidiAnalyzer::detectScale(const std::vector<TrackAnalysis>& tracks, int& rootNote)
{
    double pitchBins[12] = {};
    for (const auto& t : tracks) {
        for (const auto& n : t.notes) {
            int bin = ((n.pitch % 12) + 12) % 12;
            pitchBins[bin] += n.durationBeats * (n.velocity / 127.0);
        }
    }

    int bestBin = 0;
    for (int i = 1; i < 12; ++i)
        if (pitchBins[i] > pitchBins[bestBin])
            bestBin = i;
    rootNote = bestBin;

    const auto& modes = PhraseGenerator::getScaleModes();
    int bestMode = -1;
    double bestScore = -1.0;

    for (const auto& mode : modes) {
        if (mode.index == 12) continue;

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
    std::vector<double> offsets;
    for (const auto& t : tracks) {
        for (const auto& n : t.notes) {
            double posInBeat = std::fmod(n.startBeat, 1.0);
            if (posInBeat < 0.0) posInBeat += 1.0;

            double subdivPos = std::fmod(posInBeat * beatDivision, 1.0);
            if (std::fmod(subdivPos, 2.0) >= 0.8 || std::fmod(subdivPos, 2.0) <= 0.2) {
                double straightOffset = std::floor(posInBeat * beatDivision + 0.5) / beatDivision;
                double offset = (posInBeat - straightOffset) * beatDivision;
                offsets.push_back(offset);
            }
        }
    }

    if (offsets.empty())
        return 0.0;

    double avgOffset = std::accumulate(offsets.begin(), offsets.end(), 0.0) / offsets.size();
    return std::max(0.0, std::min(1.0, avgOffset * 5.0));
}

double MidiAnalyzer::detectGridInterval(const std::vector<TrackAnalysis>& tracks)
{
    std::vector<double> intervals;
    for (const auto& t : tracks) {
        for (size_t i = 1; i < t.notes.size(); ++i) {
            double dt = t.notes[i].startBeat - t.notes[i - 1].startBeat;
            if (dt > 0.001 && dt < 4.0)
                intervals.push_back(dt);
        }
    }

    if (intervals.empty())
        return 0.25;

    std::unordered_map<int, int> hist;
    for (double d : intervals) {
        int bin = static_cast<int>(d * 100.0 + 0.5);
        hist[bin]++;
    }

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

    double pitchBins[12] = {};
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

        for (const auto& n : t.notes) {
            int simCount = 0;
            for (const auto& m : t.notes) {
                if (&m == &n) continue;
                if (m.startBeat <= n.startBeat &&
                    m.startBeat + m.durationBeats > n.startBeat)
                    simCount++;
            }
            totalPolyphony += simCount + 1;
            polyphonySamples++;
        }
    }

    if (totalNotes == 0)
        return fp;

    fp.avgNoteDensity = totalNotes / std::max(1.0, totalBeatSpan);
    fp.avgNoteDuration = totalDuration / totalNotes;
    fp.avgVelocity = (totalVelocity / totalNotes) / 127.0;
    fp.velocityRange = (maxVel - minVel) / 127.0;
    double meanVel = totalVelocity / totalNotes;
    double variance = (velocitySquaredSum / totalNotes) - (meanVel * meanVel);
    fp.velocityDynamicRange = std::sqrt(std::max(0.0, variance)) / 127.0;
    fp.pitchRange = maxPitch - minPitch;
    fp.scaleType = detectScale(tracks, fp.rootNote);

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

    fp.barCount = totalBeatSpan / beatsPerBar;
    fp.voiceCount = static_cast<int>(tracks.size());
    fp.avgPolyphony = polyphonySamples > 0 ? totalPolyphony / polyphonySamples : 1.0;

    double gridInterval = detectGridInterval(tracks);

    double totalDeviation = 0.0;
    for (double onset : allOnsets) {
        double nearestGrid = std::round(onset / gridInterval) * gridInterval;
        totalDeviation += std::abs(onset - nearestGrid);
    }
    if (!allOnsets.empty()) {
        double avgDev = totalDeviation / allOnsets.size();
        fp.quantizationStrength = std::max(0.0, 1.0 - (avgDev / gridInterval) * 2.0);
    }

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

    int offBeat = 0;
    for (double onset : allOnsets) {
        double posInBar = std::fmod(onset, beatsPerBar);
        double posInBeat = std::fmod(posInBar, 1.0);
        if (posInBeat > 0.1 && posInBeat < 0.9)
            offBeat++;
    }
    fp.syncopationScore = allOnsets.empty() ? 0.0 :
        static_cast<double>(offBeat) / allOnsets.size();

    fp.swingAmount = detectSwing(tracks, 2.0);

    return fp;
}

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

    struct BarSignature {
        std::vector<std::pair<double, int>> onsetPitches;
    };

    std::unordered_map<size_t, std::vector<BarKey>> sigToBars;
    for (auto& [key, barNotes] : barMap) {
        BarSignature sig;
        for (const auto& n : barNotes.notes)
            sig.onsetPitches.push_back({ n.startBeat, n.pitch });
        std::sort(sig.onsetPitches.begin(), sig.onsetPitches.end());

        size_t h = sig.onsetPitches.size();
        for (const auto& [onset, pitch] : sig.onsetPitches) {
            h ^= std::hash<double>()(onset + 0.001) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(pitch) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        sigToBars[h].push_back(key);
    }

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

std::vector<MidiPattern> MidiAnalyzer::extractMotifs(
    const std::vector<TrackAnalysis>& tracks, double beatsPerBar)
{
    std::vector<MidiPattern> motifs;
    std::unordered_map<size_t, int> motifCount;

    for (const auto& t : tracks) {
        if (t.notes.size() < 3) continue;

        double maxBeat = t.notes.back().startBeat + t.notes.back().durationBeats;

        for (double winLen = beatsPerBar * 0.5; winLen <= beatsPerBar * 2.0;
             winLen += beatsPerBar * 0.25)
        {
            for (double winStart = 0.0; winStart + winLen <= maxBeat;
                 winStart += beatsPerBar * 0.5)
            {
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

                int minPitch = windowNotes[0].pitch;
                for (const auto& n : windowNotes)
                    minPitch = std::min(minPitch, n.pitch);

                std::vector<std::pair<double, int>> sig;
                for (const auto& n : windowNotes)
                    sig.push_back({ n.startBeat, n.pitch - minPitch });
                std::sort(sig.begin(), sig.end());

                size_t h = sig.size();
                for (const auto& [onset, pitch] : sig) {
                    h ^= std::hash<double>()(onset + 0.001) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    h ^= std::hash<int>()(pitch) + 0x9e3779b9 + (h << 6) + (h >> 2);
                }

                motifCount[h]++;

                if (motifCount[h] == 2) {
                    MidiPattern pat;
                    pat.startBar = static_cast<int>(winStart / beatsPerBar);
                    pat.lengthBars = std::max(1, static_cast<int>(std::ceil(winLen / beatsPerBar)));
                    pat.trackIndex = 0;
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

int MidiAnalyzer::classifyStyle(const MidiFingerprint& fp)
{
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

std::pair<juce::String, juce::String> MidiAnalyzer::extractParams(
    const MidiFingerprint& fp, int style, double beatsPerBar)
{
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

    juce::String paramsJson = juce::JSON::toString(juce::var(obj.get()), true);

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

    juce::String styleParamsJson = juce::JSON::toString(juce::var(sp.get()), true);
    return { paramsJson, styleParamsJson };
}

std::pair<juce::String, juce::String> MidiAnalyzer::toPatternJson(
    const MidiAnalysisResult& result)
{
    return { result.paramsJson, result.styleParamsJson };
}

} // namespace HDAW
