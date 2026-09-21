#pragma once

// Shared parse/format for the FX_SLOT offline param-override ledger
// (IDs::appliedParamOverrides, format "idx=val;idx=val", values normalized 0..1).
//
// Lives in src/common so that the three participants agree on ONE ledger
// grammar — parity by construction, not by discipline:
//   * writer A: McpTools_Matrix::writeAppliedParamOverrides (matrix presets,
//     REPLACE semantics: one preset per render);
//   * writer B: AudioEngineCommands::setPluginParam (plain plugin-param
//     persistence, MERGE semantics: one index at a time);
//   * reader:   ExportManager::replayAppliedParamOverrides (fresh export child).
//
// Engine surface only: no DSP, no audio thread, no plugin instantiation.
//
// Plan:    docs/plans/2026-09-21-plugin-param-persistence.md
// Why:     docs/plans/2026-09-21-vavra-live-param-delivery.md — host-param
//          writes reach the LIVE isolated child only, so the ledger is the
//          only channel that reaches tree-copy renders (export / audition /
//          verify_part) and save-load.

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace HDAW {

using ParamOverridePair = std::pair<int, float>;

// "idx=val" for ONE entry. The value uses juce::String(value, 6) exactly like
// the matrix writer, so both writers emit a byte-identical grammar.
juce::String formatParamOverride(int index, float normalizedValue);

// Parses the ledger into (liveParamIndex, normalizedValue) pairs.
// Absent/empty ledger -> empty. Malformed tokens (no leading "=", negative
// index) are skipped, never fatal. Order is preserved and duplicates are NOT
// collapsed: the replay applies entries in order, so an old ledger keeps its
// exact last-wins semantics.
std::vector<ParamOverridePair> parseParamOverrides(const juce::String& ledger);

// Returns `ledger` with `index` set to `normalizedValue`: the first occurrence
// keeps its position, later duplicates of the same index are dropped, and a
// missing index is appended. Idempotent. Negative index returns `ledger`.
juce::String mergeParamOverride(const juce::String& ledger, int index,
                                float normalizedValue);

// Returns `ledger` without any entry for `index` (empty string when none
// remain — callers use that to REMOVE the property entirely).
juce::String removeParamOverride(const juce::String& ledger, int index);

} // namespace HDAW
