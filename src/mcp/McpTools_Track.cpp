#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
// The shared bodies behind the track tools: add_track_with_fx's composite
// (pluginId gate + created-track shape + payload), the remove-track dryRun/force
// guard, and the creation payload shaper — all three are also what the RPC
// routes use, so the surfaces agree by construction. AddTrackWithFx.h pulls in
// FxPluginIdCheck.h (the gate it runs) — this TU has no direct use for it.
#include "../common/AddTrackWithFx.h"
#include "../common/TrackJson.h"
#include "../common/TrackRemoveGuard.h"
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
        "Add a track; returns compact JSON {\"trackId\":<index>,\"routed\":0|1,\"trackID\":<id>} "
        "(routed=1 when the track is registered for routing). `trackId` is the new track's "
        "TRACK_LIST INDEX — a position, so it shifts when a track above is removed or moved — "
        "while `trackID` is its STABLE identity (design B1) and survives those splices: hold the "
        "id, not the index, if you mean to refer to this track later. Color defaults to the next "
        "palette color if omitted.",
        objSchema({{"name", QJsonObject{{"type","string"}}},
                  {"color", QJsonObject{{"type","integer"}}},
                  {"parentBus", QJsonObject{{"type","integer"}}}}, {"name"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto& um = m.getUndoManager();
            int idx = m.getTrackListTree().getNumChildren();
            juce::ValueTree t(IDs::TRACK);
            // Stable identity (design B1): the same tree-derived allocator
            // createTrackValueTree uses, minted before the node is appended. Not
            // the index (`idx` shifts when a track above is removed); this does not.
            t.setProperty(IDs::trackID, m.allocateTrackID(), nullptr);
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
            // P3-2: JSON response (was plain text "trackId=N routed=1"). The
            // trackId/routed keys are semantically unchanged, so parsers that
            // look up the key by name keep working. The shape lives in
            // common/TrackJson.h — duplicate_track and the RPC route that now
            // answers duplicateTrack emit THE SAME object. trackID (B1) is the
            // created track's stable id, read off the node just inserted.
            return McpToolResult::text(QString::fromStdString(
                HDAW::shapeTrackCreatedJson(idx, m.getTrackListTree().getNumChildren(),
                                            static_cast<int>(t.getProperty(IDs::trackID, 0)))));
        }});

    s.registerTool({"remove_track",
        "Remove a track (destructive). Returns compact JSON "
        "{\"ok\":true,\"removed\":<oldIndex>,\"shifted\":[{\"from\":N,\"to\":N-1},...]}: "
        "`removed` is the deleted track's old index and `shifted` lists every track "
        "index above it moved down by one ([] when the last track was removed) — the "
        "identical payload RPC project.removeTrack returns. Folder parent/child links "
        "and song-plan cell track refs are remapped by the shared command path. "
        "`dryRun:true` reports what would be removed without mutating; a track that "
        "still carries clips refuses removal unless `force:true` — RPC "
        "project.removeTrack runs the SAME guard (src/common/TrackRemoveGuard.h) and "
        "answers the same text (dryRun preview / refusal), so neither surface can "
        "destroy clips by accident.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"dryRun",  QJsonObject{{"type","boolean"}}},
                  {"force",   QJsonObject{{"type","boolean"}}}}, {"trackId"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            int id = a.value("trackId").toInt();
            // The dryRun preview and the clip refusal come from
            // common/TrackRemoveGuard.h — the SAME guard RPC
            // project.removeTrack now runs, so both surfaces answer the same
            // bytes (and both refuse to destroy clips without force).
            const auto guard = HDAW::inspectTrackForRemoval(m, id);
            if (!guard.found) return McpToolResult::text("track not found", true);
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(
                    QString::fromStdString(HDAW::trackRemovalDryRunText(id, guard)));
            if (guard.clipCount > 0 && !a.value("force").toBool(false))
                return McpToolResult::text(
                    QString::fromStdString(HDAW::trackRemovalRefusalText(id, guard)), true);
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

    s.registerTool({"set_track",
        "Update track properties (partial). `trackId` is the TRACK_LIST index. "
        "Properties: name, volume, pan, mute, solo, color, hidden, armed, "
        "inputMonitor, height, midiChannel, trackType, collapsed — each one is "
        "written through the SAME ProjectCommands setter the matching RPC route "
        "(project.setTrackName / setTrackVolume / setTrackPan / setTrackMuted / "
        "setTrackSoloed / setTrackColor / setTrackHidden / setTrackArmed / "
        "setTrackInputMonitor / setTrackHeight / setTrackMidiChannel / "
        "setTrackType / setTrackCollapsed) calls, so an MCP write and a route write "
        "land the identical tree property. The property names are the route "
        "arguments' (only `mute`/`solo` keep this tool's historical spelling; the "
        "routes spell them `muted`/`soloed`). Returns text \"ok\" (RPC answers Null "
        "— a text tool cannot return JSON null).",
        objSchema({{"trackId",      QJsonObject{{"type","integer"}}},
                  {"name",         QJsonObject{{"type","string"}}},
                  {"volume",       QJsonObject{{"type","number"}}},
                  {"pan",          QJsonObject{{"type","number"}}},
                  {"mute",         QJsonObject{{"type","boolean"}}},
                  {"solo",         QJsonObject{{"type","boolean"}}},
                  {"color",        QJsonObject{{"type","integer"}}},
                  {"hidden",       QJsonObject{{"type","boolean"}}},
                  {"armed",        QJsonObject{{"type","boolean"}}},
                  {"inputMonitor", QJsonObject{{"type","boolean"}}},
                  {"height",       QJsonObject{{"type","integer"}}},
                  {"midiChannel",  QJsonObject{{"type","integer"}}},
                  {"trackType",    QJsonObject{{"type","integer"}}},
                  {"collapsed",    QJsonObject{{"type","boolean"}}}}, {"trackId"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto& cmds = e->getProjectCommands();
            int id = a.value("trackId").toInt();
            if (id < 0 || id >= m.getTrackListTree().getNumChildren())
                return McpToolResult::text("track not found", true);
            // ONE write path per property: the commands the RPC routes call.
            // Writing the ValueTree here instead (as this tool used to) is a fork
            // of the route's logic — the two surfaces could then disagree about
            // which property a key lands on and under which undo unit.
            if (a.contains("name"))         cmds.setTrackName(id, a.value("name").toString().toStdString());
            if (a.contains("volume"))       cmds.setTrackVolume(id, static_cast<float>(a.value("volume").toDouble()));
            if (a.contains("pan"))          cmds.setTrackPan(id, static_cast<float>(a.value("pan").toDouble()));
            if (a.contains("mute"))         cmds.setTrackMuted(id, a.value("mute").toBool());
            if (a.contains("solo"))         cmds.setTrackSoloed(id, a.value("solo").toBool());
            if (a.contains("color"))        cmds.setTrackColor(id, a.value("color").toInt());
            if (a.contains("hidden"))       cmds.setTrackHidden(id, a.value("hidden").toBool());
            if (a.contains("armed"))        cmds.setTrackArmed(id, a.value("armed").toBool());
            if (a.contains("inputMonitor")) cmds.setTrackInputMonitor(id, a.value("inputMonitor").toBool());
            if (a.contains("height"))       cmds.setTrackHeight(id, a.value("height").toInt());
            if (a.contains("midiChannel"))  cmds.setTrackMidiChannel(id, a.value("midiChannel").toInt());
            if (a.contains("trackType"))    cmds.setTrackType(id, a.value("trackType").toInt());
            if (a.contains("collapsed"))    cmds.setTrackCollapsed(id, a.value("collapsed").toBool());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_master_gain", "Set the master bus gain (linear, >= 0).",
        objSchema({{"gain", QJsonObject{{"type","number"},{"minimum",0}}}}, {"gain"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getProjectCommands().setMasterGain(static_cast<float>(a.value("gain").toDouble(1.0)));
            return McpToolResult::text("ok");
        }});

    s.registerTool({"move_track",
        "Move a track to a new index (reorder). `newIndex` is an index into the "
        "CURRENT track order: a forward move places the track immediately before "
        "whatever sits at `newIndex` today, so it can never reach the last slot "
        "that way. Range contract is the shared command's — the SAME call RPC "
        "project.moveTrack makes: an out-of-range `newIndex` (< 0 or >= trackCount) or "
        "newIndex == trackId is a NO-OP (no clamp, no reorder, still \"ok\"); an "
        "out-of-range `trackId` reports \"track not found\". Returns text \"ok\" "
        "either way. Folder parent/child links and song-plan cell track refs are "
        "remapped by the shared command path.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"newIndex", QJsonObject{{"type","integer"}}}}, {"trackId","newIndex"}),
        "track",
        [e](const QJsonObject& a) {
            auto& m = e->getProjectModel();
            const int id = a.value("trackId").toInt();
            if (id < 0 || id >= m.getTrackListTree().getNumChildren())
                return McpToolResult::text("track not found", true);
            // ONE move path (handoff 7): the shared command owns the splice, the
            // range contract and the durable-ref remap, so MCP and the RPC route
            // produce the same order by construction. The inline splice this
            // replaces re-inserted at the un-decremented index on FORWARD moves —
            // a different order than the command's ([B,C,A] vs [B,A,C] for
            // [A,B,C] + move 0 -> 2) — and then ran the command's permutation on
            // a tree that did not match it, so a folder childIds entry / SONG_PLAN
            // cellTrack could survive pointing at the wrong track (silent
            // wrong-track mute/solo/hide/cell-fill). Undo also improves: the
            // removal was `removeChild(id, nullptr)`, un-undoable; it now joins
            // the command's single undo unit.
            e->getProjectCommands().moveTrack(id, a.value("newIndex").toInt());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"duplicate_track",
        "Duplicate a track (deep copy with new clip/note IDs). Returns the SAME "
        "compact JSON as add_track — {\"trackId\":N,\"routed\":0|1,\"trackID\":<id>}: `trackId` is "
        "the new track's INDEX (the copy is appended last), `trackID` its STABLE identity (design "
        "B1) — a copy is a NEW entity, so its id differs from its source's. RPC "
        "project.duplicateTrack returns that same object (it used to answer a bare int).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel();
            int id = a.value("trackId").toInt();
            if (id < 0 || id >= m.getTrackListTree().getNumChildren())
                return McpToolResult::text("track not found", true);
            int newIdx = e->getProjectCommands().duplicateTrack(id);
            if (newIdx < 0)
                return McpToolResult::text("duplicate failed", true);
            // trackID (design B1): read the COPY's own stable id off the tree —
            // duplicateTrack re-stamps it, so this is the new entity's identity,
            // not the source's.
            const int newTrackID = static_cast<int>(
                m.getTrackListTree().getChild(newIdx).getProperty(IDs::trackID, 0));
            return McpToolResult::text(QString::fromStdString(
                HDAW::shapeTrackCreatedJson(newIdx, m.getTrackListTree().getNumChildren(),
                                            newTrackID)));
        }});

    // The two folder moves: RPC-only until now (project.moveTrackIntoFolder /
    // project.moveTrackOutOfFolder), which is why an agent could not group
    // tracks at all. Both call the SAME ProjectCommands entry points the routes
    // call and take the routes' argument names — `trackId` / `folderId` are
    // TRACK_LIST indices (folder membership is a positional pair: the folder's
    // childIds CSV and the child's parentId).
    s.registerTool({"move_track_into_folder",
        "Move a track into a folder track: the folder's childIds gains the track and "
        "the track's parentId is set to it. `trackId` and `folderId` are TRACK_LIST "
        "indices; the SAME ProjectCommands::moveTrackIntoFolder call RPC "
        "project.moveTrackIntoFolder makes, with the same argument names. A "
        "non-folder target, an out-of-range index or trackId == folderId is the "
        "command's NO-OP (still \"ok\"). Returns text \"ok\" (RPC answers Null — a "
        "text tool cannot return JSON null).",
        objSchema({{"trackId",  QJsonObject{{"type","integer"}}},
                  {"folderId", QJsonObject{{"type","integer"}}}}, {"trackId","folderId"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getProjectCommands().moveTrackIntoFolder(a.value("trackId").toInt(),
                                                        a.value("folderId").toInt());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"move_track_out_of_folder",
        "Move a track out of its folder: the parent folder's childIds drops the track "
        "and the track's parentId returns to the folder-less sentinel -1. `trackId` "
        "is a TRACK_LIST index; the SAME ProjectCommands::moveTrackOutOfFolder call "
        "RPC project.moveTrackOutOfFolder makes, with the same argument name. A track "
        "that has no parent is the command's NO-OP (still \"ok\"). Returns text "
        "\"ok\" (RPC answers Null — a text tool cannot return JSON null).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            e->getProjectCommands().moveTrackOutOfFolder(a.value("trackId").toInt());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"add_track_with_fx",
        "Add a track with an FX slot. fxType in {eq,compressor,reverb,delay,chorus,flanger,phaser,filter,saturator,sampler,fm_synth,growl_bass,psyarp,psy_fm,sub_synth}, or provide pluginId for a VST3/CLAP plugin (fxType is then inferred as \"plugin\"). Returns compact JSON {trackId, routed, trackID, fxType} — the same creation shape add_track returns, plus the echoed fxType; `trackId` is the new track's INDEX and `trackID` its STABLE identity (design B1, unchanged by a later splice). RPC project.addTrackWithFx takes these SAME argument names, runs the SAME composite (src/common/AddTrackWithFx.h) and returns the identical payload, refusing an ungateable pluginId with the identical text.",
        objSchema({{"name",     QJsonObject{{"type","string"}}},
                   {"fxType",   QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"eq","compressor","reverb","delay","chorus","flanger","phaser","filter","saturator","sampler","fm_synth","growl_bass","psyarp","psy_fm","sub_synth"}}}},
                   {"pluginId", QJsonObject{{"type","string"}}},
                   {"color",    QJsonObject{{"type","integer"}}},
                   {"parentBus",QJsonObject{{"type","integer"}}}}, {"name"}),
        "track",
        [e](const QJsonObject& a) -> McpToolResult {
            // ONE composite body, shared with RPC project.addTrackWithFx
            // (src/common/AddTrackWithFx.h): the pluginId gate runs BEFORE the
            // track exists (a shadow-edition or unresolvable id must not
            // silently slot a 'none' placeholder — item-1 hole), and the
            // created track's shape and the payload are identical by
            // construction instead of by two hand-written copies.
            const auto r = HDAW::addTrackWithFx(
                e->getProjectModel(),
                a.value("name").toString().toStdString(),
                a.value("fxType").toString().toStdString(),
                a.value("pluginId").toString().toStdString(),
                a.contains("color") ? a.value("color").toInt() : -1,
                a.value("parentBus").toInt(0));
            if (!r.ok)
                return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(
                QString::fromStdString(HDAW::shapeAddTrackWithFxJson(r)));
        }});
}

} // namespace mcp
