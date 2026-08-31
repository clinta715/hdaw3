#pragma once

namespace HDAW {

class PsyFmEngine;

/// Default algorithm functions for common psytrance roles.
/// Each defines how the 6 operators chain/sum into the carrier output.

/// Growl bass: op6 (feedback) → op5 → op1 (carrier)
void growlBassAlgorithm (PsyFmEngine& voice, int numSamples);

/// Acid lead: op6 (high feedback) → op1 (carrier) — near self-oscillation
void acidLeadAlgorithm (PsyFmEngine& voice, int numSamples);

/// Metallic pluck: op4 (non-integer ratio) → op2 → op1 (carrier)
void metallicPluckAlgorithm (PsyFmEngine& voice, int numSamples);

/// Riser: op5 (ratio-sweep LFO target) → op3 → op1 (carrier)
void riserAlgorithm (PsyFmEngine& voice, int numSamples);

} // namespace HDAW
