#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"

TEST(TrackMixerState, RestoredAfterRoutingGraphRebuild)
{
    AudioEngine engine;
    engine.initialize();

    engine.setTrackVolume(0, 0.5f);
    engine.setTrackPan(0, -0.25f);
    engine.setTrackMuted(0, true);

    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* tr = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(tr, nullptr);
    EXPECT_TRUE(tr->getMuted());
    EXPECT_FLOAT_EQ(tr->getVolume(), 0.5f);
    EXPECT_FLOAT_EQ(tr->getPan(), -0.25f);
}

TEST(TrackMixerState, DefaultsAppliedWhenUnset)
{
    AudioEngine engine;
    engine.initialize();

    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* tr = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(tr, nullptr);
    EXPECT_FALSE(tr->getMuted());
    EXPECT_FLOAT_EQ(tr->getVolume(), 1.0f);
    EXPECT_FLOAT_EQ(tr->getPan(), 0.0f);
}
