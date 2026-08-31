#pragma once
#include <vector>

namespace HDAW {

/// Modulation source pool — LFOs and performance sources feeding the FM matrix.
/// Per-operator envelopes are NOT here; each PsyFmOperator owns its own envelope.
struct PsyFmModSourcePool
{
    float ratioSweepLFORateHz = 0.2f;
    float feedbackLFORateHz   = 0.1f;
    float modWheelValue = 0.0f;
    float velocityValue = 0.0f;
    float feedbackOffset = 0.0f;  // additive feedback offset (track-level LFO target 306)

    void advanceControlRate (int numSamples, double sampleRate);
    float getSourceValue (int sourceIndex) const;

private:
    double ratioSweepPhase_ = 0.0;
    double feedbackPhase_   = 0.0;
};

/// A single modulation route: source → destination with depth.
struct PsyFmModRoute
{
    enum class Source { RatioSweepLFO, FeedbackLFO, ModWheel, Velocity, BarClock };
    enum class Dest   { Op1Ratio, Op2Ratio, Op3Ratio, Op4Ratio, Op5Ratio, Op6Ratio,
                         Op6Feedback, RatioSweepRateItself };

    Source source;
    Dest   dest;
    float  depth = 0.0f;
};

/// Modulation matrix — routes sources to block-rate FM destinations.
/// Output level / index is NOT a matrix destination; it lives inside PsyFmOperator.
class PsyFmModMatrix
{
public:
    void addRoute (PsyFmModRoute route);
    void clearRoutes();

    /// Apply all routes to base params, writing into outRatios/outFeedback.
    void apply (const PsyFmModSourcePool& sources,
                const float baseRatios[6], float baseFeedback,
                float outRatios[6], float& outFeedback);

    const std::vector<PsyFmModRoute>& getRoutes() const { return routes_; }

private:
    int sourceIndexFor (PsyFmModRoute::Source s) const;
    std::vector<PsyFmModRoute> routes_;
};

} // namespace HDAW
