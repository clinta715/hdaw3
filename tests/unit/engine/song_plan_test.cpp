// Song plan (Phase B keystone, docs/plans/2026-09-11): plan state +
// section-typed arranger regions, one-undo-unit semantics, save/load
// persistence, section templates, and Song Brief interchange. Note: plan
// state is MODEL-level (ValueTree) — no DSP state — so projections under
// test are the tree + command layer, not the live audio processor.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include <algorithm>

#include "engine/AudioEngine.h"
#include "common/ProjectCommands.h"
#include "model/ProjectModel.h"

using SongPlanData = ProjectCommands::SongPlanData;

namespace {

SongPlanData makePlan()
{
    SongPlanData plan;
    plan.bpm = 138.0;
    plan.keyRoot = 5;
    plan.scaleMode = 7;
    plan.style = "full-on";
    plan.seed = 777;
    plan.totalBars = 32;
    plan.sections = { { "intro", "intro", 8, 0.0, 32.0 },
                      { "build", "build", 8, 32.0, 64.0 },
                      { "drop", "mainA", 16, 64.0, 128.0 } };
    return plan;
}

} // namespace

TEST(SongPlan, SetSyncsTypedRegionsAndEchoes)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    auto r = cmds.setSongPlan(makePlan());
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.regionsCreated, 3);
    EXPECT_EQ(r.regionsUpdated, 0);
    ASSERT_EQ(r.plan.sections.size(), 3u);
    EXPECT_DOUBLE_EQ(r.plan.sections[2].startBeat, 64.0);
    EXPECT_DOUBLE_EQ(r.plan.sections[2].endBeat, 128.0);

    // Region sync: names, order, bounds, section kinds.
    auto regions = engine.getReadModel().getArrangerRegions();
    ASSERT_EQ(regions.size(), 3u);
    EXPECT_EQ(regions[0].name, "intro");
    EXPECT_DOUBLE_EQ(regions[0].startTime, 0.0);
    EXPECT_DOUBLE_EQ(regions[1].startTime, 32.0);
    EXPECT_DOUBLE_EQ(regions[2].startTime, 64.0);

    // SONG_PLAN and ARRANGER_LIST both live on the root tree.
    auto regionTree = engine.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_LIST);
    ASSERT_TRUE(regionTree.isValid());
    ASSERT_EQ(regionTree.getNumChildren(), 3);
    EXPECT_EQ(regionTree.getChild(0).getProperty(IDs::sectionKind).toString(), "intro");
    EXPECT_EQ(regionTree.getChild(1).getProperty(IDs::sectionKind).toString(), "build");
    EXPECT_EQ(regionTree.getChild(2).getProperty(IDs::sectionKind).toString(), "mainA");

    auto plan = cmds.getSongPlan();
    ASSERT_EQ(plan.sections.size(), 3u);
    EXPECT_DOUBLE_EQ(plan.bpm, 138.0);
    EXPECT_EQ(plan.keyRoot, 5);
    EXPECT_EQ(plan.scaleMode, 7);
    EXPECT_EQ(plan.style, "full-on");
    EXPECT_EQ(plan.seed, 777u);
    EXPECT_EQ(plan.totalBars, 32);
}

TEST(SongPlan, ValidationRejectsBadPlansWithNoMutation)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    auto badKind = makePlan();
    badKind.sections[0].kind = "wub";               // unknown kind
    EXPECT_FALSE(cmds.setSongPlan(badKind).ok);

    auto mismatch = makePlan();
    mismatch.totalBars = 99;                         // bars-sum mismatch
    EXPECT_FALSE(cmds.setSongPlan(mismatch).ok);

    auto empty = makePlan();
    empty.sections.clear();                          // no sections
    EXPECT_FALSE(cmds.setSongPlan(empty).ok);

    auto bpm = makePlan();
    bpm.bpm = 999.0;                                 // out of range
    EXPECT_FALSE(cmds.setSongPlan(bpm).ok);

    // Nothing was written by any rejected plan.
    EXPECT_TRUE(cmds.getSongPlan().sections.empty());
}

TEST(SongPlan, OneUndoRevertsPlan)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    EXPECT_TRUE(cmds.setSongPlan(makePlan()).ok);
    EXPECT_EQ(cmds.getSongPlan().sections.size(), 3u);

    engine.getProjectModel().getUndoManager().undo();

    EXPECT_TRUE(cmds.getSongPlan().sections.empty());
    auto regionTree = engine.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_LIST);
    int typed = 0;
    if (regionTree.isValid())
        for (int i = 0; i < regionTree.getNumChildren(); ++i)
            if (regionTree.getChild(i).hasProperty(IDs::sectionKind)) ++typed;
    EXPECT_EQ(typed, 0);
}

TEST(SongPlan, PersistsAcrossSaveLoad)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    EXPECT_TRUE(cmds.setSongPlan(makePlan()).ok);

    const juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("hdaw_songplan_test.hdaw");
    file.deleteFile();
    EXPECT_TRUE(cmds.saveProject(file.getFullPathName().toStdString()));

    AudioEngine engine2;
    engine2.initialize();
    EXPECT_TRUE(engine2.getProjectCommands().loadProject(file.getFullPathName().toStdString()));

    auto plan = engine2.getProjectCommands().getSongPlan();
    ASSERT_EQ(plan.sections.size(), 3u);
    EXPECT_EQ(plan.sections[1].kind, "build");
    EXPECT_EQ(plan.totalBars, 32);
    EXPECT_EQ(plan.seed, 777u);
    auto regions = engine2.getReadModel().getArrangerRegions();
    ASSERT_EQ(regions.size(), 3u);
    EXPECT_EQ(regions[0].name, "intro");
    file.deleteFile();
}

TEST(SongPlan, TemplateRoundTripDoesNotApply)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    EXPECT_TRUE(cmds.setSongPlan(makePlan()).ok);
    EXPECT_TRUE(cmds.saveSectionTemplate("hdaw-plan-test", nullptr));

    auto names = cmds.listSectionTemplates();
    EXPECT_NE(std::find(names.begin(), names.end(), "hdaw-plan-test"), names.end());

    // Switch to a different plan, then load — load returns the SAVED data
    // without applying it.
    auto other = makePlan();
    other.bpm = 100.0;
    other.totalBars = 32;
    other.sections[0].bars = 16;
    other.sections[1].bars = 8;
    other.sections[2].bars = 8;
    EXPECT_TRUE(cmds.setSongPlan(other).ok);
    EXPECT_DOUBLE_EQ(cmds.getSongPlan().bpm, 100.0);

    std::string err;
    auto loaded = cmds.loadSectionTemplate("hdaw-plan-test", &err);
    ASSERT_EQ(loaded.sections.size(), 3u);
    EXPECT_DOUBLE_EQ(loaded.bpm, 138.0);
    EXPECT_EQ(loaded.sections[0].bars, 8);
    EXPECT_DOUBLE_EQ(cmds.getSongPlan().bpm, 100.0); // still the other plan

    juce::File dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("HDAW").getChildFile("section-templates")
                         .getChildFile("hdaw-plan-test.json");
    dir.deleteFile();
}

TEST(SongPlan, BriefRoundTripAndValidation)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    const char* brief =
        R"({"bpm":140,"keyRoot":0,"scaleMode":1,"style":"dark","seed":9,"totalBars":48,"sections":[{"name":"a","type":"intro","bars":8},{"name":"b","type":"peak","bars":16},{"name":"c","type":"breakdown","bars":8},{"name":"d","type":"outro","bars":16}]})";

    auto r = cmds.applySongBrief(brief);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.plan.sections.size(), 4u);
    // Brief type aliases map onto canonical kinds.
    EXPECT_EQ(r.plan.sections[2].kind, "breakdown");
    EXPECT_EQ(r.plan.sections[3].kind, "finale");

    std::string err;
    auto exported = cmds.exportSongBrief(&err);
    ASSERT_FALSE(exported.empty()) << err;
    auto parsed = juce::JSON::parse(juce::String(exported));
    ASSERT_TRUE(parsed.isObject());
    auto types = parsed.getProperty("sections", {});
    ASSERT_EQ(types.size(), 4);
    EXPECT_EQ(types[1].getProperty("type", "").toString(), "peak");      // verbatim brief
    EXPECT_EQ(types[2].getProperty("type", "").toString(), "breakdown"); // verbatim brief

    // Gate 9: unknown kind and bars-sum mismatch are errors, no mutation.
    EXPECT_FALSE(cmds.applySongBrief(
        R"({"totalBars":8,"sections":[{"name":"x","type":"wub","bars":8}]})").ok);
    EXPECT_FALSE(cmds.applySongBrief(
        R"({"totalBars":99,"sections":[{"name":"x","type":"intro","bars":8}]})").ok);
    EXPECT_EQ(cmds.getSongPlan().sections.size(), 4u); // unchanged
}

// ── Cells (Phase C) ───────────────────────────────────────────────────────

namespace {
ProjectCommands::CellRecipe makeCell(const std::string& section, const std::string& role,
                                     const std::string& source, const std::string& params = "{}")
{
    ProjectCommands::CellRecipe r;
    r.section = section; r.role = role; r.trackId = 1; // Synth (MIDI) track
    r.sourceKind = source; r.paramsJson = params;
    return r;
}

juce::ValueTree findClipNode(AudioEngine& engine, int clipId)
{
    auto tl = engine.getProjectModel().getTrackListTree();
    for (int t = 0; t < tl.getNumChildren(); ++t)
    {
        auto clips = tl.getChild(t).getChildWithName(IDs::CLIP_LIST);
        for (int i = 0; i < clips.getNumChildren(); ++i)
            if ((int) clips.getChild(i).getProperty(IDs::clipID) == clipId)
                return clips.getChild(i);
    }
    return {};
}
} // namespace

TEST(SongCells, ValidationRejectsWithoutCells)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    std::string err;

    // No plan yet.
    EXPECT_FALSE(cmds.setCellRecipe(makeCell("intro", "bass", "phrase"), &err));

    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    EXPECT_FALSE(cmds.setCellRecipe(makeCell("nope", "bass", "phrase"), &err));      // unknown section
    EXPECT_FALSE(cmds.setCellRecipe(makeCell("intro", "bass", "wub"), &err));        // unknown source
    auto badStyle = makeCell("intro", "bass", "phrase", R"({"style":"NopeStyle"})");
    EXPECT_FALSE(cmds.setCellRecipe(badStyle, &err));                                 // unknown style
    EXPECT_FALSE(cmds.setCellRecipe(makeCell("intro", "bass", "harvest", "{}"), &err)); // harvest needs notes
    EXPECT_TRUE(cmds.getCells().empty());
}

TEST(SongCells, PhraseFillWindowReuseAndProvenance)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);   // intro 8/16/8 @138bpm, bpm metadata only; transport default 120

    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(makeCell("intro", "bass", "phrase", R"({"style":"BassLine"})"), &err)) << err;
    ASSERT_TRUE(cmds.setCellRecipe(makeCell("build", "hat", "rhythm", R"({"pulseA":4,"pulseB":0,"pitchA":42,"pitchB":42})"), &err)) << err;

    auto b = cmds.fillCells("all");
    ASSERT_TRUE(b.ok) << b.error;
    EXPECT_EQ(b.filled, 2);
    ASSERT_EQ(b.cells.size(), 2u);
    const auto phrase = b.cells[0].ok ? b.cells[0] : b.cells[1];
    const auto rhythm = b.cells[0].ok ? b.cells[1] : b.cells[0];
    EXPECT_TRUE(phrase.ok) << phrase.error;
    EXPECT_TRUE(rhythm.ok) << rhythm.error;
    EXPECT_GT(phrase.noteCount, 0);
    EXPECT_GT(rhythm.noteCount, 0);
    EXPECT_GT(phrase.seedUsed, 0u);

    // Window math: clip spans EXACTLY the section (120 BPM transport default).
    auto clip = findClipNode(engine, phrase.clipId);
    ASSERT_TRUE(clip.isValid());
    const double bpm = 120.0;
    EXPECT_NEAR((double) clip.getProperty(IDs::startTime), 0.0 * 60.0 / bpm, 1e-6);   // intro starts at beat 0
    EXPECT_NEAR((double) clip.getProperty(IDs::duration), 8.0 * 4 * 60.0 / bpm, 1e-6); // 8 bars = 32 beats

    // Provenance.
    auto prov = juce::JSON::parse(juce::String(cmds.getClipProvenance(phrase.clipId)));
    ASSERT_TRUE(prov.isObject());
    EXPECT_TRUE((bool) prov.getProperty("found", false));
    EXPECT_EQ(prov.getProperty("source", "").toString(), "phrase");
    EXPECT_TRUE(findClipNode(engine, rhythm.clipId).hasProperty(IDs::genTool));

    // Re-fill: same seed, same clip reused (not a second clip).
    auto b2 = cmds.fillCells("all");
    ASSERT_EQ(b2.cells.size(), 2u);
    EXPECT_EQ(b2.cells[0].clipId, b.cells[0].clipId);
    EXPECT_EQ(b2.cells[1].clipId, b.cells[1].clipId);

    // unfilled mode now finds nothing (both cells have a clip).
    EXPECT_EQ(cmds.fillCells("unfilled").filled, 0);
}

TEST(SongCells, LockSkipsAndRerollBumpsSeed)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(makeCell("intro", "bass", "phrase", R"({"style":"RandomWalk"})"), &err));
    ASSERT_TRUE(cmds.setCellRecipe(makeCell("drop", "arp", "phrase", R"({"style":"Lead"})"), &err));

    auto b1 = cmds.fillCells("all");
    ASSERT_EQ(b1.filled, 2);
    const uint64_t seed0 = b1.cells[0].seedUsed;

    // Lock the first cell: fill skips it.
    auto cells = cmds.getCells();
    auto locked = cells[0];
    locked.locked = true;
    ASSERT_TRUE(cmds.setCellRecipe(locked, &err));
    auto b2 = cmds.fillCells("all");
    EXPECT_EQ(b2.filled, 1);
    EXPECT_EQ(b2.skippedLocked, 1);

    // Reroll matches by role; seed bumps to lastSeed + 1 (NOT the locked cell).
    auto b3 = cmds.rerollCells("", "arp");
    ASSERT_EQ(b3.filled, 1);
    EXPECT_EQ(b3.cells[0].seedUsed, b1.cells[1].seedUsed + 1);

    // Reroll-all skips locked.
    auto b4 = cmds.rerollCells("", "");
    EXPECT_EQ(b4.filled, 1);
    EXPECT_EQ(b4.skippedLocked, 1);
}

TEST(SongCells, HarvestNotesAndRemove)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(makeCell("build", "hits", "harvest",
        R"({"notes":[{"pitch":60,"velocity":100,"startBeat":0.0,"durationBeats":0.5},{"pitch":64,"velocity":90,"startBeat":1.0,"durationBeats":0.5}]})"), &err));
    auto b = cmds.fillCells("all");
    ASSERT_EQ(b.filled, 1);
    EXPECT_EQ(b.cells[0].noteCount, 2);

    EXPECT_TRUE(cmds.removeCellRecipe("build", "hits"));
    EXPECT_TRUE(cmds.getCells().empty());
    EXPECT_FALSE(cmds.removeCellRecipe("build", "hits"));
}

TEST(SongCells, CellsPersistAcrossSaveLoad)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(makeCell("intro", "bass", "rhythm", R"({"pulseA":4,"pulseB":2})"), &err));

    const juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("hdaw_songcells_test.hdaw");
    file.deleteFile();
    ASSERT_TRUE(cmds.saveProject(file.getFullPathName().toStdString()));

    AudioEngine engine2;
    engine2.initialize();
    ASSERT_TRUE(engine2.getProjectCommands().loadProject(file.getFullPathName().toStdString()));
    auto cells = engine2.getProjectCommands().getCells();
    ASSERT_EQ(cells.size(), 1u);
    EXPECT_EQ(cells[0].role, "bass");
    EXPECT_EQ(cells[0].sourceKind, "rhythm");
    file.deleteFile();
}

