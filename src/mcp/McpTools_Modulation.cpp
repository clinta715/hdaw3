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

    s.registerTool({"audit_modulation_coverage",
        "READ-ONLY coverage audit for the global modulation rule (every sounding track must carry "
        "modulation). For each track with >= 1 clip reports: lfos {total, enabled}, automation "
        "{lanes, enabledLanes, movableLanes (enabled lanes with >= 3 points — real movement, "
        "ignores the 2-point built-in static defaults)}, subSynthInternalLfo "
        "(any sub_synth slot with LFO cutoff/pitch/amp/FM amount nonzero — params 29-32), and "
        "needsAttention (no enabled LFO, no movable automation lane, and no internal LFO) + reasons. "
        "Summary: tracksWithClips, attentionRequiredIds, fullyCovered, faderOverriddenIds. "
        "Volume-lane authority is reported per track (volumeLanes/faderOverridden): an ENABLED "
        "Volume lane makes automation authoritative, so later set_track_volume/fader writes are "
        "overridden — call set_fader_authoritative before gain staging. Never mutates; no render.",
        objSchema({}, {}),
        "modulation",
        [e](const QJsonObject&) -> McpToolResult {
            auto trackList = e->getProjectModel().getTrackListTree();
            QJsonArray tracks;
            QJsonArray attention;
            QJsonArray faderOverriddenIds;
            int withClips = 0, covered = 0;
            for (int t = 0; t < trackList.getNumChildren(); ++t)
            {
                const auto track = trackList.getChild(t);
                const auto clipList = track.getChildWithName(IDs::CLIP_LIST);
                const int clipCount = clipList.isValid() ? clipList.getNumChildren() : 0;
                if (clipCount == 0) continue; // not a sounding track
                ++withClips;

                QJsonArray reasons;
                int lfoTotal = 0, lfoEnabled = 0;
                const auto modList = track.getChildWithName(IDs::MODULATION_LIST);
                if (modList.isValid())
                {
                    lfoTotal = modList.getNumChildren();
                    for (int i = 0; i < modList.getNumChildren(); ++i)
                        if (modList.getChild(i).getProperty(IDs::enabled, false)) ++lfoEnabled;
                }
                if (lfoTotal == 0) reasons.append("no-lfo");
                else if (lfoEnabled == 0) reasons.append("lfos-disabled");

                int lanes = 0, enabledLanes = 0, movableLanes = 0;
                int volumeLanes = 0, enabledVolumeLanes = 0;
                const auto autoList = track.getChildWithName(IDs::AUTOMATION_LIST);
                if (autoList.isValid())
                {
                    lanes = autoList.getNumChildren();
                    for (int i = 0; i < autoList.getNumChildren(); ++i)
                    {
                        const auto lane = autoList.getChild(i);
                        const bool en = lane.getProperty(IDs::automationEnabled, false);
                        // Volume-lane authority: an ENABLED Volume lane (paramID 1
                        // or a lane named "Volume") makes automation authoritative
                        // for that track, so later set_track_volume/fader writes are
                        // overridden. Reported so gain staging can spot the hazard
                        // (set_fader_authoritative clears it). Fix 2026-09-16.
                        const int lanePid = static_cast<int>(lane.getProperty(IDs::paramID, 0));
                        const bool isVolumeLane = (lanePid == 1)
                            || lane.getProperty(IDs::name, "").toString().equalsIgnoreCase("Volume");
                        if (isVolumeLane) { ++volumeLanes; if (en) ++enabledVolumeLanes; }
                        if (en)
                        {
                            ++enabledLanes;
                            // Movement = >= 3 points in the POINT_LIST (the
                            // built-in static lanes carry 2 default points).
                            const auto pts = lane.getChildWithName(IDs::POINT_LIST);
                            if (pts.isValid() && pts.getNumChildren() >= 3) ++movableLanes;
                        }
                    }
                }
                if (lanes == 0) reasons.append("no-automation-lanes");
                else if (enabledLanes == 0) reasons.append("lanes-disabled");
                else if (movableLanes == 0) reasons.append("lanes-static");

                bool subLfo = false;
                const auto fxChain = track.getChildWithName(IDs::FX_CHAIN);
                if (fxChain.isValid())
                {
                    for (int i = 0; i < fxChain.getNumChildren(); ++i)
                    {
                        const auto slot = fxChain.getChild(i);
                        if (slot.getProperty(IDs::fxType, "").toString() != "sub_synth") continue;
                        for (int p = 29; p <= 32; ++p)
                        {
                            const double amt = slot.getProperty(juce::Identifier("param_" + juce::String(p)), 0.0);
                            if (amt != 0.0) { subLfo = true; break; }
                        }
                        if (subLfo) break;
                    }
                }

                const bool needs = !(lfoEnabled > 0 || movableLanes > 0 || subLfo);
                QJsonObject o;
                o["trackId"] = t;
                o["name"] = jstr(track.getProperty(IDs::name, "Track").toString());
                o["clipCount"] = clipCount;
                o["lfos"] = QJsonObject{{ "total", lfoTotal }, { "enabled", lfoEnabled }};
                o["automation"] = QJsonObject{ { "lanes", lanes },
                                                 { "enabledLanes", enabledLanes },
                                                 { "movableLanes", movableLanes } };
                o["subSynthInternalLfo"] = subLfo;
                o["volumeLanes"] = QJsonObject{ { "total", volumeLanes },
                                                { "enabled", enabledVolumeLanes } };
                o["faderOverridden"] = enabledVolumeLanes > 0;
                o["needsAttention"] = needs;
                o["reasons"] = reasons;
                if (needs) attention.append(t);
                else ++covered;
                if (enabledVolumeLanes > 0) faderOverriddenIds.append(t);
                tracks.append(o);
            }
            QJsonObject summary;
            summary["tracksWithClips"] = withClips;
            summary["fullyCovered"] = covered;
            summary["attentionRequiredIds"] = attention;
            summary["faderOverriddenIds"] = faderOverriddenIds;
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                QJsonObject{ { "summary", summary }, { "tracks", tracks } }).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
