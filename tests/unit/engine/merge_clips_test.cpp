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
    return -1;
}

bool clipExists(AudioEngine& engine, int clipId)
{
    auto snap = engine.getReadModel().snapshot();
    for (const auto& c : snap.clips)
        if (c.clipId == clipId) return true;
    return false;
}

} // namespace

TEST(MergeClips, TwoContiguousClipsMergeCorrectly)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int a = cmds.addMidiClip(1, 0.0, 4.0, "A");
    int b = cmds.addMidiClip(1, 4.0, 4.0, "B");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    cmds.addNote(a, 60, 100, 0.0, 1.0);
    cmds.addNote(a, 62, 90, 1.0, 0.5);
    cmds.addNote(b, 64, 80, 0.0, 1.0);

    int merged = cmds.mergeClips({ a, b });
    ASSERT_GT(merged, 0);
    EXPECT_NE(merged, a);
    EXPECT_NE(merged, b);

    auto mc = requireClip(engine, merged);
    EXPECT_DOUBLE_EQ(mc.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(mc.durationBeats, 8.0);

    EXPECT_EQ(rawNoteCount(engine, merged), 3);
    EXPECT_FALSE(clipExists(engine, a));
    EXPECT_FALSE(clipExists(engine, b));
}

TEST(MergeClips, ClipsWithGapMergeAndReoffsetNotes)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int a = cmds.addMidiClip(1, 0.0, 4.0, "A");
    int b = cmds.addMidiClip(1, 8.0, 4.0, "B");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    cmds.addNote(a, 60, 100, 2.0, 1.0);
    cmds.addNote(b, 67, 80, 0.0, 0.5);

    int merged = cmds.mergeClips({ a, b });
    ASSERT_GT(merged, 0);

    auto mc = requireClip(engine, merged);
    EXPECT_DOUBLE_EQ(mc.startBeat, 0.0);
    EXPECT_DOUBLE_EQ(mc.durationBeats, 12.0);

    EXPECT_EQ(rawNoteCount(engine, merged), 2);
    EXPECT_FALSE(clipExists(engine, a));
    EXPECT_FALSE(clipExists(engine, b));

    auto notes = engine.getReadModel().getNotes(merged);
    ASSERT_EQ(notes.size(), 2u);
    bool found60 = false, found67 = false;
    for (const auto& n : notes)
    {
        if (n.pitch == 60)
        {
            found60 = true;
            EXPECT_DOUBLE_EQ(n.startBeat, 2.0);
        }
        if (n.pitch == 67)
        {
            found67 = true;
            EXPECT_DOUBLE_EQ(n.startBeat, 8.0);
        }
    }
    EXPECT_TRUE(found60);
    EXPECT_TRUE(found67);
}

TEST(MergeClips, RejectsFewerThanTwoClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int a = cmds.addMidiClip(1, 0.0, 4.0, "Solo");
    ASSERT_GT(a, 0);

    EXPECT_EQ(cmds.mergeClips({}), -1);
    EXPECT_EQ(cmds.mergeClips({ a }), -1);
}

TEST(MergeClips, RejectsMixedTracks)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int a = cmds.addMidiClip(1, 0.0, 4.0, "OnTrack1");
    int b = cmds.addMidiClip(0, 0.0, 4.0, "OnTrack0");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    EXPECT_EQ(cmds.mergeClips({ a, b }), -1);
    EXPECT_TRUE(clipExists(engine, a));
    EXPECT_TRUE(clipExists(engine, b));
}

TEST(MergeClips, RejectsNonMidiClips)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int audioClip = cmds.addAudioClip(0, 0.0, 4.0, "test.wav", "Audio");
    int midiClip = cmds.addMidiClip(1, 0.0, 4.0, "Midi");
    ASSERT_GT(audioClip, 0);
    ASSERT_GT(midiClip, 0);

    EXPECT_EQ(cmds.mergeClips({ audioClip, midiClip }), -1);
    EXPECT_TRUE(clipExists(engine, audioClip));
    EXPECT_TRUE(clipExists(engine, midiClip));
}

TEST(MergeClips, UndoRestoresOriginals)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int a = cmds.addMidiClip(1, 0.0, 4.0, "A");
    int b = cmds.addMidiClip(1, 4.0, 4.0, "B");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    cmds.addNote(a, 60, 100, 0.0, 1.0);
    cmds.addNote(b, 64, 80, 0.0, 1.0);

    int merged = cmds.mergeClips({ a, b });
    ASSERT_GT(merged, 0);
    EXPECT_FALSE(clipExists(engine, a));
    EXPECT_FALSE(clipExists(engine, b));

    cmds.undo();

    EXPECT_TRUE(clipExists(engine, a));
    EXPECT_TRUE(clipExists(engine, b));
    EXPECT_FALSE(clipExists(engine, merged));

    EXPECT_EQ(rawNoteCount(engine, a), 1);
    EXPECT_EQ(rawNoteCount(engine, b), 1);
}
