#include <gtest/gtest.h>
#include "engine/ChainLibrary.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "engine/PsyFmEngine.h"
#include "engine/PsyFmModMatrix.h"
#include "model/ProjectModel.h"

// Qt defines `slots` as a keyword macro (signals/slots); once Qt headers are
// pulled in (via AudioEngine.h) it textually blanks ChainPreset::slots. This
// TU uses no Qt signals/slots keywords, so undef it (same dodge as
// fx_chain_preset_test.cpp).
#ifdef slots
#undef slots
#endif

// Slot-scoped PATCH verbs (ProjectCommands::exportPatch / applyPatch).
//
// Root cause this pins: load_fx_chain is CHAIN-scoped — it preserves the
// target's instrument slots and APPENDS the preset's instrument as a new
// slot, so loading a 1-slot psy_fm chain onto a track that already had a
// psy_fm slot produced TWO psy_fm slots with the target untouched. The patch
// verbs are the SLOT-scoped sibling: exportPatch snapshots ONE slot, and
// applyPatch writes it INTO the existing slot — never appends, never removes.

namespace {

int treeFxSlotCount(AudioEngine& engine, int trackIndex)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return -1;
    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
        return 0;
    return fxChain.getNumChildren();
}

juce::ValueTree treeFxChain(AudioEngine& engine, int trackIndex)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return {};
    return trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
}

juce::String treeFxTypeAt(AudioEngine& engine, int trackIndex, int slotIndex)
{
    auto fxChain = treeFxChain(engine, trackIndex);
    if (!fxChain.isValid() || slotIndex < 0 || slotIndex >= fxChain.getNumChildren())
        return {};
    return fxChain.getChild(slotIndex).getProperty(IDs::fxType, "").toString();
}

double treeParam(AudioEngine& engine, int trackIndex, int slotIndex, int paramIndex)
{
    auto fxChain = treeFxChain(engine, trackIndex);
    if (!fxChain.isValid() || slotIndex < 0 || slotIndex >= fxChain.getNumChildren())
        return -999999.0;
    return static_cast<double>(fxChain.getChild(slotIndex).getProperty(
        juce::Identifier("param_" + juce::String(paramIndex)), -999999.0));
}

juce::String treeStringProp(AudioEngine& engine, int trackIndex, int slotIndex,
                            const char* name)
{
    auto fxChain = treeFxChain(engine, trackIndex);
    if (!fxChain.isValid() || slotIndex < 0 || slotIndex >= fxChain.getNumChildren())
        return {};
    return fxChain.getChild(slotIndex).getProperty(juce::Identifier(name), "").toString();
}

} // namespace

// Acceptance 2a: build a psy_fm track, load "bell", add a distinctive route
// and an internal param, export the patch — params + psyFmMatrix +
// psyFmSweepRate must all be captured in slot 0.
TEST(PatchPreset, ExportPatchCapturesSlotState)
{
    AudioEngine engine;
    engine.initialize();
    engine.getProjectCommands().addTrack("Source");
    engine.drainPendingRoutingRebuild();
    auto& commands = engine.getAudioEngineCommands();

    commands.addFxSlot(0, "psy_fm", 0, std::string());
    ASSERT_TRUE(commands.setFxSlotPsyFmPreset(0, 0, "bell"));
    // Distinctive route on top of bell's own "feedbackLFO:op6Feedback:0.3".
    commands.setFxSlotPsyFmModRoute(0, 0, "modWheel", "op1Ratio", 0.42f);
    // Filter Cutoff (param 33) — an internal param far from the preset value.
    commands.setFxSlotParam(0, 0, 33, 2500.0f);

    auto patch = commands.exportPatch(0, 0);
    ASSERT_EQ(patch.slots.size(), 1u);
    const auto& s = patch.slots[0];
    EXPECT_EQ(s.fxType.toStdString(), "psy_fm");

    ASSERT_TRUE(s.params.count("param_33") > 0);
    EXPECT_FLOAT_EQ(static_cast<float>(s.params.at("param_33")), 2500.0f);

    const juce::String matrix = s.psyFmMatrix;
    EXPECT_TRUE(matrix.contains("feedbackLFO:op6Feedback:0.3")) << matrix.toStdString();
    EXPECT_TRUE(matrix.contains("modWheel:op1Ratio:0.42")) << matrix.toStdString();
    // bell's sweepRateHz is a float widened to double on write, so compare as
    // float (EXPECT_DOUBLE_EQ on 0.2 would fail by ULPs).
    EXPECT_FLOAT_EQ(static_cast<float>(s.psyFmSweepRate), 0.2f);
}

// Acceptance 2b + 2c: apply the patch onto a DIFFERENT psy_fm track that
// started from "metallicPluck". That track's matrix + params must now equal
// the source's, its slot COUNT must be unchanged (no append), and its OTHER
// slots must be untouched. Also asserted on the LIVE processor after rebuild.
TEST(PatchPreset, ApplyPatchRestoresIntoExistingSlotWithoutAppend)
{
    AudioEngine engine;
    engine.initialize();
    engine.getProjectCommands().addTrack("Source");
    engine.getProjectCommands().addTrack("Target");
    engine.drainPendingRoutingRebuild();
    auto& commands = engine.getAudioEngineCommands();

    // Source slot 0: bell + route + cutoff.
    commands.addFxSlot(0, "psy_fm", 0, std::string());
    ASSERT_TRUE(commands.setFxSlotPsyFmPreset(0, 0, "bell"));
    commands.setFxSlotPsyFmModRoute(0, 0, "modWheel", "op1Ratio", 0.42f);
    commands.setFxSlotParam(0, 0, 33, 2500.0f);
    const auto sourcePatch = commands.exportPatch(0, 0);
    ASSERT_EQ(sourcePatch.slots.size(), 1u);

    // Target: a DIFFERENT psy_fm patch plus a second (eq) slot that must
    // survive the patch apply untouched.
    commands.addFxSlot(1, "psy_fm", 0, std::string());
    ASSERT_TRUE(commands.setFxSlotPsyFmPreset(1, 0, "metallicPluck"));
    commands.addFxSlot(1, "eq", 1, std::string());
    commands.setFxSlotParam(1, 1, 0, 500.0f);

    const int slotsBefore = treeFxSlotCount(engine, 1);
    ASSERT_EQ(slotsBefore, 2);
    const double eqFreqBefore = treeParam(engine, 1, 1, 0);
    const juce::String targetMatrixBefore = treeStringProp(engine, 1, 0, "psyFmMatrix");
    EXPECT_TRUE(targetMatrixBefore.contains("feedbackLFO:op6Feedback:0.15"))
        << targetMatrixBefore.toStdString();   // metallicPluck's own route

    juce::String error;
    ASSERT_TRUE(commands.applyPatch(1, 0, sourcePatch, &error)) << error.toStdString();

    // 2c. No append, no removal; the other slot is untouched.
    EXPECT_EQ(treeFxSlotCount(engine, 1), slotsBefore);
    EXPECT_EQ(treeFxTypeAt(engine, 1, 0).toStdString(), "psy_fm");
    EXPECT_EQ(treeFxTypeAt(engine, 1, 1).toStdString(), "eq");
    EXPECT_DOUBLE_EQ(treeParam(engine, 1, 1, 0), eqFreqBefore);

    // 2b. Target slot 0 now mirrors the source: same params, same matrix,
    // same sweep rate.
    for (const auto& kv : sourcePatch.slots[0].params)
    {
        const int idx = kv.first.fromLastOccurrenceOf("_", false, false).getIntValue();
        EXPECT_FLOAT_EQ(static_cast<float>(treeParam(engine, 1, 0, idx)),
                        static_cast<float>(kv.second))
            << "param " << idx;
    }
    EXPECT_EQ(treeStringProp(engine, 1, 0, "psyFmMatrix"),
              sourcePatch.slots[0].psyFmMatrix);
    EXPECT_DOUBLE_EQ(treeParam(engine, 1, 0, 33), 2500.0);
    EXPECT_FLOAT_EQ(
        static_cast<float>(treeFxChain(engine, 1).getChild(0)
                               .getProperty(juce::Identifier("psyFmSweepRate"), -1.0)),
        0.2f);

    // Live state after the rebuild applyPatch performed (Gate 1/10 seam).
    ASSERT_NE(engine.getMainProcessor(), nullptr);
    auto* target = engine.getMainProcessor()->getTrack(1);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(target->getFXChain().size(), 2u);
    EXPECT_EQ(target->getFXChain()[0]->getType().toStdString(), "psy_fm");
    EXPECT_EQ(target->getFXChain()[1]->getType().toStdString(), "eq");
    ASSERT_NE(target->getFXChain()[0]->psyFmEngine(), nullptr);
    const auto& routes =
        target->getFXChain()[0]->psyFmEngine()->getModMatrix().getRoutes();
    bool sawFeedback = false, sawModWheel = false;
    for (const auto& r : routes)
    {
        if (r.source == HDAW::PsyFmModRoute::Source::FeedbackLFO
            && r.dest == HDAW::PsyFmModRoute::Dest::Op6Feedback)
            sawFeedback = true;
        if (r.source == HDAW::PsyFmModRoute::Source::ModWheel
            && r.dest == HDAW::PsyFmModRoute::Dest::Op1Ratio)
            sawModWheel = true;
    }
    EXPECT_TRUE(sawFeedback);
    EXPECT_TRUE(sawModWheel);
}

// Acceptance 2d: applyPatch REFUSES (false + non-empty error) for an
// out-of-range slot index, a preset with no slots, and an fxType mismatch —
// and every refusal leaves the target chain untouched.
TEST(PatchPreset, ApplyPatchRefusals)
{
    AudioEngine engine;
    engine.initialize();
    engine.getProjectCommands().addTrack("Target");
    engine.drainPendingRoutingRebuild();
    auto& commands = engine.getAudioEngineCommands();

    commands.addFxSlot(0, "psy_fm", 0, std::string());
    ASSERT_TRUE(commands.setFxSlotPsyFmPreset(0, 0, "metallicPluck"));
    const juce::String matrixBefore = treeStringProp(engine, 0, 0, "psyFmMatrix");
    ASSERT_EQ(treeFxSlotCount(engine, 0), 1);

    // (i) Slot index out of range.
    {
        auto valid = commands.exportPatch(0, 0);
        ASSERT_EQ(valid.slots.size(), 1u);
        juce::String error;
        EXPECT_FALSE(commands.applyPatch(0, 99, valid, &error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_EQ(treeFxSlotCount(engine, 0), 1);
    }

    // (ii) Preset with no slots.
    {
        HDAW::ChainPreset empty;
        juce::String error;
        EXPECT_FALSE(commands.applyPatch(0, 0, empty, &error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_EQ(treeFxSlotCount(engine, 0), 1);
    }

    // (iii) fxType mismatch: a patch exported from a sub_synth slot applied
    // onto this psy_fm slot.
    {
        auto trackList = engine.getProjectModel().getTrackListTree();
        ASSERT_EQ(trackList.getNumChildren(), 1);
        engine.getProjectCommands().addTrack("SubSource");
        engine.drainPendingRoutingRebuild();
        commands.addFxSlot(1, "sub_synth", 0, std::string());
        auto subPatch = commands.exportPatch(1, 0);
        ASSERT_EQ(subPatch.slots.size(), 1u);
        ASSERT_EQ(subPatch.slots[0].fxType.toStdString(), "sub_synth");

        juce::String error;
        EXPECT_FALSE(commands.applyPatch(0, 0, subPatch, &error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_TRUE(error.contains("mismatch")) << error.toStdString();
        EXPECT_EQ(treeFxSlotCount(engine, 0), 1);
    }

    // Nothing above wrote through: the target slot is byte-identical.
    EXPECT_EQ(treeFxSlotCount(engine, 0), 1);
    EXPECT_EQ(treeFxTypeAt(engine, 0, 0).toStdString(), "psy_fm");
    EXPECT_EQ(treeStringProp(engine, 0, 0, "psyFmMatrix"), matrixBefore);

    // Live processor untouched too.
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->getFXChain().size(), 1u);
}

// The patch is carried as a single-slot ChainPreset, so a round-trip through
// the PATCH library (HDAW/patches) must preserve it.
TEST(PatchPreset, ChainLibraryPatchLibraryRoundTrip)
{
    const juce::File tempDir =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("hdaw_patch_lib_test_"
                          + juce::String(juce::Time::getMillisecondCounter()));
    tempDir.createDirectory();
    {
        // Roster::None: this case owns the root, so it seeds no built-in
        // roster (the PATCH roster — 12 bass patches — is exercised by
        // ChainLibrary.FactoryPatches* in chain_library_test.cpp).
        HDAW::ChainLibrary lib(tempDir, HDAW::ChainLibrary::Roster::None);

        HDAW::ChainPreset p;
        p.name = "Bell Patch";
        HDAW::ChainPreset::Slot s;
        s.fxType = "psy_fm";
        s.params["param_33"] = 2500.0;
        s.psyFmMatrix = "feedbackLFO:op6Feedback:0.3;modWheel:op1Ratio:0.42";
        s.psyFmSweepRate = 0.2;
        p.slots = { s };

        auto id = lib.savePreset(p);
        ASSERT_FALSE(id.isEmpty());

        auto loaded = lib.loadPreset(id);
        ASSERT_EQ(loaded.slots.size(), 1u);
        EXPECT_EQ(loaded.slots[0].fxType.toStdString(), "psy_fm");
        EXPECT_DOUBLE_EQ(loaded.slots[0].params.at("param_33"), 2500.0);
        EXPECT_EQ(loaded.slots[0].psyFmMatrix.toStdString(),
                  "feedbackLFO:op6Feedback:0.3;modWheel:op1Ratio:0.42");
        EXPECT_DOUBLE_EQ(loaded.slots[0].psyFmSweepRate, 0.2);

        // Roster::None: the patch root lists ONLY the saved preset.
        auto all = lib.listPresets();
        ASSERT_EQ(all.size(), 1u);
        EXPECT_FALSE(all[0].isFactory);
    }
    tempDir.deleteRecursively();
}
