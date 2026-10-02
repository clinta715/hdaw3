#pragma once
// The ONE slice-mode vocabulary for the sampler slice surfaces.
//
// Three modes, all case-insensitive (leading/trailing whitespace ignored):
//   transient — the band-aware onset engine (SliceDetector::onsets)
//   grid      — the nominal beat grid (SliceDetector::grid)
//   aligned   — the onset-tracked grid (SliceDetector::driftAlignedGrid): a grid
//               whose tempo/phase is least-squares fitted to the detected onsets,
//               so a source that plays a few percent slow is sliced where it
//               actually plays instead of where the nominal grid says
//
// Before this header the engine treated ANY unknown string as transient (a
// silent-acceptance defect: `"transientt"` silently changed the algorithm) and
// setSamplerSliceMode stored whatever string it was given. Both now go through
// sliceModeRefusal() / normalizeSliceMode() so the MCP tools, the RPC routes and
// the engine commands share one accepted set AND one refusal text.
//
// Layering: pure std, no JUCE/Qt — common/ is pulled in by the engine layer and
// the surface layer alike (common <- engine <- surface).

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace HDAW {

// The valid set, spelled the way the refusal names it. Kept as one literal so a
// schema enum, a tool description and the refusal text cannot drift apart.
inline constexpr const char* kSliceModesText = "transient, grid, aligned";

// Canonical lower-cased token for a known mode, or an EMPTY string for an
// unknown one (so `normalizeSliceMode(x).empty()` is the validity test).
inline std::string normalizeSliceMode(const std::string& raw)
{
    std::string s;
    s.reserve(raw.size());
    for (char c : raw)
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    s = s.substr(first, last - first + 1);

    if (s == "transient" || s == "grid" || s == "aligned")
        return s;
    return {};
}

inline bool isKnownSliceMode(const std::string& raw)
{
    return !normalizeSliceMode(raw).empty();
}

// The refusal text naming the valid set, or an EMPTY string when `raw` is valid.
// Callers hand this straight to their own error channel, so the wording lives in
// exactly one place.
inline std::string sliceModeRefusal(const std::string& raw)
{
    if (isKnownSliceMode(raw))
        return {};
    return "invalid sliceMode '" + raw + "' (valid: " + kSliceModesText + ")";
}

// The refusal text for a slicePointsOverride element that is not a JSON
// NUMBER, naming its index. The text is byte-identical to what the MCP
// validator emits for the tool's `{"type":"array","items":{"type":"number"}}`
// schema (McpSchema.cpp's typeMatches answers "expected number" for anything
// that is not a JSON number, and McpServer prefixes "invalid params: "), so the
// MCP schema gate and the RPC route refuse in the SAME bytes — the same
// construction as common/BatchEditJson.h's parse*Arg helpers.
//
// Why it matters: the two surfaces used to build the override array with
// QJsonValue::toDouble() per element, so a non-number coerced to 0 and was then
// dropped as an implicit endpoint — `[0.25, "oops"]` SUCCEEDED with one pin
// (the accepted-argument-dropped class). common/ is free of Qt, so each surface
// does its own number test (QJsonValue::isDouble(), which is TRUE for a JSON
// integer too — Qt stores every JSON number as a double), but the rule, the
// index and the text live here.
inline std::string sliceOverrideElementRefusal(std::size_t index)
{
    return "invalid params: slicePointsOverride[" + std::to_string(index)
           + "]: expected number";
}

// Validate the per-element "is this a JSON number?" flags of a
// slicePointsOverride array: the refusal text for the FIRST non-number (naming
// its index), or an empty string when EVERY element is a number. A non-empty
// answer refuses the WHOLE call — nothing is written.
inline std::string sliceOverrideRefusal(const std::vector<bool>& isNumber)
{
    for (std::size_t i = 0; i < isNumber.size(); ++i)
        if (!isNumber[i])
            return sliceOverrideElementRefusal(i);
    return {};
}

} // namespace HDAW
