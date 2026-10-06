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
// Single source of truth for the psytrance presets (values ported verbatim
// from the render-verified MCP implementation). The MCP tool and the
// frontend router both apply presets through AudioEngineCommands
// (tree-first), never by touching the live engine directly.
//
// Param mapping (matches TrackFXSlot getParamDefsForType("psy_fm")):
//   0..5 base ratios, 6 base feedback, 7..30 envelopes (7 + op*4 + {A,D,S,R}),
//   31 output level, 32 algorithm index, 33..37 post-carrier filter
//   (Cutoff, Resonance, Type, Key Track, Env Amount). Slice D (2026-10-06)
//   appended five role presets (pad/bell/pluck/drone/stab) and widened param
//   32 to 0..5 (0 growl, 1 acid, 2 pluck, 3 riser, 4 pad, 5 bell). Slice E
//   (2026-10-06) made a preset the WHOLE sound by adding `filter` and writing
//   it; the older four rows carry the NEUTRAL filter (see kNeutralFilter in
//   the rows below), which is identical to the engine defaults and to slice
//   B's bypass condition (cutoff<19999 || keyTrack!=0 || envAmount!=0), so
//   their renders stay bit-identical.

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
    float filter[5];      // param 33..37: {Cutoff, Resonance, Type, KeyTrack, EnvAmount}
};

// The preset table — ONE place. findPreset(), presetNames() and the refusal
// text below all read it, so a preset can never be loadable but unnameable.
inline const PresetDef* presetTable (std::size_t& count)
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
          "feedbackLFO:op6Feedback:0.4",
          // Slice E: neutral filter == engine defaults == slice B's bypass
          // condition, so this row's render is bit-identical to pre-slice-E.
          { 20000.0f, 0.7f, 0.0f, 0.0f, 0.0f } },
        { "acidLead",
          { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 0.1f, 1,
          { { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f },
            { 0.001f, 0.5f, 0.6f, 0.3f } },
          0.35f, 0.2f,
          "modWheel:op6Feedback:0.9",
          { 20000.0f, 0.7f, 0.0f, 0.0f, 0.0f } },
        { "metallicPluck",
          { 1.0f, 1.0f, 3.14f, 1.0f, 1.41f, 1.0f }, 0.0f, 2,
          { { 0.005f, 0.8f, 0.7f, 0.3f },     // op0 carrier: slow
            { 0.005f, 0.8f, 0.7f, 0.3f },     // op1 modulator: slow
            { 0.005f, 0.8f, 0.7f, 0.3f },     // op2: slow
            { 0.001f, 0.15f, 0.0f, 0.1f },    // op3 inharmonic: fast
            { 0.001f, 0.15f, 0.0f, 0.1f },    // op4 inharmonic: fast
            { 0.005f, 0.8f, 0.7f, 0.3f } },   // op5: slow
          0.3f, 0.2f,
          "feedbackLFO:op6Feedback:0.15",
          { 20000.0f, 0.7f, 0.0f, 0.0f, 0.0f } },
        { "riser",
          { 1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 1.0f }, 0.15f, 3,
          { { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f },
            { 0.5f, 2.0f, 0.9f, 1.0f } },
          0.35f, 0.2f,
          "barClock:ratioSweepRate:1;ratioSweepLFO:op4Ratio:0.3",
          { 20000.0f, 0.7f, 0.0f, 0.0f, 0.0f } },

        // ── Slice D (2026-10-06) role presets — APPENDED; the four rows above
        // are untouched, so their renders stay bit-identical. Each row's
        // `matrix` is the encoded form of the matching PsyFmPatches::make*
        // helper (a test pins the two together, so they cannot drift).
        // Names automatically extend psy_fm_load_preset's enum + refusal text.
        { "pad",
          { 1.0f, 1.005f, 1.0f, 2.0f, 1.0f, 1.0f }, 0.05f, 4,
          { { 0.8f, 1.5f, 0.9f, 2.0f },     // op1 carrier
            { 0.8f, 1.5f, 0.9f, 2.0f },     // op2 carrier (1.005 => slow beat)
            { 0.8f, 1.5f, 0.9f, 2.0f },
            { 1.2f, 2.0f, 0.9f, 2.0f },     // op4: lazy modulator
            { 1.2f, 2.0f, 0.9f, 2.0f },     // op5: carrier modulator
            { 0.8f, 1.5f, 0.9f, 2.0f } },
          0.35f, 0.15f,
          "ratioSweepLFO:op4Ratio:0.05",
          // Slice E: warm LP + key-track + env lift (pads open slightly on
          // attack and follow the chord register).
          { 1200.0f, 0.7f, 0.0f, 0.3f, 0.4f } },
        { "bell",
          { 1.0f, 1.0f, 2.76f, 1.0f, 3.5f, 1.0f }, 0.65f, 5,
          { { 0.001f, 1.5f, 0.0f, 0.8f },   // carrier: longest ring
            { 0.001f, 0.6f, 0.0f, 0.3f },
            { 0.001f, 0.4f, 0.0f, 0.2f },   // op3 (2.76, inharmonic)
            { 0.001f, 0.5f, 0.0f, 0.25f },
            { 0.001f, 0.2f, 0.0f, 0.12f },  // op5 (3.5, inharmonic)
            { 0.001f, 0.15f, 0.0f, 0.1f } },// op6: strike, fastest
          0.5f, 0.2f,
          "feedbackLFO:op6Feedback:0.3",
          // Slice E: bright LP with full key-track (bell pitch tracks the
          // struck note), no env offset.
          { 6000.0f, 1.2f, 0.0f, 1.0f, 0.0f } },
        { "pluck",
          { 1.0f, 5.0f, 1.0f, 9.0f, 1.0f, 1.0f }, 0.1f, 2,
          { { 0.001f, 0.10f, 0.0f, 0.08f },
            { 0.001f, 0.06f, 0.0f, 0.05f }, // op2 (5x): bright, fast
            { 0.001f, 0.08f, 0.0f, 0.05f },
            { 0.001f, 0.04f, 0.0f, 0.03f }, // op4 (9x): brightest
            { 0.001f, 0.08f, 0.0f, 0.05f },
            { 0.001f, 0.08f, 0.0f, 0.05f } },
          0.45f, 0.2f,
          "feedbackLFO:op6Feedback:0.1",
          // Slice E: bright LP (near open), half key-track, env snap on the
          // transient — keeps `pluck` bright while `stab` goes dark.
          { 10000.0f, 1.0f, 0.0f, 0.5f, 0.7f } },
        { "drone",
          { 1.0f, 2.0f, 1.0f, 1.0f, 3.0f, 1.0f }, 0.2f, 4,
          { { 2.0f, 5.0f, 0.95f, 3.0f },    // 2.0 = the def-table attack max
            { 2.0f, 5.0f, 0.95f, 3.0f },
            { 2.0f, 5.0f, 0.95f, 3.0f },
            { 2.0f, 5.0f, 0.95f, 3.0f },
            { 2.0f, 5.0f, 0.95f, 3.0f },
            { 2.0f, 5.0f, 0.95f, 3.0f } },
          0.3f, 0.08f,
          "ratioSweepLFO:op4Ratio:0.25;modWheel:op1Ratio:0.1",
          // Slice E: dark static LP (drones ride the mod matrix, not the
          // filter envelope).
          { 800.0f, 0.5f, 0.0f, 0.0f, 0.0f } },
        { "stab",
          { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 4.0f }, 0.75f, 1,
          { { 0.001f, 0.09f, 0.0f, 0.06f },
            { 0.001f, 0.09f, 0.0f, 0.06f },
            { 0.001f, 0.09f, 0.0f, 0.06f },
            { 0.001f, 0.09f, 0.0f, 0.06f },
            { 0.001f, 0.09f, 0.0f, 0.06f },
            { 0.001f, 0.05f, 0.0f, 0.04f } },// op6: hard strike
          0.5f, 0.2f,
          "modWheel:op6Feedback:0.9",
          // Slice E: dark resonant LP, gentle key-track + env snap — darker
          // than `pluck` (the two share a percussive envelope, so the filter is
          // what keeps their spectra apart; D's distinctness gate proves it).
          { 700.0f, 1.5f, 0.0f, 0.3f, 0.4f } },
    };

    count = sizeof(presets) / sizeof(presets[0]);
    return presets;
}

inline const PresetDef* findPreset (const std::string& name)
{
    std::size_t count = 0;
    const PresetDef* presets = presetTable (count);
    for (std::size_t i = 0; i < count; ++i)
        if (name == presets[i].name) return &presets[i];
    return nullptr;
}

/// The preset vocabulary, in table order (names only).
inline std::vector<const char*> presetNames()
{
    std::size_t count = 0;
    const PresetDef* presets = presetTable (count);
    std::vector<const char*> names;
    names.reserve (count);
    for (std::size_t i = 0; i < count; ++i) names.push_back (presets[i].name);
    return names;
}

/// "growlBass, acidLead, metallicPluck, riser, pad, bell, pluck, drone,
/// stab" — built from the table so it cannot drift from what findPreset()
/// actually loads.
inline std::string presetNameList()
{
    std::size_t count = 0;
    const PresetDef* presets = presetTable (count);
    std::string out;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!out.empty()) out += ", ";
        out += presets[i].name;
    }
    return out;
}

/// ONE refusal text for an unknown preset name (value + the allowed set).
inline std::string unknownPresetError (const std::string& name)
{
    return "unknown preset: " + name + " (valid: " + presetNameList() + ")";
}

/// True if `name` is one of the known preset names in presetTable().
inline bool isKnownPreset (const std::string& name)
{
    return findPreset (name) != nullptr;
}

} // namespace PsyFmState
} // namespace HDAW
