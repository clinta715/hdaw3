#pragma once
// MarkovArranger — the orchestrator for the incremental 2-bar Markov score
// generation (extracted from PsytranceMarkovGenerator). Owns the active-set
// bookkeeping, validation, the slow-tier section-energy schedule, the Markov
// action selection and the final clip assembly, and drives the three style-
// parameterized components: PercussionEngine (themes/grids/rotation),
// HarmonyEngine (key/progressions/pitched emission) and TextureEngine
// (riser/down + filter sweeps). Pure deterministic function of
// (params, seed) — no engine/model dependency.

#include "engine/PsytranceMarkovGenerator.h"

namespace HDAW {

struct MarkovArranger {
    static PsytranceMarkovScore run(const PsytranceMarkovParams& params);
};

} // namespace HDAW
