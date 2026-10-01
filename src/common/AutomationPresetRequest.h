#pragma once
// automation_preset request shaping + lane resolution + success payload, shared
// by the MCP tool (src/mcp/McpTools_Automation.cpp) and the JSON-RPC route so
// both surfaces produce byte-identical text BY CONSTRUCTION (AGENTS.md
// feature-parity contract).
//
// Unit convention: the JSON boundary speaks BEATS; ProjectCommands::applyAutomationPreset
// converts to the seconds the tree stores. Header-only; JUCE + Qt only.
#include "ProjectCommands.h"         // ProjectCommands
#include "StableRefResolve.h"        // resolveTrackRef (the ONE shared rule)
#include "../engine/AutomationPreset.h"  // HDAW::AutomationPreset::PresetWindow / presetFromName
#include "../model/ProjectModel.h"   // IDs:: namespace

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace HDAW {

// Resolve an automation lane by paramID (JSON number) or name (JSON string)
// inside a track-list tree. Tree-only form of the MCP helper
// src/mcp/McpTools.cpp::findLane (same semantics; no AudioEngine needed, so the
// RPC route can share it). Returns an invalid tree when not found.
inline juce::ValueTree findAutomationLane(const juce::ValueTree& trackList,
                                          int trackId,
                                          const QJsonValue& ref)
{
    if (trackId < 0 || trackId >= trackList.getNumChildren()) return {};
    auto al = trackList.getChild(trackId).getChildWithName(IDs::AUTOMATION_LIST);
    if (ref.isDouble())
    {
        int pid = ref.toInt();
        for (int i = 0; i < al.getNumChildren(); ++i)
            if (static_cast<int>(al.getChild(i).getProperty(IDs::paramID)) == pid) return al.getChild(i);
    }
    else if (ref.isString())
    {
        QString n = ref.toString();
        for (int i = 0; i < al.getNumChildren(); ++i)
            if (al.getChild(i).getProperty(IDs::name).toString() == juce::String(n.toUtf8().constData()))
                return al.getChild(i);
    }
    return {};
}

// Parsed automation_preset request. `appliedPresets` mirrors the ORDER of the
// windows and is the `presets` array of the success payload.
struct AutomationPresetRequest
{
    std::string laneName;
    std::vector<AutomationPreset::PresetWindow> windows;
    QJsonArray appliedPresets;
    bool clearWindowBeforeApply = false;
    uint64_t seed = 12345;
    bool enable = true;
};

// Parse + validate the windows. `error` receives the exact tool text on failure.
// Validation ORDER matches the MCP tool byte-for-byte (preset name, then the
// presence checks, then the window ordering check).
inline bool parseAutomationPresetRequest(const QJsonObject& args,
                                         AutomationPresetRequest& out,
                                         std::string& error)
{
    out.windows.clear();
    out.appliedPresets = QJsonArray();
    error.clear();

    const auto sections = args.value("sections").toArray();
    if (! sections.isEmpty())
    {
        for (const auto& sv : sections)
        {
            const auto obj = sv.toObject();
            if (! obj.contains("start") || ! obj.contains("end"))
            {
                error = "each section requires start and end";
                return false;
            }
            AutomationPreset::PresetWindow w;
            w.start = obj.value("start").toDouble();
            w.end = obj.value("end").toDouble();
            const QString pName = obj.value("preset").toString(args.value("preset").toString());
            if (pName.isEmpty())
            {
                error = "preset required (pump|macro|openClose|riser|sine|square|subtleLife|randomDrift|steppedGate|phaseSweep|delayThrow)";
                return false;
            }
            const auto p = AutomationPreset::presetFromName(pName.toStdString());
            if (! p)
            {
                error = AutomationPreset::unknownPresetError(pName.toStdString());
                return false;
            }
            w.preset = *p;
            if (obj.contains("startValue"))
                w.startValue = obj.value("startValue").toDouble();
            else if (args.contains("startValue"))
                w.startValue = args.value("startValue").toDouble();
            if (obj.contains("endValue"))
                w.endValue = obj.value("endValue").toDouble();
            else if (args.contains("endValue"))
                w.endValue = args.value("endValue").toDouble();
            // B2: cycles/midPoint get the same per-section + top-level
            // fallback treatment as startValue/endValue. The sections parse
            // used to drop both silently, so a sections-form sine
            // {cycles:6} fell back to the len/4 default (24 cycles on the
            // vector-bloom breakdown window) and audibly collapsed.
            if (obj.contains("cycles"))
                w.cycles = obj.value("cycles").toDouble();
            else if (args.contains("cycles"))
                w.cycles = args.value("cycles").toDouble();
            if (obj.contains("midPoint"))
                w.midPoint = obj.value("midPoint").toDouble();
            else if (args.contains("midPoint"))
                w.midPoint = args.value("midPoint").toDouble();
            if (! (w.end > w.start))
            {
                error = QString("bad window: end (%1) must be > start (%2)")
                            .arg(w.end).arg(w.start).toStdString();
                return false;
            }
            out.windows.push_back(w);
            out.appliedPresets.append(pName);
        }
    }
    else
    {
        const QString pName = args.value("preset").toString();
        if (pName.isEmpty())
        {
            error = "preset required (pump|macro|openClose|riser|sine|square|subtleLife|randomDrift|steppedGate|phaseSweep|delayThrow) "
                    "when sections is absent";
            return false;
        }
        const auto p = AutomationPreset::presetFromName(pName.toStdString());
        if (! p)
        {
            error = AutomationPreset::unknownPresetError(pName.toStdString());
            return false;
        }
        if (! args.contains("start") || ! args.contains("end"))
        {
            error = "start and end required when sections is absent";
            return false;
        }
        AutomationPreset::PresetWindow w;
        w.start = args.value("start").toDouble();
        w.end = args.value("end").toDouble();
        w.preset = *p;
        if (args.contains("startValue")) w.startValue = args.value("startValue").toDouble();
        if (args.contains("endValue"))   w.endValue   = args.value("endValue").toDouble();
        if (args.contains("cycles"))     w.cycles     = args.value("cycles").toDouble();
        if (! (w.end > w.start))
        {
            error = QString("bad window: end (%1) must be > start (%2)")
                        .arg(w.end).arg(w.start).toStdString();
            return false;
        }
        out.windows.push_back(w);
        out.appliedPresets.append(pName);
    }

    out.clearWindowBeforeApply = args.value("clear").toBool(false);
    out.seed = static_cast<uint64_t>(args.value("seed").toInt(12345));
    out.enable = args.value("enable").toBool(true);
    return true;
}

// Success payload text (compact JSON): {lane, presets[], pointsAdded}.
inline QString automationPresetAppliedText(const AutomationPresetRequest& req, int pointsAdded)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{
            {"lane", QString::fromStdString(req.laneName)},
            {"presets", req.appliedPresets},
            {"pointsAdded", pointsAdded}}).toJson(QJsonDocument::Compact));
}

// ONE entry point for automation_preset: resolve the track ref, resolve lane,
// parse windows, apply, report. Returns the exact tool text (error text when
// *outOk is left false). B2b split: WITH the stable `trackID` the ONE shared
// rule (common/StableRefResolve.h) resolves strictly — the id wins over the
// positional `trackId`, unknown/disagreement name themselves. WITHOUT
// `trackID` the parse is BYTE-FOR-BYTE the pre-B2b one: positional `trackId`
// with its historical -1 default and no "required" gate of its own, so a
// request naming no track still reaches the lane lookup below (the route's
// `{}` has always failed THERE, with the lane text). The two-key read mirrors
// the surfaces' refArgs shape (RouterHelpers.h / McpArgs.h): presence via
// contains(), never inferred from the value.
inline QString automationPresetToolText(ProjectCommands& commands,
                                        const juce::ValueTree& trackList,
                                        const QJsonObject& args,
                                        bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    int trackId;
    if (args.contains("trackID"))
    {
        int index = HDAW::kNoRef, stableID = 0;
        if (args.contains("trackId"))
        {
            if (! args.value("trackId").isDouble())
                return QString::fromUtf8("missing or non-numeric param: trackId");
            index = static_cast<int>(args.value("trackId").toDouble());
        }
        if (! args.value("trackID").isDouble())
            return QString::fromUtf8("missing or non-numeric param: trackID");
        stableID = static_cast<int>(args.value("trackID").toDouble());
        // P1-c: gated on contains("trackID") above, so presence is stated —
        // `trackID: 0` is an unknown id, not a missing key.
        const auto ref = resolveTrackRef(trackList, index, stableID, HDAW::kTrackRefKeys, true);
        if (! ref.ok)
            return QString::fromStdString(ref.error);
        trackId = ref.index;
    }
    else
    {
        // Pre-B2b parse, byte-for-byte: `toInt(-1)` — an absent OR
        // non-numeric positional is the "no track" default, answered by the
        // lane gate below, never by a new "trackId required" error.
        trackId = args.value("trackId").toInt(-1);
    }
    auto lane = findAutomationLane(trackList, trackId, args.value("lane"));
    if (! lane.isValid())
        return QString::fromUtf8(
            "lane not found; create it with add_automation_lane first "
            "(built-in lanes like \"Volume\" work by name)");

    AutomationPresetRequest req;
    req.laneName = lane.getProperty(IDs::name, "").toString().toStdString();

    std::string error;
    if (! parseAutomationPresetRequest(args, req, error))
        return QString::fromStdString(error);

    int pointsAdded = 0;
    const std::string err = commands.applyAutomationPreset(
        trackId, req.laneName, req.windows, req.clearWindowBeforeApply, req.seed, &pointsAdded);
    if (! err.empty())
        return QString::fromStdString(err);

    if (! req.enable)
        commands.setAutomationEnabled(trackId, req.laneName, false);

    if (outOk) *outOk = true;
    return automationPresetAppliedText(req, pointsAdded);
}

} // namespace HDAW
