#include "PsyFmModMatrixView.h"

#include "../model/ProjectModel.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/PsyFmEngine.h"
#include "../engine/PsyFmModMatrix.h"
#include "../engine/PsyFmState.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <vector>

namespace HDAW {

QJsonObject buildPsyFmModMatrixView (ProjectModel& model,
                                     MainAudioProcessor* processor,
                                     int trackIndex,
                                     int slotIndex)
{
    using SR = PsyFmModRoute::Source;
    using DR = PsyFmModRoute::Dest;

    // Source-name mapping: the matrix engine indexes the pool directly
    // (0 ratioSweepLFO, 1 feedbackLFO, 2 modWheel, 3 velocity); BarClock
    // exists only as a track-level mod target and is not a pool source.
    auto sourceNameUpper = [](SR s) -> const char* {
        switch (s) {
            case SR::RatioSweepLFO: return "RatioSweepLFO";
            case SR::FeedbackLFO:   return "FeedbackLFO";
            case SR::ModWheel:      return "ModWheel";
            case SR::Velocity:      return "Velocity";
            case SR::BarClock:      return "BarClock";
        }
        return "RatioSweepLFO";
    };
    auto destNameUpper = [](DR d) -> const char* {
        switch (d) {
            case DR::Op1Ratio:  return "Op1Ratio";
            case DR::Op2Ratio:  return "Op2Ratio";
            case DR::Op3Ratio:  return "Op3Ratio";
            case DR::Op4Ratio:  return "Op4Ratio";
            case DR::Op5Ratio:  return "Op5Ratio";
            case DR::Op6Ratio:  return "Op6Ratio";
            case DR::Op6Feedback: return "Op6Feedback";
            case DR::RatioSweepRateItself: return "RatioSweepRateItself";
        }
        return "Op1Ratio";
    };
    // Mirrors PsyFmModMatrix::sourceIndexFor (BarClock is not a pool source).
    auto poolIndexFor = [](SR s) -> int {
        switch (s) {
            case SR::RatioSweepLFO: return 0;
            case SR::FeedbackLFO:   return 1;
            case SR::ModWheel:      return 2;
            case SR::Velocity:      return 3;
            default: return -1;
        }
    };

    // Snapshot: base params from the slot tree (read-only), routes + source pool
    // from the live engine under matrixLock_ when available.
    auto slotTree = model.getTrackListTree().getChild(trackIndex)
        .getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
    if (!slotTree.isValid())
        return {};

    float baseRatios[6] = { 1, 1, 1, 1, 1, 1 };
    float baseFeedback = 0.0f;
    for (int i = 0; i < 6; ++i)
        baseRatios[i] = static_cast<float>(static_cast<double>(
            slotTree.getProperty(juce::Identifier("param_" + juce::String(i)),
                                 baseRatios[i])));
    baseFeedback = static_cast<float>(static_cast<double>(
        slotTree.getProperty(juce::Identifier("param_6"), baseFeedback)));

    PsyFmModSourcePool pool;      // defaults (phase 0, wheel/vel 0)
    std::vector<PsyFmModRoute> routes;
    bool live = false;
    if (processor != nullptr)
    {
        if (auto* track = processor->getTrack(trackIndex))
        {
            auto& chain = track->getFXChain();
            if (slotIndex < static_cast<int>(chain.size()) && chain[slotIndex])
            {
                if (auto* psyFm = chain[slotIndex]->psyFmEngine())
                {
                    psyFm->snapshotModState(routes, baseRatios, baseFeedback, pool);
                    live = true;
                }
            }
        }
    }
    if (!live)
    {
        // No audio device / processor: fall back to the persisted tree
        // routes so the patch designer still gets a usable view.
        juce::String matrixStr = slotTree.getProperty("psyFmMatrix", "").toString();
        routes = PsyFmState::decodeRoutes(matrixStr.toStdString());
        pool.ratioSweepLFORateHz = static_cast<float>(static_cast<double>(
            slotTree.getProperty("psyFmSweepRate", (double)pool.ratioSweepLFORateHz)));
    }

    // Budget: per-destination depth budget on Op6Feedback (Bug 5 fix in
    // PsyFmModMatrix::apply — ratio destinations are additive, unbudgeted).
    float feedbackTotalDepth = 0.0f;
    for (const auto& r : routes)
        if (r.dest == DR::Op6Feedback)
            feedbackTotalDepth += std::abs(r.depth);
    const float feedbackScale = (feedbackTotalDepth > 1.0f)
        ? 1.0f / feedbackTotalDepth : 1.0f;

    QJsonArray routeArr;
    for (const auto& r : routes)
    {
        QJsonObject ro;
        ro["source"] = sourceNameUpper(r.source);
        ro["dest"]   = destNameUpper(r.dest);
        ro["depth"]  = (double) r.depth;
        const float srcVal = (poolIndexFor(r.source) >= 0)
            ? pool.getSourceValue(poolIndexFor(r.source)) : 0.0f;
        const float raw = srcVal * r.depth;
        ro["sourceValue"] = (double) srcVal;
        ro["rawContribution"] = (double) raw;
        const bool scaled = (r.dest == DR::Op6Feedback && feedbackTotalDepth > 1.0f);
        ro["budgetScaled"] = scaled;
        ro["scaledContribution"] = (double)(scaled ? raw * feedbackScale : raw);
        routeArr.append(ro);
    }

    // Simulate apply() on the snapshot copies — identical math to the
    // engine's block-rate matrix pass, without rendering audio.
    float outRatios[6];
    float outFeedback = 0.0f;
    PsyFmModMatrix sim;
    for (const auto& r : routes)
        sim.addRoute(r);
    sim.apply(pool, baseRatios, baseFeedback, outRatios, outFeedback);

    QJsonArray baseArr, compArr;
    for (int i = 0; i < 6; ++i)
    {
        baseArr.append((double) baseRatios[i]);
        compArr.append((double) outRatios[i]);
    }

    QJsonObject out;
    out["live"] = live;
    out["routes"] = routeArr;

    QJsonObject budget;
    budget["totalDepth"] = (double) feedbackTotalDepth;
    budget["scaling"]    = (double) feedbackScale;
    budget["budgetHit"]  = feedbackTotalDepth > 1.0f;
    out["feedbackBudget"] = budget;

    QJsonObject baseParams;
    baseParams["ratios"] = baseArr;
    baseParams["feedback"] = (double) baseFeedback;
    out["baseParams"] = baseParams;

    QJsonObject compParams;
    compParams["ratios"] = compArr;
    compParams["feedback"] = (double) outFeedback;
    out["computedParams"] = compParams;

    QJsonObject srcVals;
    srcVals["ratioSweepLFO"] = (double) pool.getSourceValue(0);
    srcVals["feedbackLFO"]   = (double) pool.getSourceValue(1);
    srcVals["modWheel"]      = (double) pool.modWheelValue;
    srcVals["velocity"]      = (double) pool.velocityValue;
    out["sourceValues"] = srcVals;

    return out;
}

} // namespace HDAW
