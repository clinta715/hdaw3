#pragma once
// automation_preset request shaping + lane resolution + success payload, shared
// by the MCP tool (src/mcp/McpTools_Automation.cpp) and the JSON-RPC route so
// both surfaces produce byte-identical text BY CONSTRUCTION (AGENTS.md
// feature-parity contract).
//
// Unit convention: the JSON boundary speaks BEATS; ProjectCommands::applyAutomationPreset
// converts to the seconds the tree stores. Header-only; JUCE + Qt only.
#include "ProjectCommands.h"         // ProjectCommands
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
                error = "unknown preset: " + pName.toStdString();
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
            error = "unknown preset: " + pName.toStdString();
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

// ONE entry point for automation_preset: resolve lane, parse windows, apply,
// report. Returns the exact tool text (error text when *outOk is left false).
inline QString automationPresetToolText(ProjectCommands& commands,
                                        const juce::ValueTree& trackList,
                                        const QJsonObject& args,
                                        bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    const int trackId = args.value("trackId").toInt(-1);
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
