#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "PresetFileParser.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/EnvelopeGenerator.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/ProjectPool.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/Dx7SysexImport.h"
#include "../engine/MidiFx.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <optional>
#include <limits>

namespace mcp {

void registerFxPresetTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"list_plugin_presets",
        "List all preset/program names of a plugin FX slot. Uses the preset cache when available (populated during plugin scanning); falls back to querying the live plugin instance.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}}},
                 {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);

            // Try cache first (by pluginId)
            auto* presetInfo = e->getPluginManager().getPresetInfo(juce::String(fxSlots[si].pluginId));
            if (presetInfo && presetInfo->numPrograms > 1)
            {
                QJsonArray arr;
                for (int i = 0; i < presetInfo->numPrograms; ++i)
                {
                    juce::String name = i < presetInfo->programNames.size()
                        ? presetInfo->programNames[i]
                        : juce::String("Preset ") + juce::String(i);
                    arr.append(QJsonObject{{"index", i}, {"name", QString::fromStdString(name.toStdString())}});
                }
                return McpToolResult::text(
                    QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
            }

            // Fallback: live query from instantiated plugin
            auto progs = e->getFxProgramList(ti, si);
            QJsonArray arr;
            for (const auto& p : progs)
                arr.append(QJsonObject{{"index", p.index}, {"name", QString::fromStdString(p.name)}});
            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"search_plugin_presets",
        "Search for presets across all scanned plugins by name (case-insensitive substring match).",
        objSchema({{"query", QJsonObject{{"type","string"}}},
                   {"limit", QJsonObject{{"type","integer"}}}}, {"query"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            QString query = a.value("query").toString().toLower();
            if (query.isEmpty()) return McpToolResult::text("query required", true);
            int limit = a.value("limit").toInt(50);
            if (limit < 1) limit = 1;
            if (limit > 200) limit = 200;
            auto& pm = e->getPluginManager();
            QJsonArray matches;
            for (const auto& pd : pm.getPlugins()) {
                if (static_cast<int>(matches.size()) >= limit) break;
                juce::String pluginId = pd.createIdentifierString();
                auto* presetInfo = pm.getPresetInfo(pluginId);
                if (!presetInfo || presetInfo->numPrograms <= 1) continue;
                QString pluginName = QString::fromStdString(pd.name.toStdString());
                for (int i = 0; i < presetInfo->numPrograms; ++i) {
                    if (static_cast<int>(matches.size()) >= limit) break;
                    juce::String name = i < presetInfo->programNames.size()
                        ? presetInfo->programNames[i]
                        : juce::String("Preset ") + juce::String(i);
                    QString presetName = QString::fromStdString(name.toStdString());
                    if (presetName.toLower().contains(query)) {
                        matches.append(QJsonObject{
                            {"pluginId", QString::fromStdString(pluginId.toStdString())},
                            {"pluginName", pluginName},
                            {"presetIndex", i},
                            {"presetName", presetName}
                        });
                    }
                }
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(matches).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"load_plugin_preset",
        "Load a preset/program by index on a plugin FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"slotIndex", QJsonObject{{"type","integer"}}},
                  {"programIndex", QJsonObject{{"type","integer"}}}},
                 {"trackId","slotIndex","programIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);
            int pi = a.value("programIndex").toInt();
            e->getPluginParamService().setCurrentProgram(ti, fxSlots[si].pluginId, pi);
            return McpToolResult::text("ok");
        }});

s.registerTool({"load_plugin_preset_file",
        "Load a preset file into a plugin FX slot via setStateInformation. "
        "Supports .SerumPreset (Serum 2 XferJson), .fxp (standard VST2 FPCh, Serum 2 layout), and .syx (DX7 SysEx) files.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}}},
                  {"trackId","slotIndex","filePath"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= static_cast<int>(fxSlots.size()))
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "plugin")
                return McpToolResult::text("slot is not a plugin", true);

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si < 0 || si >= static_cast<int>(chain.size()) || !chain[si])
                return McpToolResult::text("FX slot not found in chain", true);
            auto* slot = chain[si].get();
            if (!slot->isPlugin() || !slot->getPluginInstance())
                return McpToolResult::text("slot has no plugin instance", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);

            juce::File fxpFile(filePath.toStdString());
            if (!fxpFile.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            juce::MemoryBlock raw;
            if (!fxpFile.loadFileAsData(raw))
                return McpToolResult::text("failed to read file", true);

            auto parsed = parsePresetFile(raw);
            if (!parsed.ok())
                return McpToolResult::text(QString::fromStdString(parsed.error.toStdString()), true);

            if (parsed.size > static_cast<size_t>(std::numeric_limits<int>::max()))
                return McpToolResult::text("preset payload too large", true);

            slot->getPluginInstance()->setStateInformation(parsed.data, static_cast<int>(parsed.size));

            auto& model = e->getProjectModel();
            auto& um = model.getUndoManager();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (slotTree.isValid()) {
                juce::MemoryBlock stateBlock(parsed.data, parsed.size);
                slotTree.setProperty(IDs::pluginState, stateBlock.toBase64Encoding(), &um);
            }

            return McpToolResult::text("ok");
        }});

}

} // namespace mcp
