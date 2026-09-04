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

// The "closest correct number of patterns" for a section type: the resolved
// layer bounds + target the explicit section script uses when the caller
// does not override them (psytrance production canon).
struct MarkovSectionBudget {
    int minTracks = 1;
    int maxTracks = 3;
    int minPercTracks = 1;
    int maxPercTracks = 2;
    int targetCount = 2;
};

struct MarkovArranger {
    static PsytranceMarkovScore run(const PsytranceMarkovParams& params);
    // Default budget for a section type. Aliases accepted (intro/outro ->
    // sparse, drop/climax -> peak); unknown types fall back to sparse.
    static MarkovSectionBudget sectionDefaults(const std::string& type);
};

} // namespace HDAW
