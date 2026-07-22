// Tests for the v0.12.0+ ghost-clip + paint/repeat features
// (spec: docs/superpowers/specs/2026-07-21-paint-ghost-clips-design.md).
//
// These exercise the engine at the same level the frontend RPC layer and the
// GUI do: AudioEngine is fully initialize()d so the AudioEngine ValueTree
// listeners that drive source→ghost propagation are live (the propagation
// logic guards on `mainProcessor != nullptr`, which is only true after init).
//
// We assert against the ReadModel (the abstract read path the frontend uses)
// and, where propagation touches raw ValueTree nodes the ReadModel doesn't
// expose (MIDI_NOTE_LIST children of a ghost), against the ProjectModel tree
// directly — mirroring the AddCcPoint test's pattern in commands_test.cpp.

#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

#include <juce_data_structures/juce_data_structures.h>

namespace {

// Find a clip's snapshot by id, or fail the calling test.
ClipSnapshot requireClip(AudioEngine& engine, int clipId)
{
    auto snap = engine.getReadModel().snapshot();
    for (const auto& c : snap.clips)
        if (c.clipId == clipId)
            return c;
    ADD_FAILURE() << "clip " << clipId << " not found in snapshot";
    return {};
}

// Walk the model tree to read the raw MIDI note count of a clip.
int rawNoteCount(AudioEngine& engine, int clipId)
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            if (static_cast<int>(clip.getProperty(IDs::clipID, -1)) == clipId)
            {
                auto nl = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
                return nl.isValid() ? nl.getNumChildren() : 0;
            }
        }
    }
    return -1; // not found
}

} // namespace

// ─── createGhostClip: basic creation & metadata ────────────────────────

TEST(GhostClips, CreateGhostSetsMetadata)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    ASSERT_GT(srcId, 0);

    int ghostId = cmds.createGhostClip(srcId, 8.0, 1);
    ASSERT_GT(ghostId, 0);
    EXPECT_NE(ghostId, srcId);

    auto ghost = requireClip(engine, ghostId);
    EXPECT_TRUE(ghost.isGhost);
    EXPECT_EQ(ghost.ghostSourceId, srcId);
    EXPECT_EQ(ghost.trackIndex, 1);
    EXPECT_DOUBLE_EQ(ghost.startBeat, 8.0);
}

TEST(GhostClips, CreateGhostFromInvalidSourceReturnsNegOne)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    EXPECT_EQ(cmds.createGhostClip(99999, 4.0, 0), -1);
}

TEST(GhostClips, CreateGhostOnInvalidTrackReturnsNegOne)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    ASSERT_GT(srcId, 0);
    EXPECT_EQ(cmds.createGhostClip(srcId, 4.0, 999), -1);
}

// ─── createGhostClip: ghost-of-ghost chain is flattened to root ────────
// Per spec §2.6: a ghost of a ghost points at the root source, depth ≤ 1.

TEST(GhostClips, GhostOfGhostResolvesToRoot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int rootId = cmds.addMidiClip(0, 0.0, 4.0, "Root");
    ASSERT_GT(rootId, 0);
    int ghost1 = cmds.createGhostClip(rootId, 4.0, 0);
    ASSERT_GT(ghost1, 0);
    int ghost2 = cmds.createGhostClip(ghost1, 8.0, 0); // ghost of a ghost
    ASSERT_GT(ghost2, 0);

    auto g2 = requireClip(engine, ghost2);
    EXPECT_TRUE(g2.isGhost);
    EXPECT_EQ(g2.ghostSourceId, rootId); // not ghost1 — the root
}

// ─── Content properties are deep-copied at creation ────────────────────

TEST(GhostClips, GhostInheritsContentProperties)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    cmds.setClipGain(srcId, 0.5f);
    cmds.setClipFadeIn(srcId, 1.0);
    cmds.setClipLooping(srcId, true);

    int ghostId = cmds.createGhostClip(srcId, 8.0, 0);
    ASSERT_GT(ghostId, 0);

    auto ghost = requireClip(engine, ghostId);
    EXPECT_FLOAT_EQ(static_cast<float>(ghost.gain), 0.5f);
    EXPECT_DOUBLE_EQ(ghost.fadeIn, 1.0);
    EXPECT_TRUE(ghost.looping);
}

TEST(GhostClips, GhostMidiNotesCopiedWithFreshIds)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int noteA = cmds.addNote(srcId, 60, 100, 0.0, 1.0);
    int noteB = cmds.addNote(srcId, 64, 90, 1.0, 0.5);
    ASSERT_GT(noteA, 0);
    ASSERT_GT(noteB, 0);

    int ghostId = cmds.createGhostClip(srcId, 8.0, 0);
    ASSERT_GT(ghostId, 0);

    // Ghost gets the same note *content* (pitch/start) …
    auto ghostNotes = engine.getReadModel().getNotes(ghostId);
    ASSERT_EQ(ghostNotes.size(), 2u);
    // … but brand-new noteIDs (never aliased to the source's).
    for (const auto& gn : ghostNotes)
    {
        EXPECT_NE(gn.noteId, noteA);
        EXPECT_NE(gn.noteId, noteB);
    }
}

// ─── Property propagation: source → ghost (live edit) ──────────────────
// Spec §2.3: changing a propagated content property on the source pushes
// the same value to every ghost.

TEST(GhostClips, PropagateGainChangeToGhosts)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int g1 = cmds.createGhostClip(srcId, 4.0, 0);
    int g2 = cmds.createGhostClip(srcId, 8.0, 1);
    ASSERT_GT(g1, 0);
    ASSERT_GT(g2, 0);

    cmds.setClipGain(srcId, 0.25f);

    EXPECT_FLOAT_EQ(static_cast<float>(requireClip(engine, g1).gain), 0.25f);
    EXPECT_FLOAT_EQ(static_cast<float>(requireClip(engine, g2).gain), 0.25f);
}

TEST(GhostClips, PropagateFadeInChangeToGhosts)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int g = cmds.createGhostClip(srcId, 4.0, 0);
    ASSERT_GT(g, 0);

    cmds.setClipFadeIn(srcId, 2.5);

    EXPECT_DOUBLE_EQ(requireClip(engine, g).fadeIn, 2.5);
}

// Non-propagated properties (per spec §2.3: startTime, duration, name)
// must NOT bleed across the source→ghost boundary.

TEST(GhostClips, DoNotPropagateStartTime)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int g = cmds.createGhostClip(srcId, 8.0, 0);
    ASSERT_GT(g, 0);

    cmds.setClipStart(srcId, 1.0);

    // Ghost keeps its own startTime (8.0); only the source moved.
    EXPECT_DOUBLE_EQ(requireClip(engine, g).startBeat, 8.0);
    EXPECT_DOUBLE_EQ(requireClip(engine, srcId).startBeat, 1.0);
}

// Edits originating ON a ghost must not bounce back to the source or to
// sibling ghosts (re-entrancy guard: isPropagating_).

TEST(GhostClips, GhostEditsDoNotPropagate)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int g1 = cmds.createGhostClip(srcId, 4.0, 0);
    int g2 = cmds.createGhostClip(srcId, 8.0, 0);
    ASSERT_GT(g1, 0);
    ASSERT_GT(g2, 0);

    cmds.setClipGain(g1, 0.9f);

    EXPECT_FLOAT_EQ(static_cast<float>(requireClip(engine, g1).gain), 0.9f);
    // Source and sibling ghost are untouched.
    EXPECT_FLOAT_EQ(static_cast<float>(requireClip(engine, srcId).gain), 1.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(requireClip(engine, g2).gain), 1.0f);
}

// ─── MIDI note propagation (spec §2.4) ─────────────────────────────────

TEST(GhostClips, NewNoteOnSourcePropagatesToGhosts)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int g = cmds.createGhostClip(srcId, 8.0, 0);
    ASSERT_GT(g, 0);
    EXPECT_EQ(rawNoteCount(engine, g), 0);

    cmds.addNote(srcId, 60, 100, 0.0, 1.0);

    EXPECT_EQ(rawNoteCount(engine, g), 1);
    auto ghostNotes = engine.getReadModel().getNotes(g);
    ASSERT_EQ(ghostNotes.size(), 1u);
    EXPECT_EQ(ghostNotes[0].pitch, 60);
}

// Removing a note from the source should remove the matching note from
// ghosts. The implementation matches by noteID (see AudioEngine.cpp
// valueTreeChildRemoved). This test pins down the current contract.
TEST(GhostClips, RemoveNoteOnSourcePropagatesToGhosts)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int noteId = cmds.addNote(srcId, 60, 100, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    // Create the ghost AFTER the note exists. The ghost gets a copy of the
    // note with a FRESH noteID (createGhostClip re-mints IDs). The removal
    // propagation matches ghost notes against the *source* noteID, so this
    // will only work if the implementation matches on content. We assert
    // the desired spec behavior and let a failure surface the mismatch.
    int g = cmds.createGhostClip(srcId, 8.0, 0);
    ASSERT_GT(g, 0);
    ASSERT_EQ(rawNoteCount(engine, g), 1);

    cmds.removeNote(noteId);

    // Spec §2.4: the matching ghost note is removed by (noteNumber, startBeat).
    EXPECT_EQ(rawNoteCount(engine, g), 0)
        << "Expected ghost note removed after source note deletion";
}

// ─── Deletion propagation (spec §2.5) ──────────────────────────────────

TEST(GhostClips, DeletingSourceRemovesAllGhosts)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int g1 = cmds.createGhostClip(srcId, 4.0, 0);
    int g2 = cmds.createGhostClip(srcId, 8.0, 1);
    int other = cmds.addMidiClip(0, 12.0, 4.0, "Unrelated");
    ASSERT_GT(g1, 0);
    ASSERT_GT(g2, 0);
    ASSERT_GT(other, 0);

    cmds.removeClip(srcId);

    auto snap = engine.getReadModel().snapshot();
    std::vector<int> remaining;
    for (const auto& c : snap.clips)
        remaining.push_back(c.clipId);

    // Source + both ghosts gone; unrelated clip survives.
    for (int id : { srcId, g1, g2 })
        EXPECT_EQ(std::find(remaining.begin(), remaining.end(), id), remaining.end())
            << "clip " << id << " should have been removed";
    EXPECT_NE(std::find(remaining.begin(), remaining.end(), other), remaining.end());
}

TEST(GhostClips, DeletingGhostLeavesSourceAndSiblings)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int g1 = cmds.createGhostClip(srcId, 4.0, 0);
    int g2 = cmds.createGhostClip(srcId, 8.0, 0);
    ASSERT_GT(g1, 0);
    ASSERT_GT(g2, 0);

    cmds.removeClip(g1);

    auto snap = engine.getReadModel().snapshot();
    std::vector<int> remaining;
    for (const auto& c : snap.clips)
        remaining.push_back(c.clipId);

    EXPECT_NE(std::find(remaining.begin(), remaining.end(), srcId), remaining.end());
    EXPECT_NE(std::find(remaining.begin(), remaining.end(), g2), remaining.end());
    EXPECT_EQ(std::find(remaining.begin(), remaining.end(), g1), remaining.end());
}

// ─── paintClips ────────────────────────────────────────────────────────

TEST(PaintClips, SingleSourceTilesEndToEnd)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "PaintSrc");
    ASSERT_GT(srcId, 0);

    auto ids = cmds.paintClips({ srcId }, /*originBeat*/ 0.0, /*spacing*/ 4.0,
                               /*targetTrackIndex*/ 0, /*count*/ 3);
    ASSERT_EQ(ids.size(), 3u);

    // Each tile is a ghost placed at origin + (tile+1) * spacing.
    for (size_t i = 0; i < ids.size(); ++i)
    {
        auto c = requireClip(engine, ids[i]);
        EXPECT_TRUE(c.isGhost);
        EXPECT_EQ(c.ghostSourceId, srcId);
        EXPECT_DOUBLE_EQ(c.startBeat, (static_cast<double>(i) + 1.0) * 4.0)
            << "tile " << i << " at wrong beat";
    }
}

TEST(PaintClips, MultipleSourcesTileAsGroup)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Two source clips at beats 0 and 2 (offsets 0 and 2 within the group).
    int a = cmds.addMidiClip(0, 0.0, 2.0, "A");
    int b = cmds.addMidiClip(0, 2.0, 1.0, "B");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    auto ids = cmds.paintClips({ a, b }, 0.0, 4.0, 0, 2);
    ASSERT_EQ(ids.size(), 4u); // 2 tiles × 2 sources

    // Tile 0: A at 4+0=4, B at 4+2=6. Tile 1: A at 8+0=8, B at 8+2=10.
    auto c0 = requireClip(engine, ids[0]);
    auto c1 = requireClip(engine, ids[1]);
    auto c2 = requireClip(engine, ids[2]);
    auto c3 = requireClip(engine, ids[3]);
    EXPECT_DOUBLE_EQ(c0.startBeat, 4.0);
    EXPECT_DOUBLE_EQ(c1.startBeat, 6.0);
    EXPECT_DOUBLE_EQ(c2.startBeat, 8.0);
    EXPECT_DOUBLE_EQ(c3.startBeat, 10.0);
    for (const auto& c : { c0, c1, c2, c3 })
        EXPECT_TRUE(c.isGhost);
}

TEST(PaintClips, ZeroCountReturnsEmpty)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Src");
    auto ids = cmds.paintClips({ srcId }, 0.0, 4.0, 0, 0);
    EXPECT_TRUE(ids.empty());
}

TEST(PaintClips, InvalidTrackReturnsEmpty)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Src");
    auto ids = cmds.paintClips({ srcId }, 0.0, 4.0, 999, 3);
    EXPECT_TRUE(ids.empty());
}

TEST(PaintClips, PaintedGhostsInheritNotes)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Src");
    cmds.addNote(srcId, 60, 100, 0.0, 1.0);
    cmds.addNote(srcId, 67, 80, 1.0, 0.5);

    auto ids = cmds.paintClips({ srcId }, 0.0, 4.0, 0, 2);
    ASSERT_EQ(ids.size(), 2u);
    for (int id : ids)
    {
        auto notes = engine.getReadModel().getNotes(id);
        EXPECT_EQ(notes.size(), 2u);
    }
}

// ─── Undo ──────────────────────────────────────────────────────────────
// All mutations flow through the UndoManager, so Ctrl+Z must roll them back.

TEST(GhostClips, UndoCreateGhostClip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Source");
    int g = cmds.createGhostClip(srcId, 4.0, 0);
    ASSERT_GT(g, 0);

    cmds.undo(); // roll back the ghost creation
    auto snap = engine.getReadModel().snapshot();
    for (const auto& c : snap.clips)
        EXPECT_NE(c.clipId, g);

    cmds.redo();
    snap = engine.getReadModel().snapshot();
    bool found = false;
    for (const auto& c : snap.clips)
        if (c.clipId == g) found = true;
    EXPECT_TRUE(found);
}

TEST(PaintClips, UndoPaintRollsBackAllTiles)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int srcId = cmds.addMidiClip(0, 0.0, 4.0, "Src");
    ASSERT_GT(srcId, 0);
    // Capture the count AFTER adding the source — the source clip is outside
    // the paint transaction and must survive the undo.
    int baseline = static_cast<int>(engine.getReadModel().snapshot().clips.size());

    auto ids = cmds.paintClips({ srcId }, 0.0, 4.0, 0, 3);
    ASSERT_EQ(ids.size(), 3u);

    cmds.undo(); // paintClips wraps the batch in begin/endTransaction
    auto snap = engine.getReadModel().snapshot();
    for (int id : ids)
        for (const auto& c : snap.clips)
            EXPECT_NE(c.clipId, id);
    EXPECT_EQ(static_cast<int>(snap.clips.size()), baseline);
}
