#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <cstring>
#include <string>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "engine/FmSynthEngine.h"
#include "model/ProjectModel.h"

// Lesson 10 discipline: the imported DX7 patch must persist into the slot's
// ValueTree (fmPatchData) AND be restored on the LIVE processor after a
// rebuildRoutingGraph() — a ReadModel-only or no-crash smoke test would not
// prove tree-copy renders (export/gain-stage/audition) hear the patch.

namespace {

void fillTestPatch(uint8_t* patch, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        patch[i] = static_cast<uint8_t>(i);
}

juce::ValueTree findSlotTree(AudioEngine& engine, int trackIndex, int slotIndex)
{
    return engine.getProjectModel().getTrackListTree()
        .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
}

} // namespace

TEST(FmPatchPersistence, SetFmPatchWritesTreeAndRestoresOnRebuild)
{
    AudioEngine engine;
    engine.initialize();

    engine.getProjectCommands().addFxSlot(0, "fm_synth", 0, "");

    uint8_t patch[FmSynthEngine::kPatchSize];
    fillTestPatch(patch, FmSynthEngine::kPatchSize);

    juce::MemoryBlock block(patch, FmSynthEngine::kPatchSize);
    engine.getProjectCommands().setFmPatch(0, 0, block.toBase64Encoding().toStdString());

    // (a) The slot's ValueTree node carries fmPatchData, decoding to exactly
    // the 156 input bytes.
    auto slotTree = findSlotTree(engine, 0, 0);
    ASSERT_TRUE(slotTree.isValid());
    ASSERT_TRUE(slotTree.hasProperty(IDs::fmPatchData));
    juce::MemoryBlock stored;
    ASSERT_TRUE(stored.fromBase64Encoding(slotTree.getProperty(IDs::fmPatchData).toString()));
    ASSERT_EQ(stored.getSize(), static_cast<size_t>(FmSynthEngine::kPatchSize));
    EXPECT_EQ(std::memcmp(stored.getData(), patch, FmSynthEngine::kPatchSize), 0);

    // (b) Lesson-10 live-processor restore: after a routing-graph rebuild the
    // LIVE processor's FM engine carries the imported patch.
    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_GE(chain.size(), 1u);
    ASSERT_NE(chain[0], nullptr);
    auto* fm = chain[0]->fmSynthEngine();
    ASSERT_NE(fm, nullptr);
    EXPECT_EQ(std::memcmp(fm->patchData(), patch, FmSynthEngine::kPatchSize), 0);
}

TEST(FmPatchPersistence, RejectsWrongSizePatch)
{
    AudioEngine engine;
    engine.initialize();

    engine.getProjectCommands().addFxSlot(0, "fm_synth", 0, "");

    // A 10-byte payload is not a valid DX7 patch (156 bytes required): the
    // command must reject it WITHOUT writing the fmPatchData property.
    uint8_t badPatch[10] = {};
    juce::MemoryBlock block(badPatch, sizeof(badPatch));
    engine.getProjectCommands().setFmPatch(0, 0, block.toBase64Encoding().toStdString());

    auto slotTree = findSlotTree(engine, 0, 0);
    ASSERT_TRUE(slotTree.isValid());
    EXPECT_FALSE(slotTree.hasProperty(IDs::fmPatchData));
}

TEST(FmPatchPersistence, PatchSurvivesReset)
{
    // Root-cause regression: TrackFXSlot::reset() re-runs FmSynthEngine::prepare(),
    // which used to unconditionally re-seed patchData_ to the DX7 init patch.
    // The offline-export path calls Track::releaseResources() between the final
    // slot prepare (patch correctly present) and the render loop, so the reset
    // wiped the imported patch and every import rendered the same init tone.
    // prepare() must seed the init patch exactly once per engine and preserve
    // whatever patch is in patchData_ on later prepare/reset calls.
    AudioEngine engine;
    engine.initialize();

    engine.getProjectCommands().addFxSlot(0, "fm_synth", 0, "");

    uint8_t patch[FmSynthEngine::kPatchSize];
    fillTestPatch(patch, FmSynthEngine::kPatchSize);

    juce::MemoryBlock block(patch, FmSynthEngine::kPatchSize);
    engine.getProjectCommands().setFmPatch(0, 0, block.toBase64Encoding().toStdString());
    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_GE(chain.size(), 1u);
    ASSERT_NE(chain[0], nullptr);

    auto* fm = chain[0]->fmSynthEngine();
    ASSERT_NE(fm, nullptr);
    ASSERT_EQ(std::memcmp(fm->patchData(), patch, FmSynthEngine::kPatchSize), 0);

    // Production reset path: Track::processBlock services the deferred reset
    // flag set by Track::releaseResources(); the slot's reset() re-runs
    // prepare() on the FM engine. The loaded patch must survive.
    chain[0]->reset();
    EXPECT_EQ(std::memcmp(fm->patchData(), patch, FmSynthEngine::kPatchSize), 0);
}