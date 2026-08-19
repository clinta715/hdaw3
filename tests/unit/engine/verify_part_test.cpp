#include <gtest/gtest.h>
#include <string>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

// verifyPart — composer self-verification: solo + full-mix renders of the
// track's window via the shared renderTrackWindow, reporting levels,
// clipping, audibility and spectral band presence. Internal fm_synth only —
// no real plugins, no env guards.

TEST(VerifyPart, ComposedPartPasses)
{
    AudioEngine engine;
    engine.initialize();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Verify";
    params.style = "Standard";
    params.lengthBeats = 4.0;
    params.seed = 7;
    params.targetRms = 0.15f;
    params.windowSeconds = 4.0;

    auto res = engine.getProjectCommands().addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0);

    auto v = engine.getProjectCommands().verifyPart(res.trackIndex, 4.0);
    EXPECT_TRUE(v.ok) << v.error;
    EXPECT_TRUE(v.audible);
    EXPECT_TRUE(v.nonClipping);
    EXPECT_GT(v.soloPeak, 1e-4f);
    EXPECT_LT(v.mixPeak, 1.0f);
    EXPECT_TRUE(v.bandsPresent)
        << "bandLow=" << v.bandLow << " bandMid=" << v.bandMid
        << " bandHigh=" << v.bandHigh
        << " soloRms=" << v.soloRms << " soloPeak=" << v.soloPeak;
}

TEST(VerifyPart, TwoPartsMixAtLeastSolo)
{
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    ProjectCommands::InstrumentPartParams p1;
    p1.trackName = "PartA";
    p1.style = "Standard";
    p1.lengthBeats = 4.0;
    p1.seed = 1;
    auto r1 = pc.addInstrumentPart(p1);
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    ProjectCommands::InstrumentPartParams p2;
    p2.trackName = "PartB";
    p2.style = "BassLine";
    p2.lengthBeats = 4.0;
    p2.seed = 2;
    auto r2 = pc.addInstrumentPart(p2);
    ASSERT_TRUE(r2.error.empty()) << r2.error;

    auto v = pc.verifyPart(r1.trackIndex, 4.0);
    ASSERT_TRUE(v.ok) << v.error;
    EXPECT_GE(v.mixRms, v.soloRms * 0.999f);
}

TEST(VerifyPart, InvalidTrack)
{
    AudioEngine engine;
    engine.initialize();

    auto v = engine.getProjectCommands().verifyPart(99, 4.0);
    EXPECT_FALSE(v.ok);
    EXPECT_FALSE(v.error.empty());
}

TEST(VerifyPart, EmptyTrack)
{
    AudioEngine engine;
    engine.initialize();

    // Default project track 0 ships with an empty CLIP_LIST (lesson 9).
    auto v = engine.getProjectCommands().verifyPart(0, 4.0);
    EXPECT_FALSE(v.ok);
    EXPECT_EQ(v.error, "track has no clips");
}
