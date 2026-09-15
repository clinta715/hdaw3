// Song plan (Phase B keystone, docs/plans/2026-09-11): plan state +
// section-typed arranger regions, one-undo-unit semantics, save/load
// persistence, section templates, and Song Brief interchange. Note: plan
// state is MODEL-level (ValueTree) — no DSP state — so projections under
// test are the tree + command layer, not the live audio processor.
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "engine/AudioEngine.h"
#include "engine/SeededCellDefaults.h"
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
    cmds.addTrack("Track 0");
    cmds.addTrack("Track 1");
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

TEST(SongCells, PadCellFillsChordVoicing)
{
    // B6 (Modular Dawn audit): a pad-role cell must produce a chord voicing
    // (root + fifth +7 + octave +12 per chord slot), not a single-note drone.
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Track 0");
    cmds.addTrack("Track 1");
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(makeCell("intro", "pad", "phrase"), &err)) << err;

    auto b = cmds.fillCells("all");
    ASSERT_TRUE(b.ok) << b.error;
    ASSERT_EQ(b.cells.size(), 1u);
    ASSERT_TRUE(b.cells[0].ok) << b.cells[0].error;
    // A voicing stack: >= 3 notes (one chord slot minimum).
    EXPECT_GE(b.cells[0].noteCount, 3);

    // Every filled note carries its +7 and +12 companions at the same start
    // beat — the chord-voicing contract (group by exact startBeat; collisions
    // between base notes are tolerated by the companion lookup).
    auto clip = findClipNode(engine, b.cells[0].clipId);
    ASSERT_TRUE(clip.isValid());
    auto notes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    ASSERT_TRUE(notes.isValid());
    struct P { double start; int pitch; };
    std::vector<P> ns;
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        auto n = notes.getChild(i);
        ns.push_back({ (double) n.getProperty(IDs::startBeat),
                       (int) n.getProperty(IDs::noteNumber) });
    }
    ASSERT_GE(ns.size(), 3u);
    // Voicing contract (B6 + E3): every filled note stacks companions from
    // ONE seeded shape ({7,12} legacy, {7,12,19} rich, {12,19} open) at the
    // same start beat — never a single-note drone. Collision-tolerant: a
    // note counts when another note in its start group sits at one of the
    // shape intervals above or below it.
    const int shape = HDAW::SeededDefaults::padVoicingShape(b.cells[0].seedUsed);
    const auto intervals = HDAW::SeededDefaults::padVoicingIntervals(shape);
    int covered = 0;
    for (const auto& n : ns)
    {
        bool member = false;
        for (const auto& m : ns)
        {
            if (&m == &n || m.start != n.start) continue;
            const int d = n.pitch - m.pitch;
            for (int iv : intervals)
                if (d == iv || d == -iv) { member = true; break; }
            if (member) break;
        }
        if (member) ++covered;
    }
    EXPECT_EQ(covered, static_cast<int>(ns.size()))
        << "every pad note must stack the seeded voicing shape (shape=" << shape << ")";
    // And the stack is real: several independent chord slots (start groups).
    std::set<double> starts;
    for (const auto& n : ns) starts.insert(n.start);
    EXPECT_GE(starts.size(), 3u);

    // The octave voice proves it's a real stack, not a unison retrigger.
    int maxPitch = 0;
    for (const auto& n : ns) maxPitch = std::max(maxPitch, n.pitch);
    EXPECT_GE(maxPitch - 12, 48) << "expected stacked octaves (root+12)";
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

namespace {

// "pitch@start;" signature of every note in a clip (start rounded to 1e-3
// so float formatting never spuriously differs).
std::string clipNoteSig(AudioEngine& engine, int clipId, double* maxStart = nullptr)
{
    std::string sig;
    double mx = 0.0;
    auto clip = findClipNode(engine, clipId);
    auto notes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    for (int i = 0; i < notes.getNumChildren(); ++i)
    {
        auto n = notes.getChild(i);
        const double st = (double) n.getProperty(IDs::startBeat, 0.0);
        mx = std::max(mx, st);
        sig += std::to_string((int) n.getProperty(IDs::noteNumber, 0)) + "@"
             + std::to_string((int) std::lround(st * 1000.0)) + ";";
    }
    if (maxStart != nullptr) *maxStart = mx;
    return sig;
}

} // namespace

// (A1) A bare rhythm cell varies with the plan seed and is deterministic
// per seed — the identical-defaults bug (4/3/1/1 in every song).
TEST(SongCells, BareRhythmCellVariesWithSeedAndIsDeterministic)
{
    // ASSERT_* cannot appear in a non-void lambda (bare failure return),
    // so this helper reports failure as an empty signature.
    auto fillSig = [](uint64_t planSeed, int* clipIdOut) -> std::string {
        AudioEngine engine;
        engine.initialize();
        auto& cmds = engine.getProjectCommands();
        cmds.addTrack("Track 0");
        cmds.addTrack("Track 1");
        auto plan = makePlan();
        plan.seed = planSeed;
        if (!cmds.setSongPlan(plan).ok) return {};
        std::string err;
        if (!cmds.setCellRecipe(makeCell("build", "hat", "rhythm", "{}"), &err))
            return {};
        auto b = cmds.fillCells("all");
        if (!b.ok || b.cells.size() != 1u || !b.cells[0].ok) return {};
        if (clipIdOut != nullptr) *clipIdOut = b.cells[0].clipId;
        // NOTE: engine dies at scope end; read the signature before that.
        return clipNoteSig(engine, b.cells[0].clipId);
    };
    const std::string sig777 = fillSig(777, nullptr);
    const std::string sig778 = fillSig(778, nullptr);
    EXPECT_FALSE(sig777.empty()) << "fill 777 produced nothing";
    EXPECT_FALSE(sig778.empty()) << "fill 778 produced nothing";
    EXPECT_NE(sig777, sig778) << "bare rhythm cells must vary per seed";
    const std::string sig777b = fillSig(777, nullptr);
    EXPECT_EQ(sig777, sig777b) << "same seed must refill identically";
}

// (B1) corpusRole fills from a seeded bank phrase at the phrase GM pitch.
TEST(SongCells, CorpusRoleFillsBankPhraseAtPhrasePitch)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Track 0");
    cmds.addTrack("Track 1");
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(
        makeCell("build", "hat", "rhythm", R"({"corpusRole":"hats"})"), &err)) << err;
    auto b = cmds.fillCells("all");
    ASSERT_TRUE(b.ok) << b.error;
    ASSERT_TRUE(b.cells[0].ok) << b.cells[0].error;
    EXPECT_GT(b.cells[0].noteCount, 0);
    auto clip = findClipNode(engine, b.cells[0].clipId);
    ASSERT_TRUE(clip.isValid());
    auto notes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    ASSERT_GT(notes.getNumChildren(), 0);
    for (int i = 0; i < notes.getNumChildren(); ++i)
        EXPECT_EQ((int) notes.getChild(i).getProperty(IDs::noteNumber, 0), 42)
            << "hats phrases sound at GM pitch 42 without pitchA";
    // Deterministic per seed: refill after touching nothing is identical.
    const std::string before = clipNoteSig(engine, b.cells[0].clipId);
    auto b2 = cmds.fillCells("all");
    ASSERT_TRUE(b2.cells[0].ok);
    EXPECT_EQ(before, clipNoteSig(engine, b2.cells[0].clipId));
}

// (B2) Unknown bank role falls back to euclidean — ok, never an error.
TEST(SongCells, CorpusRoleUnknownRoleFallsBackToEuclidean)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Track 0");
    cmds.addTrack("Track 1");
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(
        makeCell("build", "hat", "rhythm", R"({"corpusRole":"kazoo"})"), &err)) << err;
    auto b = cmds.fillCells("all");
    ASSERT_TRUE(b.ok) << b.error;
    EXPECT_TRUE(b.cells[0].ok) << b.cells[0].error;
    EXPECT_GT(b.cells[0].noteCount, 0);
}

// (B3) The phrase tiles across the whole section window (drop = 16 bars).
TEST(SongCells, CorpusRoleTilesAcrossSectionWindow)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Track 0");
    cmds.addTrack("Track 1");
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(
        makeCell("drop", "snare", "rhythm", R"({"corpusRole":"snare"})"), &err)) << err;
    auto b = cmds.fillCells("all");
    ASSERT_TRUE(b.ok) << b.error;
    ASSERT_TRUE(b.cells[0].ok) << b.cells[0].error;
    double mx = 0.0;
    clipNoteSig(engine, b.cells[0].clipId, &mx);
    // drop window = 64 beats; longest bank phrase = 8 bars = 32 beats, so a
    // tiled fill must reach at least beat 64 - 32.
    EXPECT_GE(mx, 64.0 - 32.0) << "phrase did not tile to the window end";
}

// ── Seeded cell defaults (E1–E3): pure-picker unit tests ──────────────────

TEST(SeededDefaults, BreakStyleVariesAndIsDeterministic)
{
    using HDAW::SeededDefaults::defaultBreakStyle;
    // Valid style for a spread of seeds (incl. 0), deterministic per seed.
    for (uint64_t s = 0; s < 50; ++s)
    {
        const auto st = defaultBreakStyle(s);
        EXPECT_GE((int) st, 0);
        EXPECT_LE((int) st, 4);
        EXPECT_EQ(st, defaultBreakStyle(s));
    }
    // Variation: more than one style across seeds.
    std::set<int> seen;
    for (uint64_t s = 1; s <= 20; ++s)
        seen.insert((int) defaultBreakStyle(s));
    EXPECT_GT(seen.size(), 1u) << "break default never varies";
}

TEST(SeededDefaults, PhraseRoleStyles)
{
    using HDAW::SeededDefaults::defaultPhraseStyleForRole;
    EXPECT_EQ(defaultPhraseStyleForRole("bass", 7), PhraseGenerator::BassLine);
    EXPECT_EQ(defaultPhraseStyleForRole("pad", 7), PhraseGenerator::Pad);
    EXPECT_EQ(defaultPhraseStyleForRole("riser", 7), PhraseGenerator::Buildup);
    EXPECT_EQ(defaultPhraseStyleForRole("wobble", 7), PhraseGenerator::Standard);
    EXPECT_EQ(defaultPhraseStyleForRole("", 7), PhraseGenerator::Standard);
    // Sets stay musical: lead never draws BassLine, etc.
    for (uint64_t s = 0; s < 50; ++s)
    {
        const auto lead = defaultPhraseStyleForRole("lead", s);
        EXPECT_TRUE(lead == PhraseGenerator::Lead || lead == PhraseGenerator::RandomWalk);
        const auto arp = defaultPhraseStyleForRole("arp", s);
        EXPECT_TRUE(arp == PhraseGenerator::Arpeggio || arp == PhraseGenerator::RandomWalk);
        const auto stab = defaultPhraseStyleForRole("stab", s);
        EXPECT_TRUE(stab == PhraseGenerator::ChordStab || stab == PhraseGenerator::Standard);
        EXPECT_EQ(defaultPhraseStyleForRole("lead", s), lead); // deterministic
    }
    std::set<int> leadSeen, arpSeen;
    for (uint64_t s = 1; s <= 20; ++s)
    {
        leadSeen.insert((int) defaultPhraseStyleForRole("lead", s));
        arpSeen.insert((int) defaultPhraseStyleForRole("arp", s));
    }
    EXPECT_GT(leadSeen.size(), 1u) << "lead style never varies";
    EXPECT_GT(arpSeen.size(), 1u) << "arp style never varies";
}

TEST(SeededDefaults, PadVoicingShapes)
{
    using HDAW::SeededDefaults::padVoicingIntervals;
    using HDAW::SeededDefaults::padVoicingShape;
    EXPECT_EQ(padVoicingIntervals(0), (std::vector<int>{ 7, 12 }));
    EXPECT_EQ(padVoicingIntervals(1), (std::vector<int>{ 7, 12, 19 }));
    EXPECT_EQ(padVoicingIntervals(2), (std::vector<int>{ 12, 19 }));
    EXPECT_EQ(padVoicingIntervals(99), (std::vector<int>{ 7, 12 }));
    std::set<int> seen;
    for (uint64_t s = 0; s < 50; ++s)
    {
        const int sh = padVoicingShape(s);
        EXPECT_GE(sh, 0);
        EXPECT_LE(sh, 2);
        EXPECT_EQ(padVoicingShape(s), sh); // deterministic
        seen.insert(sh);
    }
    EXPECT_GT(seen.size(), 1u) << "pad voicing never varies";
}

// (E2 wiring smoke) A bare lead phrase cell fills and refills identically.
TEST(SongCells, BareLeadCellFillsDeterministically)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addTrack("Track 0");
    cmds.addTrack("Track 1");
    ASSERT_TRUE(cmds.setSongPlan(makePlan()).ok);
    std::string err;
    ASSERT_TRUE(cmds.setCellRecipe(
        makeCell("drop", "lead", "phrase", "{}"), &err)) << err;
    auto b = cmds.fillCells("all");
    ASSERT_TRUE(b.ok) << b.error;
    ASSERT_TRUE(b.cells[0].ok) << b.cells[0].error;
    EXPECT_GT(b.cells[0].noteCount, 0);
    const std::string before = clipNoteSig(engine, b.cells[0].clipId);
    auto b2 = cmds.fillCells("all");
    ASSERT_TRUE(b2.cells[0].ok);
    EXPECT_EQ(before, clipNoteSig(engine, b2.cells[0].clipId));
}

