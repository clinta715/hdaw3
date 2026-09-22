#pragma once
// mix_verdict — ONE release-readiness verdict over a rendered file (+ the song plan), so
// "did I finish?" is one call instead of four separate verifiers with hand-written thresholds
// (2026-09-21 dogfood P3-3: structure / modulation / spectrum / tuning were each their own
// call) — docs/handoffs/2026-09-21-mcp-dogfood-composition.md.
//
// It COMPOSES gates that already have shared implementations; it adds no new analysis maths:
//   audible      rms > 1e-4                                  (silence masks every delta, lesson 25)
//   clipping     !mix_report.clipping                        (peak against 0.999)
//   loudness     drop-vs-build, WHEN a plan kind map is given (applyDropVsBuildGate)
//   structure    the arrangement-variety audit, WHEN supplied (audit_song_structure)
//   introBlast   MixReportAnalyzer::analyzeBlast over the first N seconds, when N > 0
// NOT included, deliberately: MODULATION coverage. `audit_modulation_coverage` is still
// implemented inline in the MCP layer (McpTools_Modulation.cpp) with no shared equivalent, so
// this verdict does not claim to cover it — extract that audit first (recorded follow-up).
//
// READ-ONLY: reads one WAV (and the caller's plan-derived payloads). No engine mutation.

#include <QJsonObject>
#include <QString>

#include <vector>

#include "../engine/MixReport.h"   // HDAW::SectionWindow

namespace HDAW {

struct MixVerdictResult
{
    QJsonObject verdict;
    QString error;
};

// `windows`/`bpm`/`planKinds` are exactly what mix_report takes (empty windows => whole file).
// `dropBuildRatio` applies only when `planKinds` is non-empty. `structureAudit` is the
// `structureAuditJson(...)` payload (an empty object skips the structure gate).
// `introSeconds` > 0 enables the intro-blast gate over that opening window.
MixVerdictResult buildMixVerdict(const QString& filePath,
                                 std::vector<SectionWindow> windows,
                                 const QJsonObject& planKinds,
                                 double bpm,
                                 double dropBuildRatio,
                                 const QJsonObject& structureAudit,
                                 double introSeconds);

} // namespace HDAW
