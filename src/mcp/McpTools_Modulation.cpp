#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <set>
#include <string>

namespace mcp {

// Returns the lfoIndex-th MODULATION child of trackId, or an invalid tree when
// the track/lfo indices are out of bounds. The commands themselves no-op
// silently on bad indices (AudioEngineCommands_Modulation.cpp), so the MCP
// layer validates up front and rejects with an error instead (Gate 9).
static juce::ValueTree lfoTree(AudioEngine* e, int trackId, int lfoIndex)
{
    auto trackList = e->getProjectModel().getTrackListTree();
    if (trackId < 0 || trackId >= trackList.getNumChildren()) return {};
    auto modList = trackList.getChild(trackId).getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || lfoIndex < 0 || lfoIndex >= modList.getNumChildren()) return {};
    return modList.getChild(lfoIndex);
}

void registerModulationTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_lfo",
        "Add an LFO to a track's modulation list with defaults: id 'lfo_N', name 'LFO N', waveform 0 (sine), rate 1.0, rateSync true, depth 0.3, bipolar false, phaseOffset 0.0, targetParamID 1 (volume), enabled true. Returns {\"lfoIndex\"} - the 0-based index of the appended LFO (list size BEFORE the append). One call per LFO; later calls with the same trackId append further LFOs.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "modulation",
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            auto trackList = e->getProjectModel().getTrackListTree();
            if (trackId < 0 || trackId >= trackList.getNumChildren())
                return McpToolResult::text("trackId out of range", true);
            auto modList = trackList.getChild(trackId).getChildWithName(IDs::MODULATION_LIST);
            int lfoIndex = modList.isValid() ? modList.getNumChildren() : 0;
            e->getProjectCommands().addLfo(trackId);
            QJsonObject o{{"lfoIndex", lfoIndex}};
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(o).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_lfo_param",
        "Set one LFO parameter (mirrors project.setLfoParam). Param names: waveform (0=sine, 1=triangle, 2=saw), rate (Hz or cycles/beat when rateSync), rateSync (any nonzero -> true), depth, bipolar (any nonzero -> true), phaseOffset (degrees), targetParamID (1=volume, 2=pan, 3=mute, or 100+slotIndex*100+paramIndex for a plugin FX param), enabled (any nonzero -> true). Unknown param, out-of-range trackId or lfoIndex is an error (never a silent no-op).",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"lfoIndex",  QJsonObject{{"type","integer"}}},
                  {"param",     QJsonObject{{"type","string"}}},
                  {"value",     QJsonObject{{"type","number"}}}}, {"trackId","lfoIndex","param","value"}),
        "modulation",
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            int lfoIndex = a.value("lfoIndex").toInt(-1);
            if (!lfoTree(e, trackId, lfoIndex).isValid())
                return McpToolResult::text("trackId or lfoIndex out of range", true);
            static const std::set<std::string> kParams = {
                "waveform", "rate", "rateSync", "depth", "bipolar",
                "phaseOffset", "targetParamID", "enabled" };
            std::string param = a.value("param").toString().toStdString();
            if (kParams.find(param) == kParams.end())
                return McpToolResult::text(
                    QString("unknown param '%1'").arg(a.value("param").toString()), true);
            e->getProjectCommands().setLfoParam(trackId, lfoIndex, param,
                                                a.value("value").toDouble());
            return McpToolResult::text("ok");
        }});

    s.registerTool({"list_lfos",
        "List a track's LFOs as a JSON array (mirrors read.getModulationLfos). Each entry: {index, name, waveform, rate, rateSync, depth, bipolar, phaseOffset, targetParamID, enabled}. Out-of-range trackId is an error.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        "modulation",
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            auto trackList = e->getProjectModel().getTrackListTree();
            if (trackId < 0 || trackId >= trackList.getNumChildren())
                return McpToolResult::text("trackId out of range", true);
            QJsonArray arr;
            for (const auto& l : e->getReadModel().getModulationLfos(trackId))
                arr.append(QJsonObject{
                    {"index",        l.index},
                    {"name",         QString::fromStdString(l.name)},
                    {"waveform",     l.waveform},
                    {"rate",         l.rate},
                    {"rateSync",     l.rateSync},
                    {"depth",        l.depth},
                    {"bipolar",      l.bipolar},
                    {"phaseOffset",  l.phaseOffset},
                    {"targetParamID", l.targetParamID},
                    {"enabled",      l.enabled}});
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"remove_lfo",
        "Remove an LFO from a track's modulation list by its 0-based index (mirrors project.removeLfo; the engine's tree listener rebuilds the track's live modulation sources). Out-of-range trackId or lfoIndex is an error.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                  {"lfoIndex",  QJsonObject{{"type","integer"}}}}, {"trackId","lfoIndex"}),
        "modulation",
        [e](const QJsonObject& a) -> McpToolResult {
            int trackId = a.value("trackId").toInt(-1);
            int lfoIndex = a.value("lfoIndex").toInt(-1);
            if (!lfoTree(e, trackId, lfoIndex).isValid())
                return McpToolResult::text("trackId or lfoIndex out of range", true);
            e->getProjectCommands().removeLfo(trackId, lfoIndex);
            return McpToolResult::text("ok");
        }});
}

} // namespace mcp
