#include "engine/HarmonyEngine.h"
#include "engine/PhraseGenerator.h"

namespace HDAW {

namespace {

// Chord-tone degree tables (identical to the legacy PsytranceGenerator
// voicings, so a Markov arp/stab sounds like the preplanned one).
const int kChordTones[7][4] = {
    { 0, 2, 4, 5 }, { 1, 3, 5, 6 }, { 2, 4, 6, 1 }, { 3, 5, 0, 2 },
    { 4, 6, 1, 3 }, { 5, 0, 2, 4 }, { 6, 1, 3, 5 } };

const int kDefaultProgA[8] = { 0, 5, 4, 5, 0, 5, 4, 5 }; // i-VII-VI-VII
const int kDefaultProgB[8] = { 4, 5, 0, 0, 4, 5, 2, 2 }; // VI-VII-i-i

const double kGateSet[3] = { 0.5, 0.75, 1.0 }; // staccato .. full

} // namespace

void HarmonyEngine::setProgressions(std::vector<int> a, std::vector<int> b)
{
    progA = std::move(a);
    progB = std::move(b);
    if (progA.empty()) progA.assign(kDefaultProgA, kDefaultProgA + 8);
    if (progB.empty()) progB.assign(kDefaultProgB, kDefaultProgB + 8);
    if (progA.empty()) progA.assign(1, 0);
    if (progB.empty()) progB.assign(1, 0);
    lenA = (int) progA.size();
    lenB = (int) progB.size();
}

void HarmonyEngine::initKey(int keyRoot, int scaleMode, std::mt19937& rng, int keyShiftDegrees)
{
    scale = scaleMode;
    keyDir = keyShiftDegrees > 0 ? keyShiftDegrees : markovRngInt(rng, 1, 2);
    curKeyRoot = keyRoot;
}

void HarmonyEngine::keyChange()
{
    const int base = diaRoot(curKeyRoot, 5);
    const int shifted = PhraseGenerator::scaleDegreeToPitch(base, scale, keyDir, 0);
    if (shifted > 0) curKeyRoot = shifted % 12;
}

int HarmonyEngine::currentKeyRoot() const { return curKeyRoot; }

void HarmonyEngine::toggleSwapPattern() { swapFlag = !swapFlag; }

bool HarmonyEngine::swapActive() const { return swapFlag; }

void HarmonyEngine::applyArpVariant(std::mt19937& rng)
{
    auto rngInt = [&rng](int lo, int hi) { return markovRngInt(rng, lo, hi); };
    switch (rngInt(0, 2))
    {
        case 0: arpDir = rngInt(0, 3); break;
        case 1: arpRot += rngInt(1, 3); break;
        default: arpLiftWindow = true; break; // +12 for this window
    }
}

std::vector<std::string> HarmonyEngine::gateableActive(const std::set<std::string>& active) const
{
    std::vector<std::string> out;
    for (const char* r : { "bass", "arp", "stab" }) if (active.count(r)) out.push_back(r);
    return out;
}

void HarmonyEngine::applyNoteLengthVariant(std::mt19937& rng, const std::string& role)
{
    auto rngInt = [&rng](int lo, int hi) { return markovRngInt(rng, lo, hi); };
    auto gateIndex = [](double v) {
        return v < 0.625 ? 0 : (v < 0.875 ? 1 : 2);
    };
    const int cur = gateIndex(gateFor(role));
    int next = cur;
    while (next == cur) next = rngInt(0, 2);
    gate[role] = kGateSet[next];
}

double HarmonyEngine::gateFor(const std::string& role) const
{
    // operator[]-equivalent read (the original map default-inserted 0.0 for
    // unseeded roles — "pad" is deliberately unseeded, so its gate reads 0).
    auto it = gate.find(role);
    return it != gate.end() ? it->second : 0.0;
}

int HarmonyEngine::degreeForBar(int bar) const
{
    return swapFlag ? progB[(size_t) wrapDegree(bar, lenB)]
                    : progA[(size_t) wrapDegree(bar, lenA)];
}

int HarmonyEngine::degPitch(int keyRoot, int degree, int octave) const
{
    return PhraseGenerator::scaleDegreeToPitch(diaRoot(keyRoot, octave), scale, degree, 0);
}

void HarmonyEngine::writeWindowNotes(int bar, int windowBars,
                                     const std::set<std::string>& active,
                                     const HarmonyStyle& style, RoleCtx& bass, RoleCtx& arp,
                                     RoleCtx& stab, RoleCtx& pad, int totalBars, int maxNotes)
{
    for (int wb = 0; wb < windowBars; ++wb)
    {
        const int curBar = bar + wb;
        const int deg = degreeForBar(curBar);

        for (int b = 0; b < 4; ++b)
        {
            const double beatAbs = curBar * 4.0 + b;

            // BASS — offbeat 8th, gate-multiplied (NoteLengthVariant)
            if (active.count("bass") && bass.track >= 0)
            {
                const int oct = swapFlag ? 3 : 2; // SwapPattern lifts the octave
                bass.add(beatAbs + 0.5, degPitch(curKeyRoot, deg, oct), style.bassVelocity,
                         0.4 * gateFor("bass"), maxNotes);
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
                    arp.add(beatAbs + s * 0.25, pitch, style.arpVelocity,
                            0.2 * gateFor("arp"), maxNotes);
                }
            }
            // STAB — triad on beat 2, gate-multiplied
            if (active.count("stab") && stab.track >= 0 && b == 1)
            {
                const int chordDeg = wrapDegree(deg, 7);
                stab.add(beatAbs, degPitch(curKeyRoot, kChordTones[chordDeg][0], 3),
                         style.stabVelocity, 1.3 * gateFor("stab"), maxNotes);
                stab.add(beatAbs, degPitch(curKeyRoot, kChordTones[chordDeg][1], 3),
                         style.stabVelocity, 1.3 * gateFor("stab"), maxNotes);
                stab.add(beatAbs, degPitch(curKeyRoot, kChordTones[chordDeg][2], 3),
                         style.stabVelocity, 1.3 * gateFor("stab"), maxNotes);
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
                    pad.add(startBeat, root,    vel,      dur, maxNotes);
                    pad.add(startBeat, third,   vel - 4,  dur, maxNotes);
                    pad.add(startBeat, fifth,   vel - 8,  dur, maxNotes);
                    pad.add(startBeat, seventh, vel - 12, dur, maxNotes);
                };
                const double padGate = gateFor("pad"); // unseeded → 0 (always gated pulse)
                if (padGate < 1.0)
                {
                    const bool useSixteenth = totalBars <= 128;
                    const double step = useSixteenth ? 0.25 : 0.5;
                    const double dur = useSixteenth ? 0.10 * padGate : 0.18 * padGate;
                    for (double off = 0.0; off < 4.0; off += step)
                        addPadChord(beatAbs + off, off == 0.0 ? style.padAccentVel : style.padGhostVel, dur);
                }
                else
                    addPadChord(beatAbs, style.padVelocity, 4.0);
            }
        }
    }
    arpLiftWindow = false; // the +12 lift applies to ONE window (cleared post-emission)
}

} // namespace HDAW
