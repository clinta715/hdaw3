#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

#include <cmath>
#include <vector>

namespace {

// Ripple delete is a project-wide operation: it processes every track. The
// default project ships all tracks empty, so tests add their clips to track 0
// and scope assertions to that track via trackIndex. This both isolates the
// test and verifies the op is project-wide.

std::vector<ClipSnapshot> clipsOnTrack0(AudioEngine& engine)
{
    std::vector<ClipSnapshot> out;
    for (const auto& c : engine.getReadModel().snapshot().clips)
        if (c.trackIndex == 0)
            out.push_back(c);
    return out;
}

bool clipExists(AudioEngine& engine, int clipId)
{
    for (const auto& c : engine.getReadModel().snapshot().clips)
        if (c.clipId == clipId) return true;
    return false;
}

ClipSnapshot requireClip(AudioEngine& engine, int clipId)
{
    for (const auto& c : engine.getReadModel().snapshot().clips)
        if (c.clipId == clipId)
            return c;
    ADD_FAILURE() << "clip " << clipId << " not found in snapshot";
    return {};
}

} // namespace

// Default BPM is 120 -> factor 0.5 s/beat; all values convert exactly.

// Clip fully inside [2,6) is removed; clip fully after is shifted left by 4.
TEST(RippleDelete, RemovesInsideClipAndShiftsAfterClipLeft)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int inside = cmds.addMidiClip(0, 3.0, 2.0, "inside");   // beats [3,5)  -> inside [2,6)
    ASSERT_GT(inside, 0);
    int after  = cmds.addMidiClip(0, 8.0, 4.0, "after");    // beats [8,12) -> after 6
    ASSERT_GT(after, 0);

    cmds.rippleDelete(2.0, 6.0);

    EXPECT_FALSE(clipExists(engine, inside));
    EXPECT_TRUE(clipExists(engine, after));

    auto ac = requireClip(engine, after);
    EXPECT_NEAR(ac.startBeat, 4.0, 1e-6);       // 8 - 4 = 4
    EXPECT_NEAR(ac.durationBeats, 4.0, 1e-6);   // duration unchanged
}

// Clip straddling the start boundary is trimmed: [0,4) over range [2,6) -> [0,2).
// (Slicing reassigns ids, so assert by position, not the original id.)
TEST(RippleDelete, TrimsClipStraddlingStart)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 4.0, "head");   // beats [0,4)

    cmds.rippleDelete(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 1u);
    EXPECT_NEAR(t0[0].startBeat, 0.0, 1e-6);
    EXPECT_NEAR(t0[0].durationBeats, 2.0, 1e-6);   // trimmed from 4 to 2
}

// Clip spanning the whole range splits into two: [0,10) over [2,6) -> [0,2) + [2,6).
TEST(RippleDelete, SplitsSpanningClipIntoTwo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 10.0, "span");   // beats [0,10)

    cmds.rippleDelete(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 2u);
    bool foundLeft = false, foundRight = false;
    for (const auto& c : t0)
    {
        if (std::abs(c.startBeat - 0.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundLeft = true; }
        if (std::abs(c.startBeat - 2.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 4.0, 1e-6); foundRight = true; }
    }
    EXPECT_TRUE(foundLeft);
    EXPECT_TRUE(foundRight);
}

// Empty / inverted range is a no-op (the guard at the top of rippleDelete).
TEST(RippleDelete, EmptyRangeIsNoOp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int c = cmds.addMidiClip(0, 4.0, 4.0, "untouched");
    ASSERT_GT(c, 0);

    cmds.rippleDelete(4.0, 4.0);   // zero-length
    EXPECT_TRUE(clipExists(engine, c));
    EXPECT_NEAR(requireClip(engine, c).startBeat, 4.0, 1e-6);

    cmds.rippleDelete(6.0, 2.0);   // inverted
    EXPECT_TRUE(clipExists(engine, c));
}

// A clip ending exactly at the range start is NOT removed (touching != overlapping).
TEST(RippleDelete, BoundaryTouchingClipIsUntouched)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int before = cmds.addMidiClip(0, 0.0, 2.0, "before");   // beats [0,2), touches rs=2
    ASSERT_GT(before, 0);

    cmds.rippleDelete(2.0, 6.0);

    EXPECT_TRUE(clipExists(engine, before));
    auto sc = requireClip(engine, before);
    EXPECT_NEAR(sc.startBeat, 0.0, 1e-6);
    EXPECT_NEAR(sc.durationBeats, 2.0, 1e-6);
}

// The whole ripple (slice + remove + shift) must undo in a single step.
TEST(RippleDelete, UndoRestoresEverythingInOneStep)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int span  = cmds.addMidiClip(0, 0.0, 10.0, "span");
    int after = cmds.addMidiClip(0, 12.0, 4.0, "after");
    ASSERT_GT(span, 0);
    ASSERT_GT(after, 0);

    cmds.rippleDelete(2.0, 6.0);
    // After ripple: span split into 2, after shifted to beat 8 -> 3 clips on track 0.
    EXPECT_EQ(clipsOnTrack0(engine).size(), 2u + 1u);

    engine.getProjectModel().getUndoManager().undo();
    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 2u);
    // Undo restores the original clips with their original ids.
    bool foundSpan = false, foundAfter = false;
    for (const auto& c : t0)
    {
        if (c.clipId == span)  { EXPECT_NEAR(c.startBeat, 0.0, 1e-6); EXPECT_NEAR(c.durationBeats, 10.0, 1e-6); foundSpan = true; }
        if (c.clipId == after) { EXPECT_NEAR(c.startBeat, 12.0, 1e-6); EXPECT_NEAR(c.durationBeats, 4.0, 1e-6); foundAfter = true; }
    }
    EXPECT_TRUE(foundSpan);
    EXPECT_TRUE(foundAfter);
}
