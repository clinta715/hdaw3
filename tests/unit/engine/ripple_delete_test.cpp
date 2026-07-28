#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

namespace {

ClipSnapshot requireClip(AudioEngine& engine, int clipId)
{
    auto snap = engine.getReadModel().snapshot();
    for (const auto& c : snap.clips)
        if (c.clipId == clipId)
            return c;
    ADD_FAILURE() << "clip " << clipId << " not found in snapshot";
    return {};
}

bool clipExists(AudioEngine& engine, int clipId)
{
    auto snap = engine.getReadModel().snapshot();
    for (const auto& c : snap.clips)
        if (c.clipId == clipId) return true;
    return false;
}

} // namespace

// Default project BPM is 120 -> factor 0.5 s/beat. All test values are chosen
// so beats<->seconds conversions are exact (no floating-point drift).

// Clip fully inside [2,6) is removed; clip fully after is shifted left by 4.
TEST(RippleDelete, RemovesInsideClipAndShiftsAfterClipLeft)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int inside = cmds.addMidiClip(1, 3.0, 2.0, "inside");   // beats [3,5)  -> inside [2,6)
    ASSERT_GT(inside, 0);
    int after  = cmds.addMidiClip(1, 8.0, 4.0, "after");    // beats [8,12) -> after 6
    ASSERT_GT(after, 0);

    cmds.rippleDelete(2.0, 6.0);

    EXPECT_FALSE(clipExists(engine, inside));
    EXPECT_TRUE(clipExists(engine, after));

    auto ac = requireClip(engine, after);
    EXPECT_NEAR(ac.startBeat, 4.0, 1e-6);       // 8 - 4 = 4
    EXPECT_NEAR(ac.durationBeats, 4.0, 1e-6);   // duration unchanged
}
