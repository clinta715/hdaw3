# Plan: seeded rhythm-cell defaults (A) + corpus bank for fill_cells (B)

Date: 2026-09-14. Status: implemented (all gates pass; fast tier 1535/1535 on a verified-fresh binary).

## Goal
Kill the identical-percussion problem: bare `fill_cells` rhythm cells repeat
pulseA=4/pulseB=3/rot 1/1 in every song (seed ignored), and the 62-phrase
corpus bank is unreachable from the song workflow.

## Success Gates
- [ ] A1: bare rhythm cell (no euclidean params) varies with plan/cell seed;
  same seed refilled twice is byte-identical (determinism kept).
- [ ] A2: explicit params unchanged — existing song_plan tests (explicit
  pulseA/pitchA) + mcp_server rhythm tests green unmodified.
- [ ] A3: DSL cells byte-identical (dsl present + pulses absent keeps legacy
  4/3/1/1 defaults, since DSL and pulses ADD in the generator).
- [ ] B1: `params.corpusRole` (e.g. hats/snare/kick/clap/perc) fills the cell
  from a seeded bank-phrase pick, tiled across the section window at the
  phrase GM pitch (or explicit pitchA), deterministic per seed.
- [ ] B2: unknown/empty bank role falls back to the seeded euclidean grid
  (ok, never an error) — corpus is an enhancement, not a new failure mode.
- [ ] B3: tiling covers the full window (last-bar notes present for sections
  longer than the phrase).
- [ ] Focused suites (song_plan, psytrance_markov, rhythm_pattern_generator,
  mcp_server song tests) + fast tier green on a verified-fresh binary.

## Dependency Map
- Blast radius: ONE function (fillCell rhythm branch,
  AudioEngineCommands_Song.cpp) + param docs + tests. No processBlock/DSP/
  graph/SPSC/render changes; addNote path unchanged.
- Upstream: set_cell/fill_cells/reroll (MCP + RPC) — paramsJson free-form,
  no schema/tool changes needed.
- Downstream: MIDI clips only. Seeded-default change alters output for
  bare rhythm cells under OLD seeds (intended — that output was the bug).
- God nodes: none touched. Projections: ValueTree clips (existing path).
- Path integrity: params plumbing (paramI/paramS/hasProperty) pre-exists;
  bank API (rhythmPhrasesForRole) + generator dsl path pre-exist (bank
  generatePhrase precedent sets pulses 0 with dsl).

## Pitfall Gates Triggered
- Gate 2 (silent no-op): corpusRole with an empty bank role must fall back
  audibly (euclidean), tested in B2 — never a silent empty clip.
- Lesson 1 (beats vs seconds): tiling offsets in beats (phrase bars * 4.0),
  consistent with the branch.
- Lesson 9 (zero tracks): new tests create their own tracks/plan from scratch.
- Determinism: fixed rng draw order (pulseA, pulseB, rotA, rotB; phrase
  pick), seed_seq from the 53-bit cell seed (MarkovArranger precedent).
  Explicit params consume zero rng (PercussionEngine short-circuit precedent).

## Steps
1. A: seeded euclidean defaults in the rhythm branch (dsl-absent only).
2. B: corpusRole pick + tile (pulses 0, dsl=phrase, pitch=phrase|pitchA).
3. Docs: CellRecipe rhythm params comment + set_cell/fill_cells tool
   descriptions + arranger.md one-liner (adoption).
4. Tests: A1/B1/B2/B3 in song_plan_test.cpp (A2/A3 covered by existing tests).
5. Build (Windows sync dance) + focused suites + fast tier.
