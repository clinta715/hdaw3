#include "McpTools.h"
#include "McpTools_Private.h"
// B2: the stable-id argument helpers (`trackId`/`trackID`) — thin readers over
// the ONE shared rule in common/StableRefResolve.h, whose error text is what
// the RPC twin reports for the same request.
#include "McpArgs.h"
#include "PresetRoute.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../common/FmPatchLoad.h"
#include "../common/FmSynthStateJson.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/EnvelopeGenerator.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/ProjectPool.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/FmSynthEngine.h"
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
        "Load a raw DX7 patch (156 bytes hex string) into an FM synth FX slot. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"trackID",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"patchData", QJsonObject{{"type","string"}}}}, {"slotIndex","patchData"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            // Shared loader (src/common/FmPatchLoad.h): the RPC twin
            // audio.fmSynthLoadPreset runs this SAME entry point, so both
            // surfaces answer byte-identical text by construction.
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            bool ok = false;
            const auto text = HDAW::fmLoadPresetToolText(
                *e, ti, a.value("slotIndex").toInt(),
                a.value("patchData").toString(), &ok);
            return McpToolResult::text(text, !ok);
        }});

s.registerTool({"fm_synth_import_sysex",
        "Import a DX7 .syx file into an FM synth FX slot. Supports single voice dumps "
        "(163 bytes) and 32-voice cartridge dumps (4104 bytes). For cartridges, loads "
        "voice index 0 (first voice). Returns the voice name if available. " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"trackID",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}},
                   {"voiceIndex",QJsonObject{{"type","integer"}}}},
                   {"slotIndex","filePath"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            return runFmImportSysex(*e,
                ti, a.value("slotIndex").toInt(),
                a.value("filePath").toString(),
                a.value("voiceIndex").toInt(0));
        }});

s.registerTool({"fm_synth_get_state",
        "Get the current state of an FM synth FX slot (active voices, algorithm). " +
        mcp::stableRefRuleText("trackID", "trackId"),
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"trackID",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}}}, {"slotIndex"}),
        "fx",
        [e](const QJsonObject& a) -> McpToolResult {
            // Shared read (src/common/FmSynthStateJson.h) — the same entry
            // point read.getFmSynthState runs (live activeVoiceCount for the
            // caller-chosen slot + the tree's param_0; NOT read.getFmAnalysis's
            // sources).
            int ti; std::string refErr;
            if (!trackIndexArg(a, e->getProjectModel().getTrackListTree(), ti, refErr))
                return McpToolResult::text(QString::fromStdString(refErr), true);
            bool ok = false;
            const QString text = HDAW::fmSynthStateToolText(*e,
                ti, a.value("slotIndex").toInt(), &ok);
            return McpToolResult::text(text, !ok);
        }});

}

} // namespace mcp
