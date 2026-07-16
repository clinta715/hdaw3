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

    // Original clip should be removed and replaced by exactly the 3 slices.
    // A previous bug inserted slices BEFORE removing the original, which
    // pushed the original down the list and made removeChild(clipIndex) delete
    // the first slice instead — leaving the original clip plus only the tail
    // slices. The child-count check alone is a false-pass against that bug
    // (3 children either way), so also assert none of the resulting children
    // carry the original clip's ID.
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    EXPECT_EQ(clipList.getNumChildren(), 3);
    for (int i = 0; i < clipList.getNumChildren(); ++i)
    {
        int id = static_cast<int>(clipList.getChild(i).getProperty(IDs::clipID));
        EXPECT_NE(id, clipId)
            << "the original (un-sliced) clip must not survive slicing";
    }

    // And the 3 slices must tile the original span with no overlap/gap.
    EXPECT_DOUBLE_EQ((double)clipList.getChild(0).getProperty(IDs::startTime), 0.0);
    double lastStart = clipList.getChild(2).getProperty(IDs::startTime);
    double lastDur   = clipList.getChild(2).getProperty(IDs::duration);
    EXPECT_DOUBLE_EQ(lastStart + lastDur, 4.0);
}