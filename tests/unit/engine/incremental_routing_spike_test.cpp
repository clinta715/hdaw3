#include <gtest/gtest.h>
#include "render_harness.h"

#include <iostream>

// G2 — incremental add is render-equivalent to a full rebuild. Reference
// project P = 6 audio clips (one overlapping pair, one far-gap pair, and the
// LAST clip overlapping its neighbor so sibling crossfade re-application is
// exercised). Graph A = full rebuild of P. Graph B = full rebuild of P minus
// the last clip, then one incremental addClip for it. Renders must match
// sample-near-identically (< 1e-6 max abs diff), and the final topologies
// (node count, connection count) must be identical.
TEST(IncrementalRoutingSpike, EquivalentToFullRebuild)
{
    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    // Layout (start, dur): clips 0 and 1 overlap 0.1s; clips 2 and 3 overlap
    // 0.1s; clips 3 and 4 have a 0.6s gap (no crossfade); clip 5 (added
    // incrementally) overlaps clip 4 by 0.6s.
    std::vector<double> starts = { 0.0, 0.9, 2.5, 3.4, 5.0, 5.4 };
    std::vector<double> durs   = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };
    auto allClips = makeClipList(file, 6, starts, durs);
    auto minusLast = makeClipList(file, 5, starts, durs);
    auto lastClip = allClips.getChild(5).createCopy();

    const int numBlocks = blocksFor(6.4);

    // Graph A: full rebuild of all 6 clips.
    RenderHarness a;
    a.init(allClips);
    a.build();
    EXPECT_EQ(a.routing->getAudioClipSources().size(), 6u);

    // Graph B: full rebuild of the first 5, then incremental add of clip 5.
    RenderHarness b;
    b.init(minusLast);
    b.build();
    ASSERT_EQ(b.routing->getAudioClipSources().size(), 5u);

    // Append the clip to the ValueTree first (addClip reads crossfades from
    // the model tree, and the identity-map keys assume clipIndex == last
    // position in CLIP_LIST).
    auto trackList = b.model.getTrackListTree();
    auto clipList = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    clipList.addChild(lastClip.createCopy(), -1, nullptr);
    b.addClipParked(0, 5, lastClip);
    ASSERT_TRUE(b.waitForBake()) << "addClip bake timed out";
    EXPECT_EQ(b.routing->getAudioClipSources().size(), 6u);

    juce::AudioBuffer<float> outA(2, numBlocks * RenderHarness::kBlockSize);
    juce::AudioBuffer<float> outB(2, numBlocks * RenderHarness::kBlockSize);
    float maxDiff = maxAbsDiff(a, b, numBlocks, outA, outB);

    EXPECT_LT(maxDiff, 1e-6f) << "incremental add diverges from full rebuild";
    EXPECT_EQ(a.graph.getNumNodes(), b.graph.getNumNodes());
    EXPECT_EQ(a.graph.getConnections().size(), b.graph.getConnections().size());

    a.shutdown();
    b.shutdown();
    file.deleteFile();
}

// G3 — sibling state preserved (Gate 1/6/10). Track has two non-overlapping
// clips + one overlapping pair. After an incremental addClip of an ISOLATED
// clip, the observed sibling's live processor state (envelope/start/duration)
// is bit-identical. After an addClip that DOES overlap the sibling, its
// crossfade points change to exactly what a full rebuild produces.
TEST(IncrementalRoutingSpike, SiblingStatePreserved)
{
    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    // cA(0.0,1.0) isolated; cB(2.0,1.0) isolated; cC(4.0,1.0)/cD(4.4,1.0)
    // overlap pair.
    std::vector<double> starts = { 0.0, 2.0, 4.0, 4.4 };
    std::vector<double> durs   = { 1.0, 1.0, 1.0, 1.0 };
    auto baseClips = makeClipList(file, 4, starts, durs);

    RenderHarness h;
    h.init(baseClips);
    h.build();

    auto* sibling = h.routing->getAudioClipSources().at({0, 0});
    ASSERT_NE(sibling, nullptr);
    auto envBefore = sibling->getGainEnvelopePoints();
    const double startBefore = sibling->getStartTime();
    const double durBefore = sibling->getDuration();

    // 1) Add an ISOLATED clip (start 7.0, far from everything). Sibling must
    //    be untouched: identical envelope, start, duration.
    auto isolated = ProjectModel::createAudioClip(
        "isolated", 7.0, 1.0, file.getFullPathName());
    auto trackList = h.model.getTrackListTree();
    auto clipList = trackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    clipList.addChild(isolated.createCopy(), -1, nullptr);
    h.addClipParked(0, 4, isolated);
    ASSERT_TRUE(h.waitForBake()) << "isolated addClip bake timed out";

    EXPECT_EQ(sibling->getStartTime(), startBefore);
    EXPECT_EQ(sibling->getDuration(), durBefore);
    EXPECT_EQ(sibling->getGainEnvelopePoints().size(), envBefore.size());
    for (size_t i = 0; i < envBefore.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(sibling->getGainEnvelopePoints()[i].time, envBefore[i].time);
        EXPECT_FLOAT_EQ(sibling->getGainEnvelopePoints()[i].gain, envBefore[i].gain);
    }

    // 2) Add a clip that DOES overlap the sibling (start 0.5, dur 1.0 →
    //    overlaps cA [0.0,1.0] by 0.5s). The sibling's crossfade points must
    //    change to exactly what a full rebuild produces.
    auto overlapClip = ProjectModel::createAudioClip(
        "overlap", 0.5, 1.0, file.getFullPathName());
    clipList.addChild(overlapClip.createCopy(), -1, nullptr);
    h.addClipParked(0, 5, overlapClip);
    ASSERT_TRUE(h.waitForBake()) << "overlap addClip bake timed out";

    // Reference: full rebuild of the same 6-clip layout, read sibling's envelope.
    std::vector<double> refStarts = { 0.0, 0.5, 2.0, 4.0, 4.4, 7.0 };
    std::vector<double> refDurs   = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };
    auto refClips = makeClipList(file, 6, refStarts, refDurs);
    RenderHarness ref;
    ref.init(refClips);
    ref.build();
    auto* refSibling = ref.routing->getAudioClipSources().at({0, 0});
    ASSERT_NE(refSibling, nullptr);
    auto refEnv = refSibling->getGainEnvelopePoints();

    auto incEnv = sibling->getGainEnvelopePoints();
    ASSERT_FALSE(incEnv.empty()) << "sibling crossfade points missing after overlap add";
    EXPECT_EQ(incEnv.size(), refEnv.size());
    for (size_t i = 0; i < refEnv.size(); ++i)
    {
        EXPECT_NEAR(incEnv[i].time, refEnv[i].time, 1e-6)
            << "point " << i;
        EXPECT_FLOAT_EQ(incEnv[i].gain, refEnv[i].gain) << "point " << i;
    }

    h.shutdown();
    ref.shutdown();
    file.deleteFile();
}

// G4 — benchmark. Measures and PRINTS: (a) one incremental addClip vs one full
// rebuild at 128 clips; (b) cumulative 1→128 addClip vs 1→128 full rebuilds;
// (c) the graphLock-hold time (= the timed call itself; the standalone harness
// has no graphLock, but the timed surface is exactly the mutation that would
// hold it); (d) graph latency before/after an incremental add (must be equal —
// no topology latency shift). Asserts only generous bounds (incremental < full;
// latency unchanged; no crash); numbers go to the recommendation.
TEST(IncrementalRoutingSpike, BenchmarkPrintsNumbers)
{
    auto file = writeSineWav(static_cast<int>(1.0 * 44100.0), 44100.0);
    constexpr int kNumClips = 128;

    // 128 clips: mostly non-overlapping (start = i*0.5, dur 0.4), plus a few
    // overlapping pairs at the end so the incremental crossfade recompute is
    // exercised at full size.
    std::vector<double> starts, durs;
    for (int i = 0; i < kNumClips; ++i)
    {
        starts.push_back(i * 0.5);
        durs.push_back(0.4);
    }
    for (int i = kNumClips - 4; i < kNumClips; ++i)
    {
        starts[static_cast<size_t>(i)] = static_cast<double>(i - 1) * 0.5 + 0.15;
        durs[static_cast<size_t>(i)] = 0.6;
    }
    auto fullClips = makeClipList(file, kNumClips, starts, durs);

    // (a) one full rebuild at 128 clips.
    RenderHarness fullHarness;
    fullHarness.init(fullClips);
    double tFullRebuild = fullHarness.timedFullRebuild();
    ASSERT_TRUE(fullHarness.waitForBake()) << "full rebuild bake timed out";

    // (a) one incremental addClip at 128 clips: build 127 via full rebuild,
    // then time ONLY the addClip call.
    RenderHarness incHarness;
    auto minusLast = makeClipList(file, kNumClips - 1, starts, durs);
    incHarness.init(minusLast);
    incHarness.build();
    auto lastClip = fullClips.getChild(kNumClips - 1).createCopy();
    auto incTrackList = incHarness.model.getTrackListTree();
    auto incClipList = incTrackList.getChild(0).getChildWithName(IDs::CLIP_LIST);
    incClipList.addChild(lastClip.createCopy(), -1, nullptr);
    double tAddClip = incHarness.timedAddClip(0, kNumClips - 1, lastClip);
    ASSERT_TRUE(incHarness.waitForBake()) << "addClip bake timed out";
    EXPECT_EQ(incHarness.routing->getAudioClipSources().size(),
              fullHarness.routing->getAudioClipSources().size());

    // (d) graph latency before/after an incremental add (unchanged).
    const int latencyBefore = incHarness.graph.getLatencySamples();
    // One more incremental add on the now-128 clip graph.
    auto extraClip = ProjectModel::createAudioClip(
        "extra", 999.0, 0.4, file.getFullPathName());
    incClipList.addChild(extraClip.createCopy(), -1, nullptr);
    incHarness.addClipParked(0, kNumClips, extraClip);
    ASSERT_TRUE(incHarness.waitForBake()) << "latency addClip bake timed out";
    const int latencyAfter = incHarness.graph.getLatencySamples();

    // (b) cumulative 1→128 for BOTH paths, one fresh harness per path.
    // Per-iteration bake-waits are deliberately NOT taken here: the
    // MessageManagerLock inside timedFullRebuild/timedAddClip already blocks
    // until the pump has drained the previous topology's async bake (the lock
    // waits for the message thread to go idle), so each mutation runs against a
    // fully-baked graph and the pump never races a subsequent clear/re-add
    // (lesson 12/18). A single bake-wait at the end of each loop confirms the
    // final topology baked before harness teardown.
    double cumFull = 0.0, cumInc = 0.0;
    RenderHarness cumFullH;
    cumFullH.init(makeDefaultClipList());
    cumFullH.build();
    auto cumFullClipList = cumFullH.model.getTrackListTree()
                               .getChild(0).getChildWithName(IDs::CLIP_LIST);
    for (int i = 0; i < kNumClips; ++i)
    {
        cumFullClipList.addChild(fullClips.getChild(i).createCopy(), -1, nullptr);
        cumFull += cumFullH.timedFullRebuild();
    }
    ASSERT_TRUE(cumFullH.waitForBake()) << "cumulative full rebuild bake timed out";

    RenderHarness cumIncH;
    cumIncH.init(makeDefaultClipList());
    cumIncH.build();
    auto cumIncClipList = cumIncH.model.getTrackListTree()
                              .getChild(0).getChildWithName(IDs::CLIP_LIST);
    for (int i = 0; i < kNumClips; ++i)
    {
        auto clip = fullClips.getChild(i).createCopy();
        cumIncClipList.addChild(clip.createCopy(), -1, nullptr);
        cumInc += cumIncH.timedAddClip(0, i, clip);
    }
    ASSERT_TRUE(cumIncH.waitForBake()) << "cumulative addClip bake timed out";

    std::cout << "\n[IncrementalRoutingSpike.Benchmark] at " << kNumClips << " clips\n"
              << "  single op  addClip=" << tAddClip << "ms  fullRebuild=" << tFullRebuild << "ms\n"
              << "  cumulative 1->" << kNumClips << ":  addClip=" << cumInc
              << "ms  fullRebuild=" << cumFull << "ms\n"
              << "  graph latency before=" << latencyBefore
              << " after=" << latencyAfter << " samples\n\n";

    EXPECT_LT(tAddClip, tFullRebuild);
    EXPECT_LT(cumInc, cumFull);
    EXPECT_EQ(latencyBefore, latencyAfter);

    fullHarness.shutdown();
    incHarness.shutdown();
    cumFullH.shutdown();
    cumIncH.shutdown();
    file.deleteFile();
}