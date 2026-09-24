#pragma once
// MASTER-bus FX (MASTER_FX) request shaping + validation, shared by the MCP
// tools (set_master_fx_param / set_master_fx_bypassed) and the JSON-RPC route so
// both surfaces produce byte-identical text BY CONSTRUCTION (AGENTS.md
// feature-parity contract: "Where both surfaces shape the same artifact, put the
// logic in src/common/").
//
// Lesson 23: master FX params clamp at EVERY entry point. These helpers report
// the value ProjectCommands::setMasterFxParam RETURNED (the clamped write), never
// the value the caller sent.
//
// Header-only; JUCE + Qt only, no engine linkage.
#include "MasterFxDefs.h"            // HDAW::masterFxParamDefs / MasterFxParamDef
#include "ProjectCommands.h"         // ProjectCommands
#include "../model/ProjectModel.h"   // IDs:: namespace

#include <QJsonObject>
#include <QString>

#include <string>
#include <vector>

namespace HDAW {

// Resolve a param NAME to its index against the defs list the master chain
// exposes (case-insensitive). Returns -1 when not found. Moved verbatim from
// src/mcp/McpTools_FxSlot.cpp (M4, Modular Dawn audit) so both surfaces share the
// exact matching semantics.
inline int masterParamIndexByName(const std::vector<MasterFxParamDef>& defs, const QString& name)
{
    for (size_t i = 0; i < defs.size(); ++i)
        if (QString::fromUtf8(defs[i].name).compare(name, Qt::CaseInsensitive) == 0)
            return static_cast<int>(i);
    return -1;
}

// Resolved (slotIndex, paramIndex) target for a set_master_fx_param request.
// `error` is empty exactly when ok.
struct MasterFxParamTarget
{
    bool ok = false;
    int slotIndex = -1;
    int paramIndex = -1;
    std::string error;
};

// Slot-level gate: MASTER_FX presence + slotIndex range. `error` receives the
// exact tool text ("no MASTER_FX node" / "slot not found").
inline bool resolveMasterFxSlot(const juce::ValueTree& masterFx, int slotIndex, std::string& error)
{
    error.clear();
    if (! masterFx.isValid())
    {
        error = "no MASTER_FX node";
        return false;
    }
    if (slotIndex < 0 || slotIndex >= masterFx.getNumChildren())
    {
        error = "slot not found";
        return false;
    }
    return true;
}

// Param-level gate: resolves paramName (wins) / paramIndex against the slot's
// fxType defs. Reproduces the MCP tool's validation order and text exactly.
inline MasterFxParamTarget resolveMasterFxParamTarget(const juce::ValueTree& masterFx,
                                                      int slotIndex,
                                                      const QJsonObject& args)
{
    MasterFxParamTarget target;
    if (! resolveMasterFxSlot(masterFx, slotIndex, target.error))
        return target;

    target.slotIndex = slotIndex;

    const juce::String fxType = masterFx.getChild(slotIndex).getProperty(IDs::fxType, "").toString();
    const auto& defs = masterFxParamDefs(fxType);
    const bool hasName = args.contains("paramName") && ! args.value("paramName").toString().isEmpty();
    if (! hasName && ! args.contains("paramIndex"))
    {
        target.error = "paramIndex or paramName required";
        return target;
    }

    int pi = args.value("paramIndex").toInt();
    if (hasName)
    {
        pi = masterParamIndexByName(defs, args.value("paramName").toString());
        if (pi < 0)
        {
            target.error = "unknown paramName: " + args.value("paramName").toString().toStdString();
            return target;
        }
    }
    if (pi < 0 || pi >= static_cast<int>(defs.size()))
    {
        target.error = "param index out of range";
        return target;
    }

    target.paramIndex = pi;
    target.ok = true;
    return target;
}

// Result text for a validated write. Reports the RETURNED (clamped) value.
inline QString setMasterFxParamText(ProjectCommands& commands, int slotIndex, int paramIndex, float value)
{
    const float written = commands.setMasterFxParam(slotIndex, paramIndex, value);
    if (written != value)
        return QString("ok (paramIndex %1 clamped: %2 -> %3)")
            .arg(paramIndex)
            .arg(QString::number(static_cast<double>(value), 'g', 6))
            .arg(QString::number(static_cast<double>(written), 'g', 6));
    return QString("ok");
}

// ONE entry point for set_master_fx_param: resolve + write + report. Returns the
// exact tool text (error text when *outOk is left false). *outOk is optional.
inline QString setMasterFxParamToolText(ProjectCommands& commands,
                                        const juce::ValueTree& masterFx,
                                        const QJsonObject& args,
                                        bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    const auto target = resolveMasterFxParamTarget(masterFx, args.value("slotIndex").toInt(), args);
    if (! target.ok)
        return QString::fromStdString(target.error);

    const float value = static_cast<float>(args.value("value").toDouble());
    const QString text = setMasterFxParamText(commands, target.slotIndex, target.paramIndex, value);
    if (outOk) *outOk = true;
    return text;
}

// Validated set_master_fx_bypassed: slot gate then write. `error` receives the
// exact tool text; returns false without writing when the gate fails.
inline bool setMasterFxBypassedValidated(ProjectCommands& commands,
                                         const juce::ValueTree& masterFx,
                                         int slotIndex,
                                         bool bypassed,
                                         std::string& error)
{
    if (! resolveMasterFxSlot(masterFx, slotIndex, error))
        return false;
    commands.setMasterFxBypassed(slotIndex, bypassed);
    return true;
}

// ONE entry point for set_master_fx_bypassed: resolve + write + report.
inline QString setMasterFxBypassedToolText(ProjectCommands& commands,
                                           const juce::ValueTree& masterFx,
                                           const QJsonObject& args,
                                           bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    std::string error;
    if (! setMasterFxBypassedValidated(commands, masterFx, args.value("slotIndex").toInt(),
                                       args.value("bypassed").toBool(), error))
        return QString::fromStdString(error);

    if (outOk) *outOk = true;
    return QString("ok");
}

} // namespace HDAW
