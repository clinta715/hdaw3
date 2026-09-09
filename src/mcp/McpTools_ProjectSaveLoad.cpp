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
#include "../common/ReadModel.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace mcp {

void registerProjectSaveLoadTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"save_project", "Save the project to a file.",
        objSchema({{"filePath", QJsonObject{{"type","string"}}}}, {"filePath"}),
        "project",
        [e](const QJsonObject& a) {
            auto path = a.value("filePath").toString();
            // Render-domain exclusion (handoff B1/F1): a save reads the live
            // processors; cancel + join any orphaned offline render first so it
            // cannot race the save (ProjectSerializer reads getTrack()).
            if (auto* proc = e->getMainProcessor())
                if (proc->getExportManager().isExporting())
                    proc->getExportManager().cancelAndJoin();
            juce::File f(juce::String(path.toUtf8().constData()));
            bool ok = HDAW::ProjectSerializer::save(e->getProjectModel(), f, e->getMainProcessor());
            if (ok)
                HDAW::backupProject(f);
            return McpToolResult::text(ok ? "saved" : "save failed", !ok);
        }});

    s.registerTool({"load_project", "Load a project from a file (replaces current project).",
        objSchema({{"filePath", QJsonObject{{"type","string"}}}}, {"filePath"}),
        "project",
        [e](const QJsonObject& a) {
            auto path = a.value("filePath").toString();
            // Route through the command layer (same as the RPC loadProject):
            // direct ProjectSerializer::load skipped migrations (trackType,
            // MASTER_FX) and load-progress broadcast — the MCP tool silently
            // diverged from the RPC path (2026-09-08 master FX session).
            const bool ok = e->getProjectCommands().loadProject(
                juce::String(path.toUtf8().constData()).toStdString());
            return McpToolResult::text(ok ? "loaded" : "load failed", !ok);
        }});

    s.registerTool({"new_project", "Create a new empty project.",
        objSchema({}),
        "project",
        [e](const QJsonObject&) {
            HDAW::ProjectSerializer::createNew(e->getProjectModel());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"project_info", "Return project file metadata (provenance, format version, timestamps).",
        objSchema({}),
        "project",
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

    s.registerTool({"snapshot_project", "Return a read-only JSON snapshot of the current project state.",
        objSchema({}),
        "project",
        [e](const QJsonObject&) -> McpToolResult {
            const auto snap = e->getReadModel().snapshot();
            auto transportToJson = [](const TransportSnapshot& t) {
                return QJsonObject{
                    {"bpm", t.bpm},
                    {"isPlaying", t.isPlaying},
                    {"isLooping", t.isLooping},
                    {"isRecording", t.isRecording},
                    {"punchEnabled", t.punchEnabled},
                    {"loopStart", t.loopStart},
                    {"loopEnd", t.loopEnd},
                    {"currentTimeSeconds", t.currentTimeSeconds},
                    {"sampleRate", t.sampleRate},
                    {"timeSigNumerator", t.timeSigNumerator},
                    {"timeSigDenominator", t.timeSigDenominator}
                };
            };
            QJsonObject root{
                {"name", QString::fromStdString(snap.name)},
                {"transport", transportToJson(snap.transport)},
                {"scaleRoot", snap.scaleRoot},
                {"scaleMode", snap.scaleMode},
                {"masterGain", snap.masterGain},
                {"launchedScene", snap.launchedScene},
                {"sceneCount", snap.sceneCount},
                {"createdWithApp", QString::fromStdString(snap.createdWithApp)},
                {"savedWithApp", QString::fromStdString(snap.savedWithApp)},
                {"formatVersion", snap.formatVersion},
                {"trackCount", static_cast<int>(snap.tracks.size())},
                {"clipCount", static_cast<int>(snap.clips.size())}
            };

            QJsonArray tracks;
            for (const auto& t : snap.tracks) {
                QJsonArray fxSlots;
                for (const auto& fx : e->getReadModel().getFxSlots(t.index)) {
                    fxSlots.append(QJsonObject{
                        {"slotIndex", fx.slotIndex},
                        {"fxType", QString::fromStdString(fx.fxType)},
                        {"pluginId", QString::fromStdString(fx.pluginId)},
                        {"pluginName", QString::fromStdString(fx.pluginName)},
                        {"pluginFormat", QString::fromStdString(fx.pluginFormat)},
                        {"bypassed", fx.bypassed},
                        {"paramCount", fx.paramCount}
                    });
                }
                tracks.append(QJsonObject{
                    {"index", t.index},
                    {"name", QString::fromStdString(t.name)},
                    {"color", t.color},
                    {"volume", t.volume},
                    {"pan", t.pan},
                    {"muted", t.muted},
                    {"soloed", t.soloed},
                    {"armed", t.armed},
                    {"inputMonitor", t.inputMonitor},
                    {"height", t.height},
                    {"midiChannel", t.midiChannel},
                    {"trackType", t.trackType},
                    {"isCollapsed", t.isCollapsed},
                    {"isHidden", t.isHidden},
                    {"effectiveMuted", t.effectiveMuted},
                    {"effectiveSoloed", t.effectiveSoloed},
                    {"parentId", t.parentId},
                    {"clipCount", t.clipCount},
                    {"fxSlots", fxSlots},
                    {"meter", QJsonObject{
                        {"left", e->getReadModel().getTrackMeter(t.index).leftLevel},
                        {"right", e->getReadModel().getTrackMeter(t.index).rightLevel},
                        {"rmsLeft", e->getReadModel().getTrackMeter(t.index).rmsLeftLevel},
                        {"rmsRight", e->getReadModel().getTrackMeter(t.index).rmsRightLevel},
                        {"lufsMomentary", e->getReadModel().getTrackMeter(t.index).lufsMomentary}
                    }}
                });
            }
            root["tracks"] = tracks;

            QJsonArray clips;
            for (const auto& c : snap.clips) {
                QJsonArray takeArr;
                for (const auto& take : c.takes) {
                    takeArr.append(QJsonObject{{"name", QString::fromStdString(take.name)},
                                               {"sourceFile", QString::fromStdString(take.sourceFile)}});
                }
                QJsonArray envArr;
                for (const auto& pt : c.gainEnvelope) {
                    envArr.append(QJsonObject{{"time", pt.time}, {"gain", pt.gain}});
                }
                clips.append(QJsonObject{
                    {"clipId", c.clipId},
                    {"trackIndex", c.trackIndex},
                    {"name", QString::fromStdString(c.name)},
                    {"sourceFile", QString::fromStdString(c.sourceFile)},
                    {"startBeat", c.startBeat},
                    {"durationBeats", c.durationBeats},
                    {"offset", c.offset},
                    {"gain", c.gain},
                    {"fadeIn", c.fadeIn},
                    {"fadeOut", c.fadeOut},
                    {"looping", c.looping},
                    {"muted", c.muted},
                    {"isMidi", c.isMidi},
                    {"sourceBpm", c.sourceBpm},
                    {"stretchMode", c.stretchMode},
                    {"stretchRatio", c.stretchRatio},
                    {"sourceDuration", c.sourceDuration},
                    {"isGhost", c.isGhost},
                    {"ghostSourceId", c.ghostSourceId},
                    {"sceneIndex", c.sceneIndex},
                    {"activeTake", c.activeTake},
                    {"takeCount", c.takeCount},
                    {"takes", takeArr},
                    {"gainEnvelope", envArr}
                });
            }
            root["clips"] = clips;

            QJsonArray arrangerRegions;
            for (const auto& region : e->getReadModel().getArrangerRegions()) {
                arrangerRegions.append(QJsonObject{
                    {"regionID", QString::fromStdString(region.regionID)},
                    {"name", QString::fromStdString(region.name)},
                    {"startTime", region.startTime},
                    {"duration", region.duration},
                    {"color", region.color}
                });
            }
            root["arrangerRegions"] = arrangerRegions;

            QJsonArray tempoPoints;
            for (const auto& tp : e->getReadModel().getTempoPoints()) {
                tempoPoints.append(QJsonObject{{"timeSeconds", tp.timeSeconds}, {"bpm", tp.bpm}});
            }
            root["tempoPoints"] = tempoPoints;

            return McpToolResult::text(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"scan_plugins", "Scan for VST3/CLAP plugins (may take a minute).",
        objSchema({}),
        "project",
        [e](const QJsonObject&) {
            e->getPluginManager().scanAll();
            int count = static_cast<int>(e->getPluginManager().getPlugins().size());
            return McpToolResult::text(QString("scanned %1 plugins").arg(count));
        }});

    s.registerTool({"list_plugins", "List all scanned plugins.",
        objSchema({}),
        "project",
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

} // namespace mcp
