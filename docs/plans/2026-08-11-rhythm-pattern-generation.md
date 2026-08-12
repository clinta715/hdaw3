# Rhythm Pattern Generation (Polyrhythm + Euclidean DSL + Snare/Genre) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add polyrhythmic drum-pattern generation, wire the dormant Euclidean rhythm DSL into production, and implement the vestigial Snare part with genre (House/Techno/DnB) flavoring — across engine, RPC, MCP, and UI.

**Architecture:** A new deterministic `RhythmPatternGenerator` (static utility, pure function of its params — no RNG, no seed) builds drum patterns from two independent euclidean pulse trains (the classic polyrhythm engine, e.g. 4-over-3) plus an optional rhythm-DSL voice that finally calls the existing-but-unused `Generative::expandToDivision`. In parallel, `ArrangementGenerator` gains a real `generateSnare` (house 2&4 backbeat, dnb two-step breakbeat, techno sparse) and genre-aware style weights (style 0=Techno unchanged so existing output is bit-identical, 1=House, 2=DnB). All three surfaces are wired: RPC `composition.generateRhythmPattern`, MCP `generate_rhythm_pattern` (+ `enableSnare` on `generate_arrangement`), and a new "Rhythm" mode (mode 4) in `PhraseGeneratorDialog` plus a Snare toggle + genre selector on the Arrangement mode.

**Tech Stack:** C++17 (JUCE 8), Qt JSON for RPC, React 19 + TypeScript + Zustand, gtest, Vitest.

---

## Success Gates (completion contract — evidence required)

- [ ] G1: `build/Debug/hdaw_tests.exe --gtest_filter=Rhythm*` passes (new `rhythm_pattern_generator_test.cpp` suite).
- [ ] G2: `build/Debug/hdaw_tests.exe --gtest_filter='ArrangementSnare*:ArrangementGenre*:ArrangementContent*:ArrangementEnable*:ArrangementDeterminism*'` passes (new snare/genre tests + no regression in existing arrangement suite).
- [ ] G3: `build/Debug/hdaw_tests.exe --gtest_filter=RhythmGenerationRpc*` passes (new RPC tests).
- [ ] G4: `build/Debug/hdaw_tests.exe --gtest_filter=McpServerIntegration*` (or the full integration suite) passes including the new `generate_rhythm_pattern` tools/call case; `generate_arrangement` dry-run with `enableSnare` works.
- [ ] G5: `cd frontend && npm test` passes including new `PhraseGeneratorDialog.test.tsx`; `npm run build` succeeds.
- [ ] G6: `cmake --build build --config Debug` succeeds (whole solution, no stale-binary trap: confirm `build/Debug/hdaw_tests.exe` timestamp is newer than the build).
- [ ] G7: Full engine test suite (`build/Debug/hdaw_tests.exe`, no filter) passes — no regression anywhere.
- [ ] G8: MCP parity — `generate_rhythm_pattern` registered (visible in `list_tools`), `enableSnare` accepted by `generate_arrangement`; the feature is reachable from every surface a human can use.
- [ ] G9: No new raw hex in frontend CSS (Gate 8 scan) — new UI reuses `pgd-*` classes only.
- [ ] G10: Knowledge graph refreshed (`codebase-memory index_repository` mode `fast`, project `D-pdf-roo-projects-hdaw3`) so the graph knows `RhythmPatternGenerator` and the new RPC method.

## Dependency Map

- **Blast radius:** `PhraseGenerator` cluster (composition RPC router, MCP composition tools, `PhraseGeneratorDialog`) + `ArrangementGenerator` (called by `AudioEngineCommands::generateArrangement` → RPC router → MCP tool). No audio-thread code is touched anywhere in this plan; all generation runs on the command/message thread. Graph query confirmed: `generateArrangement` (ArrangementGenerator.cpp:676 → AudioEngineCommands_Clips.cpp:728 → Router_Composition.cpp:188 → McpTools_Project.cpp:948) and `generatePhrase` (PhraseGenerator.cpp:257 → Router_Composition.cpp:87 → McpTools_Project.cpp:832) are the only production consumers of the two generators.
- **Upstream callers:** `frontend::dispatch` (FrontendRouter.cpp:81) → `dispatchComposition`; MCP server → `McpTools_Project.cpp` composition tool registry.
- **Downstream consumers:** `ProjectCommands::addMidiClip`/`addNote` (ValueTree writes → incremental clip deltas, no fullSync); `ReadModel`; frontend `read.snapshot` + delta apply.
- **God nodes in scope:** none (both generators are leaf utilities; the router/MCP registrations are additive).
- **Community boundaries crossed:** engine (generators) → frontend RPC (`Router_Composition`) → MCP (`McpTools_Project`) → React (`PhraseGeneratorDialog`). The interface contract at each boundary is the params/result JSON shape defined in Task 3/4 — keep field names identical across layers.
- **Projections affected:** ReadModel + frontend snapshot only (new clips/notes — deltas, not fullSync). Audio graph rebuilds once per generate call via the existing `generateIntoClip`/`generateArrangement` paths.
- **SPSC paths touched:** none.
- **Path integrity:** `composition.generateRhythmPattern` must be a complete chain: router parses params → `RhythmPatternGenerator::generate` → `addMidiClip` + `addNote` → clip delta → frontend. G3/G4 cover router+MCP; G1 covers the generator.

## Pitfall Gates Triggered

- **Gate 2 (unimplemented path):** The DSL (`expandToDivision`) is today a dead utility — the plan wires it end-to-end (engine test, RPC test, MCP integration test). `expandToDivision` **throws `std::invalid_argument`** on malformed input — the router and MCP handlers MUST catch it and return an error (G3 includes a malformed-DSL error test). No link in the chain may be a no-op.
- **Gate 3 (audio-thread safety):** No changes to `processBlock`, `Track`, `RoutingManager`, or any DSP. All generation is command-thread. State this explicitly in the PR/review.
- **Gate 4 (stale binaries):** New `.cpp` files must be added to the **explicit source lists**: `CMakeLists.txt` (after `src/engine/ArrangementGenerator.cpp`, line ~103) and `tests/CMakeLists.txt` (after `unit/engine/arrangement_generator_test.cpp`, line ~53). After C++ changes verify the test binary was rebuilt (timestamp check).
- **Gate 5 (frontend stale closures):** The dialog's `handleGenerate` reads React state directly (no post-`await` closure reads from props); the Rhythm branch makes exactly **one** `rpc.call` per Generate click (batch-RPC rule). No window listeners added.
- **Gate 8 (CSS tokens):** New UI reuses existing `pgd-*` classes; zero new CSS rules, zero new hex.
- **Gate 9 (IDs/validation):** No new ID allocators — clips/notes go through `ProjectCommands::addMidiClip`/`addNote`. All numeric params clamped inside `RhythmPatternGenerator` (pulse hits ≤ loop, rotation normalized, pitch/velocity clamped at RPC/MCP via schema min/max).
- **Undo:** MCP path already wraps clip creation in the UndoManager (`generateIntoClip` at McpTools_Project.cpp:813-830 passes `&um`). The RPC path follows the existing `generatePhrase` convention (no explicit transaction) — consistent with sibling methods; not a regression.

## Anti-Pattern Scan

- No N-call RPC loops (single call per action). No full-tree walks (new clips via existing commands). No `DBG` (no logging added). No raw hex. No new `.cpp` outside the two CMake source lists.

---

### Task 1: Engine — `RhythmPatternGenerator` (TDD)

**Files:**
- Create: `src/engine/RhythmPatternGenerator.h`
- Create: `src/engine/RhythmPatternGenerator.cpp`
- Test: `tests/unit/engine/rhythm_pattern_generator_test.cpp`
- Modify: `CMakeLists.txt` (engine source list), `tests/CMakeLists.txt` (test source list)

**Design contract (memorize — all tests below depend on it):**
- `loop = grid * bars` steps; a note at step `s` lands on beat `s * (4.0 / grid)`.
- Pulse A = `euclideanSteps(kA, loop, rotationA)` (priority 1), Pulse B = `euclideanSteps(kB, loop, rotationB)` (priority 2), DSL voice = `expandToDivision(dsl, loop)` (priority 3). A `pulseX <= 0` disables that pulse. Simultaneous hits on one step resolve by priority (A > B > DSL).
- `euclideanSteps` reference values (verified from `Generative.cpp:88-117` and `generative_test.cpp`): `euclideanSteps(4,16)` = {3,7,11,15}; `(4,16,rot=1)` = {0,4,8,12}; `(3,16)` = {5,10,15}; `(3,16,rot=1)` = {0,6,11}; `(5,16)` = {3,6,9,12,15}; `(4,32,rot=1)` = {0,8,16,24}. Rotation adds `+rot mod n` to every hit.
- `expandToDivision("E(3,8)", 16)` = {4,10,14} (verified generative_test.cpp:158).
- Durations: A=0.2 beats, B=0.1, DSL=0.1. Output sorted ascending by `startBeat`.

- [ ] **Step 1: Write the failing test file** `tests/unit/engine/rhythm_pattern_generator_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/RhythmPatternGenerator.h"

using RhythmPatternGenerator::Note;

TEST(RhythmPolyrhythm, DefaultFourOverThree)
{
    RhythmPatternGenerator::Params p; // grid=16 bars=1 pulseA=4 pulseB=3 rotA=1 rotB=1
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 6u);
    // A: {0,4,8,12} -> beats 0,1,2,3 (pitch 36, vel 112, dur 0.2)
    // B: {0,6,11}   -> step 0 collides with A -> beats 1.5, 2.75 (pitch 42, vel 96, dur 0.1)
    const double beats[]   = { 0.0, 1.0, 1.5, 2.0, 2.75, 3.0 };
    const int    pitches[] = { 36,  36,  42,  36,  42,   36  };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, pitches[i]);
    }
    EXPECT_EQ(notes[0].velocity, 112);
    EXPECT_EQ(notes[2].velocity, 96);
    EXPECT_DOUBLE_EQ(notes[0].durationBeats, 0.2);
    EXPECT_DOUBLE_EQ(notes[2].durationBeats, 0.1);
}

TEST(RhythmPolyrhythm, ZeroRotationPullsHitsBeforeTheBeat)
{
    RhythmPatternGenerator::Params p;
    p.rotationA = 0;
    p.rotationB = 0;
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 6u);
    // A: {3,7,11,15} -> 0.75,1.75,2.75,3.75 ; B: {5,10,15} -> 15 collides -> 1.25,2.5
    const double beats[]   = { 0.75, 1.25, 1.75, 2.5, 2.75, 3.75 };
    const int    pitches[] = { 36,   42,   36,   42,  36,   36   };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, pitches[i]);
    }
}

TEST(RhythmPolyrhythm, LoopSpansBars)
{
    RhythmPatternGenerator::Params p;
    p.bars = 2;      // loop = 32 steps
    p.pulseA = 4;    // 4 hits over 32 steps
    p.pulseB = 0;
    p.rotationA = 1; // {7,15,23,31} + 1 -> {0,8,16,24}
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 4u);
    const double beats[] = { 0.0, 2.0, 4.0, 6.0 };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, 36);
    }
}

TEST(RhythmDsl, ExpandsEuclideanToGrid)
{
    RhythmPatternGenerator::Params p;
    p.dsl = "E(3,8)"; // expands to steps {4,10,14} on the 16 grid
    p.pulseA = 0;
    p.pulseB = 0;
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 3u);
    const double beats[] = { 1.0, 2.5, 3.5 };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, 39);
        EXPECT_EQ(notes[i].velocity, 104);
        EXPECT_DOUBLE_EQ(notes[i].durationBeats, 0.1);
    }
}

TEST(RhythmDsl, CombinedPulsesAndDslDedup)
{
    RhythmPatternGenerator::Params p; // defaults: A 4/16 rot1, B 3/16 rot1
    p.dsl = "E(5,16)"; // steps {3,6,9,12,15}; 6 claimed by B, 12 by A -> {3,9,15}
    auto notes = RhythmPatternGenerator::generate(p);
    ASSERT_EQ(notes.size(), 9u);
    const double beats[]   = { 0.0, 0.75, 1.0, 1.5, 2.0, 2.25, 2.75, 3.0, 3.75 };
    const int    pitches[] = { 36,  39,   36,  42,  36,  39,   42,   36,  39   };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(notes[i].startBeat, beats[i]);
        EXPECT_EQ(notes[i].pitch, pitches[i]);
    }
}

TEST(RhythmValidation, EmptyOnBadGrid)
{
    RhythmPatternGenerator::Params p;
    p.grid = 0;
    EXPECT_TRUE(RhythmPatternGenerator::generate(p).empty());
    RhythmPatternGenerator::Params q;
    q.bars = 0;
    EXPECT_TRUE(RhythmPatternGenerator::generate(q).empty());
}

TEST(RhythmValidation, MalformedDslThrows)
{
    RhythmPatternGenerator::Params p;
    p.dsl = "E(3,8"; // unterminated E
    EXPECT_THROW(RhythmPatternGenerator::generate(p), std::invalid_argument);
    p.dsl = "z";     // bad token
    EXPECT_THROW(RhythmPatternGenerator::generate(p), std::invalid_argument);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --config Debug --target hdaw_tests`
Expected: FAIL — `RhythmPatternGenerator.h` not found (header does not exist yet). (If the `hdaw_tests` target name differs, run the full `cmake --build build --config Debug` and confirm the same failure.)

- [ ] **Step 3: Write the header** `src/engine/RhythmPatternGenerator.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ── Rhythmic pattern generation ──
// Deterministic drum-pattern builder: two independent euclidean pulses
// (the classic polyrhythm engine — e.g. 4-over-3) plus an optional
// rhythm-DSL voice (Strudel subset: 'x' '-' '[..]xN' 'E(k,n[,rot])',
// expanded via Generative::expandToDivision). Pure function of its
// params: identical params → identical notes. No RNG, no seed.
class RhythmPatternGenerator
{
public:
    struct Params
    {
        int grid = 16;           // steps per bar (16 = 16th notes)
        int bars = 1;            // loop length in bars; pulses span grid*bars steps

        int pulseA = 4;          // hits of pulse A across the loop (0 = disabled)
        int pulseB = 3;          // hits of pulse B across the loop (0 = disabled)
        int rotationA = 1;       // rotate pulse A hits by this many steps
        int rotationB = 1;
        int pitchA = 36;         // C2 kick
        int pitchB = 42;         // F#2 closed hat
        int velocityA = 112;
        int velocityB = 96;

        std::string dsl;         // optional DSL voice, e.g. "E(3,8,1) [x-]x2"
        int dslPitch = 39;       // C#2 clap
        int dslVelocity = 104;
    };

    struct Note
    {
        double startBeat;
        int pitch;
        int velocity;
        double durationBeats;
    };

    // Pulse A (highest priority) > pulse B > DSL voice; simultaneous hits
    // on one step resolve by priority. Output sorted by startBeat.
    // Throws std::invalid_argument on a malformed DSL string.
    static std::vector<Note> generate(const Params& params);
};
```

- [ ] **Step 4: Write the implementation** `src/engine/RhythmPatternGenerator.cpp`:

```cpp
#include "engine/RhythmPatternGenerator.h"
#include "engine/Generative.h"
#include <algorithm>
#include <stdexcept>

std::vector<RhythmPatternGenerator::Note> RhythmPatternGenerator::generate(const Params& p)
{
    const int loop = p.grid * p.bars;
    if (loop <= 0)
        return {};

    const double beatsPerStep = 4.0 / static_cast<double>(std::max(1, p.grid));

    std::vector<bool> claimed(static_cast<size_t>(loop), false);
    std::vector<Note> out;

    auto emit = [&](int step, int pitch, int velocity, double duration) {
        if (step < 0 || step >= loop || claimed[static_cast<size_t>(step)])
            return;
        claimed[static_cast<size_t>(step)] = true;
        out.push_back({ step * beatsPerStep, pitch, velocity, duration });
    };

    auto emitPulse = [&](int hits, int rotation, int pitch, int velocity, double duration) {
        if (hits <= 0)
            return;
        const int rot = ((rotation % loop) + loop) % loop;
        for (int s : HDAW::euclideanSteps(hits, loop, rot))
            emit(s, pitch, velocity, duration);
    };

    emitPulse(p.pulseA, p.rotationA, p.pitchA, p.velocityA, 0.2);
    emitPulse(p.pulseB, p.rotationB, p.pitchB, p.velocityB, 0.1);
    if (!p.dsl.empty())
        for (int s : HDAW::expandToDivision(p.dsl, loop)) // may throw std::invalid_argument
            emit(s, p.dslPitch, p.dslVelocity, 0.1);

    std::sort(out.begin(), out.end(),
              [](const Note& a, const Note& b) { return a.startBeat < b.startBeat; });
    return out;
}
```

- [ ] **Step 5: Register the sources**

`CMakeLists.txt`: add `src/engine/RhythmPatternGenerator.cpp` immediately after the `src/engine/ArrangementGenerator.cpp` line (line ~103, inside the same list).
`tests/CMakeLists.txt`: add `unit/engine/rhythm_pattern_generator_test.cpp` immediately after `unit/engine/arrangement_generator_test.cpp` (line ~53).

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=Rhythm* }`
Expected: 7 tests pass (RhythmPolyrhythm×3, RhythmDsl×2, RhythmValidation×2).

- [ ] **Step 7: Commit**

```bash
git add src/engine/RhythmPatternGenerator.h src/engine/RhythmPatternGenerator.cpp tests/unit/engine/rhythm_pattern_generator_test.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(engine): rhythm pattern generator (polyrhythm + euclidean DSL)"
```

---

### Task 2: Engine — Snare part + genre styles in `ArrangementGenerator`

**Files:**
- Modify: `src/engine/ArrangementGenerator.cpp` (genre-aware kick weights, clap scale, hat scale, new `generateSnare`, top-level wiring)
- Modify: `tests/unit/engine/arrangement_generator_test.cpp`

**Design contract:** `style 0 = Techno` must stay **bit-identical to today** (all weight multipliers for style 0 are identity; `enableSnare` defaults false). `style 1 = House`, `style 2 = DnB`. Snare MIDI note = **38**. House/Techno snare = steps {4,12} (beats 2&4) + optional ghost; DnB = two-step {4,11} + ghosts (velocity < 90) + optional high-energy hit at 14.

- [ ] **Step 1: Make `pickKickArchetype` genre-aware** (`ArrangementGenerator.cpp:115-129`)

Replace the signature and body:

```cpp
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
```

Update the call site (line ~166): `pickKickArchetype (rng, sections[static_cast<size_t>(bar)])` → `pickKickArchetype (rng, sections[static_cast<size_t>(bar)], p.style)`.

- [ ] **Step 2: Genre-scale clap presence** (`generateClap`, after line 202 `const double presence = ...`)

```cpp
const double styleScale = (p.style == 2) ? 0.35 : 1.0; // DnB: snare owns the backbeat
```

Use `presence * styleScale` in the two `rng.nextBool (...)` calls at lines 205 and 216.

- [ ] **Step 3: Genre-scale closed-hat density** (`generateHats`, closed-hat block, after `const double density = elementDensity (TrackRole::ClosedHat, e);` at line 257)

```cpp
if (p.style == 2) density *= 1.15; // DnB: busier 16ths
```

(style 0 and 1 keep identity — existing Techno output unchanged.)

- [ ] **Step 4: Add `generateSnare`** — insert after the `generateClap` function (after line 228), before the `// ── Hats ──` comment:

```cpp
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
```

- [ ] **Step 5: Wire into the top level** (`generateArrangement`, lines ~688-718)

In the declaration line (after `std::vector<ArrangementNote> kickNotes, clapNotes, ...`):

```cpp
std::vector<ArrangementNote> kickNotes, clapNotes, snareNotes, closedNotes, openNotes, bassNotes, leadNotes, chordsNotes;
```

After the `if (params.enableClap)` block (line ~694):

```cpp
if (params.enableSnare)
    snareNotes = generateSnare (params, repro, arr.energyByBar, sections);
```

After the clap `addPart` line (~718):

```cpp
if (params.enableSnare)     addPart (TrackRole::Snare, std::move (snareNotes));
```

- [ ] **Step 6: Add the failing tests** to `tests/unit/engine/arrangement_generator_test.cpp` (append before the final `} // namespace`... — the file has no namespace; append at end of file):

```cpp
// ── Snare ──

TEST(ArrangementSnare, HousePlacesBackbeatOnTwoAndFour)
{
    ArrangementParams p = defaultParams (99, 16);
    p.style = 1;
    p.enableSnare = true;
    auto arr = generateArrangement (p);
    const auto* snare = findPart (arr, TrackRole::Snare);
    ASSERT_NE (snare, nullptr);

    std::vector<std::set<int>> stepsByBar (16);
    for (const auto& n : snare->notes)
    {
        const int bar = beatToBar (n.startBeat);
        ASSERT_GE (bar, 0);
        ASSERT_LT (bar, 16);
        stepsByBar[static_cast<size_t>(bar)].insert (beatToStep (n.startBeat));
    }
    for (int bar = 0; bar < 16; ++bar)
    {
        EXPECT_NE (stepsByBar[static_cast<size_t>(bar)].count (4), 0u)
            << "bar " << bar << " lacks beat-2 snare";
        EXPECT_NE (stepsByBar[static_cast<size_t>(bar)].count (12), 0u)
            << "bar " << bar << " lacks beat-4 snare";
    }
}

TEST(ArrangementSnare, DnbTwoStepBackbeatWithQuietGhosts)
{
    ArrangementParams p = defaultParams (1234, 16);
    p.style = 2;
    p.enableSnare = true;
    auto arr = generateArrangement (p);
    const auto* snare = findPart (arr, TrackRole::Snare);
    ASSERT_NE (snare, nullptr);

    std::vector<std::set<int>> stepsByBar (16);
    for (const auto& n : snare->notes)
    {
        const int bar = beatToBar (n.startBeat);
        ASSERT_GE (bar, 0);
        ASSERT_LT (bar, 16);
        stepsByBar[static_cast<size_t>(bar)].insert (beatToStep (n.startBeat));
    }
    for (int bar = 0; bar < 16; ++bar)
    {
        EXPECT_NE (stepsByBar[static_cast<size_t>(bar)].count (4), 0u)
            << "bar " << bar << " lacks two-step snare 1";
        EXPECT_NE (stepsByBar[static_cast<size_t>(bar)].count (11), 0u)
            << "bar " << bar << " lacks two-step snare 2";
    }
    for (const auto& n : snare->notes)
    {
        const int step = beatToStep (n.startBeat);
        if (step != 4 && step != 11 && step != 14)
            EXPECT_LT (n.velocity, 90) << "ghost snare not quiet";
    }
}

TEST(ArrangementSnare, DisabledByDefault)
{
    auto arr = generateArrangement (defaultParams (42, 32));
    EXPECT_EQ (findPart (arr, TrackRole::Snare), nullptr);
}

// ── Genre styles ──

TEST(ArrangementGenre, HouseKickMoreFourOnFloorThanTechno)
{
    auto countFotfBars = [] (const Arrangement& arr) {
        const auto* kick = findPart (arr, TrackRole::Kick);
        if (!kick) return 0;
        std::set<int> bars;
        for (const auto& n : kick->notes) bars.insert (beatToBar (n.startBeat));
        int n = 0;
        for (int bar : bars)
        {
            std::set<int> steps;
            for (const auto& note : kick->notes)
                if (beatToBar (note.startBeat) == bar) steps.insert (beatToStep (note.startBeat));
            if (steps.count (0) != 0u) ++n;
        }
        return n;
    };

    ArrangementParams house = defaultParams (42, 32);
    house.style = 1;
    ArrangementParams techno = defaultParams (42, 32);
    techno.style = 0;
    const int h = countFotfBars (generateArrangement (house));
    const int t = countFotfBars (generateArrangement (techno));
    EXPECT_GT (h, t) << "house fotf bars=" << h << " techno=" << t;
}

TEST(ArrangementGenre, DnbClosedHatsDenserThanTechno)
{
    ArrangementParams dnb = defaultParams (42, 32);
    dnb.style = 2;
    ArrangementParams techno = defaultParams (42, 32);
    techno.style = 0;
    const auto* dnbHats = findPart (generateArrangement (dnb), TrackRole::ClosedHat);
    const auto* techHats = findPart (generateArrangement (techno), TrackRole::ClosedHat);
    ASSERT_NE (dnbHats, nullptr);
    ASSERT_NE (techHats, nullptr);
    EXPECT_GT (dnbHats->notes.size(), techHats->notes.size());
}
```

Check `#include <set>` is present at the top of the test file; add it if missing.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter='Arrangement*' }`
Expected: ALL Arrangement tests pass — the 5 new ones AND the pre-existing ones (evidence that style-0 output is unchanged).
Fallback (only if `ArrangementGenre.*` fails for seed 42 — extremely unlikely given the weight margins): change the shared seed in both genre tests to another fixed seed (e.g. 7) and re-run; the relative assertions are deterministic per seed by construction.

- [ ] **Step 8: Commit**

```bash
git add src/engine/ArrangementGenerator.cpp tests/unit/engine/arrangement_generator_test.cpp
git commit -m "feat(engine): snare generation + house/techno/dnb genre styles in arrangement generator"
```

---

### Task 3: RPC — `composition.generateRhythmPattern`

**Files:**
- Modify: `src/frontend/router/Router_Composition.cpp`
- Test: create `tests/unit/frontend/rhythm_generation_rpc_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the include** to `Router_Composition.cpp` (next to the other engine includes, after `#include "../../engine/ArrangementGenerator.h"`):

```cpp
#include "../../engine/RhythmPatternGenerator.h"
```

- [ ] **Step 2: Add the handler** in `Router_Composition.cpp` immediately before `return makeError(-32601, "unknown composition method: " + m);` (line 221):

```cpp
    if (m == "generateRhythmPattern")
    {
        int trackIndex;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");

        RhythmPatternGenerator::Params rp;
        rp.grid        = optInt(o, "grid", 16, nullptr);
        rp.bars        = optInt(o, "bars", 1, nullptr);
        rp.pulseA      = optInt(o, "pulseA", 4, nullptr);
        rp.pulseB      = optInt(o, "pulseB", 3, nullptr);
        rp.rotationA   = optInt(o, "rotationA", 1, nullptr);
        rp.rotationB   = optInt(o, "rotationB", 1, nullptr);
        rp.pitchA      = optInt(o, "pitchA", 36, nullptr);
        rp.pitchB      = optInt(o, "pitchB", 42, nullptr);
        rp.velocityA   = optInt(o, "velocityA", 112, nullptr);
        rp.velocityB   = optInt(o, "velocityB", 96, nullptr);
        rp.dsl         = optString(o, "dsl", "", nullptr);
        rp.dslPitch    = optInt(o, "dslPitch", 39, nullptr);
        rp.dslVelocity = optInt(o, "dslVelocity", 104, nullptr);

        double startBeat = optDouble(o, "startBeat", 0.0, nullptr);

        std::vector<RhythmPatternGenerator::Note> notes;
        try
        {
            notes = RhythmPatternGenerator::generate(rp);
        }
        catch (const std::invalid_argument& ex)
        {
            return makeError(-32602, QString("dsl: ") + ex.what());
        }
        if (notes.empty())
            return makeError(-32602, "pattern produced no notes");

        std::vector<PhraseGenerator::GeneratedNote> converted;
        converted.reserve(notes.size());
        for (const auto& n : notes)
            converted.push_back({ n.startBeat, n.pitch, n.velocity, n.durationBeats });

        const double totalBeats = (std::max)(1, rp.bars) * 4.0;
        return generateIntoClip(trackIndex, startBeat, totalBeats, "Rhythm Pattern", converted);
    }
```

(`optString` returns `std::string` — signature confirmed at Router_Export.cpp:39. `#include <algorithm>` is already available via the existing headers; add it if the build complains about `std::max`.)

- [ ] **Step 3: Write the RPC tests** — create `tests/unit/frontend/rhythm_generation_rpc_test.cpp`, mirroring the dispatch seam from `ghost_clips_rpc_test.cpp:10-45`:

```cpp
// RPC-layer tests for composition.generateRhythmPattern (v0.15.2+).
// Exercises the exact JSON-RPC dispatch path the frontend uses:
// frontend::dispatch() with parsed QJsonValue params.

#include <gtest/gtest.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"

namespace {

QJsonValue rpc(AudioEngine& engine, const QString& method, const QJsonValue& params = {})
{
    auto r = frontend::dispatch(engine, method, params);
    EXPECT_FALSE(r.isError)
        << "dispatch(" << method.toStdString() << ") returned error: "
        << (r.payload.isObject() ? r.payload.toObject().value("message").toString().toStdString()
                                 : std::string("non-object error"));
    return r.payload;
}

QJsonObject findClipJson(const QJsonObject& snap, int clipId)
{
    for (const auto& v : snap.value("clips").toArray())
    {
        auto o = v.toObject();
        if (static_cast<int>(o.value("clipId").toDouble()) == clipId)
            return o;
    }
    ADD_FAILURE() << "clip " << clipId << " missing from snapshot JSON";
    return {};
}

} // namespace

TEST(RhythmGenerationRpc, DefaultPolyrhythmCreatesClip)
{
    AudioEngine engine;
    engine.initialize();

    auto resp = rpc(engine, "composition.generateRhythmPattern",
                    QJsonObject{ { "trackIndex", 0 } });
    ASSERT_TRUE(resp.isObject());
    const int clipId = resp.toObject().value("clipId").toInt();
    const int noteCount = resp.toObject().value("noteCount").toInt();
    ASSERT_GT(clipId, 0);
    EXPECT_EQ(noteCount, 6);

    auto snap = rpc(engine, "read.snapshot").toObject();
    auto clip = findClipJson(snap, clipId);
    EXPECT_EQ(static_cast<int>(clip.value("trackIndex").toDouble()), 0);
    EXPECT_DOUBLE_EQ(clip.value("startBeat").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(clip.value("duration").toDouble(), 4.0);
}

TEST(RhythmGenerationRpc, DslVoiceRespected)
{
    AudioEngine engine;
    engine.initialize();
    auto resp = rpc(engine, "composition.generateRhythmPattern",
                    QJsonObject{ { "trackIndex", 0 },
                                 { "pulseA", 0 }, { "pulseB", 0 },
                                 { "dsl", "E(3,8)" } });
    ASSERT_TRUE(resp.isObject());
    EXPECT_EQ(resp.toObject().value("noteCount").toInt(), 3);
}

TEST(RhythmGenerationRpc, MalformedDslReturnsError)
{
    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "composition.generateRhythmPattern",
                                QJsonObject{ { "trackIndex", 0 }, { "dsl", "E(3,8" } });
    EXPECT_TRUE(r.isError);
    EXPECT_TRUE(r.payload.toObject().value("message").toString().contains("dsl"));
}

TEST(RhythmGenerationRpc, MissingTrackIndexErrors)
{
    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "composition.generateRhythmPattern", QJsonObject{});
    EXPECT_TRUE(r.isError);
}
```

- [ ] **Step 4: Register the test** — `tests/CMakeLists.txt`: add `unit/frontend/rhythm_generation_rpc_test.cpp` after `unit/frontend/envelope_generation_rpc_test.cpp` (line ~80).

- [ ] **Step 5: Run the tests**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=RhythmGenerationRpc* }`
Expected: 4 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/frontend/router/Router_Composition.cpp tests/unit/frontend/rhythm_generation_rpc_test.cpp tests/CMakeLists.txt
git commit -m "feat(rpc): composition.generateRhythmPattern (polyrhythm + DSL)"
```

---

### Task 4: MCP — `generate_rhythm_pattern` tool + `enableSnare` on `generate_arrangement`

**Files:**
- Modify: `src/mcp/McpTools_Project.cpp`
- Modify: `tests/integration/mcp/mcp_server_test.cpp`

- [ ] **Step 1: Add the include** (top of `McpTools_Project.cpp`, next to the existing engine includes):

```cpp
#include "engine/RhythmPatternGenerator.h"
```

- [ ] **Step 2: Register the tool** in the composition registration function, immediately after the `generate_progression` tool (after line 946):

```cpp
    s.registerTool({"generate_rhythm_pattern", "Generate a drum/percussion rhythm pattern into a new MIDI clip: two euclidean pulses (polyrhythm, e.g. 4-over-3) plus an optional rhythm-DSL voice ('x' '-' '[..]xN' 'E(k,n[,rot])'). Pure function of its params (no seed).",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"start",       QJsonObject{{"type","number"}}},
                  {"grid",        QJsonObject{{"type","integer"},{"minimum",1},{"maximum",64}}},
                  {"bars",        QJsonObject{{"type","integer"},{"minimum",1},{"maximum",16}}},
                  {"pulseA",      QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"pulseB",      QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"rotationA",   QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"rotationB",   QJsonObject{{"type","integer"},{"minimum",0}}},
                  {"pitchA",      QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"pitchB",      QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"velocityA",   QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"velocityB",   QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"dsl",         QJsonObject{{"type","string"}}},
                  {"dslPitch",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"dslVelocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}},
                 {"trackId"}),
        [e, helper = generateIntoClip](const QJsonObject& a) -> McpToolResult {
            RhythmPatternGenerator::Params p;
            p.grid        = a.value("grid").toInt(16);
            p.bars        = a.value("bars").toInt(1);
            p.pulseA      = a.contains("pulseA") ? a.value("pulseA").toInt() : 4;
            p.pulseB      = a.contains("pulseB") ? a.value("pulseB").toInt() : 3;
            p.rotationA   = a.contains("rotationA") ? a.value("rotationA").toInt() : 1;
            p.rotationB   = a.contains("rotationB") ? a.value("rotationB").toInt() : 1;
            p.pitchA      = a.contains("pitchA") ? a.value("pitchA").toInt() : 36;
            p.pitchB      = a.contains("pitchB") ? a.value("pitchB").toInt() : 42;
            p.velocityA   = a.contains("velocityA") ? a.value("velocityA").toInt() : 112;
            p.velocityB   = a.contains("velocityB") ? a.value("velocityB").toInt() : 96;
            p.dsl         = a.contains("dsl") ? a.value("dsl").toString().toStdString() : std::string();
            p.dslPitch    = a.contains("dslPitch") ? a.value("dslPitch").toInt() : 39;
            p.dslVelocity = a.contains("dslVelocity") ? a.value("dslVelocity").toInt() : 104;

            std::vector<RhythmPatternGenerator::Note> notes;
            try { notes = RhythmPatternGenerator::generate(p); }
            catch (const std::invalid_argument& ex)
            { return McpToolResult::text(QString("dsl error: ") + ex.what(), true); }
            if (notes.empty())
                return McpToolResult::text("pattern produced no notes", true);

            std::vector<PhraseGenerator::GeneratedNote> converted;
            converted.reserve(notes.size());
            for (const auto& n : notes)
                converted.push_back({ n.startBeat, n.pitch, n.velocity, n.durationBeats });
            return helper(a.value("trackId").toInt(),
                          a.value("start").toDouble(0.0),
                          (double) std::max(1, p.bars) * 4.0, converted);
        }});
```

(`generateIntoClip` is the lambda defined at McpTools_Project.cpp:813-830; `std::max` requires `<algorithm>` — already included transitively; add if the build complains.)

- [ ] **Step 3: Add `enableSnare` to `generate_arrangement`**

Schema (McpTools_Project.cpp:956-962, after `"enableClap"`):
```cpp
                  {"enableSnare",   QJsonObject{{"type","boolean"}}},
```
Handler (McpTools_Project.cpp:976, after the `enableClap` line):
```cpp
            p.enableSnare = a.contains("enableSnare") ? a.value("enableSnare").toBool() : false;
```
Also extend the tool description to document styles: `... Deterministic for a given seed (0 = random). style: 0=Techno 1=House 2=DnB.`

- [ ] **Step 4: Add the MCP integration test** to `tests/integration/mcp/mcp_server_test.cpp`, mirroring the existing `generate_phrase` tools/call case at line ~709. Find the test that issues `tools/call` "generate_phrase" and add a sibling case in the same test:

```cpp
        // generate_rhythm_pattern: default polyrhythm -> 6 notes, plus DSL + snare flags.
        {
            QString req = QString(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"generate_rhythm_pattern","arguments":{"trackId":0}}})");
            QString respText = serverJsonRpc(req);
            EXPECT_TRUE(respText.contains("notes=6")) << respText.toStdString();
        }
        {
            QString req = QString(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"generate_rhythm_pattern","arguments":{"trackId":0,"pulseA":0,"pulseB":0,"dsl":"E(3,8)"}}})");
            QString respText = serverJsonRpc(req);
            EXPECT_TRUE(respText.contains("notes=3")) << respText.toStdString();
        }
```

Match the exact fixture style of the enclosing test (variable names, JSON-RPC helper, track index expectations) — read the surrounding test before writing; adapt `serverJsonRpc`/`run` to the local helper names.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter='*MCP*:*Mcp*' }`
Expected: the new cases pass; existing MCP tests pass (list_tools now includes `generate_rhythm_pattern`).

- [ ] **Step 6: Commit**

```bash
git add src/mcp/McpTools_Project.cpp tests/integration/mcp/mcp_server_test.cpp
git commit -m "feat(mcp): generate_rhythm_pattern tool + enableSnare/genre styles on generate_arrangement"
```

---

### Task 5: UI — "Rhythm" mode + Arrangement snare/genre controls in `PhraseGeneratorDialog`

**Files:**
- Modify: `frontend/src/components/PhraseGeneratorDialog.tsx`
- Modify: `frontend/src/rpc/types.ts`
- Test: create `frontend/src/components/PhraseGeneratorDialog.test.tsx`

- [ ] **Step 1: Add rhythm state + mode option** (`PhraseGeneratorDialog.tsx`)

After the "Progression params" block (line ~70), add:

```tsx
  // Rhythm params (mode 4)
  const [rhythmGrid, setRhythmGrid] = useState(16);
  const [rhythmBars, setRhythmBars] = useState(1);
  const [pulseA, setPulseA] = useState(4);
  const [pulseB, setPulseB] = useState(3);
  const [rotationA, setRotationA] = useState(1);
  const [rotationB, setRotationB] = useState(1);
  const [pitchA, setPitchA] = useState(36);
  const [pitchB, setPitchB] = useState(42);
  const [velocityA, setVelocityA] = useState(112);
  const [velocityB, setVelocityB] = useState(96);
  const [rhythmDsl, setRhythmDsl] = useState("");
  const [dslPitch, setDslPitch] = useState(39);
```

In the "Arrangement params" block (line ~45), add:

```tsx
  const [enSnare, setEnSnare] = useState(false);
  const [arrStyle, setArrStyle] = useState(0);
```

- [ ] **Step 2: Extend `handleGenerate`**

Mode-3 branch (`PhraseGeneratorDialog.tsx:160-172`) — add the two new params:

```tsx
      } else if (mode === 3) {
        const arr = await rpc.call("composition.generateArrangement", {
          ...shared,
          bars,
          complexity,
          swingPercent: swing,
          style: arrStyle,
          enableKick: enKick,
          enableClosedHat: enHats,
          enableOpenHat: enHats,
          enableClap: enClap,
          enableSnare: enSnare,
          enableBass: enBass,
          enableLead: enLead,
          enableChords: enChords,
        }) as { trackIndices: number[]; clipIds: number[]; noteCount: number; seed: number };
```

Add the mode-4 branch after the mode-3 block (after line 177):

```tsx
      } else if (mode === 4) {
        const result = await rpc.call("composition.generateRhythmPattern", {
          trackIndex,
          startBeat: 0,
          grid: rhythmGrid,
          bars: rhythmBars,
          pulseA,
          pulseB,
          rotationA,
          rotationB,
          pitchA,
          pitchB,
          velocityA,
          velocityB,
          dsl: rhythmDsl.trim(),
          dslPitch,
        }) as { clipId: number; noteCount: number };
        setPreview(`Rhythm: ${result.noteCount} notes`);
        useProjectStore.setState({ isDirty: true });
        setTimeout(() => onClose(), 400);
        return;
      }
```

- [ ] **Step 3: Add the mode selector option** (line ~210, after "Arrangement"):

```tsx
              <option value={4}>Rhythm</option>
```

- [ ] **Step 4: Add the Arrangement-page controls** (inside the mode-3 page, after the "Tracks" row, line ~433):

```tsx
              <div className="pgd-row">
                <label className="pgd-label">Style</label>
                <select className="pgd-select" value={arrStyle} onChange={(e) => setArrStyle(Number(e.target.value))}>
                  <option value={0}>Techno</option>
                  <option value={1}>House</option>
                  <option value={2}>DnB</option>
                </select>
              </div>
```

and inside the existing "Tracks" `pgd-inline-group` (line ~426-431), add:

```tsx
                  <label className="pgd-label-sm"><input type="checkbox" checked={enSnare} onChange={(e) => setEnSnare(e.target.checked)} /> Snare</label>
```

- [ ] **Step 5: Add the Rhythm page** (after the mode-3 page block, line ~435):

```tsx
          {/* Rhythm page */}
          {mode === 4 && (
            <div className="pgd-page">
              <div className="pgd-row">
                <label className="pgd-label">Grid</label>
                <select className="pgd-select" value={rhythmGrid} onChange={(e) => setRhythmGrid(Number(e.target.value))}>
                  <option value={8}>8th notes</option>
                  <option value={16}>16th notes</option>
                  <option value={32}>32nd notes</option>
                </select>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Bars</label>
                <input className="pgd-input" type="number" min={1} max={8} value={rhythmBars} onChange={(e) => setRhythmBars(Number(e.target.value))} />
              </div>
              <div className="pgd-row-group">
                <div className="pgd-row">
                  <label className="pgd-label">Pulse A hits</label>
                  <input className="pgd-input" type="number" min={0} max={64} value={pulseA} onChange={(e) => setPulseA(Number(e.target.value))} />
                </div>
                <div className="pgd-row">
                  <label className="pgd-label">Rotate A</label>
                  <input className="pgd-input" type="number" min={0} max={64} value={rotationA} onChange={(e) => setRotationA(Number(e.target.value))} />
                </div>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Pitch A</label>
                <select className="pgd-select pgd-note-select" value={pitchA} onChange={(e) => setPitchA(Number(e.target.value))}>
                  {Array.from({ length: 128 }, (_, n) => (
                    <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1} ({n})</option>
                  ))}
                </select>
              </div>
              <div className="pgd-row-group">
                <div className="pgd-row">
                  <label className="pgd-label">Pulse B hits</label>
                  <input className="pgd-input" type="number" min={0} max={64} value={pulseB} onChange={(e) => setPulseB(Number(e.target.value))} />
                </div>
                <div className="pgd-row">
                  <label className="pgd-label">Rotate B</label>
                  <input className="pgd-input" type="number" min={0} max={64} value={rotationB} onChange={(e) => setRotationB(Number(e.target.value))} />
                </div>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">Pitch B</label>
                <select className="pgd-select pgd-note-select" value={pitchB} onChange={(e) => setPitchB(Number(e.target.value))}>
                  {Array.from({ length: 128 }, (_, n) => (
                    <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1} ({n})</option>
                  ))}
                </select>
              </div>
              <div className="pgd-row">
                <label className="pgd-label">DSL</label>
                <input className="pgd-input" type="text" value={rhythmDsl} placeholder='E.g. "E(3,8,1) [x-]x2"' onChange={(e) => setRhythmDsl(e.target.value)} />
              </div>
              <div className="pgd-row">
                <label className="pgd-label">DSL Pitch</label>
                <select className="pgd-select pgd-note-select" value={dslPitch} onChange={(e) => setDslPitch(Number(e.target.value))}>
                  {Array.from({ length: 128 }, (_, n) => (
                    <option key={n} value={n}>{NOTE_NAMES[n % 12]}{Math.floor(n / 12) - 1} ({n})</option>
                  ))}
                </select>
              </div>
            </div>
          )}
```

Note: velocity A/B are sent as state defaults (112/96) — the RPC defaults match, so no extra UI is needed for v1 (velocity controls can be a follow-up).

- [ ] **Step 6: Add the result type** to `frontend/src/rpc/types.ts` (after `GenerateResult`, line ~194):

```ts
export interface RhythmPatternResult {
  clipId: number;
  noteCount: number;
}
```

- [ ] **Step 7: Write the Vitest test** — create `frontend/src/components/PhraseGeneratorDialog.test.tsx`, using the established mock pattern (`MidiFxChain.test.tsx:10`):

```tsx
import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, act, waitFor } from "@testing-library/react";
import PhraseGeneratorDialog from "./PhraseGeneratorDialog";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

async function flushRead() {
  await act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

describe("PhraseGeneratorDialog rhythm mode", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue({});
    useProjectStore.setState({
      snapshot: {
        tracks: [{ index: 0, name: "Track 1", clipCount: 0 }],
        scaleRoot: 0,
        scaleMode: 0,
        clips: [],
      } as never,
      isDirty: false,
    });
  });

  afterEach(() => {
    cleanup();
  });

  it("shows the Rhythm mode option and its controls", async () => {
    render(<PhraseGeneratorDialog onClose={vi.fn()} />);
    await flushRead();
    expect(screen.getByRole("option", { name: "Rhythm" })).toBeInTheDocument();
  });

  it("sends a single generateRhythmPattern call with pulse params when generating", async () => {
    render(<PhraseGeneratorDialog onClose={vi.fn()} />);
    await flushRead();
    fireEvent.change(screen.getByRole("combobox", { name: "Mode" }), { target: { value: "4" } });
    fireEvent.change(screen.getByPlaceholderText('E.g. "E(3,8,1) [x-]x2"'), {
      target: { value: "E(3,8)" },
    });
    mockedCall.mockResolvedValue({ clipId: 7, noteCount: 3 });
    fireEvent.click(screen.getByRole("button", { name: "Generate" }));
    await waitFor(() => {
      const call = mockedCall.mock.calls.find((c) => c[0] === "composition.generateRhythmPattern");
      expect(call).toBeTruthy();
      const args = call![1] as Record<string, unknown>;
      expect(args.trackIndex).toBe(0);
      expect(args.pulseA).toBe(4);
      expect(args.pulseB).toBe(3);
      expect(args.dsl).toBe("E(3,8)");
    });
  });
});
```

Adjust `useProjectStore.setState` payload to the actual `ProjectSnapshot` shape (read `frontend/src/store/projectStore.ts` for the current type; the cast `as never` is a stopgap if the snapshot type is strict — fix it to a minimal valid snapshot object). If the "Mode" select lacks an accessible name, add `aria-label="Mode"` to the select in `PhraseGeneratorDialog.tsx` (line ~206).

- [ ] **Step 8: Run the frontend tests**

Run: `cd frontend; if ($?) { npm test -- --run PhraseGeneratorDialog }`
Expected: the two new tests pass.
Then: `cd frontend; if ($?) { npm run build }` — Expected: succeeds.

- [ ] **Step 9: Commit**

```bash
git add frontend/src/components/PhraseGeneratorDialog.tsx frontend/src/rpc/types.ts frontend/src/components/PhraseGeneratorDialog.test.tsx
git commit -m "feat(ui): rhythm pattern mode + snare/genre controls in phrase generator dialog"
```

---

### Task 6: Docs, knowledge graph, full verification

**Files:**
- Modify: `README.md` (feature list), `AGENTS.md` (Generative composition section)

- [ ] **Step 1: Update docs**

`README.md`: in the feature list (near line 182), extend the composition mention: add "polyrhythmic & euclidean rhythm pattern generation (`RhythmPatternGenerator`)", and note genre styles (Techno/House/DnB) + snare support in the arrangement generator.

`AGENTS.md` → "Generative composition" section (line ~305): add one sentence: rhythm/drum patterns also come from `RhythmPatternGenerator` (`src/engine/RhythmPatternGenerator.h`) — two euclidean pulses (polyrhythm) + rhythm-DSL voice (`E(k,n[,rot])`, groups), exposed over RPC as `composition.generateRhythmPattern` (and MCP `generate_rhythm_pattern`), surfaced in the UI by the "Rhythm" mode in `PhraseGeneratorDialog`.

- [ ] **Step 2: Full engine test suite**

Run: `cmake --build build --config Debug; if ($?) { build\Debug\hdaw_tests.exe }`
Expected: all suites pass (250+ tests, including the pre-existing arrangement/phrase/generative suites).

- [ ] **Step 3: Full frontend verification**

Run: `cd frontend; if ($?) { npm test }`
Expected: ~179+ tests pass (existing 177 + 2 new).

- [ ] **Step 4: E2E sanity** (engine must be rebuilt)

Run: `cd frontend; if ($?) { npm run test:e2e -- --grep "Phrase" }` (or run the full suite if the project convention runs it whole)
Expected: no regressions in the existing user-journey tests that touch generation/arrangement.

- [ ] **Step 5: Gate 8 scan**

Run: `Select-String -Path "frontend\src\**\*.css" -Pattern "#[0-9a-fA-F]{3,8}"` — no new matches in files touched by this plan (PhraseGeneratorDialog.css untouched).

- [ ] **Step 6: Refresh the knowledge graph**

Run the codebase-memory MCP `index_repository` (repo_path `D:\pdf\roo projects\hdaw3`, mode `fast`) so the graph indexes `RhythmPatternGenerator` and the new RPC method. If `graphify` CLI is configured, also run `graphify . --update`.

- [ ] **Step 7: Optional full pipeline** (only if the packaged Electron app must carry the feature now)

Run: `frontend\build.bat` — rebuilds SPA + C++ + tests + repackages Electron (this is the canonical end-to-end pipeline; expensive, so run last).

- [ ] **Step 8: Commit**

```bash
git add README.md AGENTS.md
git commit -m "docs: rhythm pattern generation feature notes"
```

---

## Self-Review

- **Spec coverage:** All three gaps are covered — polyrhythm (Task 1), Euclidean DSL wiring (Tasks 1/3/4/5 — `expandToDivision` now reachable via `dsl` param end-to-end), snare + genre presets (Task 2 + MCP `enableSnare`/style in Task 4 + UI in Task 5). Surfaces: engine (G1/G2), RPC (G3), MCP (G4), UI (G5). MCP parity holds (G8).
- **Placeholder scan:** no TBD/TODO; every edit has exact code; test expectations are exact deterministic values computed from `euclideanSteps`/`expandToDivision` semantics (verified against `Generative.cpp:88-117` and `generative_test.cpp:96-184`).
- **Type consistency:** `RhythmPatternGenerator::Note` {startBeat, pitch, velocity, durationBeats} ↔ `PhraseGenerator::GeneratedNote` {startBeat, noteNumber, velocity, durationBeats} — conversion in Task 3 Step 2 and Task 4 Step 2 maps pitch→noteNumber in the same field order. Params field names (`grid/bars/pulseA/pulseB/rotationA/rotationB/pitchA/pitchB/velocityA/velocityB/dsl/dslPitch/dslVelocity`) are identical across engine (Task 1), RPC (Task 3), MCP (Task 4), and UI (Task 5). Defaults match at every layer (4/3 hits, rotations 1, pitches 36/42/39, velocities 112/96/104, grid 16, bars 1).
- **One known risk (called out in Task 2 Step 7):** the two relative genre tests (`HouseKickMoreFourOnFloorThanTechno`, `DnbClosedHatsDenserThanTechno`) assert fixed-seed relative inequalities. They are deterministic per seed and the weight margins are wide (0.91 vs 0.50 fotf weight; 15% hat-density boost), so failure is unlikely; the fallback (bump the seed) is documented.
- **One known open point (called out in Task 5 Step 7):** the `Mode` select may need an `aria-label` for the test's accessible-name query, and the store-setState payload shape must match `ProjectSnapshot` — both have explicit fallback instructions.

---

## Execution notes (2026-08-11, subagent-driven, all gates green)

Implemented on main. Commits: `61988ce` (Task 1 engine), `3a08d06` (Task 2 snare+genre), `a3c5369` (Task 3 RPC), `24f60cb` (Task 4 MCP), `f9cff20` (Task 5 UI), `e66d5e5` (Task 6 docs), `4db729a` (E2E option-count fix).

Approved deviations from the plan text (each fixes a defect in the plan's reference code):
- **Task 2 genre tests** � the step-0 fotf metric is style-invariant (all four kick archetypes anchor step 0, `ArrangementGenerator.cpp:131-151`), so the test tied at every probed seed; replaced with the {4,12} backbeat metric (house 9 vs 5 at seed 42). The hats test had a pointer-into-destroyed-temporary UB (`findPart(generateArrangement(...))`); arrangements now stored in locals. Seed 42 kept.
- **Task 3** � `optString` is 3-arg (`RouterHelpers.h:97`), not 4-arg; snapshot clip duration field is `durationBeats` (`FrontendRpc.h:107`), not `duration`.
- **Task 4** � MCP integration test uses the file's real fixtures (`parseOne`/`textOf`); the raw string needed the `)rp"` delimiter because `E(3,8)` contains `)"`. No `generate_arrangement` tools/call anchor existed, so none added.
- **Task 5** � Vitest mock must resolve the four metadata RPC methods to `[]` (bare `{}` crashes the Scale select); snapshot setState uses a typed minimal `ProjectSnapshot`; mode-4 result cast uses the new `RhythmPatternResult` type. `aria-label="Mode"` added to the Mode select.
- **Final review** � E2E `phrase-generator.spec.ts` mode-select count 4 ? 5 with a "Rhythm" option assertion (the new option broke the pre-existing regression wall).

Verification: engine 735/741 (the 6 failures are the documented pre-existing CLAP-export/crash-recovery environment issues, proven via stash-rebuild), frontend 320/320, Phrase E2E 16/16, `npm run build` and full C++ build green. All 13 cross-layer params verified identical across engine/RPC/MCP/UI. Remaining manual step for a packaged release: `frontend\build.bat`.
