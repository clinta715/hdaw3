#pragma once
// MotifStitcher — motif-stitching melodic Markov (Phase 3 of
// docs/plans/2026-09-08-corpus-melody-pipeline.md).
//
// Learns a first-order Markov chain over 1-bar MOTIF SHAPES mined from the
// corpus MelodyPatternBank lead phrases, then STITCHES them into an evolving
// multi-bar melodic line. Distinct from the scale-driven MarkovMelody (which
// generates a degree state-machine from scratch): here the fragments are real
// corpus motifs and the transitions reflect how they actually flow.
//
// A motif SHAPE is keyed by its transposition-invariant degree-DELTA contour
// (so identical melodic shapes merge across keys/phrases, densifying the
// transition table). Transitions are the within-phrase (bar_i -> bar_{i+1})
// shape pairs, aggregated across all lead phrases.
//
// Pure, deterministic, header-only, key-agnostic: emits degree+octave (the
// contour + register); callers re-voice into their own scale (e.g. PhraseGenerator
// indexes buildScalePitches, or use scaleDegreeToPitch). No engine/DSP/model dep.

#include "engine/MelodyPatternBank.h"
#include "engine/Generative.h"

#include <map>
#include <string>
#include <vector>

namespace HDAW {

struct MotifLineNote
{
    double startBeat;  // beats, relative to line start
    int degree;        // scale degree (corpus scale space)
    int octave;        // absolute register
    int durSteps;      // duration in 16th steps
};

class MotifStitcher
{
public:
    // Stitch `bars` bars of an evolving motif-Markov melodic line from the
    // corpus lead phrases. Deterministic for a given seed. Returns the line as
    // key-relative degree+octave notes; callers re-voice into their key/scale.
    static std::vector<MotifLineNote> stitchMotifLine(int bars, uint64_t seed);

private:
    struct MotifNote
    {
        int stepInBar; // 0..grid-1 (16th step within the 1-bar cell)
        int degree;
        int octave;
        int durSteps;
    };
    struct Shape
    {
        std::string sig;
        std::vector<MotifNote> notes; // representative 1-bar motif
        int count = 0;                // number of bars with this shape
    };
    struct Transition
    {
        std::string to;
        int count = 0;
    };
    struct Model
    {
        std::vector<Shape> shapes;
        std::map<std::string, std::vector<Transition>> trans;
    };

    static const Model& model();
    static const Shape* pickShape(const Model& m, SplitMix64& rng,
                                  const std::vector<Transition>& opts);
    static std::string sigOf(const std::vector<MotifNote>& notes);
};

// ── Implementation ──

inline std::string MotifStitcher::sigOf(const std::vector<MotifNote>& notes)
{
    // transposition-invariant shape: note count + degree-delta contour
    std::string s = "n" + std::to_string(notes.size());
    int prev = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const int d = (i == 0) ? notes[i].degree : notes[i].degree - prev;
        s += ":" + std::to_string(d);
        prev = notes[i].degree;
    }
    return s;
}

inline const MotifStitcher::Model& MotifStitcher::model()
{
    static const Model mm = [] {
        Model m;
        // sig -> index into shapes
        std::map<std::string, int> idx;
        const int grid = 16;
        for (int i = 0; i < melodyPhraseCount(); ++i)
        {
            const MelodicPhrase& ph = melodyPhrases()[i];
            if (ph.role != std::string("lead")) continue;
            if (ph.bars < 2 || ph.grid <= 0) continue;
            const int g = ph.grid;
            const MelodyNote* notes = melodyPhraseNotes(ph);
            // segment into 1-bar motifs
            std::vector<std::vector<MotifNote>> bars;
            bars.resize(ph.bars);
            for (int n = 0; n < ph.nNotes; ++n)
            {
                const MelodyNote& mn = notes[n];
                const int bar = mn.step / g;
                if (bar < 0 || bar >= ph.bars) continue;
                bars[bar].push_back({ mn.step % g, mn.degree, mn.octave, mn.durSteps });
            }
            // ensure each bar's notes are sorted by step (already ascending in bank)
            std::vector<int> shapeIdx(ph.bars, -1);
            for (int b = 0; b < ph.bars; ++b)
            {
                if (bars[b].empty()) continue;
                const std::string sig = sigOf(bars[b]);
                auto it = idx.find(sig);
                if (it == idx.end())
                {
                    shapeIdx[b] = (int) m.shapes.size();
                    idx[sig] = shapeIdx[b];
                    m.shapes.push_back({ sig, bars[b], 1 });
                }
                else
                {
                    shapeIdx[b] = it->second;
                    m.shapes[it->second].count++;
                }
            }
            // transitions: bar -> bar+1
            for (int b = 0; b + 1 < ph.bars; ++b)
            {
                if (shapeIdx[b] < 0 || shapeIdx[b + 1] < 0) continue;
                const std::string& from = m.shapes[shapeIdx[b]].sig;
                const std::string& to = m.shapes[shapeIdx[b + 1]].sig;
                auto& v = m.trans[from];
                bool found = false;
                for (auto& t : v) if (t.to == to) { t.count++; found = true; break; }
                if (!found) v.push_back({ to, 1 });
            }
        }
        (void) grid;
        return m;
    }();
    return mm;
}

inline const MotifStitcher::Shape* MotifStitcher::pickShape(
    const Model& m, SplitMix64& rng, const std::vector<Transition>& opts)
{
    int total = 0;
    for (const auto& t : opts) total += t.count;
    if (total <= 0) return nullptr;
    double r = rng.nextFloat() * total;
    for (const auto& t : opts)
    {
        r -= t.count;
        if (r < 0)
            for (const auto& s : m.shapes) if (s.sig == t.to) return &s;
    }
    return nullptr;
}

inline std::vector<MotifLineNote> MotifStitcher::stitchMotifLine(int bars, uint64_t seed)
{
    std::vector<MotifLineNote> out;
    const Model& m = model();
    if (m.shapes.empty() || bars <= 0) return out;
    const int grid = 16;
    SplitMix64 rng(deriveSeed(seed, "motifstitch"));

    // seed: pick a start shape weighted by shape count
    int totalShapes = 0;
    for (const auto& s : m.shapes) totalShapes += s.count;
    const Shape* cur = nullptr;
    if (totalShapes > 0)
    {
        double r = rng.nextFloat() * totalShapes;
        for (const auto& s : m.shapes)
        {
            r -= s.count;
            if (r < 0) { cur = &s; break; }
        }
    }
    if (!cur) return out;

    for (int bar = 0; bar < bars; ++bar)
    {
        // emit the current shape's representative motif, key-agnostic.
        // 4 beats per bar; a 16th step = 4.0/grid beats within the bar.
        for (const auto& mn : cur->notes)
        {
            MotifLineNote n;
            n.startBeat = bar * 4.0 + mn.stepInBar * (4.0 / (double) grid);
            n.degree = mn.degree;
            n.octave = mn.octave;
            n.durSteps = mn.durSteps;
            out.push_back(n);
        }
        // transition to the next shape (fallback: any shape weighted by count)
        const auto it = m.trans.find(cur->sig);
        if (it != m.trans.end() && !it->second.empty())
            cur = pickShape(m, rng, it->second);
        if (!cur)
        {
            const Shape* next = nullptr;
            if (totalShapes > 0)
            {
                double r = rng.nextFloat() * totalShapes;
                for (const auto& s : m.shapes)
                {
                    r -= s.count;
                    if (r < 0) { next = &s; break; }
                }
            }
            cur = next;
        }
        if (!cur) break;
    }
    return out;
}

} // namespace HDAW
