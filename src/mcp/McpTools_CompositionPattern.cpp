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

void registerPatternTools(McpServer& s, AudioEngine* e)
{

    static HDAW::PatternLibrary patternLib(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("HDAW").getChildFile("patterns"));

s.registerTool({"analyze_midi_file",
        "Analyze a MIDI file: extract musical fingerprint (key, scale, rhythm, velocity), "
        "bar-aligned patterns, sub-bar motifs, and PhraseGenerator-compatible style parameters. "
        "Returns full analysis with generated params ready for regeneration.",
        objSchema({{"path", QJsonObject{{"type","string"}}}},
                   {"path"}),
        "composition",
        [](const QJsonObject& a) -> McpToolResult {
            auto filePath = a.value("path").toString().toStdString();
            juce::File file{ juce::String(filePath) };
            if (!file.existsAsFile())
                return McpToolResult::text("file not found: " + QString::fromStdString(filePath), true);

            auto result = HDAW::MidiAnalyzer::analyze(file);
            if (result.trackCount == 0)
                return McpToolResult::text("no MIDI data in file", true);

            auto& fp = result.fingerprint;
            QJsonObject root;

            // Fingerprint
            QJsonObject fpObj;
            fpObj["avgNoteDensity"] = fp.avgNoteDensity;
            fpObj["rhythmComplexity"] = fp.rhythmComplexity;
            fpObj["syncopationScore"] = fp.syncopationScore;
            fpObj["swingAmount"] = fp.swingAmount;
            fpObj["pitchRange"] = fp.pitchRange;
            fpObj["rootNote"] = fp.rootNote;
            fpObj["scaleType"] = fp.scaleType;
            fpObj["chromaticism"] = fp.chromaticism;
            fpObj["avgVelocity"] = fp.avgVelocity;
            fpObj["velocityRange"] = fp.velocityRange;
            fpObj["velocityDynamicRange"] = fp.velocityDynamicRange;
            fpObj["quantizationStrength"] = fp.quantizationStrength;
            fpObj["avgNoteDuration"] = fp.avgNoteDuration;
            fpObj["barCount"] = fp.barCount;
            fpObj["voiceCount"] = fp.voiceCount;
            fpObj["avgPolyphony"] = fp.avgPolyphony;
            root["fingerprint"] = fpObj;

            // Metadata
            root["fileName"] = QString::fromStdString(result.fileName.toStdString());
            root["sourceBpm"] = result.sourceBpm;
            root["timeSignature"] = QString("%1/%2").arg(result.timeSignatureNum).arg(result.timeSignatureDen);
            root["trackCount"] = result.trackCount;

            // Style classification
            root["guessedStyle"] = PhraseGenerator::styleName(
                static_cast<PhraseGenerator::Style>(result.guessedStyle));
            root["styleConfidence"] = result.styleConfidence;

            // Patterns
            QJsonArray patternsArr;
            for (const auto& p : result.patterns) {
                QJsonArray notesArr;
                for (const auto& n : p.notes) {
                    notesArr.append(QJsonObject{
                        {"startBeat", n.startBeat}, {"pitch", n.pitch},
                        {"velocity", n.velocity}, {"durationBeats", n.durationBeats}
                    });
                }
                patternsArr.append(QJsonObject{
                    {"name", jstr(p.name)}, {"startBar", p.startBar},
                    {"lengthBars", p.lengthBars}, {"trackIndex", p.trackIndex},
                    {"notes", notesArr}, {"frequency", p.frequency}, {"isMotif", p.isMotif}
                });
            }
            root["patterns"] = patternsArr;
            root["patternCount"] = static_cast<int>(result.patterns.size());

            // Ready-to-use params
            root["paramsJson"] = QString::fromStdString(result.paramsJson.toStdString());
            root["styleParamsJson"] = QString::fromStdString(result.styleParamsJson.toStdString());

            return McpToolResult::text(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"list_patterns",
        "Browse the pattern library. Returns saved pattern presets.",
        objSchema({{"category", QJsonObject{{"type","string"}}},
                   {"style",    QJsonObject{{"type","string"}}},
                   {"tag",      QJsonObject{{"type","string"}}}}),
        "composition",
        [](const QJsonObject& a) -> McpToolResult {
            auto entries = patternLib.listPatterns(
                a.value("category").toString().toStdString(),
                a.value("style").toString().toStdString(),
                a.value("tag").toString().toStdString());
            QJsonArray arr;
            for (const auto& e : entries) {
                QJsonArray tags;
                for (const auto& tag : e.tags) tags.append(jstr(tag));
                arr.append(QJsonObject{
                    {"id",       jstr(e.id)},
                    {"name",     jstr(e.name)},
                    {"style",    jstr(e.style)},
                    {"category", jstr(e.category)},
                    {"source",   jstr(e.source)},
                    {"tags",     tags}
                });
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"save_pattern",
        "Save generation parameters as a reusable pattern preset.",
        objSchema({{"name",        QJsonObject{{"type","string"}}},
                   {"style",       QJsonObject{{"type","string"}}},
                   {"params",      QJsonObject{{"type","object"}}},
                   {"styleParams", QJsonObject{{"type","object"}}},
                   {"description", QJsonObject{{"type","string"}}},
                   {"tags",        QJsonObject{{"type","array"},{"items", QJsonObject{{"type","string"}}}}},
                   {"category",    QJsonObject{{"type","string"}}}},
                   {"name","style","params"}),
        "composition",
        [](const QJsonObject& a) -> McpToolResult {
            HDAW::PatternPreset preset;
            preset.name = a.value("name").toString().toStdString();
            preset.style = a.value("style").toString().toStdString();
            preset.paramsJson = QString::fromUtf8(QJsonDocument(a.value("params").toObject()).toJson(QJsonDocument::Compact)).toStdString();
            if (a.contains("styleParams"))
                preset.styleParamsJson = QString::fromUtf8(QJsonDocument(a.value("styleParams").toObject()).toJson(QJsonDocument::Compact)).toStdString();
            if (a.contains("description"))
                preset.description = a.value("description").toString().toStdString();
            if (a.contains("tags")) {
                for (const auto& t : a.value("tags").toArray())
                    preset.tags.add(t.toString().toStdString());
            }
            if (a.contains("category"))
                preset.category = a.value("category").toString().toStdString();
            juce::String err;
            if (!patternLib.savePattern(preset, err))
                return McpToolResult::text(jstr(err), true);
            return McpToolResult::text("pattern saved");
        }});

s.registerTool({"load_pattern",
        "Load a pattern preset's parameters.",
        objSchema({{"id", QJsonObject{{"type","string"}}}},
                   {"id"}),
        "composition",
        [](const QJsonObject& a) -> McpToolResult {
            HDAW::PatternPreset preset;
            juce::String err;
            if (!patternLib.loadPattern(a.value("id").toString().toStdString(), preset, err))
                return McpToolResult::text(jstr(err), true);
            QJsonObject obj{
                {"name",     jstr(preset.name)},
                {"style",    jstr(preset.style)},
                {"category", jstr(preset.category)},
                {"description", jstr(preset.description)},
                {"author",   jstr(preset.author)},
                {"createdAt", jstr(preset.createdAt)}
            };
            if (!preset.paramsJson.isEmpty()) {
                auto doc = QJsonDocument::fromJson(preset.paramsJson.toRawUTF8());
                if (doc.isObject()) obj["params"] = doc.object();
            }
            if (!preset.styleParamsJson.isEmpty()) {
                auto doc = QJsonDocument::fromJson(preset.styleParamsJson.toRawUTF8());
                if (doc.isObject()) obj["styleParams"] = doc.object();
            }
            QJsonArray tags;
            for (const auto& t : preset.tags) tags.append(jstr(t));
            obj["tags"] = tags;
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"delete_pattern",
        "Delete a user-created pattern preset. Factory presets cannot be deleted.",
        objSchema({{"id", QJsonObject{{"type","string"}}}},
                   {"id"}),
        "composition",
        [](const QJsonObject& a) -> McpToolResult {
            juce::String err;
            if (!patternLib.deletePattern(a.value("id").toString().toStdString(), err))
                return McpToolResult::text(jstr(err), true);
            return McpToolResult::text("pattern deleted");
        }});

s.registerTool({"import_pattern",
        "Import a JSON pattern file or string into the pattern library.",
        objSchema({{"json", QJsonObject{{"type","string"}}}},
                   {"json"}),
        "composition",
        [](const QJsonObject& a) -> McpToolResult {
            juce::String outId, err;
            if (!patternLib.importPattern(a.value("json").toString().toStdString(), outId, err))
                return McpToolResult::text(jstr(err), true);
            return McpToolResult::text(QString("imported id=%1").arg(jstr(outId)));
        }});

s.registerTool({"export_pattern",
        "Export a pattern preset as a JSON string.",
        objSchema({{"id", QJsonObject{{"type","string"}}}},
                   {"id"}),
        "composition",
        [](const QJsonObject& a) -> McpToolResult {
            juce::String outJson, err;
            if (!patternLib.exportPattern(a.value("id").toString().toStdString(), outJson, err))
                return McpToolResult::text(jstr(err), true);
            return McpToolResult::text(jstr(outJson));
        }});

}

} // namespace mcp
