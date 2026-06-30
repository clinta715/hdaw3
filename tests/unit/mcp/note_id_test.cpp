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
    // Track 1 is "Synth" (MIDI); track 0 is "Track 1" (audio) with an empty
    // CLIP_LIST per the AGENTS.md "Default project should not reference
    // non-existent sample files" rule.
    auto nl = m.getTrackListTree().getChild(1).getChildWithName(IDs::CLIP_LIST)
                  .getChild(0).getChildWithName(IDs::MIDI_NOTE_LIST);
    nl.getChild(0).removeProperty(IDs::noteID, nullptr);
    EXPECT_FALSE(nl.getChild(0).hasProperty(IDs::noteID));
    m.scanAndSyncNoteIDs();
    EXPECT_TRUE(nl.getChild(0).hasProperty(IDs::noteID));
}
