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

} // namespace HDAW
