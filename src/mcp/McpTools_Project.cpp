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
#include "../engine/ProjectSerializer.h"
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
            for (int i = 0; i < tl.getNumChildren(); ++i) {
                if (wanted >= 0 && wanted != i) continue;
                auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
                for (int j = 0; j < cl.getNumChildren(); ++j) {
                    auto c = cl.getChild(j);
                    arr.append(QJsonObject{
                        {"id", static_cast<int>(c.getProperty(IDs::clipID))},
                        {"trackId", i},
                        {"name", jstr(c.getProperty(IDs::name).toString())},
                        {"start", static_cast<double>(c.getProperty(IDs::startTime))},
                        {"duration", static_cast<double>(c.getProperty(IDs::duration))},
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
                    QJsonObject out{
                        {"id", cid}, {"trackId", i},
                        {"name", jstr(c.getProperty(IDs::name).toString())},
                        {"start", static_cast<double>(c.getProperty(IDs::startTime))},
                        {"duration", static_cast<double>(c.getProperty(IDs::duration))},
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
            bool routingOk = false;
            if (auto* rm = e->getMainProcessor()->getRoutingManager()) {
                rm->addTrack(idx, t);
                routingOk = rm->getTrackNode(idx) != nullptr;
            }
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
                  {"color",  QJsonObject{{"type","integer"}}}}, {"trackId"}),
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
            bool routingOk = false;
            if (auto* rm = e->getMainProcessor()->getRoutingManager())
            {
                auto newTrack = tl.getChild(newIdx);
                rm->addTrack(newIdx, newTrack);
                routingOk = rm->getTrackNode(newIdx) != nullptr;
            }
            return McpToolResult::text(
                QString("trackId=%1 routed=%2").arg(newIdx).arg(routingOk ? "1" : "0"));
        }});

    s.registerTool({"add_track_with_fx",
        "Add a track with an FX slot. fxType in {eq,compressor,reverb,delay}, or provide pluginId for a VST3/CLAP plugin.",
        objSchema({{"name",     QJsonObject{{"type","string"}}},
                   {"fxType",   QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"eq","compressor","reverb","delay"}}}},
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

            bool routingOk = false;
            if (auto* rm = e->getMainProcessor()->getRoutingManager())
            {
                rm->addTrack(idx, t);
                routingOk = rm->getTrackNode(idx) != nullptr;
            }
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
            auto c = ProjectModel::createMidiClipEmpty(
                juce::String(a.value("name").toString("MIDI Clip").toUtf8().constData()),
                a.value("start").toDouble(), a.value("length").toDouble());
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
            auto c = ProjectModel::createAudioClip(
                juce::String(a.value("name").toString("Audio Clip").toUtf8().constData()),
                a.value("start").toDouble(), a.value("length").toDouble(),
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
            if (a.contains("start")) c.setProperty(IDs::startTime, a.value("start").toDouble(), &um);
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
            if (a.contains("name"))     c.setProperty(IDs::name, juce::String(a.value("name").toString().toUtf8().constData()), &um);
            if (a.contains("start"))    c.setProperty(IDs::startTime, a.value("start").toDouble(), &um);
            if (a.contains("duration")) c.setProperty(IDs::duration, a.value("duration").toDouble(), &um);
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
            double ns = a.contains("start") ? a.value("start").toDouble()
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
        {"Buildup",    PhraseGenerator::Buildup}
    };

    s.registerTool({"set_scale", "Set the project scale (root 0..11, mode 0..20).",
        objSchema({{"root", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"mode", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}}}, {"root","mode"}),
        [e](const QJsonObject& a) {
            e->getProjectModel().setScaleRoot(a.value("root").toInt());
            e->getProjectModel().setScaleMode(a.value("mode").toInt());
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
        auto c = ProjectModel::createMidiClipEmpty("Generated", start, length);
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
                      {"enum", QJsonArray{"Standard","Arpeggio","BassLine","ChordStab","Pad","Lead","RandomWalk","Buildup"}}}},
                  {"length",      QJsonObject{{"type","number"}}},
                  {"density",     QJsonObject{{"type","integer"}}},
                  {"start",       QJsonObject{{"type","number"}}},
                  {"lowNote",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"highNote",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"noteDuration",QJsonObject{{"type","number"}}},
                  {"minVelocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"maxVelocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"scaleRoot",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"scaleMode",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}}},
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
                  {"scaleMode",   QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}}},
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
                  {"scaleMode",        QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}}},
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
            auto notes = PhraseGenerator::generateProgression(p);
            const auto& pats = PhraseGenerator::getProgressionPatterns();
            int patIdx = std::clamp(p.patternIndex, 0, (int)pats.size() - 1);
            double total = p.beatsPerChord * pats[patIdx].chords.size();
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0), total, notes);
        }});
}

static void registerProjectSaveLoadTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"save_project", "Save the project to a file.",
        objSchema({{"filePath", QJsonObject{{"type","string"}}}}, {"filePath"}),
        [e](const QJsonObject& a) {
            auto path = a.value("filePath").toString();
            juce::File f(juce::String(path.toUtf8().constData()));
            bool ok = HDAW::ProjectSerializer::save(e->getProjectModel(), f);
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
            QJsonArray arr;
            for (const auto& pd : e->getPluginManager().getPlugins()) {
                QJsonObject o;
                o["name"] = jstr(pd.name);
                o["manufacturer"] = jstr(pd.manufacturerName);
                o["format"] = jstr(pd.pluginFormatName);
                o["category"] = jstr(pd.category);
                o["id"] = jstr(pd.createIdentifierString());
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{{"plugins", arr}}).toJson(QJsonDocument::Compact)));
        }});
}

void registerProjectDomain(McpServer& s, AudioEngine* e)
{
    registerReadTools(s, e);
    registerTrackTools(s, e);
    registerClipTools(s, e);
    registerNoteTools(s, e);
    registerCompositionTools(s, e);
    registerProjectSaveLoadTools(s, e);
}

} // namespace mcp
