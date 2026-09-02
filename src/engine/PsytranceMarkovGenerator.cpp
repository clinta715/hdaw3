#include "engine/PsytranceMarkovGenerator.h"
#include "engine/PhraseGenerator.h"

#include <algorithm>
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
    return r == "kick" || r == "hat" || r == "clap";
}

// Core pool (layers that count toward min/max tracks), fixed order.
const char* kCoreRoles[7] = { "kick", "bass", "hat", "arp", "stab", "pad", "clap" };
// Lead family = age-biased replacement candidates (spec: arp,stab,pad,bass).
const char* kLeadFamily[4] = { "bass", "arp", "stab", "pad" };

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
    if (p.maxTracks > 7) { score.error = "maxTracks must be <= 7"; return score; }
    if (p.minTracks > p.maxTracks) { score.error = "minTracks must be <= maxTracks"; return score; }
    if (p.minPercTracks < 0) { score.error = "minPercTracks must be >= 0"; return score; }
    if (p.maxPercTracks > 3) { score.error = "maxPercTracks must be <= 3 (pool is kick+hat+clap)"; return score; }
    if (p.minPercTracks > p.maxPercTracks) { score.error = "minPercTracks must be <= maxPercTracks"; return score; }
    if (p.maxPercTracks > p.maxTracks) { score.error = "maxPercTracks must be <= maxTracks"; return score; }
    if (p.minPercTracks > p.minTracks) { score.error = "minPercTracks must be <= minTracks"; return score; }
    if (p.everyBars < 0 || (p.everyBars > 0 && p.everyBars < 8))
    { score.error = "everyBars must be 0 (off) or >= 8"; return score; }
    if (p.keyShiftDegrees < 0 || p.keyShiftDegrees > 11)
    { score.error = "keyShiftDegrees must be 0 (auto) or in 1..11"; return score; }
    if (p.sectionCycleBars < 0 || (p.sectionCycleBars > 0 && p.sectionCycleBars < 8))
    { score.error = "sectionCycleBars must be 0 (off) or >= 8"; return score; }

    int roleTracks[9] = { p.kick, p.bass, p.hat, p.arp, p.stab, p.pad, p.riser, p.down, p.clap };
    for (int t : roleTracks)
        if (t < -1) { score.error = "track indices must be -1 (unmapped) or >= 0"; return score; }

    int mappedCount = 0, mappedPercCount = 0;
    for (const char* r : kCoreRoles)
    {
        const int t = r == std::string("kick") ? p.kick  : r == std::string("bass") ? p.bass
                    : r == std::string("hat")  ? p.hat   : r == std::string("arp")  ? p.arp
                    : r == std::string("stab") ? p.stab  : r == std::string("pad")  ? p.pad
                                                   : p.clap;
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
        for (const char* r : { "kick", "hat", "clap" }) if (active.count(r)) ++n;
        return n;
    };
    auto tryActivate = [&](const std::string& role) -> bool {
        bool mapped = false;
        for (const char* r : kCoreRoles) if (role == r)
        {
            const int t = role == "kick" ? p.kick  : role == "bass" ? p.bass
                        : role == "hat"  ? p.hat   : role == "arp"  ? p.arp
                        : role == "stab" ? p.stab  : role == "pad"  ? p.pad
                                                           : p.clap;
            mapped = t >= 0;
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

    // ── Variation state (mutated by the variant actions) ──
    int hatMode = 0;             // 0 offbeat 8ths, 1 16ths, 2 roll-bars, 3 ghost shuffle
    bool kickBroken = false;     // 4-on-floor vs broken (Breakbeat/RhythmVariant)
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
    auto ageWeightedPick = [&](const std::vector<std::string>& candidates) -> std::string {
        if (candidates.empty()) return "";
        double totalW = 0.0;
        for (const auto& c : candidates) totalW += 1.0 + 0.5 * (double) ages[c];
        double r = rng01() * totalW;
        for (const auto& c : candidates)
        {
            r -= 1.0 + 0.5 * (double) ages[c];
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
        if (!isPercRole(r) && bar % 4 != 0) return false; // melodic remove: 4-bar boundary
        if (r == "bass" && ages[r] < kBassHoldBars) return false; // bass min-hold
        if (r == "kick" && ages[r] < kKickHoldBars) return false; // kick min-hold
        return !isPercRole(r) || percActiveCount() > p.minPercTracks;
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
            bool pitchedActive = false, variantable = false, gateable = false;
            for (const auto& a : active)
            {
                if (a == "arp" || a == "stab" || a == "pad" || a == "bass") pitchedActive = true;
                if (a == "kick" || a == "hat") variantable = true;
                if (a == "bass" || a == "arp" || a == "stab" || a == "pad") gateable = true;
            }

            double wAdd    = anyAddable ? std::max(0.0, 1.6 - 1.3 * pos) : 0.0; // high near min
            double wRemove = 0.0;
            for (const auto& a : active)
                if (canRemoveTarget(a, bar)) { wRemove = 0.5 + 1.3 * pos; break; } // high near max
            double wKeep   = 2.0; // hypnotic anchor: most windows hold steady
            double wSwap   = pitchedActive ? 0.5 : 0.0;
            double wFx     = (riserMapped || downMapped) ? 0.35 : 0.0;
            double wBreak  = active.count("kick") ? 0.35 : 0.0;
            double wFilter = pitchedActive ? 0.45 : 0.0;
            double wRhythm = variantable ? 0.5 : 0.0;
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
                        active.erase(evict);
                }
                if (chosen == MarkovAction::AddLayer)
                    tryActivate(targetRole);
            }
        }
        else if (chosen == MarkovAction::RemoveLayer)
        {
            std::vector<std::string> removable;
            for (const auto& a : active) if (canRemoveTarget(a, bar)) removable.push_back(a);
            if (!removable.empty())
            {
                targetRole = ageWeightedPick(removable); // longest-running most likely
                active.erase(targetRole);
                ages.erase(targetRole);
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
            kickBroken = !kickBroken; // alternates 4-on-floor / broken
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
            // Mutate one percussive role's rhythm for >= 1 window.
            const bool hatOn = active.count("hat") != 0, kickOn = active.count("kick") != 0;
            if (hatOn && kickOn) targetRole = rngInt(0, 1) == 0 ? "hat" : "kick";
            else if (hatOn)      targetRole = "hat";
            else if (kickOn)     targetRole = "kick";
            if (targetRole == "hat") hatMode = rngInt(0, 3);
            else if (targetRole == "kick") kickBroken = !kickBroken;
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
        // Accent micro-variation: one seeded accent shift per window (a 2-bar
        // clock micro action — constant variation that never announces itself).
        const int hatAccent = rngInt(-6, 6);
        const int windowBars = std::min(2, totalBars - bar);
        for (int wb = 0; wb < windowBars; ++wb)
        {
            const int curBar = bar + wb;
            const int deg = swapFlag ? progB[wrapDegree(curBar, lenB)]
                                     : progA[wrapDegree(curBar, lenA)];
            const int kickPitch = diaRoot(curKeyRoot, 2);
            const int clapPitch = 42;
            const int hatOff = 44, hatRoll = 48;

            for (int b = 0; b < 4; ++b)
            {
                const double beatAbs = curBar * 4.0 + b;

                // KICK — 4-on-floor, or broken (beat 2 dropped); accent on beat 1
                if (active.count("kick") && kick.track >= 0)
                {
                    if (!kickBroken || b != 2)
                        kick.add(beatAbs, kickPitch, b == 0 ? 127 : 122, 1.9, kMaxNotesPerClip);
                }
                // HAT — mode-driven rhythm (RhythmVariant cycles the modes)
                if (active.count("hat") && hat.track >= 0)
                {
                    switch (hatMode)
                    {
                        case 1: // 16ths (kick's downbeats stay clean)
                            hat.add(beatAbs + 0.25, hatOff, 84 + hatAccent, 0.2, kMaxNotesPerClip);
                            hat.add(beatAbs + 0.5,  hatOff, 92 + hatAccent, 0.2, kMaxNotesPerClip);
                            hat.add(beatAbs + 0.75, hatOff, 84 + hatAccent, 0.2, kMaxNotesPerClip);
                            break;
                        case 2: // roll-bars: 16th roll into each bar, offbeats after
                            if (b == 0)
                            {
                                hat.add(beatAbs + 0.0,  hatRoll, 98 + hatAccent,  0.2, kMaxNotesPerClip);
                                hat.add(beatAbs + 0.25, hatRoll, 101 + hatAccent, 0.2, kMaxNotesPerClip);
                                hat.add(beatAbs + 0.5,  hatRoll, 104 + hatAccent, 0.2, kMaxNotesPerClip);
                                hat.add(beatAbs + 0.75, hatRoll, 107 + hatAccent, 0.2, kMaxNotesPerClip);
                            }
                            else
                                hat.add(beatAbs + 0.5, hatOff, 92 + hatAccent, 0.2, kMaxNotesPerClip);
                            break;
                        case 3: // ghost-note shuffle: offbeat + quiet ghost 16th
                            hat.add(beatAbs + 0.5, hatOff, 92 + hatAccent, 0.2, kMaxNotesPerClip);
                            if ((curBar + b) % 2 == 1)
                                hat.add(beatAbs + 0.75, hatOff, 60, 0.15, kMaxNotesPerClip);
                            break;
                        default: // offbeat 8ths (canonical) + seeded accent
                            hat.add(beatAbs + 0.5, hatOff,
                                    (b == 0 ? 98 : 92) + hatAccent, 0.2, kMaxNotesPerClip);
                            break;
                    }
                    // density-gated window-end flourish (kept from the legacy roll)
                    if (hatMode == 0 && wb == 1 && b == 3 && rng01() < density * 0.3)
                        hat.add(beatAbs + 0.75, hatRoll, 98, 0.2, kMaxNotesPerClip);
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
                // CLAP — 2/4 backbeat
                if (active.count("clap") && clap.track >= 0 && (b == 1 || b == 3))
                    clap.add(beatAbs, clapPitch, 100, 0.15, kMaxNotesPerClip);
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
        //    cadence guard (and the min-hold test) compare against. ──
        for (const auto& a : active) ages[a] += windowBars;

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
    for (RoleCtx* rc : { &kick, &bass, &hat, &arp, &stab, &pad, &clap, &riser, &down })
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
