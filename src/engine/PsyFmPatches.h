#pragma once
#include "PsyFmModMatrix.h"

namespace HDAW {
namespace PsyFmPatches {

/// Growl bass: feedback starts high, decays via envelope — "settling growl"
PsyFmModMatrix makeGrowlBassMatrix();

/// Riser: bar clock speeds up ratio-sweep LFO rate (sub-audio → audio-rate)
PsyFmModMatrix makeRiserMatrix();

/// Acid lead: mod wheel drives feedback toward self-oscillation
PsyFmModMatrix makeAcidLeadMatrix();

/// Metallic pluck: fast-decay envelope on modulator output level (via operator envelope)
PsyFmModMatrix makeMetallicPluckMatrix();

// ── Slice D (2026-10-06) role presets ──────────────────────────────────────
// Each helper produces the SAME routes as the matching presetTable() row's
// encoded `matrix` string (a test pins encodeRoutes(helper) == row.matrix, so
// the two cannot drift). The load path applies the string; these exist so the
// routing is expressible/testable as data at the call site.

/// Pad: slow ratio-sweep LFO drift on the modulator (op5, index 4)
PsyFmModMatrix makePadMatrix();

/// Bell: feedback LFO undulates op6 (index 5) feedback for an evolving strike
PsyFmModMatrix makeBellMatrix();

/// Pluck: light feedback-LFO movement on op6 feedback
PsyFmModMatrix makePluckMatrix();

/// Drone: slow ratio-sweep drift plus a mod-wheel ratio offset
PsyFmModMatrix makeDroneMatrix();

/// Stab: mod wheel drives op6 (index 5) feedback toward the percussive ceiling
PsyFmModMatrix makeStabMatrix();

} // namespace PsyFmPatches
} // namespace HDAW
