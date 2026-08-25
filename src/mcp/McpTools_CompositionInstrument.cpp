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

void registerInstrumentTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"generate_arrangement", "Generate a multi-track arrangement (kick, hats, clap, bass) into new clips, one track per part. Deterministic for a given seed (0 = random). style: 0=Techno 1=House 2=DnB.",
        objSchema({{"bars",            QJsonObject{{"type","integer"},{"minimum",1}}},
                  {"style",           QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"complexity",      QJsonObject{{"type","number"},{"minimum",0},{"maximum",1}}},
                  {"swingPercent",    QJsonObject{{"type","number"}}},
                  {"seed",            QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"scaleRoot",       QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"scaleMode",       QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}},
                  {"enableKick",      QJsonObject{{"type","boolean"}}},
                  {"enableClosedHat", QJsonObject{{"type","boolean"}}},
                  {"enableOpenHat",   QJsonObject{{"type","boolean"}}},
                  {"enableClap",      QJsonObject{{"type","boolean"}}},
                  {"enableSnare",     QJsonObject{{"type","boolean"}}},
                  {"enableBass",      QJsonObject{{"type","boolean"}}},
                  {"enableLead",      QJsonObject{{"type","boolean"}}},
                  {"enableChords",    QJsonObject{{"type","boolean"}}},
                  {"velocityMin",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"velocityMax",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"targetTrackIds", QJsonObject{{"type","object"},
                      {"description","Map role names to track indices: {\"Kick\":0, \"Bass\":1, ...}. Roles without mapping create new tracks."},
                      {"additionalProperties", QJsonObject{{"type","integer"}}}}}},
                 {"bars"}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            HDAW::ArrangementParams p;
            p.bars = a.value("bars").toInt(32);
            p.style = a.value("style").toInt(0);
            p.complexity = a.contains("complexity") ? a.value("complexity").toDouble() : 0.5;
            p.swingPercent = a.contains("swingPercent") ? a.value("swingPercent").toDouble() : 50.0;
            p.seed = a.contains("seed") ? static_cast<uint64_t>(a.value("seed").toDouble()) : 0;
            p.scaleRoot = a.contains("scaleRoot") ? a.value("scaleRoot").toInt() : e->getProjectModel().getScaleRoot();
            p.scaleMode = a.contains("scaleMode") ? a.value("scaleMode").toInt() : e->getProjectModel().getScaleMode();
            p.enableKick = a.contains("enableKick") ? a.value("enableKick").toBool() : true;
            p.enableClosedHat = a.contains("enableClosedHat") ? a.value("enableClosedHat").toBool() : true;
            p.enableOpenHat = a.contains("enableOpenHat") ? a.value("enableOpenHat").toBool() : true;
            p.enableClap = a.contains("enableClap") ? a.value("enableClap").toBool() : true;
            p.enableSnare = a.contains("enableSnare") ? a.value("enableSnare").toBool() : false;
            p.enableBass = a.contains("enableBass") ? a.value("enableBass").toBool() : true;
            p.enableLead = a.contains("enableLead") ? a.value("enableLead").toBool() : false;
            p.enableChords = a.contains("enableChords") ? a.value("enableChords").toBool() : false;
            p.velocityMin = a.contains("velocityMin") ? a.value("velocityMin").toInt() : 0;
            p.velocityMax = a.contains("velocityMax") ? a.value("velocityMax").toInt() : 0;
            if (a.contains("targetTrackIds")) {
                auto obj = a.value("targetTrackIds").toObject();
                for (auto it = obj.begin(); it != obj.end(); ++it)
                    p.targetTrackIds[it.key().toStdString()] = it.value().toInt();
            }
            auto r = e->getProjectCommands().generateArrangement(p);
            QJsonArray parts;
            const size_t n = r.clipIds.size();
            for (size_t i = 0; i < n; ++i) {
                QJsonObject part;
                part["role"] = QString::fromStdString(i < r.roleNames.size() ? r.roleNames[i] : "");
                part["trackIndex"] = r.trackIndices[i];
                part["clipId"] = r.clipIds[i];
                parts.append(part);
            }
            QJsonObject res;
            res["seed"] = QJsonValue(static_cast<qint64>(r.seed));
            res["noteCount"] = r.noteCount;
            res["parts"] = parts;
            return McpToolResult::text(QJsonDocument(res).toJson(QJsonDocument::Compact));
        }});

s.registerTool({"add_instrument_part",
        "Compose a complete instrument part in one command: add a track with an instrument FX slot, generate a phrase, paint it across the arrangement, and optionally gain-stage to a target RMS. One undo unit. Calls the same engine command as the composition.addInstrumentPart RPC.",
        objSchema({{"trackName",    QJsonObject{{"type","string"}}},
                  {"style",        QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"Standard","Arpeggio","BassLine","ChordStab","Pad","Lead","RandomWalk","Buildup","Euclidean","Percussion","TrapHiHat","DrillBass","Counterpoint","WalkingBass","SwingComping","MarkovMelody","EvolvingTexture","Aleatoric","ScalarRun","ChordToneSeq","CallResponse","PhaseShift","AdditiveRhythm","MinimalistLoop","Layered"}}}},
                  {"role",         QJsonObject{{"type","string"},{"description","Part template: bass | lead | chords | drums (case-insensitive). When provided, fills unset phrase params with role defaults; explicit params always win."}}},
                  {"pluginId",     QJsonObject{{"type","string"}}},
                  {"programIndex", QJsonObject{{"type","integer"},{"minimum",-1}}},
                  {"lengthBeats",  QJsonObject{{"type","number"},{"minimum",0.25}}},
                  {"placement",    QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"wholeSong","region"}}}},
                  {"startBeat",    QJsonObject{{"type","number"},{"minimum",0.0}}},
                  {"count",        QJsonObject{{"type","integer"},{"minimum",1}}},
                  {"scaleRoot",    QJsonObject{{"type","integer"},{"minimum",-1},{"maximum",11}}},
                  {"scaleMode",    QJsonObject{{"type","integer"},{"minimum",-1},{"maximum",20}}},
                  {"density",      QJsonObject{{"type","integer"},{"minimum",1}}},
                  {"noteDuration", QJsonObject{{"type","number"},{"minimum",0.01}}},
                  {"lowNote",      QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"highNote",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"minVelocity",  QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"maxVelocity",  QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"seed",         QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"targetRms",    QJsonObject{{"type","number"},{"minimum",0.0}}},
                  {"windowSeconds",QJsonObject{{"type","number"},{"minimum",0.1}}},
                  {"verify",       QJsonObject{{"type","boolean"}}},
                  {"allowGlobalScale", QJsonObject{{"type","boolean"}}}},
                 {"trackName"}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            ProjectCommands::InstrumentPartParams p;
            p.trackName = a.value("trackName").toString().toStdString();
            p.style = a.contains("style") ? a.value("style").toString().toStdString() : std::string();
            p.role = a.contains("role") ? a.value("role").toString().toStdString() : std::string();
            p.pluginId = a.contains("pluginId") ? a.value("pluginId").toString().toStdString() : std::string();
            p.programIndex = a.contains("programIndex") ? a.value("programIndex").toInt() : -1;
            p.lengthBeats = a.value("lengthBeats").toDouble(4.0);
            p.placement = a.contains("placement") ? a.value("placement").toString().toStdString() : "region";
            p.startBeat = a.value("startBeat").toDouble(0.0);
            p.count = a.value("count").toInt(1);
            p.scaleRoot = a.contains("scaleRoot") ? a.value("scaleRoot").toInt() : -1;
            p.scaleMode = a.contains("scaleMode") ? a.value("scaleMode").toInt() : -1;
            p.density = a.value("density").toInt(8);
            p.noteDuration = a.value("noteDuration").toDouble(0.5);
            p.lowNote = a.value("lowNote").toInt(48);
            p.highNote = a.value("highNote").toInt(84);
            p.minVelocity = a.value("minVelocity").toInt(60);
            p.maxVelocity = a.value("maxVelocity").toInt(110);
            p.seed = a.contains("seed") ? static_cast<uint64_t>(a.value("seed").toDouble()) : 0;
            p.targetRms = a.contains("targetRms") ? static_cast<float>(a.value("targetRms").toDouble()) : 0.0f;
            p.windowSeconds = a.value("windowSeconds").toDouble(4.0);
            p.verify = a.contains("verify") ? a.value("verify").toBool() : false;
            p.allowGlobalScale = a.contains("allowGlobalScale") ? a.value("allowGlobalScale").toBool() : false;
            if (a.contains("style"))            p.explicitMask |= ProjectCommands::kRoleBitStyle;
            if (a.contains("lowNote"))          p.explicitMask |= ProjectCommands::kRoleBitLowNote;
            if (a.contains("highNote"))         p.explicitMask |= ProjectCommands::kRoleBitHighNote;
            if (a.contains("density"))          p.explicitMask |= ProjectCommands::kRoleBitDensity;
            if (a.contains("noteDuration"))     p.explicitMask |= ProjectCommands::kRoleBitNoteDuration;
            if (a.contains("minVelocity"))      p.explicitMask |= ProjectCommands::kRoleBitMinVelocity;
            if (a.contains("maxVelocity"))      p.explicitMask |= ProjectCommands::kRoleBitMaxVelocity;
            if (a.contains("targetRms"))        p.explicitMask |= ProjectCommands::kRoleBitTargetRms;
            if (a.contains("allowGlobalScale")) p.explicitMask |= ProjectCommands::kRoleBitAllowGlobalScale;
            auto r = e->getProjectCommands().addInstrumentPart(p);
            if (!r.error.empty())
                return McpToolResult::text(QString::fromStdString(r.error), true);
            QString out = QString("trackIndex=%1 clips=%2 notes=%3")
                              .arg(r.trackIndex).arg((int) r.clipIds.size()).arg(r.noteCount);
            if (p.targetRms > 0.0f) {
                out += QString(" gainOk=%1 fader=%2 rms=%3 peak=%4 clamped=%5 globalScale=%6 masterGain=%7 mixPeak=%8")
                           .arg(r.gain.ok).arg(r.gain.fader).arg(r.gain.measuredRms)
                           .arg(r.gain.peak).arg(r.gain.clamped).arg(r.gain.globalScale)
                           .arg(r.gain.masterGain).arg(r.gain.mixPeak);
            }
            return McpToolResult::text(out);
        }});

s.registerTool({"auto_gain_to_target",
        "Gain-stage a track to a target RMS: solo-render its first window to a temp WAV, measure, and set the track fader (clamped at 1.0). With allowGlobalScale, when the fader clamps and the full mix clips, the master bus gain is scaled down and the fader raised into the created headroom (one undo unit). Calls the same engine command as the composition.autoGainToTarget RPC.",
        objSchema({{"trackId",        QJsonObject{{"type","integer"}}},
                  {"targetRms",      QJsonObject{{"type","number"},{"minimum",0.000001}}},
                  {"windowSeconds",  QJsonObject{{"type","number"},{"minimum",0.1}}},
                  {"verify",         QJsonObject{{"type","boolean"}}},
                  {"allowGlobalScale", QJsonObject{{"type","boolean"}}}},
                 {"trackId","targetRms"}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            auto r = e->getProjectCommands().autoGainToTarget(
                a.value("trackId").toInt(),
                static_cast<float>(a.value("targetRms").toDouble()),
                a.value("windowSeconds").toDouble(4.0),
                a.contains("verify") ? a.value("verify").toBool() : false,
                a.contains("allowGlobalScale") ? a.value("allowGlobalScale").toBool() : false);
            if (!r.error.empty())
                return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString("ok=%1 fader=%2 rms=%3 peak=%4 clamped=%5 globalScale=%6 masterGain=%7 mixPeak=%8")
                .arg(r.ok).arg(r.fader).arg(r.measuredRms).arg(r.peak).arg(r.clamped)
                .arg(r.globalScale).arg(r.masterGain).arg(r.mixPeak));
        }});

s.registerTool({"audition_plugin",
        "Solo-render a plugin or internal FX (fm_synth/sampler) — on a temp probe track (trackIndex < 0) or an existing slot — over a short window and report peak/rms/audible so silent-at-default plugins stop being a blocker. programIndex -1 reports the current program. Calls the same engine command as the composition.auditionPlugin RPC.",
        objSchema({{"pluginId",     QJsonObject{{"type","string"}}},
                  {"programIndex",  QJsonObject{{"type","integer"},{"minimum",-1}}},
                  {"trackIndex",    QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"slotIndex",     QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"style",         QJsonObject{{"type","string"}}},
                  {"lengthBeats",   QJsonObject{{"type","number"},{"minimum",0.25}}},
                  {"density",       QJsonObject{{"type","integer"},{"minimum",1}}},
                  {"noteDuration",  QJsonObject{{"type","number"},{"minimum",0.01}}},
                  {"lowNote",       QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"highNote",      QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"minVelocity",   QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"maxVelocity",   QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"seed",          QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"windowSeconds", QJsonObject{{"type","number"},{"minimum",0.1}}},
                   {"keepTrack",     QJsonObject{{"type","boolean"}}}}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            ProjectCommands::AuditionParams p;
            p.pluginId = a.contains("pluginId") ? a.value("pluginId").toString().toStdString() : std::string();
            p.programIndex = a.contains("programIndex") ? a.value("programIndex").toInt() : -1;
            p.trackIndex = a.contains("trackIndex") ? a.value("trackIndex").toInt() : -1;
            p.slotIndex = a.value("slotIndex").toInt(0);
            p.style = a.contains("style") ? a.value("style").toString().toStdString() : "Arpeggio";
            p.lengthBeats = a.value("lengthBeats").toDouble(4.0);
            p.density = a.value("density").toInt(8);
            p.noteDuration = a.value("noteDuration").toDouble(0.5);
            p.lowNote = a.value("lowNote").toInt(48);
            p.highNote = a.value("highNote").toInt(84);
            p.minVelocity = a.value("minVelocity").toInt(60);
            p.maxVelocity = a.value("maxVelocity").toInt(110);
            p.seed = a.contains("seed") ? static_cast<uint64_t>(a.value("seed").toDouble()) : 0;
            p.windowSeconds = a.value("windowSeconds").toDouble(4.0);
            p.keepTrack = a.contains("keepTrack") ? a.value("keepTrack").toBool() : false;
            auto r = e->getProjectCommands().auditionPlugin(p);
            if (!r.error.empty())
                return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString("ok=%1 trackIndex=%2 slotIndex=%3 program=%4 name=%5 numPrograms=%6 rms=%7 peak=%8 duration=%9 audible=%10")
                .arg(r.ok).arg(r.trackIndex).arg(r.slotIndex).arg(r.programIndex)
                .arg(QString::fromStdString(r.programName)).arg(r.numPrograms)
                .arg(r.rms).arg(r.peak).arg(r.durationSeconds).arg(r.audible));
        }});

s.registerTool({"verify_part",
        "Self-verify a composed part: solo-render + full-mix render of the track's window; reports solo/mix rms+peak, nonClipping (mix peak < 1.0), audible (solo peak > -80 dBFS), bandsPresent (low/mid/high spectral energy). Read-only. Calls the same engine command as the composition.verifyPart RPC.",
        objSchema({{"trackIndex",    QJsonObject{{"type","integer"},{"minimum",0}}},
                   {"windowSeconds", QJsonObject{{"type","number"},{"minimum",0.1}}}},
                   {"trackIndex"}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            const int trackIndex = a.value("trackIndex").toInt(-1);
            const double windowSeconds = a.value("windowSeconds").toDouble(4.0);
            auto r = e->getProjectCommands().verifyPart(trackIndex, windowSeconds);
            if (!r.error.empty())
                return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString("ok=%1 soloRms=%2 soloPeak=%3 mixRms=%4 mixPeak=%5 nonClipping=%6 audible=%7 bandsPresent=%8")
                .arg(r.ok).arg(r.soloRms).arg(r.soloPeak).arg(r.mixRms).arg(r.mixPeak)
                .arg(r.nonClipping).arg(r.audible).arg(r.bandsPresent));
        }});

}

} // namespace mcp
