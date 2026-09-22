#pragma once
// mix_report payload — the SINGLE builder behind the MCP `mix_report` tool and the RPC
// `audio.mixReport` method (AGENTS.md "Feature parity: MCP + RPC").
//
// WHY (2026-09-21 dogfood follow-up): there were TWO hand-written builders and they had
// drifted — the MCP emitted `bands` as an object and sections with `boundaryPeak` + a
// band-energy object, the RPC emitted `bands` as an array plus `bandLabels` and a different
// section shape, and the RPC was MISSING the file-visibility guard (which is what sets
// `measurementSuspicious`) — while the RPC's own comment claimed "Same JSON shape as the MCP
// mix_report tool". See docs/handoffs/2026-09-21-mcp-dogfood-composition.md (P2).
//
// READ-ONLY: reads one WAV from disk through the offline analyzer. No engine state, no DSP,
// no graph mutation.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

#include "../engine/MixReport.h"   // HDAW::MixReport / HDAW::SectionWindow

namespace HDAW {

struct MixReportPayloadResult
{
    QJsonObject payload;              // byte-identical for every surface
    QString error;                    // non-empty => failure
    bool allWindowsDropped = false;   // every supplied window fell outside the file
};

// Analyze `filePath` over `windows` (empty => the whole file) at `bpm` and build the payload.
//
// Windows are clamped to the file duration and windows that fall entirely outside are
// DROPPED (their names land in `clampedSections`); when every window is dropped the caller
// decides what to say (`allWindowsDropped`), because the message is surface wording.
//
// Runs the file-visibility guard: a just-finished export's writer may still hold the file with
// unflushed data, so another handle reads zeros for real audio (this burned an entire agentic
// remix session on 2026-09-15). A first pass that measures SILENCE on a non-trivial file waits
// and re-measures once; a genuinely silent render measures silent twice, which is reported as
// `measurementSuspicious`.
MixReportPayloadResult buildMixReportPayload(const QString& filePath,
                                             std::vector<SectionWindow> windows,
                                             double bpm);

// The drop-vs-build loudness gate: a build louder than its payoff is a FAIL. Pure shaping of
// an already-built payload (reads `sections` + the plan's name->kind map), so it is shared
// rather than re-implemented per surface.
void applyDropVsBuildGate(QJsonObject& root, const QJsonObject& planKinds, double ratio);

} // namespace HDAW
