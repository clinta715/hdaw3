#include "engine/PercussionEngine.h"
#include "engine/RhythmPatternGenerator.h"

#include <algorithm>
#include <cmath>

namespace HDAW {

namespace {

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
std::array<uint8_t, 16> snareFiller(const PercussionStyle& style)
{
    std::array<uint8_t, 16> g{}; // canonical 2/4 backbeat (layers with the clap)
    g[4]  = (uint8_t) style.fillerVelocity;
    g[12] = (uint8_t) style.fillerVelocity;
    return g;
}
std::array<uint8_t, 16> rimFiller(const PercussionStyle& style)
{
    std::array<uint8_t, 16> g{}; // sparse 16th pushes into beats 3 and 1
    g[7]  = (uint8_t) style.fillerVelocity;
    g[15] = (uint8_t) style.fillerVelocity;
    return g;
}

// A voice's effective grid: the theme's own cells when the theme HAS that
// voice, the canonical filler when it does not.
inline const std::array<uint8_t, 16>& gridOrFiller(
    const std::array<uint8_t, 16>& grid, const std::array<uint8_t, 16>& filler)
{
    for (uint8_t v : grid) if (v != 0) return grid;
    return filler;
}

} // namespace

void PercussionEngine::drawThemes(std::mt19937& rng, const PercussionStyle& style)
{
    auto rngInt = [&rng](int lo, int hi) { return markovRngInt(rng, lo, hi); };

    // Theme 0 is the canonical opener. Themes 1..T-1 (T seeded 2..3) derive
    // each voice's 16-step grid from the euclidean RhythmPatternGenerator —
    // PURE, so its PARAMS are seeded from the mt19937 in a fixed order
    // (pulseA hits, pulseB hits, rotations, velocities) and the returned
    // notes convert to step-velocity arrays (dedupe by step, louder wins).
    // Snare leans ghost/soft with an optional 2/4 backbeat accent (90 on
    // steps 4/12), rim stays sparse, hats run denser; kickBroken draws ~35%.
    const int themeCount = rngInt(style.themeCountMin, style.themeCountMax);
    themes.clear();
    themes.reserve((size_t) themeCount);
    themes.push_back(canonicalTheme());
    for (int t = 1; t < themeCount; ++t) themes.push_back(drawTheme(rng, style));
    themeIndex = 0;
    themeAge = 0;
    kickClock = 0;
    patternHoldBars = style.percPatternHoldBars;
}

PercTheme PercussionEngine::drawTheme(std::mt19937& rng, const PercussionStyle& style)
{
    auto rng01 = [&rng]() { return markovRng01(rng); };
    auto rngInt = [&rng](int lo, int hi) { return markovRngInt(rng, lo, hi); };
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
    PercTheme t;
    t.kickBroken = rng01() < style.kickBrokenProb;
    t.hat   = drawVoiceGrid(style.hatHitsLo, style.hatHitsHi,
                            style.hatVelALo, style.hatVelAHi, style.hatVelBLo, style.hatVelBHi);
    t.snare = drawVoiceGrid(style.snareHitsLo, style.snareHitsHi,
                            style.snareVelALo, style.snareVelAHi, style.snareVelBLo, style.snareVelBHi);
    if (rng01() < style.snareBackbeatProb)   // optional 2/4 backbeat
    {
        t.snare[4]  = (uint8_t) style.snareBackbeatVel;
        t.snare[12] = (uint8_t) style.snareBackbeatVel;
    }
    t.rim = drawVoiceGrid(style.rimHitsLo, style.rimHitsHi,
                          style.rimVelALo, style.rimVelAHi, style.rimVelBLo, style.rimVelBHi);
    return t;
}

bool PercussionEngine::themeRotationViable(const std::set<std::string>& active) const
{
    if (themeAge < patternHoldBars) return false;
    for (const char* r : { "hat", "snare", "rim", "kick" })
        if (active.count(r) != 0) return true;
    return false;
}

void PercussionEngine::rotate()
{
    themeIndex = (themeIndex + 1) % (int) themes.size();
    themeAge = 0;  // rotation is the ONLY themeAge reset
    kickClock = 0; // the incoming theme may carry a different kick flag
}

void PercussionEngine::toggleBreakbeat()
{
    // Toggle the CURRENT theme's kick flag — it persists per theme,
    // so rotating back to that theme later keeps the flipped feel.
    themes[(size_t) themeIndex].kickBroken =
        !themes[(size_t) themeIndex].kickBroken;
    kickClock = 0;            // the kick pattern flipped: kick clock restarts
}

void PercussionEngine::advanceClocks(int windowBars)
{
    themeAge += windowBars;
    kickClock += windowBars;
}

int PercussionEngine::kickClockValue() const { return kickClock; }

void PercussionEngine::writeWindowNotes(int bar, int windowBars,
                                        const std::set<std::string>& active,
                                        const PercussionStyle& style, int kickPitch,
                                        RoleCtx& kick, RoleCtx& hat, RoleCtx& snare,
                                        RoleCtx& rim, RoleCtx& clap, int maxNotes) const
{
    const PercTheme& theme = themes[(size_t) themeIndex];
    const std::array<uint8_t, 16> snareFill = snareFiller(style);
    const std::array<uint8_t, 16> rimFill = rimFiller(style);
    for (int wb = 0; wb < windowBars; ++wb)
    {
        const int curBar = bar + wb;

        // HAT/SNARE/RIM — the current theme's 16-step velocity grids
        // (step s lands at s*0.25 beats; all three rotate as a unit).
        // A theme may leave a voice silent (theme 0 has no snare/rim):
        // an ACTIVE voice then plays its canonical filler (see
        // snareFiller/rimFiller) so added layers are never silent.
        if (hat.track >= 0 || snare.track >= 0 || rim.track >= 0)
        {
            const auto& snareGrid = gridOrFiller(theme.snare, snareFill);
            const auto& rimGrid   = gridOrFiller(theme.rim,   rimFill);
            const double barBeat = curBar * 4.0;
            for (int st = 0; st < 16; ++st)
            {
                const double stepBeat = barBeat + st * 0.25;
                if (active.count("hat") && hat.track >= 0 && theme.hat[(size_t) st])
                    hat.add(stepBeat, style.hatPitch, theme.hat[(size_t) st],
                            0.2, maxNotes);
                if (active.count("snare") && snare.track >= 0 && snareGrid[(size_t) st])
                    snare.add(stepBeat, style.snarePitch, snareGrid[(size_t) st],
                              0.15, maxNotes);
                if (active.count("rim") && rim.track >= 0 && rimGrid[(size_t) st])
                    rim.add(stepBeat, style.rimPitch, rimGrid[(size_t) st],
                            0.15, maxNotes);
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
                    kick.add(beatAbs, kickPitch, b == 0 ? style.kickAccentVel : style.kickNormalVel,
                             1.9, maxNotes);
            }
            // CLAP — canonical 2/4 backbeat (theme-independent by design)
            if (active.count("clap") && clap.track >= 0 && (b == 1 || b == 3))
                clap.add(beatAbs, style.clapPitch, style.clapVelocity, 0.15, maxNotes);
        }
    }
}

} // namespace HDAW
