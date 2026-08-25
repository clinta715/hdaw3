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
