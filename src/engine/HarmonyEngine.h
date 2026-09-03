#pragma once
// HarmonyEngine — key, progressions, chord tones and pitched note emission
// (bass/arp/stab/pad), extracted from PsytranceMarkovGenerator. Key
// discipline keeps every pitched note in-scale before AND after a change;
// the arp runs a 16th chord-tone pattern with direction/rotation/+12-lift
// variants; pads are thick gated chord beds. P2's future home (riff-centric
// harmony). Pure deterministic component — no engine/model dependency.
// Style lives in HarmonyStyle (the genre style-pack seam).

#include "engine/MarkovRoles.h"

#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace HDAW {

struct HarmonyStyle {
    int bassVelocity = 112, arpVelocity = 85, stabVelocity = 96;
    int padVelocity = 84, padAccentVel = 102, padGhostVel = 66;
    // Gate set stays { 0.5, 0.75, 1.0 } (staccato .. full, NoteLengthVariant
    // multipliers); bass octaves 2/3 (SwapPattern lifts the octave).
};

class HarmonyEngine {
public:
    // Defaults when empty (kDefaultProgA/B; empty→{0}).
    void setProgressions(std::vector<int> a, std::vector<int> b);

    // Key-change direction: ONE seeded choice at generation start — draws
    // 1..2 ONLY when keyShiftDegrees == 0 (an explicit shift consumes no
    // draw).
    void initKey(int keyRoot, int scaleMode, std::mt19937& rng, int keyShiftDegrees);
    void keyChange();        // shiftKey
    int currentKeyRoot() const;

    void toggleSwapPattern(); // swapFlag = !swapFlag (A/B progression + bass octave)
    bool swapActive() const;

    void applyArpVariant(std::mt19937& rng); // the rngInt(0,2) subchoice + draws

    // bass/arp/stab intersect active (fixed order, NoteLengthVariant pool).
    std::vector<std::string> gateableActive(const std::set<std::string>& active) const;

    // Gate map transition: leave the current index, seeded re-draw.
    void applyNoteLengthVariant(std::mt19937& rng, const std::string& role);
    double gateFor(const std::string& role) const;
    int degreeForBar(int bar) const;         // progA/progB by wrapDegree + swapFlag

    // Per-bar pitched emission (bass/arp/stab/pad). Non-const: the arp
    // random-walk advances per note, and the one-window arp octave lift is
    // consumed (+ cleared) here.
    void writeWindowNotes(int bar, int windowBars, const std::set<std::string>& active,
                          const HarmonyStyle& style, RoleCtx& bass, RoleCtx& arp,
                          RoleCtx& stab, RoleCtx& pad, int totalBars, int maxNotes);

private:
    int degPitch(int keyRoot, int degree, int octave) const;

    std::vector<int> progA, progB;
    int lenA = 1, lenB = 1;
    int keyDir = 1;              // KeyChange size in scale degrees
    int curKeyRoot = 0;
    int scale = 1;               // PhraseGenerator scale index
    int arpDir = 0;              // 0 up, 1 down, 2 updown, 3 random-walk
    int arpRot = 0;              // degree-sequence rotation
    bool arpLiftWindow = false;  // +12 for one window
    int arpWalk = 0;             // random-walk position (0..3), bounces
    std::map<std::string, double> gate; // NoteLengthVariant multipliers
    bool swapFlag = false;       // SwapPattern: A/B progression + bass octave
};

} // namespace HDAW
