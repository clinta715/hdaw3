#include "Router_Composition.h"
#include "RouterHelpers.h"

#include "../../engine/AudioEngine.h"
#include "../../engine/PhraseGenerator.h"
#include "../../engine/ArrangementGenerator.h"
#include "../../engine/RhythmPatternGenerator.h"
#include "../../engine/PsytranceGenerator.h"
#include "../../engine/PsytranceMarkovGenerator.h"
#include "../../common/ProjectCommands.h"
#include "../../common/AudioGraphCommands.h"
#include "../../engine/PatternLibrary.h"
#include "../../engine/MidiAnalyzer.h"

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

    // Pattern library (initialized lazily)
    static HDAW::PatternLibrary patternLib(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("HDAW").getChildFile("patterns"));

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
        for (int i = 0; i < static_cast<int>(PhraseGenerator::NumStyles); ++i) {
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

    // --- Pattern Library ---

    if (m == "listPatterns") {
        std::string category, style, tag;
        if (o.contains("category")) category = o.value("category").toString().toStdString();
        if (o.contains("style"))    style    = o.value("style").toString().toStdString();
        if (o.contains("tag"))      tag      = o.value("tag").toString().toStdString();
        auto entries = patternLib.listPatterns(
            juce::String(category), juce::String(style), juce::String(tag));
        QJsonArray arr;
        for (const auto& e : entries) {
            QJsonArray tagsArr;
            for (const auto& t : e.tags) tagsArr.append(QString::fromStdString(t.toStdString()));
            arr.append(QJsonObject{
                { "id", QString::fromStdString(e.id.toStdString()) },
                { "name", QString::fromStdString(e.name.toStdString()) },
                { "style", QString::fromStdString(e.style.toStdString()) },
                { "category", QString::fromStdString(e.category.toStdString()) },
                { "tags", tagsArr },
                { "source", QString::fromStdString(e.source.toStdString()) }
            });
        }
        return { false, arr };
    }

    if (m == "savePattern") {
        std::string name, style, paramsStr, styleParamsStr;
        if (!requireString(o, "name", name, nullptr))
            return makeError(-32602, "name required");
        if (!requireString(o, "style", style, nullptr))
            return makeError(-32602, "style required");
        if (!requireString(o, "params", paramsStr, nullptr))
            return makeError(-32602, "params required");
        if (!requireString(o, "styleParams", styleParamsStr, nullptr))
            return makeError(-32602, "styleParams required");
        HDAW::PatternPreset preset;
        preset.name = juce::String(name);
        preset.style = juce::String(style);
        preset.paramsJson = juce::String(paramsStr);
        preset.styleParamsJson = juce::String(styleParamsStr);
        preset.description = optString(o, "description", "");
        preset.category = optString(o, "category", "user");
        if (o.contains("tags")) {
            auto tagsArr = o.value("tags").toArray();
            for (const auto& t : tagsArr)
                preset.tags.add(juce::String(t.toString().toStdString()));
        }
        juce::String err;
        if (!patternLib.savePattern(preset, err))
            return makeError(-32603, QString::fromStdString(err.toStdString()));
        return { false, QJsonObject{ { "id", QString::fromStdString(preset.name.toStdString()) }, { "success", true } } };
    }

    if (m == "loadPattern") {
        std::string id;
        if (!requireString(o, "id", id, nullptr))
            return makeError(-32602, "id required");
        HDAW::PatternPreset preset;
        juce::String err;
        if (!patternLib.loadPattern(juce::String(id), preset, err))
            return makeError(-32603, QString::fromStdString(err.toStdString()));
        QJsonArray tagsArr;
        for (const auto& t : preset.tags) tagsArr.append(QString::fromStdString(t.toStdString()));
        return { false, QJsonObject{
            { "name", QString::fromStdString(preset.name.toStdString()) },
            { "style", QString::fromStdString(preset.style.toStdString()) },
            { "params", QString::fromStdString(preset.paramsJson.toStdString()) },
            { "styleParams", QString::fromStdString(preset.styleParamsJson.toStdString()) },
            { "description", QString::fromStdString(preset.description.toStdString()) },
            { "tags", tagsArr }
        } };
    }

    if (m == "deletePattern") {
        std::string id;
        if (!requireString(o, "id", id, nullptr))
            return makeError(-32602, "id required");
        juce::String err;
        if (!patternLib.deletePattern(juce::String(id), err))
            return makeError(-32603, QString::fromStdString(err.toStdString()));
        return { false, QJsonObject{ { "success", true } } };
    }

    if (m == "importPattern") {
        std::string json;
        if (!requireString(o, "json", json, nullptr))
            return makeError(-32602, "json required");
        juce::String outId, err;
        if (!patternLib.importPattern(juce::String(json), outId, err))
            return makeError(-32603, QString::fromStdString(err.toStdString()));
        return { false, QJsonObject{ { "id", QString::fromStdString(outId.toStdString()) }, { "success", true } } };
    }

    if (m == "exportPattern") {
        std::string id;
        if (!requireString(o, "id", id, nullptr))
            return makeError(-32602, "id required");
        juce::String outJson, err;
        if (!patternLib.exportPattern(juce::String(id), outJson, err))
            return makeError(-32603, QString::fromStdString(err.toStdString()));
        return { false, QJsonObject{ { "json", QString::fromStdString(outJson.toStdString()) } } };
    }

    if (m == "getStyleParams") {
        std::string styleStr;
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
        else if (styleStr == "TrapHiHat")       style = PhraseGenerator::TrapHiHat;
        else if (styleStr == "DrillBass")       style = PhraseGenerator::DrillBass;
        else if (styleStr == "Counterpoint")    style = PhraseGenerator::Counterpoint;
        else if (styleStr == "WalkingBass")     style = PhraseGenerator::WalkingBass;
        else if (styleStr == "SwingComping")    style = PhraseGenerator::SwingComping;
        else if (styleStr == "MarkovMelody")    style = PhraseGenerator::MarkovMelody;
        else if (styleStr == "EvolvingTexture") style = PhraseGenerator::EvolvingTexture;
        else if (styleStr == "Aleatoric")       style = PhraseGenerator::Aleatoric;
        else if (styleStr == "ScalarRun")       style = PhraseGenerator::ScalarRun;
        else if (styleStr == "ChordToneSeq")    style = PhraseGenerator::ChordToneSeq;
        else if (styleStr == "CallResponse")    style = PhraseGenerator::CallResponse;
        else if (styleStr == "PhaseShift")      style = PhraseGenerator::PhaseShift;
        else if (styleStr == "AdditiveRhythm")  style = PhraseGenerator::AdditiveRhythm;
        else if (styleStr == "MinimalistLoop")  style = PhraseGenerator::MinimalistLoop;
        else if (styleStr == "Layered")         style = PhraseGenerator::Layered;
        auto schema = PhraseGenerator::getStyleParamsSchema(style);
        QJsonArray fields;
        for (const auto& f : schema) {
            fields.append(QJsonObject{
                { "name", QString::fromStdString(f.name) },
                { "type", QString::fromStdString(f.type) },
                { "min", f.min },
                { "max", f.max },
                { "default", f.defaultVal },
                { "label", QString::fromStdString(f.label) }
            });
        }
        return { false, QJsonObject{ { "fields", fields } } };
    }

    // --- MIDI Analysis ---

    if (m == "analyzeMidiFile") {
        std::string filePath;
        if (!requireString(o, "path", filePath, nullptr))
            return makeError(-32602, "path required");

        juce::File file{ juce::String(filePath) };
        if (!file.existsAsFile())
            return makeError(-32602, QStringLiteral("file does not exist: %1").arg(QString::fromStdString(filePath)));

        auto result = HDAW::MidiAnalyzer::analyze(file);
        if (result.trackCount == 0)
            return makeError(-32603, "no MIDI data found in file");

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
        else if (styleStr == "TrapHiHat")       style = PhraseGenerator::TrapHiHat;
        else if (styleStr == "DrillBass")       style = PhraseGenerator::DrillBass;
        else if (styleStr == "Counterpoint")    style = PhraseGenerator::Counterpoint;
        else if (styleStr == "WalkingBass")     style = PhraseGenerator::WalkingBass;
        else if (styleStr == "SwingComping")    style = PhraseGenerator::SwingComping;
        else if (styleStr == "MarkovMelody")    style = PhraseGenerator::MarkovMelody;
        else if (styleStr == "EvolvingTexture") style = PhraseGenerator::EvolvingTexture;
        else if (styleStr == "Aleatoric")       style = PhraseGenerator::Aleatoric;
        else if (styleStr == "ScalarRun")       style = PhraseGenerator::ScalarRun;
        else if (styleStr == "ChordToneSeq")    style = PhraseGenerator::ChordToneSeq;
        else if (styleStr == "CallResponse")    style = PhraseGenerator::CallResponse;
        else if (styleStr == "PhaseShift")      style = PhraseGenerator::PhaseShift;
        else if (styleStr == "AdditiveRhythm")  style = PhraseGenerator::AdditiveRhythm;
        else if (styleStr == "MinimalistLoop")  style = PhraseGenerator::MinimalistLoop;
        else if (styleStr == "Layered")         style = PhraseGenerator::Layered;

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

        // Parse styleParams for new styles
        if (o.contains("styleParams")) {
            auto sp = o.value("styleParams").toObject();
            switch (style) {
                case PhraseGenerator::TrapHiHat:
                    pp.trapHiHat.rollDensity = sp.value("rollDensity").toInt(pp.trapHiHat.rollDensity);
                    pp.trapHiHat.velocityDecay = sp.value("velocityDecay").toDouble(pp.trapHiHat.velocityDecay);
                    pp.trapHiHat.ratchetChance = sp.value("ratchetChance").toDouble(pp.trapHiHat.ratchetChance);
                    break;
                case PhraseGenerator::DrillBass:
                    pp.drillBass.glideDuration = sp.value("glideDuration").toDouble(pp.drillBass.glideDuration);
                    pp.drillBass.slideIntensity = sp.value("slideIntensity").toDouble(pp.drillBass.slideIntensity);
                    pp.drillBass.sustainTail = sp.value("sustainTail").toBool(pp.drillBass.sustainTail);
                    pp.drillBass.displacement = sp.value("displacement").toDouble(pp.drillBass.displacement);
                    break;
                case PhraseGenerator::Counterpoint:
                    pp.counterpoint.voiceCount = sp.value("voiceCount").toInt(pp.counterpoint.voiceCount);
                    pp.counterpoint.species = sp.value("species").toInt(pp.counterpoint.species);
                    pp.counterpoint.intervalConstraint = sp.value("intervalConstraint").toInt(pp.counterpoint.intervalConstraint);
                    break;
                case PhraseGenerator::WalkingBass:
                    pp.walkingBass.approachNotes = sp.value("approachNotes").toBool(pp.walkingBass.approachNotes);
                    pp.walkingBass.ghostNotes = sp.value("ghostNotes").toDouble(pp.walkingBass.ghostNotes);
                    pp.walkingBass.chromaticism = sp.value("chromaticism").toDouble(pp.walkingBass.chromaticism);
                    break;
                case PhraseGenerator::SwingComping:
                    pp.swingComping.swingPercent = sp.value("swingPercent").toInt(pp.swingComping.swingPercent);
                    pp.swingComping.compPattern = sp.value("compPattern").toInt(pp.swingComping.compPattern);
                    pp.swingComping.voicingSpread = sp.value("voicingSpread").toInt(pp.swingComping.voicingSpread);
                    break;
                case PhraseGenerator::MarkovMelody:
                    pp.markovMelody.rhythmGrid = sp.value("rhythmGrid").toInt(pp.markovMelody.rhythmGrid);
                    pp.markovMelody.stateCount = sp.value("stateCount").toInt(pp.markovMelody.stateCount);
                    break;
                case PhraseGenerator::EvolvingTexture:
                    pp.evolvingTexture.layerCount = sp.value("layerCount").toInt(pp.evolvingTexture.layerCount);
                    pp.evolvingTexture.driftSpeed = sp.value("driftSpeed").toDouble(pp.evolvingTexture.driftSpeed);
                    pp.evolvingTexture.densitySwell = sp.value("densitySwell").toDouble(pp.evolvingTexture.densitySwell);
                    break;
                case PhraseGenerator::Aleatoric:
                    pp.aleatoric.constraintTightness = sp.value("constraintTightness").toDouble(pp.aleatoric.constraintTightness);
                    pp.aleatoric.rhythmVariety = sp.value("rhythmVariety").toDouble(pp.aleatoric.rhythmVariety);
                    pp.aleatoric.restProbability = sp.value("restProbability").toDouble(pp.aleatoric.restProbability);
                    break;
                case PhraseGenerator::ScalarRun:
                    pp.scalarRun.direction = sp.value("direction").toInt(pp.scalarRun.direction);
                    pp.scalarRun.octaveSpan = sp.value("octaveSpan").toInt(pp.scalarRun.octaveSpan);
                    pp.scalarRun.runSpeed = sp.value("runSpeed").toInt(pp.scalarRun.runSpeed);
                    break;
                case PhraseGenerator::ChordToneSeq:
                    pp.chordToneSeq.approachType = sp.value("approachType").toInt(pp.chordToneSeq.approachType);
                    pp.chordToneSeq.patternShape = sp.value("patternShape").toInt(pp.chordToneSeq.patternShape);
                    break;
                case PhraseGenerator::CallResponse:
                    pp.callResponse.phraseLength = sp.value("phraseLength").toInt(pp.callResponse.phraseLength);
                    pp.callResponse.responseVariation = sp.value("responseVariation").toDouble(pp.callResponse.responseVariation);
                    pp.callResponse.restBeats = sp.value("restBeats").toDouble(pp.callResponse.restBeats);
                    break;
                case PhraseGenerator::PhaseShift:
                    pp.phaseShift.voice1Grid = sp.value("voice1Grid").toInt(pp.phaseShift.voice1Grid);
                    pp.phaseShift.voice2Grid = sp.value("voice2Grid").toInt(pp.phaseShift.voice2Grid);
                    pp.phaseShift.phaseRate = sp.value("phaseRate").toDouble(pp.phaseShift.phaseRate);
                    break;
                case PhraseGenerator::AdditiveRhythm:
                    pp.additiveRhythm.grouping = sp.value("grouping").toString(QString::fromStdString(pp.additiveRhythm.grouping)).toStdString();
                    pp.additiveRhythm.subdivision = sp.value("subdivision").toInt(pp.additiveRhythm.subdivision);
                    break;
                case PhraseGenerator::MinimalistLoop:
                    pp.minimalistLoop.cellLength = sp.value("cellLength").toInt(pp.minimalistLoop.cellLength);
                    pp.minimalistLoop.mutationRate = sp.value("mutationRate").toDouble(pp.minimalistLoop.mutationRate);
                    pp.minimalistLoop.phaseOffset = sp.value("phaseOffset").toInt(pp.minimalistLoop.phaseOffset);
                    break;
                default:
                    break;
            }
        }

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

        QJsonArray tracks, clips, roles;
        for (int t : result.trackIndices) tracks.append(t);
        for (int ci : result.clipIds) clips.append(ci);
        for (const auto& rn : result.roleNames) roles.append(QString::fromStdString(rn));
        QJsonObject res{
            { "trackIndices", tracks },
            { "clipIds", clips },
            { "roleNames", roles },
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
        p.style = optString(o, "style", "");
        p.role  = optString(o, "role", "");
        if (p.style.empty() && p.role.empty())
            return makeError(-32602, "style required (or provide role)");
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
        p.allowGlobalScale = optBool(o, "allowGlobalScale", false, nullptr);
        if (o.contains("style"))            p.explicitMask |= ProjectCommands::kRoleBitStyle;
        if (o.contains("lowNote"))          p.explicitMask |= ProjectCommands::kRoleBitLowNote;
        if (o.contains("highNote"))         p.explicitMask |= ProjectCommands::kRoleBitHighNote;
        if (o.contains("density"))          p.explicitMask |= ProjectCommands::kRoleBitDensity;
        if (o.contains("noteDuration"))     p.explicitMask |= ProjectCommands::kRoleBitNoteDuration;
        if (o.contains("minVelocity"))      p.explicitMask |= ProjectCommands::kRoleBitMinVelocity;
        if (o.contains("maxVelocity"))      p.explicitMask |= ProjectCommands::kRoleBitMaxVelocity;
        if (o.contains("targetRms"))        p.explicitMask |= ProjectCommands::kRoleBitTargetRms;
        if (o.contains("allowGlobalScale")) p.explicitMask |= ProjectCommands::kRoleBitAllowGlobalScale;

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
                { "clamped", r.gain.clamped },
                { "globalScale", static_cast<double>(r.gain.globalScale) },
                { "masterGain", static_cast<double>(r.gain.masterGain) },
                { "mixPeak", static_cast<double>(r.gain.mixPeak) }
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
        const bool allowGlobalScale = optBool(o, "allowGlobalScale", false, nullptr);

        auto r = c.autoGainToTarget(trackIndex, targetRms, windowSeconds, verify, allowGlobalScale);
        QJsonObject res{
            { "ok", r.ok },
            { "fader", static_cast<double>(r.fader) },
            { "measuredRms", static_cast<double>(r.measuredRms) },
            { "peak", static_cast<double>(r.peak) },
            { "clamped", r.clamped },
            { "globalScale", static_cast<double>(r.globalScale) },
            { "masterGain", static_cast<double>(r.masterGain) },
            { "mixPeak", static_cast<double>(r.mixPeak) }
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

    if (m == "verifyPart") {
        // Self-verify a composed part: solo + full-mix render of the track's
        // window. Synchronous RPC that blocks for both renders. Errors return
        // in-band via the result's error field like the cases above.
        int trackIndex;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        const double windowSeconds = optDouble(o, "windowSeconds", 4.0, nullptr);

        auto r = c.verifyPart(trackIndex, windowSeconds);
        QJsonObject res{
            { "ok", r.ok },
            { "soloRms", static_cast<double>(r.soloRms) },
            { "soloPeak", static_cast<double>(r.soloPeak) },
            { "mixRms", static_cast<double>(r.mixRms) },
            { "mixPeak", static_cast<double>(r.mixPeak) },
            { "nonClipping", r.nonClipping },
            { "audible", r.audible },
            { "bandsPresent", r.bandsPresent },
            { "bandLow", r.bandLow },
            { "bandMid", r.bandMid },
            { "bandHigh", r.bandHigh },
            { "windowStart", r.windowStart },
            { "durationSeconds", r.durationSeconds }
        };
        if (!r.error.empty())
            res.insert("error", QString::fromStdString(r.error));
        return { false, res };
    }

    if (m == "generatePsytrance") {
        HDAW::PsytranceParams p;
        p.keyRoot = optInt(o, "keyRoot", 0, nullptr);
        p.scaleMode = optInt(o, "scaleMode", 1, nullptr);
        p.density = optDouble(o, "density", 0.7, nullptr);
        p.seed = optInt<uint64_t>(o, "seed", 0, nullptr);
        if (o.contains("sections") && o.value("sections").isArray())
        {
            for (const auto& sv : o.value("sections").toArray())
            {
                const auto so = sv.toObject();
                HDAW::PsytranceSection s;
                s.name  = so.value("name").toString().toStdString();
                s.start = so.value("start").toDouble(0.0);
                s.end   = so.value("end").toDouble(0.0);
                p.sections.push_back(s);
            }
        }
        if (o.contains("progressionA") && o.value("progressionA").isArray())
            for (const auto& v : o.value("progressionA").toArray()) p.progressionA.push_back(v.toInt());
        if (o.contains("progressionB") && o.value("progressionB").isArray())
            for (const auto& v : o.value("progressionB").toArray()) p.progressionB.push_back(v.toInt());
        if (o.contains("paletteTrackIds") && o.value("paletteTrackIds").isObject())
        {
            const auto obj = o.value("paletteTrackIds").toObject();
            auto set = [&](const QString& role, int& out) { if (obj.contains(role)) out = obj.value(role).toInt(-1); };
            set("kick", p.kick);     set("bass", p.bass);
            set("hat", p.hat);       set("arp", p.arp);
            set("stab", p.stab);     set("pad", p.pad);
            set("riser", p.riser);   set("down", p.down);
            set("clap", p.clap);
        }
        auto r = c.generatePsytrance(p);
        QJsonArray clips;
        for (const auto& rc : r.clips)
            clips.append(QJsonObject{
                { "role", QString::fromStdString(rc.role) },
                { "trackId", rc.trackIndex },
                { "clipId", rc.clipId },
                { "noteCount", rc.noteCount } });
        QJsonArray skipped;
        for (const auto& s : r.skippedRoles) skipped.append(QString::fromStdString(s));
        QJsonObject res{
            { "clips", clips },
            { "skipped", skipped },
            { "totalBeats", r.totalBeats },
            { "notesTotal", r.notesTotal },
            { "notesSkipped", r.notesSkipped }
        };
        if (!r.error.empty())
            res["error"] = QString::fromStdString(r.error);
        return { false, res };
    }

    if (m == "generatePsytranceMarkov") {
        HDAW::PsytranceMarkovParams p;
        p.keyRoot = optInt(o, "keyRoot", 0, nullptr);
        p.scaleMode = optInt(o, "scaleMode", 1, nullptr);
        p.density = optDouble(o, "density", 0.7, nullptr);
        p.seed = optInt<uint64_t>(o, "seed", 0, nullptr);
        p.totalBars = optInt(o, "totalBars", 32, nullptr);
        p.minTracks = optInt(o, "minTracks", 2, nullptr);
        p.maxTracks = optInt(o, "maxTracks", 6, nullptr);
        p.minPercTracks = optInt(o, "minPercTracks", 1, nullptr);
        p.maxPercTracks = optInt(o, "maxPercTracks", 3, nullptr);
        p.everyBars = optInt(o, "everyBars", 32, nullptr);
        p.sectionCycleBars = optInt(o, "sectionCycleBars", 32, nullptr);
        p.keyShiftDegrees = optInt(o, "keyShiftDegrees", 0, nullptr);
        if (o.contains("progressionA") && o.value("progressionA").isArray())
            for (const auto& v : o.value("progressionA").toArray()) p.progressionA.push_back(v.toInt());
        if (o.contains("progressionB") && o.value("progressionB").isArray())
            for (const auto& v : o.value("progressionB").toArray()) p.progressionB.push_back(v.toInt());
        if (o.contains("paletteTrackIds") && o.value("paletteTrackIds").isObject())
        {
            const auto obj = o.value("paletteTrackIds").toObject();
            auto set = [&](const QString& role, int& out) { if (obj.contains(role)) out = obj.value(role).toInt(-1); };
            set("kick", p.kick);     set("bass", p.bass);
            set("hat", p.hat);       set("snare", p.snare);
            set("rim", p.rim);       set("arp", p.arp);
            set("stab", p.stab);     set("pad", p.pad);
            set("riser", p.riser);   set("down", p.down);
            set("clap", p.clap);
        }
        if (o.contains("sections"))
        {
            if (!o.value("sections").isArray())
                return makeError(-32602, "sections must be an array");
            for (const auto& v : o.value("sections").toArray())
            {
                if (!v.isObject())
                    return makeError(-32602, "sections entries must be objects");
                const auto so = v.toObject();
                HDAW::MarkovSectionSpec spec;
                spec.type = optString(so, "type", "");
                spec.bars = optInt(so, "bars", 8, nullptr);
                spec.minTracks = optInt(so, "minTracks", -1, nullptr);
                spec.maxTracks = optInt(so, "maxTracks", -1, nullptr);
                spec.minPercTracks = optInt(so, "minPercTracks", -1, nullptr);
                spec.maxPercTracks = optInt(so, "maxPercTracks", -1, nullptr);
                p.sections.push_back(spec);
            }
        }
        auto r = c.generatePsytranceMarkov(p);
        QJsonArray clips;
        for (const auto& rc : r.clips)
            clips.append(QJsonObject{
                { "role", QString::fromStdString(rc.role) },
                { "trackId", rc.trackIndex },
                { "clipId", rc.clipId },
                { "noteCount", rc.noteCount } });
        QJsonArray skipped;
        for (const auto& s : r.skippedRoles) skipped.append(QString::fromStdString(s));
        QJsonArray steps;
        for (const auto& s : r.steps)
        {
            QJsonArray roles;
            for (const auto& role : s.activeRoles) roles.append(QString::fromStdString(role));
            QJsonArray ages;
            for (const auto& a : s.ages) ages.append(a);
            steps.append(QJsonObject{
                { "barStart", s.barStart },
                { "action", QString::fromStdString(s.action) },
                { "targetRole", QString::fromStdString(s.targetRole) },
                { "activeRoles", roles },
                { "ages", ages },
                { "keyRoot", s.keyRoot },
                { "section", QString::fromStdString(s.section) } });
        }
        QJsonArray automations;
        for (const auto& a : r.automations)
            automations.append(QJsonObject{
                { "role", QString::fromStdString(a.role) },
                { "param", QString::fromStdString(a.param) },
                { "startBeat", a.startBeat },
                { "value", a.value },
                { "durationBeats", a.durationBeats } });
        QJsonObject res{
            { "clips", clips },
            { "skipped", skipped },
            { "steps", steps },
            { "automations", automations },
            { "totalBeats", r.totalBeats },
            { "notesTotal", r.notesTotal },
            { "notesSkipped", r.notesSkipped },
            { "automationsSkipped", r.automationsSkipped }
        };
        if (!r.error.empty())
            res["error"] = QString::fromStdString(r.error);
        return { false, res };
    }

    return makeError(-32601, "unknown composition method: " + m);
}

} // namespace frontend
