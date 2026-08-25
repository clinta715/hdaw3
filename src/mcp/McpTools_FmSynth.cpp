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

void registerFmSynthTools(McpServer& s, AudioEngine* e)
{

s.registerTool({"fm_synth_load_preset",
        "Load a raw DX7 patch (156 bytes hex string) into an FM synth FX slot.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"patchData", QJsonObject{{"type","string"}}}}, {"trackId","slotIndex","patchData"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "fm_synth")
                return McpToolResult::text("slot is not an FM synth", true);

            QString hex = a.value("patchData").toString();
            if (hex.isEmpty() || hex.size() != 312)
                return McpToolResult::text("patchData must be 312 hex characters (156 bytes)", true);

            uint8_t patch[156];
            for (int i = 0; i < 156; i++)
            {
                bool ok;
                patch[i] = static_cast<uint8_t>(hex.mid(i * 2, 2).toInt(&ok, 16));
                if (!ok) return McpToolResult::text("invalid hex at offset " + QString::number(i), true);
            }

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si >= (int)chain.size() || !chain[si])
                return McpToolResult::text("FX slot not found in chain", true);
            auto* slot = chain[si].get();
            if (!slot->fmSynthEngine())
                return McpToolResult::text("FM synth engine not initialized", true);

            slot->fmSynthEngine()->loadPatch(patch);
            return McpToolResult::text("ok");
        }});

s.registerTool({"fm_synth_import_sysex",
        "Import a DX7 .syx file into an FM synth FX slot. Supports single voice dumps "
        "(163 bytes) and 32-voice cartridge dumps (4104 bytes). For cartridges, loads "
        "voice index 0 (first voice). Returns the voice name if available.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}},
                   {"voiceIndex",QJsonObject{{"type","integer"}}}},
                   {"trackId","slotIndex","filePath"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "fm_synth")
                return McpToolResult::text("slot is not an FM synth", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);

            juce::File syxFile(filePath.toStdString());
            if (!syxFile.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            juce::MemoryBlock raw;
            if (!syxFile.loadFileAsData(raw))
                return McpToolResult::text("failed to read file", true);

            auto* bytes = static_cast<const uint8_t*>(raw.getData());
            size_t fileSize = raw.getSize();

            std::optional<HDAW::Dx7Voice> voice;
            std::vector<HDAW::Dx7Voice> voices;
            int resolvedVoiceIndex = 0;

            if (fileSize >= 163 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x00) {
                voice = HDAW::parseSingleVoiceSysex(bytes, fileSize);
            } else if (fileSize >= 4104 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x09) {
                voices = HDAW::parseCartridgeSysex(bytes, fileSize);
                int vi = a.value("voiceIndex").toInt(0);
                if (vi >= 0 && vi < (int)voices.size()) {
                    voice = voices[vi];
                    resolvedVoiceIndex = vi;
                }
            } else {
                return McpToolResult::text(
                    "not a recognized DX7 SysEx file (expected F0 43 00 00 or F0 43 00 09 header)", true);
            }

            if (!voice.has_value())
                return McpToolResult::text("failed to parse SysEx data (bad checksum or size)", true);

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si >= (int)chain.size() || !chain[si])
                return McpToolResult::text("FX slot not found in chain", true);
            auto* slot = chain[si].get();
            if (!slot->fmSynthEngine())
                return McpToolResult::text("FM synth engine not initialized", true);

            slot->fmSynthEngine()->loadPatch(voice->patchData.data());

            QJsonObject result;
            result["ok"] = true;
            result["voiceName"] = QString::fromStdString(voice->voiceName);
            result["algorithm"] = voice->algorithm;
            result["feedback"] = voice->feedback;
            if (!voices.empty()) {
                result["totalVoices"] = static_cast<int>(voices.size());
                QJsonArray voicesArr;
                for (int i = 0; i < (int)voices.size(); ++i) {
                    QJsonObject v;
                    v["index"] = i;
                    v["name"] = QString::fromStdString(voices[i].voiceName);
                    v["algorithm"] = voices[i].algorithm;
                    voicesArr.append(v);
                }
                result["voices"] = voicesArr;
                result["voiceIndex"] = resolvedVoiceIndex;
            }

            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

s.registerTool({"fm_synth_get_state",
        "Get the current state of an FM synth FX slot (active voices, algorithm).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "fm_synth")
                return McpToolResult::text("slot is not an FM synth", true);

            QJsonObject state;
            auto* proc = e->getMainProcessor();
            if (proc)
            {
                auto* track = proc->getTrack(ti);
                if (track)
                {
                    auto& chain = track->getFXChain();
                    if (si < (int)chain.size() && chain[si])
                    {
                        auto* engine = chain[si]->fmSynthEngine();
                        if (engine)
                            state["activeVoices"] = engine->activeVoiceCount();
                    }
                }
            }
            auto& model = e->getProjectModel();
            auto slotTree = model.getTrackListTree().getChild(ti)
                .getChildWithName(IDs::FX_CHAIN).getChild(si);
            state["algorithm"] = slotTree.isValid()
                ? static_cast<int>(slotTree.getProperty("param_0", 0)) : 0;
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(state).toJson(QJsonDocument::Compact)));
        }});

}

} // namespace mcp
