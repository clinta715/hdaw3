# Plan: DarkForestV5 mix fix — band assignment (EQ + compression + filters)

Date: 2026-09-02 · Project: HDAW · Scope: ONE test body, no engine changes.

## Goal
"Dark forest" (TEST(PsytranceComposition, DarkForestV5) in
tests/unit/engine/psytrance_composition_stress_test.cpp, deliverable
.tmp_dnb_theme/psytrance_darkforest_v5.wav) is washed out and the kick is
indiscernible. Fix by assigning each element a frequency band: accentuate the
band each sound belongs in (EQ), remove out-of-band content (SVF filters),
tame the reverb wash. Do NOT just raise track volumes.

## Diagnosis (measured on 2026-09-02 render, main groove beats 176-344)
- Kick transient/bed contrast 35-120 Hz: +5.2 dB (want >= 8.5)
- Mud band 150-700 Hz crest 90/10: 9.9 dB (want >= 13)
- Broadband crest 90/10: 7.9 dB (want >= 10.5)
- Causes: reverb wet 0.30-0.42 / room 0.75-0.95 on hats+lead+stab+pad with NO
  high-pass anywhere -> sustained wash; kick EQ + bass EQ slots exist with
  gain 0 dB (no-ops); kick comp -18 dB/4:1 squashes peaks (no makeup gain);
  bass "BassSweep" lane + LFO automate eq-slot Frequency of a 0 dB peak
  (inaudible no-ops).

## Band map (F minor; kick/bass root F2 = 87.3 Hz; pads/stabs/lead root F3 = 174.6 Hz)
| Role   | Owns                       | Moves |
|--------|----------------------------|-------|
| kick   | 40-120 Hz + 2.5-5 kHz click | eq 82 Hz +3 dB Q1.0; eq 3.6 kHz +3 dB Q1.0; comp -22/2.5 rel 90 |
| bass   | 60-250 Hz body, LP 3.5 kHz  | eq 130 Hz +2 dB Q0.9; filter LP 3500; comp rel 60; lane+LFO repointed to real filter |
| hats   | 3-16 kHz                    | HP 500 before reverb; reverb 0.50 room / 0.20 wet / 0.65 damp |
| lead   | 175 Hz-4 kHz arp            | HP 150 before delay; reverb 0.60/0.16/0.70; delay mix 0.26 fb 0.35 |
| stabs  | 175-1200 Hz                 | HP 140 before reverb; reverb 0.55/0.20/0.70; flanger mix 0.45 |
| pads   | 174-900 Hz + air            | HP 130 (chain end); reverb 0.65/0.24/0.70 |

## Contracts (verified in source)
- FX param defs (src/engine/TrackFXSlot.h getParamDefsForType):
  eq = 0 Freq, 1 Q, 2 Gain(dB) (single peak band); filter = 0 Cutoff,
  1 Mode(0=LP,1=HP,2=BP), 2 Resonance; reverb = 0 Room, 1 Damping, 2 Wet,
  3 Dry; compressor = 0 Thr dB, 1 Ratio, 2 Atk ms, 3 Rel ms.
- setFxSlotParam = REAL def values (clamped). Automation lanes + LFO
  targetParamID = NORMALIZED 0..1 mapped through defs.
- pid >= 100: slot = (pid-100)/100, param = (pid-100)%100. So pid 200 =
  slot1/param0 (bass eq Freq today; pad chorus Rate today), pid 400 =
  slot3/param0 (new bass filter Cutoff).
- addFxSlot inserts at a position and shifts later slot indices -> insert
  AFTER all setFxSlotParam calls that use the old indices on that track, and
  on PAD append at END (Riser lane targets pid 200 = slot1 chorus Rate).

## Edits (exact, all inside DarkForestV5)
KICK:  comp params 0/1 -> -22.0/2.5, add param 3 = 90.0f; existing eq slot2
       (3600) add param1 Q=1.0f param2 gain=3.0f; NEW addFx(kickT,"eq",3):
       62->82 Hz: (3,0,82.0f) (3,1,1.0f) (3,2,3.0f).
BASS:  eq slot1 add (1,0,130.0f) (1,1,0.9f) (1,2,2.0f); comp slot2 add
       param3 = 60.0f; NEW addFx(bassT,"filter",3): (3,0,3500.0f) (3,1,0.0f)
       (3,2,0.7f); LFO0 depth 0.30->0.05, targetParamID 200->400; BassSweep
       lane target 200->400, points rescaled: {64,0.10} {128,0.30}
       {192,0.45} {224,0.15} {288,0.35} {352,0.48} {384,0.22} {448,0.42}
       {512,0.55}.
HAT:   reverb slot1: room 0.75->0.50, wet 0.30->0.20, NEW param1 0.65f; then
       NEW addFx(hatT,"filter",0): (0,0,500.0f) (0,1,1.0f) (0,2,0.7f).
LEAD:  delay slot1: mix 0.32->0.26, fb 0.40->0.35; reverb slot2: 0.9->0.60,
       0.25->0.16, NEW param1 0.70f; after lead comp+pumpLfo lines NEW
       addFx(leadT,"filter",0): (0,0,150.0f) (0,1,1.0f) (0,2,0.7f).
STAB:  flanger slot1 param1 0.55->0.45; reverb slot2: 0.9->0.55, 0.42->0.20,
       NEW param1 0.70f; then NEW addFx(stabT,"filter",0): (0,0,140.0f)
       (0,1,1.0f) (0,2,0.7f).
PAD:   reverb slot2: 0.95->0.65, 0.38->0.24, NEW param1 0.70f; then NEW
       addFx(padT,"filter",3)  // END: Riser targets pid 200 = slot1
       (3,0,130.0f) (3,1,1.0f) (3,2,0.7f).
Do not change notes, mixer state, master/render logic, or any other test.

## Success gates
- G1 build: hdaw_tests builds clean (verify binary timestamp moved - stale-obj
  trap lesson 15).
- G2 test: --gtest_filter=PsytranceComposition.DarkForestV5 PASSES; wav
  re-rendered (size > 1 MB, fresh mtime); log line "V5: truePeak=..." present;
  existing peak asserts hold (canary <= 0.9, final in [0.35, 0.95]).
- G3 smoke: --gtest_filter=InternalFx.EqDefaultGainPassesAudio:
  InternalFx.Filter*:PsytranceComposition.FxBinarySearchKick PASS (FX
  primitives + kick FX path unaffected).
- G4/G5/G6 (orchestrator, from rendered wav): kick contrast >= +8.5 dB;
  mud crest >= 13 dB; broadband crest >= 10.5 dB.

## Pitfalls applied
- PowerShell 5.1: no && separators. Use `powershell.exe -NoProfile
  -Command "..."` single commands or `; if ($?) { }`.
- Lesson 20: before running tests, check for stale HDAW engines/plugin hosts.
- Lesson 15: after build, confirm hdaw_tests.exe mtime > source mtime.
- No engine/source changes outside the one test body.


## Outcome (2026-09-02, after 3 iterations)
Render: .tmp_dnb_theme/psytrance_darkforest_v5.wav (fresh). truePeak 0.992, finalPeak 0.900.
| Metric | Baseline | Final | Gate |
|---|---|---|---|
| Kick contrast 35-120 Hz | +5.17 | **+8.80** | >= +8.5 PASS |
| Mud crest 150-700 Hz | 9.9 | **14.2** | >= 13 PASS |
| Broadband crest 90/10 | 7.9 | **16.6** | >= 10.5 PASS |
| Click/mud band ratio 2-6k/150-700 | -4.95 | **-3.80** | +1.15 dB (share-of-total metric retired: kick sub boost raises total) |
| Smoke gates | - | 5/5 PASSED | PASS |
Iteration history: (1) HP filters + reverb cuts + kick/bass EQ accents +7.84;
(2) bass pump 0.72, 0.3-beat bass gate, stab LP 2200, bass accent 130->170
+8.12; (3) kick 82 Hz +4 dB / 3.6 kHz +3.5 dB Q1.2, hats wet 0.16 damp 0.70
+8.80. Deviation kept: hat/lead/stab HP at chain slot 1 (slot 0 is upstream
of the sampler and audio-dead). Engine finding left for discussion: pids
>= 200 (automation lanes + LFO targets) route to midiFxChain since e917c1f1
(2026-08-08) -> BassSweep/Riser/jungle lanes are inert engine-wide.
