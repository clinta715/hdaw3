#include <gtest/gtest.h>
#include "render_harness.h"

// Task 2 incremental-routing tests: RoutingManager::removeClip and
// RoutingManager::updateClipPlacement must be behaviorally identical to a full
// rebuildRoutingGraph, proven by render-equivalence against LIVE processors
// (Gate T2-G1 / T2-G2 / T2-G3 / T2-G4). Uses the shared RenderHarness
// extracted from the Task 1 spike. Every harness is explicitly shutdown()
// (parked teardown + bake-wait) — see render_harness.h.
namespace {

// 6-clip layout: clips 0/1 and 2/3 overlap; clip 5 (start 5.4) overlaps clip 4
// (start 5.0) so an add/remove of clip 5 must re-apply crossfades to clip 4.
juce::ValueTree makeRemoveLayoutClips(const juce::File& file)
{
    std::vector<double> starts = { 0.0, 0.9, 2.5, 3.4, 5.0, 5.4 };
    std::vector<double> durs   = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };
    return makeClipList(file, 6, starts, durs);
}

} // namespace

// T2-G1 — incremental removeClip is render-equivalent to a full rebuild.
// Graph B = full rebuild of the 6-clip layout, then incremental remove of clip
// 5. Reference C = full rebuild of the 5-clip layout. Render B vs C < 1e-6,
// equal node + connection counts, live map = 5 entries, and the sibling (clip
// 4) that overlapped the removed clip loses its crossfade points — matching C
// on the live processor.
TEST(IncrementalRoutingRemoveMove, RemoveEquivalentToFullRebuild)
{
    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    auto sixClips = makeRemoveLayoutClips(file);
    auto fiveClips = juce::ValueTree(IDs::CLIP_LIST);
    for (int i = 0; i < 5; ++i)
        fiveClips.addChild(sixClips.getChild(i).createCopy(), -1, nullptr);

    const int numBlocks = blocksFor(6.0);

    // Graph B: full rebuild of all 6, then incremental remove of clip 5.
    RenderHarness b;
    b.init(sixClips);
    b.build();
    ASSERT_EQ(b.routing->getAudioClipSources().size(), 6u);

    // The sibling that will lose its crossfade must currently HAVE crossfade
    // points (clip 4 overlaps clip 5), so the remove has a crossfade to drop.
    auto* sibBefore = b.routing->getAudioClipSources().at({0, 4});
    ASSERT_NE(sibBefore, nullptr);
    ASSERT_FALSE(sibBefore->getGainEnvelopePoints().empty())
        << "clip 4 must overlap the removed clip 5 so removal drops a crossfade";

    // Remove clip 5 from the ValueTree FIRST (removeClip reads crossfades back
    // from the model tree).
    auto clipList = b.model.getTrackListTree().getChild(0).getChildWithName(IDs::CLIP_LIST);
    clipList.removeChild(5, nullptr);

    {
        const juce::MessageManagerLock pumpPark;
        b.routing->removeClip(0, 5);
    }
    ASSERT_TRUE(b.waitForBake()) << "removeClip bake timed out";
    EXPECT_EQ(b.routing->getAudioClipSources().size(), 5u);

    // Graph C (reference): full rebuild of the 5-clip layout.
    RenderHarness c;
    c.init(fiveClips);
    c.build();
    ASSERT_EQ(c.routing->getAudioClipSources().size(), 5u);

    juce::AudioBuffer<float> outB(2, numBlocks * RenderHarness::kBlockSize);
    juce::AudioBuffer<float> outC(2, numBlocks * RenderHarness::kBlockSize);
    float maxDiff = maxAbsDiff(b, c, numBlocks, outB, outC);

    EXPECT_LT(maxDiff, 1e-6f) << "incremental remove diverges from full rebuild";
    EXPECT_EQ(b.graph.getNumNodes(), c.graph.getNumNodes());
    EXPECT_EQ(b.graph.getConnections().size(), c.graph.getConnections().size());
    EXPECT_EQ(b.routing->getAudioClipSources().size(), c.routing->getAudioClipSources().size());

    // Sibling that overlapped the removed clip: crossfade dropped, live
    // envelope matches the reference.
    auto* sibInc = b.routing->getAudioClipSources().at({0, 4});
    auto* sibRef = c.routing->getAudioClipSources().at({0, 4});
    ASSERT_NE(sibInc, nullptr);
    ASSERT_NE(sibRef, nullptr);
    EXPECT_TRUE(sibInc->getGainEnvelopePoints().empty())
        << "clip 4 must lose its crossfade points after clip 5 is removed";
    expectEnvelopeEqual(sibInc->getGainEnvelopePoints(), sibRef->getGainEnvelopePoints());

    b.shutdown();
    c.shutdown();
    file.deleteFile();
}

// T2-G2 — incremental updateClipPlacement (move) is render-equivalent to a full
// rebuild. Graph B = full rebuild of the initial layout (clip 4 isolated at
// 6.0), then clip 4's ValueTree startTime is changed to 1.6 (into a NEW overlap
// with clip 1 ending at 3.0) and updateClipPlacement(0, 4) is applied.
// Reference C = full rebuild of the CHANGED layout. Render B vs C < 1e-6,
// equal node/connection counts, and the moved clip + overlapped sibling's
// envelope on the LIVE processors matches C. Graph A proves the move created a
// new overlap (clip 4 starts with no crossfade points).
TEST(IncrementalRoutingRemoveMove, PlacementEquivalentToFullRebuild)
{
    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    // Initial layout: clip 4 at start 6.0 (isolated). Clip 1 spans [2.0, 3.0].
    std::vector<double> starts = { 0.0, 2.0, 4.0, 4.4, 6.0 };
    std::vector<double> durs   = { 1.0, 1.0, 1.0, 1.0, 1.0 };
    auto initialClips = makeClipList(file, 5, starts, durs);

    // Changed layout: clip 4 moved to 1.6 → [1.6, 2.6] overlaps clip 1 by 0.6.
    std::vector<double> changedStarts = { 0.0, 2.0, 4.0, 4.4, 1.6 };
    auto changedClips = makeClipList(file, 5, changedStarts, durs);

    const int numBlocks = blocksFor(6.0);

    // Graph A: initial layout must have clip 4 with NO crossfade (isolated),
    // proving the incremental move creates a genuinely new overlap.
    RenderHarness a;
    a.init(initialClips);
    a.build();
    auto* aMoved = a.routing->getAudioClipSources().at({0, 4});
    ASSERT_NE(aMoved, nullptr);
    EXPECT_TRUE(aMoved->getGainEnvelopePoints().empty())
        << "clip 4 must start isolated so the move creates a NEW overlap";

    // Graph B: full rebuild of the initial layout, then incremental move.
    RenderHarness b;
    b.init(initialClips);
    b.build();

    auto clipList = b.model.getTrackListTree().getChild(0).getChildWithName(IDs::CLIP_LIST);
    auto clip4 = clipList.getChild(4);
    clip4.setProperty(IDs::startTime, 1.6, nullptr);

    {
        const juce::MessageManagerLock pumpPark;
        b.routing->updateClipPlacement(0, 4);
    }
    ASSERT_TRUE(b.waitForBake()) << "updateClipPlacement bake timed out";

    // Graph C (reference): full rebuild of the changed layout.
    RenderHarness c;
    c.init(changedClips);
    c.build();

    juce::AudioBuffer<float> outB(2, numBlocks * RenderHarness::kBlockSize);
    juce::AudioBuffer<float> outC(2, numBlocks * RenderHarness::kBlockSize);
    float maxDiff = maxAbsDiff(b, c, numBlocks, outB, outC);

    EXPECT_LT(maxDiff, 1e-6f) << "incremental placement change diverges from full rebuild";
    EXPECT_EQ(b.graph.getNumNodes(), c.graph.getNumNodes());
    EXPECT_EQ(b.graph.getConnections().size(), c.graph.getConnections().size());

    // Moved clip gained crossfade points and matches the reference on the LIVE
    // processor.
    auto* bMoved = b.routing->getAudioClipSources().at({0, 4});
    auto* cMoved = c.routing->getAudioClipSources().at({0, 4});
    ASSERT_NE(bMoved, nullptr);
    ASSERT_NE(cMoved, nullptr);
    ASSERT_FALSE(bMoved->getGainEnvelopePoints().empty())
        << "moved clip must gain crossfade points against clip 1";
    expectEnvelopeEqual(bMoved->getGainEnvelopePoints(), cMoved->getGainEnvelopePoints());

    // Overlapped sibling (clip 1) gained crossfade points and matches the
    // reference on the LIVE processor.
    auto* bSib = b.routing->getAudioClipSources().at({0, 1});
    auto* cSib = c.routing->getAudioClipSources().at({0, 1});
    ASSERT_NE(bSib, nullptr);
    ASSERT_NE(cSib, nullptr);
    ASSERT_FALSE(bSib->getGainEnvelopePoints().empty())
        << "overlapped sibling must gain crossfade points";
    expectEnvelopeEqual(bSib->getGainEnvelopePoints(), cSib->getGainEnvelopePoints());

    a.shutdown();
    b.shutdown();
    c.shutdown();
    file.deleteFile();
}

// T2-G3 — add → undo (incremental remove) → redo (incremental re-add) is
// render-equivalent to a full rebuild. Emulates the command layer with direct
// ValueTree ops + model-level append/remove (the real command wiring is Task 3).
// Final render < 1e-6 vs a full rebuild of the 6-clip layout; the live
// getAudioClipSources() map matches. Undo-of-add leaves an UNTOUCHED sibling
// (clip 0) bit-identical and a TOUCHED sibling (clip 4) matching the 5-clip
// full-rebuild reference (its crossfade dropped).
TEST(IncrementalRoutingRemoveMove, UndoRedoEquivalentToFullRebuild)
{
    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    // Base 5-clip layout (clip 0/1 overlap pair preserved throughout).
    std::vector<double> starts = { 0.0, 0.9, 2.5, 3.4, 5.0 };
    std::vector<double> durs   = { 1.0, 1.0, 1.0, 1.0, 1.0 };
    auto baseClips = makeClipList(file, 5, starts, durs);

    // Clip 5 overlaps clip 4 ([5.0, 6.0]) → the add/undo/redo round-trip
    // exercises sibling crossfade re-application.
    // Throwaway id mint — this test addresses clips by index, never by id.
    ProjectModel mint;
    auto clip5 = mint.createAudioClip("c5", 5.4, 1.0, file.getFullPathName());

    // Reference C: full rebuild of the 6-clip layout (base + clip 5).
    auto sixClips = juce::ValueTree(IDs::CLIP_LIST);
    for (int i = 0; i < 5; ++i)
        sixClips.addChild(baseClips.getChild(i).createCopy(), -1, nullptr);
    sixClips.addChild(clip5.createCopy(), -1, nullptr);

    const int numBlocks = blocksFor(6.4);

    RenderHarness b;
    b.init(baseClips);
    b.build();
    ASSERT_EQ(b.routing->getAudioClipSources().size(), 5u);

    auto clipList = b.model.getTrackListTree().getChild(0).getChildWithName(IDs::CLIP_LIST);

    // Untouched-sibling baseline: clip 0's envelope right after the initial
    // full rebuild (clip 0 only overlaps clip 1, never clip 5).
    auto* sib0Base = b.routing->getAudioClipSources().at({0, 0});
    ASSERT_NE(sib0Base, nullptr);
    auto sib0EnvBefore = sib0Base->getGainEnvelopePoints();

    // "add" (the redo target).
    clipList.addChild(clip5.createCopy(), -1, nullptr);
    {
        const juce::MessageManagerLock pumpPark;
        b.routing->addClip(0, 5, clip5);
    }
    ASSERT_TRUE(b.waitForBake()) << "add bake timed out";
    EXPECT_EQ(b.routing->getAudioClipSources().size(), 6u);

    // "undo of add" → incremental remove. Clip 5 dropped from the ValueTree
    // first, then removed from the graph.
    clipList.removeChild(5, nullptr);
    {
        const juce::MessageManagerLock pumpPark;
        b.routing->removeClip(0, 5);
    }
    ASSERT_TRUE(b.waitForBake()) << "undo remove bake timed out";
    EXPECT_EQ(b.routing->getAudioClipSources().size(), 5u);

    // Undo must leave the UNTOUCHED sibling (clip 0) bit-identical...
    EXPECT_EQ(sib0Base->getGainEnvelopePoints().size(), sib0EnvBefore.size());
    for (size_t i = 0; i < sib0EnvBefore.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(sib0Base->getGainEnvelopePoints()[i].time, sib0EnvBefore[i].time);
        EXPECT_FLOAT_EQ(sib0Base->getGainEnvelopePoints()[i].gain, sib0EnvBefore[i].gain);
    }

    // ...and the TOUCHED sibling (clip 4) back to the 5-clip reference (its
    // crossfade against clip 5 dropped).
    RenderHarness ref5;
    ref5.init(baseClips);
    ref5.build();
    auto* sib4Undo = b.routing->getAudioClipSources().at({0, 4});
    auto* sib4Ref = ref5.routing->getAudioClipSources().at({0, 4});
    ASSERT_NE(sib4Undo, nullptr);
    ASSERT_NE(sib4Ref, nullptr);
    EXPECT_TRUE(sib4Undo->getGainEnvelopePoints().empty())
        << "clip 4 must lose crossfade after the undo of the add";
    expectEnvelopeEqual(sib4Undo->getGainEnvelopePoints(), sib4Ref->getGainEnvelopePoints());

    // "redo" → incremental re-add (append-restore only).
    clipList.addChild(clip5.createCopy(), -1, nullptr);
    {
        const juce::MessageManagerLock pumpPark;
        b.routing->addClip(0, 5, clip5);
    }
    ASSERT_TRUE(b.waitForBake()) << "redo add bake timed out";
    EXPECT_EQ(b.routing->getAudioClipSources().size(), 6u);

    // Final state must match a full rebuild of the 6-clip layout, and the live
    // map must match.
    RenderHarness c;
    c.init(sixClips);
    c.build();
    ASSERT_EQ(c.routing->getAudioClipSources().size(), 6u);

    juce::AudioBuffer<float> outB(2, numBlocks * RenderHarness::kBlockSize);
    juce::AudioBuffer<float> outC(2, numBlocks * RenderHarness::kBlockSize);
    float maxDiff = maxAbsDiff(b, c, numBlocks, outB, outC);

    EXPECT_LT(maxDiff, 1e-6f) << "undo/redo round-trip diverges from full rebuild";
    EXPECT_EQ(b.graph.getNumNodes(), c.graph.getNumNodes());
    EXPECT_EQ(b.graph.getConnections().size(), c.graph.getConnections().size());
    EXPECT_EQ(b.routing->getAudioClipSources().size(), c.routing->getAudioClipSources().size());
    for (const auto& kv : b.routing->getAudioClipSources())
        EXPECT_TRUE(c.routing->getAudioClipSources().count(kv.first))
            << "live map key (" << kv.first.first << "," << kv.first.second
            << ") missing from full-rebuild reference";

    b.shutdown();
    ref5.shutdown();
    c.shutdown();
    file.deleteFile();
}