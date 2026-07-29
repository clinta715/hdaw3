#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

#include <cmath>
#include <vector>

namespace {

// Region ops are project-wide; the default project ships track 1 ("Synth")
// with Melody/Chords. Tests use the EMPTY track 0 and scope by trackIndex
// (AGENTS.md lesson 7).
std::vector<ClipSnapshot> clipsOnTrack0(AudioEngine& engine)
{
    std::vector<ClipSnapshot> out;
    for (const auto& c : engine.getReadModel().snapshot().clips)
        if (c.trackIndex == 0)
            out.push_back(c);
    return out;
}

} // namespace

// BPM 120 -> 0.5 s/beat; all values convert exactly.

// ─── insertSilence ──────────────────────────────────────────────────────────

// Insert silence [2,6): a clip after 6 shifts right by 4; the gap [2,6) is empty.
TEST(InsertSilence, ShiftsLaterClipsRightAndOpensGap)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int after = cmds.addMidiClip(0, 8.0, 4.0, "after");   // beats [8,12)
    ASSERT_GT(after, 0);

    cmds.insertSilence(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 1u);
    EXPECT_EQ(t0[0].clipId, after);
    EXPECT_NEAR(t0[0].startBeat, 12.0, 1e-6);   // 8 + 4
    EXPECT_NEAR(t0[0].durationBeats, 4.0, 1e-6);
}

// A clip straddling the insertion point is split: [0,4) over insert at 2 -> [0,2) + [6,10).
TEST(InsertSilence, SplitsClipStraddlingInsertionPoint)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 4.0, "head");   // beats [0,4), insertion point at beat 2

    cmds.insertSilence(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 2u);
    bool foundHead = false, foundTail = false;
    for (const auto& c : t0)
    {
        if (std::abs(c.startBeat - 0.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundHead = true; }
        // tail [2,4) shifted right by 4 -> [6,10)
        if (std::abs(c.startBeat - 6.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundTail = true; }
    }
    EXPECT_TRUE(foundHead);
    EXPECT_TRUE(foundTail);
}

// Empty / inverted range is a no-op.
TEST(InsertSilence, EmptyRangeIsNoOp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int c = cmds.addMidiClip(0, 4.0, 4.0, "untouched");
    ASSERT_GT(c, 0);

    cmds.insertSilence(4.0, 4.0);
    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 1u);
    EXPECT_NEAR(t0[0].startBeat, 4.0, 1e-6);
    EXPECT_NEAR(t0[0].durationBeats, 4.0, 1e-6);
}

// --- duplicateRegion -------------------------------------------------------

// Duplicate region [2,6): inside clip [3,5) is copied to [7,9); after clip
// [8,12) shifts right by 4 to [12,16).
TEST(DuplicateRegion, CopiesInsideAndShiftsAfterRight)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 3.0, 2.0, "inside");  // beats [3,5) inside [2,6)
    cmds.addMidiClip(0, 8.0, 4.0, "after");   // beats [8,12) after 6

    cmds.duplicateRegion(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 3u);   // original inside + copy + shifted after
    bool foundOrigInside = false, foundCopy = false, foundAfter = false;
    for (const auto& c : t0)
    {
        if (std::abs(c.startBeat - 3.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundOrigInside = true; }
        // copy of [3,5) lands at 3+4=7 -> [7,9)
        if (std::abs(c.startBeat - 7.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 2.0, 1e-6); foundCopy = true; }
        // after [8,12) shifts to [12,16)
        if (std::abs(c.startBeat - 12.0) < 1e-6) { EXPECT_NEAR(c.durationBeats, 4.0, 1e-6); foundAfter = true; }
    }
    EXPECT_TRUE(foundOrigInside);
    EXPECT_TRUE(foundCopy);
    EXPECT_TRUE(foundAfter);
}

// A clip spanning the whole region is split: [0,10) over [2,6) -> head [0,2) kept,
// inside [2,6) copied to [6,10), tail [6,10) shifted to [10,14) = 4 clips.
TEST(DuplicateRegion, SplitsSpanningClipAndDuplicatesInside)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 0.0, 10.0, "span");   // beats [0,10)

    cmds.duplicateRegion(2.0, 6.0);

    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 4u);
}

// Empty range is a no-op.
TEST(DuplicateRegion, EmptyRangeIsNoOp)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiClip(0, 4.0, 4.0, "untouched");

    cmds.duplicateRegion(4.0, 4.0);
    auto t0 = clipsOnTrack0(engine);
    ASSERT_EQ(t0.size(), 1u);
    EXPECT_NEAR(t0[0].startBeat, 4.0, 1e-6);
    EXPECT_NEAR(t0[0].durationBeats, 4.0, 1e-6);
}
