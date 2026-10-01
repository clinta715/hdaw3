// O2 fix (2026-09-30): the export_audio `trackIds` filter used to match
// POSITION inside the render copy's track list while every modern surface
// documents STABLE trackID (position i carries trackID i+1, so the values
// never coincide — `allocateTrackID()` floors at 1). `trackIds:[1]` therefore
// muted the kick (position 0) and exported the bass.
//
// These tests pin the ONE fix point — HDAW::applyTrackFilterToRenderCopy
// (src/common/RenderLaunch.h) matches the track node's `trackID` property —
// on a CONSTRUCTED tree where positions and trackIDs deliberately differ, and
// the all-or-nothing validation in launchProjectRender (an unknown id refuses
// the whole render, loudly, before ExportManager::startExport).

#include <gtest/gtest.h>

#include "common/RenderLaunch.h"
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

namespace {

// 5 tracks, none removed: position i carries trackID i+1. Filtering
// `trackIds:[2]` must unmute ONLY the track whose trackID == 2 (position 1)
// and mute + zero every other — the positional bug would have picked
// position 2 (trackID 3) instead.
TEST(RenderLaunchTrackFilter, MatchesStableTrackIdNotPosition)
{
    juce::ValueTree project(IDs::PROJECT);
    juce::ValueTree list(IDs::TRACK_LIST);
    for (int i = 0; i < 5; ++i)
    {
        juce::ValueTree t(IDs::TRACK);
        t.setProperty(IDs::trackID, i + 1, nullptr);
        t.setProperty(IDs::isMuted, false, nullptr);
        t.setProperty(IDs::isSoloed, i == 4, nullptr); // a stray solo must be cleared too
        t.setProperty(IDs::volume, 0.8, nullptr);
        list.addChild(t, -1, nullptr);
    }
    project.addChild(list, -1, nullptr);

    HDAW::applyTrackFilterToRenderCopy(project, {2});

    for (int i = 0; i < list.getNumChildren(); ++i)
    {
        auto t = list.getChild(i);
        const int trackID = static_cast<int>(t.getProperty(IDs::trackID));
        if (trackID == 2)
        {
            EXPECT_FALSE(static_cast<bool>(t.getProperty(IDs::isMuted)))
                << "trackID 2 must stay unmuted";
            EXPECT_NE(static_cast<double>(t.getProperty(IDs::volume, 1.0)), 0.0)
                << "the kept track keeps its volume";
        }
        else
        {
            EXPECT_TRUE(static_cast<bool>(t.getProperty(IDs::isMuted)))
                << "trackID " << trackID << " must be muted";
            EXPECT_EQ(static_cast<double>(t.getProperty(IDs::volume, 1.0)), 0.0)
                << "trackID " << trackID << " must be zeroed";
        }
        EXPECT_FALSE(static_cast<bool>(t.getProperty(IDs::isSoloed)))
            << "solos are cleared on every track";
    }
}

// Empty trackIds = no-op: the whole project renders, nothing is mutated.
TEST(RenderLaunchTrackFilter, EmptyFilterIsNoOp)
{
    juce::ValueTree project(IDs::PROJECT);
    juce::ValueTree list(IDs::TRACK_LIST);
    juce::ValueTree t(IDs::TRACK);
    t.setProperty(IDs::trackID, 1, nullptr);
    t.setProperty(IDs::isMuted, false, nullptr);
    t.setProperty(IDs::volume, 0.8, nullptr);
    list.addChild(t, -1, nullptr);
    project.addChild(list, -1, nullptr);

    HDAW::applyTrackFilterToRenderCopy(project, {});

    EXPECT_FALSE(static_cast<bool>(list.getChild(0).getProperty(IDs::isMuted)));
    EXPECT_EQ(static_cast<double>(list.getChild(0).getProperty(IDs::volume)), 0.8);
}

// A legacy positional id (0 — trackIDs start at 1) must fail LOUDLY before
// any render starts, naming the offending id. All-or-nothing: a mixed list
// (one valid + one unknown) is refused whole, no partial filter, no render.
TEST(RenderLaunchTrackFilter, UnknownTrackIdRefusesWholeRender)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int idxA = cmds.addTrack("Filter A", -1, -1, 0);
    const int idxB = cmds.addTrack("Filter B", -1, -1, 0);
    ASSERT_GE(idxA, 0);
    ASSERT_GE(idxB, 0);
    engine.drainPendingRoutingRebuild();

    auto launch = [&engine](const std::vector<int>& ids) {
        const QString outPath = QString::fromUtf8(
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("hdaw_render_launch_filter_refusal.wav")
                .getFullPathName().toRawUTF8());
        return HDAW::launchProjectRender(engine, outPath,
            44100.0, 16, HDAW::ExportManager::WAV, 0.0, 0.25, ids);
    };

    // trackID 0 does not exist (floor 1): refused, id named, nothing started.
    {
        auto r = launch({0});
        EXPECT_FALSE(r.started);
        EXPECT_NE(r.error.toStdString().find("0"), std::string::npos)
            << r.error.toStdString();
    }
    // Mixed valid + unknown: the WHOLE list is refused (unknown 999 named).
    {
        const int validID = static_cast<int>(engine.getProjectModel()
                                .getTrackListTree().getChild(idxA)
                                .getProperty(IDs::trackID));
        ASSERT_GE(validID, 1);
        auto r = launch({validID, 999});
        EXPECT_FALSE(r.started);
        EXPECT_NE(r.error.toStdString().find("999"), std::string::npos)
            << r.error.toStdString();
        // The valid id must not have been applied silently either: the live
        // tree is untouched (the filter only ever worked on a render copy).
        EXPECT_FALSE(static_cast<bool>(engine.getProjectModel().getTrackListTree()
                                          .getChild(idxB).getProperty(IDs::isMuted, false)))
            << "a refused filter must not mutate the live tree";
    }
}

// GAPPED ids: after track removals the stable trackIDs are non-contiguous
// (4 tracks, remove the 2nd → existing {1, 3, 4}). A valid `trackIds:[3]`
// must isolate trackID 3 — which sits at POSITION 1 after the splice, so the
// old positional match would have isolated trackID 4 instead. And a refusal
// must list the ACTUAL existing ids ({1, 3, 4}), never a 1..N range.
TEST(RenderLaunchTrackFilter, GappedTrackIdsFilterAndRefusalList)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // A fresh project starts with ZERO tracks; add 4 → ids {1,2,3,4}.
    for (const char* n : {"G1", "G2", "G3", "G4"})
        ASSERT_GE(cmds.addTrack(n, -1, -1, 0), 0);
    engine.drainPendingRoutingRebuild();
    // Remove the 2nd (position 1, trackID 2) → existing {1, 3, 4}.
    const auto removed = cmds.removeTrack(1);
    ASSERT_EQ(removed.removed, 1);
    engine.drainPendingRoutingRebuild();

    const auto trackIDAt = [&engine](int pos) {
        return static_cast<int>(engine.getProjectModel().getTrackListTree()
                                   .getChild(pos).getProperty(IDs::trackID));
    };
    ASSERT_EQ(trackIDAt(0), 1);
    ASSERT_EQ(trackIDAt(1), 3);
    ASSERT_EQ(trackIDAt(2), 4);

    // Valid request for trackID 3: only THAT track stays unmuted on the copy.
    {
        juce::ValueTree copy = engine.getProjectModel().getTree().createCopy();
        HDAW::applyTrackFilterToRenderCopy(copy, {3});
        auto list = copy.getChildWithName(IDs::TRACK_LIST);
        ASSERT_EQ(list.getNumChildren(), 3);
        for (int i = 0; i < list.getNumChildren(); ++i)
        {
            auto t = list.getChild(i);
            const int id = static_cast<int>(t.getProperty(IDs::trackID));
            EXPECT_EQ(static_cast<bool>(t.getProperty(IDs::isMuted)), id != 3)
                << "trackID " << id << " mute flag wrong";
            if (id != 3)
                EXPECT_EQ(static_cast<double>(t.getProperty(IDs::volume, 1.0)), 0.0);
        }
        // The positional trap, spelled out: position 1 holds trackID 3, so a
        // `trackIds:[3]`-is-positional reading would have kept trackID 4.
        EXPECT_FALSE(static_cast<bool>(list.getChild(1).getProperty(IDs::isMuted)));
        EXPECT_TRUE(static_cast<bool>(list.getChild(2).getProperty(IDs::isMuted)));
    }

    // Refusal for a now-removed id lists the ACTUAL existing ids.
    {
        const QString outPath = QString::fromUtf8(
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("hdaw_render_launch_gapped_refusal.wav")
                .getFullPathName().toRawUTF8());
        auto r = HDAW::launchProjectRender(engine, outPath,
            44100.0, 16, HDAW::ExportManager::WAV, 0.0, 0.25, {2});
        EXPECT_FALSE(r.started);
        EXPECT_NE(r.error.toStdString().find("2"), std::string::npos)
            << r.error.toStdString();
        EXPECT_NE(r.error.toStdString().find("existing: 1, 3, 4"), std::string::npos)
            << r.error.toStdString();
        EXPECT_EQ(r.error.toStdString().find("1.."), std::string::npos)
            << "the false 1..N range must not come back";
    }
}

// A track node WITHOUT the trackID property (malformed/legacy) must not mint
// a phantom id 0: `[0]` still refuses naming 0, and the refusal's
// existing-ids list never contains 0 (trackIDs are floor-1 by contract).
TEST(RenderLaunchTrackFilter, MalformedTrackWithoutIdDoesNotMintZero)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    // A fresh project starts with ZERO tracks: the first track is malformed,
    // the second stays well-formed so the refusal can name its existing id.
    ASSERT_GE(cmds.addTrack("Malformed", -1, -1, 0), 0);
    ASSERT_GE(cmds.addTrack("WellFormed", -1, -1, 0), 0);
    engine.drainPendingRoutingRebuild();
    // Strip the property off the FIRST track (trackID 1) — malformed node.
    engine.getProjectModel().getTrackListTree().getChild(0)
        .removeProperty(IDs::trackID, nullptr);

    const QString outPath = QString::fromUtf8(
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("hdaw_render_launch_malformed_refusal.wav")
            .getFullPathName().toRawUTF8());
    auto r = HDAW::launchProjectRender(engine, outPath,
        44100.0, 16, HDAW::ExportManager::WAV, 0.0, 0.25, {0});
    EXPECT_FALSE(r.started) << "[0] must stay refused";
    EXPECT_NE(r.error.toStdString().find("0"), std::string::npos)
        << r.error.toStdString();
    // The existing-ids list names only the well-formed track; never 0.
    const std::string err = r.error.toStdString();
    EXPECT_NE(err.find("existing: 2"), std::string::npos) << err;
    EXPECT_EQ(err.find("existing: 0"), std::string::npos) << err;
    EXPECT_EQ(err.find(", 0,"), std::string::npos) << err;
}

} // namespace
