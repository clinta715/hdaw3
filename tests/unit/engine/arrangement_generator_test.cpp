#include <gtest/gtest.h>
#include "engine/ArrangementGenerator.h"
#include "engine/AudioEngine.h"
#include <cmath>
#include <set>
#include <vector>

using namespace HDAW;

namespace
{

const ArrangementPart* findPart (const Arrangement& arr, TrackRole role)
{
    for (const auto& p : arr.parts)
        if (p.role == role)
            return &p;
    return nullptr;
}

int beatToBar (double beat) { return static_cast<int> (std::floor (beat / 4.0 + 1e-9)); }

int beatToStep (double beat)
{
    const int bar = beatToBar (beat);
    return static_cast<int> (std::lround ((beat - bar * 4.0) / 0.25));
}

ArrangementParams defaultParams (uint64_t seed, int bars = 32)
{
    ArrangementParams p;
    p.bars = bars;
    p.seed = seed;
    return p;
}

} // namespace

// ── Section planning ──

TEST(ArrangementSections, MapCoversAllBarsContiguously)
{
    for (int bars : { 1, 8, 16, 32, 48, 64, 128, 200 })
    {
        auto map = buildSectionMap (bars);
        ASSERT_FALSE (map.empty()) << "bars=" << bars;
        EXPECT_EQ (map.front().first, 0) << "bars=" << bars;

        // sections are strictly increasing and within range
        for (size_t i = 1; i < map.size(); ++i)
            EXPECT_GT (map[i].first, map[i - 1].first);
        EXPECT_LT (map.back().first, bars);

        // the last section spans to the end (no uncovered bars)
        auto energy = energyCurveFromSections (bars, map);
        EXPECT_EQ (static_cast<int> (energy.size()), bars);
    }
}

TEST(ArrangementSections, ThirtyTwoBarStructure)
{
    auto map = buildSectionMap (32);
    ASSERT_EQ (map.size(), 4u);
    EXPECT_EQ (map[0].second, Section::Intro);
    EXPECT_EQ (map[1].second, Section::BuildUp);
    EXPECT_EQ (map[2].second, Section::MainA);
    EXPECT_EQ (map[3].second, Section::Drop);
}

// ── Energy curve ──

TEST(ArrangementEnergy, BoundsAndShape)
{
    auto map = buildSectionMap (32);
    auto energy = energyCurveFromSections (32, map);
    ASSERT_EQ (energy.size(), 32u);
    for (int e : energy) { EXPECT_GE (e, 0); EXPECT_LE (e, 3); }

    EXPECT_EQ (energy[0], 0);   // intro starts low
    EXPECT_EQ (energy[24], 3);  // drop starts at peak
    // energy is not constant across the whole arrangement
    std::set<int> distinct (energy.begin(), energy.end());
    EXPECT_GT (distinct.size(), 1u);
}

TEST(ArrangementEnergy, ZeroBarsEmpty)
{
    auto map = buildSectionMap (0);
    EXPECT_TRUE (map.empty());
    EXPECT_TRUE (energyCurveFromSections (0, map).empty());
}

// ── Determinism ──

TEST(ArrangementDeterminism, SameSeedIdentical)
{
    auto a = generateArrangement (defaultParams (1337));
    auto b = generateArrangement (defaultParams (1337));

    ASSERT_EQ (a.parts.size(), b.parts.size());
    ASSERT_EQ (a.energyByBar, b.energyByBar);
    EXPECT_EQ (a.resolvedSeed, b.resolvedSeed);
    for (size_t i = 0; i < a.parts.size(); ++i)
    {
        EXPECT_EQ (a.parts[i].role, b.parts[i].role);
        ASSERT_EQ (a.parts[i].notes.size(), b.parts[i].notes.size()) << "part " << i;
        for (size_t j = 0; j < a.parts[i].notes.size(); ++j)
        {
            const auto& na = a.parts[i].notes[j];
            const auto& nb = b.parts[i].notes[j];
            EXPECT_DOUBLE_EQ (na.startBeat, nb.startBeat);
            EXPECT_EQ (na.noteNumber, nb.noteNumber);
            EXPECT_EQ (na.velocity, nb.velocity);
            EXPECT_DOUBLE_EQ (na.durationBeats, nb.durationBeats);
        }
    }
}

TEST(ArrangementDeterminism, DifferentSeedDiffers)
{
    auto a = generateArrangement (defaultParams (1));
    auto b = generateArrangement (defaultParams (2));
    bool differs = a.energyByBar != b.energyByBar || a.parts.size() != b.parts.size();
    for (size_t i = 0; i < a.parts.size() && i < b.parts.size() && !differs; ++i)
    {
        differs = a.parts[i].notes.size() != b.parts[i].notes.size();
        for (size_t j = 0; j < a.parts[i].notes.size() && j < b.parts[i].notes.size() && !differs; ++j)
            differs = a.parts[i].notes[j].noteNumber != b.parts[i].notes[j].noteNumber
                   || a.parts[i].notes[j].velocity != b.parts[i].notes[j].velocity;
    }
    EXPECT_TRUE (differs);
}

// ── Content ──

TEST(ArrangementContent, DefaultProducesCoreParts)
{
    auto arr = generateArrangement (defaultParams (42, 32));
    EXPECT_NE (findPart (arr, TrackRole::Kick), nullptr);
    EXPECT_NE (findPart (arr, TrackRole::Bass), nullptr);
    EXPECT_GE (arr.parts.size(), 4u);
    for (const auto& part : arr.parts)
        EXPECT_FALSE (part.notes.empty());
}

TEST(ArrangementContent, NotesStayWithinArrangement)
{
    const int bars = 32;
    auto arr = generateArrangement (defaultParams (7, bars));
    const double endBeat = bars * 4.0;
    for (const auto& part : arr.parts)
        for (const auto& n : part.notes)
        {
            EXPECT_GE (n.startBeat, 0.0);
            EXPECT_LT (n.startBeat, endBeat);
            EXPECT_GT (n.durationBeats, 0.0);
            EXPECT_GE (n.velocity, 1);
            EXPECT_LE (n.velocity, 127);
        }
}

TEST(ArrangementContent, ZeroBarsYieldsEmpty)
{
    auto arr = generateArrangement (defaultParams (5, 0));
    EXPECT_TRUE (arr.parts.empty());
    EXPECT_TRUE (arr.energyByBar.empty());
}

// ── Kick exclusion invariant ──

TEST(ArrangementExclusion, BassNeverLandsOnKickStep)
{
    auto arr = generateArrangement (defaultParams (2024, 64));
    const auto* kick = findPart (arr, TrackRole::Kick);
    const auto* bass = findPart (arr, TrackRole::Bass);
    ASSERT_NE (kick, nullptr);
    ASSERT_NE (bass, nullptr);

    // per-bar set of kick 16th-steps
    std::vector<std::set<int>> kickSteps (64);
    for (const auto& n : kick->notes)
    {
        const int bar = beatToBar (n.startBeat);
        if (bar >= 0 && bar < 64)
            kickSteps[static_cast<size_t>(bar)].insert (beatToStep (n.startBeat));
    }

    int bassNotes = 0;
    for (const auto& n : bass->notes)
    {
        const int bar = beatToBar (n.startBeat);
        ASSERT_GE (bar, 0);
        ASSERT_LT (bar, 64);
        const int step = beatToStep (n.startBeat);
        EXPECT_EQ (kickSteps[static_cast<size_t>(bar)].count (step), 0u)
            << "bass note collides with kick at bar " << bar << " step " << step;
        ++bassNotes;
    }
    EXPECT_GT (bassNotes, 0);
}

// ── Enable flags ──

TEST(ArrangementEnable, OnlyKickProducesSinglePart)
{
    ArrangementParams p = defaultParams (42, 16);
    p.enableKick = true;
    p.enableClosedHat = p.enableOpenHat = p.enableClap = p.enableSnare = false;
    p.enableBass = p.enableLead = p.enableChords = false;

    auto arr = generateArrangement (p);
    ASSERT_EQ (arr.parts.size(), 1u);
    EXPECT_EQ (arr.parts[0].role, TrackRole::Kick);
}

TEST(ArrangementEnable, DisablingKickChangesBassViaDucking)
{
    ArrangementParams withKick = defaultParams (42, 32);
    ArrangementParams noKick = defaultParams (42, 32);
    noKick.enableKick = false;

    auto a = generateArrangement (withKick);
    auto b = generateArrangement (noKick);

    EXPECT_NE (findPart (a, TrackRole::Kick), nullptr);
    EXPECT_EQ (findPart (b, TrackRole::Kick), nullptr);

    const auto* ba = findPart (a, TrackRole::Bass);
    const auto* bb = findPart (b, TrackRole::Bass);
    ASSERT_NE (ba, nullptr);
    ASSERT_NE (bb, nullptr);

    // Ducking drops bass notes that coincide with kicks, so the two bass lines
    // must differ (in count or in the onsets used).
    bool differs = ba->notes.size() != bb->notes.size();
    for (size_t i = 0; i < ba->notes.size() && i < bb->notes.size() && !differs; ++i)
        differs = ba->notes[i].startBeat != bb->notes[i].startBeat;
    EXPECT_TRUE (differs);
}

// ── Project integration ──

TEST(ArrangementIntegration, GeneratesTracksClipsAndNotes)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    HDAW::ArrangementParams p;
    p.bars = 16;
    p.seed = 1337;
    auto result = cmds.generateArrangement (p);

    EXPECT_GT (result.noteCount, 0);
    EXPECT_FALSE (result.clipIds.empty());
    EXPECT_EQ (result.trackIndices.size(), result.clipIds.size());
    EXPECT_EQ (result.seed, 1337u);

    auto& read = engine.getReadModel();
    for (int clipId : result.clipIds)
        EXPECT_FALSE (read.getNotes (clipId).empty()) << "clip " << clipId;
}

TEST(ArrangementIntegration, DeterministicAcrossEngines)
{
    auto run = [] {
        AudioEngine engine;
        engine.initialize();
        HDAW::ArrangementParams p;
        p.bars = 16;
        p.seed = 4242;
        return engine.getProjectCommands().generateArrangement (p);
    };
    auto a = run();
    auto b = run();
    EXPECT_EQ (a.noteCount, b.noteCount);
    EXPECT_EQ (a.clipIds.size(), b.clipIds.size());
    EXPECT_EQ (a.trackIndices.size(), b.trackIndices.size());
    EXPECT_EQ (a.seed, b.seed);
}

// ── Lead / Chords ──

TEST(ArrangementLeadChords, PresentAndBoundedWhenEnabled)
{
    ArrangementParams p = defaultParams (77, 32);
    p.enableKick = p.enableClosedHat = p.enableOpenHat = p.enableClap = p.enableBass = false;
    p.enableLead = true;
    p.enableChords = true;
    auto arr = generateArrangement (p);

    const auto* lead = findPart (arr, TrackRole::Lead);
    const auto* chords = findPart (arr, TrackRole::Chords);
    ASSERT_NE (lead, nullptr);
    ASSERT_NE (chords, nullptr);
    EXPECT_FALSE (lead->notes.empty());
    EXPECT_FALSE (chords->notes.empty());

    for (const auto& n : lead->notes)   { EXPECT_GE (n.noteNumber, 48); EXPECT_LE (n.noteNumber, 76); }
    for (const auto& n : chords->notes) { EXPECT_GE (n.noteNumber, 48); EXPECT_LE (n.noteNumber, 84); }
}

TEST(ArrangementLeadChords, Deterministic)
{
    ArrangementParams p = defaultParams (555, 32);
    p.enableLead = true;
    p.enableChords = true;
    auto a = generateArrangement (p);
    auto b = generateArrangement (p);

    const auto* la = findPart (a, TrackRole::Lead);
    const auto* lb = findPart (b, TrackRole::Lead);
    ASSERT_NE (la, nullptr);
    ASSERT_NE (lb, nullptr);
    ASSERT_EQ (la->notes.size(), lb->notes.size());
    for (size_t i = 0; i < la->notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ (la->notes[i].startBeat, lb->notes[i].startBeat);
        EXPECT_EQ (la->notes[i].noteNumber, lb->notes[i].noteNumber);
    }
}

// ── Snare ──

TEST(ArrangementSnare, HousePlacesBackbeatOnTwoAndFour)
{
    ArrangementParams p = defaultParams (99, 16);
    p.style = 1;
    p.enableSnare = true;
    auto arr = generateArrangement (p);
    const auto* snare = findPart (arr, TrackRole::Snare);
    ASSERT_NE (snare, nullptr);

    std::vector<std::set<int>> stepsByBar (16);
    for (const auto& n : snare->notes)
    {
        const int bar = beatToBar (n.startBeat);
        ASSERT_GE (bar, 0);
        ASSERT_LT (bar, 16);
        stepsByBar[static_cast<size_t>(bar)].insert (beatToStep (n.startBeat));
    }
    for (int bar = 0; bar < 16; ++bar)
    {
        EXPECT_NE (stepsByBar[static_cast<size_t>(bar)].count (4), 0u)
            << "bar " << bar << " lacks beat-2 snare";
        EXPECT_NE (stepsByBar[static_cast<size_t>(bar)].count (12), 0u)
            << "bar " << bar << " lacks beat-4 snare";
    }
}

TEST(ArrangementSnare, DnbTwoStepBackbeatWithQuietGhosts)
{
    ArrangementParams p = defaultParams (1234, 16);
    p.style = 2;
    p.enableSnare = true;
    auto arr = generateArrangement (p);
    const auto* snare = findPart (arr, TrackRole::Snare);
    ASSERT_NE (snare, nullptr);

    std::vector<std::set<int>> stepsByBar (16);
    for (const auto& n : snare->notes)
    {
        const int bar = beatToBar (n.startBeat);
        ASSERT_GE (bar, 0);
        ASSERT_LT (bar, 16);
        stepsByBar[static_cast<size_t>(bar)].insert (beatToStep (n.startBeat));
    }
    for (int bar = 0; bar < 16; ++bar)
    {
        EXPECT_NE (stepsByBar[static_cast<size_t>(bar)].count (4), 0u)
            << "bar " << bar << " lacks two-step snare 1";
        EXPECT_NE (stepsByBar[static_cast<size_t>(bar)].count (11), 0u)
            << "bar " << bar << " lacks two-step snare 2";
    }
    for (const auto& n : snare->notes)
    {
        const int step = beatToStep (n.startBeat);
        if (step != 4 && step != 11 && step != 14)
            EXPECT_LT (n.velocity, 90) << "ghost snare not quiet";
    }
}

TEST(ArrangementSnare, DisabledByDefault)
{
    auto arr = generateArrangement (defaultParams (42, 32));
    EXPECT_EQ (findPart (arr, TrackRole::Snare), nullptr);
}

// ── Genre styles ──

TEST(ArrangementGenre, HouseKickMoreFourOnFloorThanTechno)
{
    auto countFotfBars = [] (const Arrangement& arr) {
        const auto* kick = findPart (arr, TrackRole::Kick);
        if (!kick) return 0;
        std::set<int> bars;
        for (const auto& n : kick->notes) bars.insert (beatToBar (n.startBeat));
        int n = 0;
        for (int bar : bars)
        {
            std::set<int> steps;
            for (const auto& note : kick->notes)
                if (beatToBar (note.startBeat) == bar) steps.insert (beatToStep (note.startBeat));
            if (steps.count (4) != 0u && steps.count (12) != 0u) ++n;
        }
        return n;
    };

    ArrangementParams house = defaultParams (42, 32);
    house.style = 1;
    ArrangementParams techno = defaultParams (42, 32);
    techno.style = 0;
    const int h = countFotfBars (generateArrangement (house));
    const int t = countFotfBars (generateArrangement (techno));
    EXPECT_GT (h, t) << "house fotf bars=" << h << " techno=" << t;
}

TEST(ArrangementGenre, DnbClosedHatsDenserThanTechno)
{
    ArrangementParams dnb = defaultParams (42, 32);
    dnb.style = 2;
    ArrangementParams techno = defaultParams (42, 32);
    techno.style = 0;
    auto dnbArr = generateArrangement (dnb);
    auto techArr = generateArrangement (techno);
    const auto* dnbHats = findPart (dnbArr, TrackRole::ClosedHat);
    const auto* techHats = findPart (techArr, TrackRole::ClosedHat);
    ASSERT_NE (dnbHats, nullptr);
    ASSERT_NE (techHats, nullptr);
    EXPECT_GT (dnbHats->notes.size(), techHats->notes.size());
}

TEST(ArrangementIntegration, TargetTrackIdsHonoredForAllRoles)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    std::map<std::string, int> roleMap;
    roleMap["Kick"] = cmds.addTrack("K", -1, -1, 0);
    roleMap["ClosedHat"] = cmds.addTrack("CH", -1, -1, 0);
    roleMap["OpenHat"] = cmds.addTrack("OH", -1, -1, 0);
    roleMap["Clap"] = cmds.addTrack("CL", -1, -1, 0);
    roleMap["Bass"] = cmds.addTrack("B", -1, -1, 0);

    int trackCountBefore = engine.getProjectModel().getTrackListTree().getNumChildren();

    HDAW::ArrangementParams p;
    p.bars = 8;
    p.seed = 42;
    p.enableSnare = p.enableLead = p.enableChords = false;
    p.targetTrackIds = roleMap;
    auto result = cmds.generateArrangement(p);

    int trackCountAfter = engine.getProjectModel().getTrackListTree().getNumChildren();
    EXPECT_EQ(trackCountBefore, trackCountAfter);

    for (int idx : result.trackIndices)
    {
        bool found = false;
        for (const auto& [k, v] : roleMap)
            if (v == idx) { found = true; break; }
        EXPECT_TRUE(found) << "trackIdx " << idx << " not in targetTrackIds";
    }
}

TEST(ArrangementIntegration, GeneratedNotesHaveVelocity)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    HDAW::ArrangementParams p;
    p.bars = 16;
    p.seed = 174;
    p.style = 2; // DnB
    auto result = cmds.generateArrangement (p);

    EXPECT_GT (result.noteCount, 0);
    auto& read = engine.getReadModel();
    for (int clipId : result.clipIds)
    {
        auto notes = read.getNotes (clipId);
        for (const auto& n : notes)
        {
            EXPECT_GT (n.velocity, 0) << "clip " << clipId << " noteId " << n.noteId;
        }
    }
}

TEST(ArrangementIntegration, VelocityRangeApplied)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    HDAW::ArrangementParams p;
    p.bars = 8;
    p.seed = 42;
    p.velocityMin = 60;
    p.velocityMax = 90;
    auto result = cmds.generateArrangement (p);

    EXPECT_GT (result.noteCount, 0);
    auto& read = engine.getReadModel();
    for (int clipId : result.clipIds)
    {
        auto notes = read.getNotes (clipId);
        for (const auto& n : notes)
        {
            EXPECT_GE (n.velocity, 60) << "velocity below min";
            EXPECT_LE (n.velocity, 90) << "velocity above max";
        }
    }
}
