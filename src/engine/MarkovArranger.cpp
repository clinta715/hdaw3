#include "engine/MarkovArranger.h"

#include "engine/HarmonyEngine.h"
#include "engine/MarkovRoles.h"
#include "engine/PercussionEngine.h"
#include "engine/TextureEngine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <vector>

namespace HDAW {

namespace {

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

} // namespace

PsytranceMarkovScore MarkovArranger::run(const PsytranceMarkovParams& paramsIn)
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

    // Style packs: the psytrance values are the structs' defaults — the seam
    // where future genre style packs (JSON) land.
    const PercussionStyle percStyle;
    const HarmonyStyle harmonyStyle;
    const TextureStyle textureStyle;

    PercussionEngine perc;
    HarmonyEngine harmony;
    harmony.setProgressions(p.progressionA, p.progressionB);

    // ── Rng: full 64-bit seed → mt19937 via seed_seq (fixed draw order below) ──
    std::seed_seq seedSeq { (uint32_t) (p.seed & 0xffffffffu), (uint32_t) (p.seed >> 32) };
    std::mt19937 rng(seedSeq);
    auto rng01 = [&rng]() { return markovRng01(rng); };
    auto rngInt = [&rng](int lo, int hi) { return markovRngInt(rng, lo, hi); }; // inclusive
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

    // ── Key-change direction: ONE seeded choice at generation start ──
    harmony.initKey(p.keyRoot, p.scaleMode, rng, p.keyShiftDegrees);

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
    perc.drawThemes(rng, percStyle);

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
            // Theme-hold eligibility (P1): see PercussionEngine::
            // themeRotationViable — the kick flag rides its own clock
            // (Breakbeat, or a rotation that swaps the flag). Weights stay 0
            // with no eligible target so the single draw is never biased by
            // dead slots.
            const bool themeEligible = perc.themeRotationViable(active);
            const bool kickEligible = active.count("kick") != 0
                                      && perc.kickClockValue() >= percStyle.percPatternHoldBars;

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
            harmony.keyChange();
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
            harmony.toggleSwapPattern();
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
            perc.toggleBreakbeat();
            targetRole = "kick";
        }
        else if (chosen == MarkovAction::FilterSweep)
        {
            std::vector<std::string> pitched;
            for (const char* r : kLeadFamily) if (active.count(r)) pitched.push_back(r);
            targetRole = oldestPick(pitched); // oldest pitched active role
            if (!targetRole.empty())
                score.automations.push_back(TextureEngine::filterSweepPoint(
                    targetRole, bar,
                    textureStyle.filterSweepValMin + rng01() * textureStyle.filterSweepValSpan,
                    textureStyle));
        }
        else if (chosen == MarkovAction::RhythmVariant)
        {
            // Rotate the WHOLE theme one step (hat/snare/rim grids + kick
            // flag move together) — only a theme that has held
            // percPatternHoldBars is eligible (weight gating above); this
            // re-check mirrors the other no-target paths.
            if (perc.themeRotationViable(active))
            {
                perc.rotate();
                targetRole = "theme";
            }
            else chosen = MarkovAction::Keep;
        }
        else if (chosen == MarkovAction::ArpVariant)
        {
            if (active.count("arp"))
            {
                targetRole = "arp";
                harmony.applyArpVariant(rng);
            }
            else chosen = MarkovAction::Keep;
        }
        else if (chosen == MarkovAction::NoteLengthVariant)
        {
            const std::vector<std::string> gateableRoles = harmony.gateableActive(active);
            if (!gateableRoles.empty())
            {
                targetRole = gateableRoles[rngInt(0, (int) gateableRoles.size() - 1)];
                harmony.applyNoteLengthVariant(rng, targetRole);
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
        const int kickPitch = diaRoot(harmony.currentKeyRoot(), 2);
        perc.writeWindowNotes(bar, windowBars, active, percStyle, kickPitch,
                              kick, hat, snare, rim, clap, kMaxNotesPerClip);
        harmony.writeWindowNotes(bar, windowBars, active, harmonyStyle,
                                 bass, arp, stab, pad, totalBars, kMaxNotesPerClip);

        // FX accents (FxHit windows + KeyChange transition FX) — pitched from
        // the CURRENT key.
        const std::string fxRole = (chosen == MarkovAction::FxHit) ? targetRole : fxPick;
        if (!fxRole.empty() && (chosen == MarkovAction::FxHit || chosen == MarkovAction::KeyChange))
            TextureEngine::writeFxAccents(fxRole, bar, harmony.currentKeyRoot(), p.scaleMode,
                                          riser, down, textureStyle, kMaxNotesPerClip);

        // ── Age update: +windowBars per active role (reset happens on re-add);
        //    the step below records POST-update ages = bars actually run by the
        //    time this window's audio ends — exactly what the next window's
        //    cadence guard (and the min-hold test) compare against. The theme
        //    clocks advance every window regardless of layer state; themeAge
        //    resets on rotation only, kickClock also on Breakbeat. ──
        for (const auto& a : active) ages[a] += windowBars;
        perc.advanceClocks(windowBars);

        // ── Record the step (post-action snapshot + post-update ages) ──
        MarkovStep step;
        step.barStart = bar;
        step.action = chosen;
        step.targetRole = targetRole;
        step.activeRoles.assign(active.begin(), active.end());
        for (const auto& a : step.activeRoles) step.ages.push_back(ages[a]);
        step.keyRoot = harmony.currentKeyRoot();
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
