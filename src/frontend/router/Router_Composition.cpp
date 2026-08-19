#include "Router_Composition.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/PhraseGenerator.h"
#include "../../engine/ArrangementGenerator.h"
#include "../../engine/RhythmPatternGenerator.h"
#include "../../common/ProjectCommands.h"
#include "../../common/AudioGraphCommands.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>
#include <vector>
#include <algorithm>

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

    if (m == "generateRhythmPattern")
    {
        int trackIndex;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");

        RhythmPatternGenerator::Params rp;
        rp.grid        = optInt(o, "grid", 16, nullptr);
        rp.bars        = optInt(o, "bars", 1, nullptr);
        rp.pulseA      = optInt(o, "pulseA", 4, nullptr);
        rp.pulseB      = optInt(o, "pulseB", 3, nullptr);
        rp.rotationA   = optInt(o, "rotationA", 1, nullptr);
        rp.rotationB   = optInt(o, "rotationB", 1, nullptr);
        rp.pitchA      = optInt(o, "pitchA", 36, nullptr);
        rp.pitchB      = optInt(o, "pitchB", 42, nullptr);
        rp.velocityA   = optInt(o, "velocityA", 112, nullptr);
        rp.velocityB   = optInt(o, "velocityB", 96, nullptr);
        rp.dsl         = optString(o, "dsl", "");
        rp.dslPitch    = optInt(o, "dslPitch", 39, nullptr);
        rp.dslVelocity = optInt(o, "dslVelocity", 104, nullptr);

        double startBeat = optDouble(o, "startBeat", 0.0, nullptr);

        std::vector<RhythmPatternGenerator::Note> notes;
        try
        {
            notes = RhythmPatternGenerator::generate(rp);
        }
        catch (const std::invalid_argument& ex)
        {
            return makeError(-32602, QString("dsl: ") + ex.what());
        }
        if (notes.empty())
            return makeError(-32602, "pattern produced no notes");

        std::vector<PhraseGenerator::GeneratedNote> converted;
        converted.reserve(notes.size());
        for (const auto& n : notes)
            converted.push_back({ n.startBeat, n.pitch, n.velocity, n.durationBeats });

        const double totalBeats = (std::max)(1, rp.bars) * 4.0;
        return generateIntoClip(trackIndex, startBeat, totalBeats, "Rhythm Pattern", converted);
    }

    if (m == "addInstrumentPart") {
        // Composite: add track + instrument FX + phrase + paint (+ optional
        // gain staging). One engine command = one undo unit / one rebuild.
        // Synchronous RPC that blocks the channel for the gain-stage render
        // duration (windowSeconds + bake) when targetRms > 0 — same shape as
        // export.audio. Beats are accepted at this boundary (lesson #1).
        ProjectCommands::InstrumentPartParams p;
        if (!requireString(o, "trackName", p.trackName, nullptr))
            return makeError(-32602, "trackName required");
        if (!requireString(o, "style", p.style, nullptr))
            return makeError(-32602, "style required");
        p.pluginId      = optString(o, "pluginId", "");
        p.programIndex  = optInt(o, "programIndex", -1, nullptr);
        p.lengthBeats   = optDouble(o, "lengthBeats", 4.0, nullptr);
        p.placement     = optString(o, "placement", "region");
        p.startBeat     = optDouble(o, "startBeat", 0.0, nullptr);
        p.count         = optInt(o, "count", 1, nullptr);
        p.scaleRoot     = optInt(o, "scaleRoot", -1, nullptr);
        p.scaleMode     = optInt(o, "scaleMode", -1, nullptr);
        p.density       = optInt(o, "density", 8, nullptr);
        p.noteDuration  = optDouble(o, "noteDuration", 0.5, nullptr);
        p.lowNote       = optInt(o, "lowNote", 48, nullptr);
        p.highNote      = optInt(o, "highNote", 84, nullptr);
        p.minVelocity   = optInt(o, "minVelocity", 60, nullptr);
        p.maxVelocity   = optInt(o, "maxVelocity", 110, nullptr);
        p.seed          = optInt<uint64_t>(o, "seed", 0, nullptr);
        p.targetRms     = optFloat(o, "targetRms", 0.0f, nullptr);
        p.windowSeconds = optDouble(o, "windowSeconds", 4.0, nullptr);
        p.verify        = optBool(o, "verify", false, nullptr);

        auto r = c.addInstrumentPart(p);
        QJsonArray clipIds;
        for (int id : r.clipIds) clipIds.append(id);
        QJsonObject res{
            { "trackIndex", r.trackIndex },
            { "clipIds", clipIds },
            { "noteCount", r.noteCount }
        };
        if (!r.error.empty())
            res.insert("error", QString::fromStdString(r.error));
        if (p.targetRms > 0.0f) {   // engine populated r.gain only when staged
            QJsonObject gain{
                { "ok", r.gain.ok },
                { "fader", static_cast<double>(r.gain.fader) },
                { "measuredRms", static_cast<double>(r.gain.measuredRms) },
                { "peak", static_cast<double>(r.gain.peak) },
                { "clamped", r.gain.clamped }
            };
            if (!r.gain.error.empty())
                gain.insert("error", QString::fromStdString(r.gain.error));
            res.insert("gain", gain);
        }
        return { false, res };
    }

    if (m == "autoGainToTarget") {
        // Gain-stage primitive: solo-render the track's first window, measure,
        // set the fader. Synchronous RPC that blocks for the render duration.
        int trackIndex;
        float targetRms;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        if (!requireFloat(o, "targetRms", targetRms, nullptr))
            return makeError(-32602, "targetRms required");
        const double windowSeconds = optDouble(o, "windowSeconds", 4.0, nullptr);
        const bool verify = optBool(o, "verify", false, nullptr);

        auto r = c.autoGainToTarget(trackIndex, targetRms, windowSeconds, verify);
        QJsonObject res{
            { "ok", r.ok },
            { "fader", static_cast<double>(r.fader) },
            { "measuredRms", static_cast<double>(r.measuredRms) },
            { "peak", static_cast<double>(r.peak) },
            { "clamped", r.clamped }
        };
        if (!r.error.empty())
            res.insert("error", QString::fromStdString(r.error));
        return { false, res };
    }

    if (m == "auditionPlugin") {
        // Solo-render a plugin over a short window and report peak/rms/audible
        // so silent-at-default plugins stop being a blocker. Probe mode
        // (trackIndex < 0) is one self-reverting undo unit; errors return
        // in-band via the result's error field like the cases above.
        ProjectCommands::AuditionParams p;
        p.pluginId      = optString(o, "pluginId", "");
        p.programIndex  = optInt(o, "programIndex", -1, nullptr);
        p.trackIndex    = optInt(o, "trackIndex", -1, nullptr);
        p.slotIndex     = optInt(o, "slotIndex", 0, nullptr);
        p.style         = optString(o, "style", "Arpeggio");
        p.lengthBeats   = optDouble(o, "lengthBeats", 4.0, nullptr);
        p.density       = optInt(o, "density", 8, nullptr);
        p.noteDuration  = optDouble(o, "noteDuration", 0.5, nullptr);
        p.lowNote       = optInt(o, "lowNote", 48, nullptr);
        p.highNote      = optInt(o, "highNote", 84, nullptr);
        p.minVelocity   = optInt(o, "minVelocity", 60, nullptr);
        p.maxVelocity   = optInt(o, "maxVelocity", 110, nullptr);
        p.seed          = optInt<uint64_t>(o, "seed", 0, nullptr);
        p.windowSeconds = optDouble(o, "windowSeconds", 4.0, nullptr);
        p.keepTrack     = optBool(o, "keepTrack", false, nullptr);

        auto r = c.auditionPlugin(p);
        QJsonObject res{
            { "ok", r.ok },
            { "trackIndex", r.trackIndex },
            { "slotIndex", r.slotIndex },
            { "programIndex", r.programIndex },
            { "programName", QString::fromStdString(r.programName) },
            { "numPrograms", r.numPrograms },
            { "rms", static_cast<double>(r.rms) },
            { "peak", static_cast<double>(r.peak) },
            { "durationSeconds", r.durationSeconds },
            { "audible", r.audible }
        };
        if (!r.error.empty())
            res.insert("error", QString::fromStdString(r.error));
        return { false, res };
    }

    return makeError(-32601, "unknown composition method: " + m);
}

} // namespace frontend
