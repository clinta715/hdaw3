#include <gtest/gtest.h>
#include "model/ProjectModel.h"

TEST(NoteID, AllocatesUniqueIDs) {
    int a = ProjectModel::allocateNoteID();
    int b = ProjectModel::allocateNoteID();
    int c = ProjectModel::allocateNoteID();
    EXPECT_NE(a, b); EXPECT_NE(b, c); EXPECT_NE(a, c);
}

TEST(NoteID, CreateMidiNoteAssignsID) {
    ProjectModel m;
    m.createDefaultProject();
    // The default project now ships empty; add a MIDI clip with a note so the
    // walk below has something to find.
    auto clip = ProjectModel::createMidiClipEmpty("T", 0.0, 1.0);
    clip.getChildWithName(IDs::MIDI_NOTE_LIST)
        .addChild(ProjectModel::createMidiNote(60, 0.8f, 0.0, 1.0), -1, nullptr);
    m.getTrackListTree().getChild(0)
        .getChildWithName(IDs::CLIP_LIST)
        .addChild(clip, -1, nullptr);

    auto trackList = m.getTrackListTree();
    int found = 0;
    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto cl = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        for (int c = 0; c < cl.getNumChildren(); ++c) {
            auto nl = cl.getChild(c).getChildWithName(IDs::MIDI_NOTE_LIST);
            for (int n = 0; n < nl.getNumChildren(); ++n) {
                EXPECT_TRUE(nl.getChild(n).hasProperty(IDs::noteID));
                ++found;
            }
        }
    }
    EXPECT_GT(found, 0);
}

TEST(NoteID, ScanAndSyncAssignsMissing) {
    ProjectModel m;
    m.createDefaultProject();
    // The default project now ships empty; add a MIDI clip with one note to
    // track 0, then strip its noteID and verify scanAndSync restores it.
    auto clip = ProjectModel::createMidiClipEmpty("T", 0.0, 1.0);
    auto nl = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    nl.addChild(ProjectModel::createMidiNote(60, 0.8f, 0.0, 1.0), -1, nullptr);
    m.getTrackListTree().getChild(0)
        .getChildWithName(IDs::CLIP_LIST)
        .addChild(clip, -1, nullptr);

    auto noteList = m.getTrackListTree().getChild(0)
                        .getChildWithName(IDs::CLIP_LIST)
                        .getChild(0)
                        .getChildWithName(IDs::MIDI_NOTE_LIST);
    noteList.getChild(0).removeProperty(IDs::noteID, nullptr);
    EXPECT_FALSE(noteList.getChild(0).hasProperty(IDs::noteID));
    m.scanAndSyncNoteIDs();
    EXPECT_TRUE(noteList.getChild(0).hasProperty(IDs::noteID));
}
