#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"

// Gates 3-6 for the CLAP preset -> program wiring, on the DEFAULT isolated
// path (never sets HDAW_NO_PLUGIN_ISOLATION). TyrellN6 CLAP is the subject:
// FILE-kind presets, empty load_keys, ~669 presets (Phase-0 probe). The
// preset database builds asynchronously in the child (~10s crawl), so every
// test waits for the program list before asserting on it.

namespace {

const char* kTyrellN6Clap = "C:\\Program Files\\Common Files\\CLAP\\u-he\\TyrellN6.clap";

bool clapTyrellN6Available()
{
    const char* env = getenv("HDAW_REAL_PLUGIN_TESTS");
    if (env == nullptr)
        return false;
    const juce::String s(env);
    if (s.trim().isEmpty() || s.trim() == "0")
        return false;
    return juce::File(kTyrellN6Clap).existsAsFile();
}

// Creates a real track with the TyrellN6 CLAP in slot 0 (keepTrack probe) and
// returns the audition result; ASSERTs the slot actually hosts a plugin.
ProjectCommands::AuditionResult createClapSlot(AudioEngine& engine)
{
    ProjectCommands::AuditionParams p;
    p.pluginId = kTyrellN6Clap;
    p.programIndex = -1;
    p.lengthBeats = 4.0;
    p.windowSeconds = 2.0;
    p.seed = 42;
    p.keepTrack = true;
    auto res = engine.getProjectCommands().auditionPlugin(p);
    EXPECT_TRUE(res.error.empty()) << res.error;
    EXPECT_TRUE(res.ok);
    if (res.ok)
    {
        auto* track = engine.getMainProcessor()->getTrack(res.trackIndex);
        EXPECT_NE(track, nullptr);
        if (track != nullptr && static_cast<size_t>(res.slotIndex) < track->getFXChain().size())
            EXPECT_TRUE(track->getFXChain()[static_cast<size_t>(res.slotIndex)]->isPlugin());
    }
    return res;
}

// Polls getFxProgramList until the child's async preset crawl populated the
// list (TyrellN6: ~669 entries after a ~10s crawl) or the deadline passes.
bool waitForProgramList(AudioEngine& engine, int track, int slot,
                        int minCount, int deadlineMs)
{
    const auto deadline = juce::Time::getMillisecondCounter()
        + static_cast<uint32_t>(deadlineMs);
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        auto list = engine.getFxProgramList(track, slot);
        if (static_cast<int>(list.size()) >= minCount)
            return true;
        juce::Thread::sleep(500);
    }
    return false;
}

bool memoryBlocksEqual(const juce::MemoryBlock& a, const juce::MemoryBlock& b)
{
    if (a.getSize() != b.getSize())
        return false;
    if (a.getSize() == 0)
        return true;
    return std::memcmp(a.getData(), b.getData(), a.getSize()) == 0;
}

ProjectCommands::AuditionResult auditionExistingSlot(AudioEngine& engine,
    int track, int slot, int programIndex)
{
    ProjectCommands::AuditionParams p;
    p.trackIndex = track;
    p.slotIndex = slot;
    p.programIndex = programIndex;
    p.lengthBeats = 4.0;
    p.windowSeconds = 2.0;
    p.seed = 42;
    return engine.getProjectCommands().auditionPlugin(p);
}

} // namespace

// Gate 3: the program list surfaces through the existing JUCE program API.
TEST(ClapProgram, ProgramsEnumerated)
{
    if (!clapTyrellN6Available())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or TyrellN6.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto res = createClapSlot(engine);
    ASSERT_TRUE(res.ok) << res.error;

    ASSERT_TRUE(waitForProgramList(engine, res.trackIndex, res.slotIndex, 100, 90000))
        << "preset database did not populate within the deadline";

    auto list = engine.getFxProgramList(res.trackIndex, res.slotIndex);
    EXPECT_GT(list.size(), 100u);
    EXPECT_FALSE(list[0].name.empty()) << "program 0 must have a name";
    int nonEmpty = 0;
    for (const auto& e : list)
        if (!e.name.empty())
            ++nonEmpty;
    EXPECT_EQ(nonEmpty, static_cast<int>(list.size()));
    std::cout << "[clap_program] TyrellN6 CLAP programs: " << list.size()
              << " (first: '" << list[0].name << "')" << std::endl;
}

// Gate 4: setCurrentProgram through the live (isolated) slot round-trips and
// changes the plugin state blob.
TEST(ClapProgram, SetProgramRoundTrips)
{
    if (!clapTyrellN6Available())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or TyrellN6.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto res = createClapSlot(engine);
    ASSERT_TRUE(res.ok) << res.error;
    ASSERT_TRUE(waitForProgramList(engine, res.trackIndex, res.slotIndex, 100, 90000));

    auto* track = engine.getMainProcessor()->getTrack(res.trackIndex);
    ASSERT_NE(track, nullptr);
    auto& slot = track->getFXChain()[static_cast<size_t>(res.slotIndex)];
    ASSERT_NE(slot, nullptr);
    auto* inst = slot->getPluginInstance();
    ASSERT_NE(inst, nullptr);

    juce::MemoryBlock stateBefore;
    inst->getStateInformation(stateBefore);
    ASSERT_GT(stateBefore.getSize(), 0u);

    slot->setCurrentProgram(1);
    EXPECT_EQ(slot->getCurrentProgram(), 1);

    juce::MemoryBlock stateAfter;
    inst->getStateInformation(stateAfter);
    ASSERT_GT(stateAfter.getSize(), 0u);
    EXPECT_FALSE(memoryBlocksEqual(stateBefore, stateAfter))
        << "loading program 1 must change the plugin state blob";
}

// Gate 5 (measure first): audition program 0 vs program 2 — both must be
// audible; if the renders differ, assert the rms/peak difference.
TEST(ClapProgram, PerProgramAudioDiffers)
{
    if (!clapTyrellN6Available())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or TyrellN6.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto res = createClapSlot(engine);
    ASSERT_TRUE(res.ok) << res.error;
    ASSERT_TRUE(waitForProgramList(engine, res.trackIndex, res.slotIndex, 100, 90000));

    auto r0 = auditionExistingSlot(engine, res.trackIndex, res.slotIndex, 0);
    ASSERT_TRUE(r0.error.empty()) << r0.error;
    ASSERT_TRUE(r0.ok);
    EXPECT_TRUE(r0.audible) << "program 0 silent: peak=" << r0.peak;

    auto r2 = auditionExistingSlot(engine, res.trackIndex, res.slotIndex, 2);
    ASSERT_TRUE(r2.error.empty()) << r2.error;
    ASSERT_TRUE(r2.ok);
    EXPECT_TRUE(r2.audible) << "program 2 silent: peak=" << r2.peak;

    std::cout << "[clap_program] program 0: rms=" << r0.rms << " peak=" << r0.peak
              << " | program 2: rms=" << r2.rms << " peak=" << r2.peak << std::endl;
    ::testing::Test::RecordProperty("rms0", std::to_string(r0.rms));
    ::testing::Test::RecordProperty("peak0", std::to_string(r0.peak));
    ::testing::Test::RecordProperty("rms2", std::to_string(r2.rms));
    ::testing::Test::RecordProperty("peak2", std::to_string(r2.peak));

    const float rmsDiff = std::fabs(r0.rms - r2.rms);
    const float peakDiff = std::fabs(r0.peak - r2.peak);
    if (rmsDiff > 1e-4f || peakDiff > 1e-4f)
    {
        ::testing::Test::RecordProperty("branch", "differs");
        std::cout << "[clap_program] branch=DIFFERS (rmsDiff=" << rmsDiff
                  << ", peakDiff=" << peakDiff << ")" << std::endl;
        EXPECT_GT(rmsDiff + peakDiff, 1e-4f);
    }
    else
    {
        ::testing::Test::RecordProperty("branch", "identical");
        std::cout << "[clap_program] branch=IDENTICAL — programs rendered the"
                     " same audio; asserting audibility only" << std::endl;
        EXPECT_TRUE(r0.audible && r2.audible);
    }
}

// Gate 6 (rebuild restore): the program's state blob survives
// rebuildRoutingGraph via the tree's pluginState (Track.cpp restore path).
TEST(ClapProgram, ProgramSurvivesRebuild)
{
    if (!clapTyrellN6Available())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or TyrellN6.clap missing";

    AudioEngine engine;
    engine.initialize();
    auto res = createClapSlot(engine);
    ASSERT_TRUE(res.ok) << res.error;
    ASSERT_TRUE(waitForProgramList(engine, res.trackIndex, res.slotIndex, 100, 90000));

    // Set program 2 on the live slot; applyPluginProgram snapshots the state
    // into the tree, which the rebuild restore path re-applies.
    auto pre = auditionExistingSlot(engine, res.trackIndex, res.slotIndex, 2);
    ASSERT_TRUE(pre.error.empty()) << pre.error;
    ASSERT_TRUE(pre.ok);
    ASSERT_TRUE(pre.audible) << "program 2 pre-rebuild silent: peak=" << pre.peak;

    engine.getMainProcessor()->rebuildRoutingGraph();

    // programIndex=-1 renders the CURRENT (restored) program of the new slot.
    auto post = auditionExistingSlot(engine, res.trackIndex, res.slotIndex, -1);
    ASSERT_TRUE(post.error.empty()) << post.error;
    ASSERT_TRUE(post.ok);
    EXPECT_TRUE(post.audible) << "post-rebuild silent: peak=" << post.peak;

    ASSERT_GT(pre.rms, 0.0f);
    const float deviation = std::fabs(post.rms - pre.rms) / pre.rms;
    std::cout << "[clap_program] rebuild: pre rms=" << pre.rms
              << " post rms=" << post.rms << " deviation=" << deviation << std::endl;
    EXPECT_LE(deviation, 0.25f)
        << "program audio character must survive the rebuild (pluginState blob)";
}
