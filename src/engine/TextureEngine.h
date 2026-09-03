#pragma once
// TextureEngine — composed FX texture accents (riser / downlifter, pitched
// from the current key) and FilterSweep automation-point emission, extracted
// from PsytranceMarkovGenerator. P3's future home (long-form textures, dub
// bursts). Pure deterministic component — no engine/model dependency. Style
// lives in TextureStyle (the genre style-pack seam). Note: filterCutoff
// automations remain ADVISORY data on the score — the command layer writes
// only "volume" entries to real lanes.

#include "engine/MarkovRoles.h"
#include "engine/PsytranceMarkovGenerator.h"

#include <string>

namespace HDAW {

struct TextureStyle {
    int riserSteps = 8;               // 8th-note steps into the drop
    double riserStepBeats = 0.5;
    int riserVelBase = 60, riserVelStep = 7;
    double riserNoteDur = 0.45;
    int downVelocity = 95;            // downlifter: single 8-beat note
    double downDurBeats = 8.0;
    double filterSweepValMin = 0.2, filterSweepValSpan = 0.6;
    double filterSweepDurBeats = 8.0;
};

class TextureEngine {
public:
    // FX accent emission (FxHit windows + KeyChange transition FX) — pitched
    // from the CURRENT key; checks the RoleCtx track mapping itself.
    static void writeFxAccents(const std::string& fxRole, int bar, int keyRoot, int scaleMode,
                               RoleCtx& riser, RoleCtx& down, const TextureStyle& style,
                               int maxNotes);

    // Advisory filterCutoff automation point; value01 is drawn by the caller
    // (single seeded draw in the arranger's fixed slot).
    static PsytranceAutomationPoint filterSweepPoint(const std::string& role, int bar,
                                                     double value01, const TextureStyle& style);
};

} // namespace HDAW
