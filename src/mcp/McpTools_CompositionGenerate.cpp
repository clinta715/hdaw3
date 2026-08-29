#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/PluginManager.h"
#include "../engine/Track.h"
#include "../engine/PhraseGenerator.h"
#include "../engine/ArrangementGenerator.h"
#include "engine/RhythmPatternGenerator.h"
#include "../engine/PatternLibrary.h"
#include "../engine/MidiAnalyzer.h"
#include "../engine/ProjectSerializer.h"
#include "../engine/ProjectBackup.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace mcp {

void registerGenerateTools(McpServer& s, AudioEngine* e)
{

    static const std::pair<const char*, PhraseGenerator::Style> kStyleMap[] = {
        {"Standard",   PhraseGenerator::Standard},
        {"Arpeggio",   PhraseGenerator::Arpeggio},
        {"BassLine",   PhraseGenerator::BassLine},
        {"ChordStab",  PhraseGenerator::ChordStab},
        {"Pad",        PhraseGenerator::Pad},
        {"Lead",       PhraseGenerator::Lead},
        {"RandomWalk", PhraseGenerator::RandomWalk},
        {"Buildup",    PhraseGenerator::Buildup},
        {"Euclidean",  PhraseGenerator::Euclidean},
        {"Percussion", PhraseGenerator::Percussion},
        {"TrapHiHat",       PhraseGenerator::TrapHiHat},
        {"DrillBass",       PhraseGenerator::DrillBass},
        {"Counterpoint",    PhraseGenerator::Counterpoint},
        {"WalkingBass",     PhraseGenerator::WalkingBass},
        {"SwingComping",    PhraseGenerator::SwingComping},
        {"MarkovMelody",    PhraseGenerator::MarkovMelody},
        {"EvolvingTexture", PhraseGenerator::EvolvingTexture},
        {"Aleatoric",       PhraseGenerator::Aleatoric},
        {"ScalarRun",       PhraseGenerator::ScalarRun},
        {"ChordToneSeq",    PhraseGenerator::ChordToneSeq},
        {"CallResponse",    PhraseGenerator::CallResponse},
        {"PhaseShift",      PhraseGenerator::PhaseShift},
        {"AdditiveRhythm",  PhraseGenerator::AdditiveRhythm},
        {"MinimalistLoop",  PhraseGenerator::MinimalistLoop},
        {"Layered",         PhraseGenerator::Layered}
    };

    auto generateIntoClip = [e](int trackId, double start, double length,
                                const std::vector<PhraseGenerator::GeneratedNote>& notes) -> McpToolResult {
        auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
        auto tl = m.getTrackListTree();
        if (trackId < 0 || trackId >= tl.getNumChildren())
            return McpToolResult::text("track not found", true);
        double bpm = e->getReadModel().getTransport().bpm;
        double startSec = HDAW::beatsToSeconds(start, bpm);
        double durSec = HDAW::beatsToSeconds(length, bpm);
        auto c = m.createMidiClipEmpty("Generated", startSec, durSec);
        c.setProperty(IDs::color, static_cast<int>(ProjectModel::trackColorForIndex(trackId)), nullptr);
        auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
        for (const auto& gn : notes)
            nl.addChild(m.createMidiNote(gn.noteNumber, static_cast<float>(gn.velocity) / 127.0f, gn.startBeat, gn.durationBeats), -1, nullptr);
        int cid = static_cast<int>(c.getProperty(IDs::clipID));
        tl.getChild(trackId).getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
        return McpToolResult::text(QString("clipId=%1 notes=%2").arg(cid).arg((int) notes.size()));
    };

s.registerTool({"generate_phrase", "Generate a phrase into a new clip on the given track.",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"style",       QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"Standard","Arpeggio","BassLine","ChordStab","Pad","Lead","RandomWalk","Buildup","Euclidean","Percussion","TrapHiHat","DrillBass","Counterpoint","WalkingBass","SwingComping","MarkovMelody","EvolvingTexture","Aleatoric","ScalarRun","ChordToneSeq","CallResponse","PhaseShift","AdditiveRhythm","MinimalistLoop","Layered"}},
                      {"description","Phrase style."}}},
                  {"styleParams", QJsonObject{{"type","object"},{"description","Style-specific parameters. See getStyleParamsSchema for per-style fields."}}},
                  {"length",      QJsonObject{{"type","number"}}},
                  {"density",     QJsonObject{{"type","integer"}}},
                  {"start",       QJsonObject{{"type","number"}}},
                  {"lowNote",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"highNote",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"noteDuration",QJsonObject{{"type","number"}}},
                  {"minVelocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"maxVelocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"scaleRoot",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"scaleMode",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}},
                  {"seed",        QJsonObject{{"type","integer"},{"minimum",0}}}},
                 {"trackId","style","length","density"}),
        "composition",
        [e, helper = generateIntoClip](const QJsonObject& a) -> McpToolResult {
            PhraseGenerator::PhraseParams p;
            QString sname = a.value("style").toString();
            for (const auto& kv : kStyleMap)
                if (sname == kv.first) p.style = kv.second;
            p.lengthBeats = a.value("length").toDouble();
            p.density = a.value("density").toInt();
            p.lowNote = a.contains("lowNote") ? a.value("lowNote").toInt() : 48;
            p.highNote = a.contains("highNote") ? a.value("highNote").toInt() : 84;
            p.noteDuration = a.contains("noteDuration") ? a.value("noteDuration").toDouble() : 0.5;
            p.minVelocity = a.contains("minVelocity") ? a.value("minVelocity").toInt() : 60;
            p.maxVelocity = a.contains("maxVelocity") ? a.value("maxVelocity").toInt() : 110;
            p.scaleRoot = a.contains("scaleRoot") ? a.value("scaleRoot").toInt() : e->getProjectModel().getScaleRoot();
            p.scaleMode = a.contains("scaleMode") ? a.value("scaleMode").toInt() : e->getProjectModel().getScaleMode();
            p.seed = a.contains("seed") ? static_cast<uint64_t>(a.value("seed").toDouble()) : 0;
            if (a.contains("styleParams")) {
                auto sp = a.value("styleParams").toObject();
                switch (p.style) {
                    case PhraseGenerator::TrapHiHat:
                        p.trapHiHat.rollDensity = sp.value("rollDensity").toInt(p.trapHiHat.rollDensity);
                        p.trapHiHat.velocityDecay = sp.value("velocityDecay").toDouble(p.trapHiHat.velocityDecay);
                        p.trapHiHat.ratchetChance = sp.value("ratchetChance").toDouble(p.trapHiHat.ratchetChance);
                        break;
                    case PhraseGenerator::DrillBass:
                        p.drillBass.glideDuration = sp.value("glideDuration").toDouble(p.drillBass.glideDuration);
                        p.drillBass.slideIntensity = sp.value("slideIntensity").toDouble(p.drillBass.slideIntensity);
                        p.drillBass.sustainTail = sp.value("sustainTail").toBool(p.drillBass.sustainTail);
                        p.drillBass.displacement = sp.value("displacement").toDouble(p.drillBass.displacement);
                        break;
                    case PhraseGenerator::Counterpoint:
                        p.counterpoint.voiceCount = sp.value("voiceCount").toInt(p.counterpoint.voiceCount);
                        p.counterpoint.species = sp.value("species").toInt(p.counterpoint.species);
                        p.counterpoint.intervalConstraint = sp.value("intervalConstraint").toInt(p.counterpoint.intervalConstraint);
                        break;
                    case PhraseGenerator::WalkingBass:
                        p.walkingBass.approachNotes = sp.value("approachNotes").toBool(p.walkingBass.approachNotes);
                        p.walkingBass.ghostNotes = sp.value("ghostNotes").toDouble(p.walkingBass.ghostNotes);
                        p.walkingBass.chromaticism = sp.value("chromaticism").toDouble(p.walkingBass.chromaticism);
                        break;
                    case PhraseGenerator::SwingComping:
                        p.swingComping.swingPercent = sp.value("swingPercent").toInt(p.swingComping.swingPercent);
                        p.swingComping.compPattern = sp.value("compPattern").toInt(p.swingComping.compPattern);
                        p.swingComping.voicingSpread = sp.value("voicingSpread").toInt(p.swingComping.voicingSpread);
                        break;
                    case PhraseGenerator::MarkovMelody:
                        p.markovMelody.rhythmGrid = sp.value("rhythmGrid").toInt(p.markovMelody.rhythmGrid);
                        p.markovMelody.stateCount = sp.value("stateCount").toInt(p.markovMelody.stateCount);
                        break;
                    case PhraseGenerator::EvolvingTexture:
                        p.evolvingTexture.layerCount = sp.value("layerCount").toInt(p.evolvingTexture.layerCount);
                        p.evolvingTexture.driftSpeed = sp.value("driftSpeed").toDouble(p.evolvingTexture.driftSpeed);
                        p.evolvingTexture.densitySwell = sp.value("densitySwell").toDouble(p.evolvingTexture.densitySwell);
                        break;
                    case PhraseGenerator::Aleatoric:
                        p.aleatoric.constraintTightness = sp.value("constraintTightness").toDouble(p.aleatoric.constraintTightness);
                        p.aleatoric.rhythmVariety = sp.value("rhythmVariety").toDouble(p.aleatoric.rhythmVariety);
                        p.aleatoric.restProbability = sp.value("restProbability").toDouble(p.aleatoric.restProbability);
                        break;
                    case PhraseGenerator::ScalarRun:
                        p.scalarRun.direction = sp.value("direction").toInt(p.scalarRun.direction);
                        p.scalarRun.octaveSpan = sp.value("octaveSpan").toInt(p.scalarRun.octaveSpan);
                        p.scalarRun.runSpeed = sp.value("runSpeed").toInt(p.scalarRun.runSpeed);
                        break;
                    case PhraseGenerator::ChordToneSeq:
                        p.chordToneSeq.approachType = sp.value("approachType").toInt(p.chordToneSeq.approachType);
                        p.chordToneSeq.patternShape = sp.value("patternShape").toInt(p.chordToneSeq.patternShape);
                        break;
                    case PhraseGenerator::CallResponse:
                        p.callResponse.phraseLength = sp.value("phraseLength").toInt(p.callResponse.phraseLength);
                        p.callResponse.responseVariation = sp.value("responseVariation").toDouble(p.callResponse.responseVariation);
                        p.callResponse.restBeats = sp.value("restBeats").toDouble(p.callResponse.restBeats);
                        break;
                    case PhraseGenerator::PhaseShift:
                        p.phaseShift.voice1Grid = sp.value("voice1Grid").toInt(p.phaseShift.voice1Grid);
                        p.phaseShift.voice2Grid = sp.value("voice2Grid").toInt(p.phaseShift.voice2Grid);
                        p.phaseShift.phaseRate = sp.value("phaseRate").toDouble(p.phaseShift.phaseRate);
                        break;
                    case PhraseGenerator::AdditiveRhythm:
                        p.additiveRhythm.grouping = sp.value("grouping").toString(QString::fromStdString(p.additiveRhythm.grouping)).toStdString();
                        p.additiveRhythm.subdivision = sp.value("subdivision").toInt(p.additiveRhythm.subdivision);
                        break;
                    case PhraseGenerator::MinimalistLoop:
                        p.minimalistLoop.cellLength = sp.value("cellLength").toInt(p.minimalistLoop.cellLength);
                        p.minimalistLoop.mutationRate = sp.value("mutationRate").toDouble(p.minimalistLoop.mutationRate);
                        p.minimalistLoop.phaseOffset = sp.value("phaseOffset").toInt(p.minimalistLoop.phaseOffset);
                        break;
                    default:
                        break;
                }
            }
            auto notes = PhraseGenerator::generatePhrase(p);
            if (notes.empty())
                return McpToolResult::text("phrase generator produced 0 notes — check scale/range settings", true);
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0),
                          a.value("length").toDouble(), notes);
        }});

s.registerTool({"generate_chord", "Generate a chord (or arpeggio) into a new clip.",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"rootPitch",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"chordType",   QJsonObject{{"type","integer"}}},
                  {"voicing",     QJsonObject{{"type","integer"}}},
                  {"inversion",   QJsonObject{{"type","integer"}}},
                  {"arpeggiate",  QJsonObject{{"type","boolean"}}},
                  {"start",       QJsonObject{{"type","number"}}},
                  {"length",      QJsonObject{{"type","number"}}},
                  {"arpeggioRate",QJsonObject{{"type","number"}}},
                  {"lowNote",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"highNote",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"minVelocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"maxVelocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"scaleRoot",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"scaleMode",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}},
                  {"seed",        QJsonObject{{"type","integer"},{"minimum",0}}}},
                 {"trackId","rootPitch","chordType","length"}),
        "composition",
        [e, helper = generateIntoClip](const QJsonObject& a) -> McpToolResult {
            PhraseGenerator::ChordParams p;
            p.chordType = a.value("chordType").toInt();
            p.voicing = a.value("voicing").toInt(0);
            p.inversion = a.value("inversion").toInt(0);
            p.arpeggiate = a.contains("arpeggiate") ? a.value("arpeggiate").toBool() : false;
            p.arpeggioRate = a.contains("arpeggioRate") ? a.value("arpeggioRate").toDouble() : 0.125;
            p.durationBeats = a.value("length").toDouble();
            p.lowNote = a.contains("lowNote") ? a.value("lowNote").toInt() : 24;
            p.highNote = a.contains("highNote") ? a.value("highNote").toInt() : 96;
            p.minVelocity = a.contains("minVelocity") ? a.value("minVelocity").toInt() : 60;
            p.maxVelocity = a.contains("maxVelocity") ? a.value("maxVelocity").toInt() : 110;
            p.scaleRoot = a.contains("scaleRoot") ? a.value("scaleRoot").toInt() : e->getProjectModel().getScaleRoot();
            p.scaleMode = a.contains("scaleMode") ? a.value("scaleMode").toInt() : e->getProjectModel().getScaleMode();
            p.seed = a.contains("seed") ? static_cast<uint64_t>(a.value("seed").toDouble()) : 0;
            auto notes = PhraseGenerator::generateChord(a.value("rootPitch").toInt(), p);
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0),
                          a.value("length").toDouble(), notes);
        }});

s.registerTool({"generate_progression", "Generate a chord progression into a new clip.",
        objSchema({{"trackId",          QJsonObject{{"type","integer"}}},
                  {"pattern",          QJsonObject{{"type","integer"}}},
                  {"beatsPerChord",    QJsonObject{{"type","number"}}},
                  {"start",            QJsonObject{{"type","number"}}},
                  {"chordTypeOverride", QJsonObject{{"type","integer"}}},
                  {"arpeggiate",       QJsonObject{{"type","boolean"}}},
                  {"arpeggioRate",     QJsonObject{{"type","number"}}},
                  {"durationBeats",    QJsonObject{{"type","number"}}},
                  {"lowNote",          QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"highNote",         QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"minVelocity",      QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"maxVelocity",      QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"scaleRoot",        QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"scaleMode",        QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}},
                  {"seed",             QJsonObject{{"type","integer"},{"minimum",0}}}},
                 {"trackId","pattern","beatsPerChord"}),
        "composition",
        [e, helper = generateIntoClip](const QJsonObject& a) -> McpToolResult {
            PhraseGenerator::ProgressionParams p;
            p.patternIndex = a.value("pattern").toInt();
            p.beatsPerChord = a.value("beatsPerChord").toDouble();
            p.chordTypeOverride = a.contains("chordTypeOverride") ? a.value("chordTypeOverride").toInt() : -1;
            p.arpeggiate = a.contains("arpeggiate") ? a.value("arpeggiate").toBool() : false;
            p.arpeggioRate = a.contains("arpeggioRate") ? a.value("arpeggioRate").toDouble() : 0.125;
            p.durationBeats = a.contains("durationBeats") ? a.value("durationBeats").toDouble() : 2.0;
            p.lowNote = a.contains("lowNote") ? a.value("lowNote").toInt() : 24;
            p.highNote = a.contains("highNote") ? a.value("highNote").toInt() : 96;
            p.minVelocity = a.contains("minVelocity") ? a.value("minVelocity").toInt() : 60;
            p.maxVelocity = a.contains("maxVelocity") ? a.value("maxVelocity").toInt() : 110;
            p.scaleRoot = a.contains("scaleRoot") ? a.value("scaleRoot").toInt() : e->getProjectModel().getScaleRoot();
            p.scaleMode = a.contains("scaleMode") ? a.value("scaleMode").toInt() : e->getProjectModel().getScaleMode();
            p.seed = a.contains("seed") ? static_cast<uint64_t>(a.value("seed").toDouble()) : 0;
            auto notes = PhraseGenerator::generateProgression(p);
            const auto& pats = PhraseGenerator::getProgressionPatterns();
            int patIdx = std::clamp(p.patternIndex, 0, (int)pats.size() - 1);
            double total = p.beatsPerChord * pats[patIdx].chords.size();
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0), total, notes);
        }});

s.registerTool({"generate_rhythm_pattern", "Generate a drum/percussion rhythm pattern into a new MIDI clip: two euclidean pulses (polyrhythm, e.g. 4-over-3) plus an optional rhythm-DSL voice ('x' '-' '[..]xN' 'E(k,n[,rot])'). Pure function of its params (no seed).",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"start",       QJsonObject{{"type","number"}}},
                  {"grid",        QJsonObject{{"type","integer"},{"minimum",1},{"maximum",64},{"description","Steps per bar (16 = 16th notes). Notes align to beat grid positions."}}},
                  {"bars",        QJsonObject{{"type","integer"},{"minimum",1},{"description","Number of bars (not seconds). Each bar = 4 beats."}}},
                  {"pulseA",      QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"pulseB",      QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"rotationA",   QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"rotationB",   QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"pitchA",      QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"pitchB",      QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"velocityA",   QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"velocityB",   QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"dsl",         QJsonObject{{"type","string"}}},
                  {"dslPitch",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"dslVelocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}},
                 {"trackId"}),
        "composition",
        [e, helper = generateIntoClip](const QJsonObject& a) -> McpToolResult {
            RhythmPatternGenerator::Params p;
            p.grid        = a.value("grid").toInt(16);
            p.bars        = a.value("bars").toInt(1);
            p.pulseA      = a.contains("pulseA") ? a.value("pulseA").toInt() : 4;
            p.pulseB      = a.contains("pulseB") ? a.value("pulseB").toInt() : 3;
            p.rotationA   = a.contains("rotationA") ? a.value("rotationA").toInt() : 1;
            p.rotationB   = a.contains("rotationB") ? a.value("rotationB").toInt() : 1;
            p.pitchA      = a.contains("pitchA") ? a.value("pitchA").toInt() : 36;
            p.pitchB      = a.contains("pitchB") ? a.value("pitchB").toInt() : 42;
            p.velocityA   = a.contains("velocityA") ? a.value("velocityA").toInt() : 112;
            p.velocityB   = a.contains("velocityB") ? a.value("velocityB").toInt() : 96;
            p.dsl         = a.contains("dsl") ? a.value("dsl").toString().toStdString() : std::string();
            p.dslPitch    = a.contains("dslPitch") ? a.value("dslPitch").toInt() : 39;
            p.dslVelocity = a.contains("dslVelocity") ? a.value("dslVelocity").toInt() : 104;

            std::vector<RhythmPatternGenerator::Note> notes;
            try { notes = RhythmPatternGenerator::generate(p); }
            catch (const std::invalid_argument& ex)
            { return McpToolResult::text(QString("dsl error: ") + ex.what(), true); }
            if (notes.empty())
                return McpToolResult::text("pattern produced no notes", true);

            std::vector<PhraseGenerator::GeneratedNote> converted;
            converted.reserve(notes.size());
            for (const auto& n : notes)
                converted.push_back({ n.startBeat, n.pitch, n.velocity, n.durationBeats });
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0),
                          (double) std::max(1, p.bars) * 4.0, converted);
        }});

    s.registerTool({"generate_chopped_break",
        "Compose a MIDI break pattern from a sliced sampler sample and write it into an existing MIDI clip. Requires the sampler slot to have DETECTED slices: set_sampler_mode {mode:'slice'} + detect_sampler_slices first, or the tool errors with 'no slices: run detect_sampler_slices first'. Reads the slot's slicePoints + baseNote from sampler state (baseNote default 60, configurable via set_sampler_param baseNote). Slice trigger mapping: slice i sounds at MIDI note baseNote+i, so the written notes trigger the detected slices when the clip plays. Styles: amen (Think-break skeleton: kicks 0/8, snares 4/12, 16th fills 14/15, off-beat ghosts), twoStep (kicks 0/9, snares 4/12, sparse), halftime (kick 0 + 13 clutch, snare 8, minimal), jungleEdit (amen skeleton + per-bar seeded slice-swap on the 14/15 fills + first kick always dropped), random (seeded random walk over slice indices). Returns {added, firstPitch, lastPitch, sliceCount, baseNote}.",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"slotIndex",   QJsonObject{{"type","integer"}}},
                  {"clipId",      QJsonObject{{"type","integer"}}},
                  {"bars",        QJsonObject{{"type","integer"},{"minimum",1},{"maximum",64},{"description","Number of 4/4 bars (beats = bars*4)."}}},
                  {"grid",        QJsonObject{{"type","integer"},{"minimum",1},{"maximum",8},{"description","Output steps per beat: 1=quarter, 2=8th, 4=16th (default), 8=32nds."}}},
                  {"style",       QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"amen","twoStep","halftime","jungleEdit","random"}}}},
                  {"dropFirst",   QJsonObject{{"type","boolean"}}},
                  {"ghostFills",  QJsonObject{{"type","integer"},{"minimum",0},{"maximum",2}}},
                  {"velocityMin", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"velocityMax", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"seed",        QJsonObject{{"type","integer"},{"description","Seeded RNG; same seed + params reproduce the same pattern. 0 = deterministic default."}}}},
                 {"trackId","clipId"}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            AudioEngineCommands::BreakPatternParams p;
            p.trackIndex = a.value("trackId").toInt();
            p.slotIndex  = a.value("slotIndex").toInt(0);
            p.clipId     = a.value("clipId").toInt(-1);
            std::string styleName = a.value("style").toString("amen").toStdString();
            BreakPatternGenerator::Style style;
            if (!BreakPatternGenerator::styleFromName(styleName, style))
                return McpToolResult::text("unknown style: " + QString::fromStdString(styleName), true);
            p.style = style;
            p.bars        = a.value("bars").toInt(8);
            p.grid        = a.value("grid").toInt(4);
            p.dropFirst   = a.value("dropFirst").toBool(false);
            // jungleEdit's intended density is one ghost fill (its signature
            // edit); every other style defaults to none.
            p.ghostFills  = a.value("ghostFills").toInt(
                style == BreakPatternGenerator::Style::JungleEdit ? 1 : 0);
            p.velocityMin = a.value("velocityMin").toInt(60);
            p.velocityMax = a.value("velocityMax").toInt(100);
            uint64_t seed = 12345;
            if (a.contains("seed"))
                seed = static_cast<uint64_t>(a.value("seed").toVariant().toULongLong());
            p.seed = seed;

            const auto result = e->getAudioEngineCommands().generateChoppedBreak(p);
            if (!result.ok)
                return McpToolResult::text(QString::fromStdString(result.error), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                {"added",      result.added},
                {"firstPitch", result.firstPitch},
                {"lastPitch",  result.lastPitch},
                {"sliceCount", result.sliceCount},
                {"baseNote",   result.baseNote}}).toJson(QJsonDocument::Compact)));
        }});

}

} // namespace mcp
