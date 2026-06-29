#include "McpTools.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/Track.h"
#include "../engine/TrackFXSlot.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>

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
        [e](const QJsonObject& a) -> McpToolResult {
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
}

} // namespace mcp
