// ParamVerity corpus engine tests (Phase 3 gates P2 + P3): sweep MANY params of
// one slot in a single call, aggregate verdicts, write a re-readable sidecar.
// Contract: docs/plans/2026-09-22-param-verity-pipeline.md.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"

namespace {

// fm_synth instrument (slot 0) + saturator FX (slot 1), stable phrase clip.
int makeVerityTrack(AudioEngine& engine, std::string& err)
{
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("Verity");
    if (track < 0) { err = "addTrack failed"; return -1; }
    cmds.addFxSlot(track, "fm_synth", -1, "");
    cmds.addFxSlot(track, "saturator", -1, "");
    const int clipId = cmds.addMidiClip(track, 0.0, 4.0, "Verity");
    if (clipId <= 0) { err = "addMidiClip failed"; return -1; }
    for (int i = 0; i < 8; ++i)
        if (cmds.addNote(clipId, 60 + (i % 3), 100, i * 0.5, 0.45) <= 0)
        { err = "addNote failed"; return -1; }
    engine.drainPendingRoutingRebuild();
    return track;
}

} // namespace

TEST(ParamCorpus, SaturatorAllDefsVerdictsAndSidecar)
{
    AudioEngine engine;
    engine.initialize();
    std::string err;
    const int track = makeVerityTrack(engine, err);
    ASSERT_GE(track, 0) << err;

    const juce::File sidecar = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("hdaw_corpus_test_"
                                                 + juce::String(juce::Random::getSystemRandom().nextInt())
                                                 + ".json");

    ProjectCommands::ParamCorpusParams p;
    p.trackIndex = track;
    p.slotIndex = 1;                       // saturator: 6 defs (0..5)
    p.windowSeconds = 2.0;
    p.baselineRuns = 1;                    // keep the corpus fast; spread 0
    p.outPath = sidecar.getFullPathName().toStdString();

    const auto r = engine.getProjectCommands().verifyParamCorpus(p);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.fxType, "saturator");
    EXPECT_EQ(r.ran, 6);
    EXPECT_EQ(r.results.size(), 6u);
    EXPECT_GT(r.audibleCount, 0) << "Drive dB and Mix are audibly strong params";
    EXPECT_TRUE(r.sidecarWritten);
    EXPECT_TRUE(sidecar.existsAsFile());

    // Drive dB (0) and Mix (3) must read audible at 0 vs 1 normalized.
    bool driveAudible = false, mixAudible = false;
    for (const auto& e : r.results)
    {
        if (e.paramIndex == 0 && e.anyAudible) driveAudible = true;
        if (e.paramIndex == 3 && e.anyAudible) mixAudible = true;
    }
    EXPECT_TRUE(driveAudible);
    EXPECT_TRUE(mixAudible);

    // Sidecar is re-readable JSON with the corpus schema.
    const auto doc = juce::JSON::parse(sidecar.loadFileAsString());
    ASSERT_TRUE(doc.isObject());
    EXPECT_EQ(doc.getProperty("schema", "").toString(),
              juce::String("hdaw.param.verity.corpus.v1"));
    EXPECT_EQ(static_cast<int>(doc.getProperty("ran", 0)), 6);

    sidecar.deleteFile();
}

TEST(ParamCorpus, MaxParamsCapsAndExplicitListWins)
{
    AudioEngine engine;
    engine.initialize();
    std::string err;
    const int track = makeVerityTrack(engine, err);
    ASSERT_GE(track, 0) << err;
    auto& cmds = engine.getProjectCommands();

    // maxParams caps auto-enumeration.
    ProjectCommands::ParamCorpusParams capped;
    capped.trackIndex = track;
    capped.slotIndex = 1;
    capped.maxParams = 2;
    capped.windowSeconds = 1.0;
    auto rc = cmds.verifyParamCorpus(capped);
    ASSERT_TRUE(rc.error.empty()) << rc.error;
    EXPECT_EQ(rc.ran, 2);

    // Explicit paramIndexes win and out-of-range indexes are REPORTED, not
    // silently dropped (P3).
    ProjectCommands::ParamCorpusParams explicitList;
    explicitList.trackIndex = track;
    explicitList.slotIndex = 1;
    explicitList.paramIndexes = { 3, 99 };
    explicitList.windowSeconds = 1.0;
    auto re = cmds.verifyParamCorpus(explicitList);
    ASSERT_TRUE(re.error.empty()) << re.error;
    ASSERT_EQ(re.results.size(), 2u);
    EXPECT_TRUE(re.results[0].ok);
    EXPECT_FALSE(re.results[1].ok);
    EXPECT_FALSE(re.results[1].error.empty());
}

TEST(ParamCorpus, PluginSlotWithoutCacheIsAnError)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    const int track = cmds.addTrack("Verity");
    ASSERT_GE(track, 0);
    cmds.addFxSlot(track, "plugin", -1, "hdaw.testplugin");   // no live cache deviceless
    cmds.addMidiClip(track, 0.0, 4.0, "x");
    engine.drainPendingRoutingRebuild();

    ProjectCommands::ParamCorpusParams p;
    p.trackIndex = track;
    p.slotIndex = 0;
    // No paramIndexes, no live cache -> explicit error, never a guess (P3).
    const auto r = cmds.verifyParamCorpus(p);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("param cache"), std::string::npos);
}
