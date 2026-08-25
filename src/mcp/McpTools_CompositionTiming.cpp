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

void registerTimingTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"set_scale", "Set the project scale (root 0..11, mode 0..20).",
        objSchema({{"root", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
                  {"mode", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}}}, {"root","mode"}),
        "composition",
        [e](const QJsonObject& a) {
            e->getProjectModel().setScaleRoot(a.value("root").toInt());
            e->getProjectModel().setScaleMode(a.value("mode").toInt());
            return McpToolResult::text("ok");
        }});

s.registerTool({"set_tempo", "Set the project tempo (BPM).",
        objSchema({{"bpm", QJsonObject{{"type","number"},{"minimum",1.0},{"maximum",999.0}}}}, {"bpm"}),
        "composition",
        [e](const QJsonObject& a) {
            e->getProjectCommands().setTempo(a.value("bpm").toDouble());
            return McpToolResult::text("ok");
        }});

s.registerTool({"add_tempo_point", "Add a tempo point at the given time (seconds) with the given BPM. Returns the new point index.",
        objSchema({{"timeSeconds", QJsonObject{{"type","number"},{"description","Time in seconds"}}},
                   {"bpm", QJsonObject{{"type","number"},{"minimum",1.0},{"maximum",999.0}}}},
                  {"timeSeconds","bpm"}),
        "composition",
        [e](const QJsonObject& a) {
            int idx = e->getProjectCommands().addTempoPoint(
                a.value("timeSeconds").toDouble(), a.value("bpm").toDouble());
            return McpToolResult::text(QString("added at index %1").arg(idx));
        }});

s.registerTool({"remove_tempo_point", "Remove a tempo point by index.",
        objSchema({{"index", QJsonObject{{"type","integer"},{"minimum",0}}}}, {"index"}),
        "composition",
        [e](const QJsonObject& a) {
            e->getProjectCommands().removeTempoPoint(a.value("index").toInt());
            return McpToolResult::text("removed");
        }});

s.registerTool({"set_tempo_point_bpm", "Set the BPM of a tempo point.",
        objSchema({{"index", QJsonObject{{"type","integer"},{"minimum",0}}},
                   {"bpm", QJsonObject{{"type","number"},{"minimum",1.0},{"maximum",999.0}}}},
                  {"index","bpm"}),
        "composition",
        [e](const QJsonObject& a) {
            e->getProjectCommands().setTempoPointBpm(
                a.value("index").toInt(), a.value("bpm").toDouble());
            return McpToolResult::text("ok");
        }});

s.registerTool({"set_tempo_point_time", "Set the time (seconds) of a tempo point.",
        objSchema({{"index", QJsonObject{{"type","integer"},{"minimum",0}}},
                   {"timeSeconds", QJsonObject{{"type","number"},{"description","Time in seconds"}}}},
                  {"index","timeSeconds"}),
        "composition",
        [e](const QJsonObject& a) {
            e->getProjectCommands().setTempoPointTime(
                a.value("index").toInt(), a.value("timeSeconds").toDouble());
            return McpToolResult::text("ok");
        }});

s.registerTool({"set_time_signature", "Set the project time signature (numerator/denominator).",
        objSchema({{"numerator", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",32}}},
                  {"denominator", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",32}}}}, {"numerator","denominator"}),
        "composition",
        [e](const QJsonObject& a) {
            e->getProjectCommands().setTimeSignature(a.value("numerator").toInt(), a.value("denominator").toInt());
            return McpToolResult::text("ok");
        }});

s.registerTool({"get_chord_types", "List all available chord types.",
        objSchema({}),
        "composition",
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
        "composition",
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
        "composition",
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

}

} // namespace mcp
