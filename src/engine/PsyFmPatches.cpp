#include "PsyFmPatches.h"

namespace HDAW {
namespace PsyFmPatches {

PsyFmModMatrix makeGrowlBassMatrix()
{
    PsyFmModMatrix m;
    // Feedback LFO modulates Op6 feedback — slow undulation on aggression
    m.addRoute ({ PsyFmModRoute::Source::FeedbackLFO,
                  PsyFmModRoute::Dest::Op6Feedback, 0.4f });
    return m;
}

PsyFmModMatrix makeRiserMatrix()
{
    PsyFmModMatrix m;
    // Bar clock speeds up the ratio-sweep LFO rate itself (nested modulation)
    m.addRoute ({ PsyFmModRoute::Source::BarClock,
                  PsyFmModRoute::Dest::RatioSweepRateItself, 1.0f });
    // Ratio-sweep LFO modulates Op4 ratio — creates vibrato → scream sweep
    m.addRoute ({ PsyFmModRoute::Source::RatioSweepLFO,
                  PsyFmModRoute::Dest::Op4Ratio, 0.3f });
    return m;
}

PsyFmModMatrix makeAcidLeadMatrix()
{
    PsyFmModMatrix m;
    // Mod wheel directly drives Op6 feedback toward self-oscillation
    // — filter-sweep-style performance control without a filter
    m.addRoute ({ PsyFmModRoute::Source::ModWheel,
                  PsyFmModRoute::Dest::Op6Feedback, 0.9f });
    return m;
}

PsyFmModMatrix makeMetallicPluckMatrix()
{
    PsyFmModMatrix m;
    // Feedback LFO adds slight movement to Op6 feedback
    // (main pluck character comes from the fast-decay operator envelope,
    //  which is set directly on the PsyFmOperator, not via the matrix)
    m.addRoute ({ PsyFmModRoute::Source::FeedbackLFO,
                  PsyFmModRoute::Dest::Op6Feedback, 0.15f });
    return m;
}

// ── Slice D (2026-10-06) role presets — see PsyFmPatches.h ──

PsyFmModMatrix makePadMatrix()
{
    PsyFmModMatrix m;
    // A slow ratio-sweep drift on op4 (index 3) — the innermost modulator of
    // padAlgorithm — gives the pad its evolving timbre without disturbing the
    // two summed carriers.
    m.addRoute ({ PsyFmModRoute::Source::RatioSweepLFO,
                  PsyFmModRoute::Dest::Op4Ratio, 0.05f });
    return m;
}

PsyFmModMatrix makeBellMatrix()
{
    PsyFmModMatrix m;
    // The feedback LFO undulates op6 feedback: the strike's noise content
    // breathes instead of ringing metronomically.
    m.addRoute ({ PsyFmModRoute::Source::FeedbackLFO,
                  PsyFmModRoute::Dest::Op6Feedback, 0.3f });
    return m;
}

PsyFmModMatrix makePluckMatrix()
{
    PsyFmModMatrix m;
    // Light feedback movement; the pluck's character is the fast, bright
    // high-ratio envelope chain (indices 1/3 at 5x/9x), not the matrix.
    m.addRoute ({ PsyFmModRoute::Source::FeedbackLFO,
                  PsyFmModRoute::Dest::Op6Feedback, 0.1f });
    return m;
}

PsyFmModMatrix makeDroneMatrix()
{
    PsyFmModMatrix m;
    // Slow ratio-sweep drift on the modulator, plus a mod-wheel ratio offset
    // so a held drone can be played.
    m.addRoute ({ PsyFmModRoute::Source::RatioSweepLFO,
                  PsyFmModRoute::Dest::Op4Ratio, 0.25f });
    m.addRoute ({ PsyFmModRoute::Source::ModWheel,
                  PsyFmModRoute::Dest::Op1Ratio, 0.1f });
    return m;
}

PsyFmModMatrix makeStabMatrix()
{
    PsyFmModMatrix m;
    // Mod wheel drives op6 feedback toward the percussive ceiling (base 0.75),
    // the same "performance control without a filter" idiom as acidLead.
    m.addRoute ({ PsyFmModRoute::Source::ModWheel,
                  PsyFmModRoute::Dest::Op6Feedback, 0.9f });
    return m;
}

} // namespace PsyFmPatches
} // namespace HDAW
