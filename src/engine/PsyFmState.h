#pragma once
#include "PsyFmModMatrix.h"

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <optional>

namespace HDAW {
namespace PsyFmState {

// ── Route codec (tree representation of the mod matrix) ──
//
// The FX-slot ValueTree carries the matrix as a text property `psyFmMatrix`:
//     "source:dest:depth;source:dest:depth;..."
// Names match the psy_fm_set_mod_route MCP enum strings; dest also accepts
// `ratioSweepRate` (used by the riser preset). Text (not enum ordinals) so
// the save format is stable across enum changes. Absent/empty = no routes.

inline const char* sourceName (PsyFmModRoute::Source s)
{
    switch (s)
    {
        case PsyFmModRoute::Source::RatioSweepLFO: return "ratioSweepLFO";
        case PsyFmModRoute::Source::FeedbackLFO:   return "feedbackLFO";
        case PsyFmModRoute::Source::ModWheel:      return "modWheel";
        case PsyFmModRoute::Source::Velocity:      return "velocity";
        case PsyFmModRoute::Source::BarClock:      return "barClock";
    }
    return "ratioSweepLFO";
}

inline const char* destName (PsyFmModRoute::Dest d)
{
    switch (d)
    {
        case PsyFmModRoute::Dest::Op1Ratio:  return "op1Ratio";
        case PsyFmModRoute::Dest::Op2Ratio:  return "op2Ratio";
        case PsyFmModRoute::Dest::Op3Ratio:  return "op3Ratio";
        case PsyFmModRoute::Dest::Op4Ratio:  return "op4Ratio";
        case PsyFmModRoute::Dest::Op5Ratio:  return "op5Ratio";
        case PsyFmModRoute::Dest::Op6Ratio:  return "op6Ratio";
        case PsyFmModRoute::Dest::Op6Feedback: return "op6Feedback";
        case PsyFmModRoute::Dest::RatioSweepRateItself: return "ratioSweepRate";
    }
    return "op1Ratio";
}

inline std::optional<PsyFmModRoute::Source> sourceFromName (const std::string& n)
{
    if (n == "ratioSweepLFO") return PsyFmModRoute::Source::RatioSweepLFO;
    if (n == "feedbackLFO")   return PsyFmModRoute::Source::FeedbackLFO;
    if (n == "modWheel")      return PsyFmModRoute::Source::ModWheel;
    if (n == "velocity")      return PsyFmModRoute::Source::Velocity;
    if (n == "barClock")      return PsyFmModRoute::Source::BarClock;
    return std::nullopt;
}

inline std::optional<PsyFmModRoute::Dest> destFromName (const std::string& n)
{
    if (n == "op1Ratio")  return PsyFmModRoute::Dest::Op1Ratio;
    if (n == "op2Ratio")  return PsyFmModRoute::Dest::Op2Ratio;
    if (n == "op3Ratio")  return PsyFmModRoute::Dest::Op3Ratio;
    if (n == "op4Ratio")  return PsyFmModRoute::Dest::Op4Ratio;
    if (n == "op5Ratio")  return PsyFmModRoute::Dest::Op5Ratio;
    if (n == "op6Ratio")  return PsyFmModRoute::Dest::Op6Ratio;
    if (n == "op6Feedback") return PsyFmModRoute::Dest::Op6Feedback;
    if (n == "ratioSweepRate") return PsyFmModRoute::Dest::RatioSweepRateItself;
    return std::nullopt;
}

inline std::string encodeRoutes (const std::vector<PsyFmModRoute>& routes)
{
    std::string out;
    char depth[32];
    for (size_t i = 0; i < routes.size(); ++i)
    {
        if (i > 0) out += ";";
        std::snprintf (depth, sizeof (depth), "%.6g", static_cast<double> (routes[i].depth));
        out += sourceName (routes[i].source);
        out += ":";
        out += destName (routes[i].dest);
        out += ":";
        out += depth;
    }
    return out;
}

inline std::vector<PsyFmModRoute> decodeRoutes (const std::string& encoded)
{
    std::vector<PsyFmModRoute> routes;
    if (encoded.empty()) return routes;

    size_t pos = 0;
    while (pos <= encoded.size())
    {
        size_t semi = encoded.find (';', pos);
        if (semi == std::string::npos) semi = encoded.size();
        std::string route = encoded.substr (pos, semi - pos);
        pos = semi + 1;
        if (route.empty()) continue;

        size_t c1 = route.find (':');
        size_t c2 = (c1 == std::string::npos) ? std::string::npos : route.find (':', c1 + 1);
        if (c1 == std::string::npos || c2 == std::string::npos) continue; // skip malformed

        auto src = sourceFromName (route.substr (0, c1));
        auto dst = destFromName (route.substr (c1 + 1, c2 - c1 - 1));
        if (! src || ! dst) continue;

        float depth = 0.0f;
        try { depth = std::stof (route.substr (c2 + 1)); } catch (...) { continue; }
        if (! (std::isfinite (depth))) continue;

        routes.push_back ({ *src, *dst, depth });
    }
    return routes;
}

// ── Preset table ──
//
// Single source of truth for the four psytrance presets (values ported
// verbatim from the render-verified MCP implementation). The MCP tool and the
// frontend router both apply presets through AudioEngineCommands
// (tree-first), never by touching the live engine directly.
//
// Param mapping (matches TrackFXSlot getParamDefsForType("psy_fm")):
//   0..5 base ratios, 6 base feedback, 7..30 envelopes (7 + op*4 + {A,D,S,R}),
//   31 output level, 32 algorithm index (0 growl, 1 acid, 2 pluck, 3 riser).

struct PresetDef
{
    const char* name;
    float ratios[6];
    float feedback;
    int   algorithm;
    float env[6][4];      // [op][attack, decay, sustain, release]
    float outputLevel;
    float sweepRateHz;    // PsyFmModSourcePool::ratioSweepLFORateHz
    const char* matrix;   // encoded routes
};

inline const PresetDef* findPreset (const std::string& name)
{
    static const PresetDef presets[] = {
        { "growlBass",
          { 1.0f, 2.0f, 1.0f, 1.0f, 3.0f, 1.0f }, 0.3f, 0,
          { { 0.005f, 0.4f, 0.8f, 0.1f },
            { 0.005f, 0.4f, 0.8f, 0.1f },
            { 0.005f, 0.4f, 0.8f, 0.1f },
            { 0.005f, 0.4f, 0.8f, 0.1f },
            { 0.005f, 0.4f, 0.8f, 0.1f },
            { 0.005f, 0.4f, 0.8f, 0.1f } },
          0.4f, 0.2f,
          "feedbackLFO:op6Feedback:0.4" },
        { "acidLead",
          { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 0.1f, 1,
          { { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f } },
          0.35f, 0.2f,
          "modWheel:op6Feedback:0.9" },
        { "metallicPluck",
          { 1.0f, 1.0f, 3.14f, 1.0f, 1.41f, 1.0f }, 0.0f, 2,
          { { 0.005f, 0.8f, 0.7f, 0.3f },     // op0 carrier: slow
            { 0.005f, 0.8f, 0.7f, 0.3f },     // op1 modulator: slow
            { 0.005f, 0.8f, 0.7f, 0.3f },     // op2: slow
            { 0.001f, 0.15f, 0.0f, 0.1f },    // op3 inharmonic: fast
            { 0.001f, 0.15f, 0.0f, 0.1f },    // op4 inharmonic: fast
            { 0.005f, 0.8f, 0.7f, 0.3f } },   // op5: slow
          0.3f, 0.2f,
          "feedbackLFO:op6Feedback:0.15" },
        { "riser",
          { 1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 1.0f }, 0.15f, 3,
          { { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f } },
          0.35f, 0.2f,
          "barClock:ratioSweepRate:1;ratioSweepLFO:op4Ratio:0.3" },
    };

    for (const auto& p : presets)
        if (name == p.name) return &p;
    return nullptr;
}

/// True if `name` is one of the four known presets.
inline bool isKnownPreset (const std::string& name)
{
    return findPreset (name) != nullptr;
}

} // namespace PsyFmState
} // namespace HDAW
