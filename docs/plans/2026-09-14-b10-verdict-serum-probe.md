# Plan: B10 crash verdict (no code) + Serum2 state-honoring probe

Date: 2026-09-14. Status: B10 verdict recorded (no code change); Serum probe harness landed, verdict pending audio (see below).

## Item 1 — B10 MasterBusFx rebuild AV: verdict WITHOUT code change

Finding: both crashing tests pass in isolation (16/16 across MasterBusFx +
TrackMixerState). Rebuild path restores gain + FX chain; fresh processor
starts bypassed with zeroed atomics; processBlock on the unprepared object
only touches atomics/buffer/meter-update (LUFS block guarded by
kwInitialized) — no unguarded deref on the direct-drive path. The AV is
order-dependent (heap corruption from an earlier suite) or audio-outage
collateral, NOT a deterministic master-bus defect.

Effort/risk (sound-engine rule): touching rebuildRoutingGraph / master DSP
without a deterministic repro is wide-blast-radius speculation with real
regression risk to render/export. Cost of being wrong >> cost of waiting.
Recommendation: NO production change; re-run full suite after RDP reconnect
and triage any remaining AV with a stack trace then.

Success gates:
- [ ] Verdict recorded here with evidence (isolated 16/16 pass output).
- [ ] `git diff --stat` shows zero changes under src/engine/MasterBus*,
  RoutingManager*, LevelMeter.h for this item (no speculative edits).

## Item 2 — Serum2 setStateInformation probe (test-only change)

Goal: determine, by the Dexed standard (state-compare), whether Serum2
 honors setStateInformation: snapshot S0, tweak a param (S1 must differ or
 the probe is inconclusive), restore S0, read S2. S2==S0 honors; S2==S1
 ignores.

Success gates:
- [ ] Probe test runs gated (HDAW_REAL_PLUGIN_TESTS + bundle exists),
  skips cleanly otherwise.
- [ ] Verdict (honors vs ignores) recorded from test output.
- [ ] Follow-up disposition taken on evidence (keep path / cut path /
  convert test to documentation); no failing test left behind.

## Dependency Map
- Blast radius: ONE new test file + one CMakeLists line. No engine/MCP/
  DSP/graph changes. Uses existing setFxSlotPlugin + get/setStateInformation
  APIs only.
- Upstream: none (new file). Downstream: none (gated test).
- God nodes: none. Projections: none. SPSC: none.
- Path integrity: setFxSlotPlugin -> live instance -> get/setStateInformation
  is the established instrument_part_test precedent.

## Pitfall Gates Triggered
- Gate 9: null-check track/slot/instance at every step (Gate: lesson 2
  setProperty n/a — no tree writes asserted).
- Lesson 16: no new lifecycle calls (existing load path only).
- Lesson 11: relies on test_main message pump (established pattern).
- Anti-pattern: new .cpp MUST be added to tests/CMakeLists.txt source list.
- Env discipline: HDAW_REAL_PLUGIN_TESTS gate (instrument_part_test
  precedent); never unguarded real-plugin load.

## Steps
1. Write tests/unit/engine/serum_state_probe_test.cpp + CMakeLists entry.
2. Pre-build time-sync; build; run filtered (with env var set).
3. Record verdict; disposition (keep/cut/convert) in this file + report.

## FINAL VERDICT 2026-09-14 (user decision: stop — Serum2 is retired)

The probe completed enough cycles to close the question, and the answer is
negative across every path tried:

- `setStateInformation` round-trip: **S0 == S1 == S2** (3076 B, byte-identical)
  regardless of param tweaks, driven blocks, or dispatch pumps. The state
  snapshot NEVER reflects any change — there is nothing to restore, so the
  Dexed-style honors/ignores comparison is vacuous.
- Program switches: Serum2 exposes **128 programs**, and `setCurrentProgram(1)`
  also produced a byte-identical 3076 B blob. The program API is cosmetic
  (same family as TyrellN6, verified live 2026-08-19).
- Render path (auditionPlugin through the real graph, track clips + note):
  audible (peak ≈ 0.20, rms ≈ 0.076 — Serum renders its default patch), but
  **identical across param extremes** (v=0.9 vs v=0.05, persisted to tree
  before render) and identical across program switches. Staged params via the
  SHM ring are verified delivered to the child (PluginHost drains the ring
  before each block), yet the audio does not change.
- Conclusion: Serum2 in the isolated VST3 path is a **read-only default
  patch**. No host-side control surface (state bytes, programs, params) moves
  it. This matches and extends the 2026-09-08 handoff conclusion — not only is
  preset *loading* by state bytes a dead end, the entire state/param/program
  surface is unresponsive.
- Disposition: probe test deleted (`serum_state_probe_test.cpp` + CMake
  entry). No further Serum2 automation work; any future use requires the
  semi-automated UI capture loop (2026-09-08 handoff, Option A) or nothing.
- B10 master-bus crash verdict stands from earlier in this plan: no defect
  (16/16 in isolation), no code change, re-triage only if the full suite
  reproduces it with a stack trace.

## Results 2026-09-14
- B10: 16/16 MasterBusFx+TrackMixerState pass in isolation (fresh binary).
  Crash is order-dependent or outage collateral — NO master-bus code
  touched (sound-engine rule: no proven defect, no change). Re-run full
  suite after RDP reconnect; triage with stack only if it reproduces.
- Serum probe run 1 (no pump): S0=3076B, tweak staged, S1 identical ->
  exposed probe-design gap (ProxiedParameter::getValue reads parent-local
  cache; child applies only on processBlock SHM flush).
- Serum probe run 2 (+8 driven blocks, +pumps): same S1==S0. Child-side
  apply unconfirmed; render comparison still open.
- Runs 3-4: live track never projects (5s poll) — RDP session disconnected
  (qwinsta: hapbt session Disc), engine init logs "No driver" for default
  AND output-only. Lesson-17 fallback covers zero INPUT devices, not zero
  endpoints. Whole-suite live-graph tests red until reconnect — environmental.
- Probe hardened to SKIP (loud verdict prints) on: no processor / no live
  track / no instance / no automatable params / S1==S0. Tree stays green;
  the honors/ignores EXPECT fires only when S1!=S0 is actually observed.
- Follow-ups when audio returns: (a) full suite re-run; (b) Serum probe to
  completion (add render-compare if state still identical: distinguishes
  proxy-never-delivered [HDAW bug] from state-not-serialized [Serum verdict]);
  (c) rotate/truncate %TEMP%/hdaw_debug.log (176MB/1.2M lines).
