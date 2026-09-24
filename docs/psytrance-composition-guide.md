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
§4/§5 choices (§5 onward now lives in `docs/psytrance-va-and-production.md`).

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
   - Alternative: `psy_fm` with high feedback (param_6 = base feedback)
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
   (§5c in `docs/psytrance-va-and-production.md`) is purpose-built for this.

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
python analyze_psytrance.py   # strides each folder, DSP→CLAP→LLM sidecars,
                              # registers each folder as an HDAW audio library
```

Per-folder variant (list explicit folders / just register):

```
python lib_analyze.py "E:\samples\Some Pack" --library NAME
python register_library.py --path "E:\samples\Some Pack" --name Short-Name
python analyze_multi.py      # multi-folder, models loaded once, strided
```

Sidecar records now carry `key` and `bpm` (filename tag first — `Am`,
`F#m`, `C min`, `128 BPM` — then Krumhansl chroma / onset-tempo estimation).
FileLibraryManager ingests them on scan: sidecar key overrides the native
chroma guess; sidecar bpm fills in when the entry has none. `search_library`
key/BPM filters therefore match analyzed pads/loops directly.

- `lib_analyze.py --library` writes the registry via script — safe only when NO
  engine is running (an engine restart clobbers externally-written registry
  entries; while a DAW/MCP engine may run, register via MCP `add_library`).
- Sidecars land as `<file>.timbre.json` next to each sample; `scan_library`
  ingests them (async — poll `list_libraries` fileCount/lastScan for done).
- Pack filenames inside `E:\samples` are reliable role hints; analysis CLAP
  tags + DSP descriptors back them up (see `select_psy_samples.py`).

### Source material locations (dev box, verified 2026-09-22)

| What | Root | Files | Sidecars |
| --- | --- | --- | --- |
| Sample packs (audio) | `E:\samples` | 52,742 | `.timbre.json` per analyzed sample |
| MIDI packs | `E:\midi` | 19,137 | — (parse with `analyze_midi_file`) |
| Virus patches (Vavra / OsTIrus / Osirus) | `D:\pdf\Virus Presets` | 303 | 148 `.virus.json` |
| JE8086 patches | `D:\pdf\je8086` | 7,503 | 3,735 `.je8086.json` |
| Waldorf microQ | `D:\pdf\rhythm-lab.com_waldorf_micro_q` | — | 528 `.vavra.json` |
| Waldorf Microwave XT | `D:\pdf\microwave` | 116 | 34 `.xenia.json` |
| Nord Lead 2x banks | `D:\pdf\NL2x Banks` | 13,739 | 6,841 `.nl2x.json` |
| DX7 cartridges (fm_synth) | `D:\pdf\Dexed Presets` | **19,197 `.syx`** | — (load with `fm_synth_import_sysex`, 32-voice carts; `voiceIndex` picks the voice, e.g. `1980 Sounds\Keys.syx` v3 = "RHODES EGH") |

**MIDI snippet labels are NOT trustworthy — analyse them.** `E:\midi` pack filenames carry
`[root] [mode]` tags (e.g. `18 - [RIFF] [C] [Natural Minor] [8bars].mid`). Measured
2026-09-22: of 104 files whose names claim `[C] [Natural Minor]`, only **55** actually
analyse as C + Natural Minor — 8 have a different root (63/67 = Eb/G, not C in another
octave) and **two analysed as `scaleType: 0` (Major) despite the "Natural Minor" label**.
Gating on the filename would have put sour notes in the arrangement. Always run
`analyze_midi_file {path}` first and keep only snippets where
`fingerprint.rootNote % 12 == 0` (C in any octave) **and** `fingerprint.scaleType == 1`
(Natural Minor, the same index as the project's `scaleMode`). The tool also returns
`key: "C minor"` and `scale: "Minor (Aeolian)"` as a human-readable confirmation, plus
`patterns[]` with per-bar `{pitch, startBeat, durationBeats, velocity}` ready to place via
`add_notes`.

- Only a fraction of `E:\samples` / `E:\midi` is HDAW-**registered**. Register per pack
  via MCP `add_library {name, path, type}` then `scan_library {id}` — the script path
  (`register_library.py`) is safe only when NO engine runs, because an engine restart
  clobbers externally written registry entries.
- Several large roots are still **unexpanded archives**, invisible to `search_library`
  until extracted: `samples.7z` 20.9 GB, `_Drum Loops.7z` 20.5 GB,
  `_Percussion Loops.7z` 26.5 GB, `_real_leads.7z` 7.0 GB, `_FXs.7z` 2.5 GB,
  `_atmospheres.7z` 1.8 GB.
- Genre-relevant packs already on disk — **psytrance**: `Kampfer Audio Psytrance Kicks 2`,
  `Psytrance Elements by Inside Mind Vol.2`, `Prism - Psytrance - Zenhiser`,
  `Santo Grau Records WS Dark Psytrance Sample Pack #2`,
  `FLOW36 Psytrance Sample pack Loops 2025`, `Antinomy Psytrance Sounds Vol.2 WAV MiDi`;
  **dub/reggae (psydub)**: `_Reggaeton and Dancehall/Full Dub Riddims Big Reggae Sample Pack{,_2}`;
  **breaks**: `_break/` (235 packs incl. Amen tributes, `100 Amen Breaks By Veak - Volume 2`);
  **atmosphere/SFX**: `_soundfx/` (108 packs), `Atmospheric Loops/`.

### Genre note: `audit_song_structure`'s backbeat gate assumes psytrance

`allDropsHaveBackbeat` requires a clap/snare on the **2-and-4** backbeat in every drop.
That is correct for psytrance and **wrong for dub**, where the defining accent is the
*one-drop*: rim/snare on **beat 3 only**, with beat 1 left open, and the offbeat skank
carrying the rhythm. Measured 2026-09-22 (`dub_embers`): a faithful one-drop (rim on
beat 3, `[RIFF]`-style offbeat skank) failed the structure gate with
"a drop has no clap/snare backbeat" while every other gate passed.

Two legitimate responses:
1. **Hybridise** — add a clap on 2-and-4 under the rim-on-3. That reads as
   electro-reggae / dancehall and satisfies the gate; it is what `dub_embers` does.
2. **Accept the failure** and record it — a roots one-drop is *supposed* to fail a
   psytrance-shaped gate; note it in the brief rather than distorting the groove.
3. **Skip the gate** (since 2026-09-23) — both surfaces take `expectBackbeat`
   (default `true`): `audit_song_structure {expectBackbeat:false}` / RPC
   `composition.auditSongStructure {expectBackbeat:false}` treats the gate as
   satisfied — `gates.allDropsHaveBackbeat` reports `true` with
   `dropChecks.backbeatChecked:false`, and `dropsMissingBackbeat` still lists the
   would-fail drops informationally. `ok` no longer counts the gate against the plan;
   every other gate is unchanged.

Do NOT "fix" it by moving the rim to 2-and-4: that converts the groove into a rock/pop
backbeat, which every reggae programming guide warns against.

### Render time scales with automation density, not just length

Measured 2026-09-22: a 300 s render of a 14-track arrangement with ~6,000 notes and
~15,000 automation points took **410 s — slower than realtime** (a 9-track
sampler-only render of the same length took ~10 s). Budget render time per *automation
points x tracks*, not per minute of audio: dense envelope work can make a "5 minute
render" a 7-minute wait, and two of them (measure + final) a quarter hour.

```
python select_psy_samples.py   # → timbre-lib/psy_sample_selection.tsv
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
| `set_lfo_param` | `{trackId, lfoIndex, param, value}` | `"ok"` | One property per call. targetParamID is a track-wide pid: 1=Volume, 2=Pan, 3=Mute, 100+slotIndex\*100+paramIndex (track FX), 1000+slotIndex\*100+paramIndex (MIDI FX), 2000+sendIndex (send level), 3000+busID\*8+paramIndex (bus FX). **FM 300–308 is NOT reachable** — pid ≥ 100 is tested first, so 306 decodes as track-FX slot 2 param 6; see the FM section below for the working route. |
| `add_automation_lane` | `{trackId, laneName, paramID}` | `"ok"` | **Disabled by default** — must `set_automation_enabled`. |
| `set_automation_points` | `{trackId, lane, points[{time,value}], mode:"replace"}` | `"ok"` | Key is `time` (beats). |
| `mix_report` | `{filePath, bpm, sections[{name,start,end}], fromPlan?}` | peak/RMS/bands/pumpDepth/boundaryPeak | Band cutoffs: sub<40, bass<300, body<2000, high>6000. `fromPlan` derives windows from the song plan and CLAMPS them to the file duration (short previews measure instead of hard-erroring; clamped section names return in `clampedSections`). Per-section `boundaryPeak` = first-0.1 s peak — the drop-entry transient gate. |
| `set_track` | `{trackId, volume?, pan?, mute?, solo?, name?, color?}` | `"ok"` | **There is NO `set_track_volume` MCP tool** — track volume/pan/mute go through `set_track` (the RPC route is `setTrackVolume`, but the MCP surface uses `set_track`). A wrong name returns `Tool "..." not found`, which a lax success check can mistake for success — verify the readback via `list_tracks` before trusting a gain move. |
| `scan_plugins` / `list_plugins` | `{wait?}` / `{kind: effect\|instrument\|all}` | `{jobId,…}` / `{plugins[], scanning, scannedCount}` | **Scan is async by default** (`wait:false` → poll `poll_job` for `{scanned,durationMs}`); `list_plugins` reports `scanning`/`scannedCount` so an empty catalog is never ambiguous. The engine also auto-scans at first launch when its cache is empty — never start a second scan or kill the engine mid-scan (an incomplete scan is not cached). |
| `psy_fm_load_preset` | `{trackId, slotIndex, preset}` | `"loaded preset: ..."` | preset ∈ {growlBass, acidLead, metallicPluck, riser}. |
| `psy_fm_get_analysis` | `{trackId, slotIndex}` | `{activeVoices, opEgLevels}` | Live audio-thread data (lock-free atomics). |
| `psy_fm_set_mod_route` | `{trackId, slotIndex, source, dest, depth}` | `"ok"` | source ∈ {ratioSweepLFO, feedbackLFO, modWheel, velocity, barClock}; dest ∈ {op1Ratio..op6Ratio, op6Feedback}. |
| `psy_fm_clear_mod_matrix` | `{trackId, slotIndex}` | `"ok"` | Removes all modulation routes. |
| `apply_sub_synth_mod_preset` | `{trackId, slotIndex, presetId}` | `"ok: preset '…' applied (params 27-32)"` | sub_synth LFO factory presets; atomic + undoable, patch params 0–26 untouched. presetId ∈ {off, slow_filter_drift, vibrato, tremolo, fm_motion, animated_sweep}. |
| `set_song_plan` | `{bpm, keyRoot, scaleMode, style, seed, totalBars, sections[{name,kind,bars}]}` | resolved plan JSON | Deterministic skeleton; syncs section-typed arranger regions in ONE undo unit; 4/4, totalBars must equal the bars sum; kinds: intro/build/mainA/mini/mainB/breakdown/finale/other. |
| `get_song_plan` | `{}` | `{hasPlan, sections[…startBeat/endBeat]}` | Read the plan back — every other tool references sections by NAME. |
| `apply_song_brief` / `export_song_brief` | `{brief}` / `{}` | plan echo / verbatim brief JSON | psy-song-session Brief ⇄ plan (peak→mainA, outro→finale, drop→mainB). |
| `set_cell` / `set_cells` / `get_cells` / `remove_cell` | `{section, role, trackId, source, params, seed, locked}` etc. / `{cells:[…recipes]}` | ok / cells JSON | Content recipes on the section×role matrix. source ∈ phrase/rhythm/break/pattern/harvest; seed 0 = derived from plan seed. **`set_cells` is the batch form: N recipes in ONE undo unit and one round trip** (a 9-role × 10-section track is 55 cells); per-recipe failures are reported without aborting the batch. |
| `fill_cells` | `{mode: all\|unfilled}` | `{filled, skippedLocked, failed, cells[{clipId, noteCount, seedUsed}]}` | ONE undo transaction; clips span exactly their section window; re-fill reuses the cell's clip; locked cells skipped. |
| `reroll` / `get_clip_provenance` | `{section?, role?}` / `{clipId}` | batch JSON / `{found, tool, source, seed}` | Variation = lastSeed+1, deterministic; provenance answers "where did this clip come from". |
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

### Plan/cell workflow (recommended): deterministic skeleton, seeded content

The composition model this guide's sessions converged on: **structure stays
pinned** (sections, lengths, key, style), while **content inside each section
window is probabilistic** (any generator source, seeded, re-rollable). The
song plan is engine state now — humans edit it in the Compose tab ▸ Song Plan,
agents over MCP:

```python
# 1. Pin the skeleton (also creates/updates section-typed arranger regions)
await mcp_call("set_song_plan", { "bpm": 140, "keyRoot": 5, "scaleMode": 7,
    "style": "full-on", "seed": 777, "totalBars": 48, "sections": [
    {"name": "intro", "kind": "intro", "bars": 8},
    {"name": "main",  "kind": "mainA", "bars": 16},
    {"name": "break", "kind": "breakdown", "bars": 8},
    {"name": "drop",  "kind": "finale", "bars": 16}]})
#  or start from a session brief: apply_song_brief {brief: <SongBrief JSON>}

# 2. Assign content recipes on the section×role matrix (palette map = brief's)
await mcp_call("set_cell", {"section": "main", "role": "bass", "trackId": 1,
    "source": "phrase", "params": {"style": "BassLine"}})          # seed omitted = derived from plan seed
await mcp_call("set_cell", {"section": "main", "role": "hat", "trackId": 2,
    "source": "rhythm", "params": {"pulseA": 16, "pulseB": 0}})
await mcp_call("set_cell", {"section": "drop", "role": "arp", "trackId": 3,
    "source": "pattern", "params": {"patternId": "factory/melodic/acid-run"}})
await mcp_call("set_cell", {"section": "break", "role": "hits", "trackId": 4,
    "source": "harvest", "params": {"notes": [...]}})               # analyze_midi_file output

# 3. Fill (ONE undo unit), then verify + iterate by seed — never by rewriting structure
await mcp_call("fill_cells", {"mode": "all"})
# export_audio → mix_report {filePath, fromPlan: true}   (windows come from the plan)
await mcp_call("reroll", {"section": "drop"})   # same cell, seed+1 — structure untouched
```

Sources: `phrase` (all generate_phrase styles + styleParams), `rhythm`
(euclidean/DSL/bank), `break` (needs `set_sampler_mode slice` +
`detect_sampler_slices` first), `pattern` (PatternLibrary preset),
`harvest` (raw note arrays). Lock a cell to freeze content you like;
`get_clip_provenance` reports tool/source/seed of any filled clip. The
whole-table UI is Compose tab ▸ Song Plan; RPC mirrors every tool
(`composition.*`) so the frontend and agents share one state. The panel's
**Check energy** button closes the loop in the UI: `export.temporaryRender`
(whole-project WAV into the temp dir, progress on the normal export channel)
→ `audio.mixReport { fromPlan: true }` → one RMS bar per plan section (red
when a section peaks ≥ 0.99), with peak / kick-prominence / pump readouts.

### Sketch tools: `generate_psytrance` (one call, whole song)

Instead of hand-computing 2,600+ notes via the per-role templates below,
use the `generate_psytrance` MCP tool (one call writes the complete score).
Sketch-grade: its section plan evolves probabilistically — for a pinned,
re-rollable structure use the plan/cell workflow above:

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

**Component architecture.** The generator is decomposed into four
genre-agnostic components, each owning a style-parameter struct:
`MarkovArranger` (orchestrator — validation, active-set bookkeeping, Markov
action selection, section-energy schedule, clip assembly),
`PercussionEngine` (percussive themes, 16-step velocity grids, unit
rotation), `HarmonyEngine` (key, progressions, chord tones, pitched
emission) and `TextureEngine` (riser/downlifter accents, filter sweeps).
The style structs (`PercussionStyle`/`HarmonyStyle`/`TextureStyle`, in
`src/engine/`) are the seam where future genre style packs (JSON) land, and
where P2 (riff-centric harmony, in HarmonyEngine) and P3 (long-form
textures, dub bursts, in TextureEngine) will extend. Genre breadth =
parameter presets, not new generators; cross-generator combination is an
agent-level move (multiple MCP calls), never engine coupling.

> Same-seed determinism is the only sequencing contract (same seed + params
> → byte-identical score, run to run). The 0.29.0 component refactor
> preserved the seeded draw order, so per-seed scores carry over from
> 0.28.x; per-seed continuity across future versions is NOT contractual —
> a reorder is acceptable (tests are self-comparisons and property sweeps).

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

## 4C. Layered composition (per-element agents — PREFERRED for final tracks)

Generating the whole section×role matrix at once writes every layer against an
empty slate, and mixing all layers together afterwards leaves voices that land
in the same registers masking each other. Measured evidence, Neon Mycelium
2026-09: the lead (F4–C7) masked the rhythm bed, and body-band energy dropped
5x once the psy_fm output cut + an octave-down lead were in. The fix that
stuck: build the song LAYER BY LAYER, one dedicated element agent per layer,
each MEASURING the cumulative mix (spectrum, register, levels) before writing,
and crafting its part into the gaps. Bulk cell fill (§4 plan/cell) stays as
the SKETCH-mode fallback; layered is the PREFERRED path for final tracks.

**Layer order** — each a separate dispatch, strictly sequential, single writer
(full playbook: `docs/skills/psy-song-session/roles/layer-agent.md`):

| Layer | Band target | Register cap |
| --- | --- | --- |
| kick | <120 Hz | n/a |
| bass | 60–250 Hz | ≤ MIDI 60 |
| hats/snare/clap/down | n/a (percussion) | n/a |
| stab | 200–900 Hz | ≤ MIDI 72 |
| pad | 150–600 Hz | ≤ MIDI 72 |
| lead | 400–3000 Hz | ≤ MIDI 88 — THE one high part |
| riser | sweep (band sweep) | 400–4000 Hz |

**Register budget rule:** at most 2 parts above MIDI 72 across the whole song,
yours included; the lead is the usual holder. Gate G2 in the playbook counts
the existing parts BEFORE you commit.

**psy_fm level rule (non-negotiable):** Output Level (param 31) =
**0.15..0.22**, fader **0.7..0.8** — NEVER 0.3+. The 0.3+ overdrive is the
measured cause of the Neon Mycelium lead masking the rhythm bed. Write in real
units via `set_internal_fx_param` and read every param back (lesson 23).

**Measuring gates (condensed from the playbook):** cumulative render
(`export_audio`) → `mix_report {fromPlan: true}` for rms/peak/4-band;
`analyze_tuning` per role against its band target (kick <120, bass 60–250,
arp/lead 400–3000, hat >6k); `verify_part` after writing (audible=1,
nonClipping=1); level ceiling — your layer's solo render may add at most +20%
to the cumulative rms, then reduce YOUR output level/fader, never other parts.

**Delay + bitcrusher recipes (same as the playbook):** tempo-synced echo on a
melodic lead — two delay slots: A SyncToTempo=1, Division=4 (dotted-1/8 =
3/16), Feedback=0.30, Mix=0.35; B SyncToTempo=0, DelayTime=0.5172 (5/16 @145 —
no division slot exists, so manual seconds), Feedback=0.30, Mix=0.30; read
back the derived seconds (no ping-pong yet — tagged engine future). Bitcrusher:
saturator Type=3 (Bitcrush), Bits 8–10, Mix ≤0.4 — subtle (idx1 Type 0..3,
idx5 Bits, idx3 Mix; Bits range 2..16).

Progress ledger: each layer's handoff `{role, trackId, band, register,
beforeRms, afterRms, verifyPart, ...}` is appended to
`compositions/<song>/layers.json` by the orchestrator between layers — read it
before you start; it is the contract input for your register/level gates.
Persist the machine-readable handoff in-project with `set_layer_handoff` (read
back with `get_layer_handoffs`; verified by `audit_modulation_coverage`) —
layers.json is the human mirror.

## 4D. Hardware VA suite — the gearmulator CLAPs (summary)

Real synth firmware running as isolated CLAP plugins (OsTIrus/Osirus Virus, Vavra
microQ, Xenia Microwave, JE8086 JP-8080, NodalRed2x Nord 2x; Dexed retired
2026-09-14). Six of seven render audibly offline; an isolated export restores
`pluginState` into a fresh child, so a plugin whose patch state does not round-trip
auditions differently from what it renders. The gearmulators' INTERNAL FX are
host-visible automatable CLAP params (probe 2026-09-16) — drive them with
`set_fx_param`/automation lanes instead of CC sweeps; `send_fx_midi` (PC/CC/sysEx)
covers what params cannot. The audition loop: inject → `save_project` →
`export_audio` → measure — the save persists preset state into the tree.

Full §4D detail (param indices, durability rules, known limitations):
[`docs/psytrance-va-and-production.md`](psytrance-va-and-production.md) §4D.
Device matrix, per-device recipes, modulation-first policy, measured per-engine
status: `docs/hardware-va-suite.md`.

## Where the rest went (split 2026-09-24)

The second half of this guide moved verbatim to
[`docs/psytrance-va-and-production.md`](psytrance-va-and-production.md): §4D
hardware VA detail, §5 production stack, §5a Jordan cave-dub FX preset, §5b FM
synthesis, §5c internal instruments (growl_bass/psyarp/sub_synth), §6 mix +
master, §7 verify loop, §8 export housekeeping, §8.5 MCP launcher, §9 contract
traps, §10 evidence locations. The per-plugin status log that used to live in
`hardware-va-suite.md` §9 moved to
[`docs/va-suite-status-log.md`](va-suite-status-log.md).
