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

void registerArrangerTools(McpServer& s, AudioEngine* e)
{
    // --- Read ---
    s.registerTool({"get_arranger_regions",
        "List all arranger regions (regionID, name, startTime, duration, color).",
        objSchema({}),
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
        [e](const QJsonObject& a) -> McpToolResult {
            std::string name = a.value("name").toString().toStdString();
            auto id = e->getProjectCommands().addArrangerChain(name);
            return McpToolResult::text(QString("chainID=%1").arg(QString::fromStdString(id)));
        }});

    s.registerTool({"remove_arranger_chain",
        "Remove an arranger chain by chainID (destructive).",
        objSchema({{"chainID", QJsonObject{{"type","string"}}}},
                  {"chainID"}),
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
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
        "arranger",
        [e](const QJsonObject&) -> McpToolResult {
            e->getProjectCommands().flattenArranger();
            return McpToolResult::text("ok");
        }});
}

} // namespace mcp
