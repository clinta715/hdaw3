#include <gtest/gtest.h>
#include "engine/TransportManager.h"

TEST(TransportManager, AdvanceIncrementsPosition)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setPlaying(true);
    tm.advance(512);
    EXPECT_EQ(tm.getCurrentSample(), 512);
    tm.advance(512);
    EXPECT_EQ(tm.getCurrentSample(), 1024);
}

TEST(TransportManager, AdvanceNoOpWhenStopped)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setPlaying(false);
    tm.advance(512);
    EXPECT_EQ(tm.getCurrentSample(), 0);
}

TEST(TransportManager, AdvanceNoOpWhenRecording)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setRecording(true);
    tm.advance(512);
    EXPECT_EQ(tm.getCurrentSample(), 512);
}

TEST(TransportManager, AutoStopFiresAtProjectEnd)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setPlaying(true);
    tm.setProjectEndSample(1000);

    // Advance to just before the end
    tm.advance(900);
    EXPECT_EQ(tm.getCurrentSample(), 900);
    EXPECT_TRUE(tm.isPlayingNow());

    // Advance past the end — auto-stop should fire
    bool stopped = tm.advance(200);
    EXPECT_TRUE(stopped);
    EXPECT_FALSE(tm.isPlayingNow());
    EXPECT_EQ(tm.getCurrentSample(), 1000); // clamped, not 1100
}

TEST(TransportManager, AutoStopReturnsFalseWhenNotTriggered)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setPlaying(true);
    tm.setProjectEndSample(10000);

    bool stopped = tm.advance(512);
    EXPECT_FALSE(stopped);
    EXPECT_TRUE(tm.isPlayingNow());
}

TEST(TransportManager, AutoStopIgnoredWhenProjectEndZero)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setPlaying(true);
    tm.setProjectEndSample(0);

    tm.advance(10000);
    EXPECT_TRUE(tm.isPlayingNow());
    EXPECT_EQ(tm.getCurrentSample(), 10000);
}

TEST(TransportManager, AutoStopIgnoredWhenLooping)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setPlaying(true);
    tm.setLooping(true);
    tm.setLoopStartSample(0);
    tm.setLoopEndSample(500);
    tm.setProjectEndSample(1000);

    // Advance past loop end — should wrap, not auto-stop
    tm.advance(600);
    EXPECT_TRUE(tm.isPlayingNow());
    // Position should be wrapped: (600 - 0) % 500 + 0 = 100
    EXPECT_EQ(tm.getCurrentSample(), 100);
}

TEST(TransportManager, AutoStopExactBoundary)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setPlaying(true);
    tm.setProjectEndSample(1024);

    // Advance exactly to the boundary
    tm.advance(1024);
    EXPECT_FALSE(tm.isPlayingNow());
    EXPECT_EQ(tm.getCurrentSample(), 1024);
}

TEST(TransportManager, ConsumeAutoStopRequested)
{
    HDAW::TransportManager tm;
    EXPECT_FALSE(tm.consumeAutoStopRequested()); // initially false

    tm.setSampleRate(44100.0);
    tm.setPlaying(true);
    tm.setProjectEndSample(100);
    tm.advance(200); // triggers auto-stop
    EXPECT_TRUE(tm.consumeAutoStopRequested());
    EXPECT_FALSE(tm.consumeAutoStopRequested()); // consumed
}

TEST(TransportManager, LoopWrapDoesNotTriggerAutoStop)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setPlaying(true);
    tm.setLooping(true);
    tm.setLoopStartSample(0);
    tm.setLoopEndSample(1000);
    tm.setProjectEndSample(5000);

    // Multiple advances past loop end — should keep looping, never auto-stop
    for (int i = 0; i < 20; ++i)
        tm.advance(512);

    EXPECT_TRUE(tm.isPlayingNow());
    EXPECT_FALSE(tm.consumeAutoStopRequested());
}
