#pragma once
// The sampler-slice payload shapers for BOTH surfaces, so the objects cannot
// drift between them (AGENTS.md "identical payload by construction"):
//   * samplerSlicePayloadJson          — detection/re-cut results
//       MCP  detect_sampler_slices / recut_sampler_slices (src/mcp/McpTools_Sampler.cpp)
//       RPC  sampler.detectSlices   / sampler.recutSlices   (src/frontend/router/Router_Sampler.cpp)
//   * samplerSliceOverridePayloadJson  — the pin write surface
//       MCP  set_sampler_slice_overrides
//       RPC  sampler.setSliceOverrides
// The two hand-rolled copies samplerSlicePayloadJson replaced had already
// diverged — the route emitted only {ok,totalSlices,slicePoints} while the
// new band-aware engine reports bandMasks/strengths/overrideCount too.
//
// The shaper is duck-typed over the engine's SamplerDetectionResult
// (src/engine/AudioEngineCommands.h) BY MEMBER NAME — ok / totalSlices /
// slicePoints / bandMasks / strengths / overrideCount — so this header stays
// free of the engine include: it is pulled in by surface TUs and must not drag
// the command layer (and its JUCE dependency) into the common layer
// (layering: common <- engine <- surface).
//
// Payload shape (compact JSON, identical bytes on both surfaces):
//   {ok, totalSlices, slicePoints:[…], bandMasks:[…], strengths:[…], overrideCount, error}
// All slicePoints are normalized 0..1; bandMasks is parallel to slicePoints
// (one mask per BOUNDARY; 0 for the implicit 0 and len endpoints) and
// strengths likewise (0..1, the band's flux at the onset / that band's max).
// `error` is the refusal/failure text ("" when ok) — the slice-mode validator's
// text travels here so both surfaces report the same refusal.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace HDAW {

template <typename DetectionResult>
inline QJsonObject samplerSlicePayloadJson(const DetectionResult& r)
{
    QJsonArray slicePoints, bandMasks, strengths;
    for (float p : r.slicePoints)
        slicePoints.append(static_cast<double>(p));
    for (uint32_t m : r.bandMasks)
        bandMasks.append(static_cast<int>(m));
    for (float s : r.strengths)
        strengths.append(static_cast<double>(s));
    return QJsonObject{{"ok", r.ok},
                       {"totalSlices", r.totalSlices},
                       {"slicePoints", slicePoints},
                       {"bandMasks", bandMasks},
                       {"strengths", strengths},
                       {"overrideCount", r.overrideCount},
                       {"error", QString::fromStdString(r.error)}};
}

// The pin write surface's payload, shared by the set_sampler_slice_overrides
// MCP tool and the sampler.setSliceOverrides route so the two cannot drift:
//   {ok, overrideCount, slicePointsOverride:[…]}
// slicePointsOverride is the STORED normalized 0..1, sorted set (endpoints
// dropped) — not the raw request, so a caller sees exactly what was pinned.
template <typename OverrideResult>
inline QJsonObject samplerSliceOverridePayloadJson(const OverrideResult& r)
{
    QJsonArray slicePointsOverride;
    for (float p : r.slicePointsOverride)
        slicePointsOverride.append(static_cast<double>(p));
    return QJsonObject{{"ok", r.ok},
                       {"overrideCount", r.overrideCount},
                       {"slicePointsOverride", slicePointsOverride}};
}

} // namespace HDAW
