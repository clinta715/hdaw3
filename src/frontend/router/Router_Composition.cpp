#include "Router_Composition.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/PhraseGenerator.h"
#include "../../engine/ArrangementGenerator.h"
#include "../../common/ProjectCommands.h"
#include "../../common/AudioGraphCommands.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>
#include <vector>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchComposition(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    auto& c = engine.getProjectCommands();
    auto& ag = engine.getAudioGraphCommands();

    // --- Read-only queries (PhraseGenerator is a static utility) ---

    if (m == "getScaleModes") {
        QJsonArray arr;
        for (const auto& sm : PhraseGenerator::getScaleModes()) {
            QJsonArray intervals;
            for (int iv : sm.intervals) intervals.append(iv);
            arr.append(QJsonObject{ { "index", sm.index }, { "name", sm.name }, { "intervals", intervals } });
        }
        return { false, arr };
    }
    if (m == "getChordTypes") {
        QJsonArray arr;
        for (const auto& ct : PhraseGenerator::getChordTypes()) {
            QJsonArray intervals;
            for (int iv : ct.intervals) intervals.append(iv);
            arr.append(QJsonObject{ { "index", ct.index }, { "name", ct.name }, { "intervals", intervals } });
        }
        return { false, arr };
    }
    if (m == "getProgressionPatterns") {
        QJsonArray arr;
        for (const auto& pp : PhraseGenerator::getProgressionPatterns()) {
            QJsonArray chords;
            for (const auto& [degree, chordType] : pp.chords) {
                chords.append(QJsonObject{ { "degree", degree }, { "chordType", chordType } });
            }
            arr.append(QJsonObject{ { "index", pp.index }, { "name", pp.name }, { "chords", chords } });
        }
        return { false, arr };
    }
    if (m == "getStyleNames") {
        QJsonArray arr;
        for (int i = 0; i <= static_cast<int>(PhraseGenerator::Euclidean); ++i) {
            arr.append(QJsonObject{ { "index", i }, { "name", PhraseGenerator::styleName(static_cast<PhraseGenerator::Style>(i)) } });
        }
        return { false, arr };
    }
    if (m == "getNoteName") {
        int pitch;
        if (!requireInt(o, "pitch", pitch, nullptr))
            return makeError(-32602, "pitch required");
        return { false, QString::fromUtf8(PhraseGenerator::noteName(pitch)) };
    }

    // --- Mutations: generate + insert MIDI clip ---

    // Shared lambda: generate notes, create clip, add notes, return { clipId, noteCount }
    auto generateIntoClip = [&](int trackIndex, double startBeat, double clipDuration,
                                const std::string& clipName,
                                const std::vector<PhraseGenerator::GeneratedNote>& notes) -> DispatchResult {
        int clipId = c.addMidiClip(trackIndex, startBeat, clipDuration, clipName);
        for (const auto& n : notes) {
            c.addNote(clipId, n.noteNumber, n.velocity, n.startBeat, n.durationBeats);
        }
        ag.rebuildRoutingGraph();
        QJsonObject result{ { "clipId", clipId }, { "noteCount", static_cast<int>(notes.size()) } };
        return { false, result };
    };

    if (m == "generatePhrase") {
        int trackIndex;
        std::string styleStr;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        if (!requireString(o, "style", styleStr, nullptr))
            return makeError(-32602, "style required");

        PhraseGenerator::Style style = PhraseGenerator::Standard;
        if      (styleStr == "Arpeggio")   style = PhraseGenerator::Arpeggio;
        else if (styleStr == "BassLine")   style = PhraseGenerator::BassLine;
        else if (styleStr == "ChordStab")  style = PhraseGenerator::ChordStab;
        else if (styleStr == "Pad")        style = PhraseGenerator::Pad;
        else if (styleStr == "Lead")       style = PhraseGenerator::Lead;
        else if (styleStr == "RandomWalk") style = PhraseGenerator::RandomWalk;
        else if (styleStr == "Buildup")    style = PhraseGenerator::Buildup;
        else if (styleStr == "Euclidean")  style = PhraseGenerator::Euclidean;

        PhraseGenerator::PhraseParams pp;
        pp.style = style;
        pp.lengthBeats = optDouble(o, "lengthBeats", 4.0, nullptr);
        pp.density = optInt(o, "density", 8, nullptr);
        pp.noteDuration = optDouble(o, "noteDuration", 0.5, nullptr);
        pp.scaleRoot = optInt(o, "scaleRoot", 0, nullptr);
        pp.scaleMode = optInt(o, "scaleMode", 0, nullptr);
        pp.lowNote = optInt(o, "lowNote", 48, nullptr);
        pp.highNote = optInt(o, "highNote", 84, nullptr);
        pp.minVelocity = optInt(o, "minVelocity", 60, nullptr);
        pp.maxVelocity = optInt(o, "maxVelocity", 110, nullptr);
        pp.seed = optInt<uint64_t>(o, "seed", 0, nullptr);

        double startBeat = optDouble(o, "startBeat", 0.0, nullptr);
        auto notes = PhraseGenerator::generatePhrase(pp);
        std::string name = std::string("Phrase: ") + styleStr;
        return generateIntoClip(trackIndex, startBeat, pp.lengthBeats, name, notes);
    }

    if (m == "generateChord") {
        int trackIndex, rootPitch, chordType;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        if (!requireInt(o, "rootPitch", rootPitch, nullptr))
            return makeError(-32602, "rootPitch required");
        if (!requireInt(o, "chordType", chordType, nullptr))
            return makeError(-32602, "chordType required");

        PhraseGenerator::ChordParams cp;
        cp.chordType = chordType;
        cp.voicing = optInt(o, "voicing", 0, nullptr);
        cp.inversion = optInt(o, "inversion", 0, nullptr);
        cp.arpeggiate = optBool(o, "arpeggiate", false, nullptr);
        cp.arpeggioRate = optDouble(o, "arpeggioRate", 0.125, nullptr);
        cp.durationBeats = optDouble(o, "durationBeats", 2.0, nullptr);
        cp.scaleRoot = optInt(o, "scaleRoot", 0, nullptr);
        cp.scaleMode = optInt(o, "scaleMode", 0, nullptr);
        cp.lowNote = optInt(o, "lowNote", 48, nullptr);
        cp.highNote = optInt(o, "highNote", 84, nullptr);
        cp.minVelocity = optInt(o, "minVelocity", 60, nullptr);
        cp.maxVelocity = optInt(o, "maxVelocity", 110, nullptr);
        cp.seed = optInt<uint64_t>(o, "seed", 0, nullptr);

        double startBeat = optDouble(o, "startBeat", 0.0, nullptr);
        auto notes = PhraseGenerator::generateChord(rootPitch, cp);
        const char* ctName = PhraseGenerator::chordTypeName(chordType);
        std::string name = std::string("Chord: ") + ctName;
        return generateIntoClip(trackIndex, startBeat, cp.durationBeats, name, notes);
    }

    if (m == "generateProgression") {
        int trackIndex, patternIndex;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        if (!requireInt(o, "patternIndex", patternIndex, nullptr))
            return makeError(-32602, "patternIndex required");

        PhraseGenerator::ProgressionParams prp;
        prp.patternIndex = patternIndex;
        prp.chordTypeOverride = optInt(o, "chordTypeOverride", -1, nullptr);
        prp.arpeggiate = optBool(o, "arpeggiate", false, nullptr);
        prp.arpeggioRate = optDouble(o, "arpeggioRate", 0.125, nullptr);
        prp.durationBeats = optDouble(o, "durationBeats", 2.0, nullptr);
        prp.beatsPerChord = optDouble(o, "beatsPerChord", 4.0, nullptr);
        prp.scaleRoot = optInt(o, "scaleRoot", 0, nullptr);
        prp.scaleMode = optInt(o, "scaleMode", 0, nullptr);
        prp.lowNote = optInt(o, "lowNote", 48, nullptr);
        prp.highNote = optInt(o, "highNote", 84, nullptr);
        prp.minVelocity = optInt(o, "minVelocity", 60, nullptr);
        prp.maxVelocity = optInt(o, "maxVelocity", 110, nullptr);
        prp.seed = optInt<uint64_t>(o, "seed", 0, nullptr);

        const auto& patterns = PhraseGenerator::getProgressionPatterns();
        if (patternIndex < 0 || patternIndex >= static_cast<int>(patterns.size()))
            return makeError(-32602, "patternIndex out of range");

        double startBeat = optDouble(o, "startBeat", 0.0, nullptr);
        auto notes = PhraseGenerator::generateProgression(prp);
        double clipDuration = prp.beatsPerChord * static_cast<double>(patterns[patternIndex].chords.size());
        std::string name = std::string("Progression: ") + patterns[patternIndex].name;
        return generateIntoClip(trackIndex, startBeat, clipDuration, name, notes);
    }

    if (m == "generateArrangement") {
        HDAW::ArrangementParams ap;
        ap.bars        = optInt(o, "bars", 32, nullptr);
        ap.bpm         = optDouble(o, "bpm", engine.getTransportManager().getBPM(), nullptr);
        ap.scaleRoot   = optInt(o, "scaleRoot", engine.getProjectModel().getScaleRoot(), nullptr);
        ap.scaleMode   = optInt(o, "scaleMode", engine.getProjectModel().getScaleMode(), nullptr);
        ap.seed        = optInt<uint64_t>(o, "seed", 0, nullptr);
        ap.style       = optInt(o, "style", 0, nullptr);
        ap.complexity  = optDouble(o, "complexity", 0.5, nullptr);
        ap.swingPercent= optDouble(o, "swingPercent", 50.0, nullptr);
        ap.enableKick      = optBool(o, "enableKick", true, nullptr);
        ap.enableClosedHat = optBool(o, "enableClosedHat", true, nullptr);
        ap.enableOpenHat   = optBool(o, "enableOpenHat", true, nullptr);
        ap.enableClap      = optBool(o, "enableClap", true, nullptr);
        ap.enableSnare     = optBool(o, "enableSnare", false, nullptr);
        ap.enableBass      = optBool(o, "enableBass", true, nullptr);
        ap.enableLead      = optBool(o, "enableLead", false, nullptr);
        ap.enableChords    = optBool(o, "enableChords", false, nullptr);

        auto result = c.generateArrangement(ap);

        QJsonArray tracks, clips;
        for (int t : result.trackIndices) tracks.append(t);
        for (int ci : result.clipIds) clips.append(ci);
        QJsonObject res{
            { "trackIndices", tracks },
            { "clipIds", clips },
            { "noteCount", result.noteCount },
            { "seed", static_cast<double>(result.seed) }
        };
        return { false, res };
    }

    return makeError(-32601, "unknown composition method: " + m);
}

} // namespace frontend
