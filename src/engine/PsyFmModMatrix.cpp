#include "PsyFmModMatrix.h"
#include <cmath>
#include <algorithm>

namespace HDAW {

// ── PsyFmModSourcePool ──

void PsyFmModSourcePool::advanceControlRate (int numSamples, double sampleRate)
{
    ratioSweepPhase_ += (ratioSweepLFORateHz * numSamples) / sampleRate;
    feedbackPhase_   += (feedbackLFORateHz   * numSamples) / sampleRate;
    if (ratioSweepPhase_ >= 1.0) ratioSweepPhase_ -= 1.0;
    if (feedbackPhase_   >= 1.0) feedbackPhase_   -= 1.0;
}

float PsyFmModSourcePool::getSourceValue (int sourceIndex) const
{
    switch (sourceIndex)
    {
        case 0: return std::sin (2.0 * 3.14159265358979323846 * ratioSweepPhase_) * 0.5f + 0.5f;
        case 1: return std::sin (2.0 * 3.14159265358979323846 * feedbackPhase_) * 0.5f + 0.5f;
        case 2: return modWheelValue;
        case 3: return velocityValue;
        default: return 0.0f;
    }
}

// ── PsyFmModMatrix ──

void PsyFmModMatrix::addRoute (PsyFmModRoute route)
{
    routes_.push_back (route);
}

void PsyFmModMatrix::clearRoutes()
{
    routes_.clear();
}

int PsyFmModMatrix::sourceIndexFor (PsyFmModRoute::Source s) const
{
    switch (s)
    {
        case PsyFmModRoute::Source::RatioSweepLFO: return 0;
        case PsyFmModRoute::Source::FeedbackLFO:   return 1;
        case PsyFmModRoute::Source::ModWheel:      return 2;
        case PsyFmModRoute::Source::Velocity:      return 3;
        default: return 0;
    }
}

void PsyFmModMatrix::apply (const PsyFmModSourcePool& sources,
                             const float baseRatios[6], float baseFeedback,
                             float outRatios[6], float& outFeedback)
{
    // Reset to base values (feedback includes the track-level LFO offset)
    for (int i = 0; i < 6; ++i)
        outRatios[i] = baseRatios[i];
    outFeedback = std::clamp (baseFeedback + sources.feedbackOffset, 0.0f, 1.0f);

    // Bug 5 fix: per-destination depth budget. When multiple routes target the
    // same destination (e.g. two LFOs on Op6Feedback), their |depth| values
    // are summed. If the total exceeds 1.0, all contributions to that
    // destination are scaled proportionally so the effective modulation stays
    // within the 0..1 budget. This prevents runaway PM feedback energy.
    // Ratio destinations are additive by design (no clamp), so they are not
    // budgeted — only clamped destinations need the protection.
    float feedbackTotalDepth = 0.0f;
    for (const auto& route : routes_)
        if (route.dest == PsyFmModRoute::Dest::Op6Feedback)
            feedbackTotalDepth += std::abs (route.depth);
    const float feedbackScale = (feedbackTotalDepth > 1.0f)
        ? 1.0f / feedbackTotalDepth : 1.0f;

    for (const auto& route : routes_)
    {
        float srcVal = sources.getSourceValue (sourceIndexFor (route.source));
        float amount = srcVal * route.depth;

        switch (route.dest)
        {
            case PsyFmModRoute::Dest::Op1Ratio: outRatios[0] += amount; break;
            case PsyFmModRoute::Dest::Op2Ratio: outRatios[1] += amount; break;
            case PsyFmModRoute::Dest::Op3Ratio: outRatios[2] += amount; break;
            case PsyFmModRoute::Dest::Op4Ratio: outRatios[3] += amount; break;
            case PsyFmModRoute::Dest::Op5Ratio: outRatios[4] += amount; break;
            case PsyFmModRoute::Dest::Op6Ratio: outRatios[5] += amount; break;
            case PsyFmModRoute::Dest::Op6Feedback:
                outFeedback = std::clamp (outFeedback + amount * feedbackScale, 0.0f, 1.0f);
                break;
            case PsyFmModRoute::Dest::RatioSweepRateItself:
                // Handled externally by PsyFmEngine::onBarBoundary before apply() runs
                break;
        }
    }
}

} // namespace HDAW
