# Psytrance Composition Guide (HDAW MCP)

How to compose a psytrance track with the HDAW engine + MCP from scratch.
Distilled from the two long composition sessions on 2026-08-26/27 (Antinomy
pack track, then the new-pack production runs v3/v4/v5). Every number below
was verified end-to-end in rendered audio, not guessed.

Quick start: the canonical recipes are gtest tests in
`tests/unit/engine/psytrance_composition_stress_test.cpp`:
`FullProductionArrangement` (v3, 138 BPM), `FullProductionV4` (long-form,
140 BPM), `DarkForestV5` (150 BPM, F minor, key-discipline). If you want a
new track, copy the test, change palette/sections/scale, rebuild, run,
and read the render back. The MCP path (step-by-step below) is the same
recipe driven over the wire.

---

## 0. What "psytrance" means here (style canon, verified)

- Tempo 125–150 BPM (we used 138–150). Constant pounding 4-on-floor kick.
- Layers build every 4–8 bars toward a climax; atmospheric intro + true
  breakdown (kick AND bass drop out — a fake breakdown that keeps the
  groove reads as no change in the RMS arc).
- Signature elements, all proven in our renders:
  - **Offbeat rolling bass** on the "and" of every beat (the backbone).
  - **Kick driving a per-beat pumping feel** (sidechain-style LFO duck).
  - Reverb + delay on hats/stabs/leads; 16th arps; flanger/phaser movement.
  - Wide (chorused) pads filling the whole arrangement.
  - Filter/LFO sweeps that carry energy across sections; reverse-cymbal /
    reverse-hat "downlifters" into drops; risers out of the breakdown.
- Length: real tracks are 5–8 min. Our v3 was 2:08 and felt skeletal;
  v4 (3:56) and v5 (3:28) with full sections are the shape to copy.

## 1. The 7-step workflow

1. **Index libraries** — make the sample packs HDAW-registered + analyzed.
2. **Select role-classified samples** — one kick/bass/lead/hat/pad set.
3. **Build the kit** — tracks + sampler slots + natural-pitch rootNotes.
4. **Compose the score** — one clip per role, beats-based, section grammar.
5. **Add production** — per-role internal FX chains + LFOs + automation lanes.
6. **Mix** — per-role faders, canary render → master scale to ~−1 dBFS.
7. **Verify + iterate** — RMS arc per section, band-energy, peak; refine.

---

## 2. Sample pipeline (timbre-lib)

### Index new packs (one-time per pack)

```bash
cd timbre-lib
python3 analyze_psytrance.py   # strides each folder, DSP→CLAP→LLM sidecars,
                               # registers each folder as an HDAW audio library
```

Per-folder variant (list explicit folders / just register):

```bash
python3 analyze.sh --library NAME --path "E:\samples\Some Pack"
python3 register_library.py --path "E:\samples\Some Pack" --name Short-Name
python3 analyze_multi.py      # multi-folder, models loaded once, strided
```

- `analyze.sh --library` writes the registry via script — safe only when NO
  engine is running (an engine restart clobbers externally-written registry
  entries; while a DAW/MCP engine may run, register via MCP `add_library`).
- Sidecars land as `<file>.timbre.json` next to each sample; `scan_library`
  ingests them (async — poll `list_libraries` fileCount/lastScan for done).
- Pack filenames inside `E:\samples` are reliable role hints; analysis CLAP
  tags + DSP descriptors back them up (see `select_psy_samples.py`).

### Select a role-classified palette

```bash
python3 select_psy_samples.py   # → timbre-lib/psy_sample_selection.tsv
```

TSV format (verified): `role<TAB>win_path<TAB>library<TAB>name`, one line per
sample, roles ∈ {kick, bass, lead, hat, pad}. The selection uses the *first*
file per role; `loadSelection(1)` in the test skips the first per role to get
a *different palette* for a second track from the same TSV (v4 did this).

### Theme/darkness sort for variant tracks

Group the TSV by role and sort each role by library darkness rank when you
want a "dark forest" character — v5's order was: TerraTech → SantoGrau →
Hypnoticum → Hipotermic → FLOW36 → Batuhan → Avalon → Ascend. Track identity
came mostly from palette choice + tempo + phrase set, not from FX.

## 3. Kit construction (MCP / commands)

One sampler track per role sample. Verified pattern:

```python
t = await mcp_call("add_track", {"name": f"Psy{role}{i}"})   # → "trackId=N routed=1" (plain text, parse it)
await mcp_call("add_fx", {"trackId": t, "fxType": "sampler"})  # slot 0
await mcp_call("sampler_set_sample", {"trackId": t, "slotIndex": 0,
                                      "filePath": win_path, "rootNote": root})
await mcp_call("set_track", {"trackId": t, "volume": vol})
```

**rootNote (the #1 audible-mix mistake):** set it so the sample plays at its
NATURAL pitch in the register you place it. Verified natural-pitch roots by
role: kick 36 (or 35/41 for different kick samples), bass 36–38, hat 44–46,
pad 52–53, lead/stab 60–65. In v1 the hats were set at 60 → played −18
semitones → mud; first renders came out kick+bass-only. Assign the root per
*sample*, not per role formula, when the sample's key differs.

**Faders (verified starting point):** kick 0.85, bass 0.80, hat 1.00, lead
0.95, pad 0.75. Psytrance sums HOT; these keep the pre-master sum safe.

**Sampler notes:** notes gate at note-duration + release; set long durations
for sustained layers. `set_sampler_param` 0=Attack 1=Decay 2=Sustain
3=Release 4=Transpose. Transposed-up notes get LOUDER and shorter — shave
velocity for high transpositions.

## 4. Score grammar (beats; all verified)

Units: `add_midi_clip` start/length and note start/duration are **beats**.
`export_audio` start/end are **seconds**. Note starts are **clip-local**
(startBeat relative to clip start) — either place clips at beat 0 (what the
production tests do: one clip per role spanning the whole track) or subtract
the clip start from every note start.

One clip per role spanning the whole arrangement; sections are beat ranges:

| Section | Beats (v4 @140) | Contents |
|---|---|---|
| Intro | 0–32 | pads (soft), sparse hat quarters from bar 4, atmos |
| Build | 32–64 | + hats, perc, claps on 2/4, riser |
| Main A | 64–192 | full stack: kick+bass+hats+arp+stabs+pads |
| Mini-break | 192–224 | kick/bass out for 4–8 bars, pads + riser |
| Main B | 224–352 | new phrase, bass +1 octave, extra hat 16ths |
| True breakdown | 352–384 | kick AND bass out; slow reverbed melody over pads |
| Finale | 384–512+ | densest; octave bass, extra 16ths, all layers |

Default pattern variables: totalBeats ≈ 128–160 per 32–40 bars (v1-simple) up
to 512–544 (v3/v4/v5 long-form). Render duration =
`ExportManager::calculateProjectDuration(...)` exactly — never a fixed
window, or you get dead-tail silence in the render.

Per-role pattern templates (all verified in the production tests):

- **Kick:** `{pitch: root(36/F2), start: b, duration: 1.9, velocity: 120}` for
  every beat in the section; skip beats inside mini/true breakdowns.
- **Bass:** offbeat 8ths — `start: b + 0.5` for each beat of the bar, pitch =
  `rootSeq[bar % 8] + octave`, alternate `+ (b % 2)` semitone for the rolling
  feel. `rootSeq` example: `{36,36,43,41,38,38,45,43}` (moves up a 5th/3rd
  per bar, drops back). Octave lifts: `oct = +12` for Main B and finale.
  Velocity ~110, duration 0.4 (short, punchy).
- **Hats:** `{44, b + 0.5}` offbeat 8ths; 16th rolls (`46 at b+0.75/b+1.0/
  b+1.25`) at every 8-bar boundary and through the finale; sparse soft
  quarters (`{44, b}`) in the intro. Velocity ~90, duration 0.2.
- **Lead arp:** 16ths — `start: b + (b%4)*0.25`, pitch from an 8-note
  chord-tone pattern, e.g. `{62,65,69,74,65,69,77,74}`, with the last 16th
  of each beat +12 for the classic psy arp glint. Velocity ~90, dur 0.2.
- **Stabs:** triad chord on beat 2 of each bar (`start: bar*4 + 1.0`),
  in-key voicing, velocity ~96, duration 1.3.
- **Pads:** long notes every bar (`{chordTone, bar*4}` + a second tone
  +0.5), duration 4.0, velocity ~64, whole arrangement.
- **Breakdown melody:** slow long reverbed phrase (e.g. `{69,74,71,76}` at
  +8 beats, dur 3.0, velocity 95) over the pad wash.
- **Transitions:** reverse cymbal/hat samples at 4–8 beat spots before each
  drop; riser samples for the last 4–8 beats into each drop, rising velocity.

### Generative-composition alternative

If hand-scoring isn't wanted, the composition generators work too
(`generate_phrase`, `generate_chord`, `generate_progression`,
`generate_rhythm_pattern`, `generate_arrangement`, `add_instrument_part`,
`auto_gain_to_target`) — verified caveats: `generate_arrangement` parts land
at beat 0 on mapped target trackIds (roles unmapped create new tracks), and
you MUST pass `enableOpenHat:false` + `enableSnare:false` or stray tracks
appear; generated note templates are staccato (0.1–0.7 beat durations) →
through a gating sampler they render quiet, so lengthen via `set_note`.

## 5. Production stack (the difference between a sketch and a psytrance track)

Per-role internal FX chains + LFOs — this is what the "too stripped down"
first renders lacked. Verified full recipe (v3/v4/v5):

| Role | FX chain (slots) | LFOs / automation |
|---|---|---|
| Kick | compressor (slot1: thr −18, ratio 4) → EQ (slot2: freq 3600) | — |
| Bass | EQ (slot1) → compressor (slot2: thr −20, ratio 3) | LFO0: 2 cycles/beat sine → EQ cutoff (targetParam 200), depth 0.28; LFO1: 1/beat pump → Volume (target 1), depth 0.6, phase 180 |
| Hats | reverb (mix 0.75, size 0.30) | — (flanger-rate automation in stress variants) |
| Lead | flanger (rate 0.5, depth 0.6) → compressor → reverb (mix 0.9, size 0.22) | LFO: saw 0.5 → flanger rate (200), depth 0.4 |
| Stabs | reverb (mix 0.85, size 0.42) → delay (feedback 0.19, dry 0.45, wet 0.22) | — |
| Pads | chorus (params 0/1/4 = 1.4/0.65/0.55) → reverb (params 0/2 = 0.95/0.35) | LFO0: slow volume swell (→ Volume, target 1, depth 0.22); LFO1: 1/beat pump (depth 0.4, phase 180) |

**LFO contract (verified):** `add_lfo(track)` then `set_lfo_param` with
`waveform` (0=sin,1=tri,2=saw), `rateSync:1`, `rate` in the units the
samples above use, `depth`, `bipolar:1`, `phaseOffset` (180 = pump feel),
`targetParamID`. Volume = paramID 1; FX params use the automation ID scheme
below. LFOs on the SAME target accumulate (pump + swell on pads sum).

**Automation lanes (verified):** `add_automation_lane(track, name, paramID)` +
`add_automation_point(track, name, beat, value)` +
**`set_automation_enabled(track, lane, true)` — lanes default OFF and
silently do nothing until enabled.** Lane values are NORMALIZED 0..1 (except
Volume-lane raw gain and sampler-Transpose semitones). FX-param lane IDs:
`100 + slotIndex*100 + paramIndex` (e.g. bass EQ slot1 → 200 = cutoff freq;
lead phaser slot1 → 201/202 = depth/CF; flanger slot1 → 200 = rate).
Automation DOES drive internal FX (EQ/phaser/flanger) end-to-end — verified
audible in renders (bass cutoff sweeps per section, pad riser into breaks).

Macro sweep recipe: points every 32 beats, values 0.1→0.7 across the track
for energy growth (bass EQ freq), plus a breakdown riser point-cluster
(0.1 @ 0, 0.12 @ 188, 0.3 @ 192, 0.7 @ 206, 0.35 @ 224).

## 6. Mix + master (all measured)

- **Clipping is PRE-master.** If peaks pin at 1.000 and master gain changes
  don't move them, the sum clips before the master — cut per-track faders /
  velocity, not master.
- **Canary render (verified technique):** render once at master 0.25, read
  the TRUE peak (= canaryPeak / 0.25), then re-render at
  `finalGain = min(0.90 / truePeak, 1.0)` for ~−1 dBFS. A 24-bit WAV pins
  at full scale, so you cannot see headroom at master 1.0.
- **Sanity band targets (v3/v4 measured):** sub≈40–275, bass≈15–89,
  body≈14–21, mid≈7–15, high≈4–5 — bass-weighted but every band present.
  First renders were sub:high ≈ 300–1000:1 (kick+bass only). If bands are
  sub-dominant, it's usually rootNotes/velocities, not the master.
- `set_master_gain` after faders; keep master ≤ ~0.9 pre-canary.

## 7. Verify + iterate loop (do this on EVERY render)

Read the WAV back with numpy: peak (≤0.95, ≥0.35), RMS per section (the arc
must have a real dip at the breakdown: v3.4's "breakdown" was 0.165 vs main
0.170 — a fake break; v3.5 dropped it to 0.039), per-beat pump strength,
and band energy. Inspect waveforms around section boundaries for the kick
hit/transition shape. Render per-role isolation files (mute everything but
one role) when a single voice is suspect — fastest way to hear a bad
sample/rootNote/FX chain. All this work lives in the session's Python cells;
the gtest suite keeps `RoleIsolationDiag` + `FxExplosionDiag` as permanent
diagnostics.

## 8. Export + deliverable housekeeping (verified)

- `export_audio` ignores trackIds (always full project); serialize calls
  ("export already in progress"); use a FRESH filename per render and wait
  for the file size to stabilize; output is 24-bit PCM.
- Render the REAL project duration (see §4), not a fixed window.
- Save `.hdaw` projects next to renders: `.tmp_dnb_theme/<name>.hdaw` +
  `<name>.wav`. Renders from the keep-flag run land there by convention.
- Long renders with automation + save/load accumulation are crash-stress
  scenarios (see handoff §3 'unreproduced abort'); run under
  `%TEMP%\hdaw_capture\run_with_capture.ps1` (procdump) when iterating on
  engine changes, and keep the automation lanes' normalized values in the
  safe 0.01–0.85 range.

## 9. Contract traps that WILL bite again (from the 8/26–27 handoff)

1. **Note caps:** clip caches are large now (8192 ceiling) — but keep parts
   under it; a part past the cap or notes written with ABSOLUTE starts into
   a clip at start>0 silently misplays. Clip-local beats, always.
2. **`sampler_set_sample` re-set** used to kill the live sound (fixed: now
   rebuilds the FX chain and re-loads from the tree — and any re-set still
   needs a rebuild to be live). In HEADLESS MCP with no audio device,
   `sampler_get_state.hasSound` reads false for EVERYTHING — the tree is
   the truth; hasSound is NOT a render predictor.
3. **`duplicate_region` ripple-inserts:** copies [start,end) at end AND
   shifts later content. Extend grooves BEFORE placing later material.
4. **Registry clobber:** an engine restart rewrites
   `%APPDATA%\HDAW\libraries\registry.json` from memory, dropping
   externally-written entries. Register libraries via MCP `add_library`
   while engines may run; scripted registry writes only when idle.
5. **Daemon drops:** "Connection closed" mid-project → `await mcp.reload(
   'hdaw')` then `load_project` from the last save. Save often on long
   builds (`.tmp_dnb_theme/<name>.hdaw` after every section pass).
6. **`add_track` returns plain text** `trackId=N routed=1` — parse it; other
   tools mix "ok" text and JSON; try both.
7. **Key discipline:** derive every pitch (bass roots, arp tones, stab
   triads, pad voicings, breakdown melody, even kick root) from ONE scale's
   degree set — v5's `fMinorDeg(degree, octave)` helper over
   {F,G,Ab,Bb,C,Db,Eb} produced an entirely in-key track with progressions
   like i–VII–VI–VII (A) and VI–VII–i–i (B). Don't hand-type chromatic
   pitches into a long score.

## 10. Where the evidence lives

- **Session logs** (agent): `01a03f69-…` (Antinomy MCP track, first dose of
  every contract trap), `01a04356-…` (new packs + production v3/v4/v5 +
  EQ-gain engine bug found mid-track).
- **Handoff:** `docs/handoffs/2026-08-27-mcp-cluster-compose-session-bugs.md`
  (bugs §1–§5, cheat-sheet §5, full session inventory §6).
- **Recipes:** `tests/unit/engine/psytrance_composition_stress_test.cpp`
  (FullProductionArrangement / FullProductionV4 / DarkForestV5 /
  RoleIsolationDiag / FxExplosionDiag; requires
  `timbre-lib/psy_sample_selection.tsv`).
- **Sample tooling:** `timbre-lib/analyze_psytrance.py`,
  `select_psy_samples.py`, `register_library.py`, `analyze_multi.py`,
  `analyze_targeted.py`, `analyze.sh`.
- **Deliverables:** `.tmp_dnb_theme/` (antinomy_*, psytrance_production_v3/4,
  psytrance_darkforest_v5.wav + .hdaw projects).
