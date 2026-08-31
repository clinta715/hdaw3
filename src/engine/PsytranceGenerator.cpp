#include "engine/PsytranceGenerator.h"
#include "engine/PhraseGenerator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>

namespace HDAW {

namespace {

// Scientific-octave root placement (matches PhraseGenerator::diatonicRoots):
// octave 2 → F2 (41) for keyRoot 5 (F), octave 3 → F3 (53), etc.
inline int diaRoot(int pc, int octave) { return 12 * (octave + 1) + pc; }

// v5-verified chord-tone DEGREE table: degree d → 4 in-scale 7th tones
// (degrees, wrapped). Used for the arp pattern and the pad/stab voicings.
const int kChordTones[7][4] = {
    { 0, 2, 4, 5 }, { 1, 3, 5, 6 }, { 2, 4, 6, 1 }, { 3, 5, 0, 2 },
    { 4, 6, 1, 3 }, { 5, 0, 2, 4 }, { 6, 1, 3, 5 } };

const int kDefaultProgA[8] = { 0, 5, 4, 5, 0, 5, 4, 5 }; // i VII VI VII (minor)
const int kDefaultProgB[8] = { 4, 5, 0, 0, 4, 5, 2, 2 }; // VI VII i i (minor)

inline int wrapDegree(int d, int len) { int r = d % len; return r < 0 ? r + len : r; }

struct RoleCtx {
    int track = -1;
    PsytranceClip clip;
    bool used = false;

    void add(double startBeat, int pitch, int velocity, double durationBeats)
    {
        if (pitch < 0 || pitch > 127)
            return; // key-discipline guard: never emit an out-of-range note
        if (velocity < 1) velocity = 1;
        clip.notes.push_back({ startBeat, pitch, velocity, durationBeats });
    }
};

} // namespace

PsytranceSectionKind PsytranceGenerator::kindFromName(const std::string& name)
{
    std::string n;
    n.reserve(name.size());
    for (char c : name)
    {
        char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == ' ' || lower == '-' || lower == '_')
            continue;                                  // tolerant: "main a", "mini-break"
        n.push_back(lower);
    }
    if (n == "intro")               return PsytranceSectionKind::Intro;
    if (n == "build" || n == "buildup") return PsytranceSectionKind::Build;
    if (n == "maina" || n == "main") return PsytranceSectionKind::MainA;
    if (n == "mini" || n == "minibreak") return PsytranceSectionKind::Mini;
    if (n == "mainb")               return PsytranceSectionKind::MainB;
    if (n == "breakdown" || n == "truebreakdown") return PsytranceSectionKind::Breakdown;
    if (n == "finale" || n == "outro") return PsytranceSectionKind::Finale;
    return PsytranceSectionKind::Other;                 // full stack, like MainA
}

PsytranceScore PsytranceGenerator::generate(const PsytranceParams& p)
{
    PsytranceScore score;

    if (p.sections.empty())
    {
        score.error = "no sections defined";
        return score;
    }
    if (p.keyRoot < 0 || p.keyRoot > 11)
    {
        score.error = "keyRoot must be in 0..11";
        return score;
    }

    double totalBeats = 0.0;
    for (const auto& s : p.sections)
        totalBeats = std::max(totalBeats, s.end);
    if (totalBeats < 1.0)
    {
        score.error = "sections span less than 1 beat";
        return score;
    }
    score.totalBeats = totalBeats;

    // Progression defaults (DarkForestV5 shapes).
    std::vector<int> progA = p.progressionA.empty() ? std::vector<int>(kDefaultProgA, kDefaultProgA + 8) : p.progressionA;
    std::vector<int> progB = p.progressionB.empty() ? std::vector<int>(kDefaultProgB, kDefaultProgB + 8) : p.progressionB;
    if (progA.empty()) progA.assign(1, 0);
    if (progB.empty()) progB.assign(1, 0);

    // Seeded RNG, consumed in a fixed order → determinism. 0 = fixed default.
    std::mt19937 rng(static_cast<uint32_t>(p.seed));
    auto rng01 = [&rng]() { return static_cast<double>(rng() - rng.min()) / (rng.max() - rng.min() + 1.0); };
    const double density = std::clamp(p.density, 0.0, 1.0);

    const int lenA = static_cast<int>(progA.size());
    const int lenB = static_cast<int>(progB.size());

    // Role → track wiring; clap defaults to the hat track.
    auto makeRole = [](const std::string& role, int track) {
        RoleCtx rc;
        rc.track = track;
        rc.clip.role = role;
        rc.clip.trackIndex = track;
        return rc;
    };
    RoleCtx kick  = makeRole("kick",  p.kick);
    RoleCtx bass  = makeRole("bass",  p.bass);
    RoleCtx hat   = makeRole("hat",   p.hat);
    RoleCtx arp   = makeRole("arp",   p.arp);
    RoleCtx stab  = makeRole("stab",  p.stab);
    RoleCtx pad   = makeRole("pad",   p.pad);
    RoleCtx riser = makeRole("riser", p.riser);
    RoleCtx down  = makeRole("down",  p.down);
    RoleCtx clap  = makeRole("clap",  p.clap);

    auto markSkipped = [&score](const std::string& role) {
        if (std::find(score.skipped.begin(), score.skipped.end(), role) == score.skipped.end())
            score.skipped.push_back(role);
    };

    // Register bases (scientific octaves, v5-verified registers).
    const int kickPitch = diaRoot(p.keyRoot, 2);      // F2 for F minor
    const int bassOctLow = 2, bassOctHigh = 3;
    const int arpBase = diaRoot(p.keyRoot, 3);        // F3
    const int stabBase = diaRoot(p.keyRoot, 3);
    const int padBase = diaRoot(p.keyRoot, 3);
    const int riserPitch = diaRoot(p.keyRoot, 3);
    const int downPitch = diaRoot(p.keyRoot, 2);
    const int clapPitch = 42;                          // unpitched
    const int hatQuarter = 46, hatOff = 44, hatRoll = 48;

    const int scale = p.scaleMode;
    const int kMaxNotesPerClip = 8192;                 // MidiClipProcessor ceiling

    // Helper: degree → pitch at a given scientific octave (key-disciplined).
    auto degPitch = [&](int degree, int octave) {
        return PhraseGenerator::scaleDegreeToPitch(diaRoot(p.keyRoot, octave), scale, degree, 0);
    };

    bool arpMapped = p.arp >= 0;
    bool padMapped = p.pad >= 0;

    for (size_t si = 0; si < p.sections.size(); ++si)
    {
        const auto& sec = p.sections[si];
        const PsytranceSectionKind kind = kindFromName(sec.name);
        const int s0 = static_cast<int>(std::ceil(sec.start));
        const int s1 = static_cast<int>(std::floor(sec.end));
        if (s1 <= s0) continue;

        const bool fullStack = (kind == PsytranceSectionKind::MainA ||
                                kind == PsytranceSectionKind::MainB ||
                                kind == PsytranceSectionKind::Finale ||
                                kind == PsytranceSectionKind::Other);
        const bool bassPresent = fullStack;
        const bool rollsEverywhere = (kind == PsytranceSectionKind::Finale);
        // v5-consistent: the B phrase (VI–VII–i–i) runs through mainB AND the
        // finale; A (i–VII–VI–VII) covers mainA and the builds/breakdowns.
        const bool useB = (kind == PsytranceSectionKind::MainB ||
                           kind == PsytranceSectionKind::Finale);
        const int* progFor = useB ? &progB[0] : &progA[0];
        const int lenFor = useB ? lenB : lenA;

        for (int b = s0; b < s1; ++b)
        {
            const int bar = b / 4;
            const int deg = progFor[wrapDegree(bar, lenFor)];

            // ── PADS (whole arrangement, every bar) ──
            if (padMapped && (b % 4) == 0)
            {
                // Root of the current progression degree + its diatonic third.
                if (static_cast<int>(pad.clip.notes.size()) < kMaxNotesPerClip)
                {
                    pad.add(b, degPitch(deg, 3), 64, 4.0);
                    pad.add(b + 0.5, degPitch(deg + 2, 3), 58, 3.5);
                    pad.used = true;
                }
            }

            // ── KICK (4-on-floor from first Build onward; silent in mini/breakdown) ──
            if (kick.track >= 0 &&
                (kind == PsytranceSectionKind::Build || fullStack) &&
                static_cast<int>(kick.clip.notes.size()) < kMaxNotesPerClip)
            {
                kick.add(b, kickPitch, 122, 1.9);
                kick.used = true;
            }

            // ── HATS ──
            if (hat.track >= 0 && static_cast<int>(hat.clip.notes.size()) < kMaxNotesPerClip)
            {
                if (kind == PsytranceSectionKind::Intro)
                {
                    if (b >= 16)          // sparse soft quarters from bar 4 (guide §4)
                        hat.add(b, hatQuarter, 90, 0.2);
                }
                else if (kind == PsytranceSectionKind::Build)
                {
                    hat.add(b, hatQuarter, 90, 0.2);  // quarters through the build
                }
                else if (fullStack)
                {
                    hat.add(b + 0.5, hatOff, 92, 0.2); // offbeat 8ths (the backbone)
                    // 16th rolls at 8-bar boundaries, throughout the finale, and
                    // at density-gated bars in the main sections (seeded).
                    bool rollBar = ((b / 4) % 8 == 4) || rollsEverywhere;
                    if (!rollBar && (kind == PsytranceSectionKind::MainA ||
                                     kind == PsytranceSectionKind::MainB))
                        rollBar = (rng01() < density);
                    if (rollBar && b + 1.25 < s1)
                    {
                        hat.add(b + 0.75, hatRoll, 98, 0.2);
                        hat.add(b + 1.00, hatRoll, 102, 0.2);
                        hat.add(b + 1.25, hatRoll, 106, 0.2);
                    }
                }
                hat.used = !hat.clip.notes.empty();
            }

            // ── CLAP on 2/4 (beats 2 and 4 of the bar) from the build on ──
            if (clap.track >= 0 && (kind == PsytranceSectionKind::Build || fullStack)
                && (b % 4 == 1 || b % 4 == 3)
                && static_cast<int>(clap.clip.notes.size()) < kMaxNotesPerClip)
            {
                clap.add(b, clapPitch, 100, 0.15);
                clap.used = true;
            }

            // ── BASS (offbeat 8ths, only in the full stack) ──
            if (bass.track >= 0 && bassPresent
                && static_cast<int>(bass.clip.notes.size()) < kMaxNotesPerClip)
            {
                const int oct = useB ? bassOctHigh : bassOctLow;
                bass.add(b + 0.5, degPitch(deg, oct), 112, 0.4);
                bass.used = true;
            }

            // ── ARP (16th chord-tone arps; the +12 glint on the last 16th) ──
            if (arpMapped && fullStack)
            {
                const int chordDeg = wrapDegree(deg, 7);
                for (int s = 0; s < 4; ++s)
                {
                    if (static_cast<int>(arp.clip.notes.size()) >= kMaxNotesPerClip) break;
                    const int tone = kChordTones[chordDeg][s];
                    const int glint = (s == 3) ? 12 : 0; // classic psy arp glint
                    arp.add(b + s * 0.25, degPitch(tone, 3) + glint, 85, 0.2);
                }
                arp.used = true;
            }

            // ── STABS (triad on beat 2 of each bar; extra triad on beat 4 in
            //    mainB/finale when density >= 0.7) ──
            if (stab.track >= 0 && fullStack
                && static_cast<int>(stab.clip.notes.size()) < kMaxNotesPerClip)
            {
                const int chordDeg = wrapDegree(deg, 7);
                if (b % 4 == 1)
                {
                    stab.add(b, degPitch(kChordTones[chordDeg][0], 3), 96, 1.3);
                    stab.add(b, degPitch(kChordTones[chordDeg][1], 3), 96, 1.3);
                    stab.add(b, degPitch(kChordTones[chordDeg][2], 3), 96, 1.3);
                    stab.used = true;
                }
                else if (b % 4 == 3 && useB && density >= 0.7)
                {
                    stab.add(b, degPitch(kChordTones[chordDeg][0], 3), 90, 1.0);
                    stab.add(b, degPitch(kChordTones[chordDeg][1], 3), 90, 1.0);
                    stab.add(b, degPitch(kChordTones[chordDeg][2], 3), 90, 1.0);
                }
            }
        }

        // ── BREAKDOWN MELODY (slow reverbed phrase, written into the arp clip) ──
        if (kind == PsytranceSectionKind::Breakdown && arpMapped)
        {
            const int phrase[4] = { 0, 2, 4, 5 };
            for (int k = 0; k < 4; ++k)
            {
                const double at = sec.start + k * 8.0;
                if (at + 3.0 > sec.end) break;
                if (static_cast<int>(arp.clip.notes.size()) >= kMaxNotesPerClip) break;
                arp.add(at, degPitch(phrase[k], 3), 95, 3.0);
                arp.used = true;
            }
        }
    }

    // ── RISERS + DOWNLIFTERS: 8 beats into every drop (MainA/MainB/Finale
    //    section starts); rising-velocity 8th riser, long tonal-reverse down. ──
    for (const auto& sec : p.sections)
    {
        const PsytranceSectionKind kind = kindFromName(sec.name);
        if (kind != PsytranceSectionKind::MainA &&
            kind != PsytranceSectionKind::MainB &&
            kind != PsytranceSectionKind::Finale)
            continue;
        const int drop = static_cast<int>(std::ceil(sec.start));
        if (drop < 8) continue;
        if (riser.track >= 0)
        {
            for (int i = 0; i < 8; ++i)
            {
                if (static_cast<int>(riser.clip.notes.size()) >= kMaxNotesPerClip) break;
                riser.add(drop - 8 + i * 0.5, riserPitch, 60 + 7 * i, 0.45); // 60→109
                riser.used = true;
            }
        }
        if (down.track >= 0)
        {
            down.add(drop - 8.0, downPitch, 95, 8.0); // spans into the drop
            down.used = true;
        }
    }

    // Assemble the score: mapped+used roles with notes; skip the rest.
    RoleCtx* roles[] = { &kick, &bass, &hat, &arp, &stab, &pad, &clap, &riser, &down };
    for (RoleCtx* rc : roles)
    {
        if (rc->track < 0)
            markSkipped(rc->clip.role);
        else if (!rc->clip.notes.empty())
        {
            std::sort(rc->clip.notes.begin(), rc->clip.notes.end(),
                      [](const PsytranceNote& a, const PsytranceNote& b) { return a.startBeat < b.startBeat; });
            score.notesTotal += static_cast<int>(rc->clip.notes.size());
            score.clips.push_back(std::move(rc->clip));
        }
        else
            markSkipped(rc->clip.role); // mapped but produced nothing
    }

    return score;
}

} // namespace HDAW