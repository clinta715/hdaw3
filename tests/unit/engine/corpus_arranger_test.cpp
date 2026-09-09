#include <gtest/gtest.h>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "engine/CorpusArranger.h"
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"
#include "frontend/FrontendRouter.h"

// CorpusArranger gates: deterministic planner; structural invariants
// (contiguous, exactly-filling, capped intros); forced axes respected;
// score generation writes mapped roles; RPC round-trip determinism.
// Mirrors the psytrance_markov_test structure for this generator.

namespace {

bool sectionNamed(const HDAW::CorpusPlan& p, const char* name)
{
    for (const auto& s : p.sections)
        if (s.name == name)
            return true;
    return false;
}

bool roleInSection(const HDAW::CorpusPlan& p, const char* sec, const char* role)
{
    for (const auto& s : p.sections)
        if (s.name == sec)
            for (const auto& r : s.roles)
                if (r == role)
                    return true;
    return false;
}

} // namespace

TEST(CorpusArranger, DeterministicForSeed)
{
    HDAW::CorpusOptions a;
    a.seed = 7; a.bars = 95; a.breakdown = 1; a.lateNovelty = 1; a.noveltyRole = "lead";
    const auto p1 = HDAW::CorpusArranger::samplePlan(a);
    const auto p2 = HDAW::CorpusArranger::samplePlan(a);
    EXPECT_EQ(p1.totalBars, p2.totalBars);
    EXPECT_EQ(p1.sections.size(), p2.sections.size());
    for (size_t i = 0; i < p1.sections.size(); ++i)
    {
        EXPECT_EQ(p1.sections[i].name, p2.sections[i].name);
        EXPECT_EQ(p1.sections[i].barStart, p2.sections[i].barStart);
        EXPECT_EQ(p1.sections[i].bars, p2.sections[i].bars);
    }
    EXPECT_EQ(p1.constBass, p2.constBass);
    EXPECT_EQ(p1.lateNovelty, p2.lateNovelty);
    EXPECT_EQ(p1.breakdown, p2.breakdown);
    EXPECT_EQ(p1.introMode, p2.introMode);
}

TEST(CorpusArranger, PlanFillsSongContiguously)
{
    for (int seed = 1; seed <= 12; ++seed)
    {
        HDAW::CorpusOptions o;
        o.seed = seed;
        const auto p = HDAW::CorpusArranger::samplePlan(o);
        int cur = 0;
        for (const auto& s : p.sections)
        {
            EXPECT_EQ(s.barStart, cur) << "seed " << seed << " section " << s.name;
            EXPECT_GE(s.bars, 2) << "seed " << seed << " section " << s.name;
            cur = s.barStart + s.bars;
        }
        EXPECT_EQ(cur, p.totalBars) << "seed " << seed;
        ASSERT_FALSE(p.sections.empty());
        EXPECT_LE(p.sections.front().bars, 24) << "seed " << seed;
    }
}

TEST(CorpusArranger, ForcedAxesRespected)
{
    HDAW::CorpusOptions o;
    o.seed = 3; o.bars = 96; o.breakdown = 1; o.lateNovelty = 1; o.noveltyRole = "lead";
    o.introMode = 2; o.constBass = 1; o.lengthMode = 2;
    const auto p = HDAW::CorpusArranger::samplePlan(o);
    EXPECT_TRUE(p.breakdown);
    EXPECT_TRUE(p.lateNovelty);
    EXPECT_TRUE(p.constBass);
    EXPECT_EQ(p.noveltyRole, "lead");
    EXPECT_EQ(p.introMode, "midIntro");
    EXPECT_EQ(p.lengthMode, "extended");
    EXPECT_TRUE(sectionNamed(p, "minibreak"));
    EXPECT_TRUE(roleInSection(p, "dropB", "lead"));
}

TEST(CorpusArranger, GenerateScoreWritesMappedRoles)
{
    HDAW::CorpusParams par;
    par.seed = 5; par.totalBars = 48; par.keyRoot = 4; par.scaleMode = 1;
    par.kick = 0; par.bass = 1; par.hat = 2; par.arp = 3; par.stab = 4; par.pad = 5;
    par.snare = 6; par.clap = 7;
    const auto s1 = HDAW::CorpusArranger::generate(par);
    EXPECT_TRUE(s1.error.empty()) << s1.error;
    ASSERT_GT(s1.clips.size(), 0u);
    EXPECT_DOUBLE_EQ(s1.totalBeats, 48.0 * 4.0);
    bool kick = false;
    for (const auto& c : s1.clips)
        if (c.role == "kick")
        {
            kick = true;
            ASSERT_GT(c.notes.size(), 0u);
            break;
        }
    EXPECT_TRUE(kick);
    const auto s2 = HDAW::CorpusArranger::generate(par);
    ASSERT_EQ(s1.clips.size(), s2.clips.size());
    for (size_t i = 0; i < s1.clips.size(); ++i)
        EXPECT_EQ(s1.clips[i].notes.size(), s2.clips[i].notes.size())
            << "clip " << i << " role " << s1.clips[i].role;
}

TEST(CorpusArranger, RpcRoundTripDeterministic)
{
    AudioEngine engine;
    engine.initialize();
    // Mirror the live 16-track palette project before generating (palette
    // indices 3..13 must be valid).
    for (int i = 0; i < 14; ++i)
    {
        auto t = juce::ValueTree(IDs::TRACK);
        t.setProperty(IDs::name, juce::String("T") + juce::String(i), nullptr);
        t.addChild(juce::ValueTree(IDs::CLIP_LIST), -1, nullptr);
        engine.getProjectModel().getTrackListTree().addChild(t, -1, nullptr);
    }
    const QJsonObject args{{"seed", 141}, {"bars", 128}, {"keyRoot", 4}, {"scaleMode", 1},
        {"lateNovelty", true}, {"noveltyRole", "lead"},
        {"paletteTrackIds", QJsonObject{{"kick", 3}, {"bass", 4}, {"hat", 5},
                                        {"snare", 6}, {"clap", 7}, {"arp", 8},
                                        {"stab", 9}, {"pad", 10}, {"lead", 11},
                                        {"riser", 12}, {"down", 13}}}};
    const auto r1 = frontend::dispatch(engine, "composition.generateArrangementCorpus", args);
    ASSERT_FALSE(r1.isError);
    const auto o1 = r1.payload.toObject();
    if (o1.contains("error"))
        FAIL() << "command error: " << o1.value("error").toString().toStdString();
    EXPECT_DOUBLE_EQ(o1.value("totalBeats").toDouble(), 128.0 * 4.0);
    EXPECT_TRUE(o1.contains("plan"));
    EXPECT_GT(o1.value("clips").toArray().size(), 0);
    // The clips must ALSO be attached to the palette tracks in the tree
    // (regression: result was populated but clips never added to CLIP_LIST).
    {
        int treeClips = 0;
        const auto tl = engine.getProjectModel().getTrackListTree();
        for (int t = 0; t < tl.getNumChildren(); ++t)
            treeClips += tl.getChild(t).getChildWithName(IDs::CLIP_LIST).getNumChildren();
        EXPECT_GT(treeClips, 0) << "corpus clips not attached to tracks";
        // Per-role tree note counts (regression: melodic/drum roles were
        // populated in generate() but empty once attached in the command).
        std::map<std::string, int> roleNotes;
        for (int t = 0; t < tl.getNumChildren(); ++t)
        {
            const auto cl = tl.getChild(t).getChildWithName(IDs::CLIP_LIST);
            for (int i = 0; i < cl.getNumChildren(); ++i)
            {
                auto name = cl.getChild(i).getProperty(IDs::name).toString().toStdString();
                const auto nl = cl.getChild(i).getChildWithName(IDs::MIDI_NOTE_LIST);
                roleNotes[name] += nl.isValid() ? nl.getNumChildren() : 0;
            }
        }
        EXPECT_GT(roleNotes[std::string("Corpbass")], 0) << "bass notes lost in attach path";
        EXPECT_GT(roleNotes[std::string("Corphats")], 0) << "hats notes lost in attach path";
        EXPECT_GT(roleNotes[std::string("Corparp")], 0) << "arp notes lost in attach path";
        EXPECT_GT(roleNotes[std::string("Corppad")], 0) << "pad notes lost in attach path";
        EXPECT_GT(roleNotes[std::string("Corpkick")], 0) << "kick notes lost in attach path";
        EXPECT_GT(roleNotes[std::string("Corparp")], 0) << "arp notes lost in attach path";
        EXPECT_GT(roleNotes[std::string("Corppad")], 0) << "pad notes lost in attach path";
    }
    const auto r2 = frontend::dispatch(engine, "composition.generateArrangementCorpus", args);
    ASSERT_FALSE(r2.isError);
    const auto o2 = r2.payload.toObject();
    EXPECT_EQ(o1.value("clips").toArray().size(), o2.value("clips").toArray().size());
    EXPECT_EQ(o1.value("plan").toObject().value("bars").toInt(),
              o2.value("plan").toObject().value("bars").toInt());
}
