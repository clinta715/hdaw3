// W10: psy_fm_mod_matrix_debug — read-only mod-matrix debug MCP tool tests.
//
// Plan (hdaw-guard): the tool must (a) register via registerPsyFmTools,
// (b) expose the per-route budget math (Bug 5 scaling on Op6Feedback),
// (c) compute computedParams identical to a manual PsyFmModMatrix::apply()
// over the same snapshot, (d) degrade to the persisted tree routes with
// live:false when no audio processor exists, (e) error on non-psy_fm /
// missing slots. All tool calls are read-only — nothing here mutates the
// project and no audio is rendered.

#include <gtest/gtest.h>
#include "mcp/McpServer.h"
#include "mcp/McpTools_Private.h"
#include "engine/PsyFmModMatrix.h"
#include "engine/PsyFmState.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "model/ProjectModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace HDAW;

namespace
{
// handleRequestOnTestThread returns the BARE result payload (see the bare-
// result contract note in McpServer.cpp / engine_tools_test.cpp): isError and
// content sit at the top level, not under "result".
bool isError (const QJsonValue& r) { return r.toObject().value("isError").toBool(); }

QString textOf (const QJsonValue& r)
{
    return r.toObject().value("content").toArray().at(0).toObject()
        .value("text").toString();
}

QJsonObject resultObj (const QJsonValue& r)
{
    return QJsonDocument::fromJson (textOf (r).toUtf8()).object();
}

// One track with a single psy_fm slot, built through the command layer so the
// tree (param_N, psyFmMatrix) and the live FX chain stay in sync.
struct PsyFmFixture
{
    AudioEngine engine;
    mcp::McpServer server;

    PsyFmFixture()
    {
        engine.initialize();
        engine.getProjectCommands().addTrack ("Test");
        engine.getProjectCommands().addFxSlot (0, "psy_fm", 0, "");
        engine.drainPendingRoutingRebuild();
        server.setEngine (&engine);
        mcp::registerPsyFmTools (server, &engine);
    }

    QJsonValue call (const QJsonObject& args)
    {
        return server.handleRequestOnTestThread (1, "tools/call",
            QJsonObject { { "name", "psy_fm_mod_matrix_debug" }, { "arguments", args } });
    }

    // Push a matrix through the command layer (tree-first, listener applies it
    // to the live engine — same path psy_fm_set_mod_route batches use).
    void setMatrix (const std::string& encoded)
    {
        engine.getAudioEngineCommands().clearFxSlotPsyFmModRoutes (0, 0);
        if (encoded.empty()) return;
        auto routes = PsyFmState::decodeRoutes (encoded);
        for (const auto& r : routes)
        {
            engine.getAudioEngineCommands().setFxSlotPsyFmModRoute (0, 0,
                PsyFmState::sourceName (r.source),
                PsyFmState::destName (r.dest), r.depth);
        }
    }
};
} // namespace

TEST (PsyFmModMatrixDebug, ToolRegisteredByRegisterPsyFmTools)
{
    mcp::McpServer s;
    mcp::registerPsyFmTools (s, nullptr);
    ASSERT_TRUE (s.tools().contains ("psy_fm_mod_matrix_debug"));
    const auto def = s.tools().value ("psy_fm_mod_matrix_debug");
    EXPECT_TRUE (def.inputSchema.value("properties").toObject().contains("trackId"));
    EXPECT_TRUE (def.inputSchema.value("properties").toObject().contains("slotIndex"));
}

TEST (PsyFmModMatrixDebug, FeedbackBudgetScalesWhenDepthExceedsOne)
{
    PsyFmFixture fx;
    // The gate scenario: two feedback routes, |depth| 0.6 each -> total 1.2.
    fx.setMatrix ("modWheel:op6Feedback:0.6;velocity:op6Feedback:0.6");

    auto r = fx.call (QJsonObject { { "trackId", 0 }, { "slotIndex", 0 } });
    ASSERT_FALSE (isError (r)) << textOf (r).toStdString();
    const auto o = resultObj (r);
    EXPECT_TRUE (o.value("live").toBool());

    const auto budget = o.value("feedbackBudget").toObject();
    EXPECT_NEAR (budget.value("totalDepth").toDouble(), 1.2, 1e-5);
    EXPECT_NEAR (budget.value("scaling").toDouble(), 1.0 / 1.2, 1e-4); // 0.8333
    EXPECT_TRUE (budget.value("budgetHit").toBool());

    const auto routes = o.value("routes").toArray();
    ASSERT_EQ (routes.size(), 2);
    for (const auto& rv : routes)
    {
        const auto ro = rv.toObject();
        EXPECT_EQ (ro.value("dest").toString(), QString("Op6Feedback"));
        EXPECT_TRUE (ro.value("budgetScaled").toBool());
        EXPECT_NEAR (ro.value("sourceValue").toDouble(), 0.0, 1e-5); // wheel/vel default 0
        // raw = 0 * 0.6 = 0; scaled = raw * (1/1.2) = 0 — the scaledContribution
        // still proves the scaling factor was applied (raw * 0.8333).
        EXPECT_NEAR (ro.value("scaledContribution").toDouble(),
                     ro.value("rawContribution").toDouble() * (1.0 / 1.2), 1e-6);
    }

    // computed feedback must equal a manual apply() over the same state.
    const auto baseFb = o.value("baseParams").toObject().value("feedback").toDouble();
    const auto compFb = o.value("computedParams").toObject().value("feedback").toDouble();

    PsyFmModMatrix manual;
    manual.addRoute ({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op6Feedback, 0.6f });
    manual.addRoute ({ PsyFmModRoute::Source::Velocity, PsyFmModRoute::Dest::Op6Feedback, 0.6f });
    PsyFmModSourcePool pool; // default sources (0.0 wheel/velocity, phase 0 LFOs)
    float baseRatios[6] = { 1, 1, 1, 1, 1, 1 };
    float outRatios[6];
    float outFeedback = 0.0f;
    manual.apply (pool, baseRatios, static_cast<float> (baseFb), outRatios, outFeedback);
    EXPECT_NEAR (compFb, static_cast<double> (outFeedback), 1e-5); // 0.5 base + 0 (sources 0)
}

TEST (PsyFmModMatrixDebug, ComputedParamsMatchManualApplyWithDrivenSources)
{
    PsyFmFixture fx;
    fx.setMatrix ("modWheel:op6Feedback:0.3;modWheel:op1Ratio:0.5;velocity:op6Ratio:1.0");

    auto r = fx.call (QJsonObject { { "trackId", 0 }, { "slotIndex", 0 } });
    ASSERT_FALSE (isError (r)) << textOf (r).toStdString();
    const auto o = resultObj (r);

    const auto srcVals = o.value("sourceValues").toObject();
    const auto base = o.value("baseParams").toObject();
    const auto comp = o.value("computedParams").toObject();

    // Manual apply() with the reported source values must reproduce computedParams.
    PsyFmModSourcePool pool;
    pool.modWheelValue = static_cast<float> (srcVals.value("modWheel").toDouble());
    pool.velocityValue = static_cast<float> (srcVals.value("velocity").toDouble());
    // LFO phases default to 0 in a fresh pool; getSourceValue(0/1) at phase 0
    // is sin(0)*0.5 + 0.5 = 0.5, so a fresh engine reports 0.5 for both LFOs.
    EXPECT_NEAR (srcVals.value("ratioSweepLFO").toDouble(), 0.5, 1e-5);
    EXPECT_NEAR (srcVals.value("feedbackLFO").toDouble(), 0.5, 1e-5);

    float baseRatios[6];
    for (int i = 0; i < 6; ++i)
        baseRatios[i] = static_cast<float> (base.value("ratios").toArray().at(i).toDouble());
    const float baseFeedback = static_cast<float> (base.value("feedback").toDouble());

    PsyFmModMatrix manual;
    manual.addRoute ({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op6Feedback, 0.3f });
    manual.addRoute ({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op1Ratio, 0.5f });
    manual.addRoute ({ PsyFmModRoute::Source::Velocity, PsyFmModRoute::Dest::Op6Ratio, 1.0f });

    float outRatios[6];
    float outFeedback = 0.0f;
    manual.apply (pool, baseRatios, baseFeedback, outRatios, outFeedback);

    for (int i = 0; i < 6; ++i)
        EXPECT_NEAR (comp.value("ratios").toArray().at(i).toDouble(),
                     static_cast<double> (outRatios[i]), 1e-5) << "op " << i;
    EXPECT_NEAR (comp.value("feedback").toDouble(),
                 static_cast<double> (outFeedback), 1e-5);
}

TEST (PsyFmModMatrixDebug, EmptyMatrixReturnsBaseParams)
{
    PsyFmFixture fx;
    fx.setMatrix ("");

    auto r = fx.call (QJsonObject { { "trackId", 0 }, { "slotIndex", 0 } });
    ASSERT_FALSE (isError (r)) << textOf (r).toStdString();
    const auto o = resultObj (r);

    EXPECT_EQ (o.value("routes").toArray().size(), 0);
    const auto budget = o.value("feedbackBudget").toObject();
    EXPECT_NEAR (budget.value("totalDepth").toDouble(), 0.0, 1e-6);
    EXPECT_NEAR (budget.value("scaling").toDouble(), 1.0, 1e-6);
    EXPECT_FALSE (budget.value("budgetHit").toBool());

    // No routes -> computed == base for every ratio and the feedback.
    const auto base = o.value("baseParams").toObject();
    const auto comp = o.value("computedParams").toObject();
    for (int i = 0; i < 6; ++i)
        EXPECT_NEAR (comp.value("ratios").toArray().at(i).toDouble(),
                     base.value("ratios").toArray().at(i).toDouble(), 1e-6);
    EXPECT_NEAR (comp.value("feedback").toDouble(),
                 base.value("feedback").toDouble(), 1e-6);
}

TEST (PsyFmModMatrixDebug, NonPsyFmSlotReturnsError)
{
    AudioEngine engine;
    engine.initialize();
    engine.getProjectCommands().addTrack ("Test");
    engine.getProjectCommands().addFxSlot (0, "reverb", 0, "");
    engine.drainPendingRoutingRebuild();

    mcp::McpServer s;
    s.setEngine (&engine);
    mcp::registerPsyFmTools (s, &engine);

    auto r = s.handleRequestOnTestThread (1, "tools/call",
        QJsonObject { { "name", "psy_fm_mod_matrix_debug" },
                      { "arguments", QJsonObject { { "trackId", 0 }, { "slotIndex", 0 } } } });
    EXPECT_TRUE (isError (r));
    EXPECT_TRUE (textOf (r).contains ("not a psy_fm"));
}

TEST (PsyFmModMatrixDebug, OutOfRangeSlotReturnsError)
{
    AudioEngine engine;
    engine.initialize();
    engine.getProjectCommands().addTrack ("Test");
    engine.drainPendingRoutingRebuild();

    mcp::McpServer s;
    s.setEngine (&engine);
    mcp::registerPsyFmTools (s, &engine);

    auto r = s.handleRequestOnTestThread (1, "tools/call",
        QJsonObject { { "name", "psy_fm_mod_matrix_debug" },
                      { "arguments", QJsonObject { { "trackId", 0 }, { "slotIndex", 7 } } } });
    EXPECT_TRUE (isError (r));
    EXPECT_TRUE (textOf (r).contains ("slot not found"));
}

TEST (PsyFmModMatrixDebug, TreeFallbackWhenNoLiveProcessor)
{
    // Tree fallback: closing the audio device makes MainAudioProcessor::
    // releaseResources() null the routingManager, so proc->getTrack(0) is
    // null (the device-less CI/RDP situation, lesson 17) and the tool must
    // degrade to the persisted psyFmMatrix tree property. Commands/readModel
    // still exist because initialize() ran.
    AudioEngine engine;
    engine.initialize();
    engine.getDeviceManager().closeAudioDevice();
    engine.getProjectCommands().addTrack ("Test");
    engine.getProjectCommands().addFxSlot (0, "psy_fm", 0, "");
    engine.getAudioEngineCommands().setFxSlotPsyFmModRoute (0, 0, "feedbackLFO", "op6Feedback", 0.4f);
    engine.getAudioEngineCommands().setFxSlotPsyFmModRoute (0, 0, "modWheel", "op1Ratio", 0.5f);

    mcp::McpServer s;
    s.setEngine (&engine);
    mcp::registerPsyFmTools (s, &engine);

    auto r = s.handleRequestOnTestThread (1, "tools/call",
        QJsonObject { { "name", "psy_fm_mod_matrix_debug" },
                      { "arguments", QJsonObject { { "trackId", 0 }, { "slotIndex", 0 } } } });
    ASSERT_FALSE (isError (r)) << textOf (r).toStdString();
    const auto o = resultObj (r);
    EXPECT_FALSE (o.value("live").toBool());
    const auto routes = o.value("routes").toArray();
    ASSERT_EQ (routes.size(), 2);
    EXPECT_EQ (routes.at(0).toObject().value("source").toString(), QString("FeedbackLFO"));
    EXPECT_EQ (routes.at(0).toObject().value("dest").toString(), QString("Op6Feedback"));
    EXPECT_NEAR (routes.at(0).toObject().value("depth").toDouble(), 0.4, 1e-5);
    EXPECT_EQ (routes.at(1).toObject().value("source").toString(), QString("ModWheel"));
}
