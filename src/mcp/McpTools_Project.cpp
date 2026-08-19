#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/PluginManager.h"
#include "../engine/Track.h"
#include "../engine/PhraseGenerator.h"
#include "../engine/ArrangementGenerator.h"
#include "engine/RhythmPatternGenerator.h"
#include "../engine/ProjectSerializer.h"
#include "../engine/ProjectBackup.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace mcp {

static void registerReadTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"get_project_summary",
        "Return project name, tempo, track/clip counts, transport state.",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto tp = m.getTransportTree();
            auto tl = m.getTrackListTree();
            int tracks = tl.getNumChildren();
            int clips = 0;
            for (int i = 0; i < tracks; ++i)
                clips += tl.getChild(i).getChildWithName(IDs::CLIP_LIST).getNumChildren();
            return McpToolResult::text(QString(
                "name=%1\ntempo=%2\ntracks=%3\nclips=%4\nposition=%5\nisPlaying=%6")
                .arg(jstr(m.getTree().getProperty(IDs::name).toString()))
                .arg(static_cast<double>(m.getTree().getProperty(IDs::tempo)))
                .arg(tracks).arg(clips)
                .arg(static_cast<double>(tp.getProperty(IDs::position)))
                .arg(jstr(tp.getProperty(IDs::isPlaying).toString())));
        }});

    s.registerTool({"get_scale", "Return the project scale (root, mode).",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) {
            auto& m = e->getProjectModel();
            QJsonObject o{{"root", m.getScaleRoot()}, {"mode", m.getScaleMode()}};
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(o).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"list_tracks",
        "List all tracks (id, name, color, volume, pan, mute, solo, clipCount).",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) {
            auto tl = e->getProjectModel().getTrackListTree();
            QJsonArray arr;
            for (int i = 0; i < tl.getNumChildren(); ++i) {
                auto t = tl.getChild(i);
                arr.append(QJsonObject{
                    {"id", i},
                    {"name", jstr(t.getProperty(IDs::name).toString())},
                    {"color", static_cast<int>(t.getProperty(IDs::color))},
                    {"volume", static_cast<double>(t.getProperty(IDs::volume))},
                    {"pan", static_cast<double>(t.getProperty(IDs::pan))},
                    {"mute", static_cast<bool>(t.getProperty(IDs::isMuted))},
                    {"solo", static_cast<bool>(t.getProperty(IDs::isSoloed))},
                    {"clipCount", t.getChildWithName(IDs::CLIP_LIST).getNumChildren()}
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"list_clips", "List clips (optionally on a single trackId).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}),
        [e](const QJsonObject& a) {
            auto tl = e->getProjectModel().getTrackListTree();
            int wanted = a.value("trackId").toInt(-1);
            QJsonArray arr;
            double bpm = e->getReadModel().getTransport().bpm;
            double toBeats = (bpm > 0) ? bpm / 60.0 : 1.0;
            for (int i = 0; i < tl.getNumChildren(); ++i) {
                if (wanted >= 0 && wanted != i) continue;
                auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
                for (int j = 0; j < cl.getNumChildren(); ++j) {
                    auto c = cl.getChild(j);
                    arr.append(QJsonObject{
                        {"id", static_cast<int>(c.getProperty(IDs::clipID))},
                        {"trackId", i},
                        {"name", jstr(c.getProperty(IDs::name).toString())},
                        {"start", static_cast<double>(c.getProperty(IDs::startTime)) * toBeats},
                        {"duration", static_cast<double>(c.getProperty(IDs::duration)) * toBeats},
                        {"type", jstr(c.getProperty(IDs::clipType).toString())},
                        {"gain", static_cast<double>(c.getProperty(IDs::gain))},
                        {"fadeIn", static_cast<double>(c.getProperty(IDs::fadeIn))},
                        {"fadeOut", static_cast<double>(c.getProperty(IDs::fadeOut))},
                        {"looping", static_cast<bool>(c.getProperty(IDs::looping))},
                        {"muted", static_cast<bool>(c.getProperty(IDs::muted))}
                    });
                }
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_clip",
        "Return full properties of a clip, including its note list if MIDI.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        [e](const QJsonObject& a) {
            int cid = a.value("clipId").toInt(-1);
            auto tl = e->getProjectModel().getTrackListTree();
            for (int i = 0; i < tl.getNumChildren(); ++i) {
                auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
                for (int j = 0; j < cl.getNumChildren(); ++j) {
                    auto c = cl.getChild(j);
                    if (static_cast<int>(c.getProperty(IDs::clipID)) != cid) continue;
                    double bpm = e->getReadModel().getTransport().bpm;
                    double toBeats = (bpm > 0) ? bpm / 60.0 : 1.0;
                    QJsonObject out{
                        {"id", cid}, {"trackId", i},
                        {"name", jstr(c.getProperty(IDs::name).toString())},
                        {"start", static_cast<double>(c.getProperty(IDs::startTime)) * toBeats},
                        {"duration", static_cast<double>(c.getProperty(IDs::duration)) * toBeats},
                        {"type", jstr(c.getProperty(IDs::clipType).toString())},
                        {"gain", static_cast<double>(c.getProperty(IDs::gain))},
                        {"fadeIn", static_cast<double>(c.getProperty(IDs::fadeIn))},
                        {"fadeOut", static_cast<double>(c.getProperty(IDs::fadeOut))},
                        {"looping", static_cast<bool>(c.getProperty(IDs::looping))},
                        {"muted", static_cast<bool>(c.getProperty(IDs::muted))}
                    };
                    if (c.getProperty(IDs::clipType).toString() == juce::String("midi")) {
                        auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
                        QJsonArray notes;
                        for (int k = 0; k < nl.getNumChildren(); ++k) {
                            auto n = nl.getChild(k);
                            notes.append(QJsonObject{
                                {"noteId", static_cast<int>(n.getProperty(IDs::noteID))},
                                {"pitch", static_cast<int>(n.getProperty(IDs::noteNumber))},
                                {"start", static_cast<double>(n.getProperty(IDs::startBeat))},
                                {"duration", static_cast<double>(n.getProperty(IDs::durationBeats))},
                                {"velocity", static_cast<int>(n.getProperty(IDs::velocity))}
                            });
                        }
                        out["notes"] = notes;
                    }
                    return McpToolResult::text(QString::fromUtf8(
                        QJsonDocument(out).toJson(QJsonDocument::Indented)));
                }
            }
            return McpToolResult::text(QString("clipId %1 not found").arg(cid), true);
        }});
}

static void registerTrackTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_track",
        "Add a track. Color defaults to the next palette color if omitted.",
        objSchema({{"name", QJsonObject{{"type","string"}}},
                  {"color", QJsonObject{{"type","integer"}}},
                  {"parentBus", QJsonObject{{"type","integer"}}}}, {"name"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto& um = m.getUndoManager();
            int idx = m.getTrackListTree().getNumChildren();
            juce::ValueTree t(IDs::TRACK);
            t.setProperty(IDs::name, juce::String(a.value("name").toString().toUtf8().constData()), &um);
            t.setProperty(IDs::volume, 0.85, &um);
            t.setProperty(IDs::pan, 0.0, &um);
            t.setProperty(IDs::isMuted, false, &um);
            t.setProperty(IDs::isSoloed, false, &um);
            t.setProperty(IDs::parentBus, a.value("parentBus").toInt(0), &um);
            int color = a.contains("color") ? a.value("color").toInt()
                                             : static_cast<int>(ProjectModel::trackColorForIndex(idx));
            t.setProperty(IDs::color, color, &um);
            t.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, &um);
            t.addChild(juce::ValueTree(IDs::FX_CHAIN), -1, &um);
            t.addChild(ProjectModel::createTrackAutomationList(), -1, &um);
            m.getTrackListTree().addChild(t, -1, &um);
            bool routingOk = idx >= 0 && idx < e->getProjectModel().getTrackListTree().getNumChildren();
            return McpToolResult::text(
                QString("trackId=%1 routed=%2").arg(idx).arg(routingOk ? "1" : "0"));
        }});

    s.registerTool({"remove_track", "Remove a track (destructive).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"dryRun",  QJsonObject{{"type","boolean"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto tl = m.getTrackListTree();
            int id = a.value("trackId").toInt();
            if (id < 0 || id >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            QString name = jstr(tl.getChild(id).getProperty(IDs::name).toString());
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove track %1 (%2)").arg(id).arg(name));
            tl.removeChild(id, &m.getUndoManager());
            return McpToolResult::text(QString("removed track %1").arg(id));
        }});

    s.registerTool({"set_track", "Update track properties (partial).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"name",   QJsonObject{{"type","string"}}},
                  {"volume", QJsonObject{{"type","number"}}},
                  {"pan",    QJsonObject{{"type","number"}}},
                  {"mute",   QJsonObject{{"type","boolean"}}},
                  {"solo",   QJsonObject{{"type","boolean"}}},
                  {"color",  QJsonObject{{"type","integer"}}},
                  {"hidden", QJsonObject{{"type","boolean"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            int id = a.value("trackId").toInt();
            auto tl = m.getTrackListTree();
            if (id < 0 || id >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            auto t = tl.getChild(id);
            if (a.contains("name"))   t.setProperty(IDs::name, juce::String(a.value("name").toString().toUtf8().constData()), &um);
            if (a.contains("volume")) t.setProperty(IDs::volume, a.value("volume").toDouble(), &um);
            if (a.contains("pan"))    t.setProperty(IDs::pan, a.value("pan").toDouble(), &um);
            if (a.contains("mute"))   t.setProperty(IDs::isMuted, a.value("mute").toBool(), &um);
            if (a.contains("solo"))   t.setProperty(IDs::isSoloed, a.value("solo").toBool(), &um);
            if (a.contains("color"))  t.setProperty(IDs::color, a.value("color").toInt(), &um);
            if (a.contains("hidden")) t.setProperty(IDs::isHidden, a.value("hidden").toBool(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_master_gain", "Set the master bus gain (linear, >= 0).",
        objSchema({{"gain", QJsonObject{{"type","number"},{"minimum",0}}}}, {"gain"}),
        [e](const QJsonObject& a) -> McpToolResult {
            e->getProjectCommands().setMasterGain(static_cast<float>(a.value("gain").toDouble(1.0)));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"move_track", "Move a track to a new index.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"newIndex", QJsonObject{{"type","integer"}}}}, {"trackId","newIndex"}),
        [e](const QJsonObject& a) {
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            auto tl = m.getTrackListTree();
            int id = a.value("trackId").toInt();
            int ni = a.value("newIndex").toInt();
            if (id < 0 || id >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            ni = std::clamp(ni, 0, tl.getNumChildren() - 1);
            auto t = tl.getChild(id);
            tl.removeChild(id, nullptr);
            tl.addChild(t, ni, &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"duplicate_track",
        "Duplicate a track (deep copy with new clip/note IDs). Returns the new track index.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto tl = m.getTrackListTree();
            int id = a.value("trackId").toInt();
            if (id < 0 || id >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            int newIdx = e->getProjectCommands().duplicateTrack(id);
            if (newIdx < 0)
                return McpToolResult::text("duplicate failed", true);
            bool routingOk = newIdx >= 0 && newIdx < e->getProjectModel().getTrackListTree().getNumChildren();
            return McpToolResult::text(
                QString("trackId=%1 routed=%2").arg(newIdx).arg(routingOk ? "1" : "0"));
        }});

    s.registerTool({"add_track_with_fx",
        "Add a track with an FX slot. fxType in {eq,compressor,reverb,delay,chorus,flanger,phaser}, or provide pluginId for a VST3/CLAP plugin.",
        objSchema({{"name",     QJsonObject{{"type","string"}}},
                   {"fxType",   QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger","phaser"}}}},
                   {"pluginId", QJsonObject{{"type","string"}}},
                   {"color",    QJsonObject{{"type","integer"}}},
                   {"parentBus",QJsonObject{{"type","integer"}}}}, {"name"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto& um = m.getUndoManager();
            int idx = m.getTrackListTree().getNumChildren();

            juce::ValueTree t(IDs::TRACK);
            t.setProperty(IDs::name, juce::String(a.value("name").toString().toUtf8().constData()), &um);
            t.setProperty(IDs::volume, 0.85, &um);
            t.setProperty(IDs::pan, 0.0, &um);
            t.setProperty(IDs::isMuted, false, &um);
            t.setProperty(IDs::isSoloed, false, &um);
            t.setProperty(IDs::parentBus, a.value("parentBus").toInt(0), &um);
            int color = a.contains("color") ? a.value("color").toInt()
                                             : static_cast<int>(ProjectModel::trackColorForIndex(idx));
            t.setProperty(IDs::color, color, &um);
            t.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, &um);
            t.addChild(juce::ValueTree(IDs::FX_CHAIN), -1, &um);
            t.addChild(ProjectModel::createTrackAutomationList(), -1, &um);
            m.getTrackListTree().addChild(t, -1, &um);

            std::string fxType = a.value("fxType").toString().toStdString();
            std::string pluginId = a.value("pluginId").toString().toStdString();
            if (fxType.empty() && !pluginId.empty()) fxType = "plugin";
            if (!fxType.empty())
                m.addFxSlot(idx, fxType, -1, pluginId);

            bool routingOk = idx >= 0 && idx < e->getProjectModel().getTrackListTree().getNumChildren();
            return McpToolResult::text(
                QString("trackId=%1 routed=%2 fxType=%3").arg(idx)
                    .arg(routingOk ? "1" : "0")
                    .arg(QString::fromStdString(fxType)));
        }});
}

static void registerClipTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_midi_clip", "Add an empty MIDI clip to a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"start",   QJsonObject{{"type","number"}}},
                  {"length",  QJsonObject{{"type","number"}}},
                  {"name",    QJsonObject{{"type","string"}}}}, {"trackId","start","length"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            int ti = a.value("trackId").toInt();
            auto tl = m.getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            double bpm = e->getReadModel().getTransport().bpm;
            double startSec = (bpm > 0) ? a.value("start").toDouble() * 60.0 / bpm : a.value("start").toDouble();
            double durSec = (bpm > 0) ? a.value("length").toDouble() * 60.0 / bpm : a.value("length").toDouble();
            auto c = ProjectModel::createMidiClipEmpty(
                juce::String(a.value("name").toString("MIDI Clip").toUtf8().constData()),
                startSec, durSec);
            c.setProperty(IDs::color, static_cast<int>(ProjectModel::trackColorForIndex(ti)), nullptr);
            int cid = static_cast<int>(c.getProperty(IDs::clipID));
            tl.getChild(ti).getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
            return McpToolResult::text(QString("clipId=%1").arg(cid));
        }});

    s.registerTool({"add_audio_clip", "Add an audio clip referencing a source file.",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"start",       QJsonObject{{"type","number"}}},
                  {"length",      QJsonObject{{"type","number"}}},
                  {"sourceFile",  QJsonObject{{"type","string"}}},
                  {"name",        QJsonObject{{"type","string"}}}}, {"trackId","start","length","sourceFile"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            int ti = a.value("trackId").toInt();
            auto tl = m.getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            juce::File src(QString::fromUtf8(a.value("sourceFile").toString().toUtf8()).toStdString());
            if (!src.existsAsFile()) return McpToolResult::text("source file not found", true);
            double bpm = e->getReadModel().getTransport().bpm;
            double startSec = (bpm > 0) ? a.value("start").toDouble() * 60.0 / bpm : a.value("start").toDouble();
            double durSec = (bpm > 0) ? a.value("length").toDouble() * 60.0 / bpm : a.value("length").toDouble();
            auto c = ProjectModel::createAudioClip(
                juce::String(a.value("name").toString("Audio Clip").toUtf8().constData()),
                startSec, durSec,
                src.getFullPathName());
            c.setProperty(IDs::color, static_cast<int>(ProjectModel::trackColorForIndex(ti)), nullptr);
            int cid = static_cast<int>(c.getProperty(IDs::clipID));
            tl.getChild(ti).getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
            return McpToolResult::text(QString("clipId=%1").arg(cid));
        }});

    s.registerTool({"remove_clip", "Remove a clip (destructive).",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                  {"dryRun", QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            QString name = jstr(c.getProperty(IDs::name).toString());
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove clip %1 (%2)").arg(static_cast<int>(c.getProperty(IDs::clipID))).arg(name));
            e->getProjectModel().getTrackListTree().getChild(ti)
                .getChildWithName(IDs::CLIP_LIST).removeChild(c, &e->getProjectModel().getUndoManager());
            return McpToolResult::text(QString("removed clip %1").arg(static_cast<int>(c.getProperty(IDs::clipID))));
        }});

    s.registerTool({"move_clip", "Move a clip to a new start (and optionally a new track).",
        objSchema({{"clipId",  QJsonObject{{"type","integer"}}},
                  {"start",   QJsonObject{{"type","number"}}},
                  {"trackId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            if (a.contains("start")) {
                double bpm = e->getReadModel().getTransport().bpm;
                double startSec = (bpm > 0) ? a.value("start").toDouble() * 60.0 / bpm : a.value("start").toDouble();
                c.setProperty(IDs::startTime, startSec, &um);
            }
            if (a.contains("trackId")) {
                int nti = a.value("trackId").toInt();
                auto tl = e->getProjectModel().getTrackListTree();
                if (nti < 0 || nti >= tl.getNumChildren()) return McpToolResult::text("target track not found", true);
                e->getProjectModel().getTrackListTree().getChild(ti)
                    .getChildWithName(IDs::CLIP_LIST).removeChild(c, nullptr);
                tl.getChild(nti).getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
            }
            return McpToolResult::text("ok");
        }});

    s.registerTool({"ripple_delete",
        "Ripple-delete a time range: remove all clip content within "
        "[startBeat, endBeat) and shift later clips left to close the gap.",
        objSchema({{"startBeat", QJsonObject{{"type","number"}}},
                   {"endBeat",   QJsonObject{{"type","number"}}}}, {"startBeat","endBeat"}),
        [e](const QJsonObject& a) -> McpToolResult {
            if (!a.contains("startBeat") || !a.contains("endBeat"))
                return McpToolResult::text("startBeat and endBeat required", true);
            double sb = a.value("startBeat").toDouble();
            double eb = a.value("endBeat").toDouble();
            if (eb <= sb)
                return McpToolResult::text("endBeat must be greater than startBeat", true);
            e->getProjectCommands().rippleDelete(sb, eb);
            return McpToolResult::text(QString("rippled [%1, %2)").arg(sb).arg(eb));
        }});

    s.registerTool({"insert_silence",
        "Insert silence: split any clip crossing startBeat and shift all later "
        "content right by (endBeat - startBeat), opening an empty gap.",
        objSchema({{"startBeat", QJsonObject{{"type","number"}}},
                   {"endBeat",   QJsonObject{{"type","number"}}}}, {"startBeat","endBeat"}),
        [e](const QJsonObject& a) -> McpToolResult {
            if (!a.contains("startBeat") || !a.contains("endBeat"))
                return McpToolResult::text("startBeat and endBeat required", true);
            double sb = a.value("startBeat").toDouble();
            double eb = a.value("endBeat").toDouble();
            if (eb <= sb)
                return McpToolResult::text("endBeat must be greater than startBeat", true);
            e->getProjectCommands().insertSilence(sb, eb);
            return McpToolResult::text(QString("inserted silence [%1, %2)").arg(sb).arg(eb));
        }});

    s.registerTool({"duplicate_region",
        "Duplicate region: copy all clip content within [startBeat, endBeat) "
        "and paste it at endBeat, shifting later content right.",
        objSchema({{"startBeat", QJsonObject{{"type","number"}}},
                   {"endBeat",   QJsonObject{{"type","number"}}}}, {"startBeat","endBeat"}),
        [e](const QJsonObject& a) -> McpToolResult {
            if (!a.contains("startBeat") || !a.contains("endBeat"))
                return McpToolResult::text("startBeat and endBeat required", true);
            double sb = a.value("startBeat").toDouble();
            double eb = a.value("endBeat").toDouble();
            if (eb <= sb)
                return McpToolResult::text("endBeat must be greater than startBeat", true);
            e->getProjectCommands().duplicateRegion(sb, eb);
            return McpToolResult::text(QString("duplicated [%1, %2) to %3").arg(sb).arg(eb).arg(eb));
        }});

    s.registerTool({"set_clip", "Update clip properties (partial).",
        objSchema({{"clipId",    QJsonObject{{"type","integer"}}},
                  {"name",      QJsonObject{{"type","string"}}},
                  {"start",     QJsonObject{{"type","number"}}},
                  {"duration",  QJsonObject{{"type","number"}}},
                  {"gain",      QJsonObject{{"type","number"}}},
                  {"fadeIn",    QJsonObject{{"type","number"}}},
                  {"fadeOut",   QJsonObject{{"type","number"}}},
                  {"looping",   QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            double bpm = e->getReadModel().getTransport().bpm;
            double factor = (bpm > 0) ? 60.0 / bpm : 1.0;
            if (a.contains("name"))     c.setProperty(IDs::name, juce::String(a.value("name").toString().toUtf8().constData()), &um);
            if (a.contains("start"))    c.setProperty(IDs::startTime, a.value("start").toDouble() * factor, &um);
            if (a.contains("duration")) c.setProperty(IDs::duration, a.value("duration").toDouble() * factor, &um);
            if (a.contains("gain"))     c.setProperty(IDs::gain, a.value("gain").toDouble(), &um);
            if (a.contains("fadeIn"))   c.setProperty(IDs::fadeIn, a.value("fadeIn").toDouble(), &um);
            if (a.contains("fadeOut"))  c.setProperty(IDs::fadeOut, a.value("fadeOut").toDouble(), &um);
            if (a.contains("looping"))  c.setProperty(IDs::looping, a.value("looping").toBool(), &um);
            if (a.contains("muted"))    c.setProperty(IDs::muted, a.value("muted").toBool(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"duplicate_clip", "Duplicate a clip (destructive: creates a new clip).",
        objSchema({{"clipId",   QJsonObject{{"type","integer"}}},
                  {"start",    QJsonObject{{"type","number"}}},
                  {"trackId",  QJsonObject{{"type","integer"}}},
                  {"dryRun",   QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto src = findClip(e, a.value("clipId").toInt(), &ti);
            if (!src.isValid()) return McpToolResult::text("clip not found", true);
            int nti = a.contains("trackId") ? a.value("trackId").toInt() : ti;
            double bpm = e->getReadModel().getTransport().bpm;
            double factor = (bpm > 0) ? 60.0 / bpm : 1.0;
            double ns = a.contains("start") ? a.value("start").toDouble() * factor
                                            : static_cast<double>(src.getProperty(IDs::startTime));
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would duplicate clip %1 to track %2 @ %3")
                    .arg(static_cast<int>(src.getProperty(IDs::clipID))).arg(nti).arg(ns));
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            auto tl = m.getTrackListTree();
            if (nti < 0 || nti >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            auto xml = src.toXmlString();
            juce::ValueTree copy(juce::ValueTree::fromXml(xml));
            int newId = m.allocateClipID();
            copy.setProperty(IDs::clipID, newId, nullptr);
            copy.setProperty(IDs::startTime, ns, nullptr);
            tl.getChild(nti).getChildWithName(IDs::CLIP_LIST).addChild(copy, -1, &um);
            return McpToolResult::text(QString("clipId=%1").arg(newId));
        }});
}

static void registerNoteTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_note", "Add a MIDI note to a clip; returns noteId.",
        objSchema({{"clipId",    QJsonObject{{"type","integer"}}},
                  {"pitch",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"start",     QJsonObject{{"type","number"}}},
                  {"duration",  QJsonObject{{"type","number"}}},
                  {"velocity",  QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}},
                 {"clipId","pitch","start","duration","velocity"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            if (c.getProperty(IDs::clipType).toString() != juce::String("midi"))
                return McpToolResult::text("clip is not MIDI", true);
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!nl.isValid()) { nl = juce::ValueTree(IDs::MIDI_NOTE_LIST); c.addChild(nl, -1, nullptr); }
            juce::ValueTree n(IDs::MIDI_NOTE);
            int nid = m.allocateNoteID();
            n.setProperty(IDs::noteID, nid, nullptr);
            n.setProperty(IDs::noteNumber, a.value("pitch").toInt(), &um);
            n.setProperty(IDs::startBeat, a.value("start").toDouble(), &um);
            n.setProperty(IDs::durationBeats, a.value("duration").toDouble(), &um);
            n.setProperty(IDs::velocity, a.value("velocity").toInt(), &um);
            nl.addChild(n, -1, &um);
            return McpToolResult::text(QString("noteId=%1").arg(nid));
        }});

    s.registerTool({"set_note", "Update a note's properties (partial).",
        objSchema({{"noteId",   QJsonObject{{"type","integer"}}},
                  {"pitch",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"start",    QJsonObject{{"type","number"}}},
                  {"duration", QJsonObject{{"type","number"}}},
                  {"velocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}}, {"noteId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            if (a.contains("pitch"))    n.setProperty(IDs::noteNumber, a.value("pitch").toInt(), &um);
            if (a.contains("start"))    n.setProperty(IDs::startBeat, a.value("start").toDouble(), &um);
            if (a.contains("duration")) n.setProperty(IDs::durationBeats, a.value("duration").toDouble(), &um);
            if (a.contains("velocity")) n.setProperty(IDs::velocity, a.value("velocity").toInt(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"remove_notes", "Remove notes by filter or by noteIds (destructive).",
        objSchema({{"clipId",   QJsonObject{{"type","integer"}}},
                  {"pitches",  QJsonObject{{"type","array"},
                      {"items", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}}},
                  {"startGte", QJsonObject{{"type","number"}}},
                  {"startLt",  QJsonObject{{"type","number"}}},
                  {"noteIds",  QJsonObject{{"type","array"},
                      {"items", QJsonObject{{"type","integer"}}}}},
                  {"dryRun",   QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!nl.isValid()) return McpToolResult::text("ok");
            QSet<int> pitches; for (const auto& p : a.value("pitches").toArray()) pitches.insert(p.toInt());
            QSet<int> ids;     for (const auto& i : a.value("noteIds").toArray()) ids.insert(i.toInt());
            bool hasGte = a.contains("startGte"); bool hasLt = a.contains("startLt");
            double gte = a.value("startGte").toDouble(); double lt = a.value("startLt").toDouble();
            int matched = 0;
            for (int k = nl.getNumChildren() - 1; k >= 0; --k) {
                auto n = nl.getChild(k);
                int nid = static_cast<int>(n.getProperty(IDs::noteID));
                int p   = static_cast<int>(n.getProperty(IDs::noteNumber));
                double s= static_cast<double>(n.getProperty(IDs::startBeat));
                bool match = false;
                if (!ids.isEmpty() && ids.contains(nid)) match = true;
                if (!pitches.isEmpty() && pitches.contains(p)) match = true;
                if (hasGte && s < gte) match = false;
                if (hasLt  && s >= lt) match = false;
                if (match) {
                    ++matched;
                    if (!a.value("dryRun").toBool(false))
                        nl.removeChild(k, &e->getProjectModel().getUndoManager());
                }
            }
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove %1 notes").arg(matched));
            return McpToolResult::text(QString("removed %1 notes").arg(matched));
        }});

    s.registerTool({"clear_notes", "Remove all notes from a MIDI clip (destructive).",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                  {"dryRun", QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            int n = nl.isValid() ? nl.getNumChildren() : 0;
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would clear %1 notes").arg(n));
            if (nl.isValid()) c.removeChild(nl, &e->getProjectModel().getUndoManager());
            return McpToolResult::text(QString("cleared %1 notes").arg(n));
        }});

    s.registerTool({"set_note_chance", "Set a note's chance (probability) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"chance", QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}}, {"noteId","chance"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::chance, a.value("chance").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_repeat_count", "Set a note's repeat count (repeats/ratchets) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"repeatCount", QJsonObject{{"type","integer"},{"minimum",0}}}}, {"noteId","repeatCount"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::repeatCount, a.value("repeatCount").toInt(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_repeat_rate", "Set a note's repeat rate (beat fraction) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"repeatRate", QJsonObject{{"type","number"},{"minimum",0.0}}}}, {"noteId","repeatRate"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::repeatRate, a.value("repeatRate").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_repeat_curve", "Set a note's repeat curve (bunching, -1.0 to 1.0) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"repeatCurve", QJsonObject{{"type","number"},{"minimum",-1.0},{"maximum",1.0}}}}, {"noteId","repeatCurve"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::repeatCurve, a.value("repeatCurve").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_occurrence", "Set a note's occurrence (cycle-aware bitmask) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"occurrence", QJsonObject{{"type","integer"},{"minimum",0}}}}, {"noteId","occurrence"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::occurrence, a.value("occurrence").toInt(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_recurrence", "Set a note's recurrence (previous-event dependency) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"recurrence", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",2}}}}, {"noteId","recurrence"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::recurrence, a.value("recurrence").toInt(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_gain", "Set a note's per-note gain multiplier (0.0 to 2.0).",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"gain", QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",2.0}}}}, {"noteId","gain"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::noteGain, a.value("gain").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_pan", "Set a note's per-note pan (-1.0 left to 1.0 right).",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"pan", QJsonObject{{"type","number"},{"minimum",-1.0},{"maximum",1.0}}}}, {"noteId","pan"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::notePan, a.value("pan").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_pitch_offset", "Set a note's per-note pitch offset in semitones.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"pitchOffset", QJsonObject{{"type","number"}}}}, {"noteId","pitchOffset"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::notePitch, a.value("pitchOffset").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_timbre", "Set a note's per-note timbre (0.0 dark to 1.0 bright).",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"timbre", QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}}, {"noteId","timbre"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::noteTimbre, a.value("timbre").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_pressure", "Set a note's per-note aftertouch pressure (0.0 to 1.0).",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"pressure", QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}}, {"noteId","pressure"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::notePressure, a.value("pressure").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_clip_seed", "Set the deterministic seed for a clip's operators.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                  {"seed", QJsonObject{{"type","integer"}}}}, {"clipId","seed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            c.setProperty(IDs::seed, static_cast<int64_t>(a.value("seed").toDouble()), &um);
            return McpToolResult::text("ok");
        }});
}

static void registerCompositionTools(McpServer& s, AudioEngine* e)
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
        {"Euclidean",  PhraseGenerator::Euclidean}
    };

    s.registerTool({"set_scale", "Set the project scale (root 0..11, mode 0..20).",
        objSchema({{"root", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"mode", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}}}, {"root","mode"}),
        [e](const QJsonObject& a) {
            e->getProjectModel().setScaleRoot(a.value("root").toInt());
            e->getProjectModel().setScaleMode(a.value("mode").toInt());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_tempo", "Set the project tempo (BPM).",
        objSchema({{"bpm", QJsonObject{{"type","number"},{"minimum",1.0},{"maximum",999.0}}}}, {"bpm"}),
        [e](const QJsonObject& a) {
            e->getProjectCommands().setTempo(a.value("bpm").toDouble());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_time_signature", "Set the project time signature (numerator/denominator).",
        objSchema({{"numerator", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",32}}},
                  {"denominator", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",32}}}}, {"numerator","denominator"}),
        [e](const QJsonObject& a) {
            e->getProjectCommands().setTimeSignature(a.value("numerator").toInt(), a.value("denominator").toInt());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"get_chord_types", "List all available chord types.",
        objSchema({}),
        [](const QJsonObject&) {
            QJsonArray arr;
            for (const auto& ct : PhraseGenerator::getChordTypes()) {
                QJsonObject o;
                o["index"] = ct.index;
                o["name"] = ct.name;
                QJsonArray iv;
                for (int i : ct.intervals) iv.append(i);
                o["intervals"] = iv;
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{{"chordTypes", arr}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_progression_patterns", "List all available progression patterns.",
        objSchema({}),
        [](const QJsonObject&) {
            QJsonArray arr;
            for (const auto& pp : PhraseGenerator::getProgressionPatterns()) {
                QJsonObject o;
                o["index"] = pp.index;
                o["name"] = pp.name;
                QJsonArray ch;
                for (const auto& [deg, ct] : pp.chords) {
                    QJsonObject c; c["degree"] = deg; c["chordType"] = ct;
                    ch.append(c);
                }
                o["chords"] = ch;
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{{"patterns", arr}}).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_scale_modes", "List all available scale modes.",
        objSchema({}),
        [](const QJsonObject&) {
            QJsonArray arr;
            for (const auto& sm : PhraseGenerator::getScaleModes()) {
                QJsonObject o;
                o["index"] = sm.index;
                o["name"] = sm.name;
                QJsonArray iv;
                for (int i : sm.intervals) iv.append(i);
                o["intervals"] = iv;
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{{"scaleModes", arr}}).toJson(QJsonDocument::Compact)));
        }});

    auto generateIntoClip = [e](int trackId, double start, double length,
                                const std::vector<PhraseGenerator::GeneratedNote>& notes) -> McpToolResult {
        auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
        auto tl = m.getTrackListTree();
        if (trackId < 0 || trackId >= tl.getNumChildren())
            return McpToolResult::text("track not found", true);
        double bpm = e->getReadModel().getTransport().bpm;
        double startSec = (bpm > 0) ? start * 60.0 / bpm : start;
        double durSec = (bpm > 0) ? length * 60.0 / bpm : length;
        auto c = ProjectModel::createMidiClipEmpty("Generated", startSec, durSec);
        c.setProperty(IDs::color, static_cast<int>(ProjectModel::trackColorForIndex(trackId)), nullptr);
        auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
        for (const auto& gn : notes)
            nl.addChild(ProjectModel::createMidiNote(gn.noteNumber, gn.velocity, gn.startBeat, gn.durationBeats), -1, nullptr);
        int cid = static_cast<int>(c.getProperty(IDs::clipID));
        tl.getChild(trackId).getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
        return McpToolResult::text(QString("clipId=%1 notes=%2").arg(cid).arg((int) notes.size()));
    };

    s.registerTool({"generate_phrase", "Generate a phrase into a new clip on the given track.",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"style",       QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"Standard","Arpeggio","BassLine","ChordStab","Pad","Lead","RandomWalk","Buildup","Euclidean"}}}},
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
            auto notes = PhraseGenerator::generatePhrase(p);
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
                  {"grid",        QJsonObject{{"type","integer"},{"minimum",1},{"maximum",64}}},
                  {"bars",        QJsonObject{{"type","integer"},{"minimum",1},{"maximum",16}}},
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
                  {"enableChords",    QJsonObject{{"type","boolean"}}}},
                 {"bars"}),
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
            auto r = e->getProjectCommands().generateArrangement(p);
            return McpToolResult::text(QString("tracks=%1 clips=%2 notes=%3 seed=%4")
                .arg(r.trackIndices.size()).arg(r.clipIds.size()).arg(r.noteCount)
                .arg(static_cast<qulonglong>(r.seed)));
        }});

    s.registerTool({"add_instrument_part",
        "Compose a complete instrument part in one command: add a track with an instrument FX slot, generate a phrase, paint it across the arrangement, and optionally gain-stage to a target RMS. One undo unit. Calls the same engine command as the composition.addInstrumentPart RPC.",
        objSchema({{"trackName",    QJsonObject{{"type","string"}}},
                  {"style",        QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"Standard","Arpeggio","BassLine","ChordStab","Pad","Lead","RandomWalk","Buildup","Euclidean"}}}},
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
                 {"trackName","style"}),
        [e](const QJsonObject& a) -> McpToolResult {
            ProjectCommands::InstrumentPartParams p;
            p.trackName = a.value("trackName").toString().toStdString();
            p.style = a.value("style").toString().toStdString();
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
        "Solo-render a plugin — on a temp probe track (trackIndex < 0) or an existing plugin slot — over a short window and report peak/rms/audible so silent-at-default plugins stop being a blocker. programIndex -1 reports the current program. Calls the same engine command as the composition.auditionPlugin RPC.",
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

static void registerArrangerTools(McpServer& s, AudioEngine* e)
{
    // --- Read ---
    s.registerTool({"get_arranger_regions",
        "List all arranger regions (regionID, name, startTime, duration, color).",
        objSchema({}),
        [e](const QJsonObject&) {
            auto regions = e->getReadModel().getArrangerRegions();
            QJsonArray arr;
            for (const auto& r : regions) {
                arr.append(QJsonObject{
                    {"regionID",  QString::fromStdString(r.regionID)},
                    {"name",      QString::fromStdString(r.name)},
                    {"startTime", r.startTime},
                    {"duration",  r.duration},
                    {"color",     r.color}
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"get_arranger_chains",
        "List all arranger chains (chainID, name, isActive, entries).",
        objSchema({}),
        [e](const QJsonObject&) {
            auto chains = e->getReadModel().getArrangerChains();
            QJsonArray arr;
            for (const auto& c : chains) {
                QJsonArray entries;
                for (const auto& en : c.entries) {
                    entries.append(QJsonObject{
                        {"regionID",    QString::fromStdString(en.regionID)},
                        {"repeatCount", en.repeatCount}
                    });
                }
                arr.append(QJsonObject{
                    {"chainID",  QString::fromStdString(c.chainID)},
                    {"name",     QString::fromStdString(c.name)},
                    {"isActive", c.isActive},
                    {"entries",  entries}
                });
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    // --- Arranger Regions ---
    s.registerTool({"add_arranger_region",
        "Add an arranger region. Returns the new regionID.",
        objSchema({{"name",      QJsonObject{{"type","string"}}},
                   {"startTime", QJsonObject{{"type","number"}}},
                   {"duration",  QJsonObject{{"type","number"}}},
                   {"color",     QJsonObject{{"type","integer"}}}},
                  {"name","startTime","duration"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string name = a.value("name").toString().toStdString();
            double start = a.value("startTime").toDouble();
            double dur = a.value("duration").toDouble();
            int color = a.contains("color") ? a.value("color").toInt() : 0xFFd97706;
            auto id = e->getProjectCommands().addArrangerRegion(name, start, dur, color);
            return McpToolResult::text(QString("regionID=%1").arg(QString::fromStdString(id)));
        }});

    s.registerTool({"remove_arranger_region",
        "Remove an arranger region by regionID (destructive).",
        objSchema({{"regionID", QJsonObject{{"type","string"}}}},
                  {"regionID"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string rid = a.value("regionID").toString().toStdString();
            e->getProjectCommands().removeArrangerRegion(rid);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_arranger_region_name",
        "Rename an arranger region.",
        objSchema({{"regionID", QJsonObject{{"type","string"}}},
                   {"name",     QJsonObject{{"type","string"}}}},
                  {"regionID","name"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string rid = a.value("regionID").toString().toStdString();
            std::string name = a.value("name").toString().toStdString();
            e->getProjectCommands().setArrangerRegionName(rid, name);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_arranger_region_bounds",
        "Set an arranger region's startTime and duration.",
        objSchema({{"regionID",  QJsonObject{{"type","string"}}},
                   {"startTime", QJsonObject{{"type","number"}}},
                   {"duration",  QJsonObject{{"type","number"}}}},
                  {"regionID","startTime","duration"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string rid = a.value("regionID").toString().toStdString();
            double start = a.value("startTime").toDouble();
            double dur = a.value("duration").toDouble();
            e->getProjectCommands().setArrangerRegionBounds(rid, start, dur);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_arranger_region_color",
        "Set an arranger region's color (ARGB int).",
        objSchema({{"regionID", QJsonObject{{"type","string"}}},
                   {"color",    QJsonObject{{"type","integer"}}}},
                  {"regionID","color"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string rid = a.value("regionID").toString().toStdString();
            int color = a.value("color").toInt();
            e->getProjectCommands().setArrangerRegionColor(rid, color);
            return McpToolResult::text("ok");
        }});

    // --- Arranger Chains ---
    s.registerTool({"add_arranger_chain",
        "Add an arranger chain. Returns the new chainID.",
        objSchema({{"name", QJsonObject{{"type","string"}}}},
                  {"name"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string name = a.value("name").toString().toStdString();
            auto id = e->getProjectCommands().addArrangerChain(name);
            return McpToolResult::text(QString("chainID=%1").arg(QString::fromStdString(id)));
        }});

    s.registerTool({"remove_arranger_chain",
        "Remove an arranger chain by chainID (destructive).",
        objSchema({{"chainID", QJsonObject{{"type","string"}}}},
                  {"chainID"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string cid = a.value("chainID").toString().toStdString();
            e->getProjectCommands().removeArrangerChain(cid);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_arranger_chain_name",
        "Rename an arranger chain.",
        objSchema({{"chainID", QJsonObject{{"type","string"}}},
                   {"name",    QJsonObject{{"type","string"}}}},
                  {"chainID","name"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string cid = a.value("chainID").toString().toStdString();
            std::string name = a.value("name").toString().toStdString();
            e->getProjectCommands().setArrangerChainName(cid, name);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_arranger_chain_active",
        "Set the active arranger chain.",
        objSchema({{"chainID", QJsonObject{{"type","string"}}}},
                  {"chainID"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string cid = a.value("chainID").toString().toStdString();
            e->getProjectCommands().setArrangerChainActive(cid);
            return McpToolResult::text("ok");
        }});

    // --- Chain Entries ---
    s.registerTool({"add_chain_entry",
        "Add a region entry to an arranger chain. Returns the entry index.",
        objSchema({{"chainID",     QJsonObject{{"type","string"}}},
                   {"regionID",    QJsonObject{{"type","string"}}},
                   {"repeatCount", QJsonObject{{"type","integer"}}}},
                  {"chainID","regionID"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string cid = a.value("chainID").toString().toStdString();
            std::string rid = a.value("regionID").toString().toStdString();
            int repeat = a.contains("repeatCount") ? a.value("repeatCount").toInt() : 1;
            int idx = e->getProjectCommands().addChainEntry(cid, rid, repeat);
            return McpToolResult::text(QString("entryIndex=%1").arg(idx));
        }});

    s.registerTool({"remove_chain_entry",
        "Remove an entry from an arranger chain by index.",
        objSchema({{"chainID",   QJsonObject{{"type","string"}}},
                   {"entryIndex", QJsonObject{{"type","integer"}}}},
                  {"chainID","entryIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string cid = a.value("chainID").toString().toStdString();
            int idx = a.value("entryIndex").toInt();
            e->getProjectCommands().removeChainEntry(cid, idx);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"reorder_chain_entry",
        "Move an entry within an arranger chain from fromIndex to toIndex.",
        objSchema({{"chainID",  QJsonObject{{"type","string"}}},
                   {"fromIndex", QJsonObject{{"type","integer"}}},
                   {"toIndex",   QJsonObject{{"type","integer"}}}},
                  {"chainID","fromIndex","toIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string cid = a.value("chainID").toString().toStdString();
            int from = a.value("fromIndex").toInt();
            int to = a.value("toIndex").toInt();
            e->getProjectCommands().reorderChainEntry(cid, from, to);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_chain_entry_repeat",
        "Set the repeat count for a chain entry.",
        objSchema({{"chainID",    QJsonObject{{"type","string"}}},
                   {"entryIndex", QJsonObject{{"type","integer"}}},
                   {"repeatCount", QJsonObject{{"type","integer"}}}},
                  {"chainID","entryIndex","repeatCount"}),
        [e](const QJsonObject& a) -> McpToolResult {
            std::string cid = a.value("chainID").toString().toStdString();
            int idx = a.value("entryIndex").toInt();
            int repeat = a.value("repeatCount").toInt();
            e->getProjectCommands().setChainEntryRepeat(cid, idx, repeat);
            return McpToolResult::text("ok");
        }});

    // --- Flatten ---
    s.registerTool({"flatten_arranger",
        "Flatten the arranger: expand all chain regions into actual clips on the timeline.",
        objSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            e->getProjectCommands().flattenArranger();
            return McpToolResult::text("ok");
        }});
}

static void registerProjectSaveLoadTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"save_project", "Save the project to a file.",
        objSchema({{"filePath", QJsonObject{{"type","string"}}}}, {"filePath"}),
        [e](const QJsonObject& a) {
            auto path = a.value("filePath").toString();
            juce::File f(juce::String(path.toUtf8().constData()));
            bool ok = HDAW::ProjectSerializer::save(e->getProjectModel(), f, e->getMainProcessor());
            if (ok)
                HDAW::backupProject(f);
            return McpToolResult::text(ok ? "saved" : "save failed", !ok);
        }});

    s.registerTool({"load_project", "Load a project from a file (replaces current project).",
        objSchema({{"filePath", QJsonObject{{"type","string"}}}}, {"filePath"}),
        [e](const QJsonObject& a) {
            auto path = a.value("filePath").toString();
            juce::File f(juce::String(path.toUtf8().constData()));
            bool ok = HDAW::ProjectSerializer::load(e->getProjectModel(), f);
            if (ok) {
                auto* proc = e->getMainProcessor();
                if (proc) proc->rebuildRoutingGraph();
            }
            return McpToolResult::text(ok ? "loaded" : "load failed", !ok);
        }});

    s.registerTool({"new_project", "Create a new empty project.",
        objSchema({}),
        [e](const QJsonObject&) {
            HDAW::ProjectSerializer::createNew(e->getProjectModel());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"project_info", "Return project file metadata (provenance, format version, timestamps).",
        objSchema({}),
        [e](const QJsonObject&) {
            auto& tree = e->getProjectModel().getTree();
            QJsonObject o{
                { "createdWithApp", jstr(tree.getProperty(IDs::createdWithApp, "unknown").toString()) },
                { "savedWithApp",   jstr(tree.getProperty(IDs::savedWithApp,   "unknown").toString()) },
                { "formatVersion",  static_cast<int>(tree.getProperty(IDs::formatVersion, 0)) },
                { "createdAt",      jstr(tree.getProperty(IDs::createdAt,      "").toString()) },
                { "lastSavedAt",    jstr(tree.getProperty(IDs::lastSavedAt,    "").toString()) },
            };
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(o).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"scan_plugins", "Scan for VST3/CLAP plugins (may take a minute).",
        objSchema({}),
        [e](const QJsonObject&) {
            e->getPluginManager().scanAll();
            int count = static_cast<int>(e->getPluginManager().getPlugins().size());
            return McpToolResult::text(QString("scanned %1 plugins").arg(count));
        }});

    s.registerTool({"list_plugins", "List all scanned plugins.",
        objSchema({}),
        [e](const QJsonObject&) {
            auto& pm = e->getPluginManager();
            QJsonArray arr;
            for (const auto& pd : pm.getPlugins()) {
                QJsonObject o;
                o["name"] = jstr(pd.name);
                o["manufacturer"] = jstr(pd.manufacturerName);
                o["format"] = jstr(pd.pluginFormatName);
                o["category"] = jstr(pd.category);
                o["id"] = jstr(pd.createIdentifierString());
                auto* presetInfo = pm.getPresetInfo(pd.createIdentifierString());
                if (presetInfo && presetInfo->numPrograms > 1) {
                    o["hasPresets"] = true;
                    o["presetCount"] = presetInfo->numPrograms;
                } else {
                    o["hasPresets"] = false;
                    o["presetCount"] = 0;
                }
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{{"plugins", arr}}).toJson(QJsonDocument::Compact)));
        }});
}

static void registerCcTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_cc_point", "Add a MIDI CC point to a clip; returns ccId.",
        objSchema({{"clipId",           QJsonObject{{"type","integer"}}},
                  {"controllerNumber", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"beat",             QJsonObject{{"type","number"}}},
                  {"value",            QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}},
                 {"clipId","controllerNumber","beat","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            if (c.getProperty(IDs::clipType).toString() != juce::String("midi"))
                return McpToolResult::text("clip is not MIDI", true);
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            auto cl = c.getChildWithName(IDs::CC_LIST);
            if (!cl.isValid()) { cl = juce::ValueTree(IDs::CC_LIST); c.addChild(cl, -1, nullptr); }
            juce::ValueTree pt(IDs::CC_POINT);
            int cid = m.allocateCcID();
            pt.setProperty(IDs::ccID, cid, nullptr);
            pt.setProperty(IDs::controllerNumber, a.value("controllerNumber").toInt(), &um);
            pt.setProperty(IDs::beat, a.value("beat").toDouble(), &um);
            pt.setProperty(IDs::value, a.value("value").toInt(), &um);
            cl.addChild(pt, -1, &um);
            return McpToolResult::text(QString("ccId=%1").arg(cid));
        }});

    s.registerTool({"get_cc_points", "List CC points in a clip (optionally one controller).",
        objSchema({{"clipId",           QJsonObject{{"type","integer"}}},
                  {"controllerNumber", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}}, {"clipId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto cl = c.getChildWithName(IDs::CC_LIST);
            QJsonArray arr;
            if (cl.isValid()) {
                bool filter = a.contains("controllerNumber");
                int wanted = a.value("controllerNumber").toInt();
                for (int i = 0; i < cl.getNumChildren(); ++i) {
                    auto pt = cl.getChild(i);
                    int cc = static_cast<int>(pt.getProperty(IDs::controllerNumber));
                    if (filter && cc != wanted) continue;
                    arr.append(QJsonObject{
                        {"ccId",             static_cast<int>(pt.getProperty(IDs::ccID))},
                        {"controllerNumber", cc},
                        {"beat",             static_cast<double>(pt.getProperty(IDs::beat))},
                        {"value",            static_cast<int>(pt.getProperty(IDs::value))}
                    });
                }
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_cc_point", "Update a CC point's beat and/or value (partial).",
        objSchema({{"ccId",  QJsonObject{{"type","integer"}}},
                  {"beat",  QJsonObject{{"type","number"}}},
                  {"value", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}}, {"ccId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto pt = findCcPoint(e, a.value("ccId").toInt(), &dummy);
            if (!pt.isValid()) return McpToolResult::text("cc point not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            if (a.contains("beat"))  pt.setProperty(IDs::beat, a.value("beat").toDouble(), &um);
            if (a.contains("value")) pt.setProperty(IDs::value, a.value("value").toInt(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"remove_cc_point", "Remove a CC point by ccId (destructive).",
        objSchema({{"ccId",   QJsonObject{{"type","integer"}}},
                  {"dryRun", QJsonObject{{"type","boolean"}}}}, {"ccId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = 0; auto pt = findCcPoint(e, a.value("ccId").toInt(), &clipId);
            if (!pt.isValid()) return McpToolResult::text("cc point not found", true);
            if (a.value("dryRun").toBool())
                return McpToolResult::text(QString("would remove ccId=%1 (clipId=%2, CC%3 @ beat %4)")
                    .arg(a.value("ccId").toInt()).arg(clipId)
                    .arg(static_cast<int>(pt.getProperty(IDs::controllerNumber)))
                    .arg(static_cast<double>(pt.getProperty(IDs::beat))));
            auto& um = e->getProjectModel().getUndoManager();
            pt.getParent().removeChild(pt, &um);
            return McpToolResult::text("ok");
        }});
}

void registerProjectDomain(McpServer& s, AudioEngine* e)
{
    registerReadTools(s, e);
    registerTrackTools(s, e);
    registerClipTools(s, e);
    registerNoteTools(s, e);
    registerCcTools(s, e);
    registerCompositionTools(s, e);
    registerArrangerTools(s, e);
    registerProjectSaveLoadTools(s, e);
}

} // namespace mcp
