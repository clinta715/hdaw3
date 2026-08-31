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

} // namespace PsyFmPatches
} // namespace HDAW
