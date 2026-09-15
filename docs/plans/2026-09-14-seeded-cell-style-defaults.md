# Plan: seeded cell style defaults (break/phrase/pad) + playbook refresh

Date: 2026-09-14. Status: implemented (all gates pass; fast tier 1539/1539 on a verified-fresh binary).

## Goal
Extend the A-pattern (seed the defaults, pin the choices) to the remaining
fixed content templates: break style (always amen), phrase style (always
Standard except pad), pad voicing (always root+5+oct). Plus a playbook
refresh for the agent-side repetition vectors (seed reuse, shallow
shortlists, no mutation pass).

## Success Gates
- [ ] E1: bare break cell varies with seed, deterministic per seed; explicit
  style (incl. invalid-name error) unchanged. Fill-level break needs sampler
  context (no precedent) — covered by pure-helper tests + generator suite.
- [ ] E2: role style sets — bass->BassLine, pad->Pad (pinned); lead/arp/
  stab draw from 2-entry musical sets and vary across seeds; unknown role
  ->Standard (legacy). Fill smoke: bare lead cell fills ok + deterministic.
- [ ] E3: pad notes at fill level belong to one of 3 known voicing shapes
  ({7,12},{7,12,19},{12,19}); shapes vary across seeds.
- [ ] P1-P4 playbook edits: SKILL fresh-seed mandate + brief template
  variety; selector shortlist depth; layer-agent mutation gate; CellRecipe
  + set_cell doc touch.
- [ ] Focused suites + fast tier green on a verified-fresh binary.

## Dependency Map
- New header SeededCellDefaults.h (pure inline pickers; seed_seq from the
  53-bit cell seed; explicit-always-wins preserved by callers).
- Blast radius: fillCell phrase/break branches only. No DSP/graph/SPSC.
- Style precedence preserved: preset style -> params.style -> seeded role
  default (pad still resolves Pad = B6 intact). Break: explicit invalid
  name still errors.
- Determinism: one rng per helper call, fixed draw order (single draw each).

## Pitfall Gates Triggered
- Gate 2: unknown break-style NAME still errors (explicit path untouched).
- Lesson 1: no time units involved. Lesson 9: tests self-seed tracks/plan.
- Musical safety: sets are consonant/function-fixed (bass/pad pinned;
  intervals perfect only; lead never draws BassLine).

## Steps
1. SeededCellDefaults.h (3 pickers + interval table).
2. _Song.cpp wiring (phrase styleExplicit, pad shape, break default).
3. Tests in song_plan_test.cpp (helper units + fill-level).
4. Docs: CellRecipe comment, set_cell desc, SKILL, selector, layer-agent.
5. Windows sync dance + focused suites + fast tier.
