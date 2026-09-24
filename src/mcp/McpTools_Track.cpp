#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../common/FxPluginIdCheck.h"
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

void registerTrackTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_track",
        "Add a track; returns compact JSON {\"trackId\":N,\"routed\":1} (routed=1 when the track is registered for routing). Color defaults to the next palette color if omitted.",
        objSchema({{"name", QJsonObject{{"type","string"}}},
                  {"color", QJsonObject{{"type","integer"}}},
                  {"parentBus", QJsonObject{{"type","integer"}}}}, {"name"}),
        "track",
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
            // P3-2: JSON response (was plain text "trackId=N routed=1"). The
            // trackId/routed keys are semantically unchanged, so parsers that
            // look up the key by name keep working.
            QJsonObject result{{"trackId", idx}, {"routed", routingOk ? 1 : 0}};
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"remove_track",
        "Remove a track (destructive). Returns compact JSON "
        "{\"ok\":true,\"removed\":<oldIndex>,\"shifted\":[{\"from\":N,\"to\":N-1},...]}: "
        "`removed` is the deleted track's old index and `shifted` lists every track "
        "index above it moved down by one ([] when the last track was removed) — the "
        "identical payload RPC project.removeTrack returns. Folder parent/child links "
        "and song-plan cell track refs are remapped by the shared command path.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"dryRun",  QJsonObject{{"type","boolean"}}},
                  {"force",   QJsonObject{{"type","boolean"}}}}, {"trackId"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto tl = m.getTrackListTree();
            int id = a.value("trackId").toInt();
            if (id < 0 || id >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            auto track = tl.getChild(id);
            QString name = jstr(track.getProperty(IDs::name).toString());
            auto clipList = track.getChildWithName(IDs::CLIP_LIST);
            int clipCount = clipList.isValid() ? clipList.getNumChildren() : 0;
            if (a.value("dryRun").toBool(false))
            {
                QString info = QString("would remove track %1 (%2), %3 clips").arg(id).arg(name).arg(clipCount);
                if (clipCount > 0)
                    info += ". Pass force:true to confirm deletion of clips.";
                return McpToolResult::text(info);
            }
            if (clipCount > 0 && !a.value("force").toBool(false))
                return McpToolResult::text(QString("track %1 (%2) has %3 clips. Pass force:true to confirm deletion.").arg(id).arg(name).arg(clipCount), true);
            // ONE removal path (handoff 7): the shared command splices AND
            // remaps the durable positional refs, and returns the shift report.
            // Undo semantics are unchanged — the command's removeChild(&um) is
            // exactly what this inline path did before.
            const auto res = e->getProjectCommands().removeTrack(id);
            QJsonArray shifted;
            for (const auto& p : res.shifted)
                shifted.append(QJsonObject{{"from", p.first}, {"to", p.second}});
            const QJsonObject result{{"ok", res.ok}, {"removed", res.removed},
                                     {"shifted", shifted}};
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact)));
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
        "track",
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
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getProjectCommands().setMasterGain(static_cast<float>(a.value("gain").toDouble(1.0)));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"move_track", "Move a track to a new index.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"newIndex", QJsonObject{{"type","integer"}}}}, {"trackId","newIndex"}),
        "track",
        [e](const QJsonObject& a) {
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            auto tl = m.getTrackListTree();
            int id = a.value("trackId").toInt();
            int ni = a.value("newIndex").toInt();
            if (id < 0 || id >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            ni = std::clamp(ni, 0, tl.getNumChildren() - 1);
            auto t = tl.getChild(id);
            const int count = tl.getNumChildren();
            tl.removeChild(id, nullptr);
            tl.addChild(t, ni, &um);
            // This splice stays inline (its undo behavior is untouched), but it
            // runs the SAME durable-ref fixup the command path runs — a reorder
            // shifts every index between the two positions (handoff 7).
            HDAW::remapTrackPositionalRefs(tl, m.getTree(),
                                            HDAW::trackMoveIndexMap(count, id, ni), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"duplicate_track",
        "Duplicate a track (deep copy with new clip/note IDs). Returns the new track index.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "track",
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
        "Add a track with an FX slot. fxType in {eq,compressor,reverb,delay,chorus,flanger,phaser,filter,saturator,sampler,fm_synth,growl_bass,psyarp,psy_fm,sub_synth}, or provide pluginId for a VST3/CLAP plugin. Returns compact JSON {trackId, routed, fxType} (same shape as add_track).",
        objSchema({{"name",     QJsonObject{{"type","string"}}},
                   {"fxType",   QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger","phaser","filter","saturator","sampler","fm_synth","growl_bass","psyarp","psy_fm","sub_synth"}}}},
                   {"pluginId", QJsonObject{{"type","string"}}},
                   {"color",    QJsonObject{{"type","integer"}}},
                   {"parentBus",QJsonObject{{"type","integer"}}}}, {"name"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto& um = m.getUndoManager();
            // Gate pluginId BEFORE the track exists: a shadow-edition or
            // unresolvable id must not silently slot a 'none' placeholder
            // through this composite tool (item-1 hole). The SAME shared
            // validator as add_fx / the project.addFxSlot intercept formats
            // the text, so both surfaces fail byte-identically
            // (src/common/FxPluginIdCheck.h). Empty pluginId (track without
            // a plugin) passes untouched — the add_fx accept rules.
            const std::string pluginId = a.value("pluginId").toString().toStdString();
            if (const auto err = HDAW::fxPluginIdError(pluginId, m); !err.empty())
                return McpToolResult::text(QString::fromStdString(err), true);
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
            if (fxType.empty() && !pluginId.empty()) fxType = "plugin";
            if (!fxType.empty())
                m.addFxSlot(idx, fxType, -1, pluginId);

            bool routingOk = idx >= 0 && idx < e->getProjectModel().getTrackListTree().getNumChildren();
            // Compact JSON like add_track (the P3-2 convention — a text return forces every
            // caller to parse a string). fxType is echoed because it is inferred from
            // pluginId when only that was given.
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                { "trackId", idx }, { "routed", routingOk ? 1 : 0 },
                { "fxType", QString::fromStdString(fxType) } }).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
