#include "engine/PsytranceMarkovGenerator.h"
#include "engine/PhraseGenerator.h"
#include "engine/RhythmPatternGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <random>
#include <set>

namespace HDAW {

namespace {

inline int diaRoot(int pc, int octave) { return 12 * (octave + 1) + pc; }

// Chord-tone degree tables (identical to the legacy PsytranceGenerator
// voicings, so a Markov arp/stab sounds like the preplanned one).
const int kChordTones[7][4] = {
    { 0, 2, 4, 5 }, { 1, 3, 5, 6 }, { 2, 4, 6, 1 }, { 3, 5, 0, 2 },
    { 4, 6, 1, 3 }, { 5, 0, 2, 4 }, { 6, 1, 3, 5 } };

const int kDefaultProgA[8] = { 0, 5, 4, 5, 0, 5, 4, 5 }; // i-VII-VI-VII
const int kDefaultProgB[8] = { 4, 5, 0, 0, 4, 5, 2, 2 }; // VI-VII-i-i

inline int wrapDegree(int d, int len) { int r = d % len; return r < 0 ? r + len : r; }

inline bool isPercRole(const std::string& r)
{
    return r == "kick" || r == "hat" || r == "clap" || r == "snare" || r == "rim";
}

// ── Three-group element ontology (P0) ────────────────────────────────────
// FLOOR  = kick, bass      — protected tonal foundation (breakdown-only
//                            removal; hard-edged enters/leaves, NO fades).
// CORE   = arp, stab, pad, bass — persistent tonal identity, varied by
//                            synth tweaks (bass is BOTH floor-protected
//                            AND core-group; predicates overlap on purpose).
// PERC   = kick, hat, clap, snare, rim — isPercRole; themed pattern sets
//                            held >= 32 bars between flips (see PercTheme).
// FX     = riser, down     — composed texture accents.
inline bool isFloorRole(const std::string& r) { return r == "kick" || r == "bass"; }
inline bool isCoreRole(const std::string& r)
{
    return r == "arp" || r == "stab" || r == "pad" || r == "bass";
}
inline bool isFxRole(const std::string& r) { return r == "riser" || r == "down"; }

// Percussive THEME hold: the whole groove (hat/snare/rim grids + the kick
// broken flag) is one unit — a rotation must leave it in place for at least
// this many bars so the groove establishes before it mutates (themeAge is
// tracked in bars and gates RhythmVariant viability; a rotation resets it).
// The kick flag additionally rides its own clock (Breakbeat OR rotation
// resets it) so the backbeat structure never flips faster than the hold.
constexpr int kPercPatternHoldBars = 32;

// Core pool (layers that count toward min/max tracks), fixed order — the
// perc voices cluster up front (kick,bass,hat,snare,rim) and the melodic
// tail keeps its P0 relative order.
const char* kCoreRoles[9] = { "kick", "bass", "hat", "snare", "rim",
                              "arp", "stab", "pad", "clap" };
// Lead family = age-biased replacement candidates (spec: arp,stab,pad,bass).
const char* kLeadFamily[4] = { "bass", "arp", "stab", "pad" };

// ── Percussive themes (P1) ───────────────────────────────────────────────
// A theme is ONE coordinated groove statement: 16-step velocity grids per
// voice (0 = silent, else velocity) for hat/snare/rim plus the kick broken
// flag. Theme 0 is the canonical opener (offbeat-8th hats, beat-1 offbeat
// slightly accented, silent snare/rim, straight kick); themes 1..T-1 seed
// RhythmPatternGenerator::Params (euclidean, grid=16, bars=1 — the generator
// itself stays pure/no-RNG) from the generation mt19937 and convert the
// returned notes to step-velocity arrays (dedupe by step, louder wins).
// All voices of a theme rotate TOGETHER (unit rotation).
struct PercTheme {
    bool kickBroken = false;
    std::array<uint8_t, 16> hat{};   // 0 = silent, else velocity
    std::array<uint8_t, 16> snare{}; // pitch 38 (acoustic snare)
    std::array<uint8_t, 16> rim{};   // pitch 37 (side stick)
};

// Fixed-pitch perc voices — all distinct (clap 42, hat 44 below).
constexpr int kSnarePitch = 38;
constexpr int kRimPitch = 37;
constexpr int kHatPitch = 44;
constexpr int kClapPitch = 42;

// Canonical opener theme (bar 0 until the first rotation — >= 32 bars).
PercTheme canonicalTheme()
{
    PercTheme t;
    t.hat[2]  = 98;  // beat-1 offbeat accent (baked into the grid)
    t.hat[6]  = 92;
    t.hat[10] = 92;
    t.hat[14] = 92;
    return t;
}

// Filler grids for a voice the current theme leaves SILENT (theme 0 is the
// canonical snare/rim-free opener): while the voice is ACTIVE it plays the
// filler, so an added layer is never a silent fade target; the theme's own
// grid takes over on the next rotation. Filler velocities (100) sit OUTSIDE
// every seeded theme velocity range, so a theme grid can never equal a
// filler — unit rotation stays observable (a rotation always changes an
// active voice's bar signature).
const std::array<uint8_t, 16> kSnareFiller = [] {
    std::array<uint8_t, 16> g{}; // canonical 2/4 backbeat (layers with the clap)
    g[4] = 100; g[12] = 100;
    return g;
}();
const std::array<uint8_t, 16> kRimFiller = [] {
    std::array<uint8_t, 16> g{}; // sparse 16th pushes into beats 3 and 1
    g[7] = 100; g[15] = 100;
    return g;
}();

// A voice's effective grid: the theme's own cells when the theme HAS that
// voice, the canonical filler when it does not.
inline const std::array<uint8_t, 16>& gridOrFiller(
    const std::array<uint8_t, 16>& grid, const std::array<uint8_t, 16>& filler)
{
    for (uint8_t v : grid) if (v != 0) return grid;
    return filler;
}

// Slow-tier section-energy states (production-theory two-tier model).
enum class SectionEnergy { Sparse, Build, Peak, Breakdown };
const char* sectionName(SectionEnergy s)
{
    switch (s) {
        case SectionEnergy::Sparse:    return "sparse";
        case SectionEnergy::Build:     return "build";
        case SectionEnergy::Peak:      return "peak";
        case SectionEnergy::Breakdown: return "breakdown";
    }
    return "build";
}

struct RoleCtx {
    int track = -1;
    PsytranceClip clip;
    void add(double startBeat, int pitch, int velocity, double durationBeats, int maxNotes)
    {
        if ((int) clip.notes.size() >= maxNotes) return;
        if (pitch < 0 || pitch > 127) return;
        if (velocity < 1) velocity = 1;
        if (velocity > 127) velocity = 127;
        clip.notes.push_back({ startBeat, pitch, velocity, durationBeats });
    }
};

} // namespace

const char* PsytranceMarkovGenerator::actionName(MarkovAction a)
{
    switch (a) {
        case MarkovAction::Keep:              return "Keep";
        case MarkovAction::AddLayer:          return "AddLayer";
        case MarkovAction::RemoveLayer:       return "RemoveLayer";
        case MarkovAction::SwapPattern:       return "SwapPattern";
        case MarkovAction::FxHit:             return "FxHit";
        case MarkovAction::Breakbeat:         return "Breakbeat";
        case MarkovAction::FilterSweep:       return "FilterSweep";
        case MarkovAction::RhythmVariant:     return "RhythmVariant";
        case MarkovAction::ArpVariant:        return "ArpVariant";
        case MarkovAction::NoteLengthVariant: return "NoteLengthVariant";
        case MarkovAction::KeyChange:         return "KeyChange";
    }
    return "Keep";
}

PsytranceMarkovScore PsytranceMarkovGenerator::generate(const PsytranceMarkovParams& paramsIn)
{
    PsytranceMarkovScore score;
    const PsytranceMarkovParams p = paramsIn;

    // ── Validation (tool-named errors; the command layer prefixes the tool) ──
    if (p.keyRoot < 0 || p.keyRoot > 11) { score.error = "keyRoot must be in 0..11"; return score; }
    if (p.scaleMode < 0 || p.scaleMode > 12) { score.error = "scaleMode must be in 0..12"; return score; }
    if (p.density < 0.0 || p.density > 1.0) { score.error = "density must be in 0..1"; return score; }
    if (p.totalBars < 1 || p.totalBars > 256) { score.error = "totalBars must be in 1..256"; return score; }
    if (p.minTracks < 1) { score.error = "minTracks must be >= 1"; return score; }
    if (p.maxTracks > 9) { score.error = "maxTracks must be <= 9"; return score; }
    if (p.minTracks > p.maxTracks) { score.error = "minTracks must be <= maxTracks"; return score; }
    if (p.minPercTracks < 0) { score.error = "minPercTracks must be >= 0"; return score; }
    if (p.maxPercTracks > 5)
    { score.error = "maxPercTracks must be <= 5 (pool is kick+hat+clap+snare+rim)"; return score; }
    if (p.minPercTracks > p.maxPercTracks) { score.error = "minPercTracks must be <= maxPercTracks"; return score; }
    if (p.maxPercTracks > p.maxTracks) { score.error = "maxPercTracks must be <= maxTracks"; return score; }
    if (p.minPercTracks > p.minTracks) { score.error = "minPercTracks must be <= minTracks"; return score; }
    if (p.everyBars < 0 || (p.everyBars > 0 && p.everyBars < 8))
    { score.error = "everyBars must be 0 (off) or >= 8"; return score; }
    if (p.keyShiftDegrees < 0 || p.keyShiftDegrees > 11)
    { score.error = "keyShiftDegrees must be 0 (auto) or in 1..11"; return score; }
    if (p.sectionCycleBars < 0 || (p.sectionCycleBars > 0 && p.sectionCycleBars < 8))
    { score.error = "sectionCycleBars must be 0 (off) or >= 8"; return score; }

    // Role → palette track index for the core layers (FX roles excluded).
    auto roleTrack = [&p](const std::string& r) -> int {
        if (r == "kick")  return p.kick;
        if (r == "bass")  return p.bass;
        if (r == "hat")   return p.hat;
        if (r == "snare") return p.snare;
        if (r == "rim")   return p.rim;
        if (r == "arp")   return p.arp;
        if (r == "stab")  return p.stab;
        if (r == "pad")   return p.pad;
        if (r == "clap")  return p.clap;
        return -1;
    };

    const int roleTracks[] = { p.kick, p.bass, p.hat, p.snare, p.rim, p.arp,
                               p.stab, p.pad, p.riser, p.down, p.clap };
    for (int t : roleTracks)
        if (t < -1) { score.error = "track indices must be -1 (unmapped) or >= 0"; return score; }

    int mappedCount = 0, mappedPercCount = 0;
    for (const char* r : kCoreRoles)
    {
        const int t = roleTrack(r);
        if (t >= 0) { ++mappedCount; if (isPercRole(r)) ++mappedPercCount; }
    }
    if (mappedCount == 0) { score.error = "no palette roles mapped"; return score; }
    if (p.minTracks > mappedCount) { score.error = "minTracks exceeds mapped role count"; return score; }
    if (p.minPercTracks > mappedPercCount) { score.error = "minPercTracks exceeds mapped percussive roles"; return score; }

    // totalBars: even, round up (spec). 255 → 256, 1 → 2.
    const int totalBars = p.totalBars + (p.totalBars % 2);
    const double totalBeats = totalBars * 4.0;
    score.totalBeats = totalBeats;

    std::vector<int> progA = p.progressionA.empty()
        ? std::vector<int>(kDefaultProgA, kDefaultProgA + 8) : p.progressionA;
    std::vector<int> progB = p.progressionB.empty()
        ? std::vector<int>(kDefaultProgB, kDefaultProgB + 8) : p.progressionB;
    if (progA.empty()) progA.assign(1, 0);
    if (progB.empty()) progB.assign(1, 0);
    const int lenA = (int) progA.size(), lenB = (int) progB.size();

    // ── Rng: full 64-bit seed → mt19937 via seed_seq (fixed draw order below) ──
    std::seed_seq seedSeq { (uint32_t) (p.seed & 0xffffffffu), (uint32_t) (p.seed >> 32) };
    std::mt19937 rng(seedSeq);
    auto rng01 = [&rng]() {
        return (double) (rng() - rng.min()) / (double) (rng.max() - rng.min() + 1.0);
    };
    auto rngInt = [&rng](int lo, int hi) { // inclusive
        std::uniform_int_distribution<int> d(lo, hi);
        return d(rng);
    };
    const double density = std::clamp(p.density, 0.0, 1.0);

    // ── Role contexts ──
    auto makeRole = [](const char* role, int track) {
        RoleCtx rc; rc.track = track; rc.clip.role = role; rc.clip.trackIndex = track; return rc;
    };
    RoleCtx kick  = makeRole("kick",  p.kick);
    RoleCtx bass  = makeRole("bass",  p.bass);
    RoleCtx hat   = makeRole("hat",   p.hat);
    RoleCtx snare = makeRole("snare", p.snare);
    RoleCtx rim   = makeRole("rim",   p.rim);
    RoleCtx arp   = makeRole("arp",   p.arp);
    RoleCtx stab  = makeRole("stab",  p.stab);
    RoleCtx pad   = makeRole("pad",   p.pad);
    RoleCtx clap  = makeRole("clap",  p.clap);
    RoleCtx riser = makeRole("riser", p.riser);
    RoleCtx down  = makeRole("down",  p.down);
    const bool riserMapped = p.riser >= 0, downMapped = p.down >= 0;
    const int kMaxNotesPerClip = 8192; // MidiClipProcessor ceiling (legacy parity)
    const int scale = p.scaleMode;

    auto degPitch = [&](int keyRoot, int degree, int octave) {
        return PhraseGenerator::scaleDegreeToPitch(diaRoot(keyRoot, octave), scale, degree, 0);
    };

    // ── Key-change direction: ONE seeded choice at generation start ──
    const int keyDir = p.keyShiftDegrees > 0 ? p.keyShiftDegrees : rngInt(1, 2);
    int curKeyRoot = p.keyRoot;
    auto shiftKey = [&]() {
        const int base = diaRoot(curKeyRoot, 5);
        const int shifted = PhraseGenerator::scaleDegreeToPitch(base, scale, keyDir, 0);
        if (shifted > 0) curKeyRoot = shifted % 12;
    };

    // ── Start state: kick+hat if mapped (perc-min aware), fill to minTracks ──
    std::set<std::string> active;
    std::map<std::string, int> ages; // running bars; absent = 0 (fresh (re-)add)
    auto percActiveCount = [&active]() {
        int n = 0;
        for (const auto& r : active) if (isPercRole(r)) ++n;
        return n;
    };
    auto tryActivate = [&](const std::string& role) -> bool {
        bool mapped = false;
        for (const char* r : kCoreRoles) if (role == r)
        {
            mapped = roleTrack(role) >= 0;
            break;
        }
        if (!mapped || active.count(role)) return false;
        active.insert(role);
        ages.erase(role); // fresh start on (re-)add
        return true;
    };
    // Start state respects BOTH limit sets from bar 0: percussive adds are
    // capped by maxPercTracks, everything by maxTracks. The rolling bass and
    // the arp lead the stack (genre: full sections always carry them) — the
    // Markov can strip them later, but the track starts as a track.
    auto startActivate = [&](const std::string& role) -> bool {
        if ((int) active.size() >= p.maxTracks) return false;
        if (isPercRole(role) && percActiveCount() >= p.maxPercTracks) return false;
        return tryActivate(role);
    };
    startActivate("kick");
    startActivate("hat");
    while (percActiveCount() < p.minPercTracks) // perc floor (clap tops up)
    {
        if (p.clap >= 0 && !active.count("clap")) { if (!startActivate("clap")) break; }
        else break;
    }
    startActivate("bass");
    startActivate("arp");
    for (const char* r : { "stab", "pad" }) // melodic fill to the floor
    {
        if ((int) active.size() >= p.minTracks) break;
        startActivate(r);
    }

    // ── Percussive theme set (P1): drawn ONCE, at this fixed draw position
    //    before the window loop, then rotated as a UNIT ──
    // Theme 0 is the canonical opener. Themes 1..T-1 (T seeded 2..3) derive
    // each voice's 16-step grid from the euclidean RhythmPatternGenerator —
    // PURE, so its PARAMS are seeded from the mt19937 in a fixed order
    // (pulseA hits, pulseB hits, rotations, velocities) and the returned
    // notes convert to step-velocity arrays (dedupe by step, louder wins).
    // Snare leans ghost/soft with an optional 2/4 backbeat accent (90 on
    // steps 4/12), rim stays sparse, hats run denser; kickBroken draws ~35%.
    auto drawVoiceGrid = [&](int hitsLo, int hitsHi, int velALo, int velAHi,
                             int velBLo, int velBHi) -> std::array<uint8_t, 16> {
        RhythmPatternGenerator::Params rp;
        rp.grid = 16;
        rp.bars = 1;
        rp.pulseA = rngInt(hitsLo, hitsHi);
        rp.pulseB = rngInt(0, 3); // 0 disables the second pulse
        rp.rotationA = rngInt(0, 15);
        rp.rotationB = rngInt(0, 15);
        rp.velocityA = rngInt(velALo, velAHi);
        rp.velocityB = rngInt(velBLo, velBHi);
        // pitchA/pitchB stay defaults — a grid stores per-voice velocity only
        std::array<uint8_t, 16> grid{};
        for (const auto& n : RhythmPatternGenerator::generate(rp))
        {
            const int step = (int) std::lround(n.startBeat * 4.0);
            if (step < 0 || step > 15) continue;
            grid[(size_t) step] = (uint8_t) std::max<int>(grid[(size_t) step], n.velocity);
        }
        return grid;
    };
    auto drawTheme = [&]() -> PercTheme {
        PercTheme t;
        t.kickBroken = rng01() < 0.35;
        t.hat   = drawVoiceGrid(4, 12, 88, 104, 64, 84); // denser, brighter
        t.snare = drawVoiceGrid(2, 5, 60, 80, 45, 60);   // ghost/soft base
        if (rng01() < 0.5)                               // optional 2/4 backbeat
        {
            t.snare[4]  = 90;
            t.snare[12] = 90;
        }
        t.rim = drawVoiceGrid(1, 4, 70, 90, 50, 64);     // sparse
        return t;
    };
    const int themeCount = rngInt(2, 3);
    std::vector<PercTheme> themes;
    themes.reserve((size_t) themeCount);
    themes.push_back(canonicalTheme());
    for (int t = 1; t < themeCount; ++t) themes.push_back(drawTheme());
    int themeIndex = 0;  // current theme; RhythmVariant rotates (targetRole "theme")
    // Theme hold clocks in bars (P0 hold, now unit-wide): +windowBars per
    // window. themeAge resets on ROTATION only; kickClock resets on every
    // kick-pattern structure event (Breakbeat OR rotation — a rotation may
    // swap the kick flag too). A flip is only viable once its clock reaches
    // kPercPatternHoldBars — the groove establishes before it mutates.
    int themeAge = 0;
    int kickClock = 0;
    int arpDir = 0;              // 0 up, 1 down, 2 updown, 3 random-walk
    int arpRot = 0;              // degree-sequence rotation
    bool arpLiftWindow = false;  // +12 for one window
    int arpWalk = 0;             // random-walk position (0..3), bounces
    std::map<std::string, double> gate; // NoteLengthVariant multipliers
    gate["bass"] = 1.0; gate["arp"] = 1.0; gate["stab"] = 1.0;
    const double kGateSet[3] = { 0.5, 0.75, 1.0 }; // staccato .. full
    bool swapFlag = false;       // SwapPattern: A/B progression + bass octave

    // ── Slow tier: section energy (sparse/build/peak/breakdown) ──
    SectionEnergy section = (SectionEnergy) rngInt(0, 2); // seeded opener (no breakdown start)
    auto advanceSection = [&]() { // seeded transition at each slow boundary
        const double r = rng01();
        switch (section)
        {
            case SectionEnergy::Sparse:
                if (r < 0.65) section = SectionEnergy::Build;
                break;
            case SectionEnergy::Build:
                if (r < 0.60) section = SectionEnergy::Peak;
                break;
            case SectionEnergy::Peak:
                if (r < 0.45) section = SectionEnergy::Breakdown;
                else if (r >= 0.85) section = SectionEnergy::Sparse;
                break;
            case SectionEnergy::Breakdown:
                if (r < 0.75) section = SectionEnergy::Build;
                else if (r < 0.95) section = SectionEnergy::Sparse;
                break;
        }
    };

    // ── Vague macro-structure: jittered section schedule ──────────────────
    // p.sectionCycleBars is a gravity well, not a grid: the next slow
    // boundary is drawn seeded per section, biased by the section's
    // character (sparse/breakdown run short, peaks sustain). A state may
    // renew itself once within the base cycle but is then force-advanced,
    // so buildups/breakdowns drift in time yet always arrive.
    const int kBaseCycle = p.sectionCycleBars;
    auto evenClamp = [](int bars) { return std::max(8, (bars / 2) * 2); };
    auto drawSectionLength = [&](SectionEnergy s) -> int {
        const double r = rng01();
        double mult = 1.0;
        switch (s)
        {
            case SectionEnergy::Sparse:    mult = (r < 0.5 ? 0.5 : 0.75); break;
            case SectionEnergy::Build:     mult = (r < 0.5 ? 0.75 : 1.0); break;
            case SectionEnergy::Peak:      mult = (r < 0.4 ? 1.0 : (r < 0.75 ? 1.25 : 1.5)); break;
            case SectionEnergy::Breakdown: mult = (r < 0.5 ? 0.5 : 0.75); break;
        }
        return evenClamp((int) std::lround(kBaseCycle * mult));
    };
    int nextSectionBar = (kBaseCycle > 0) ? drawSectionLength(section) : 0;
    int sectionStartBar = 0;

    MarkovAction lastAction = MarkovAction::Keep;
    int repeats = 0;             // consecutive repeats of lastAction
    int keepStreak = 0;          // consecutive Keep windows (barsSinceChange)
    int windowsSinceStructural = 0; // windows since last Add/Remove/Swap

    // Fixed role iteration order for age-weighted draws (std::set is sorted).
    // floorMult > 1 biases the genre floor (kick/bass) — used by the
    // breakdown removal preference; 1.0 keeps the plain age-weighted draw.
    auto ageWeightedPick = [&](const std::vector<std::string>& candidates,
                               double floorMult = 1.0) -> std::string {
        if (candidates.empty()) return "";
        auto weight = [&](const std::string& c) {
            const double base = 1.0 + 0.5 * (double) ages[c];
            return (floorMult != 1.0 && (c == "kick" || c == "bass")) ? base * floorMult : base;
        };
        double totalW = 0.0;
        for (const auto& c : candidates) totalW += weight(c);
        double r = rng01() * totalW;
        for (const auto& c : candidates)
        {
            r -= weight(c);
            if (r < 0.0) return c;
        }
        return candidates.back();
    };
    auto oldestPick = [&](const std::vector<std::string>& candidates) -> std::string {
        std::string best;
        for (const auto& c : candidates)
            if (best.empty() || ages[c] > ages[best]) best = c;
        return best;
    };

    // ── Cadence gating (nested variation clocks) ──
    // 2-bar clock: percussion + micro actions fire any window.
    // 4-bar boundary: melodic (bass/arp/stab/pad) Add/Remove.
    // 8-bar hold: bass remove/swap and kick remove wait for age >= 8 bars.
    constexpr int kBassHoldBars = 8, kKickHoldBars = 8;
    auto canAddTarget = [&](const std::string& r, int bar) {
        const bool percR = isPercRole(r);
        if (!percR && bar % 4 != 0) return false; // melodic add: 4-bar boundary only
        const int count = (int) active.size();
        const int percN = percActiveCount();
        if (count < p.maxTracks)
            return percR ? percN < p.maxPercTracks : true;
        // count == maxTracks → evict path (swap keeps the total constant)
        if (percR) return percN < p.maxPercTracks;
        bool nonPercActive = false;
        for (const auto& a : active) if (!isPercRole(a)) { nonPercActive = true; break; }
        return nonPercActive || percN > p.minPercTracks;
    };
    auto canRemoveTarget = [&](const std::string& r, int bar) {
        if ((int) active.size() <= p.minTracks) return false;
        // Floor canon: kick+bass are the genre floor — they only drop out as
        // a tension device inside a breakdown (and are preferred to return at
        // the drop). With the section tier off (sectionCycleBars=0) the state
        // can never be breakdown, so the floor is simply never removed.
        if (isFloorRole(r) && section != SectionEnergy::Breakdown)
            return false;
        if (!isPercRole(r) && bar % 4 != 0) return false; // melodic remove: 4-bar boundary
        if (r == "bass" && ages[r] < kBassHoldBars) return false; // bass min-hold
        if (r == "kick" && ages[r] < kKickHoldBars) return false; // kick min-hold
        return !isPercRole(r) || percActiveCount() > p.minPercTracks;
    };
    // Theme-hold viability (P1): a rotation may only fire once the current
    // theme has held kPercPatternHoldBars AND at least one theme voice
    // (hat/snare/rim/kick) is audible — rotating with nothing audible would
    // spend the hold clock on a no-op. Used by the weight gating AND the
    // execution re-check.
    auto themeRotationViable = [&]() {
        if (themeAge < kPercPatternHoldBars) return false;
        for (const char* r : { "hat", "snare", "rim", "kick" })
            if (active.count(r) != 0) return true;
        return false;
    };

    // ── Volume fade automation (P0 ontology) ──
    // Non-floor layers enter/leave with engine-written volume fades; the
    // floor enters/leaves HARD-EDGED (hard edges are the point of the floor)
    // and bar-0 initial activations are never faded. The command layer
    // writes "volume" entries to real Volume lanes (paramID 1);
    // filterCutoff stays advisory.
    auto emitFadeIn = [&](const std::string& role, int bar) {
        if (bar <= 0 || isFloorRole(role)) return;
        const double lenBeats = 4.0 * (isCoreRole(role) ? 4 : 2); // core 4 bars, perc 2 bars
        score.automations.push_back({ role, "volume", bar * 4.0, 0.0, lenBeats });
        score.automations.push_back({ role, "volume", bar * 4.0 + lenBeats, 1.0, lenBeats });
    };
    auto emitFadeOut = [&](const std::string& role, int bar) {
        if (bar <= 0 || isFloorRole(role)) return;
        const double lenBeats = 8.0; // 2 bars
        const double startBeat = std::max(0.0, (bar - 2) * 4.0);
        score.automations.push_back({ role, "volume", startBeat, 1.0, lenBeats });
        score.automations.push_back({ role, "volume", bar * 4.0, 0.0, lenBeats });
    };

    for (int bar = 0; bar < totalBars; bar += 2)
    {
        const int activeCount = (int) active.size();
        arpLiftWindow = false;


        MarkovAction chosen = MarkovAction::Keep;
        std::string targetRole;

        // Fixed per-window draw order: (1) slow-tier section re-roll at the
        // boundary, (2) periodic KeyChange short-circuit, (3) THE action draw,
        // (4) execution target draws, (5) accent micro-variation, (6) note extras.
        const bool sectionBoundary = kBaseCycle > 0 && bar >= nextSectionBar;
        bool sectionChanged = false;
        if (sectionBoundary)
        {
            const SectionEnergy prevSection = section;
            const int sectionAge = bar - sectionStartBar;
            advanceSection();
            if (section == prevSection && sectionAge >= kBaseCycle)
            {
                // forced progress: no state may renew itself beyond the base
                // cycle — buildups/breakdowns drift, but they always arrive
                switch (section)
                {
                    case SectionEnergy::Sparse:    section = SectionEnergy::Build; break;
                    case SectionEnergy::Build:     section = SectionEnergy::Peak; break;
                    case SectionEnergy::Peak:      section = SectionEnergy::Breakdown; break;
                    case SectionEnergy::Breakdown: section = SectionEnergy::Build; break;
                }
            }
            sectionChanged = section != prevSection;
            if (sectionChanged) sectionStartBar = bar;
            nextSectionBar = bar + drawSectionLength(section);
        }

        const bool keyBoundary = p.everyBars > 0 && bar > 0 && bar % p.everyBars == 0;

        // ── Action selection (viability folds into weights so a single
        //       rng01() picks a viable action) ──
        if (keyBoundary)
        {
            chosen = MarkovAction::KeyChange;
        }
        else
        {
            const int range = std::max(1, p.maxTracks - p.minTracks);
            const double pos = (double) (activeCount - p.minTracks) / (double) range;
            bool anyAddable = false;
            for (const char* r : kCoreRoles)
                if (!active.count(r) && canAddTarget(r, bar)) { anyAddable = true; break; }
            bool pitchedActive = false, gateable = false;
            for (const auto& a : active)
            {
                if (a == "arp" || a == "stab" || a == "pad" || a == "bass") pitchedActive = true;
                if (a == "bass" || a == "arp" || a == "stab" || a == "pad") gateable = true;
            }
            // Theme-hold eligibility (P1): see themeRotationViable — the kick
            // flag rides its own clock (Breakbeat, or a rotation that swaps
            // the flag). Weights stay 0 with no eligible target so the single
            // draw is never biased by dead slots.
            const bool themeEligible = themeRotationViable();
            const bool kickEligible = active.count("kick") != 0
                                      && kickClock >= kPercPatternHoldBars;

            double wAdd    = anyAddable ? std::max(0.0, 1.6 - 1.3 * pos) : 0.0; // high near min
            double wRemove = 0.0;
            for (const auto& a : active)
                if (canRemoveTarget(a, bar)) { wRemove = 0.5 + 1.3 * pos; break; } // high near max
            double wKeep   = 2.0; // hypnotic anchor: most windows hold steady
            double wSwap   = pitchedActive ? 0.5 : 0.0;
            double wFx     = (riserMapped || downMapped) ? 0.35 : 0.0;
            double wBreak  = kickEligible ? 0.35 : 0.0;
            double wFilter = pitchedActive ? 0.45 : 0.0;
            double wRhythm = themeEligible ? 0.5 : 0.0;
            double wArp    = active.count("arp") ? 0.45 : 0.0;
            double wNoteLn = gateable ? 0.45 : 0.0;

            // Slow-tier bias (two-tier model): the section state shapes the
            // fast weights; the fast Markov still respects the limit sets.
            switch (section)
            {
                case SectionEnergy::Sparse:
                    wAdd *= 1.8; wRemove = 0.0; break;
                case SectionEnergy::Build:
                    wAdd *= 1.6; wRemove *= 0.5; break;
                case SectionEnergy::Peak:
                    wKeep *= 1.5; wSwap *= 1.2; wRemove *= 0.7; break;
                case SectionEnergy::Breakdown:
                    if (activeCount > p.minTracks) { wAdd = 0.0; wRemove *= 2.2; }
                    else wAdd *= 2.0; // subtracted to the floor → re-build
                    break;
            }

            if (activeCount >= p.maxTracks) wAdd *= 0.6;         // evict-swap is pricier
            // Density scales the variation probability (Keep is the anchor).
            const double varScale = 0.25 + 1.5 * density;
            wAdd *= varScale; wRemove *= varScale; wSwap *= varScale; wFx *= varScale;
            wBreak *= varScale; wFilter *= varScale; wRhythm *= varScale;
            wArp *= varScale; wNoteLn *= varScale;
            // Staleness ramp: when nothing structural happened for a while,
            // lean harder on swap/remove so elements never sit too long.
            if (windowsSinceStructural > 6)
            {
                const double ramp = std::min(1.0, (windowsSinceStructural - 6) / 4.0);
                wSwap += 0.30 * ramp; wRemove += 0.30 * ramp;
            }
            // No same action >2x in a row: penalize the streaked weight.
            auto streaked = [&](MarkovAction a, double& w) {
                if (a == lastAction && repeats >= 2) w *= 0.3;
            };
            streaked(MarkovAction::AddLayer, wAdd);       streaked(MarkovAction::RemoveLayer, wRemove);
            streaked(MarkovAction::Keep, wKeep);          streaked(MarkovAction::SwapPattern, wSwap);
            streaked(MarkovAction::FxHit, wFx);           streaked(MarkovAction::Breakbeat, wBreak);
            streaked(MarkovAction::FilterSweep, wFilter); streaked(MarkovAction::RhythmVariant, wRhythm);
            streaked(MarkovAction::ArpVariant, wArp);     streaked(MarkovAction::NoteLengthVariant, wNoteLn);
            // barsSinceChange: after 4 keeps, force a variation.
            if (keepStreak >= 4) wKeep = 0.0;

            const double totalW = wAdd + wRemove + wKeep + wSwap + wFx + wBreak
                                + wFilter + wRhythm + wArp + wNoteLn;
            double r = rng01() * totalW; // THE action draw (fixed slot order)
            auto take = [&](double w) { const bool hit = r < w; r -= w; return hit; };
            if (take(wAdd))          chosen = MarkovAction::AddLayer;
            else if (take(wRemove))  chosen = MarkovAction::RemoveLayer;
            else if (take(wKeep))    chosen = MarkovAction::Keep;
            else if (take(wSwap))    chosen = MarkovAction::SwapPattern;
            else if (take(wFx))      chosen = MarkovAction::FxHit;
            else if (take(wBreak))   chosen = MarkovAction::Breakbeat;
            else if (take(wFilter))  chosen = MarkovAction::FilterSweep;
            else if (take(wRhythm))  chosen = MarkovAction::RhythmVariant;
            else if (take(wArp))     chosen = MarkovAction::ArpVariant;
            else if (take(wNoteLn))  chosen = MarkovAction::NoteLengthVariant;
            else                     chosen = MarkovAction::Keep;

            // Fallback chain when a chosen action has no valid target under
            // the limit sets + cadence: Keep/Swap/FilterSweep (spec).
            auto viableRemove = [&]() {
                for (const auto& a : active) if (canRemoveTarget(a, bar)) return true;
                return false;
            };
            auto fallback = [&](std::initializer_list<MarkovAction> chain) {
                for (MarkovAction c : chain)
                {
                    if (c == MarkovAction::AddLayer)
                    {
                        for (const char* r : kCoreRoles)
                            if (!active.count(r) && canAddTarget(r, bar)) { chosen = c; return; }
                    }
                    else if (c == MarkovAction::RemoveLayer) { if (viableRemove()) { chosen = c; return; } }
                    else if (c == MarkovAction::SwapPattern) { if (pitchedActive) { chosen = c; return; } }
                    else if (c == MarkovAction::FilterSweep) { if (pitchedActive) { chosen = c; return; } }
                    else if (c == MarkovAction::Keep)        { chosen = c; return; }
                }
            };
            if (chosen == MarkovAction::AddLayer && !anyAddable)
                fallback({ MarkovAction::RemoveLayer, MarkovAction::SwapPattern,
                           MarkovAction::FilterSweep, MarkovAction::Keep });
            if (chosen == MarkovAction::RemoveLayer && !viableRemove())
                fallback({ MarkovAction::SwapPattern, MarkovAction::FilterSweep, MarkovAction::Keep });

            // A section change must be AUDIBLE: the transition window never
            // emits Keep — something structural/FX always marks the moment.
            // The chain is shaped by the NEW section (first entry preferred).
            if (sectionChanged && chosen == MarkovAction::Keep)
            {
                switch (section)
                {
                    case SectionEnergy::Sparse:
                        fallback({ MarkovAction::RemoveLayer, MarkovAction::FilterSweep }); break;
                    case SectionEnergy::Build:
                        fallback({ MarkovAction::AddLayer, MarkovAction::FilterSweep }); break;
                    case SectionEnergy::Peak:
                        fallback({ MarkovAction::SwapPattern, MarkovAction::FxHit,
                                   MarkovAction::FilterSweep }); break;
                    case SectionEnergy::Breakdown:
                        fallback({ MarkovAction::RemoveLayer, MarkovAction::FilterSweep }); break;
                }
            }
        }

        // ── Execution ──
        std::string fxPick; // KeyChange transition FX (empty = none)
        if (chosen == MarkovAction::KeyChange)
        {
            shiftKey();
            targetRole = "key";
            // Key changes are structural moments: a riser rises into the new
            // key or a downlifter lands on it (seeded pick, mapping-aware).
            if (riserMapped && downMapped) fxPick = rng01() < 0.5 ? "riser" : "down";
            else if (riserMapped)          fxPick = "riser";
            else if (downMapped)           fxPick = "down";
        }
        else if (chosen == MarkovAction::AddLayer)
        {
            std::vector<std::string> inactive;
            for (const char* r : kCoreRoles)
                if (!active.count(r) && canAddTarget(r, bar)) inactive.push_back(r);
            if (!inactive.empty())
            {
                // Drop-return bias: the transition INTO Build is THE drop —
                // prefer the floor's return (kick first, then bass) with a
                // seeded 75% draw. Bias, never force: when the draw declines
                // or the floor is not addable, the uniform pick below runs.
                std::string floorPref;
                if (sectionChanged && section == SectionEnergy::Build)
                {
                    if (!active.count("kick") && canAddTarget("kick", bar)) floorPref = "kick";
                    else if (!active.count("bass") && canAddTarget("bass", bar)) floorPref = "bass";
                }
                if (!floorPref.empty() && rng01() < 0.75)
                    targetRole = floorPref;
                else
                    targetRole = inactive[rngInt(0, (int) inactive.size() - 1)];
                if ((int) active.size() >= p.maxTracks)
                {
                    // evict-on-max: age-weighted replacement, cadence + perc-min aware.
                    std::vector<std::string> evictable, evictableNonPerc;
                    for (const auto& a : active)
                    {
                        if (!canRemoveTarget(a, bar)) continue;
                        if (isPercRole(a)) continue; // prefer non-perc evictees
                        evictableNonPerc.push_back(a);
                    }
                    if (evictableNonPerc.empty())
                        for (const auto& a : active)
                            if (canRemoveTarget(a, bar)) evictable.push_back(a);
                    const std::string evict = ageWeightedPick(
                        !evictableNonPerc.empty() ? evictableNonPerc : evictable);
                    if (evict.empty())
                    {
                        // No legal eviction under the limit sets → hold instead
                        // of exceeding maxTracks.
                        chosen = MarkovAction::Keep;
                        targetRole.clear();
                    }
                    else
                    {
                        active.erase(evict);
                        emitFadeOut(evict, bar);
                    }
                }
                // The fade rides the AddLayer ACTION (target picked, eviction
                // did not downgrade to Keep) — including targets whose track
                // is unmapped: those reach the command layer and are counted
                // in automationsSkipped rather than silently dropped.
                if (chosen == MarkovAction::AddLayer)
                {
                    tryActivate(targetRole);
                    emitFadeIn(targetRole, bar);
                }
            }
        }
        else if (chosen == MarkovAction::RemoveLayer)
        {
            std::vector<std::string> removable;
            for (const auto& a : active) if (canRemoveTarget(a, bar)) removable.push_back(a);
            if (!removable.empty())
            {
                // Breakdown drop preference: bias the pick toward the floor
                // (kick/bass) so the tension actually strips the foundation;
                // a draw landing elsewhere is fine (bias, never force).
                const double floorMult = section == SectionEnergy::Breakdown ? 8.0 : 1.0;
                targetRole = ageWeightedPick(removable, floorMult); // longest-running most likely
                active.erase(targetRole);
                ages.erase(targetRole);
                emitFadeOut(targetRole, bar);
            }
        }
        else if (chosen == MarkovAction::SwapPattern)
        {
            swapFlag = !swapFlag;
            std::vector<std::string> leads;
            for (const char* r : kLeadFamily)
                if (active.count(r) && !(r == std::string("bass") && ages[r] < kBassHoldBars))
                    leads.push_back(r); // bass min-hold covers swap too
            if (!leads.empty())
            {
                targetRole = ageWeightedPick(leads);
            }
        }
        else if (chosen == MarkovAction::FxHit)
        {
            if (riserMapped && downMapped) targetRole = rng01() < 0.5 ? "riser" : "down";
            else if (riserMapped)          targetRole = "riser";
            else                           targetRole = "down";
        }
        else if (chosen == MarkovAction::Breakbeat)
        {
            // Toggle the CURRENT theme's kick flag — it persists per theme,
            // so rotating back to that theme later keeps the flipped feel.
            themes[(size_t) themeIndex].kickBroken =
                !themes[(size_t) themeIndex].kickBroken;
            kickClock = 0;            // the kick pattern flipped: kick clock restarts
            targetRole = "kick";
        }
        else if (chosen == MarkovAction::FilterSweep)
        {
            std::vector<std::string> pitched;
            for (const char* r : kLeadFamily) if (active.count(r)) pitched.push_back(r);
            targetRole = oldestPick(pitched); // oldest pitched active role
            if (!targetRole.empty())
                score.automations.push_back({ targetRole, "filterCutoff", bar * 4.0,
                                              0.2 + rng01() * 0.6, 8.0 });
        }
        else if (chosen == MarkovAction::RhythmVariant)
        {
            // Rotate the WHOLE theme one step (hat/snare/rim grids + kick
            // flag move together) — only a theme that has held
            // kPercPatternHoldBars is eligible (weight gating above); this
            // re-check mirrors the other no-target paths.
            if (themeRotationViable())
            {
                themeIndex = (themeIndex + 1) % (int) themes.size();
                themeAge = 0;  // rotation is the ONLY themeAge reset
                kickClock = 0; // the incoming theme may carry a different kick flag
                targetRole = "theme";
            }
            else chosen = MarkovAction::Keep;
        }
        else if (chosen == MarkovAction::ArpVariant)
        {
            if (active.count("arp"))
            {
                targetRole = "arp";
                switch (rngInt(0, 2))
                {
                    case 0: arpDir = rngInt(0, 3); break;
                    case 1: arpRot += rngInt(1, 3); break;
                    default: arpLiftWindow = true; break; // +12 for this window
                }
            }
            else chosen = MarkovAction::Keep;
        }
        else if (chosen == MarkovAction::NoteLengthVariant)
        {
            std::vector<std::string> gateable;
            for (const char* r : { "bass", "arp", "stab" }) if (active.count(r)) gateable.push_back(r);
            if (!gateable.empty())
            {
                targetRole = gateable[rngInt(0, (int) gateable.size() - 1)];
                auto gateIndex = [](double v) {
                    return v < 0.625 ? 0 : (v < 0.875 ? 1 : 2);
                };
                const int cur = gateIndex(gate[targetRole]);
                int next = cur;
                while (next == cur) next = rngInt(0, 2);
                gate[targetRole] = kGateSet[next];
            }
            else chosen = MarkovAction::Keep;
        }
        // Streak bookkeeping (barsSinceChange forces variation after 4 keeps).
        if (chosen == lastAction) ++repeats;
        else { lastAction = chosen; repeats = 1; }
        if (chosen == MarkovAction::Keep) ++keepStreak; else keepStreak = 0;
        if (chosen == MarkovAction::AddLayer || chosen == MarkovAction::RemoveLayer
            || chosen == MarkovAction::SwapPattern)
            windowsSinceStructural = 0;
        else
            ++windowsSinceStructural;

        // ── Notes for this window ──
        // The theme grids ARE the velocity-shaping carrier now (P1): accents
        // live in the grids themselves (theme 0 bakes its beat-1 offbeat
        // accent into the grid), so the per-window accent jitter and the
        // density-gated flourish are retired — a bar's grid signature only
        // changes when the THEME rotates, which keeps unit rotation audible
        // and verifiable.
        const int windowBars = std::min(2, totalBars - bar);
        for (int wb = 0; wb < windowBars; ++wb)
        {
            const int curBar = bar + wb;
            const int deg = swapFlag ? progB[wrapDegree(curBar, lenB)]
                                     : progA[wrapDegree(curBar, lenA)];
            const int kickPitch = diaRoot(curKeyRoot, 2);

            // HAT/SNARE/RIM — the current theme's 16-step velocity grids
            // (step s lands at s*0.25 beats; all three rotate as a unit).
            // A theme may leave a voice silent (theme 0 has no snare/rim):
            // an ACTIVE voice then plays its canonical filler (see
            // kSnareFiller/kRimFiller) so added layers are never silent.
            const PercTheme& theme = themes[(size_t) themeIndex];
            if (hat.track >= 0 || snare.track >= 0 || rim.track >= 0)
            {
                const auto& snareGrid = gridOrFiller(theme.snare, kSnareFiller);
                const auto& rimGrid   = gridOrFiller(theme.rim,   kRimFiller);
                const double barBeat = curBar * 4.0;
                for (int st = 0; st < 16; ++st)
                {
                    const double stepBeat = barBeat + st * 0.25;
                    if (active.count("hat") && hat.track >= 0 && theme.hat[(size_t) st])
                        hat.add(stepBeat, kHatPitch, theme.hat[(size_t) st],
                                0.2, kMaxNotesPerClip);
                    if (active.count("snare") && snare.track >= 0 && snareGrid[(size_t) st])
                        snare.add(stepBeat, kSnarePitch, snareGrid[(size_t) st],
                                  0.15, kMaxNotesPerClip);
                    if (active.count("rim") && rim.track >= 0 && rimGrid[(size_t) st])
                        rim.add(stepBeat, kRimPitch, rimGrid[(size_t) st],
                                0.15, kMaxNotesPerClip);
                }
            }

            for (int b = 0; b < 4; ++b)
            {
                const double beatAbs = curBar * 4.0 + b;

                // KICK — 4-on-floor, or the current theme's broken flag
                // (beat 2 dropped); accent on beat 1
                if (active.count("kick") && kick.track >= 0)
                {
                    if (!theme.kickBroken || b != 2)
                        kick.add(beatAbs, kickPitch, b == 0 ? 127 : 122, 1.9, kMaxNotesPerClip);
                }
                // BASS — offbeat 8th, gate-multiplied (NoteLengthVariant)
                if (active.count("bass") && bass.track >= 0)
                {
                    const int oct = swapFlag ? 3 : 2; // SwapPattern lifts the octave
                    bass.add(beatAbs + 0.5, degPitch(curKeyRoot, deg, oct), 112,
                             0.4 * gate["bass"], kMaxNotesPerClip);
                }
                // ARP — 16th chord-tone pattern (ArpVariant: direction/rotation/lift)
                if (active.count("arp") && arp.track >= 0)
                {
                    const int chordDeg = wrapDegree(deg + arpRot, 7);
                    const int* tones = kChordTones[chordDeg];
                    for (int s = 0; s < 4; ++s)
                    {
                        int idx;
                        switch (arpDir)
                        {
                            case 1:  idx = 3 - s; break;            // down
                            case 2:  idx = (s == 3) ? 2 : s; break; // updown
                            case 3:                                  // random-walk (bounces)
                                idx = arpWalk;
                                arpWalk += (arpWalk == 3 ? -1 : (arpWalk == 0 ? 1 : (s % 2 ? -1 : 1)));
                                if (arpWalk < 0) arpWalk = 1;
                                if (arpWalk > 3) arpWalk = 2;
                                break;
                            default: idx = s; break;                // up
                        }
                        int pitch = degPitch(curKeyRoot, tones[idx], 3);
                        if (pitch >= 0)
                        {
                            if (idx == 3) pitch += 12;      // legacy +12 glint
                            if (arpLiftWindow) pitch += 12; // ArpVariant octave lift
                        }
                        arp.add(beatAbs + s * 0.25, pitch, 85, 0.2 * gate["arp"], kMaxNotesPerClip);
                    }
                }
                // STAB — triad on beat 2, gate-multiplied
                if (active.count("stab") && stab.track >= 0 && b == 1)
                {
                    const int chordDeg = wrapDegree(deg, 7);
                    stab.add(beatAbs, degPitch(curKeyRoot, kChordTones[chordDeg][0], 3), 96,
                             1.3 * gate["stab"], kMaxNotesPerClip);
                    stab.add(beatAbs, degPitch(curKeyRoot, kChordTones[chordDeg][1], 3), 96,
                             1.3 * gate["stab"], kMaxNotesPerClip);
                    stab.add(beatAbs, degPitch(curKeyRoot, kChordTones[chordDeg][2], 3), 96,
                             1.3 * gate["stab"], kMaxNotesPerClip);
                }
                // PAD — thick full chord voicings; gated pads pulse on a subdivision grid.
                if (active.count("pad") && pad.track >= 0 && b == 0)
                {
                    const int chordDeg = wrapDegree(deg, 7);
                    const int* tones = kChordTones[chordDeg];
                    const int root = degPitch(curKeyRoot, tones[0], 2);
                    const int third = degPitch(curKeyRoot, tones[1], 3);
                    const int fifth = degPitch(curKeyRoot, tones[2], 3);
                    const int seventh = degPitch(curKeyRoot, tones[3], 4);
                    auto addPadChord = [&](double startBeat, int vel, double dur) {
                        pad.add(startBeat, root,    vel,      dur, kMaxNotesPerClip);
                        pad.add(startBeat, third,   vel - 4,  dur, kMaxNotesPerClip);
                        pad.add(startBeat, fifth,   vel - 8,  dur, kMaxNotesPerClip);
                        pad.add(startBeat, seventh, vel - 12, dur, kMaxNotesPerClip);
                    };
                    if (gate["pad"] < 1.0)
                    {
                        const bool useSixteenth = totalBars <= 128;
                        const double step = useSixteenth ? 0.25 : 0.5;
                        const double dur = useSixteenth ? 0.10 * gate["pad"] : 0.18 * gate["pad"];
                        for (double off = 0.0; off < 4.0; off += step)
                            addPadChord(beatAbs + off, off == 0.0 ? 102 : 66, dur);
                    }
                    else
                        addPadChord(beatAbs, 84, 4.0);
                }
                // CLAP — canonical 2/4 backbeat (theme-independent by design)
                if (active.count("clap") && clap.track >= 0 && (b == 1 || b == 3))
                    clap.add(beatAbs, kClapPitch, 100, 0.15, kMaxNotesPerClip);
            }
        }
        // FX accents (FxHit windows + KeyChange transition FX) — pitched from
        // the CURRENT key.
        const std::string fxRole = (chosen == MarkovAction::FxHit) ? targetRole : fxPick;
        if (!fxRole.empty() && (chosen == MarkovAction::FxHit || chosen == MarkovAction::KeyChange))
        {
            const double windowStart = bar * 4.0;
            const int riserPitch = diaRoot(curKeyRoot, 3);
            const int downPitch = diaRoot(curKeyRoot, 2);
            if (fxRole == "riser" && riserMapped)
            {
                for (int i = 0; i < 8; ++i)
                    riser.add(windowStart + i * 0.5, riserPitch, 60 + 7 * i, 0.45, kMaxNotesPerClip);
            }
            else if (fxRole == "down" && downMapped)
                down.add(windowStart, downPitch, 95, 8.0, kMaxNotesPerClip);
        }

        // ── Age update: +windowBars per active role (reset happens on re-add);
        //    the step below records POST-update ages = bars actually run by the
        //    time this window's audio ends — exactly what the next window's
        //    cadence guard (and the min-hold test) compare against. The theme
        //    clocks advance every window regardless of layer state; themeAge
        //    resets on rotation only, kickClock also on Breakbeat. ──
        for (const auto& a : active) ages[a] += windowBars;
        themeAge += windowBars;
        kickClock += windowBars;

        // ── Record the step (post-action snapshot + post-update ages) ──
        MarkovStep step;
        step.barStart = bar;
        step.action = chosen;
        step.targetRole = targetRole;
        step.activeRoles.assign(active.begin(), active.end());
        for (const auto& a : step.activeRoles) step.ages.push_back(ages[a]);
        step.keyRoot = curKeyRoot;
        step.section = sectionName(section);
        score.steps.push_back(step);
    }

    // ── Assemble score (legacy clip shape; sorted notes; skipped roles) ──
    auto markSkipped = [&score](const std::string& role) {
        if (std::find(score.skipped.begin(), score.skipped.end(), role) == score.skipped.end())
            score.skipped.push_back(role);
    };
    for (RoleCtx* rc : { &kick, &bass, &hat, &snare, &rim, &arp, &stab, &pad,
                         &clap, &riser, &down })
    {
        if (rc->track < 0 || rc->clip.notes.empty()) { markSkipped(rc->clip.role); continue; }
        std::sort(rc->clip.notes.begin(), rc->clip.notes.end(),
                  [](const PsytranceNote& x, const PsytranceNote& y) { return x.startBeat < y.startBeat; });
        score.notesTotal += (int) rc->clip.notes.size();
        score.clips.push_back(std::move(rc->clip));
    }
    return score;
}

} // namespace HDAW
