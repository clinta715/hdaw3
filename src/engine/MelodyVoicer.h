#pragma once
// MelodyVoicer — score-level voicing of a MelodyPatternBank phrase into a
// target key (Phase 2 of docs/plans/2026-09-08-corpus-melody-pipeline.md).
// Pure deterministic component — no engine/model/DSP dependency. Consumes an
// std::mt19937 only for the optional contour mutation, and ONLY when the
// caller opts in (mutation > 0), so a mutation-free call draws nothing.

#include "engine/MelodyPatternBank.h"

#include <random>
#include <vector>

namespace HDAW {

enum class MelodyTransposeMode
{
    Diatonic = 0,     // re-map degrees to target scale (in-scale by construction)
    RootRelative = 1, // pure semitone shift (preserves exact source intervals)
    ChordTone = 2     // nearest chord tone of the current chord (diatonic for now)
};

struct VoicedNote
{
    double startBeat;     // relative to phrase start (bar 0)
    int pitch;
    int velocity;
    double durationBeats;
};

// Voice a bank phrase into targetRootPc + targetScaleMode. startBeat is
// relative to the phrase start. maxBars caps the phrase to the first maxBars
// bars (for a 2-bar window, pass maxBars=2 so only the window's span plays).
// mutation (0..1) is the per-note probability of a diatonic +/-1 degree
// jitter; when mutation > 0 it consumes rng draws, else no draws.
inline std::vector<VoicedNote> voiceMelodyPhrase(
    const MelodicPhrase& ph, int targetRootPc, int targetScaleMode,
    MelodyTransposeMode mode, int velocity, int maxBars,
    double mutation, std::mt19937& rng)
{
    std::vector<VoicedNote> out;
    const int grid = ph.grid > 0 ? ph.grid : 16;
    const int spanBars = (maxBars > 0 && maxBars < ph.bars) ? maxBars : ph.bars;
    const int limitStep = spanBars * grid;
    const MelodyScale& srcScale = melodyScales()[ph.scaleMode];
    const MelodyNote* notes = melodyPhraseNotes(ph);
    for (int i = 0; i < ph.nNotes; ++i)
    {
        const MelodyNote& n = notes[i];
        if (n.step >= limitStep) continue; // multi-bar window cap
        int degree = n.degree;
        if (mutation > 0.0)
        {
            const double r = (double) rng() / (double) (rng.max() - rng.min() + 1.0);
            if (r < mutation)
            {
                const int d = (int) (rng() & 1u) ? 1 : -1; // +-1 degree
                degree += d;
                if (degree < 0) degree = 0; // wrap-safe within target scale
            }
        }
        int pitch = -1;
        switch (mode)
        {
            case MelodyTransposeMode::RootRelative:
            {
                // exact source intervals shifted by the nearest root delta
                const int srcPitch = 12 * n.octave + ph.rootPc + srcScale.intervals[n.degree];
                int delta = targetRootPc - ph.rootPc;
                while (delta > 6) delta -= 12;
                while (delta < -5) delta += 12;
                pitch = srcPitch + delta;
                break;
            }
            case MelodyTransposeMode::ChordTone: // nearest chord-tone snap (diatonic for now)
            case MelodyTransposeMode::Diatonic:
            default:
                // Re-voice the phrase's degree contour through the TARGET scale so
                // every note lands in the target key (in-scale by construction),
                // regardless of the phrase's source scale mode.
                pitch = melodyDegreeToPitchInScale(targetScaleMode, targetRootPc, degree, n.octave);
                break;
        }
        if (pitch < 0 || pitch > 127) continue;
        VoicedNote v;
        v.startBeat = n.step / (double) grid;
        v.pitch = pitch;
        v.velocity = velocity;
        v.durationBeats = n.durSteps / (double) grid;
        out.push_back(v);
    }
    return out;
}

} // namespace HDAW
