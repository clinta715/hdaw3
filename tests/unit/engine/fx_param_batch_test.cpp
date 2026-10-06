// Batched param writes — engine-layer foundation (slice 1 of
// docs/plans/2026-10-05-param-batch-and-bugfixes.md).
//
// The contract under test is the COMMAND layer's, on the engine directly (no
// MCP/RPC surface yet — those land in slice 2):
//   * setFxParams writes N params in ONE undo unit: a single undo restores
//     EVERY value (a loop of single-item calls could not pass this);
//   * setFxParams PARTIAL-APPLIES (the set_cells precedent, deliberately NOT
//     setNotesGain's validate-then-apply): a batch {valid, unknown name, valid}
//     writes the two valid writes, reports failed=1, and surfaces the per-write
//     error in the parallel `errors` vector;
//   * the lesson-23 write-side clamp survives the batch: an out-of-range real
//     value is clamped (writtenValue == the def max) and the tree carries the
//     clamped value, not the poison one;
//   * setBusFxParams / setLfoParams share the one-transaction + partial-apply
//     shape, and the LFO batch REFUSES an unknown param name (the single
//     setLfoParam silently no-ops on one — lesson 38);
//   * the shared request parser (common/FxParamBatchJson.h) refuses an unknown
//     write key and a non-integral index with the validator's wording, and
//     resolves a stable `trackID` to the positional `trackIndex`.

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <juce_core/juce_core.h>

#include "common/FxParamBatchJson.h"
#include "common/BusInfo.h"
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class FxParamBatch : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
    }

    // Internal FX def indices used below.
    //   eq      : 0=Frequency Hz [20,20000], 1=Q [0.1,10], 2=Gain dB [-24,24]
    //   filter  : 0=Cutoff Hz [20,20000], 1=Mode [0,2], 2=Resonance [0.1,10]
    int addEqTrack(const char* name = "Track") {
        auto& cmds = engine->getProjectCommands();
        const int t = cmds.addTrack(name);
        engine->drainPendingRoutingRebuild();
        cmds.addFxSlot(t, "eq", 0, "");
        engine->drainPendingRoutingRebuild();
        return t;
    }

    juce::ValueTree slotTree(int trackIndex, int slotIndex) {
        return engine->getProjectModel().getTrackListTree()
            .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
    }

    double slotParam(int trackIndex, int slotIndex, int paramIndex) {
        return static_cast<double>(slotTree(trackIndex, slotIndex).getProperty(
            juce::Identifier(("param_" + std::to_string(paramIndex)).c_str()), -999.0));
    }

    int undoDepth() {
        return static_cast<int>(engine->getProjectCommands().getUndoDescriptions().size());
    }

    std::unique_ptr<AudioEngine> engine;
};

// (a) THREE writes on ONE slot in ONE undo unit, then a SINGLE undo restores
// all three — the direct proof of batching (a loop of singles fails this).
TEST_F(FxParamBatch, SetFxParamsIsOneUndoUnitAndRestoresEveryValue) {
    auto& cmds = engine->getProjectCommands();
    const int t = addEqTrack();

    const double before0 = slotParam(t, 0, 0);
    const double before1 = slotParam(t, 0, 1);
    const double before2 = slotParam(t, 0, 2);
    const int depth = undoDepth();

    std::vector<ProjectCommands::FxParamWrite> writes(3);
    writes[0].trackIndex = t; writes[0].slotIndex = 0;
    writes[0].paramIndex = 0; writes[0].hasParamIndex = true; writes[0].value = 440.0;   // Frequency
    writes[1].trackIndex = t; writes[1].slotIndex = 0;
    writes[1].paramIndex = 1; writes[1].hasParamIndex = true; writes[1].value = 3.5;     // Q
    writes[2].trackIndex = t; writes[2].slotIndex = 0;
    writes[2].paramIndex = 2; writes[2].hasParamIndex = true; writes[2].value = -8.0;    // Gain dB

    std::vector<std::string> errors;
    const auto res = cmds.setFxParams(writes, &errors);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.applied, 3);
    ASSERT_EQ(errors.size(), 3u);
    EXPECT_TRUE(errors[0].empty());
    EXPECT_TRUE(errors[1].empty());
    EXPECT_TRUE(errors[2].empty());

    EXPECT_DOUBLE_EQ(slotParam(t, 0, 0), 440.0);
    EXPECT_DOUBLE_EQ(slotParam(t, 0, 1), 3.5);
    EXPECT_DOUBLE_EQ(slotParam(t, 0, 2), -8.0);
    EXPECT_EQ(undoDepth(), depth + 1) << "the batch must be exactly one transaction";

    cmds.undo();   // ONE undo
    EXPECT_DOUBLE_EQ(slotParam(t, 0, 0), before0);
    EXPECT_DOUBLE_EQ(slotParam(t, 0, 1), before1);
    EXPECT_DOUBLE_EQ(slotParam(t, 0, 2), before2);
}

// (b) PARTIAL-APPLY: a bad write in the middle does not drop the good ones, and
// its failure is reported in the parallel `errors` vector at the same index.
TEST_F(FxParamBatch, SetFxParamsPartialApplyReportsPerWriteErrors) {
    auto& cmds = engine->getProjectCommands();
    const int t = addEqTrack();

    std::vector<ProjectCommands::FxParamWrite> writes(3);
    writes[0].trackIndex = t; writes[0].slotIndex = 0;
    writes[0].paramIndex = 0; writes[0].hasParamIndex = true; writes[0].value = 800.0;
    writes[1].trackIndex = t; writes[1].slotIndex = 0;
    writes[1].paramName = "NoSuchParam"; writes[1].value = 1.0;   // unknown name
    writes[2].trackIndex = t; writes[2].slotIndex = 0;
    writes[2].paramIndex = 2; writes[2].hasParamIndex = true; writes[2].value = 5.0;

    std::vector<std::string> errors;
    const auto res = cmds.setFxParams(writes, &errors);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.applied, 2) << "the two valid writes must land";
    ASSERT_EQ(errors.size(), 3u);
    EXPECT_TRUE(errors[0].empty());
    EXPECT_FALSE(errors[1].empty());
    EXPECT_TRUE(errors[1].find("unknown paramName") != std::string::npos) << errors[1];
    EXPECT_TRUE(errors[2].empty());

    EXPECT_DOUBLE_EQ(slotParam(t, 0, 0), 800.0);
    EXPECT_DOUBLE_EQ(slotParam(t, 0, 2), 5.0);
}

// (b') Every write bad -> ok=false, applied=0, and `.error` carries the first
// failure (never a bare ok:false), with the per-write rows still populated.
TEST_F(FxParamBatch, SetFxParamsAllBadReturnsFirstError) {
    auto& cmds = engine->getProjectCommands();
    const int t = addEqTrack();

    std::vector<ProjectCommands::FxParamWrite> writes(2);
    writes[0].trackIndex = t; writes[0].slotIndex = 99;   // no such slot
    writes[0].paramIndex = 0; writes[0].hasParamIndex = true; writes[0].value = 1.0;
    writes[1].trackIndex = 42; writes[1].slotIndex = 0;   // no such track
    writes[1].paramIndex = 0; writes[1].hasParamIndex = true; writes[1].value = 1.0;

    std::vector<std::string> errors;
    const auto res = cmds.setFxParams(writes, &errors);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.applied, 0);
    EXPECT_FALSE(res.error.empty());
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0], "slot not found");
    EXPECT_EQ(errors[1], "track not found");
}

// (c) The lesson-23 clamp survives the batch: an out-of-range REAL value is
// clamped to the def max, the writtenValue reports the clamped bound, and the
// tree carries the clamped value (never the poison one).
TEST_F(FxParamBatch, SetFxParamsKeepsLesson23Clamp) {
    auto& cmds = engine->getProjectCommands();
    const int t = addEqTrack();

    // Gain def max is 24 dB; a normalized 1.0 write denormalizes to the max.
    std::vector<ProjectCommands::FxParamWrite> writes(2);
    writes[0].trackIndex = t; writes[0].slotIndex = 0;
    writes[0].paramIndex = 2; writes[0].hasParamIndex = true; writes[0].value = 900.0;  // real
    writes[1].trackIndex = t; writes[1].slotIndex = 0;
    writes[1].paramIndex = 2; writes[1].hasParamIndex = true; writes[1].value = 1.0;
    writes[1].normalized = true;   // 0..1 -> exactly the def max

    const auto r0 = cmds.writeFxParam(writes[0]);
    EXPECT_TRUE(r0.ok);
    EXPECT_FALSE(r0.plugin);
    EXPECT_FLOAT_EQ(r0.writtenValue, 24.0f) << "the real value clamps to the def max";
    EXPECT_DOUBLE_EQ(slotParam(t, 0, 2), 24.0);

    const auto r1 = cmds.writeFxParam(writes[1]);
    EXPECT_TRUE(r1.ok);
    EXPECT_FLOAT_EQ(r1.writtenValue, 24.0f) << "normalized 1.0 denormalizes to the def max";
    EXPECT_DOUBLE_EQ(slotParam(t, 0, 2), 24.0);
}

// (d) setBusFxParams: two bus params in ONE undo unit, clamped, restored by one
// undo.
TEST_F(FxParamBatch, SetBusFxParamsIsOneUndoUnit) {
    auto& cmds = engine->getProjectCommands();
    const auto bus = cmds.createBus("fx", "Reverb Ret", "reverb", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    engine->drainPendingRoutingRebuild();

    auto busList = engine->getProjectModel().getBusListTree();
    auto busNode = HDAW::findBusNode(busList, bus.busID);
    ASSERT_TRUE(busNode.isValid());

    const double before0 = static_cast<double>(busNode.getProperty("param_0", -999.0));
    const double before1 = static_cast<double>(busNode.getProperty("param_2", -999.0));
    const int depth = undoDepth();

    std::vector<ProjectCommands::BusFxParamWrite> writes(2);
    writes[0] = { bus.busID, 0, 0.8 };   // Room Size
    writes[1] = { bus.busID, 2, 0.45 };  // Wet Level
    std::vector<std::string> errors;
    const auto res = cmds.setBusFxParams(writes, &errors);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.applied, 2);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_TRUE(errors[0].empty());
    EXPECT_TRUE(errors[1].empty());

    EXPECT_FLOAT_EQ(static_cast<float>(busNode.getProperty("param_0", -999.0)), 0.8f);
    EXPECT_FLOAT_EQ(static_cast<float>(busNode.getProperty("param_2", -999.0)), 0.45f);
    EXPECT_EQ(undoDepth(), depth + 1) << "one transaction";

    cmds.undo();
    EXPECT_FLOAT_EQ(static_cast<float>(busNode.getProperty("param_0", -999.0)),
                    static_cast<float>(before0));
    EXPECT_FLOAT_EQ(static_cast<float>(busNode.getProperty("param_2", -999.0)),
                    static_cast<float>(before1));
}

// (d') setBusFxParams partial-applies: a bad busID is reported, the good write
// still lands.
TEST_F(FxParamBatch, SetBusFxParamsPartialApply) {
    auto& cmds = engine->getProjectCommands();
    const auto bus = cmds.createBus("fx", "Delay Ret", "delay", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    engine->drainPendingRoutingRebuild();
    auto busNode = HDAW::findBusNode(engine->getProjectModel().getBusListTree(), bus.busID);
    ASSERT_TRUE(busNode.isValid());

    std::vector<ProjectCommands::BusFxParamWrite> writes(2);
    writes[0] = { 999, 0, 0.5 };          // no such bus
    writes[1] = { bus.busID, 1, 0.7 };    // Feedback
    std::vector<std::string> errors;
    const auto res = cmds.setBusFxParams(writes, &errors);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.applied, 1);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_FALSE(errors[0].empty());
    EXPECT_TRUE(errors[1].empty());
    EXPECT_FLOAT_EQ(static_cast<float>(busNode.getProperty("param_1", -999.0)), 0.7f);
}

// (e) setLfoParams: two LFO params in ONE undo unit; an unknown param NAME is
// REFUSED per write (the single setLfoParam silently no-ops on one).
TEST_F(FxParamBatch, SetLfoParamsBatchAndUnknownNameRefusal) {
    auto& cmds = engine->getProjectCommands();
    const int t = cmds.addTrack("Mod Track");
    engine->drainPendingRoutingRebuild();
    cmds.addLfo(t);
    engine->drainPendingRoutingRebuild();

    auto modList = engine->getProjectModel().getTrackListTree()
                       .getChild(t).getChildWithName(IDs::MODULATION_LIST);
    ASSERT_EQ(modList.getNumChildren(), 1);
    auto lfo = modList.getChild(0);
    const double beforeDepth = static_cast<double>(lfo.getProperty(IDs::depth, -999.0));
    const int depth = undoDepth();

    std::vector<ProjectCommands::LfoParamWrite> writes(3);
    writes[0] = { t, 0, "depth", 0.9 };
    writes[1] = { t, 0, "targetParamID", 2 };
    writes[2] = { t, 0, "bogusParam", 1.0 };   // unknown name -> refused
    std::vector<std::string> errors;
    const auto res = cmds.setLfoParams(writes, &errors);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.applied, 2);
    ASSERT_EQ(errors.size(), 3u);
    EXPECT_TRUE(errors[0].empty());
    EXPECT_TRUE(errors[1].empty());
    EXPECT_FALSE(errors[2].empty());
    EXPECT_TRUE(errors[2].find("unknown param") != std::string::npos) << errors[2];

    EXPECT_DOUBLE_EQ(static_cast<double>(lfo.getProperty(IDs::depth, -999.0)), 0.9);
    EXPECT_EQ(static_cast<int>(lfo.getProperty(IDs::targetParamID, -1)), 2);
    EXPECT_EQ(undoDepth(), depth + 1) << "one transaction";

    cmds.undo();
    EXPECT_DOUBLE_EQ(static_cast<double>(lfo.getProperty(IDs::depth, -999.0)), beforeDepth);
}

// (f) The shared request parser refuses an unknown write key and a
// non-integral index (the validator's wording), and resolves a stable `trackID`
// to the positional trackIndex.
TEST_F(FxParamBatch, ParserRejectsUnknownKeyAndNonIntegralIndex) {
    auto& cmds = engine->getProjectCommands();
    const int t = addEqTrack();
    auto trackList = engine->getProjectModel().getTrackListTree();
    const int stableID = static_cast<int>(trackList.getChild(t).getProperty(IDs::trackID, 0));
    ASSERT_GT(stableID, 0);

    // Unknown key -> refused, naming the key in the validator's wording.
    {
        std::vector<ProjectCommands::FxParamWrite> out;
        QString err;
        const QJsonArray arr{ QJsonObject{
            { "trackId", t }, { "slotIndex", 0 }, { "paramIndex", 0 },
            { "value", 1.0 }, { "bogusKey", 7 } } };
        EXPECT_FALSE(HDAW::parseFxParamWrites(trackList, arr, out, err));
        EXPECT_EQ(err, QStringLiteral("invalid params: writes[0].bogusKey: unknown property"));
    }
    // Non-integral index -> the validator's "expected integer".
    {
        std::vector<ProjectCommands::FxParamWrite> out;
        QString err;
        const QJsonArray arr{ QJsonObject{
            { "trackId", t }, { "slotIndex", 0.5 },
            { "paramIndex", 0 }, { "value", 1.0 } } };
        EXPECT_FALSE(HDAW::parseFxParamWrites(trackList, arr, out, err));
        EXPECT_EQ(err, QStringLiteral("invalid params: writes[0].slotIndex: expected integer"));
    }
    // Missing required `value` -> the validator's "missing required property".
    {
        std::vector<ProjectCommands::FxParamWrite> out;
        QString err;
        const QJsonArray arr{ QJsonObject{
            { "trackId", t }, { "slotIndex", 0 }, { "paramIndex", 0 } } };
        EXPECT_FALSE(HDAW::parseFxParamWrites(trackList, arr, out, err));
        EXPECT_EQ(err, QStringLiteral("invalid params: writes[0].value: missing required property 'value'"));
    }
    // Empty array -> the shared empty-batch text.
    {
        std::vector<ProjectCommands::FxParamWrite> out;
        QString err;
        EXPECT_FALSE(HDAW::parseFxParamWrites(trackList, QJsonArray{}, out, err));
        EXPECT_EQ(err, QString::fromUtf8(HDAW::kEmptyFxParamWritesError));
    }
    // Stable `trackID` resolves to the positional index; a per-write `mode`
    // overrides the caller default.
    {
        std::vector<ProjectCommands::FxParamWrite> out;
        QString err;
        const QJsonArray arr{ QJsonObject{
            { "trackID", stableID }, { "slotIndex", 0 }, { "paramIndex", 0 },
            { "value", 0.5 }, { "mode", "normalized" } } };
        ASSERT_TRUE(HDAW::parseFxParamWrites(trackList, arr, out, err)) << err.toStdString();
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].trackIndex, t);
        EXPECT_TRUE(out[0].hasParamIndex);
        EXPECT_TRUE(out[0].normalized);
    }
    // Unknown stable id -> refused, naming it (the shared rule's text).
    {
        std::vector<ProjectCommands::FxParamWrite> out;
        QString err;
        const QJsonArray arr{ QJsonObject{
            { "trackID", 4242 }, { "slotIndex", 0 }, { "paramIndex", 0 }, { "value", 1.0 } } };
        EXPECT_FALSE(HDAW::parseFxParamWrites(trackList, arr, out, err));
        EXPECT_EQ(err, QStringLiteral("unknown trackID 4242"));
    }
}

// (f') The bus + LFO parsers share the same strict-key / required behaviour.
TEST_F(FxParamBatch, BusAndLfoParsersAreStrict) {
    auto& cmds = engine->getProjectCommands();
    const int t = addEqTrack();
    auto trackList = engine->getProjectModel().getTrackListTree();

    {
        std::vector<ProjectCommands::BusFxParamWrite> out;
        QString err;
        const QJsonArray arr{ QJsonObject{ { "busID", 1 }, { "paramIndex", 0 }, { "value", 1.0 },
                                           { "trackId", 0 } } };
        EXPECT_FALSE(HDAW::parseBusFxParamWrites(arr, out, err));
        EXPECT_EQ(err, QStringLiteral("invalid params: writes[0].trackId: unknown property"));
    }
    {
        std::vector<ProjectCommands::LfoParamWrite> out;
        QString err;
        const QJsonArray arr{ QJsonObject{ { "trackId", t }, { "lfoIndex", 0 },
                                           { "paramName", "depth" } } };
        EXPECT_FALSE(HDAW::parseLfoParamWrites(trackList, arr, out, err));
        EXPECT_EQ(err, QStringLiteral("invalid params: writes[0].value: missing required property 'value'"));
    }
    {
        std::vector<ProjectCommands::LfoParamWrite> out;
        QString err;
        const QJsonArray arr{ QJsonObject{ { "trackId", t }, { "lfoIndex", 0 },
                                           { "paramName", "depth" }, { "value", 0.4 } } };
        ASSERT_TRUE(HDAW::parseLfoParamWrites(trackList, arr, out, err)) << err.toStdString();
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].trackIndex, t);
        EXPECT_EQ(out[0].paramName, "depth");
    }
    // The unknown-name refusal text is the shared one.
    EXPECT_EQ(QString::fromStdString(HDAW::unknownLfoParamError("nope")),
              QStringLiteral("unknown param 'nope' (valid: bipolar, depth, enabled, "
                             "phaseOffset, rate, rateSync, targetParamID, waveform)"));
}

// (g) Slice 3: the ONE shared resolver (common/FxParamResolve.h) backs
// writeFxParam, and its CODES map 1:1 to the message families each surface
// renders. Pin the code->text mapping here (the engine half of the byte-parity
// contract), including the presence rule for an explicit paramIndex 0.
TEST_F(FxParamBatch, SharedResolverCodesAndPrecedence) {
    auto& cmds = engine->getProjectCommands();
    const int t = addEqTrack();

    // Presence, not value: an explicit paramIndex 0 is a REAL address.
    {
        ProjectCommands::FxParamWrite w; w.trackIndex = t; w.slotIndex = 0;
        w.hasParamIndex = true; w.paramIndex = 0; w.value = 500.0;
        const auto r = cmds.writeFxParam(w);
        EXPECT_TRUE(r.ok) << r.error;
        EXPECT_FALSE(r.plugin);
        EXPECT_FLOAT_EQ(r.writtenValue, 500.0f);
        EXPECT_FLOAT_EQ(static_cast<float>(slotParam(t, 0, 0)), 500.0f);
    }
    // paramName beats paramIndex (the documented precedence).
    {
        ProjectCommands::FxParamWrite w; w.trackIndex = t; w.slotIndex = 0;
        w.hasParamIndex = true; w.paramIndex = 0; w.paramName = "Q"; w.value = 5.5;
        const auto r = cmds.writeFxParam(w);
        EXPECT_TRUE(r.ok) << r.error;
        EXPECT_FLOAT_EQ(static_cast<float>(slotParam(t, 0, 1)), 5.5f);
        EXPECT_FLOAT_EQ(static_cast<float>(slotParam(t, 0, 0)), 500.0f);   // untouched
    }
    // paramRequired: no addressing field at all.
    {
        ProjectCommands::FxParamWrite w; w.trackIndex = t; w.slotIndex = 0; w.value = 1.0;
        const auto r = cmds.writeFxParam(w);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(r.error, "paramIndex, paramName or intent required");
    }
    // unknownParamName.
    {
        ProjectCommands::FxParamWrite w; w.trackIndex = t; w.slotIndex = 0;
        w.paramName = "Nope"; w.value = 1.0;
        const auto r = cmds.writeFxParam(w);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(r.error, "unknown paramName: Nope");
    }
    // indexOutOfRange (addresses the eq's 3 params).
    {
        ProjectCommands::FxParamWrite w; w.trackIndex = t; w.slotIndex = 0;
        w.hasParamIndex = true; w.paramIndex = 9; w.value = 1.0;
        const auto r = cmds.writeFxParam(w);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(r.error, "param index out of range");
    }
    // Slot-resolution texts ("slot not found" / "track not found") unchanged.
    {
        ProjectCommands::FxParamWrite w; w.trackIndex = t; w.slotIndex = 9;
        w.hasParamIndex = true; w.paramIndex = 0; w.value = 1.0;
        EXPECT_EQ(cmds.writeFxParam(w).error, "slot not found");
        w.trackIndex = 99;
        EXPECT_EQ(cmds.writeFxParam(w).error, "track not found");
    }
    // "slot is empty" (fxType none) survives.
    {
        engine->getProjectCommands().addFxSlot(t, std::string("none"), -1, std::string());
        engine->drainPendingRoutingRebuild();
        ProjectCommands::FxParamWrite w; w.trackIndex = t; w.slotIndex = 1;
        w.hasParamIndex = true; w.paramIndex = 0; w.value = 1.0;
        EXPECT_EQ(cmds.writeFxParam(w).error, "slot is empty");
    }
}

} // namespace
