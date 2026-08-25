#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
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

namespace mcp {

void registerFxPresetTools(McpServer& s, AudioEngine* e)
{

            auto readBE32 = [](const uint8_t* p) -> uint32_t {
                return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
            };

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
        "Supports .fxp (standard VST2 FPCh, Serum 2 layout) and .syx (DX7 SysEx) files.",
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

            auto* bytes = static_cast<const uint8_t*>(raw.getData());

            // SysEx file (DX7 format: starts with F0 43)
            if (bytes[0] == 0xF0 && bytes[1] == 0x43) {
                slot->getPluginInstance()->setStateInformation(raw.getData(), static_cast<int>(raw.getSize()));

                auto& model = e->getProjectModel();
                auto& um = model.getUndoManager();
                auto slotTree = model.getTrackListTree().getChild(ti)
                    .getChildWithName(IDs::FX_CHAIN).getChild(si);
                if (slotTree.isValid()) {
                    slotTree.setProperty(IDs::pluginState, raw.toBase64Encoding(), &um);
                }
                return McpToolResult::text("ok");
            }

            // FXP file (starts with CcnK)
            if (raw.getSize() < 60)
                return McpToolResult::text("file too small for FXP header", true);

            uint32_t chunkMagic = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
            uint32_t fxMagic    = (bytes[8] << 24) | (bytes[9] << 16) | (bytes[10] << 8) | bytes[11];

            if (chunkMagic != 0x43636e4b)
                return McpToolResult::text("not a valid FXP or SysEx file", true);
            if (fxMagic != 0x46504368)
                return McpToolResult::text("not a chunk-based FXP (bad FPCh magic)", true);

            const uint8_t* chunkData = nullptr;
            size_t chunkSize = 0;

            auto readBE32 = [](const uint8_t* p) -> uint32_t {
                return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
            };

            if (raw.getSize() >= 60) {
                uint32_t cs2 = readBE32(bytes + 56);
                if (cs2 > 0 && 60 + cs2 == raw.getSize()) {
                    chunkData = bytes + 60;
                    chunkSize = cs2;
                }
            }

            if (!chunkData && raw.getSize() >= 56) {
                uint32_t cs1 = readBE32(bytes + 52);
                if (cs1 > 0 && 56 + cs1 == raw.getSize()) {
                    chunkData = bytes + 56;
                    chunkSize = cs1;
                }
            }

            if (!chunkData || chunkSize == 0)
                return McpToolResult::text("could not locate chunk data in FXP file", true);

            slot->getPluginInstance()->setStateInformation(chunkData, static_cast<int>(chunkSize));

            auto& model = e->getProjectModel();
            auto& um = model.getUndoManager();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            if (slotTree.isValid()) {
                juce::MemoryBlock stateBlock(chunkData, chunkSize);
                slotTree.setProperty(IDs::pluginState, stateBlock.toBase64Encoding(), &um);
            }

            return McpToolResult::text("ok");
        }});

}

} // namespace mcp
