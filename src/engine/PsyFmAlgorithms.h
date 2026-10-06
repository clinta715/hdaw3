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

// ── Slice D (2026-10-06): pads + bells — ADDITIVE, indices 4/5 ──
// Existing indices 0..3 and their renders are untouched; these two are
// appended, so param 32's range only widens (0..5).

/// Pad: DUAL carrier — op1 + op2 (indices 0, 1) both summed into
/// carrierMix(), phase-modulated by op5 (index 4), which op4 (index 3)
/// modulates in turn.
void padAlgorithm (PsyFmEngine& voice, int numSamples);

/// Bell: deep chain op5 → op4 → op2 → op1 (indices 4→3→1→0), driven by the
/// preset's high base feedback (inharmonic ratios + fast decay).
void bellAlgorithm (PsyFmEngine& voice, int numSamples);

} // namespace HDAW
