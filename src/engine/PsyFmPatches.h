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

} // namespace PsyFmPatches
} // namespace HDAW
