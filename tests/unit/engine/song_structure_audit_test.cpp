// SongStructureAudit — arrangement-variety gates (Mix Verifier boredom/
// static-span contract). PURE tree analysis: no engine, no audio. Builds a
// minimal TRACK_LIST ValueTree by hand and audits it against plan sections.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

#include "engine/SongStructureAudit.h"
#include "model/ProjectModel.h"

namespace {

using HDAW::SongStructureAudit;
using SongPlanData = ProjectCommands::SongPlanData;

SongPlanData plan3()
{
    SongPlanData p;         // 120 BPM -> 0.5 s/beat
    p.bpm = 120.0;
    p.totalBars = 32;
    p.sections = {
        { "intro", "intro", 8, 0.0, 32.0 },
        { "build", "build", 8, 32.0, 64.0 },
        { "dropA", "mainA", 16, 64.0, 128.0 } };
    return p;
}

// Tracks with WHOLE-project clips (0..64 s = 32 bars at 120 BPM) so every
// section hears every track — the composition-writer convention.
juce::ValueTree trackListWith(std::initializer_list<const char*> roles)
{
    auto list = juce::ValueTree(IDs::TRACK_LIST);
    for (const char* role : roles)
    {
        auto t = juce::ValueTree(IDs::TRACK);
        t.setProperty(IDs::name, role, nullptr);
        t.setProperty(IDs::layerRole, role, nullptr);
        auto cl = juce::ValueTree(IDs::CLIP_LIST);
        auto c = juce::ValueTree(IDs::CLIP);
        c.setProperty(IDs::startTime, 0.0, nullptr);
        c.setProperty(IDs::duration, 64.0, nullptr);
        cl.addChild(c, -1, nullptr);
        t.addChild(cl, -1, nullptr);
        list.addChild(t, -1, nullptr);
    }
    return list;
}

} // namespace

TEST(SongStructureAudit, BoringBassHatBedFlagsSpans)
{
    auto audit = HDAW::auditSongStructure(trackListWith({ "kick", "bass", "hats" }), plan3(), 0.0);
    ASSERT_TRUE(audit.hasPlan);
    // intro (8) and dropA (16) are non-build sparse sections -> two runs;
    // the build section is excluded (tension marker).
    ASSERT_EQ(audit.spans.size(), 2u);
    EXPECT_EQ(audit.spans[0].flag, "bass-hat-only");
    EXPECT_EQ(audit.spans[0].startName, "intro");
    EXPECT_EQ(audit.spans[0].bars, 8);
    EXPECT_EQ(audit.spans[1].startName, "dropA");
    EXPECT_EQ(audit.spans[1].bars, 16);
    // Drop gates: no backbeat and no motif in the first drop.
    EXPECT_FALSE(audit.dropNamesMissingBackbeat.empty());
    EXPECT_EQ(audit.dropNamesMissingBackbeat.size(), 1u);
    EXPECT_FALSE(audit.firstDropHasMotif);
    EXPECT_FALSE(audit.ok);
}

TEST(SongStructureAudit, BuildIsTensionNotFlagged)
{
    SongPlanData p;         // intro(8) + build(8) only
    p.bpm = 120.0;
    p.totalBars = 16;
    p.sections = { { "intro", "intro", 8, 0.0, 32.0 },
                   { "build", "build", 8, 32.0, 64.0 } };
    auto audit = HDAW::auditSongStructure(trackListWith({ "kick", "bass", "hats" }), p, 0.0);
    ASSERT_EQ(audit.spans.size(), 1u);
    EXPECT_EQ(audit.spans[0].startName, "intro");  // build not in the run
}

TEST(SongStructureAudit, ShortSectionBelowThresholdPasses)
{
    SongPlanData p;
    p.bpm = 120.0;
    p.totalBars = 4;
    p.sections = { { "intro", "intro", 4, 0.0, 16.0 } };
    auto audit = HDAW::auditSongStructure(trackListWith({ "kick" }), p, 0.0);
    EXPECT_TRUE(audit.spans.empty());
    EXPECT_TRUE(audit.ok);   // nothing to judge
}

TEST(SongStructureAudit, HatsOnlyAndSilenceClassified)
{
    SongPlanData p;
    p.bpm = 120.0;
    p.totalBars = 32;
    p.sections = { { "mainA", "mainA", 16, 0.0, 64.0 },
                   { "mainB", "mainB", 16, 64.0, 128.0 } };
    // Texture only -> hats-only in both sections; a track with no clips adds
    // nothing (silence only when NO role sounds anywhere).
    auto list = trackListWith({ "hats", "shaker" });
    auto silent = juce::ValueTree(IDs::TRACK);
    silent.setProperty(IDs::name, "silent", nullptr);
    silent.setProperty(IDs::layerRole, "other", nullptr);
    list.addChild(silent, -1, nullptr);

    auto audit = HDAW::auditSongStructure(list, p, 0.0);
    ASSERT_EQ(audit.spans.size(), 1u);
    EXPECT_EQ(audit.spans[0].flag, "hats-only");
    EXPECT_EQ(audit.spans[0].bars, 32);
}

TEST(SongStructureAudit, FullArrangementPassesAllGates)
{
    auto audit = HDAW::auditSongStructure(
        trackListWith({ "kick", "bass", "hats", "clap", "lead" }), plan3(), 0.0);
    EXPECT_TRUE(audit.spans.empty());
    EXPECT_TRUE(audit.dropNamesMissingBackbeat.empty());
    EXPECT_TRUE(audit.firstDropHasMotif);
    EXPECT_EQ(audit.firstDropName, "dropA");
    EXPECT_TRUE(audit.ok);
    // Per-section role intelligence surfaces in the table.
    ASSERT_EQ(audit.sections.size(), 3u);
    EXPECT_TRUE(audit.sections[2].hasMelodic);
    EXPECT_TRUE(audit.sections[2].hasBackbeat);
}

TEST(SongStructureAudit, TrackNameFallbackWhenNoHandoff)
{
    auto list = juce::ValueTree(IDs::TRACK_LIST);
    // No layerRole properties — roles come from names.
    for (const char* name : { "kick", "snare", "acid lead" })
    {
        auto t = juce::ValueTree(IDs::TRACK);
        t.setProperty(IDs::name, name, nullptr);
        auto cl = juce::ValueTree(IDs::CLIP_LIST);
        auto c = juce::ValueTree(IDs::CLIP);
        c.setProperty(IDs::startTime, 0.0, nullptr);
        c.setProperty(IDs::duration, 64.0, nullptr);
        cl.addChild(c, -1, nullptr);
        t.addChild(cl, -1, nullptr);
        list.addChild(t, -1, nullptr);
    }
    SongPlanData p;
    p.bpm = 120.0;
    p.totalBars = 8;
    p.sections = { { "mainA", "mainA", 8, 0.0, 32.0 } };
    auto audit = HDAW::auditSongStructure(list, p, 0.0);
    ASSERT_EQ(audit.sections.size(), 1u);
    // backbeat (snare) + melodic (lead) present -> no sparse flag at all.
    EXPECT_TRUE(audit.spans.empty());
    EXPECT_TRUE(audit.sections[0].hasBackbeat);
    EXPECT_TRUE(audit.sections[0].hasMelodic);
    EXPECT_TRUE(audit.ok);
}

// Structural proxy for the drop-vs-build loudness gate: a drop carrying FEWER
// sounding roles than the build before it is the classic quiet payoff.
TEST(SongStructureAudit, DropThinnerThanBuildFlagged)
{
    SongPlanData p;
    p.bpm = 120.0;              // 1 bar = 2 s; build [0,16 s), drop [16,32 s)
    p.totalBars = 16;
    p.sections = { { "build", "build", 8, 0.0, 32.0 },
                   { "dropA", "mainA", 8, 32.0, 64.0 } };
    auto list = juce::ValueTree(IDs::TRACK_LIST);
    auto addTrack = [&list](const char* role, double clipStart, double clipDur) {
        auto t = juce::ValueTree(IDs::TRACK);
        t.setProperty(IDs::name, role, nullptr);
        t.setProperty(IDs::layerRole, role, nullptr);
        auto cl = juce::ValueTree(IDs::CLIP_LIST);
        auto c = juce::ValueTree(IDs::CLIP);
        c.setProperty(IDs::startTime, clipStart, nullptr);
        c.setProperty(IDs::duration, clipDur, nullptr);
        cl.addChild(c, -1, nullptr);
        t.addChild(cl, -1, nullptr);
        list.addChild(t, -1, nullptr);
    };
    addTrack("kick", 0.0, 32.0);   // the whole track
    addTrack("bass", 0.0, 16.0);   // build only
    addTrack("hats", 0.0, 16.0);   // build only
    addTrack("lead", 0.0, 16.0);   // build only

    auto audit = HDAW::auditSongStructure(list, p, 0.0);
    ASSERT_EQ(audit.sections.size(), 2u);
    ASSERT_FALSE(audit.dropNamesThinnerThanBuild.empty());
    EXPECT_EQ(audit.dropNamesThinnerThanBuild[0], "dropA");
    EXPECT_FALSE(audit.ok);
}

// Boundary hygiene: segments are half-open and cell-filled clips share the exact
// boundary values, so a clip ENDING at the next section's start must not count
// there (float rounding used to leak it — buildC reported every project role).
TEST(SongStructureAudit, ClipEndingAtNextSectionStartDoesNotLeak)
{
    SongPlanData p;
    p.bpm = 120.0;                  // 1 bar = 2 s; 8 bars = 16 s
    p.totalBars = 16;
    p.sections = { { "intro", "intro", 8, 0.0, 32.0 },
                   { "dropA", "mainA", 8, 32.0, 64.0 } };
    auto list = juce::ValueTree(IDs::TRACK_LIST);
    auto addClipTrack = [&list](const char* role, double start, double dur) {
        auto t = juce::ValueTree(IDs::TRACK);
        t.setProperty(IDs::name, role, nullptr);
        t.setProperty(IDs::layerRole, role, nullptr);
        auto cl = juce::ValueTree(IDs::CLIP_LIST);
        auto c = juce::ValueTree(IDs::CLIP);
        c.setProperty(IDs::startTime, start, nullptr);
        c.setProperty(IDs::duration, dur, nullptr);
        cl.addChild(c, -1, nullptr);
        t.addChild(cl, -1, nullptr);
        list.addChild(t, -1, nullptr);
    };
    // Intro-only clip ending EXACTLY at dropA's start (16 s), plus a drop clip
    // starting exactly at 16 s (must be included there).
    addClipTrack("pad", 0.0, 16.0);
    addClipTrack("kick", 16.0, 16.0);

    auto audit = HDAW::auditSongStructure(list, p, 0.0);
    ASSERT_EQ(audit.sections.size(), 2u);
    const auto introRoles = audit.sections[0].soundingRoles;
    const auto dropRoles  = audit.sections[1].soundingRoles;
    EXPECT_EQ(std::find(introRoles.begin(), introRoles.end(), "pad") != introRoles.end(), true);
    EXPECT_EQ(std::find(introRoles.begin(), introRoles.end(), "kick") != introRoles.end(), false)
        << "a clip starting at the section end must not count in the earlier section";
    EXPECT_EQ(std::find(dropRoles.begin(), dropRoles.end(), "kick") != dropRoles.end(), true)
        << "a clip starting exactly at the section start must count";
    EXPECT_EQ(std::find(dropRoles.begin(), dropRoles.end(), "pad") != dropRoles.end(), false)
        << "a clip ending exactly at the section start must NOT count";
}

TEST(SongStructureAudit, NoPlanIsReported)
{
    auto audit = HDAW::auditSongStructure(trackListWith({ "kick" }), SongPlanData{}, 0.0);
    EXPECT_FALSE(audit.hasPlan);
    EXPECT_FALSE(audit.ok);
}
