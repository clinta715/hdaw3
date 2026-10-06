#include "PsyFmAlgorithms.h"
#include "PsyFmEngine.h"
#include <algorithm>

namespace HDAW {

void growlBassAlgorithm (PsyFmEngine& voice, int numSamples)
{
    // op6 (index 5, with feedback) → modulates op5 (index 4) → modulates op1 (index 0, carrier)
    float* op6Buf = voice.getScratch (5);
    voice.op (5).renderBlock (op6Buf, nullptr, numSamples);

    float* op5Buf = voice.getScratch (4);
    voice.op (4).renderBlock (op5Buf, op6Buf, numSamples);

    auto& carrierOut = voice.carrierMix();
    carrierOut.resize (static_cast<size_t> (numSamples));
    voice.op (0).renderBlock (carrierOut.data(), op5Buf, numSamples);
}

void acidLeadAlgorithm (PsyFmEngine& voice, int numSamples)
{
    // op6 (index 5, high feedback) → modulates op1 (index 0, carrier)
    // High feedback pushes the operator toward self-oscillation / noise
    float* op6Buf = voice.getScratch (5);
    voice.op (5).renderBlock (op6Buf, nullptr, numSamples);

    auto& carrierOut = voice.carrierMix();
    carrierOut.resize (static_cast<size_t> (numSamples));
    voice.op (0).renderBlock (carrierOut.data(), op6Buf, numSamples);
}

void metallicPluckAlgorithm (PsyFmEngine& voice, int numSamples)
{
    // op4 (index 3, non-integer ratio for inharmonic content) → modulates op2 (index 1) → op1 (index 0, carrier)
    float* op4Buf = voice.getScratch (3);
    voice.op (3).renderBlock (op4Buf, nullptr, numSamples);

    float* op2Buf = voice.getScratch (1);
    voice.op (1).renderBlock (op2Buf, op4Buf, numSamples);

    auto& carrierOut = voice.carrierMix();
    carrierOut.resize (static_cast<size_t> (numSamples));
    voice.op (0).renderBlock (carrierOut.data(), op2Buf, numSamples);
}

void riserAlgorithm (PsyFmEngine& voice, int numSamples)
{
    // op5 (index 4, ratio-sweep LFO target) → modulates op3 (index 2) → op1 (index 0, carrier)
    float* op5Buf = voice.getScratch (4);
    voice.op (4).renderBlock (op5Buf, nullptr, numSamples);

    float* op3Buf = voice.getScratch (2);
    voice.op (2).renderBlock (op3Buf, op5Buf, numSamples);

    auto& carrierOut = voice.carrierMix();
    carrierOut.resize (static_cast<size_t> (numSamples));
    voice.op (0).renderBlock (carrierOut.data(), op3Buf, numSamples);
}

// ── Slice D (docs/plans/2026-10-05-internal-synth-expansion.md §D) ──
//
// ADDITIVE: indices 4 and 5. The four above are untouched, so their renders
// (and the presets that select them) are bit-identical to before.

void padAlgorithm (PsyFmEngine& voice, int numSamples)
{
    // DUAL carrier: op2 (index 1, optionally detuned for beating) is summed
    // with op1 (index 0) — BOTH are phase-modulated by op5 (index 4), which
    // op4 (index 3) modulates in turn. Two summed carriers give pads/chords
    // their thickness; the single modulator chain keeps them coherent.
    float* op4Buf = voice.getScratch (3);
    voice.op (3).renderBlock (op4Buf, nullptr, numSamples);

    float* op5Buf = voice.getScratch (4);
    voice.op (4).renderBlock (op5Buf, op4Buf, numSamples);

    auto& carrierOut = voice.carrierMix();
    carrierOut.resize (static_cast<size_t> (numSamples));
    // renderBlock OVERWRITES, so the first carrier lands directly and the
    // second is rendered into its own scratch and added (scratch index 2 is
    // free — nothing in this algorithm modulates op3).
    voice.op (0).renderBlock (carrierOut.data(), op5Buf, numSamples);

    float* secondCarrier = voice.getScratch (2);
    voice.op (1).renderBlock (secondCarrier, op5Buf, numSamples);
    for (int i = 0; i < numSamples; ++i)
        carrierOut[static_cast<size_t> (i)] += secondCarrier[i];
}

void bellAlgorithm (PsyFmEngine& voice, int numSamples)
{
    // Deep chain: op6 (index 5) → op5 (index 4) → op3 (index 2) → op1
    // (index 0, carrier). Three serial modulators are what a bell needs: the
    // preset's high base feedback drives the innermost operator toward a
    // noisy strike, and the 3-deep chain turns that into inharmonic partials
    // that decay at different rates (per-operator envelopes).
    float* op6Buf = voice.getScratch (5);
    voice.op (5).renderBlock (op6Buf, nullptr, numSamples);

    float* op5Buf = voice.getScratch (4);
    voice.op (4).renderBlock (op5Buf, op6Buf, numSamples);

    float* op3Buf = voice.getScratch (2);
    voice.op (2).renderBlock (op3Buf, op5Buf, numSamples);

    auto& carrierOut = voice.carrierMix();
    carrierOut.resize (static_cast<size_t> (numSamples));
    voice.op (0).renderBlock (carrierOut.data(), op3Buf, numSamples);
}

} // namespace HDAW
