#pragma once
// Modulation-coverage audit — the SINGLE implementation behind the MCP tool
// `audit_modulation_coverage` and the RPC method `modulation.coverage`.
//
// It was inline in src/mcp/McpTools_Modulation.cpp (no RPC route at all) until 2026-09-21; the
// extraction also lets `mix_verdict` include the modulation gate it previously had to exclude
// (docs/handoffs/2026-09-21-mcp-dogfood-composition.md).
//
// The global modulation rule: every SOUNDING track (>= 1 clip) must carry movement — an enabled
// LFO, an ENABLED automation lane with >= 3 points (the built-in static lanes carry 2), or a
// sub_synth internal LFO. Volume-lane authority is reported per track: an enabled Volume lane
// makes automation authoritative, so later fader writes are overridden (call
// set_fader_authoritative before gain staging).
//
// READ-ONLY: reads the project tree. No mutation, no render.

#include <QJsonObject>

#include <juce_data_structures/juce_data_structures.h>

namespace HDAW {

// Returns {summary: {tracksWithClips, fullyCovered, attentionRequiredIds, faderOverriddenIds},
//          tracks: [{trackId, name, clipCount, lfos{...}, automation{...}, volumeLanes{...},
//                    subSynthInternalLfo, needsAttention, reasons[]}]}
QJsonObject modulationCoverageJson(const juce::ValueTree& trackList);

} // namespace HDAW
