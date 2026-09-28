#pragma once
// verify_window payload — the SINGLE shaper behind the MCP `verify_window` tool
// and the RPC `composition.verifyWindow` route (AGENTS.md "Feature parity:
// MCP + RPC" / S4 of docs/plans/2026-09-28-agent-mechanization.md).
//
// The payload reports the WINDOW, never the file:
//   {wavPath, window:{startBeat,endBeat,startSec,endSec,durationSec},
//    report, targetChecks, targetsOk, ok}
// `report` is buildWindowReportPayload's payload — the window's metrics promoted
// to the root (duration/peak/rms/bands/kickProminence/ceilingHitPct/
// ceilingHitFrames), plus the `targetChecks`/`targetsOk` rows applyTargetGates
// wrote. Both surfaces emit these bytes for the same result, because this
// function is the only shaper.
//
// READ-ONLY: shapes a command result. No engine state, no DSP.

#include <QJsonObject>
#include <QString>

#include "ProjectCommands.h"

namespace HDAW {

inline QJsonObject buildVerifyWindowPayload(const ProjectCommands::VerifyWindowResult& r)
{
    return QJsonObject{
        { "ok", r.ok },
        { "wavPath", QString::fromStdString(r.wavPath) },
        { "window", QJsonObject{
            { "startBeat", r.startBeat },
            { "endBeat", r.endBeat },
            { "startSec", r.startSec },
            { "endSec", r.endSec },
            { "durationSec", r.durationSec } } },
        { "report", r.report },
        { "targetChecks", r.targetChecks },
        { "targetsOk", r.targetsOk } };
}

} // namespace HDAW
