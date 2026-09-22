// Router_Modulation.cpp — RPC surface for the modulation-coverage audit.
//
// modulation.coverage mirrors the MCP tool `audit_modulation_coverage` 1:1 by calling the shared
// HDAW::modulationCoverageJson (src/common/ModulationCoverage.cpp), so the payload cannot drift
// between the surfaces — parity by construction (AGENTS.md "Feature parity: MCP + RPC"). Before
// 2026-09-21 the audit was inline in the MCP layer with NO RPC route at all.
//
// READ-ONLY: walks the project tree. No DSP, no audio thread, no render, no mutation.

#include "Router_Modulation.h"
#include "RouterHelpers.h"

#include "../../common/ModulationCoverage.h"
#include "../../engine/AudioEngine.h"
#include "../../model/ProjectModel.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace frontend {

DispatchResult dispatchModulation(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    (void)params;   // the audit takes no arguments

    if (m == "coverage")
        return { false, HDAW::modulationCoverageJson(engine.getProjectModel().getTrackListTree()) };

    return makeError(-32601, "unknown modulation method: " + m);
}

} // namespace frontend
