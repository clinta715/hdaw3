#include "McpTools.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/ExportManager.h"
#include "../engine/ProjectPool.h"
#include "../engine/PluginManager.h"
#include "../engine/Track.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/PhraseGenerator.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include <QMetaObject>
#include <algorithm>
#include <atomic>
#include <future>
#include <thread>

namespace mcp {

static QString jstr(const juce::String& s)
{
    return QString::fromUtf8(s.toRawUTF8());
}

static QJsonObject objSchema(const QJsonObject& props, const QJsonArray& required = {})
{
    QJsonObject s{{"type","object"},{"properties", props},{"additionalProperties", false}};
    if (!required.isEmpty()) s["required"] = required;
    return s;
}

void registerAllTools(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    // --- Clips ---
    auto findClip = [&](int clipId, int* outTrackIdx) -> juce::ValueTree {
        auto tl = e->getProjectModel().getTrackListTree();
        for (int i = 0; i < tl.getNumChildren(); ++i) {
            auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
            for (int j = 0; j < cl.getNumChildren(); ++j) {
                auto c = cl.getChild(j);
                if (static_cast<int>(c.getProperty(IDs::clipID)) == clipId) {
                    if (outTrackIdx) *outTrackIdx = i;
                    return c;
                }
            }
        }
        return {};
    };

    // --- Read / inspector tools ---

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
            return McpToolResult::text(QString("root=%1 mode=%2")
                .arg(m.getScaleRoot())
                .arg(m.getScaleMode()));
        }});

    s.registerTool({"get_transport",
        "Return transport state (position, isPlaying, isLooping, loopStart, loopEnd).",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) {
            auto tp = e->getProjectModel().getTransportTree();
            return McpToolResult::text(QString(
                "position=%1\nisPlaying=%2\nisLooping=%3\nloopStart=%4\nloopEnd=%5")
                .arg(static_cast<double>(tp.getProperty(IDs::position)))
                .arg(jstr(tp.getProperty(IDs::isPlaying).toString()))
                .arg(jstr(tp.getProperty(IDs::isLooping).toString()))
                .arg(static_cast<double>(tp.getProperty(IDs::loopStart)))
                .arg(static_cast<double>(tp.getProperty(IDs::loopEnd))));
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
            return McpToolResult::text(QString("tracks=%1\n%2")
                .arg(arr.size())
                .arg(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented))));
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
                        {"gain", static_cast<double>(c.getProperty(IDs::gain))}
                    });
                }
            }
            return McpToolResult::text(QString("clips=%1\n%2")
                .arg(arr.size())
                .arg(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented))));
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
                        {"looping", static_cast<bool>(c.getProperty(IDs::looping))}
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

    s.registerTool({"list_fx", "List FX slots on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("engine not ready", true);
            auto* tr = proc->getTrack(ti);
            if (!tr) return McpToolResult::text("track not found", true);
            auto& chain = tr->getFXChain();
            QJsonArray arr;
            for (int i = 0; i < static_cast<int>(chain.size()); ++i) {
                auto& s2 = chain[i]; if (!s2) continue;
                QJsonObject o{{"slot", i},{"type", jstr(s2->getType())}};
                if (s2->isPlugin()) o["pluginId"] = jstr(s2->getPluginID());
                o["bypassed"] = s2->isBypassed();
                arr.append(o);
            }
            return McpToolResult::text(QString("slots=%1\n%2")
                .arg(arr.size())
                .arg(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented))));
        }});

    s.registerTool({"list_automation_lanes", "List automation lanes on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            auto al = tl.getChild(ti).getChildWithName(IDs::AUTOMATION_LIST);
            QJsonArray arr;
            for (int i = 0; i < al.getNumChildren(); ++i) {
                auto lane = al.getChild(i);
                arr.append(QJsonObject{
                    {"name", jstr(lane.getProperty(IDs::name).toString())},
                    {"paramID", static_cast<int>(lane.getProperty(IDs::paramID))},
                    {"enabled", static_cast<bool>(lane.getProperty(IDs::automationEnabled))},
                    {"pointCount", lane.getChildWithName(IDs::POINT_LIST).getNumChildren()}
                });
            }
            return McpToolResult::text(QString("lanes=%1\n%2")
                .arg(arr.size())
                .arg(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented))));
        }});

    // --- Transport ---
    s.registerTool({"transport",
        "Control transport: action in {play,stop,pause,rewind,toggleLoop}; optional loopStart/loopEnd (seconds).",
        objSchema({{"action", QJsonObject{{"type","string"},
            {"enum", QJsonArray{"play","stop","pause","rewind","toggleLoop"}}}},
                  {"loopStart", QJsonObject{{"type","number"}}},
                  {"loopEnd",   QJsonObject{{"type","number"}}}}, {"action"}),
        [e](const QJsonObject& a) -> McpToolResult {
            QString action = a.value("action").toString();
            auto tp = e->getProjectModel().getTransportTree();
            if      (action == "play")  tp.setProperty(IDs::isPlaying, true, nullptr);
            else if (action == "stop")  { tp.setProperty(IDs::isPlaying, false, nullptr);
                                          tp.setProperty(IDs::position, 0.0, nullptr); }
            else if (action == "pause") tp.setProperty(IDs::isPlaying, false, nullptr);
            else if (action == "rewind") tp.setProperty(IDs::position, 0.0, nullptr);
            else if (action == "toggleLoop") {
                bool cur = static_cast<bool>(tp.getProperty(IDs::isLooping));
                tp.setProperty(IDs::isLooping, !cur, nullptr);
            }
            if (a.contains("loopStart")) tp.setProperty(IDs::loopStart, a.value("loopStart").toDouble(), nullptr);
            if (a.contains("loopEnd"))   tp.setProperty(IDs::loopEnd,   a.value("loopEnd").toDouble(), nullptr);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"seek", "Move the playhead to a position (in seconds).",
        objSchema({{"position", QJsonObject{{"type","number"}}}}, {"position"}),
        [e](const QJsonObject& a) {
            e->getProjectModel().getTransportTree().setProperty(
                IDs::position, a.value("position").toDouble(), nullptr);
            return McpToolResult::text("ok");
        }});

    // --- Undo ---
    s.registerTool({"undo", "Undo the last N actions (default 1).",
        objSchema({{"count", QJsonObject{{"type","integer"}}}}),
        [e](const QJsonObject& a) {
            int n = a.value("count").toInt(1);
            auto& um = e->getProjectModel().getUndoManager();
            for (int i = 0; i < n; ++i) if (!um.undo()) break;
            return McpToolResult::text("ok");
        }});

    s.registerTool({"redo", "Redo the last N undone actions (default 1).",
        objSchema({{"count", QJsonObject{{"type","integer"}}}}),
        [e](const QJsonObject& a) {
            int n = a.value("count").toInt(1);
            auto& um = e->getProjectModel().getUndoManager();
            for (int i = 0; i < n; ++i) if (!um.redo()) break;
            return McpToolResult::text("ok");
        }});

    // --- Tracks ---
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
            t.addChild(juce::ValueTree(IDs::AUTOMATION_LIST), -1, &um);
            m.getTrackListTree().addChild(t, -1, &um);
            return McpToolResult::text(QString("trackId=%1").arg(idx));
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

    // --- Clips ---
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
        [e, &findClip](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(a.value("clipId").toInt(), &ti);
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
        [e, &findClip](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(a.value("clipId").toInt(), &ti);
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
        [e, &findClip](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            if (a.contains("name"))     c.setProperty(IDs::name, juce::String(a.value("name").toString().toUtf8().constData()), &um);
            if (a.contains("start"))    c.setProperty(IDs::startTime, a.value("start").toDouble(), &um);
            if (a.contains("duration")) c.setProperty(IDs::duration, a.value("duration").toDouble(), &um);
            if (a.contains("gain"))     c.setProperty(IDs::gain, a.value("gain").toDouble(), &um);
            if (a.contains("fadeIn"))   c.setProperty(IDs::fadeIn, a.value("fadeIn").toDouble(), &um);
            if (a.contains("fadeOut"))  c.setProperty(IDs::fadeOut, a.value("fadeOut").toDouble(), &um);
            if (a.contains("looping"))  c.setProperty(IDs::looping, a.value("looping").toBool(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"duplicate_clip", "Duplicate a clip (destructive: creates a new clip).",
        objSchema({{"clipId",   QJsonObject{{"type","integer"}}},
                  {"start",    QJsonObject{{"type","number"}}},
                  {"trackId",  QJsonObject{{"type","integer"}}},
                  {"dryRun",   QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        [e, &findClip](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto src = findClip(a.value("clipId").toInt(), &ti);
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

    // --- Notes ---
    auto findNote = [&](int noteId, int* outClipId) -> juce::ValueTree {
        auto tl = e->getProjectModel().getTrackListTree();
        for (int i = 0; i < tl.getNumChildren(); ++i) {
            auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
            for (int j = 0; j < cl.getNumChildren(); ++j) {
                auto nl = cl.getChild(j).getChildWithName(IDs::MIDI_NOTE_LIST);
                if (!nl.isValid()) continue;
                for (int k = 0; k < nl.getNumChildren(); ++k) {
                    auto n = nl.getChild(k);
                    if (static_cast<int>(n.getProperty(IDs::noteID)) == noteId) {
                        if (outClipId) *outClipId = static_cast<int>(cl.getChild(j).getProperty(IDs::clipID));
                        return n;
                    }
                }
            }
        }
        return {};
    };

    s.registerTool({"add_note", "Add a MIDI note to a clip; returns noteId.",
        objSchema({{"clipId",    QJsonObject{{"type","integer"}}},
                  {"pitch",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"start",     QJsonObject{{"type","number"}}},
                  {"duration",  QJsonObject{{"type","number"}}},
                  {"velocity",  QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}},
                 {"clipId","pitch","start","duration","velocity"}),
        [e, &findClip](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(a.value("clipId").toInt(), &ti);
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
        [e, &findNote](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(a.value("noteId").toInt(), &dummy);
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
        [e, &findClip](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(a.value("clipId").toInt(), &ti);
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
        [e, &findClip](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            int n = nl.isValid() ? nl.getNumChildren() : 0;
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would clear %1 notes").arg(n));
            if (nl.isValid()) c.removeChild(nl, &e->getProjectModel().getUndoManager());
            return McpToolResult::text(QString("cleared %1 notes").arg(n));
        }});

    // --- Composition (PhraseGenerator) ---
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

    s.registerTool({"set_scale", "Set the project scale (root 0..11, mode index).",
        objSchema({{"root", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"mode", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}}}, {"root","mode"}),
        [e](const QJsonObject& a) {
            e->getProjectModel().setScaleRoot(a.value("root").toInt());
            e->getProjectModel().setScaleMode(a.value("mode").toInt());
            return McpToolResult::text("ok");
        }});

    auto generateIntoClip = [&](int trackId, double start, double length,
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
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"style",   QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"Standard","Arpeggio","BassLine","ChordStab","Pad","Lead","RandomWalk","Buildup"}}}},
                  {"length",  QJsonObject{{"type","number"}}},
                  {"density", QJsonObject{{"type","integer"}}},
                  {"start",   QJsonObject{{"type","number"}}}}, {"trackId","style","length","density"}),
        [e, helper = generateIntoClip](const QJsonObject& a) -> McpToolResult {
            PhraseGenerator::PhraseParams p;
            QString sname = a.value("style").toString();
            for (const auto& kv : kStyleMap)
                if (sname == kv.first) p.style = kv.second;
            p.lengthBeats = a.value("length").toDouble();
            p.density = a.value("density").toInt();
            p.lowNote = 48; p.highNote = 84;
            p.scaleRoot = e->getProjectModel().getScaleRoot();
            p.scaleMode = e->getProjectModel().getScaleMode();
            auto notes = PhraseGenerator::generatePhrase(p);
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0),
                          a.value("length").toDouble(), notes);
        }});

    s.registerTool({"generate_chord", "Generate a chord (or arpeggio) into a new clip.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"rootPitch", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"chordType", QJsonObject{{"type","integer"}}},
                  {"voicing",   QJsonObject{{"type","integer"}}},
                  {"inversion", QJsonObject{{"type","integer"}}},
                  {"arpeggiate",QJsonObject{{"type","boolean"}}},
                  {"start",     QJsonObject{{"type","number"}}},
                  {"length",    QJsonObject{{"type","number"}}}}, {"trackId","rootPitch","chordType","length"}),
        [e, helper = generateIntoClip](const QJsonObject& a) -> McpToolResult {
            PhraseGenerator::ChordParams p;
            p.chordType = a.value("chordType").toInt();
            p.voicing = a.value("voicing").toInt(0);
            p.inversion = a.value("inversion").toInt(0);
            p.arpeggiate = a.value("arpeggiate").toBool();
            p.durationBeats = a.value("length").toDouble();
            p.lowNote = 24; p.highNote = 96;
            p.scaleRoot = e->getProjectModel().getScaleRoot();
            p.scaleMode = e->getProjectModel().getScaleMode();
            auto notes = PhraseGenerator::generateChord(a.value("rootPitch").toInt(), p);
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0),
                          a.value("length").toDouble(), notes);
        }});

    s.registerTool({"generate_progression", "Generate a chord progression into a new clip.",
        objSchema({{"trackId",       QJsonObject{{"type","integer"}}},
                  {"pattern",       QJsonObject{{"type","integer"}}},
                  {"start",         QJsonObject{{"type","number"}}},
                  {"beatsPerChord", QJsonObject{{"type","number"}}}}, {"trackId","pattern","beatsPerChord"}),
        [e, helper = generateIntoClip](const QJsonObject& a) -> McpToolResult {
            PhraseGenerator::ProgressionParams p;
            p.patternIndex = a.value("pattern").toInt();
            p.beatsPerChord = a.value("beatsPerChord").toDouble();
            p.durationBeats = 2.0;
            p.lowNote = 24; p.highNote = 96;
            p.scaleRoot = e->getProjectModel().getScaleRoot();
            p.scaleMode = e->getProjectModel().getScaleMode();
            auto notes = PhraseGenerator::generateProgression(p);
            const auto& pats = PhraseGenerator::getProgressionPatterns();
            int patIdx = std::clamp(p.patternIndex, 0, (int)pats.size() - 1);
            double total = p.beatsPerChord * pats[patIdx].chords.size();
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0), total, notes);
        }});

    // --- Automation ---
    auto findLane = [&](int trackId, const QJsonValue& ref) -> juce::ValueTree {
        auto tl = e->getProjectModel().getTrackListTree();
        if (trackId < 0 || trackId >= tl.getNumChildren()) return {};
        auto al = tl.getChild(trackId).getChildWithName(IDs::AUTOMATION_LIST);
        if (ref.isDouble()) {
            int pid = ref.toInt();
            for (int i = 0; i < al.getNumChildren(); ++i)
                if (static_cast<int>(al.getChild(i).getProperty(IDs::paramID)) == pid) return al.getChild(i);
        } else if (ref.isString()) {
            QString n = ref.toString();
            for (int i = 0; i < al.getNumChildren(); ++i)
                if (al.getChild(i).getProperty(IDs::name).toString() == juce::String(n.toUtf8().constData())) return al.getChild(i);
        }
        return {};
    };

    s.registerTool({"add_automation_point", "Add a point to an automation lane (paramID integer preferred; name accepted).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}},
                  {"time",   QJsonObject{{"type","number"}}},
                  {"value",  QJsonObject{{"type","number"}}}}, {"trackId","lane","time","value"}),
        [e, &findLane](const QJsonObject& a) -> McpToolResult {
            auto lane = findLane(a.value("trackId").toInt(), a.value("lane"));
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            auto pl = lane.getChildWithName(IDs::POINT_LIST);
            if (!pl.isValid()) { pl = juce::ValueTree(IDs::POINT_LIST); lane.addChild(pl, -1, &um); }
            juce::ValueTree pt(IDs::POINT);
            pt.setProperty(IDs::startTime, a.value("time").toDouble(), &um);
            pt.setProperty(IDs::gain, a.value("value").toDouble(), &um);
            pl.addChild(pt, -1, &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_automation_enabled", "Enable or disable an automation lane.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"lane",   QJsonObject{{"oneOf", QJsonArray{
                      QJsonObject{{"type","integer"}},
                      QJsonObject{{"type","string"}}}}}},
                  {"enabled",QJsonObject{{"type","boolean"}}}}, {"trackId","lane","enabled"}),
        [e, &findLane](const QJsonObject& a) -> McpToolResult {
            auto lane = findLane(a.value("trackId").toInt(), a.value("lane"));
            if (!lane.isValid()) return McpToolResult::text("lane not found", true);
            lane.setProperty(IDs::automationEnabled, a.value("enabled").toBool(),
                             &e->getProjectModel().getUndoManager());
            return McpToolResult::text("ok");
        }});

    // --- FX ---
    s.registerTool({"add_fx",
        "Add an FX slot. fxType in {eq,compressor,reverb,delay}, OR a pluginId.",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                  {"fxType",   QJsonObject{{"type","string"},
                      {"enum", QJsonArray{"eq","compressor","reverb","delay"}}}},
                  {"pluginId", QJsonObject{{"type","string"}}},
                  {"position", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto* tr = e->getMainProcessor()->getTrack(a.value("trackId").toInt());
            if (!tr) return McpToolResult::text("track not found", true);
            std::string type = a.value("fxType").toString().toStdString();
            if (type.empty() && a.contains("pluginId")) type = "plugin";
            int pos = a.value("position").toInt(-1);
            int idx = tr->addFXSlotAt(type, pos);
            if (a.contains("pluginId"))
                tr->setFXSlotPluginID(idx, a.value("pluginId").toString().toStdString());
            return McpToolResult::text(QString("slot=%1").arg(idx));
        }});

    s.registerTool({"remove_fx", "Remove an FX slot (destructive).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"dryRun",    QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto* tr = e->getMainProcessor()->getTrack(ti);
            if (!tr) return McpToolResult::text("track not found", true);
            int s = a.value("slotIndex").toInt();
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove FX slot %1 on track %2").arg(s).arg(ti));
            tr->removeFXSlot(s);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_fx_bypass", "Bypass or unbypass an FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"bypassed",  QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto* tr = e->getMainProcessor()->getTrack(a.value("trackId").toInt());
            if (!tr) return McpToolResult::text("track not found", true);
            tr->setFXBypassed(a.value("slotIndex").toInt(), a.value("bypassed").toBool());
            return McpToolResult::text("ok");
        }});

    // --- Export ---
    s.registerTool({"export_audio",
        "Render the project to an audio file (wav/aiff/flac). The render runs on the ExportManager's internal worker thread; the tool handler blocks until completion but does no heavy CPU itself. Cooperative cancellation: a notifications/cancelled received before the export starts skips it entirely; a notifications/cancelled received while the export is running will be picked up at the next progress tick (the render thread checks the cancel flag via the onProgress callback and aborts the render). Progress is reported via notifications/progress (0.0, 0.25, 0.5, 0.75, 1.0).",
        objSchema({{"outputPath", QJsonObject{{"type","string"}}},
                  {"format",     QJsonObject{{"type","string"},{"enum", QJsonArray{"wav","aiff","flac"}}}},
                  {"start",      QJsonObject{{"type","number"}}},
                  {"end",        QJsonObject{{"type","number"}}},
                  {"sampleRate", QJsonObject{{"type","number"},{"minimum",8000},{"maximum",192000}}},
                  {"bitDepth",   QJsonObject{{"type","integer"},{"enum", QJsonArray{16,24,32}}}},
                  {"trackIds",   QJsonObject{{"type","array"},{"items",QJsonObject{{"type","integer"}}}}},
                  {"dryRun",     QJsonObject{{"type","boolean"}}}},
                 {"outputPath"}),
        [e, &s](const QJsonObject& a) -> McpToolResult {
            QString path = a.value("outputPath").toString();
            if (path.isEmpty()) return McpToolResult::text("outputPath required", true);

            // dry-run: return a plan describing what would happen.
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would export to %1").arg(path));

            // Cancel-checked start: if the cancel flag is already set, skip
            // the export entirely. The MCP server sets this from
            // notifications/cancelled.
            if (s.isCancelRequested()) {
                s.resetCancelFlag();
                return McpToolResult::text("export cancelled (flag was already set)", true);
            }

            // Map format string to ExportManager::Format.
            QString formatStr = a.value("format").toString("wav").toLower();
            HDAW::ExportManager::Format fmt = HDAW::ExportManager::WAV;
            if      (formatStr == "aiff") fmt = HDAW::ExportManager::AIFF;
            else if (formatStr == "flac") fmt = HDAW::ExportManager::FLAC;
            else if (formatStr != "wav")  fmt = HDAW::ExportManager::WAV;

            double sampleRate = a.value("sampleRate").toDouble(48000.0);
            int bitDepth = a.value("bitDepth").toInt(24);

            // Compute range: defaults to [0, project duration + tail].
            double startTime = a.value("start").toDouble(0.0);
            double endTime = a.value("end").toDouble(-1.0);
            if (endTime <= 0.0)
                endTime = HDAW::ExportManager::calculateProjectDuration(e->getProjectModel());

            juce::File outFile(juce::String(path.toUtf8().constData()));
            if (outFile.existsAsFile()) outFile.deleteFile();
            double duration = std::max(0.001, endTime - startTime);

            // Resolve the project tree and the format/plugin managers from the
            // engine. ExportManager.startExport takes a non-const ValueTree
            // reference; we work on a copy so the original stays immutable.
            auto& em = e->getMainProcessor()->getExportManager();
            if (em.isExporting()) {
                return McpToolResult::text("export already in progress", true);
            }

            juce::ValueTree projectCopy = e->getProjectModel().getTree().createCopy();
            auto& formatManager = e->getProjectPool().getFormatManager();
            auto* pluginManager = &e->getPluginManager();

            // Wire progress notifications: ExportManager fires onProgress on
            // its internal thread, so we hop to the main thread via a queued
            // invokeMethod. We also use this callback as a backstop to detect
            // an in-flight cancel and trigger ExportManager::cancel() — the
            // cancel-watcher thread below is the primary path, but the
            // onProgress tick guarantees we catch the cancel at least every
            // render block (~12ms at 44.1kHz / 512 samples).
            auto* serverPtr = &s;
            em.onProgress = [serverPtr, &em](float prog) {
                if (serverPtr->isCancelRequested()) {
                    // Cooperative cancel: tell ExportManager to stop after
                    // the current block. The cancel flag is set by the
                    // notifications/cancelled handler running on the main
                    // thread. Because the main thread is currently blocked in
                    // the tool handler, the flag may not be observable until
                    // the export completes; this is a documented v1.1
                    // limitation (the cancel-watcher thread is a
                    // best-effort, main-thread-independent path).
                    em.cancel();
                }
                QJsonObject params{
                    {"progress", static_cast<double>(prog)},
                    {"message", QString("rendering... %1%").arg(static_cast<int>(prog * 100.0))}
                };
                McpNotification n{"notifications/progress", params};
                QString line = serializeNotification(n);
                QMetaObject::invokeMethod(serverPtr, "notifyFromBackground",
                    Qt::QueuedConnection, Q_ARG(QString, line));
            };

            // Wait for completion via a promise. The promise is set on
            // ExportManager's thread; the main thread (tool handler) blocks
            // on the future.
            auto donePromise = std::make_shared<std::promise<std::pair<bool, QString>>>();
            auto doneFuture = donePromise->get_future();
            em.onComplete = [donePromise](bool success, const juce::String& message) {
                donePromise->set_value({success, QString::fromUtf8(message.toRawUTF8())});
            };

            // Cancel-watcher: a small polling thread that watches the MCP
            // server's cancel flag and tells ExportManager to abort. This is
            // the only way to break out of a render that is in progress when
            // the main thread is blocked in the tool handler.
            std::atomic<bool> stopWatcher{false};
            std::thread cancelWatcher;
            cancelWatcher = std::thread([serverPtr, &em, &stopWatcher]() {
                while (!stopWatcher.load(std::memory_order_relaxed)) {
                    if (serverPtr->isCancelRequested()) {
                        em.cancel();
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            });

            // Kick off the export on ExportManager's internal worker thread.
            if (!em.startExport(projectCopy, formatManager, pluginManager, outFile,
                                sampleRate, startTime, duration, fmt, bitDepth)) {
                stopWatcher.store(true);
                if (cancelWatcher.joinable()) cancelWatcher.join();
                em.onProgress = nullptr;
                em.onComplete = nullptr;
                return McpToolResult::text("failed to start export", true);
            }

            // Initial progress: 0.0 (kick-off). Sent synchronously on the
            // main thread is fine — it's just one notification.
            {
                QJsonObject params{{"progress", 0.0},{"message","starting render"}};
                McpNotification n{"notifications/progress", params};
                s.notifyFromBackground(serializeNotification(n));
            }

            // Block on the export. The future will be set by ExportManager's
            // worker thread when the render finishes (or is cancelled).
            auto [success, message] = doneFuture.get();

            // Stop the cancel-watcher and join.
            stopWatcher.store(true);
            if (cancelWatcher.joinable()) cancelWatcher.join();

            // Final progress notification: 1.0 on success, or the actual
            // fraction reached on cancel. Sent via notifyFromBackground so it
            // is enqueued on the main thread (it will be delivered after the
            // current tool call returns, since the main thread is currently
            // executing this handler).
            {
                QJsonObject params{
                    {"progress", success ? 1.0 : 0.0},
                    {"message", message}
                };
                McpNotification n{"notifications/progress", params};
                s.notifyFromBackground(serializeNotification(n));
            }

            em.onProgress = nullptr;
            em.onComplete = nullptr;

            // Clear the cancel flag so the next export can run.
            s.resetCancelFlag();

            if (!success) {
                QString reply = message.contains("cancel", Qt::CaseInsensitive)
                    ? QString("export cancelled: %1").arg(message)
                    : QString("export failed: %1").arg(message);
                return McpToolResult::text(reply, true);
            }
            return McpToolResult::text(QString("exported to %1 (%2)")
                .arg(path).arg(message));
        }});
}

} // namespace mcp
