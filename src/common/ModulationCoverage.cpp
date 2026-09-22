#include "ModulationCoverage.h"

#include "../model/ProjectModel.h"   // IDs

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace HDAW {

namespace {

// UTF-8-safe juce::String -> QString (the MCP layer's jstr); the audit prints track names.
QString jstr(const juce::String& s)
{
    return QString::fromUtf8(s.toRawUTF8());
}

} // namespace

QJsonObject modulationCoverageJson(const juce::ValueTree& trackListIn)
{
    auto trackList = trackListIn;
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
    return QJsonObject{ { "summary", summary }, { "tracks", tracks } };
}

} // namespace HDAW
