#include "gtest/gtest.h"
#include "model/ProjectModel.h"

using namespace HDAW;

TEST(ProjectModel, SliceClipAtTime)
{
    ProjectModel model;
    auto track = model.getTrackListTree().getChild(0);  // assume default track exists
    auto clip = ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    track.getChildWithName(IDs::CLIP_LIST).addChild(clip, -1, &model.getUndoManager());
    int clipId = clip.getProperty(IDs::clipID);

    // Slice at 1.0 and 3.0
    auto slices = ProjectModel::sliceClipAtTimes(clip, {1.0, 3.0}, &model.getUndoManager());

    EXPECT_EQ(slices.size(), 3);
    EXPECT_DOUBLE_EQ(slices[0].getProperty(IDs::startTime), 0.0);
    EXPECT_DOUBLE_EQ(slices[0].getProperty(IDs::duration), 1.0);
    EXPECT_DOUBLE_EQ(slices[0].getProperty(IDs::offset), 0.0);

    EXPECT_DOUBLE_EQ(slices[1].getProperty(IDs::startTime), 1.0);
    EXPECT_DOUBLE_EQ(slices[1].getProperty(IDs::duration), 2.0);
    EXPECT_DOUBLE_EQ(slices[1].getProperty(IDs::offset), 1.0);

    EXPECT_DOUBLE_EQ(slices[2].getProperty(IDs::startTime), 3.0);
    EXPECT_DOUBLE_EQ(slices[2].getProperty(IDs::duration), 1.0);
    EXPECT_DOUBLE_EQ(slices[2].getProperty(IDs::offset), 3.0);

    // Original clip should be removed
    EXPECT_EQ(track.getChildWithName(IDs::CLIP_LIST).getNumChildren(), 3);
}