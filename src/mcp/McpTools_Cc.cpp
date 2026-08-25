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

void registerCcTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_cc_point", "Add a MIDI CC point to a clip; returns ccId.",
        objSchema({{"clipId",           QJsonObject{{"type","integer"}}},
                  {"controllerNumber", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"beat",             QJsonObject{{"type","number"}}},
                  {"value",            QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}},
                 {"clipId","controllerNumber","beat","value"}),
        "cc",
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
        "cc",
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
        "cc",
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
        "cc",
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

} // namespace mcp
