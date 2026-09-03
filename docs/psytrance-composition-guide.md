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

## 0.5 Sound-design canon (2026-09 additions)

Seven principles from the production sessions. They extend §0 and inform
§4/§5 choices.

1. **Random synth FX are welcome.** Glitches, zaps, noise bursts, alien
   blips — throw them in as dedicated ear-candy/FX tracks. Almost any sound
   works if it is (a) filtered hard enough or (b) made rhythmic
   (gated/quantized to the grid). See §4 "FX/blip track" recipe.

2. **Filter discipline — the growly metallic edge.** Synths are heavily
   filtered, usually twice: one filter ON the synth, then a SECOND filter
   pass after it, then a waveshaping distortion that squares off the tops
   of the waveform. Chain: `[instrument] → filter FX → filter FX →
   waveshaper/distortion FX`. In HDAW:
   - **growl_bass** has built-in waveshaping (`ClipType` 0=SoftTanh,
     1=SoftAtan, 2=Hard, 3=Bitcrush) plus `Drive dB` (0–40) and an
     internal filter (LP/BP, cutoff/res/env-amount). This covers the
     "instrument filter + distortion" in one slot.
   - Add 1–2 external `filter` FX slots after for the second (and third)
     filter pass. Automate the last filter's cutoff for movement.
   - Alternative: `psy_fm` with high feedback (param_306 = OP6Feedback)
     produces waveshaping naturally.

3. **Anything can be ear candy if it's rhythmic or filtered.** A wrong-
   sounding beep becomes psytrance the moment it's gated to 16ths or LP-
   filtered into the background. Don't audition sounds for "beauty" —
   audition them for rhythmic function.

4. **Kick + bass are the root; everything else is garnish.** Nearly
   anything can go ON TOP of the kick/bass skeleton — mix decisions protect
   the low end first (sidechain pump, sub headroom), and every added layer
   is mixed UNDER the backbone, not with it.

5. **Leads are arps.** Nearly all psytrance leads are arpeggios built
   from one scale (raga-like modes: Harmonic Minor index 7,
   Phrygian index 3, Dorian index 2 — any with exotic intervals).
   Compose leads PROGRAMMATICALLY: pick a scale-degree pattern
   (e.g. 0-1-3-2-0-4-3-2 cycling), map to `scaleNote`, repeat with
   variations — don't hand-type melodies. The `psyarp` internal synth
   (§5c) is purpose-built for this.

6. **Non-arp leads are monotonic or simple intervals.** When a lead
   isn't an arp, it's usually 1–3 pitches (root + b2 or root + 5th) with
   the interest coming from timbre/filter/timing, not pitch. Simple-
   interval stabs over the bass root are the idiom.

7. **Long-track variation = tweak the repetition.** The 16th-note lead
   repeating for 8 bars is correct — variation comes from: L/R phasing
   (auto-pan LFOs, targetParamID 2), LFO rate/depth changes between
   sections, cutoff sweeps (automation lanes on filter params), feedback/
   resonance changes, and per-section FX-state changes. Repetition of
   NOTES + evolution of TIMBRE is the genre's core long-form device.

---

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

Sidecar records now carry `key` and `bpm` (filename tag first — `Am`,
`F#m`, `C min`, `128 BPM` — then Krumhansl chroma / onset-tempo estimation).
FileLibraryManager ingests them on scan: sidecar key overrides the native
chroma guess; sidecar bpm fills in when the entry has none. `search_library`
key/BPM filters therefore match analyzed pads/loops directly.

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

### Cluster-driven palette flow (alternative to TSV-first)

Instead of hand-picking from a TSV, use `cluster_library` to auto-classify
samples by timbre role:

1. `cluster_library {libraryIds:["antinomy_vol2"], k:8, method:"hybrid"}`
   → returns clusters with semantic labels (e.g. c1="dark" = low-end,
   c2="bright" = clap/riser, c4="stab", c7="pad", c8="soft" = tonal reverse).
2. Build the kit straight from member paths — cluster labels are
   semantically useful for role assignment.
3. Sample-key hints in filenames (`…_C.wav`, `…_F#_9.wav`) feed
   natural-pitch `rootNote` values.
4. Save the preset with `saveAs` for reuse: `cp_<hash>`.

Verified in the 2026-08-30 F-minor session: Antinomy Vol.2 k=8 hybrid
cleanly separated role clusters, with the 12-key bass multisamples
shunted to `unassigned` as redundant.

## 3. Kit construction (MCP / commands)

One sampler track per role sample. Verified pattern:

```python
t = await mcp_call("add_track", {"name": f"Psy{role}{i}"})   # → "trackId=N routed=1" (plain text, parse it)
await mcp_call("add_fx", {"trackId": t, "fxType": "sampler"})  # slot 0
await mcp_call("sampler_set_sample", {"trackId": t, "slotIndex": 0,
                                      "filePath": win_path, "rootNote": root})
await mcp_call("set_track", {"trackId": t, "volume": vol})
```

### MCP tool-name contract map (verified 2026-08-30)

The key tools and their shapes, distilled from the composition sessions:

| Tool | Key params | Returns | Notes |
| ------ | ----------- | --------- | ------- |
| `add_midi_clip` | `{trackId, start, length, name}` | `{"clipId":N}` JSON | Clips are beats. |
| `add_notes` | `{clipId, notes[{start,duration,pitch,velocity}], relative}` | full `noteIds` array | Note starts are **clip-local** by default; `relative:false` = timeline-absolute. |
| `sampler_set_sample` | `{trackId, slotIndex, filePath, rootNote}` | `"ok"` | Must be slot 0 (add_fx first). All sampler slots on a track share MIDI. |
| `set_internal_fx_param` | `{trackId, slotIndex, paramIndex, value}` | `"ok"` | REAL units (Hz, dB, ratio). `list_fx_params` reveals indices/min/max. |
| `add_lfo` | `{trackId}` | `"lfoIndex"` | Then `set_lfo_param` for waveform/rate/depth/target. |
| `set_lfo_param` | `{trackId, lfoIndex, param, value}` | `"ok"` | targetParamID: 1=Volume, 2=Pan, 100+=FX, 300+=FM (300–308). |
| `add_automation_lane` | `{trackId, laneName, paramID}` | `"ok"` | **Disabled by default** — must `set_automation_enabled`. |
| `set_automation_points` | `{trackId, lane, points[{time,value}], mode:"replace"}` | `"ok"` | Key is `time` (beats). |
| `mix_report` | `{filePath, bpm, sections[{name,start,end}]}` | peak/RMS/bands/pumpDepth | Band cutoffs: sub<40, bass<300, body<2000, high>6000. |
| `psy_fm_load_preset` | `{trackId, slotIndex, preset}` | `"loaded preset: ..."` | preset ∈ {growlBass, acidLead, metallicPluck, riser}. |
| `psy_fm_get_analysis` | `{trackId, slotIndex}` | `{activeVoices, opEgLevels}` | Live audio-thread data (lock-free atomics). |
| `psy_fm_set_mod_route` | `{trackId, slotIndex, source, dest, depth}` | `"ok"` | source ∈ {ratioSweepLFO, feedbackLFO, modWheel, velocity, barClock}; dest ∈ {op1Ratio..op6Ratio, op6Feedback}. |
| `psy_fm_clear_mod_matrix` | `{trackId, slotIndex}` | `"ok"` | Removes all modulation routes. |
| `slice_clip_at_playhead` | `{clipId}` | `"sliced clip N at playhead"` | Works for audio and MIDI clips. |
| `slice_clip_at_times` | `{clipId, times[beats]}` | `"sliced clip N at M positions"` | Times are timeline-absolute beats. |
| `slice_clip_at_transients` | `{clipId}` | `"sliced clip N at transients"` | Audio clips only. |
| `set_clip_stretch_mode` | `{clipId, mode}` | `"ok"` | mode: 0=Off, 1=TempoMatch, 2=ManualRatio. |
| `set_clip_stretch_ratio` | `{clipId, ratio}` | `"ok"` | ratio: 0.25–4.0. |
| `tempo_match_clip` | `{clipId}` | `"tempo-matched clip N"` | Requires sourceBpm set. |
| `fit_clip_to_loop` | `{clipId}` | `"fit clip N to loop"` | Stretches clip to fill loop region. |

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
| --- | --- | --- |
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

### One-call alternative: `generate_psytrance`

Instead of hand-computing 2,600+ notes via the per-role templates below,
use the `generate_psytrance` MCP tool (one call writes the complete score):

```python
result = await mcp_call("generate_psytrance", {
    "paletteTrackIds": {"kick": 0, "bass": 1, "hat": 2, "arp": 3,
                        "stab": 4, "pad": 5, "riser": 6, "down": 7},
    "sections": [
        {"name": "intro", "start": 0, "end": 32},
        {"name": "build", "start": 32, "end": 64},
        {"name": "mainA", "start": 64, "end": 192},
        {"name": "mini", "start": 192, "end": 224},
        {"name": "mainB", "start": 224, "end": 352},
        {"name": "breakdown", "start": 352, "end": 384},
        {"name": "finale", "start": 384, "end": 512}
    ],
    "keyRoot": 5, "scaleMode": 1, "density": 0.7, "seed": 42
})
# result = {clips:[{role,trackId,clipId,noteCount}], skipped, totalBeats, notesTotal}
```

The tool writes one clip per mapped role at beat 0 spanning the arrangement.
Unmapped roles are reported in `skipped`. Returns a compact summary (no
note payload — use `get_clip` for detail). NOTES ONLY — kit (sample loading)
and production (FX/LFO/automation) stay separate MCP steps per palette.

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
- **Pads:** gated chord beds — triads or 7ths on 8th/16th grids,
  with one home harmony and at most one secondary harmony center.
  Use `pad` as the thick wash, not a single long note.
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

### §4B Incremental Markov mode — build 2 bars at a time (`generate_psytrance_markov`)

Where `generate_psytrance` preplans the WHOLE song from a section table, the
Markov mode grows the arrangement incrementally: a pool of role elements
(kick, bass, hat, snare, rim, arp, stab, pad, clap) starts minimal and the
track evolves 2 bars at a time under a seeded Markov chain. Pure deterministic engine
(`PsytranceMarkovGenerator`, no engine dependency), wired end-to-end:
MCP tool `generate_psytrance_markov` → RPC `generatePsytranceMarkov` →
`AudioEngineCommands::generatePsytranceMarkov` (same clip-writing contract as
the legacy path: one clip per produced role at beat 0 spanning
`totalBars*4` beats, notes clip-local, ONE undo unit).

**Pool-of-elements philosophy.** The arrangement is not a fixed schedule; it
is a pool of layers that enter, hold, vary, and leave. Global active-layer
count stays within `[minTracks, maxTracks]` (default 2..6, ceiling 9).
Percussive roles `{kick, hat, clap, snare, rim}` additionally stay within
`[minPercTracks, maxPercTracks]` (default 1..3, ceiling 5 — the perc pool is
now five voices) *inside* the global bounds — at perc-max a percussive Add is
suppressed (a non-perc layer is picked instead), at perc-min a percussive
Remove is suppressed. If no target is valid under both limit sets the chain
falls back to Keep/Swap/FilterSweep. This is what makes build-ups and
breakdowns EMERGE instead of being scheduled.

**Three-group element ontology (P0).** Roles partition into three groups,
encoded as predicates in `PsytranceMarkovGenerator` (`isFloorRole`,
`isCoreRole`, `isPercRole`, `isFxRole` — bass is intentionally BOTH
floor-protected AND core-group):

| Group | Roles | Meaning | Transitions |
| ----- | ----- | ------- | ----------- |
| CORE | arp, stab, pad (+ bass) | persistent tonal identity — varied by synth tweaks (ArpVariant, NoteLengthVariant, SwapPattern), not constant re-rolls | volume fade-in **4 bars** |
| PERC | kick, hat, clap, snare, rim | coordinated percussive THEMES held >= 32 bars between rotations (snare pitch 38, rim 37 — fixed-pitch voices; hat 44, clap 42) | volume fade-in **2 bars** (hat/snare/rim/clap only) |
| FX | riser, down | composed texture accents (FxHit / KeyChange) | no fades |
| floor (CORE subset) | kick, bass | the protected foundation — breakdown-only removal | **hard-edged: NO fades, in or out** |

**Volume fades are engine-written; filterCutoff stays advisory.** Every
non-floor AddLayer/RemoveLayer/evict emits `volume` automation points that
`generatePsytranceMarkov` writes FOR REAL onto the target track's Volume lane
(paramID 1) inside the same undo unit — fade rules:

| Event | Points (beats) | Length |
| ----- | -------------- | ------ |
| AddLayer, bar > 0, non-floor | `0.0` at `bar*4` → `1.0` at `bar*4 + len*4` | CORE len 4 bars, PERC len 2 bars |
| RemoveLayer / evict-on-max, bar > 0, non-floor | `1.0` at `(bar-2)*4` → `0.0` at `bar*4` | 2 bars (start clamped >= beat 0) |
| bar-0 initial activations, ALL floor adds/removes | — none — | hard-edged |

The lane write reuses the `add_automation_lane`/`add_automation_point`
command path (paramID 1, points stored in seconds, appended — the runtime
cache sorts). A disabled (fader-authoritative) Volume lane is RE-ENABLED for
generated fades — a disabled lane would swallow the points silently (same
enable-on-write contract as `automation_preset`); the factory hold
placeholders (0 s/16 s) are dropped when untouched so they cannot ramp a
fade-out back to unity. `automationsSkipped` in the result counts volume
fades that could NOT be written (unmapped role, out-of-range track, or
Volume-lane conflict). The `filterCutoff` points (FilterSweep) remain
ADVISORY data — apply them yourself with `set_automation_points` (the target
paramID depends on the track's FX chain).

**Percussive THEMES rotate as a unit (P1).** The percussive groove is not a
bag of independent pattern variables — it is a THEME: one coordinated set of
16-step velocity grids for hat/snare/rim plus the kick broken/straight flag.
Theme 0 is the canonical opener: offbeat-8th hats with the beat-1 offbeat
slightly accented (98 vs 92, baked into the grid), silent snare/rim, straight
kick. The theme SET (T seeded 2..3) is drawn ONCE at generation start, at a
fixed RNG draw position before the window loop: each non-canonical theme
seeds `RhythmPatternGenerator::Params` per voice (euclidean, grid=16,
bars=1 — the generator itself stays pure/no-RNG; only its PARAMS come from
the mt19937: pulseA hits, pulseB hits 0..3, rotations 0..15, velocities).
Snare themes lean ghost/soft (velocities 45..80, 2..5 hits, optional seeded
2/4 backbeat accent at 90 on steps 4/12), rim themes stay sparse (1..4
hits), hat themes run denser (4..12 hits); the kick flag draws ~35% broken.
All voices of a theme rotate TOGETHER — that is the point: the groove evolves
as one coordinated statement, never as drifting soloists.

**Rotation rules.** `themeAge` counts bars since the last rotation and
resets on rotation ONLY. `RhythmVariant` rotates to the next theme (ALL
grids + the kick flag move together; `targetRole` is `"theme"`) and is viable
only once `themeAge >= 32` (and at least one theme voice — hat/snare/rim/
kick — is audible, so the hold clock is never spent on a no-op). A theme may
leave a voice silent — theme 0 carries no snare/rim — but an ACTIVE voice
never goes silent: it falls back to its canonical filler grid (snare: 2/4
backbeat, rim: sparse 16th pushes into beats 3/1, both at velocity 100 — a
level no seeded theme grid uses, so the two are never confusable) until the
next rotation brings a theme that has that voice. `Breakbeat`
toggles the CURRENT theme's kick flag (the flip persists per theme, so
rotating back later keeps the flipped feel); it is viable only once >= 32
bars have passed since the last kick-pattern structure event — a Breakbeat
OR a rotation (a rotation may swap the kick flag, so it resets the kick
clock too). Expect a groove to establish for >= 32 bars (eight 4-bar
phrases) before it mutates. Because theme grids ARE the velocity carrier
(theme 0 bakes its beat-1 accent into the grid), the old per-window hat
accent jitter (±6) and the density-gated window-end flourish were retired in
P1 — a bar's grid signature now only changes when the theme rotates, which
keeps unit rotation audible and verifiable.

**Two-tier model.** A slow section-energy state
(`sparse | build | peak | breakdown`; see the vague-timing note below —
`sectionCycleBars` (default 32) is the base of a jittered schedule, not a
fixed grid) biases the fast per-window weights: build boosts AddLayer and
suppresses RemoveLayer; peak boosts Keep and micro-variation; breakdown
forces subtractive moves until `minTracks`, then re-builds; sparse is
add-heavy with removal off. The fast tier is the 2-bar Markov operating
inside the eligibility the slow tier grants.

**Cadence table (nested variation clocks).** Everything moving every 2 bars
sounds busy — most elements hold steady while 1–2 move:

| Clock | Actions |
| ----- | ------- |
| 2-bar (any window) | Keep, FilterSweep, NoteLengthVariant, PadVariant, RhythmVariant, ArpVariant, FxHit, Breakbeat, velocity/accent micro-shifts |
| 4-bar boundary | AddLayer / RemoveLayer for melodic roles (bass, arp, stab, pad) |
| 8-bar hold | bass remove/swap and kick remove wait until the layer ran >= 8 bars |
| 32-bar pattern hold | perc THEME rotations (RhythmVariant) wait until the theme held >= 32 bars; Breakbeat (and rotations, as kick-affecting) wait >= 32 bars on the kick clock |
| 16–32 bar | KeyChange (periodic, `everyBars` default 32) + section-energy re-roll |

**Floor canon (kick + bass protection).** Genre convention: the kick/bass
floor only drops out as a tension device — it never leaks away during normal
sections. The engine gates both roles: `RemoveLayer` (and evict-on-max) may
only target kick or bass while the section state is `breakdown`, where the
age-weighted pick additionally favors the floor (×8 weight) so the breakdown
actually strips the foundation. At the drop — the window where the section
transitions into `build` — the floor's return is preferred (kick first, then
bass; seeded 75%, bias never force). The 8-bar min-hold and the 4-bar melodic
cadence (bass) still apply on top. With the section tier off
(`sectionCycleBars: 0`) the state can never be `breakdown`, so the floor is
simply never removed. Broken-kick flips (`Breakbeat`) remain normal
2-bar-clock variation — they change the pattern, not the presence.

**Slow-tier timing is deliberately vague.** `sectionCycleBars` is a gravity
well, not a grid: each section draws its length seeded ({0.5×, 0.75×, 1×,
1.25×, 1.5×} × base, even-rounded), biased by character — sparse/breakdown
run short, peaks sustain. A state may renew itself once inside the base
cycle; past that it is force-advanced (sparse→build→peak→breakdown), so
buildups/breakdowns drift in time but always arrive. A section change is
never silent — the transition window always carries a structural/FX action
(never Keep) — and a staleness ramp pushes swap/remove the longer nothing
structural happens, so no element sits unchanged for too long.

**Action table.**

| Action | Effect |
| ------ | ------ |
| Keep | nothing structural (the hypnotic default) |
| AddLayer | activate an inactive mapped layer (age-0 start); at maxTracks it evicts the oldest-weighted layer (swap) |
| RemoveLayer | deactivate a layer — target weighted by age (`1 + age*0.5`): longest-running most likely replaced (kick/bass are floor-protected: breakdown-only, ×8 floor weight there) |
| SwapPattern | A/B progression + bass-octave toggle; target from the lead family (arp/stab/pad/bass) age-weighted |
| FxHit | riser roll (8× crescendo 8ths) or downlifter into the window |
| Breakbeat | toggles the CURRENT theme's kick flag (4-on-floor / broken, beat 2 dropped); only after >= 32 bars since the last kick-pattern event (Breakbeat OR rotation) |
| FilterSweep | emits an automation point `{role, filterCutoff, startBeat, value, durationBeats}` targeting the OLDEST pitched active layer |
| RhythmVariant | rotates the percussive THEME one step — hat/snare/rim grids + kick flag move TOGETHER (targetRole `"theme"`); only after the theme held >= 32 bars |
| ArpVariant | arp direction {up, down, updown, random-walk}, degree rotation, or +12 octave lift for the window |
| NoteLengthVariant | gate-length multiplier from {0.5, 0.75, 1.0} for bass/arp/stab (staccato..full) |
| PadVariant | toggles pad voicing (triad/7th), pulse grid (8th/16th), and home/secondary harmony while staying in-key |
| KeyChange | PERIODIC: every `everyBars` (>= 8), key shifts +1/+2 scale degrees (or `keyShiftDegrees`) for the remainder; direction seeded ONCE at start; all later notes stay in the NEW scale |

**Age-based replacement.** Every active layer accumulates running bars (+2
per window; reset on re-add). RemoveLayer / SwapPattern / evict-on-max pick
their target with weight `1 + age*0.5`, so the longest-running layer is
replaced first — the mix keeps circulating instead of fossilizing. Ages are
recorded per step in `steps[]` (parallel `activeRoles`/`ages` arrays).

**Determinism.** Same seed + params → byte-identical score. The full 64-bit
seed feeds the mt19937 via `seed_seq`; every stochastic choice (section
state, action, target, accent, density extras) consumes the RNG in a FIXED
window order: section re-roll → (KeyChange short-circuit) → action draw →
target draws → accent draw → note extras.

**Accent shaping lives in the theme grids.** Since P1, hat/snare/rim
velocities come straight from the current theme's 16-step grid — theme 0
bakes its beat-1 offbeat accent (98) into the grid, and euclidean-derived
themes carry their own velocity contour. The per-window ±6 accent jitter of
P0 was retired so a bar's grid signature changes ONLY on rotation (see the
rotation rules above).

**Example params JSON.**

```json
{
  "paletteTrackIds": { "kick": 0, "bass": 1, "hat": 2, "arp": 3,
                        "stab": 4, "pad": 5, "riser": 6, "down": 7, "clap": 8,
                        "snare": 9, "rim": 10 },
  "totalBars": 64, "keyRoot": 5, "scaleMode": 1,
  "density": 0.7, "seed": 42,
  "minTracks": 2, "maxTracks": 6,
  "minPercTracks": 1, "maxPercTracks": 3,
  "everyBars": 32, "sectionCycleBars": 32, "keyShiftDegrees": 0,
  "progressionA": [0, 5, 3, 4], "progressionB": [3, 4, 0, 0]
}
```

Returns `{clips, skipped, totalBeats, notesTotal, notesSkipped, stepsCount,
stepsLast, automationsCount, automationsSkipped}` — steps are summarized
(count + last entries) to keep the tool output under ~4KB; the RPC surface
returns the full `steps[]` (barStart, action, targetRole, activeRoles, ages,
keyRoot, section) and `automations[]` for verification. `volume` entries in
`automations[]` are already written to real lanes by the tool (see the fade
rules above); `automationsSkipped` reports fade entries that could not be
written. `filterCutoff` entries remain advisory — drive them with
`set_automation_points`.

**Genre-concurrency tuning guidance.** Psytrance convention: percussion
2..8 simultaneous elements (this pool caps at 5 percussive roles —
`maxPercTracks` is validated <= 5), bass EXACTLY one layer (never doubled —
the engine models it as a single pool element that swaps pattern/octave
instead of duplicating), leads 1..4 (the default `maxTracks: 6` with a
single bass leaves exactly that headroom; the hard ceiling is 9). Start with
defaults; for a darker minimal roll use `minTracks: 2, maxTracks: 4,
density: 0.4`.

**Roadmap (automation-point family).** The FilterSweep filterCutoff output
(volume fades are already engine-written — see the ontology section above)
is the template for later actions in the same family: sidechain pump depth,
FX send builds (delay/reverb over a phrase), stereo width/pan drift, macro
mix HP-sweep during builds, reverb tail drift. These map onto `automations[]`
entries with different `param` values — no new wiring needed when they land.

## 5. Production stack (the difference between a sketch and a psytrance track)

Per-role internal FX chains + LFOs — this is what the "too stripped down"
first renders lacked. Verified full recipe (v3/v4/v5):

| Role | FX chain (slots) | LFOs / automation |
| --- | --- | --- |
| Kick | compressor (slot1: thr −18, ratio 4) → EQ (slot2: freq 3600) | — |
| Bass | EQ (slot1) → compressor (slot2: thr −20, ratio 3) | LFO0: 2 cycles/beat sine → EQ cutoff (targetParam 200), depth 0.28; LFO1: 1/beat pump → Volume (target 1), depth 0.6, phase 180 |
| Hats | reverb (mix 0.75, size 0.30) | — (flanger-rate automation in stress variants) |
| Lead | flanger (rate 0.5, depth 0.6) → compressor → reverb (mix 0.9, size 0.22) | LFO: saw 0.5 → flanger rate (200), depth 0.4 |
| Stabs | reverb (mix 0.85, size 0.42) → delay (feedback 0.19, dry 0.45, wet 0.22) | — |
| Pads | chorus (params 0/1/4 = 1.4/0.65/0.55) → reverb (params 0/2 = 0.95/0.35) | LFO0: slow volume swell (→ Volume, target 1, depth 0.22); LFO1: 1/beat pump (depth 0.4, phase 180); pad generator now prefers full triad/7th voicings and can pulse on 8th- or 16th-note grid steps when gated |

**LFO contract (verified):** `add_lfo(track)` then `set_lfo_param` with
`waveform` (0=sin,1=tri,2=saw), `rateSync:1`, `rate` in the units the
samples above use, `depth`, `bipolar:1`, `phaseOffset` (180 = pump feel),
`targetParamID`. Volume = paramID 1; FX params use the automation ID scheme
below. LFOs on the SAME target accumulate (pump + swell on pads sum).

**Automation lanes (verified):** `add_automation_lane(track, name, paramID)` +
`add_automation_point(track, name, beat, value)` +
**`set_automation_enabled(track, lane, true)` — lanes default OFF and
silently do nothing until enabled.** (Exception: the Volume fades written by
`generate_psytrance_markov` enable the target Volume lane themselves — and
`automation_preset` does the same for the lane it writes.) Lane values are
NORMALIZED 0..1 (except Volume-lane raw gain and sampler-Transpose semitones).
FX-param lane IDs:
`100 + slotIndex*100 + paramIndex` (e.g. bass EQ slot1 → 200 = cutoff freq;
lead phaser slot1 → 201/202 = depth/CF; flanger slot1 → 200 = rate).
Automation DOES drive internal FX (EQ/phaser/flanger) end-to-end — verified
audible in renders (bass cutoff sweeps per section, pad riser into breaks).

Macro sweep recipe: points every 32 beats, values 0.1→0.7 across the track
for energy growth (bass EQ freq), plus a breakdown riser point-cluster
(0.1 @ 0, 0.12 @ 188, 0.3 @ 192, 0.7 @ 206, 0.35 @ 224).

**Drive recipe (saturator, verified):** chain order matters - shape the tone
at the source first, saturate second, level last:
growl clip (ClipType/Drive) -> saturator (SoftTanh, ~18 dB Drive, Mix 1) ->
compressor (4:1). The growl's own waveshaper (growl_bass param 4 ClipType /
param 5 Drive dB, or `psy_fm` growlBass preset) provides the coarse grit;
the saturator adds the final "teeth" without third-party plugins.

```python
t = await mcp_call("add_track", {"name": "Growl Drive"})
await mcp_call("add_fx", {"trackId": t, "fxType": "growl_bass"})    # slot 0
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 0,
                                         "paramIndex": 4, "value": 2})   # ClipType = Hard
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 0,
                                         "paramIndex": 5, "value": 30})  # Drive 30 dB
await mcp_call("add_fx", {"trackId": t, "fxType": "saturator"})     # slot 1
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 1,
                                         "paramIndex": 1, "value": 0})   # Type = SoftTanh
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 1,
                                         "paramIndex": 0, "value": 18})  # Drive 18 dB
await mcp_call("set_internal_fx_param", {"trackId": t, "slotIndex": 1,
                                         "paramIndex": 3, "value": 1})   # Mix 1 (full wet blend)
await mcp_call("add_fx", {"trackId": t, "fxType": "compressor"})     # slot 2, ratio 4:1
```

Saturator params (`set_internal_fx_param`, REAL units: Drive dB 0-40,
Type 0-3 (0=SoftTanh, 1=SoftAtan, 2=Hard, 3=Bitcrush), Asymmetry -1..1,
Mix 0-1, Output dB -24..24 (trims the WET path only), Bits 2-16 (Bitcrush
only); Mix=0 is a bit-identical bypass). The 2x oversampler adds a
4-sample latency that is reported by the slot and summed into track PDC
automatically - no manual compensation.

### Preset toolkit (factory chains + preset tools)

HDAW ships 8 built-in factory chains (internal FX only, seeded to
`_factory/*.json` on first run, edits on disk survive upgrades, never
deletable). `list_fx_chains` returns them with `source:"factory"` and ids
`_factory/<File_Name>.json`; user chains carry `source:"user"`.

| Chain | Role | Slots (key params) |
| --- | --- | --- |
| `Kick Punch` | kick | saturator 14 dB SoftTanh mix 0.6 → eq 55 Hz −3 dB (sub) → eq 4 kHz +3 dB (click) |
| `Bass Glue` | bass | eq 120 Hz +1.5 dB → compressor −18 dB 3:1 → saturator 8 dB gentle |
| `Hat Air` | hats | eq 9 kHz +3 dB → reverb size 0.25 damp 0.3 wet 0.30 (short/bright) |
| `Pad Shimmer` | pads | chorus 0.6 Hz depth 0.45 → reverb size 0.92 wet 0.42 (large/lush) |
| `Acid Lead` | lead/arp | filter LP 1.2 kHz res 4.5 → delay SyncToTempo dotted-1/8 fb 0.45 |
| `Arp Width` | arps | chorus subtle → delay SyncToTempo 1/16 fb 0.30 |
| `Stab Snip` | stabs | eq 1.8 kHz Q 3.5 −2.5 dB (narrow mids) → phaser subtle |
| `Riser Sweep` | risers | filter LP 400 Hz res 6 (sweep it with an automation lane) → reverb size 0.95 wet 0.5 |

| Tool | Notes |
| --- | --- |
| `list_fx_chains` / `load_fx_chain` | ids may be `_factory/<File_Name>.json`; name resolution covers factory presets too |
| `save_fx_chain` | saves to the user dir — save tweaked variants as the project's own palette |
| `delete_fx_chain` | user presets only; factory ids are refused and the file stays |
| `list_plugin_presets` / `search_plugin_presets` / `load_plugin_preset` | host-enumerable plugin programs |
| `load_plugin_preset_file` | load .fxp/.syx from disk |
| `automation_preset` | lane shapes: `pump`, `macro`, `openClose`, `riser`, `sine`, `square` |
| `fm_synth_load_preset` / `fm_synth_import_sysex` | DX7 voice bank load / SysEx import |
| `list_cluster_presets` / `get_cluster_preset` | saved `cluster_library` presets |

Workflow: compose (markov/phrases) → `load_fx_chain` per role from the
factory roster above → tweak individual knobs (`set_fx_param` normalized /
`set_internal_fx_param` real units) → `save_fx_chain` the variant.

## 5a. Reusable FX chain preset: Jordan cave-dub

For a Jordan cave-dub voice, build `sampler -> filter -> delay`, then save the
three slots as **Dusty Skank**. Use `list_fx_params` before
`set_internal_fx_param` to choose the filter cutoff/resonance and delay time,
feedback, and mix in their real-unit ranges.

```python
track_id = await mcp_call("add_track", {"name": "Jordan Cave Dub"})
await mcp_call("add_fx", {"trackId": track_id, "fxType": "sampler"})
await mcp_call("sampler_set_sample", {"trackId": track_id, "slotIndex": 0,
                                      "filePath": win_path, "rootNote": root})
await mcp_call("add_fx", {"trackId": track_id, "fxType": "filter"})  # slot 1
await mcp_call("add_fx", {"trackId": track_id, "fxType": "delay"})   # slot 2

# Track LFOs are separate from FX-chain presets. Configure each property with
# one set_lfo_param call; filter slot 1 param 0 has targetParamID 200.
lfo = await mcp_call("add_lfo", {"trackId": track_id})
lfo_index = lfo["lfoIndex"]
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "waveform", "value": 0})
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "rateSync", "value": 1})
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "rate", "value": 0.25})
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "depth", "value": 0.35})
await mcp_call("set_lfo_param", {"trackId": track_id, "lfoIndex": lfo_index,
                                 "param": "targetParamID", "value": 200})

saved = await mcp_call("save_fx_chain", {"trackId": track_id,
                                          "name": "Dusty Skank"})
presets = await mcp_call("list_fx_chains", {})
await mcp_call("load_fx_chain", {"trackId": another_track_id,
                                  "id": saved["id"]})
# Recreate the LFO on another_track_id with add_lfo + set_lfo_param; loading
# the preset replaces its FX slots but does not copy track modulation.
# Delete only when the reusable preset is no longer wanted:
await mcp_call("delete_fx_chain", {"id": saved["id"]})
```

## 5b. FM synthesis for psytrance (internal instrument)

HDAW has a psytrance-focused FM synthesizer (`ActiveType::PsyFm`) alongside the
classic DX7 FM synth (`ActiveType::FmSynth`). The PsyFm engine is designed
specifically for psytrance timbres: growl basses, acid leads, metallic plucks,
and risers.

### Quick start: FM growl bass

```
# 1. Create a MIDI track and add the PsyFm FX slot
trackId = await mcp("add_track", {"name": "FM Growl"})
await mcp("add_fx", {"trackId": trackId, "fxType": "psy_fm"})

# 2. Load a preset routing (sets algorithm, mod matrix, default params)
await mcp("psy_fm_load_preset", {"trackId": trackId, "slotIndex": 0, "preset": "growlBass"})

# 3. Add a MIDI clip with notes
clipId = await mcp("add_midi_clip", {"trackId": trackId, "start": 0, "length": 32})
await mcp("add_notes", {"clipId": clipId, "notes": [
    {"start": 0.5, "duration": 0.4, "pitch": 36, "velocity": 110},
    {"start": 1.5, "duration": 0.4, "pitch": 36, "velocity": 110}
]})

# 4. Add an LFO to modulate the feedback (targetParamID 306 = OP6Feedback)
await mcp("add_lfo", {"trackId": trackId})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "waveform": 0, "rateSync": true, "rate": 1, "depth": 0.4,
    "bipolar": false, "targetParamID": 306})
```

### Available presets

| Preset | Algorithm | Character | Best for |
| -------- | ----------- | ----------- | ---------- |
| `growlBass` | op6→op5→op1 | Feedback-modulated growl, settling envelope | Offbeat rolling bass, acid bass |
| `acidLead` | op6→op1 | High feedback, near self-oscillation | Screaming leads, filter-sweep-style performance |
| `metallicPluck` | op4→op2→op1 | Non-integer ratios, fast transient | Metallic stabs, alien plucks, percussive FM |
| `riser` | op5→op3→op1 | Ratio-sweep LFO, nested modulation | Risers, FX sweeps, tension builders |

### Track-level modulation targets

The track's `ModulationManager` routes LFOs to FM destinations via `targetParamID`:

| targetParamID | Destination | Effect |
| --------------- | ------------- | -------- |
| 300 | OP1 Ratio | Modulates carrier ratio (pitch/timbre shift) |
| 301 | OP2 Ratio | Modulates modulator 2 ratio |
| 302 | OP3 Ratio | Modulates modulator 3 ratio |
| 303 | OP4 Ratio | Modulates modulator 4 ratio |
| 304 | OP5 Ratio | Modulates modulator 5 ratio |
| 305 | OP6 Ratio | Modulates modulator 6 ratio |
| 306 | OP6 Feedback | Modulates feedback amount (aggression/noise) |
| 307 | Output Level | Modulates master output level |
| 308 | Ratio Sweep Rate | Modulates the internal ratio-sweep LFO rate (nested) |

### Operator envelopes (set_internal_fx_param indices)

Each of the 6 operators has its own ADSR envelope:

| Indices | Operator | ADSR |
| --------- | ---------- | ------ |
| 7–10 | OP1 | Attack, Decay, Sustain, Release |
| 11–14 | OP2 | Attack, Decay, Sustain, Release |
| 15–18 | OP3 | Attack, Decay, Sustain, Release |
| 19–22 | OP4 | Attack, Decay, Sustain, Release |
| 23–26 | OP5 | Attack, Decay, Sustain, Release |
| 27–30 | OP6 | Attack, Decay, Sustain, Release |

Envelope recipes: pluck = atk 0.001, dec 0.15, sus 0.0, rel 0.1; pad = atk 0.5, dec 2.0, sus 0.9, rel 1.0; growl = atk 0.005, dec 0.4, sus 0.8, rel 0.1.

### Combining FM with the production stack

- **Bass:** `psy_fm` + `growlBass` preset. LFO → OP6Feedback (306) for evolving growl.
- **Lead:** `psy_fm` + `acidLead` preset. Mod wheel → feedback for performance control.
- **Stabs:** `psy_fm` + `metallicPluck` preset. Fast envelope on non-integer operators.
- **Risers:** `psy_fm` + `riser` preset. Bar clock auto-speeds ratio-sweep LFO.

## 5c. Psytrance internal instruments (new in v0.25.1)

Two purpose-built psytrance synths ship as internal FX types. They
complement the `psy_fm` synth (§5b) — use them as the primary instruments
for the corresponding roles.

### growl_bass — dedicated offbeat rolling bass

```
add_fx { trackId, fxType: "growl_bass" }
set_internal_fx_param { trackId, slotIndex, paramIndex: N, value: V }
```

| Index | Name | Range | Role in §0.5 canon |
| ------- | ------ | ------- | -------------------- |
| 0 | Fundamental Hz | 20–200 (def 55) | Bass register |
| 1 | Mod Ratio | 0.5–8 (def 1.5) | FM depth/grit |
| 2 | Mod Depth | 0–1 (def 0.6) | FM intensity |
| 3 | Mod Shape | 0=Sin,1=Tri,2=Sq | Waveform color |
| 4 | **Clip Type** | 0=SoftTanh,1=SoftAtan,2=Hard,3=Bitcrush | **Principle 2: waveshaping** |
| 5 | **Drive dB** | 0–40 (def 18) | **Principle 2: distortion intensity** |
| 6 | Asymmetry | −1–1 (def 0.15) | Odd-harmonic color |
| 7 | Bitcrush Bits | 2–16 (def 8) | Digital grit (ClipType=3 only) |
| 8 | **Filter Cutoff** | 20–20000 (def 800) | **Principle 2: first filter** |
| 9 | Filter Res | 0.1–20 (def 4) | Resonance sweep |
| 10 | Filter Env Amt | 0–1 (def 0.7) | Pluck/open feel |
| 11 | Filter Type | 0=LP, 1=BP | Tone color |
| 12–15 | ADSR | ms (0.1–100/1000) | Envelope |
| 16 | Output Level | 0–1 (def 0.4) | Volume |
| 17–19 | Unison | enable/voices/detune | Width/spread |
| 20–21 | Ratio Jitter | enable/amount | Organic pitch wobble |
| 22–23 | Formant | enable/morph | Vocal vowel quality |
| 24–25 | Sidechain | drive/amount | Kick pump (principle 4) |

**Key principle 2 recipe:** ClipType=2 (Hard), Drive=25–35, Filter
Cutoff=600–1200, Res=6–10. Add an external `filter` FX slot after
for the second filter pass (principle 2: instrument filter → external
filter → …). Automate the external cutoff across sections.

### psyarp — built-in arpeggiator synth (principle 5)

```
add_fx { trackId, fxType: "psyarp" }
set_internal_fx_param { trackId, slotIndex, paramIndex: N, value: V }
```

| Index | Name | Range | Role |
| ------- | ------ | ------- | ------ |
| 0 | Osc Shape | 0=Saw,1=Sq,2=SuperSaw | Timbre |
| 1 | Unison Voices | 1–4 (def 2) | Width |
| 2 | Unison Detune | 0–50 (def 8) | Spread |
| 3 | **Pattern Shape** | 0=UpDown,1=Asym332,2=Random | **Arp pattern** |
| 4 | Octave Range | 1–4 (def 3) | Register spread |
| 5 | Bars Per Motif | 0.5–8 (def 2) | Phrase length |
| 6 | Filter Cutoff | 20–20000 (def 600) | Built-in filter |
| 7 | Filter Res | 0.1–20 (def 7) | Resonance |
| 8 | **Filter Sweep Bars** | 0.5–16 (def 4) | **Principle 7: cutoff evolution** |
| 9–12 | Delay | time/feedback/ping-pong/wet | Stereo depth |
| 13–15 | Reverb | size/wet-on-dry/wet-on-delay | Space |
| 16–18 | Phaser | enable/rate/depth | **Principle 7: L/R movement** |
| 19 | Output Level | 0–1 (def 0.4) | Volume |

**Key principle 5 recipe:** feed notes from `scaleNote` in F harmonic
minor (scale mode 7). Pattern Shape=0 (UpDown) or 2 (Random). Octave
Range=2–3. Bars Per Motif=2–4. Filter Sweep Bars=4–8 (cutoff drifts
across the motif, principle 7). Phaser Enable=1, Rate=0.1–0.5
(principle 7: L/R phasing). Add an external `filter` FX slot after for
the second filter pass (principle 2).

**Root notes for arp:** use `scaleNote(degree, octave)` from the project
scale (F harmonic minor mode=7, root=5). Degrees 0–6 map to
{F,G,Ab,Bb,C,Db,E}. Feed the resulting MIDI pitches into the psyarp clip.

### Combining the instruments

| Role | Instrument | Why |
| ------ | ----------- | ----- |
| Bass | growl_bass | Dedicated growl, built-in waveshaper + filter + sidechain |
| Arp lead | psyarp | Dedicated arpeggiator, built-in sweep + phaser + delay |
| Stab/acid lead | psy_fm (acidLead preset) | High-feedback screaming tones |
| FX/blips | psy_fm (metallicPluck preset) | Non-integer ratios, alien perc |
| Pad | external sampler (chorus+reverb FX) | Sustained texture |

After the internal instrument, add 1–2 `filter` FX slots for the
second/third filter pass (principle 2), then an EQ or compressor for
final tone shaping.

---

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

**Verified canary numbers (2026-08-30 F-minor session, 140 BPM, 400 beats):**

- Canary at master 0.25 → peak 0.407 → truePeak ≈ 1.63 → final `min(0.90/1.63,1.0)` = 0.55.
- Final peak 0.852; RMS arc: intro .030 / build .082 / mainA .095 / mini .023 / mainB .096 / breakdown .040 / finale .113.
- `pumpDepth` 0.74, `kickProminence` 0.74.
- **Fader set that rendered safely at master 0.55:** kick .85, bass .80, hats 1.0, stabs .95, arp .90, pads .75, riser .90, down .90.

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
- Since v0.25.2: `export_audio` uses atomic CAS guard; `queue:true` waits for previous export (120s timeout) instead of immediate reject; `cancel_export` still aborts.
- Render the REAL project duration (see §4), not a fixed window.
- Save `.hdaw` projects next to renders: `.tmp_dnb_theme/<name>.hdaw` +
  `<name>.wav`. Renders from the keep-flag run land there by convention.
- Long renders with automation + save/load accumulation are crash-stress
  scenarios (see handoff §3 'unreproduced abort'); run under
  `%TEMP%\hdaw_capture\run_with_capture.ps1` (procdump) when iterating on
  engine changes, and keep the automation lanes' normalized values in the
  safe 0.01–0.85 range.

## 8.5. MCP launcher (fixed 2026-08-30)

The `mcp-launch.bat` launcher was broken by commit `667f108` (unescaped inner
double quotes in cmd.exe). Fix: crash-capture logic extracted to
`mcp-launch-capture.ps1` (repo root), invoked via `powershell -File`. The bat
calls it with `powershell -NoProfile -ExecutionPolicy Bypass -File`. Behavior
preserved: engine started with inherited stdio (stdout stays pure JSON-RPC),
procdump attached as watcher, exit-code propagation. `HDAW_NO_CRASH_CAPTURE=1`
bypasses the procdump attach.

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
   tools mix "ok" text and JSON; try both. `add_midi_clip`/`add_audio_clip` now return JSON `{"clipId":N}` since v0.25.2 (parse both).
7. **Key discipline:** derive every pitch (bass roots, arp tones, stab
   triads, pad voicings, breakdown melody, even kick root) from ONE scale's
   degree set — v5's `fMinorDeg(degree, octave)` helper over
   {F,G,Ab,Bb,C,Db,Eb} produced an entirely in-key track with progressions
   like i–VII–VI–VII (A) and VI–VII–i–i (B). Don't hand-type chromatic
   pitches into a long score.
8. **`related_samples` path normalization:** the tool does case-sensitive,
   exact string matching on paths. Cluster output paths may use different
   case or separators than the stored entries. Fixed in LibraryClusterer.cpp
   (2026-08-30) — now normalizes through `juce::File` for case-insensitive,
   separator-agnostic comparison.
9. **Output-size discipline:** `add_notes` returns the complete `noteIds`
   array (1,129 ids for a large arp clip). Over MCP these blow past client
   output guards. Prefer `includeIds:false` when available; `list_notes`
   routinely exceeds the guard. `list_clips` lacks `noteCount` (orphan
   detection requires `list_notes` round-trips).
10. **Unknown MCP tool names abort the script:** calling a non-existent tool
    (e.g. `hdaw_add_automation_points` — real name is `set_automation_points`)
    throws inside mcpScript with zero output, while invalid-args returns
    `{ok:false}`. Always verify tool names with `tools.search` first.
11. **`export_audio` start/end are SECONDS. AGAIN.** (2026-09-01 session.)
    Four renders cut short by passing beats. Convert: `seconds = beats × 60
    / BPM`, and verify the produced file size ≈ `duration × sampleRate ×
    channels × bytesPerSample` before analyzing. The WAV byte count is the
    ground truth for what you actually rendered.
12. **Async exports cancel in-flight work silently.** Starting a second
    export while one renders aborts the first with no error surfaced to the
    caller — two "export started" acks, one missing file. Serialize: wait
    for the output file to reach its expected byte size before starting
    the next export, never pipeline them.
13. **Verify the arrangement by scanning the rendered WAV, never the score
    bookkeeping.** Section maps, note ranges, and beat math in the build
    script can all be wrong while every tool call returns "ok". The 2026-09-01
    session found a full-instrumentation block inside a "breakdown" that the
    score claimed was empty — and a breakdown dip that the map said didn't
    exist. Decode the WAV, compute an RMS envelope (mind: interleaved stereo
    → per-sample time = index×stride/(sr×ch)), and locate the quiet regions
    empirically.
14. **Layering the same register/rhythm = masking, not reinforcement.** The
    FM growl doubled the sample bass (same offbeats, same pitches, same
    octave) and was completely inaudible in the mix despite measuring RMS
    0.23 soloed. Distinct synth layers need distinct register, rhythm, or
    role (growl OWNS the low offbeats in its sections; sample bass rests or
    moves register). Solo-probe each synth layer (`export_audio trackIds`)
    before judging it in the full mix.
15. **`psy_fm` live-engine MCP tools fail with "track not found"** (open bug
    2026-09-01): `psy_fm_load_preset` and friends go through
    `getMainProcessor()->getTrack()` → null, while the ValueTree path
    (`set_internal_fx_param`) works. Workaround: configure via
    `set_internal_fx_param` indices (see §5b). The frontend Router_PsyFm
    shares the same pattern and likely fails the same way. See
    `docs/handoffs/2026-09-01-psyfm-bugs-handoff.md`.
16. **Don't trust response-less side effects.** A tool call whose result you
    don't inspect may have errored (`remove_notes` silently no-oped once —
    wrong assumption, breakdown stayed full). Check the response text ("removed
    N notes"), use `dryRun` first for destructive ops, and re-`list_notes` to
    confirm the range is actually empty.

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
