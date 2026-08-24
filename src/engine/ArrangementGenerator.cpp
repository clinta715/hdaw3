#include "engine/ArrangementGenerator.h"
#include "engine/PhraseGenerator.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace HDAW
{

const char* trackRoleName (TrackRole role)
{
    switch (role)
    {
        case TrackRole::Kick:      return "Kick";
        case TrackRole::ClosedHat: return "Closed Hat";
        case TrackRole::OpenHat:   return "Open Hat";
        case TrackRole::Clap:      return "Clap";
        case TrackRole::Snare:     return "Snare";
        case TrackRole::Bass:      return "Bass";
        case TrackRole::Lead:      return "Lead";
        case TrackRole::Chords:    return "Chords";
    }
    return "Track";
}

const char* sectionName (Section s)
{
    switch (s)
    {
        case Section::Intro:   return "Intro";
        case Section::BuildUp: return "Build-Up";
        case Section::MainA:   return "Main A";
        case Section::MainB:   return "Main B";
        case Section::Drop:    return "Drop";
        case Section::Break:   return "Break";
        case Section::Outro:   return "Outro";
    }
    return "Main";
}

namespace
{

constexpr int kStepsPerBar = 16;
constexpr double kBeatPerStep = 0.25;

constexpr int kKick = 36;
constexpr int kClosedHat = 42;
constexpr int kOpenHat = 46;
constexpr int kClap = 39;

double stepToBeat (int bar, int step) { return bar * 4.0 + step * kBeatPerStep; }

uint64_t resolveSeed (uint64_t seed)
{
    if (seed != 0)
        return seed;
    std::random_device dev;
    uint64_t s = (static_cast<uint64_t>(dev()) << 32) ^ static_cast<uint64_t>(dev());
    return s ? s : 0x9E3779B97F4A7C15ULL;
}

std::vector<int> scaleIntervals (int scaleMode)
{
    for (const auto& m : PhraseGenerator::getScaleModes())
        if (m.index == scaleMode)
            return m.intervals;
    return { 0, 2, 3, 5, 7, 8, 10 }; // aeolian fallback
}

std::vector<Section> sectionByBar (int bars, const std::vector<std::pair<int, Section>>& map)
{
    std::vector<Section> out (static_cast<size_t>(bars), Section::MainA);
    for (int b = 0; b < bars; ++b)
    {
        Section cur = Section::MainA;
        for (const auto& [start, sec] : map)
        {
            if (start <= b) cur = sec;
            else break;
        }
        out[static_cast<size_t>(b)] = cur;
    }
    return out;
}

double elementDensity (TrackRole role, int energy)
{
    double base;
    switch (role)
    {
        case TrackRole::Kick:      base = 0.85; break;
        case TrackRole::ClosedHat: base = 0.70; break;
        case TrackRole::OpenHat:   base = 0.40; break;
        case TrackRole::Clap:      base = 0.80; break;
        case TrackRole::Bass:      base = 0.80; break;
        case TrackRole::Lead:      base = 0.50; break;
        case TrackRole::Chords:    base = 0.30; break;
        default:                   base = 0.50; break;
    }
    const double factor = 0.5 + 0.5 * (energy / 3.0); // 0.5 .. 1.0
    return std::clamp (base * factor, 0.0, 1.0);
}

double swingOffsetBeats (int step, double swingPercent)
{
    if ((step & 1) == 0)
        return 0.0;
    const double amount = std::clamp ((swingPercent - 50.0) / 50.0, -1.0, 1.0);
    return amount * 0.5 * kBeatPerStep;
}

// ── Kick ──

int pickKickArchetype (SplitMix64& rng, Section sec, int style)
{
    double w[4]; // fotf, two_step, broken, syncopated
    switch (sec)
    {
        case Section::Intro:   w[0]=0.05; w[1]=0.35; w[2]=0.45; w[3]=0.15; break;
        case Section::MainA:
        case Section::MainB:   w[0]=0.50; w[1]=0.25; w[2]=0.15; w[3]=0.10; break;
        case Section::Break:   w[0]=0.10; w[1]=0.10; w[2]=0.40; w[3]=0.40; break;
        case Section::BuildUp: w[0]=0.20; w[1]=0.20; w[2]=0.30; w[3]=0.30; break;
        case Section::Drop:    w[0]=0.60; w[1]=0.20; w[2]=0.10; w[3]=0.10; break;
        default:               w[0]=0.35; w[1]=0.30; w[2]=0.20; w[3]=0.15; break;
    }
    if (style == 1)      { w[0] *= 1.7; w[1] *= 0.3; w[2] *= 0.3; w[3] *= 0.3; } // House: fotf dominant
    else if (style == 2) { w[0] *= 0.3; w[1] *= 1.7; w[2] *= 2.0; w[3] *= 0.8; } // DnB: two-step/broken
    double sum = w[0] + w[1] + w[2] + w[3];
    if (sum > 0.0)
        for (double& x : w) x /= sum;
    return weightedChoice (rng, { { 0, w[0] }, { 1, w[1] }, { 2, w[2] }, { 3, w[3] } });
}

std::vector<int> kickAnchors (int archetype, int energy)
{
    std::vector<int> a;
    switch (archetype)
    {
        case 1:  a = { 0, 8 };
                 if (energy >= 2) a.push_back (13);
                 if (energy >= 3) a.push_back (15);
                 break;
        case 2:  a = { 0, 11 };
                 if (energy >= 2) a.push_back (14);
                 break;
        case 3:  a = { 0, 10 };
                 if (energy >= 2) a.push_back (15);
                 break;
        default: a = { 0, 4, 8, 12 }; break;
    }
    std::sort (a.begin(), a.end());
    a.erase (std::unique (a.begin(), a.end()), a.end());
    return a;
}

std::vector<ArrangementNote> generateKick (const ArrangementParams& p, ReproState& repro,
                                           const std::vector<int>& energy,
                                           const std::vector<Section>& sections,
                                           std::vector<std::vector<int>>& onsetsByBar)
{
    std::vector<ArrangementNote> notes;
    onsetsByBar.assign (static_cast<size_t>(p.bars), {});
    for (int bar = 0; bar < p.bars; ++bar)
    {
        if (sections[static_cast<size_t>(bar)] == Section::Break)
            continue; // kick drops out during breaks
        auto rng = repro.rng ("kick", std::to_string (bar));
        const int e = energy[static_cast<size_t>(bar)];
        const int arch = pickKickArchetype (rng, sections[static_cast<size_t>(bar)], p.style);
        const auto anchors = kickAnchors (arch, e);
        const double density = elementDensity (TrackRole::Kick, e);
        const int drift = rng.nextInt (-2, 2);
        for (int step : anchors)
        {
            if (! rng.nextBool (density))
                continue;
            int vel = rng.nextInt (110, 120) + drift + (step == 0 ? 2 : 0);
            ArrangementNote n;
            n.startBeat = stepToBeat (bar, step);
            n.noteNumber = kKick;
            n.velocity = std::clamp (vel, 1, 127);
            n.durationBeats = 0.2;
            notes.push_back (n);
            onsetsByBar[static_cast<size_t>(bar)].push_back (step);
        }
    }
    return notes;
}

// ── Clap ──

std::vector<ArrangementNote> generateClap (const ArrangementParams& p, ReproState& repro,
                                           const std::vector<int>& energy,
                                           const std::vector<Section>& sections,
                                           std::vector<std::vector<int>>& onsetsByBar)
{
    std::vector<ArrangementNote> notes;
    onsetsByBar.assign (static_cast<size_t>(p.bars), {});
    for (int bar = 0; bar < p.bars; ++bar)
    {
        if (sections[static_cast<size_t>(bar)] == Section::Break)
            continue;
        auto rng = repro.rng ("clap", std::to_string (bar));
        const int e = energy[static_cast<size_t>(bar)];
        const double presence = std::clamp (0.5 + 0.15 * e, 0.0, 1.0);
        const double styleScale = (p.style == 2) ? 0.35 : 1.0; // DnB: snare owns the backbeat
        for (int step : { 4, 12 }) // beats 2 and 4
        {
            if (! rng.nextBool (presence * styleScale))
                continue;
            ArrangementNote n;
            n.startBeat = stepToBeat (bar, step);
            n.noteNumber = kClap;
            n.velocity = std::clamp (rng.nextInt (105, 120) + (step == 12 ? 2 : 0), 1, 127);
            n.durationBeats = 0.1;
            notes.push_back (n);
            onsetsByBar[static_cast<size_t>(bar)].push_back (step);
        }
        const double pEnd = 0.05 + (e / 3.0) * 0.07;
        if (rng.nextBool (pEnd * styleScale))
        {
            ArrangementNote n;
            n.startBeat = stepToBeat (bar, 15);
            n.noteNumber = kClap;
            n.velocity = rng.nextInt (95, 110);
            n.durationBeats = 0.08;
            notes.push_back (n);
            onsetsByBar[static_cast<size_t>(bar)].push_back (15);
        }
    }
    return notes;
}

// ── Snare ──

std::vector<ArrangementNote> generateSnare (const ArrangementParams& p, ReproState& repro,
                                            const std::vector<int>& energy,
                                            const std::vector<Section>& sections)
{
    std::vector<ArrangementNote> notes;
    constexpr int kSnare = 38; // D2
    for (int bar = 0; bar < p.bars; ++bar)
    {
        if (sections[static_cast<size_t>(bar)] == Section::Break)
            continue;
        auto rng = repro.rng ("snare", std::to_string (bar));
        const int e = energy[static_cast<size_t>(bar)];

        std::vector<int> steps;
        if (p.style == 2) // DnB two-step breakbeat
        {
            steps = { 4, 11 };
            if (e >= 2 && rng.nextBool (0.4))
                steps.push_back (14); // extra backbeat at high energy
            const int ghosts = rng.nextInt (1, 3);
            static const int candidates[] = { 2, 6, 10, 13 };
            for (int i = 0; i < ghosts; ++i)
                steps.push_back (candidates[rng.nextInt (0, 3)]);
        }
        else if (p.style == 1) // House: 2&4 backbeat + occasional ghost
        {
            steps = { 4, 12 };
            if (e >= 2 && rng.nextBool (0.25))
                steps.push_back (14);
        }
        else // Techno: 2&4, sparser ghost only at high energy
        {
            steps = { 4, 12 };
            if (e >= 3 && rng.nextBool (0.4))
                steps.push_back (11);
        }

        std::sort (steps.begin(), steps.end());
        steps.erase (std::unique (steps.begin(), steps.end()), steps.end());
        for (int step : steps)
        {
            const bool backbeat = (step == 4 || step == 11 || step == 12);
            const int vel = backbeat ? rng.nextInt (105, 118)
                                     : rng.nextInt (60, 85);
            ArrangementNote n;
            n.startBeat = stepToBeat (bar, step) + swingOffsetBeats (step, p.swingPercent);
            n.noteNumber = kSnare;
            n.velocity = std::clamp (vel, 1, 127);
            n.durationBeats = 0.1;
            notes.push_back (n);
        }
    }
    return notes;
}

// ── Hats ──

bool nearOnset (int step, const std::vector<int>& onsets, int window)
{
    for (int o : onsets)
        if (std::abs (step - o) <= window)
            return true;
    return false;
}

void generateHats (const ArrangementParams& p, ReproState& repro,
                   const std::vector<int>& energy, const std::vector<Section>& sections,
                   const std::vector<std::vector<int>>& clapOnsetsByBar,
                   std::vector<ArrangementNote>& closedOut, std::vector<ArrangementNote>& openOut)
{
    static const std::vector<int> empty;
    for (int bar = 0; bar < p.bars; ++bar)
    {
        if (sections[static_cast<size_t>(bar)] == Section::Break)
            continue; // hats out during breaks
        const int e = energy[static_cast<size_t>(bar)];
        const auto& claps = (static_cast<size_t>(bar) < clapOnsetsByBar.size())
                                ? clapOnsetsByBar[static_cast<size_t>(bar)] : empty;

        // Closed hat
        {
            auto rng = repro.rng ("closedhat", std::to_string (bar));
            double density = elementDensity (TrackRole::ClosedHat, e);
            if (p.style == 2) density *= 1.15; // DnB: busier 16ths
            const bool offbeatMode = density < 0.35 && e <= 1;
            std::vector<int> steps;
            if (offbeatMode)
            {
                steps = { 2, 6, 10, 14 };
            }
            else
            {
                const double base = 0.15 + 0.65 * density;
                for (int i = 0; i < kStepsPerBar; ++i)
                {
                    double pb;
                    if (i % 2 == 1)                    pb = std::min (1.0, base + 0.25 * density);
                    else if (i==2||i==6||i==10||i==14) pb = std::min (1.0, base + 0.15 * density);
                    else                               pb = std::max (0.05, base - 0.10);
                    pb *= (0.9 + 0.05 * e);
                    if (rng.nextBool (pb))
                        steps.push_back (i);
                }
            }
            for (int step : steps)
            {
                ArrangementNote n;
                n.startBeat = stepToBeat (bar, step) + swingOffsetBeats (step, p.swingPercent);
                n.noteNumber = kClosedHat;
                int vel = rng.nextInt (70, 95);
                if (step % 2 == 1) vel += rng.nextInt (10, 18);
                n.velocity = std::clamp (vel, 1, 127);
                n.durationBeats = 0.05;
                closedOut.push_back (n);
            }
        }

        // Open hat
        {
            auto rng = repro.rng ("openhat", std::to_string (bar));
            const double density = elementDensity (TrackRole::OpenHat, e);
            double pOff = std::clamp (0.2 + 0.6 * density, 0.0, 1.0) * (0.9 + 0.05 * e);
            for (int step : { 2, 6, 10, 14 })
            {
                if (! rng.nextBool (pOff))
                    continue;
                if (nearOnset (step, claps, 1) && rng.nextBool (0.7))
                    continue; // mutual exclusion with clap
                ArrangementNote n;
                n.startBeat = stepToBeat (bar, step) + kBeatPerStep * 0.1; // slight delay
                n.noteNumber = kOpenHat;
                n.velocity = std::clamp (rng.nextInt (80, 110), 1, 127);
                n.durationBeats = 0.2;
                openOut.push_back (n);
            }
            double pLead = std::clamp (0.15 + 0.4 * density, 0.0, 1.0);
            if (e < 2) pLead *= 0.5;
            if (rng.nextBool (pLead))
            {
                ArrangementNote n;
                n.startBeat = stepToBeat (bar, 15);
                n.noteNumber = kOpenHat;
                n.velocity = std::clamp (rng.nextInt (86, 116), 1, 127);
                n.durationBeats = 0.15;
                openOut.push_back (n);
            }
        }
    }
}

// ── Bass ──

std::vector<ArrangementNote> generateBass (const ArrangementParams& p, ReproState& repro,
                                           const std::vector<int>& energy,
                                           const std::vector<Section>& sections,
                                           const std::vector<std::vector<int>>& kickOnsetsByBar)
{
    std::vector<ArrangementNote> notes;
    static const std::vector<int> empty;
    const auto intervals = scaleIntervals (p.scaleMode);
    const int degreeCount = static_cast<int> (intervals.size());
    const int rootMidi = 36 + p.scaleRoot; // ~octave 2 bass register
    int currentDegree = 0;

    for (int bar = 0; bar < p.bars; ++bar)
    {
        if (sections[static_cast<size_t>(bar)] == Section::Break)
            continue;
        auto rng = repro.rng ("bass", std::to_string (bar));
        const int e = energy[static_cast<size_t>(bar)];
        const double density = elementDensity (TrackRole::Bass, e);
        if (density <= 0.05)
            continue;

        std::vector<int> idxs;
        if (density < 0.4 && e <= 1)
        {
            idxs = { 2, 6, 10, 14 };
            if (rng.nextBool (0.15 + 0.35 * p.complexity)) idxs.push_back (15);
        }
        else if (density <= 0.75 || e == 2)
        {
            idxs = { 0, 3, 6, 8, 11, 14 };
            if (rng.nextBool (0.45 * p.complexity)) idxs.back() = 15;
        }
        else
        {
            idxs = { 0, 2, 6, 10, 12, 14 };
            if ((bar % 2) == 1) idxs.back() = 15;
        }

        // Syncopated anticipations
        {
            std::vector<int> extra;
            for (int i : idxs)
                if (i > 0 && rng.nextBool (0.25 * p.complexity))
                    extra.push_back (i - 1);
            for (int x : extra) idxs.push_back (x);
            std::sort (idxs.begin(), idxs.end());
            idxs.erase (std::unique (idxs.begin(), idxs.end()), idxs.end());
        }

        // Downsample to a target note count
        int target = std::clamp (static_cast<int> (std::lround (2.0 + 6.0 * density)), 1, 8);
        while (static_cast<int> (idxs.size()) > target)
            idxs.erase (idxs.begin() + rng.nextInt (0, static_cast<int> (idxs.size()) - 1));

        const auto& kicks = (static_cast<size_t>(bar) < kickOnsetsByBar.size())
                                ? kickOnsetsByBar[static_cast<size_t>(bar)] : empty;

        for (size_t i = 0; i < idxs.size(); ++i)
        {
            const int step = idxs[i];
            if (std::find (kicks.begin(), kicks.end(), step) != kicks.end())
                continue; // duck the kick
            const int nextStep = (i + 1 < idxs.size()) ? idxs[i + 1] : kStepsPerBar;

            currentDegree = nextMarkovDegree (rng, currentDegree, degreeCount);
            int pitch = std::clamp (rootMidi + intervals[static_cast<size_t>(currentDegree)], 28, 64);

            int vel = rng.nextInt (75, 105);
            if (step % 2 == 1) vel += rng.nextInt (6, 12);

            const double gateLen = (nextStep - step) * kBeatPerStep;
            const double dur = gateLen * (0.35 + rng.nextFloat() * 0.20); // staccato 35-55%

            ArrangementNote n;
            n.startBeat = stepToBeat (bar, step);
            n.noteNumber = pitch;
            n.velocity = std::clamp (vel, 1, 127);
            n.durationBeats = (std::max) (0.05, dur);
            notes.push_back (n);
        }
    }
    return notes;
}

// ── Lead ──

std::vector<ArrangementNote> generateLead (const ArrangementParams& p, ReproState& repro,
                                           const std::vector<int>& energy,
                                           const std::vector<Section>& sections)
{
    std::vector<ArrangementNote> notes;
    const auto intervals = scaleIntervals (p.scaleMode);
    const int degCount = static_cast<int> (intervals.size());
    const int rootMidi = 60 + p.scaleRoot; // ~C4 lead register
    const int variantCycle = (p.complexity <= 0.5) ? 8 : 4;

    for (int bar = 0; bar < p.bars; ++bar)
    {
        if (sections[static_cast<size_t>(bar)] == Section::Break)
            continue; // lead rests during breaks
        auto rng = repro.rng ("lead", std::to_string (bar));
        const int e = energy[static_cast<size_t>(bar)];
        const double density = elementDensity (TrackRole::Lead, e);
        if (density <= 0.05)
            continue;

        std::vector<int> steps;
        std::vector<int> degs;
        if (density < 0.4)
        {
            // Stab hook: offbeat 8ths, root-favoured degrees
            steps = { 2, 6, 10, 14 };
            int target = std::clamp (static_cast<int> (std::lround (2 + 6 * density)), 2, 4);
            while (static_cast<int> (steps.size()) > target)
                steps.erase (steps.begin() + rng.nextInt (0, static_cast<int> (steps.size()) - 1));
            std::sort (steps.begin(), steps.end());
            for (size_t i = 0; i < steps.size(); ++i)
                degs.push_back (i == 0 ? 0 : weightedChoice (rng, { { 0, 0.45 }, { 4, 0.30 }, { 6, 0.15 }, { 2, 0.10 } }));
        }
        else if (density <= 0.75 || e == 2)
        {
            // Arp up: 1-5-b3 on an 1/8 grid
            steps = { 0, 2, 4, 6, 8, 10, 12, 14 };
            int target = std::clamp (static_cast<int> (std::lround (2 + 6 * density)), 2, 8);
            while (static_cast<int> (steps.size()) > target)
                steps.erase (steps.begin() + rng.nextInt (0, static_cast<int> (steps.size()) - 1));
            std::sort (steps.begin(), steps.end());
            const std::vector<int> arp = { 0, 4, (degCount > 2 ? 2 : 0) };
            for (size_t i = 0; i < steps.size(); ++i)
                degs.push_back (arp[i % arp.size()]);
        }
        else
        {
            // Up-down arp: 1-5-b3-5 on quarters
            steps = { 0, 4, 8, 12 };
            degs = { 0, 4, (degCount > 2 ? 2 : 0), 4 };
        }

        // End-variant: nudge the last degree to a neighbour on the cycle boundary
        if (variantCycle > 0 && ((bar + 1) % variantCycle == 0) && !degs.empty())
        {
            int& d = degs.back();
            if (d > 0 && (d >= degCount - 1 || rng.nextBool (0.5))) d -= 1;
            else if (d + 1 < degCount) d += 1;
        }

        for (size_t i = 0; i < steps.size(); ++i)
        {
            const int step = steps[i];
            const int deg = std::clamp (degs[i], 0, degCount - 1);
            int pitch = rootMidi + intervals[static_cast<size_t>(deg)];
            if (e >= 2 && rng.nextBool (0.08))
                pitch += 12;
            pitch = std::clamp (pitch, 48, 76);

            const int nextStep = (i + 1 < steps.size()) ? steps[i + 1] : kStepsPerBar;
            const double maxDur = (nextStep - step) * kBeatPerStep;
            const double dur = (std::min) (maxDur * 0.9, 0.4); // short stab

            ArrangementNote n;
            n.startBeat = stepToBeat (bar, step);
            n.noteNumber = pitch;
            n.velocity = std::clamp (rng.nextInt (85, 115) + (i == 0 ? rng.nextInt (6, 10) : 0), 1, 127);
            n.durationBeats = (std::max) (0.05, dur);
            notes.push_back (n);
        }
    }
    return notes;
}

// ── Chords ──

std::vector<ArrangementNote> generateChords (const ArrangementParams& p, ReproState& repro,
                                             const std::vector<int>& energy,
                                             const std::vector<Section>& sections)
{
    std::vector<ArrangementNote> notes;
    const auto intervals = scaleIntervals (p.scaleMode);
    const int degCount = static_cast<int> (intervals.size());
    const int rootMidi = 60 + p.scaleRoot;

    auto deg = [&] (int idx) { return intervals[static_cast<size_t>(std::clamp (idx, 0, degCount - 1))]; };
    auto voicingOffsets = [&] (int templ) -> std::vector<int> {
        switch (templ)
        {
            case 0:  return { deg (0), deg (4) };            // power dyad 1-5
            case 1:  return { deg (0), deg (2), deg (4) };   // triad 1-b3-5
            case 2:  return { deg (0), deg (1), deg (4) };   // sus2 1-2-5
            default: return { deg (0), deg (2), deg (6) };   // seventh 1-b3-b7
        }
    };

    for (int bar = 0; bar < p.bars; ++bar)
    {
        if (sections[static_cast<size_t>(bar)] == Section::Break)
            continue;
        auto rng = repro.rng ("chords", std::to_string (bar));
        const int e = energy[static_cast<size_t>(bar)];
        const double density = elementDensity (TrackRole::Chords, e);
        if (density <= 0.05)
            continue;

        // Stab positions: bar start + offbeat 8ths, gated by density
        std::vector<int> steps;
        if (rng.nextBool (std::clamp (0.5 + 0.4 * density, 0.0, 1.0)))
            steps.push_back (0);
        for (int s : { 2, 6, 10, 14 })
            if (rng.nextBool (std::clamp (0.2 + 0.6 * density, 0.0, 1.0)))
                steps.push_back (s);
        if (steps.empty() && rng.nextBool (0.25 + 0.5 * density))
            steps.push_back (0);

        // Voicing template by density/complexity
        int templ;
        if (density < 0.3)
            templ = weightedChoice (rng, { { 0, 0.55 }, { 1, 0.25 }, { 2, 0.15 }, { 3, 0.05 } });
        else if (density < 0.6)
            templ = weightedChoice (rng, { { 1, 0.45 }, { 2, 0.30 }, { 0, 0.15 }, { 3, 0.10 } });
        else
            templ = weightedChoice (rng, { { 3, 0.45 + 0.1 * p.complexity }, { 1, 0.30 }, { 2, 0.15 }, { 0, 0.10 } });

        std::vector<int> pitches;
        for (int o : voicingOffsets (templ))
            pitches.push_back (rootMidi + o);
        // Adjust register so the bottom sits ~48-64
        while (!pitches.empty() && *std::min_element (pitches.begin(), pitches.end()) < 48)
            for (int& x : pitches) x += 12;
        while (!pitches.empty() && *std::min_element (pitches.begin(), pitches.end()) > 64
               && std::all_of (pitches.begin(), pitches.end(), [] (int x) { return x - 12 >= 48; }))
            for (int& x : pitches) x -= 12;
        if (p.complexity >= 0.6 && rng.nextBool (0.35) && !pitches.empty())
            pitches.back() += 12; // spread voicing
        for (int& x : pitches) x = std::clamp (x, 48, 84);

        for (int step : steps)
        {
            const int vel = std::clamp (rng.nextInt (85, 110) + (step == 0 ? rng.nextInt (6, 10) : 0), 1, 127);
            for (int pitch : pitches)
            {
                ArrangementNote n;
                n.startBeat = stepToBeat (bar, step);
                n.noteNumber = pitch;
                n.velocity = vel;
                n.durationBeats = 0.3;
                notes.push_back (n);
            }
        }
    }
    return notes;
}

} // namespace

// ── Section planning / energy ──

std::vector<std::pair<int, Section>> buildSectionMap (int bars)
{
    bars = (std::max) (0, bars);
    std::vector<std::pair<int, Section>> out;
    if (bars <= 0)
        return out;

    std::vector<std::pair<Section, int>> plan;
    if (bars >= 128)
        plan = { { Section::Intro, 16 }, { Section::BuildUp, 16 }, { Section::MainA, 32 },
                 { Section::BuildUp, 16 }, { Section::Drop, 16 }, { Section::MainB, 32 },
                 { Section::Break, 16 }, { Section::MainB, 16 } };
    else if (bars >= 64)
        plan = { { Section::Intro, 8 }, { Section::BuildUp, 8 }, { Section::MainA, 16 },
                 { Section::BuildUp, 8 }, { Section::Drop, 8 }, { Section::MainB, 16 } };
    else if (bars >= 32)
        plan = { { Section::Intro, 8 }, { Section::BuildUp, 8 }, { Section::MainA, 8 }, { Section::Drop, 8 } };
    else if (bars >= 16)
        plan = { { Section::Intro, 4 }, { Section::BuildUp, 4 }, { Section::MainA, 4 }, { Section::Drop, 4 } };
    else
        plan = { { Section::Intro, (std::max) (1, bars / 4) },
                 { Section::MainA, bars - (std::max) (1, bars / 4) } };

    int cursor = 0;
    for (auto& [sec, len] : plan)
    {
        if (len <= 0) continue;
        if (cursor + len > bars) len = bars - cursor;
        if (len <= 0) continue;
        out.push_back ({ cursor, sec });
        cursor += len;
        if (cursor >= bars) break;
    }
    if (out.empty())
        out.push_back ({ 0, Section::MainA });
    return out;
}

std::vector<int> energyCurveFromSections (int bars, const std::vector<std::pair<int, Section>>& map)
{
    bars = (std::max) (0, bars);
    std::vector<int> out (static_cast<size_t>(bars), 2);

    struct Span { int s, e; Section sec; };
    std::vector<Span> spans;
    for (size_t i = 0; i < map.size(); ++i)
    {
        const int start = map[i].first;
        int end = (i + 1 < map.size()) ? map[i + 1].first : bars;
        end = std::clamp (end, start, bars);
        spans.push_back ({ start, end, map[i].second });
    }

    auto applyConst = [&] (int s, int e, int v) {
        for (int i = s; i < e && i < bars; ++i)
            out[static_cast<size_t>(i)] = std::clamp (v, 0, 3);
    };
    auto applyRamp = [&] (int s, int e, double v0, double v1) {
        const int len = (std::max) (0, e - s);
        if (len <= 0) return;
        if (len == 1) { if (s < bars) out[static_cast<size_t>(s)] = std::clamp ((int) std::lround (v1), 0, 3); return; }
        for (int k = 0; k < len && (s + k) < bars; ++k)
        {
            const double t = static_cast<double> (k) / (len - 1);
            out[static_cast<size_t>(s + k)] = std::clamp ((int) std::lround (v0 + (v1 - v0) * t), 0, 3);
        }
    };

    for (const auto& sp : spans)
    {
        switch (sp.sec)
        {
            case Section::Intro:   applyRamp (sp.s, sp.e, 0.0, 1.0); break;
            case Section::BuildUp: applyRamp (sp.s, sp.e, 1.0, 3.0); break;
            case Section::MainA:   applyConst (sp.s, sp.e, 2); break;
            case Section::MainB:   applyConst (sp.s, sp.e, 3); break;
            case Section::Drop:    applyRamp (sp.s, sp.e, 3.0, 1.0); break;
            case Section::Break:
                if (sp.e - sp.s >= 2)
                {
                    const int mid = sp.s + (sp.e - sp.s) / 2;
                    applyRamp (sp.s, mid, 2.0, 0.0);
                    applyRamp (mid, sp.e, 0.0, 2.0);
                }
                else applyConst (sp.s, sp.e, 1);
                break;
            case Section::Outro:   applyRamp (sp.s, sp.e, 1.0, 0.0); break;
        }
    }
    return out;
}

// ── Top-level ──

Arrangement generateArrangement (const ArrangementParams& params)
{
    Arrangement arr;
    const int bars = (std::max) (0, params.bars);
    const uint64_t seed = resolveSeed (params.seed);
    arr.resolvedSeed = seed;
    ReproState repro (seed);

    arr.sectionMap = buildSectionMap (bars);
    arr.energyByBar = energyCurveFromSections (bars, arr.sectionMap);
    const auto sections = sectionByBar (bars, arr.sectionMap);

    std::vector<std::vector<int>> kickOnsets, clapOnsets;
    std::vector<ArrangementNote> kickNotes, clapNotes, snareNotes, closedNotes, openNotes, bassNotes, leadNotes, chordsNotes;

    if (params.enableKick)
        kickNotes = generateKick (params, repro, arr.energyByBar, sections, kickOnsets);
    if (params.enableClap)
        clapNotes = generateClap (params, repro, arr.energyByBar, sections, clapOnsets);
    if (params.enableSnare)
        snareNotes = generateSnare (params, repro, arr.energyByBar, sections);
    if (params.enableClosedHat || params.enableOpenHat)
        generateHats (params, repro, arr.energyByBar, sections, clapOnsets, closedNotes, openNotes);
    if (params.enableBass)
        bassNotes = generateBass (params, repro, arr.energyByBar, sections, kickOnsets);
    if (params.enableLead)
        leadNotes = generateLead (params, repro, arr.energyByBar, sections);
    if (params.enableChords)
        chordsNotes = generateChords (params, repro, arr.energyByBar, sections);

    auto addPart = [&] (TrackRole role, std::vector<ArrangementNote> notes) {
        if (notes.empty())
            return;
        std::sort (notes.begin(), notes.end(),
                   [] (const ArrangementNote& a, const ArrangementNote& b) { return a.startBeat < b.startBeat; });
        if (params.velocityMin > 0 && params.velocityMax > 0)
        {
            int genMin = 127, genMax = 1;
            for (const auto& n : notes)
            {
                genMin = (std::min) (genMin, n.velocity);
                genMax = (std::max) (genMax, n.velocity);
            }
            if (genMax > genMin)
            {
                for (auto& n : notes)
                    n.velocity = std::clamp (
                        params.velocityMin + (n.velocity - genMin) * (params.velocityMax - params.velocityMin) / (genMax - genMin),
                        params.velocityMin, params.velocityMax);
            }
        }
        ArrangementPart part;
        part.role = role;
        part.name = trackRoleName (role);
        part.trackType = 0;
        part.notes = std::move (notes);
        arr.parts.push_back (std::move (part));
    };

    if (params.enableKick)      addPart (TrackRole::Kick, std::move (kickNotes));
    if (params.enableClosedHat) addPart (TrackRole::ClosedHat, std::move (closedNotes));
    if (params.enableOpenHat)   addPart (TrackRole::OpenHat, std::move (openNotes));
    if (params.enableClap)      addPart (TrackRole::Clap, std::move (clapNotes));
    if (params.enableSnare)     addPart (TrackRole::Snare, std::move (snareNotes));
    if (params.enableBass)      addPart (TrackRole::Bass, std::move (bassNotes));
    if (params.enableLead)      addPart (TrackRole::Lead, std::move (leadNotes));
    if (params.enableChords)    addPart (TrackRole::Chords, std::move (chordsNotes));

    return arr;
}

} // namespace HDAW
