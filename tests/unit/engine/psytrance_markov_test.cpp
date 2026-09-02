#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/PsytranceMarkovGenerator.h"
#include "engine/PsytranceGenerator.h"
#include "engine/PhraseGenerator.h"
#include "engine/AudioEngineCommands_Helpers.h"
#include "model/ProjectModel.h"
#include "frontend/router/Router_Composition.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

// PsytranceMarkovGenerator gates (guide §4B — incremental 2-bar generation):
// pure-engine determinism + limits + emergence + variants + key discipline,
// then the command/RPC layer round-trip (mirrors psytrance_generator_test).
// All stochastic gates aggregate over FIXED seed sets so they are fully
// deterministic — no flaky thresholds.

namespace {

HDAW::PsytranceMarkovParams baseParams(uint64_t seed = 42, int totalBars = 32)
{
    HDAW::PsytranceMarkovParams p;
    p.keyRoot = 5;   // F
    p.scaleMode = 1; // Minor (Aeolian)
    p.density = 0.7;
    p.seed = seed;
    p.totalBars = totalBars;
    p.kick = 0; p.bass = 1; p.hat = 2; p.arp = 3; p.stab = 4; p.pad = 5;
    p.riser = 6; p.down = 7; p.clap = 8;
    return p;
}

int percCount(const HDAW::MarkovStep& s)
{
    int n = 0;
    for (const auto& r : s.activeRoles)
        if (r == "kick" || r == "hat" || r == "clap") ++n;
    return n;
}

std::string actionOf(const HDAW::MarkovStep& s)
{
    return HDAW::PsytranceMarkovGenerator::actionName(s.action);
}

// Pitch classes of a scale, relative to its root (PhraseGenerator-driven).
std::vector<int> scaleIntervals(int scaleMode)
{
    // buildScalePitches(0, mode, 0, 11) returns the in-scale pcs in octave 0.
    return PhraseGenerator::buildScalePitches(0, scaleMode, 0, 11);
}

bool pcInScale(int pitch, int keyRoot, int scaleMode)
{
    static std::map<int, std::vector<int>> cache;
    auto it = cache.find(scaleMode);
    if (it == cache.end()) it = cache.emplace(scaleMode, scaleIntervals(scaleMode)).first;
    const int pc = ((pitch % 12) + 12) % 12;
    for (int iv : it->second)
        if (((keyRoot + iv) % 12 + 12) % 12 == pc) return true;
    return false;
}

const std::vector<HDAW::PsytranceNote>* notesOf(const HDAW::PsytranceMarkovScore& s,
                                                const char* role)
{
    for (const auto& c : s.clips)
        if (c.role == role) return &c.notes;
    return nullptr;
}

bool sameScore(const HDAW::PsytranceMarkovScore& a, const HDAW::PsytranceMarkovScore& b)
{
    if (a.clips.size() != b.clips.size() || a.steps.size() != b.steps.size()
        || a.automations.size() != b.automations.size() || a.notesTotal != b.notesTotal
        || a.totalBeats != b.totalBeats)
        return false;
    for (size_t i = 0; i < a.clips.size(); ++i)
    {
        if (a.clips[i].role != b.clips[i].role) return false;
        if (a.clips[i].notes.size() != b.clips[i].notes.size()) return false;
        for (size_t j = 0; j < a.clips[i].notes.size(); ++j)
        {
            const auto& x = a.clips[i].notes[j];
            const auto& y = b.clips[i].notes[j];
            if (x.startBeat != y.startBeat || x.pitch != y.pitch || x.velocity != y.velocity
                || x.durationBeats != y.durationBeats)
                return false;
        }
    }
    for (size_t i = 0; i < a.steps.size(); ++i)
        if (a.steps[i].barStart != b.steps[i].barStart
            || a.steps[i].action != b.steps[i].action
            || a.steps[i].targetRole != b.steps[i].targetRole
            || a.steps[i].activeRoles != b.steps[i].activeRoles
            || a.steps[i].keyRoot != b.steps[i].keyRoot)
            return false;
    return true;
}

} // namespace

// ── Key discipline: every pitched pc in the CURRENT window's scale, before
//    AND after a KeyChange. ────────────────────────────────────────────────
TEST(PsytranceMarkov, KeyDiscipline)
{
    auto p = baseParams(42, 48);
    p.everyBars = 16;
    p.keyShiftDegrees = 1; // F → G (deterministic direction for the assertion)
    const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
    ASSERT_TRUE(s.error.empty()) << s.error;

    int checked = 0;
    for (const char* role : { "kick", "bass", "arp", "stab", "pad", "riser", "down" })
    {
        const auto* notes = notesOf(s, role);
        if (notes == nullptr) continue; // presence is the coverage-union gate below
        for (const auto& n : *notes)
        {
            int stepIdx = std::min((int) (n.startBeat / 8.0), (int) s.steps.size() - 1);
            const int key = s.steps[(size_t) stepIdx].keyRoot;
            ASSERT_GE(key, 0);
            EXPECT_LE(key, 11);
            EXPECT_TRUE(pcInScale(n.pitch, key, p.scaleMode))
                << role << " pitch " << n.pitch << " outside scale of key " << key
                << " at beat " << n.startBeat;
            ++checked;
        }
    }
    EXPECT_GT(checked, 100);

    // Coverage union: every role must produce a clip for at least one fixed
    // seed (which seed lands where is the Markov's business — that a full
    // palette is reachable is the gate).
    for (const char* role : { "kick", "bass", "hat", "arp", "stab", "pad", "clap", "riser", "down" })
    {
        bool any = false;
        for (uint64_t seed : { 42ull, 43ull, 44ull })
        {
            auto ps = baseParams(seed, 48);
            ps.everyBars = 16;
            ps.keyShiftDegrees = 1;
            const auto ss = HDAW::PsytranceMarkovGenerator::generate(ps);
            ASSERT_TRUE(ss.error.empty()) << ss.error;
            any = any || (notesOf(ss, role) != nullptr);
        }
        EXPECT_TRUE(any) << role << " never produced a clip across seeds 42/43/44";
    }
}

// ── Determinism: same seed byte-identical; different seed (incl. high bits)
//    differs. ──────────────────────────────────────────────────────────────
TEST(PsytranceMarkov, Determinism)
{
    const auto a = HDAW::PsytranceMarkovGenerator::generate(baseParams(42, 32));
    const auto b = HDAW::PsytranceMarkovGenerator::generate(baseParams(42, 32));
    ASSERT_TRUE(a.error.empty()) << a.error;
    EXPECT_TRUE(sameScore(a, b));

    const auto c = HDAW::PsytranceMarkovGenerator::generate(baseParams(43, 32));
    EXPECT_FALSE(sameScore(a, c)) << "different seed must differ";

    // Full 64-bit seed drives the mt19937 (seed_seq from both halves).
    auto pHi = baseParams(42, 32);
    pHi.seed = 42ull + (1ull << 32);
    const auto d = HDAW::PsytranceMarkovGenerator::generate(pHi);
    EXPECT_FALSE(sameScore(a, d)) << "high seed bits must matter";
}

// ── Global min/max active tracks AND the percussive sublimit hold on EVERY
//    recorded window across several param shapes and seeds. ───────────────
TEST(PsytranceMarkov, MinMaxAndPercSublimitsEnforced)
{
    struct Cfg { int mn, mx, pmn, pmx; };
    const Cfg cfgs[] = { { 2, 4, 1, 2 }, { 2, 6, 1, 3 }, { 3, 5, 2, 3 }, { 4, 6, 1, 1 } };
    for (const auto& cf : cfgs)
    {
        for (uint64_t seed : { 1ull, 7ull, 42ull })
        {
            auto p = baseParams(seed, 64);
            p.minTracks = cf.mn; p.maxTracks = cf.mx;
            p.minPercTracks = cf.pmn; p.maxPercTracks = cf.pmx;
            const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
            ASSERT_TRUE(s.error.empty()) << s.error;
            ASSERT_FALSE(s.steps.empty());
            for (const auto& st : s.steps)
            {
                EXPECT_GE((int) st.activeRoles.size(), cf.mn) << "bar " << st.barStart;
                EXPECT_LE((int) st.activeRoles.size(), cf.mx) << "bar " << st.barStart;
                EXPECT_GE(percCount(st), cf.pmn) << "bar " << st.barStart;
                EXPECT_LE(percCount(st), cf.pmx) << "bar " << st.barStart;
            }
        }
    }
}

// ── Build/breakdown emergence: the default 32-bar run performs structural
//    moves (adds AND removes) — aggregate over fixed seeds so the gate is
//    deterministic yet not single-stream-fragile. ──────────────────────────
TEST(PsytranceMarkov, BuildBreakdownEmergence)
{
    int adds = 0, removes = 0, seedsWithBoth = 0;
    for (uint64_t seed : { 11ull, 22ull, 33ull, 44ull })
    {
        const auto s = HDAW::PsytranceMarkovGenerator::generate(baseParams(seed, 32));
        ASSERT_TRUE(s.error.empty()) << s.error;
        int a = 0, r = 0;
        for (const auto& st : s.steps)
        {
            if (actionOf(st) == "AddLayer") ++a;
            if (actionOf(st) == "RemoveLayer") ++r;
        }
        adds += a;
        removes += r;
        if (a > 0 && r > 0) ++seedsWithBoth;
    }
    EXPECT_GE(adds, 4) << "no build-up behavior emerged";
    EXPECT_GE(removes, 2) << "no breakdown behavior emerged";
    EXPECT_GE(seedsWithBoth, 2);
}

// ── Age bias: removed targets skew old — most removals pick the oldest
//    active role (uniform would give ~1/k). Aggregate over fixed seeds. ────
TEST(PsytranceMarkov, AgeBiasedRemoval)
{
    int removals = 0, oldestPicked = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed)
    {
        auto p = baseParams(seed, 64);
        p.density = 1.0;
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        ASSERT_TRUE(s.error.empty()) << s.error;
        for (size_t i = 1; i < s.steps.size(); ++i)
        {
            const auto& st = s.steps[i];
            if (actionOf(st) != "RemoveLayer") continue;
            const auto& prev = s.steps[i - 1];
            int targetAge = -1, maxAge = 0;
            for (size_t k = 0; k < prev.activeRoles.size(); ++k)
            {
                maxAge = std::max(maxAge, prev.ages[k]);
                if (prev.activeRoles[k] == st.targetRole) targetAge = prev.ages[k];
            }
            ASSERT_GE(targetAge, 0) << "removed role absent from previous window";
            ++removals;
            if (targetAge == maxAge) ++oldestPicked;
        }
    }
    ASSERT_GE(removals, 20) << "not enough removals to evaluate age bias";
    // weight = 1 + age*0.5: the oldest layer must dominate the picks.
    EXPECT_GT(oldestPicked, removals / 3)
        << "oldest=" << oldestPicked << " of " << removals;
}

// ── Variant actions occur and are audible: hat rhythm changes, gate length
//    changes note durations, pad-specific variation is reachable. ─────────
TEST(PsytranceMarkov, VariantsOccur)
{
    // Union across fixed seeds: WHICH seed shows a given variant is the
    // Markov's business — that every variant action is reachable and AUDIBLE
    // is the gate.
    bool rhythm = false, arp = false, nlen = false, hatChanged = false;
    std::set<int> durCents; // 1/100 beat resolution
    for (uint64_t seed : { 42ull, 43ull, 44ull, 45ull, 46ull, 47ull, 48ull, 49ull })
    {
        auto p = baseParams(seed, 64);
        p.density = 1.0;
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        ASSERT_TRUE(s.error.empty()) << s.error;

        for (const auto& st : s.steps)
        {
            rhythm = rhythm || actionOf(st) == "RhythmVariant";
            arp = arp || actionOf(st) == "ArpVariant";
            nlen = nlen || actionOf(st) == "NoteLengthVariant";
        }

        // Gate-length multiplier actually changes note durations (bass/arp/stab only).
        for (const char* role : { "bass", "arp", "stab" })
            if (auto* notes = notesOf(s, role))
                for (const auto& n : *notes)
                    durCents.insert((int) std::lround(n.durationBeats * 100.0));

        // Hat rhythm pattern changes: non-offbeat-8th grid positions appear.
        if (auto* hats = notesOf(s, "hat"))
            for (const auto& n : *hats)
            {
                const double frac = n.startBeat - std::floor(n.startBeat);
                const int sixteenth = (int) std::lround(frac * 4.0);
                if (sixteenth == 1 || sixteenth == 3) hatChanged = true; // x.25 / x.75
            }
    }
    EXPECT_TRUE(rhythm) << "no RhythmVariant across seeds 42/43/44/45/46/47/48/49";
    EXPECT_TRUE(arp) << "no ArpVariant across seeds 42/43/44/45/46/47/48/49";
    EXPECT_TRUE(nlen) << "no NoteLengthVariant across seeds 42/43/44/45/46/47/48/49";
    EXPECT_GT(durCents.size(), 1u) << "gate lengths never changed durations";
    EXPECT_TRUE(hatChanged) << "hat rhythm never left the offbeat-8th grid";
}

// ── Pad chords/gating: pads should become real triad/7th stacks and should
//    pulse on a subdivision grid when gated. ────────────────────────────
TEST(PsytranceMarkov, PadChordsAndGating)
{
    bool sawPadClip = false;
    bool sawChord = false;
    bool sawNonBarStart = false;
    for (uint64_t seed : { 42ull, 43ull, 44ull, 45ull, 46ull, 47ull, 48ull, 49ull })
    {
        auto p = baseParams(seed, 160);
        p.density = 1.0;
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        ASSERT_TRUE(s.error.empty()) << s.error;

        const auto* pads = notesOf(s, "pad");
        if (pads == nullptr) continue;
        sawPadClip = true;

        std::map<int, std::set<int>> byStart;
        for (const auto& n : *pads)
        {
            const int startTick = (int) std::lround(n.startBeat * 1000.0);
            byStart[startTick].insert((n.pitch % 12 + 12) % 12);

            int stepIdx = std::min((int) (n.startBeat / 8.0), (int) s.steps.size() - 1);
            const int key = s.steps[(size_t) stepIdx].keyRoot;
            EXPECT_TRUE(pcInScale(n.pitch, key, p.scaleMode))
                << "pad pitch " << n.pitch << " outside scale of key " << key
                << " at beat " << n.startBeat;

            const double frac = n.startBeat - std::floor(n.startBeat);
            if (std::fabs(frac) > 1e-6) sawNonBarStart = true;
        }

        for (const auto& [_, pcs] : byStart)
            if ((int) pcs.size() >= 3) sawChord = true;
    }
    EXPECT_TRUE(sawPadClip) << "pad never produced notes";
    EXPECT_TRUE(sawChord) << "pad never produced a chord stack";
    EXPECT_TRUE(sawNonBarStart) << "pad never landed on a non-bar-start grid position";
}

// ── Periodic KeyChange: exactly 2 shifts in 48 bars with everyBars=16, and
//    post-change notes follow the SHIFTED scale (covered with KeyDiscipline;
//    here the bookkeeping itself). ─────────────────────────────────────────
TEST(PsytranceMarkov, PeriodicKeyChange)
{
    auto p = baseParams(42, 48);
    p.everyBars = 16;
    p.keyShiftDegrees = 1; // F (5) → +1 scale degree → G (7) in minor
    const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
    ASSERT_TRUE(s.error.empty()) << s.error;

    std::vector<int> kcBars;
    for (const auto& st : s.steps)
        if (actionOf(st) == "KeyChange") kcBars.push_back(st.barStart);
    ASSERT_EQ(kcBars.size(), 2u);
    EXPECT_EQ(kcBars[0], 16);
    EXPECT_EQ(kcBars[1], 32);

    // The root FOLDS once per KeyChange: F -> G (bar 16) -> A (bar 32).
    int newRoot = p.keyRoot;
    for (size_t i = 0; i < kcBars.size(); ++i)
        newRoot = PhraseGenerator::scaleDegreeToPitch(12 * 6 + newRoot, p.scaleMode, 1, 0) % 12;
    EXPECT_EQ(s.steps.front().keyRoot, 5);
    EXPECT_EQ(s.steps.back().keyRoot, newRoot);

    // Notes strictly after the last change use the new key.
    const auto* bass = notesOf(s, "bass");
    ASSERT_NE(bass, nullptr);
    int postChecked = 0;
    for (const auto& n : *bass)
        if (n.startBeat >= 32.0 * 4.0)
        {
            EXPECT_TRUE(pcInScale(n.pitch, newRoot, p.scaleMode)) << "pitch " << n.pitch;
            ++postChecked;
        }
    EXPECT_GT(postChecked, 8);
}

// ── Cadence gating: bass and kick hold >= 8 bars (no remove/swap earlier),
//    verified over a fixed seed sweep. ─────────────────────────────────────
TEST(PsytranceMarkov, BassAndKickMinHold)
{
    int bassEvents = 0, kickEvents = 0, violations = 0, events = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed)
    {
        const auto s = HDAW::PsytranceMarkovGenerator::generate(baseParams(seed, 64));
        ASSERT_TRUE(s.error.empty()) << s.error;
        for (size_t i = 1; i < s.steps.size(); ++i)
        {
            const auto& st = s.steps[i];
            const auto act = actionOf(st);
            if (act != "RemoveLayer" && act != "SwapPattern") continue;
            const auto& prev = s.steps[i - 1];
            for (size_t k = 0; k < prev.activeRoles.size(); ++k)
            {
                if (prev.activeRoles[k] != st.targetRole) continue;
                if (st.targetRole == "bass")
                {
                    ++bassEvents;
                    if (prev.ages[k] < 8) ++violations;
                }
                if (st.targetRole == "kick")
                {
                    ++kickEvents;
                    if (prev.ages[k] < 8) ++violations;
                }
            }
            ++events;
        }
    }
    EXPECT_EQ(violations, 0) << "bass/kick changed before their 8-bar hold elapsed";
    EXPECT_GE(events, 20);
    EXPECT_GE(bassEvents + kickEvents, 2) << "gate never exercised (vacuous)";
}

// ── Two-tier cadence: micro/2-bar-clock actions dominate; structural mass
//    changes stay rare over a 32-bar default run. ──────────────────────────
TEST(PsytranceMarkov, MicroActionsDominate)
{
    int micro = 0, total = 0;
    for (uint64_t seed : { 5ull, 6ull, 7ull })
    {
        const auto s = HDAW::PsytranceMarkovGenerator::generate(baseParams(seed, 32));
        ASSERT_TRUE(s.error.empty()) << s.error;
        for (const auto& st : s.steps)
        {
            ++total;
            const auto act = actionOf(st);
            if (act == "Keep" || act == "FilterSweep" || act == "NoteLengthVariant"
                || act == "RhythmVariant" || act == "ArpVariant"
                || act == "FxHit" || act == "Breakbeat")
                ++micro;
        }
    }
    EXPECT_GE(total, 48);
    EXPECT_GE(100.0 * micro / (double) total, 60.0)
        << "micro fraction " << micro << "/" << total << " below 60%";
}

// ── Slow tier: section energy changes ONLY on sectionCycleBars boundaries. ─
TEST(PsytranceMarkov, SectionChangesOnlyOnSlowBoundaries)
{
    std::set<std::string> seen;
    bool anyChange = false;
    for (uint64_t seed : { 2ull, 3ull, 4ull, 5ull })
    {
        auto p = baseParams(seed, 96);
        p.sectionCycleBars = 16;
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        ASSERT_TRUE(s.error.empty()) << s.error;
        std::string prev;
        for (const auto& st : s.steps)
        {
            EXPECT_TRUE(st.section == "sparse" || st.section == "build"
                        || st.section == "peak" || st.section == "breakdown")
                << "unknown section " << st.section;
            if (!prev.empty() && prev != st.section)
            {
                // Jittered schedule: boundaries drift off the fixed grid, but
                // they must still land on a 2-bar window edge, never mid-window.
                EXPECT_EQ(st.barStart % 2, 0) << "section changed mid-window at bar "
                                              << st.barStart;
                anyChange = true;
            }
            prev = st.section;
            seen.insert(st.section);
        }
    }
    EXPECT_TRUE(anyChange) << "slow tier never re-rolled (vacuous)";
    EXPECT_GE(seen.size(), 2u) << "only one section state ever observed";
}

// ── Vague macro-structure: section lengths jitter across seeds (never a
//    fixed grid again), no state over-stays the cap, and a section change
//    is always audible (never a Keep on the transition window). ──────────
TEST(PsytranceMarkov, SectionTimingJittersAndCaps)
{
    std::set<int> runLengths;
    for (uint64_t seed : { 42ull, 43ull, 44ull, 45ull })
    {
        auto p = baseParams(seed, 128);
        p.sectionCycleBars = 32;
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        ASSERT_TRUE(s.error.empty()) << s.error;
        int run = 0;
        std::string prev;
        for (const auto& st : s.steps)
        {
            if (!prev.empty() && st.section != prev)
            {
                runLengths.insert(run);
                // forced progress blocks renewal past one base cycle, so a
                // single state can span at most ~base + one drawn length
                EXPECT_LE(run, 3 * 32) << "section ran " << run
                                       << " bars — over-stay cap exceeded";
                run = 0;
            }
            prev = st.section;
            run += 2;
        }
    }
    EXPECT_GE(runLengths.size(), 2u)
        << "section lengths never varied — the schedule collapsed back to a grid";
}

TEST(PsytranceMarkov, SectionTransitionsAreAudible)
{
    int transitions = 0;
    for (uint64_t seed : { 42ull, 43ull, 44ull, 45ull, 46ull })
    {
        auto p = baseParams(seed, 128);
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        ASSERT_TRUE(s.error.empty()) << s.error;
        std::string prev;
        for (const auto& st : s.steps)
        {
            if (!prev.empty() && st.section != prev)
            {
                ++transitions;
                EXPECT_NE(st.action, HDAW::MarkovAction::Keep)
                    << "silent section change at bar " << st.barStart;
            }
            prev = st.section;
        }
    }
    EXPECT_GE(transitions, 8) << "not enough section movement to gate on";
}

// ── Validation (Gate 9): tool-named errors, nothing generated. ────────────
TEST(PsytranceMarkov, ValidationRejectsBadParams)
{
    auto expectError = [](HDAW::PsytranceMarkovParams p, const char* what)
    {
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        EXPECT_FALSE(s.error.empty()) << what;
        EXPECT_TRUE(s.clips.empty()) << what;
        EXPECT_TRUE(s.steps.empty()) << what;
    };
    { auto p = baseParams(); p.keyRoot = 12; expectError(p, "keyRoot 12"); }
    { auto p = baseParams(); p.scaleMode = 13; expectError(p, "scaleMode 13"); }
    { auto p = baseParams(); p.density = 1.5; expectError(p, "density 1.5"); }
    { auto p = baseParams(); p.totalBars = 0; expectError(p, "totalBars 0"); }
    { auto p = baseParams(); p.totalBars = 258; expectError(p, "totalBars 258"); }
    { auto p = baseParams(); p.minTracks = 3; p.maxTracks = 2; expectError(p, "min>max"); }
    { auto p = baseParams(); p.minPercTracks = 2; p.maxPercTracks = 1; expectError(p, "perc min>max"); }
    { auto p = baseParams(); p.maxPercTracks = 4; expectError(p, "maxPerc 4 > pool"); }
    { auto p = baseParams(); p.minTracks = 1; p.maxTracks = 1; p.maxPercTracks = 2;
      expectError(p, "maxPerc>maxTracks"); }
    { auto p = baseParams(); p.minPercTracks = 3; p.minTracks = 2; expectError(p, "minPerc>minTracks"); }
    { auto p = baseParams(); p.everyBars = 5; expectError(p, "everyBars 5"); }
    { auto p = baseParams(); p.sectionCycleBars = 4; expectError(p, "sectionCycleBars 4"); }
    { auto p = baseParams(); p.bass = -2; expectError(p, "track index -2"); }
    { auto p = baseParams(); p.kick = -1; p.hat = -1; p.clap = -1;
      expectError(p, "minPerc without mapped perc"); }
    // odd totalBars rounds UP to the next even count (spec).
    {
        auto p = baseParams(9, 32 + 1);
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        EXPECT_TRUE(s.error.empty()) << s.error;
        EXPECT_DOUBLE_EQ(s.totalBeats, 34.0 * 4.0);
        EXPECT_EQ(s.steps.size(), 17u);
    }
    // no roles mapped at all
    {
        auto p = baseParams();
        p.kick = p.bass = p.hat = p.arp = p.stab = p.pad = p.riser = p.down = p.clap = -1;
        const auto s = HDAW::PsytranceMarkovGenerator::generate(p);
        EXPECT_FALSE(s.error.empty());
        EXPECT_NE(s.error.find("mapped"), std::string::npos) << s.error;
    }
}

// ── Command layer: full path writes one clip per produced role at beat 0
//    spanning totalBeats, ONE undo unit (Gate 2 path mirrors legacy). ──────
TEST(PsytranceMarkovCommand, RoundTripClipsOnRightTracksOneUndo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(140.0);
    engine.drainPendingRoutingRebuild();

    for (int i = 0; i < 9; ++i)
        ASSERT_GE(cmds.addTrack("Psy" + std::to_string(i), -1, -1, 0), 0);
    engine.drainPendingRoutingRebuild();

    auto p = baseParams(42, 32);
    auto r = cmds.generatePsytranceMarkov(p);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_FALSE(r.clips.empty());
    EXPECT_DOUBLE_EQ(r.totalBeats, 128.0);
    EXPECT_GT(r.notesTotal, 0);
    EXPECT_EQ(r.notesSkipped, 0);
    EXPECT_EQ(r.steps.size(), 16u);
    EXPECT_FALSE(r.steps.front().action.empty());
    EXPECT_EQ(r.steps.front().barStart, 0);

    // Pure generator is the source of truth for the note payload.
    const auto score = HDAW::PsytranceMarkovGenerator::generate(p);
    ASSERT_EQ(score.clips.size(), r.clips.size());

    auto& m = engine.getProjectModel();
    for (const auto& rc : r.clips)
    {
        ASSERT_GE(rc.trackIndex, 0);
        auto track = m.getTrackListTree().getChild(rc.trackIndex);
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        ASSERT_NE(clipList.getNumChildren(), 0) << rc.role << " clip missing";
        auto clip = clipList.getChild(0);
        auto notes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
        EXPECT_EQ(notes.getNumChildren(), rc.noteCount) << rc.role;
        const double durSec = HDAW::beatsToSeconds(128.0, 140.0);
        EXPECT_NEAR(static_cast<double>(clip.getProperty(IDs::duration)), durSec, 1e-6)
            << rc.role;
    }

    // ONE undo removes every generated clip (single transaction).
    cmds.undo();
    engine.drainPendingRoutingRebuild();
    int remaining = 0;
    for (int t = 0; t < 9; ++t)
    {
        auto track = m.getTrackListTree().getChild(t);
        remaining += track.getChildWithName(IDs::CLIP_LIST).getNumChildren();
    }
    EXPECT_EQ(remaining, 0);

    // Redo restores them as the same unit.
    cmds.redo();
    engine.drainPendingRoutingRebuild();
    int restored = 0;
    for (int t = 0; t < 9; ++t)
    {
        auto track = m.getTrackListTree().getChild(t);
        restored += track.getChildWithName(IDs::CLIP_LIST).getNumChildren();
    }
    EXPECT_EQ(restored, (int) r.clips.size());
}

// ── Command layer: out-of-range palette track → tool-named error, no
//    partial writes. ───────────────────────────────────────────────────────
TEST(PsytranceMarkovCommand, BadTrackRejectedNoPartialWrites)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    engine.drainPendingRoutingRebuild();

    auto p = baseParams(42, 32);
    p.bass = 999;
    for (int i = 0; i < 9; ++i)
        cmds.addTrack("T" + std::to_string(i), -1, -1, 0);
    engine.drainPendingRoutingRebuild();

    auto r = cmds.generatePsytranceMarkov(p);
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("generate_psytrance_markov"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("out of range"), std::string::npos) << r.error;
    EXPECT_TRUE(r.clips.empty());

    auto& m = engine.getProjectModel();
    for (int t = 0; t < 9; ++t)
        EXPECT_EQ(m.getTrackListTree().getChild(t)
                      .getChildWithName(IDs::CLIP_LIST)
                      .getNumChildren(),
                  0);
}

// ── Command layer: invalid params produce a tool-named error and no write. ─
TEST(PsytranceMarkovCommand, InvalidParamsToolNamedError)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    engine.drainPendingRoutingRebuild();
    for (int i = 0; i < 3; ++i)
        cmds.addTrack("V" + std::to_string(i), -1, -1, 0);
    engine.drainPendingRoutingRebuild();

    auto p = baseParams(42, 32);
    p.totalBars = 300; // out of 1..256
    auto r = cmds.generatePsytranceMarkov(p);
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("generate_psytrance_markov"), std::string::npos) << r.error;
    EXPECT_TRUE(r.clips.empty());
}

// ── Router twin: dispatchComposition round-trips the JSON the MCP/frontend
//    would send and returns the steps/automations payloads. ────────────────
TEST(PsytranceMarkovRouter, DispatchCompositionRoundTrip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(140.0);
    engine.drainPendingRoutingRebuild();
    for (int i = 0; i < 9; ++i)
        cmds.addTrack("R" + std::to_string(i), -1, -1, 0);
    engine.drainPendingRoutingRebuild();

    QJsonObject params;
    params["paletteTrackIds"] = QJsonObject{
        { "kick", 0 }, { "bass", 1 }, { "hat", 2 }, { "arp", 3 },
        { "stab", 4 }, { "pad", 5 }, { "riser", 6 }, { "down", 7 }, { "clap", 8 } };
    params["keyRoot"] = 5;
    params["scaleMode"] = 1;
    params["density"] = 0.7;
    params["seed"] = 42;
    params["totalBars"] = 32;
    params["minTracks"] = 2;
    params["maxTracks"] = 6;
    params["minPercTracks"] = 1;
    params["maxPercTracks"] = 3;
    params["everyBars"] = 32;

    auto res = frontend::dispatchComposition(engine, "generatePsytranceMarkov",
                                             QJsonValue(params));
    ASSERT_FALSE(res.isError);
    ASSERT_TRUE(res.payload.isObject());
    const auto payload = res.payload.toObject();
    EXPECT_DOUBLE_EQ(payload.value("totalBeats").toDouble(), 128.0);
    EXPECT_GT(payload.value("notesTotal").toInt(), 0);
    EXPECT_FALSE(payload.value("clips").toArray().isEmpty());
    EXPECT_EQ(payload.value("steps").toArray().size(), 16);
    EXPECT_TRUE(payload.value("steps").toArray().at(0).toObject().contains("section"));

    // Unknown method still errors (dispatch surface unchanged otherwise).
    auto bad = frontend::dispatchComposition(engine, "generatePsytranceMarkovX",
                                             QJsonValue(params));
    EXPECT_TRUE(bad.isError);
}
