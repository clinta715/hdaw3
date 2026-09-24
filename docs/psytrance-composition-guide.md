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

## 4D. Hardware VA suite — the gearmulator CLAPs (verified 2026-09-11/12)

Real synth firmware running as isolated CLAP plugins — installed in
`C:\Program Files\Common Files\CLAP\` with their ROMs:

**Patch pipelines + loader reality (2026-09-16).** Every bank library now has a
decoder writing searchable sidecars: JP-8080 `timbre-lib/je8086_patch.py` (4983
entries / 2676 usable patches, plus an exploded per-patch tree), microQ
`microq_patch.py` (528 patches, categories carried in the dump), Nord 2x
`nl2x_patch.py` (6841 sidecars), Virus `virus_patch.py`. Loaders:
`load_nord_bank` is the one **verified** bank loader (a test asserts the render
changes); `load_virus_preset` (CC0+PC) works; **JE8086 DT1 dumps apply since
2026-09-20** (wrapper retargets UserPatch→temp performance; use `load_je8086_preset`),
and `set_fx_param` covers 461 params; **Vavra's FX sub-params cannot be exposed**
(derived-parameter collapse onto the type-level params) — use its 392-byte device dump
or the `FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix` params. Rendering caveat: an
isolated export restores `pluginState` into a **fresh child**, so a plugin whose
state does not round-trip its patch exports differently from what you audition.
Device matrix, per-device modulation/FX recipes and the modulation-first policy:
`docs/hardware-va-suite.md`.

| Plugin | Emulates | Isolated-render status (measured) |
| --- | --- | --- |
| OsTIrus | Access Virus TI | ✅ voices at default (peak 0.33–0.47) — the workhorse |
| Vavra | Waldorf microQ | ✅ voices (0.056–0.078) |
| Xenia | Waldorf Microwave II/XT | ✅ voices (0.30) |
| JE8086 | Roland JP-8000 | ✅ voices (0.23–0.27) |
| Osirus | Access Virus A/B/C | ✅ voices; state-apply on load rejected by the plugin (see durability rules) |
| NodalRed2x | Clavia Nord Lead 2x | ✅ renders audibly since the multi-port fix (v0.34); banks load via `load_nord_bank` SysEx injection |
| Dexed | Yamaha DX7 | ❌ preset pipeline removed 2026-09-14 (ignores injected state; probed silent) — use internal `fm_synth` for DX7 voices |

### Internal FX are HOST-VISIBLE parameters (probe 2026-09-16)

The gearmulator CLAPs expose their INTERNAL FX as fully-named, automatable CLAP
params with real-unit text — the FX stage does NOT need MIDI for them:
`list_fx_params` on the slot shows e.g. Osirus `Ch N Chorus Mix/Rate/Depth/Delay/`
`Feedback/LfoShape`, `Ch N Delay/Reverb Mode` (0-26 types), `Ch N Phaser
Mode/Mix/Depth/Frequency/Feedback/Spread`, `Ch N Distortion Curve (0-11)/`
`Intensity`, per-channel EQ; JE8086 `A/B CHORUS TYPE/LEVEL`, `A/B DELAY
TYPE/TIME/FEEDBACK/LEVEL/SYNC`, `VOCAL MIX`; NodalRed2x per-slot `Distortion`
+ `ChPrs Amount`; Dexed has none (DX7 architecture). All automatable with
hasRange=true (real units via text; value space is normalized 0-1, matching
`set_fx_param`). LFO/automation pids (100+slot*100+paramIndex) target them too.
Persist for offline renders via the standard save snapshot (audition workflow).
Also note: the dedicated effect editions (**OsirusFX, OsTIrusFX, VavraFX,
XeniaFX**) are excluded from effect lists and rejected by `add_fx` — synth-only
shadow builds that would silence a slot; making them usable (fix a) would
require a cross-repo gearmulator rebuild + MIDI forwarding — tracked as a
limitation (measurement below).

#### The `*FX` editions: no longer insertable (excluded 2026-09-23) — the measurement below is why

The four `*FX` CLAPs (OsirusFX, OsTIrusFX, VavraFX, XeniaFX) are scanned as
`kind: effect`, but inserting them inline on a track is no longer possible — they
are rejected by `add_fx` (see the first bullet). **The reason is the 2026-09-22
measurement (`dub_embers`): they do not work as insert effects:**

- **That invocation is no longer possible (fixed 2026-09-23).** The four names are
  rejected by `add_fx` on BOTH surfaces through the shared gate
  (`src/common/FxPluginIdCheck.h`, order: shadow-edition match BEFORE resolvability)
  and dropped from every effect list (`list_plugins` `kind:effect`/`all`,
  `plugin.getEffectPlugins`, the frontend FX picker), as an explicit four-name family
  list (case-insensitive) — so neither the qualified-id load nor the bare-name dead
  slot can be reproduced. The measurement below stays as the reason for the exclusion:
  before the gate they loaded **only with the format-qualified scan id** (`list_plugins`
  → `add_fx {trackId, pluginId: "CLAP-VavraFX-a405fdaa-0"}`), while the **bare name**
  (`"VavraFX"`) left a dead slot: `list_fx` reported `"pluginFormat": ""` and
  `"paramCount": 0`, and `list_fx_params` returned `{"params": []}`; with the
  qualified id the same slot reported `"pluginFormat": "CLAP"` and exposed the full
  surface (**VavraFX: 7,557 params**, oscillators through FX).
- **After the instrument in the chain they silence the track.** A/B with
  `set_fx_bypass` on the same window: bypassed `soloRms 0.0595 / audible=1`, active
  `soloRms 0 / audible=0` (VavraFX on a modal part); the same pattern held for
  XeniaFX. Placed **before** the instrument they pass audio unchanged (a no-op:
  identical RMS to 5 decimals either way).
- They also will not resolve a program/state via the usual preset tools in this
  build, so there is nothing to "fix" by loading a patch first.

**`paramCount` is no longer a broken-slot signal (fixed 2026-09-23).** It reports the
**internal FX defs-table size** for an internal FX slot (`none`/unknown types → 0) and the
**live instance param count** for a plugin slot — in-process and isolated-proxy alike —
and **0 while the instance is not loaded** (`TrackFXSlot::paramCount`, projected by
`ReadModelImpl`). So the `"paramCount": 0` above now reads as "instance never loaded",
not "slot silently broken"; regression test:
`FxSurface.ParamCountReportsInternalDefsNotTreeChildren`.

**Working path for that hardware FX character:** use the **instrument** build
(Osirus / OsTIrus / Vavra / Xenia / JE8086) as the sound source and automate *its own*
internal FX params — `Ch N Chorus Mix/Rate/Depth/Feedback`, `Ch N Delay/Reverb Mode`
(0-26 types), `Ch N Phaser Mode/Mix/Depth/Frequency`, per-channel EQ, distortion —
exactly as the section above describes. Those are real, automatable, and they render.

#### Gearmulator internal-FX recipes (probe-verified indices, 2026-09-16)

Before hand-picking a chain below, check the device's harvested matrix presets
first (`timbre-lib/matrix_presets/<engine>.json`, `docs/hardware-va-suite.md` §9)
— they are corpus-derived device-native configs.

Address any internal-FX param as a plugin param: `list_fx_params {trackId,
slotIndex}` → `paramID` (formula `100 + slotIndex*100 + paramIndex`) → drive it
with `set_fx_param` (normalized 0-1, by paramName or paramIndex), automation
(`add_automation_lane` + `automation_preset` / `apply_movement_plan`), or an
LFO (`add_lfo` targetParamID). Pids below assume **slot 0**; recompute per slot.
Plugin-slot writes **persist** into the slot's `appliedParamOverrides` ledger,
which every fresh export child replays — so `set_fx_param` reaches `export_audio` /
`audition_plugin` / `verify_part` and survives save/load (2026-09-21; before this a
plugin-slot write reached the LIVE child only and no render could see it).
`clear_fx_param_overrides` drops the ledger, `list_fx_params` flags `overridden`
entries, and the opt-in `liveParamState` argument on `audition_plugin` renders
unpersisted live-only writes (what you currently hear) instead of tree state.

| Goal | Synth | Params (slot 0 pids) | Move it with |
| --- | --- | --- | --- |
| Delay riser over a build | Osirus | base `Ch 1 Delay/Reverb Mode`=188 (set a delay type), ramp `Ch 1 Delay Time`=3156, feedback `Ch 1 Delay Feedback`=3157, clock `Ch 1 Delay Clock`=3165 | `automation_preset riser/macro` on 3156 over the build window (apply_movement_plan) |
| Chorus sweep | Osirus | `Ch 1 Chorus Mix`=182, Rate=183, Depth=184, Feedback=186 | LFO sine on 183 (depth 0.3) or openClose on 182 |
| Phaser movement | Osirus | `Ch 1 Phaser Mode`=260, Mix=261, Rate=262, Depth=263, Frequency=264, Feedback=265, Spread=266 | LFO on 263/264; band-pass character via 260 (0-6 stages) |
| Distortion gating | Osirus | `Ch 1 Distortion Curve`=275 (set 1-11), `Intensity`=276 | square preset on 276 per beat window |
| Delay throw at a drop | JE8086 | `A DELAY TYPE`=184 (PANNING L->R…), TIME=185, FEEDBACK=186, LEVEL=187 (>0 enables) | delayThrow preset on 186 |
| Vocal FX gating | JE8086 | `VOCAL MIX`=542, `EXT TO VOCAL SEND`=424 | pump on 542 |
| FX slot switch + movement | Vavra (microQ) | type-level host params (`FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix`) — the Fx1/Fx2 chorus-phaser-delay sub-params are **bit-aliases** (derived-parameter collapse) and still cannot be set with `set_fx_param` (the full surface is 7557 host params since 2026-09-19; the old `{"params":[]}` reading is superseded). **CORRECTED 2026-09-21: `set_fx_param` writes DO reach renders** — the public route persists into `appliedParamOverrides` and every fresh export child replays it (`VavraHostParamPersistedWriteAffectsExport`), while unpersisted live-only writes show up through the opt-in `liveParamState` probe (`LiveParamStateProbeReflectsUnpersistedWrite`). The earlier "LIVE host-param writes do NOT move the render" reading was a harness artifact (renders use a tree copy in a fresh child) | bake the FX into the patch or load a `waldorf_dump` when the change belongs in the patch; `set_fx_param` automation is budgetable for movement again (durable ledger route). Sub-params also via dump; the device's remote-control SysEx (`EmuButtons`/`EmuRotaries` — a `SetParam` message is ignored) is the remaining route but is not yet driven from HDAW |
| Distortion accents | NodalRed2x | `A Distortion`=154 (0/1), `ChPrs Amount A`=444 (0-7 chor./pres.) | square/steppedGate on 154 for rhythmic grit |

`list_fx_params` text gives real units (0-127, -64..+63, type enums like
"SUPER CHORUS SLW" / "Pattern X+Y" / "5.1 Delay Clocked") — verify the value
range before writing (lesson 23 discipline: no out-of-range writes).

### Injection tools (all verified end-to-end)
- `send_fx_midi {trackId, slotIndex, messages[]}` — PC / CC / note / **sysEx**
  into the slot's LIVE instance (parent → SHM → child, byte-exact round-trip
  tested). Background re-apply with backoff for slow-booting children (the
  OsTIrus DSP boot takes seconds).
- `load_virus_preset {trackId, slotIndex, bank 0-7, program 0-127}` — CC0 bank
  select + PC (Virus banks A–H singles).
- ~~`load_dexed_cartridge` removed 2026-09-14~~ — Dexed ignores injected cartridge state (probed: peak 0, state byte-identical). For DX7 voices use `fm_synth_import_sysex` into an internal `fm_synth` slot (same DX7 engine, fully controllable).
- `load_nord_bank {trackId, slotIndex, filePath, program?}` — Nord Lead 2x
  banks (`.syx` raw Clavia SysEx, `.mid` SMF-wrapped) into NodalRed2x:
  ATOMIC — every dump validated (F0 33 <dev> 04 header, F7-terminated,
  ≤32768B) BEFORE queueing, and a harmless CC125 is appended to trigger
  the deferred state capture AFTER the bank is fully consumed by the
  child. No separate send_fx_midi call needed. Sidecar pipeline:
  `timbre-lib/nl2x_patch.py` writes `<patch>.nl2x.json` descriptions
  over `D:\pdf\NL2x Banks` (6841 sidecars, 29436 dumps) — searchable
  by FileLibraryManager.
- `load_je8086_preset {trackId, slotIndex, filePath, preset?}` — Roland
  JP-8080 banks (`.syx` raw DT1 SysEx, `.mid` SMF-wrapped) into JE8086: ATOMIC —
  every DT1 message validated (F0 41 10 00 06 12 header, F7-terminated, Roland
  checksum, ≤32768B) BEFORE queueing. The dump keeps its UserPatch bank address, and the
  JE8086 wrapper retargets it onto the sounding temp performance, so the file's patch
  sounds immediately. No `CC0=1 USER + PC` recall is sent any more — a JP-8080 PC
  *loads* the emulator's bank program into the current patch and overwrote the dump
  (probe: 2026-09-20). Confirm the load with `poll_fx_capture` + a render: a patch
  dump does not appear in the param list, and the persisted `IDs::presetSysex`
  replay is what carries it into exports. `preset` is the 1-based patch unit
  in file order — choose one from the je8086 survey `roleShortlist` refs
  (`perf016/part2`, `bank0/slot25`). PER-PATCH by design: a 64-patch bank is 128
  DT1 messages while the injection carries ≤64 events over a single sysex lane
  that drops (not queues) when busy. Sidecar pipeline:
  `timbre-lib/je8086_patch.py` writes `<bank>.je8086.json` over `D:\pdf\je8086`
  (45 sidecars, 4276 entries + role shortlist) — searchable by FileLibraryManager.
  LIVE VERIFICATION (2026-09-16; the DT1 part is superseded 2026-09-20) — at that time
  DT1 dumps did not apply, the PARAMETER API did, and the plugin's saved state was a
  233-byte stub:
  - **Which tools isolate (RESOLVED 2026-09-16).** `add_fx {pluginId}` and
    `audition_plugin` BOTH create isolated slots while
    `pluginManager->isolationEnabled` is on (the default; the `--mcp-http` launcher
    does not change it). Verified two ways: a `hdaw_plugin_host.exe` child exists after
    each (`spawnPluginHost: slotId=1/2/3 plugin=C:/Program...` in the log), and the
    code path is shared - `add_fx` -> `ProjectCommands::addFxSlot` -> ValueTree change
    -> routing rebuild -> `Track::rebuildFXChain` sets
    `wantIsolated = pluginManager && pluginManager->isolationEnabled`. An earlier note
    that `add_fx` produced an in-process slot was wrong (it was inferred from a saved
    `pluginState`, which is written in both modes).
    This matters because isolation decides the capture/render semantics: an isolated
    render restores `pluginState` into a FRESH child, so a plugin whose state does not
    round-trip its patch will export differently from what you audition; an in-process
    slot (isolation off) renders from the live instance.

  - **Parameter writes work live.** `set_fx_param` on `A OSC WAVEFORM` (index 30)
    and `A OSC1 HARMONICS` (39) changed the plugin's own parameter list exactly as
    commanded (verified by diffing `list_fx_params` before/after). This is the
    supported control surface — it is what the JE8086 recipes above already use
    (delay type 184, vocal mix 542).
  - **DT1 patch dumps apply since 2026-09-20 (CORRECTED).** The 2026-09-16 reading
    ("never applied") rested on a byte-identical param list — which a *bank* write never
    changes anyway. The real cause: a real patch file addresses the UserPatch **bank**
    (`0x02000000`) and the wrapper's `jeController::parseSysexMessage` had an empty
    `case AddressArea::UserPatch`, so the write went nowhere and the sounding
    temp-performance patch stayed untouched. `JE8086.clap` now retargets host DT1s to
    `PerformanceTemp | PatchUpper` (the plugin browser's own `Controller::sendSingle`
    transform), and `load_je8086_preset` no longer appends a `CC0=1 USER + PC` recall (a
    PC *loads* the bank program and discarded the dump). Device probe + gate:
    `docs/plans/2026-09-20-je8086-userpatch-dt1-probe.md`,
    `FxMidiInjection.Je8086UserPatchDumpChangesOfflineRender`. **Confirming a load:**
    use `poll_fx_capture` + a render A/B — the param list does **not** move for a
    patch dump (a bank write never changed it, and the SysEx path does not echo back),
    and the live child state blob stayed constant too (2026-09-21). The durable
    readback is the persisted `IDs::presetSysex` replay, which is why a rebuilt child
    reproduces the patch exactly.
  - **Captured state and renders (CORRECTED 2026-09-16).** An isolated render
    instantiates a fresh child that restores `IDs::pluginState`, so an export only
    sounds like the live instance when the plugin's own `getStateInformation`
    carries the patch. The JP-8080 emulation's 233-byte state did not (2026-08 build;
    superseded by the custom `JPAR` CLAP 2026-09-18 and the DT1 retarget 2026-09-20) -
    which is why exports then played its default patch. Measured directly by
    holding the SLOT fixed (isolated, created via `add_fx {pluginId}`) and varying
    only the presence of a captured state: both renders were identical to 16 digits
    (`peak 0.10575640201568604`, rms 0.032181 vs 0.032177). Restoring a captured
    state is therefore a NO-OP for the render. The earlier reading was that a
    captured state poisons the render (peak 0.2455 default vs 0.2063 live); that
    comparison was between an **in-process** slot and an **isolated** one - a
    slot-type effect, not a state effect - so those numbers are withdrawn.
    Engine hygiene that did ship (D-lite): a capture that merely echoes the state an
    instance reported when it appeared is not persisted, and the deferred capture
    reports `captureStatus="unchanged"` instead of writing a fake ok. That is
    tidiness plus an honest receipt; it is not a fix for hear-not-equal-export.

### The audition workflow (inject → save → export → measure)
1. `send_fx_midi` (CC0 + PC) on the plugin slot.
2. `save_project` — REQUIRED: the preset lives in the plugin's live state; the
   save serializes it into the tree (`pluginState`), and offline exports render
   from the tree.
3. `export_audio {start, end, wait:true}` → measure (wavpeak / mix_report).
4. Compare RMS/peak fingerprints across presets; keep the distinct ones.

### State-durability rules (read before relying on presets)
- The serializer REFUSES to overwrite a substantial `pluginState` blob with a
  tiny read (size-regression guard, shipped 2026-09-12) — a load→save cycle
  once shrank 177KB Virus states to 262B stubs this way.
- After `load_project`, re-apply presets via `load_virus_preset` (the loaded
  state may be rejected by the plugin's own `setStateInformation` — OsTIrus
  silently falls back to its default; documented in
  `docs/plans/2026-09-12-plugin-state-durability.md` Phase 4b).
- Dexed/OsTIrus `setStateInformation` rejections are plugin-side (same family
  as Serum 2 — see the 2026-09-08 Serum investigation handoff).
- **Serum 2 is RETIRED (2026-09-14): do not pick it for sessions.** The state
  path is dead end-to-end — param tweaks, program switches (128 programs),
  and persisted-tree renders all produce a byte-identical 3076 B blob and
  identical audio (default patch only). No host-side control surface moves it;
  see `docs/plans/2026-09-14-b10-verdict-serum-probe.md` for the probe
  evidence. Use OsTIrus / Osirus / NodalRed2x / Dexed / sub_synth / psy_fm
  instead.
- **VES (Vintage Emulator Studio) is PARKED (2026-09-15).** Loads and runs
  isolated (MAME emulation executes, ~1 core) but the audio bridge delivers
  silence to the host; fails to load in-process. Patch data is never in the
  plugin state (machine+media paths only), the program API is a stub, and
  MAME NVRAM is transient per boot — presets would only ever be reachable via
  MIDI injection or disk images, and only after the silent-bridge question is
  resolved. Do not pick it; see
  `docs/plans/2026-09-15-ves-preset-loading-probe.md`.

### Isolated children render non-deterministically (known limitation)

The emulated synths run real firmware inside a DSP56300 emulator with
free-running oscillator phase — two exports of the same project produce
different sample data (~±2% RMS). The internal engines (psy_fm, sub_synth,
fm_synth, sampler) ARE deterministic.

Gate margins must tolerate ±2% when isolated plugins are in the project.
A/B comparisons should use spectral properties (centroid, band energies),
not sample-level equality. See `docs/realtime-safety.md` for the full
documentation.

### Known limitations
- TI bank switching via CC0+PC: unverified/ineffective on OsTIrus (all
  bank/program combos rendered hash-identically). TI part singles likely need
  TI-specific sysex or the plugin UI.
- `Percussion` phrase style on single-sample tracks: multi-pitch output
  (36/38/42) — pair with `set_sampler_key_range` per-role kits.

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
| `automation_preset` | movement recipes: `pump`, `macro`, `openClose`, `riser`, `sine`, `square`, `subtleLife`, `randomDrift`, `steppedGate`, `phaseSweep`, `delayThrow` |
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

# 4. Add an LFO to modulate the feedback. targetParamID 106 = track FX slot 0,
#    param 6 = psy_fm base feedback. NOT 306: the advertised 300-308 FM targets
#    are unreachable (see "Track-level modulation targets" below).
await mcp("add_lfo", {"trackId": trackId})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "param": "waveform", "value": 0})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "param": "rate", "value": 1})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "param": "depth", "value": 0.4})
await mcp("set_lfo_param", {"trackId": trackId, "lfoIndex": 0,
    "param": "targetParamID", "value": 106})
```

### Available presets

| Preset | Algorithm | Character | Best for |
| -------- | ----------- | ----------- | ---------- |
| `growlBass` | op6→op5→op1 | Feedback-modulated growl, settling envelope | Offbeat rolling bass, acid bass |
| `acidLead` | op6→op1 | High feedback, near self-oscillation | Screaming leads, filter-sweep-style performance |
| `metallicPluck` | op4→op2→op1 | Non-integer ratios, fast transient | Metallic stabs, alien plucks, percussive FM |
| `riser` | op5→op3→op1 | Ratio-sweep LFO, nested modulation | Risers, FX sweeps, tension builders |

### Track-level modulation targets

An LFO's `targetParamID` is a track-wide pid, decoded by the SAME chain the
automation lanes use (`src/engine/Track.cpp`; `docs/adr-automation-model.md`):

| targetParamID | Destination |
| --------------- | ------------- |
| 1 / 2 / 3 | Volume / Pan / Mute |
| `100 + slotIndex*100 + paramIndex` | Track FX param |
| `1000 + slotIndex*100 + paramIndex` | MIDI FX param |
| `2000 + sendIndex` | Send level |
| `3000 + busID*8 + paramIndex` | Bus FX param |

**The `300..308` FM targets listed here previously (OP1 Ratio … Ratio Sweep
Rate) do NOT work.** `Track.cpp` tests `pid >= 100` (the track-FX compound)
BEFORE the FM branch, so `306` decodes as track-FX slot 2 param 6 — with the
`psy_fm` of this recipe in slot 0 it drives nothing at all, or, worse, another
slot's param 6. That shadowing is pre-existing and deliberately out of scope,
so the FM branch under it is unreachable from every `targetParamID`.

**Reachable route for the same intent:** modulate the `psy_fm` slot's own
automatable params with the track-FX compound — pid `100 + slotIndex*100 +
paramIndex` where the psy_fm indices are `0..5` base ratios, `6` base feedback,
`7..30` operator ADSR, `31` output level, `32` algorithm (see
`src/engine/PsyFmState.h`; `list_fx_params` reports the indices). The recipe
above is slot 0, so base feedback is pid `106` and OP6's attack is pid `127`.
For modulation of the engine's internal *matrix* destinations
(`op1Ratio..op6Ratio`, `op6Feedback`, `ratioSweepRate`) use
`psy_fm_set_mod_route`, which runs inside the synth.

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

- **Bass:** `psy_fm` + `growlBass` preset (the preset already arms its own
  `feedbackLFO → op6Feedback` route). A TRACK LFO on that feedback uses the
  slot's own pid — `106` for slot 0 — never 306; see "Track-level modulation
  targets" above.
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

### sub_synth — modulation-matrix factory presets

```
add_fx { trackId, fxType: "sub_synth" }
apply_sub_synth_mod_preset { trackId, slotIndex, presetId: "<id>" }
```

One atomic, undoable call rewrites ONLY the internal LFO params (27–32:
wave/rate/cutoff/pitch/amp/FM amounts) — the loaded patch, oscillators,
filter, and envelopes (params 0–26) are untouched. Use it to give a staged
sub_synth voice its long-form movement (principle 7) without hand-writing
six `set_internal_fx_param` calls; the track-level ModulationManager LFOs
(§5b) remain the tool for cross-track/FX routing.

| presetId | Character | Psytrance use |
| ------ | ----------- | ------------- |
| `off` | Static, pure | Reference/audit; dry sub layer |
| `slow_filter_drift` | 0.12 Hz cutoff drift ±12 st | Rolling bass evolution (principle 7) |
| `vibrato` | 5.5 Hz, 18 cents | Lead/stab expression |
| `tremolo` | 6 Hz amplitude | Offbeat pluck pulse, percussive beds |
| `fm_motion` | 2 Hz triangle → osc2→osc1 FM | Growl texture, alien timbre motion |
| `animated_sweep` | 0.25 Hz cutoff+pitch+amp+FM | Build/riser beds, section transitions |

**Trap: verify `Cutoff` after any `sub_synth_import_sysex`.** The Virus→sub_synth
mapping writes the patch's filter as-is, and Virus patches with a closed filter land
at `Cutoff = 20 Hz` (the parameter minimum) — the slot then renders **near-silent**
while reporting a successful import with "24 params mapped". Observed 2026-09-22 on
two `Access_Virus_TI/*.syx` banks: solo RMS 0.0029 (inaudible under a kick), and
opening `Cutoff` (param 7) to 300 Hz took the same part to 0.0446 — a 15× change from
one parameter. So after importing, read the slot back
(`list_fx_params`) and set `Cutoff` for the role (sub ≈200–400 Hz, pad ≈1.5–3 kHz)
before auditioning; `audition_plugin {trackIndex, slotIndex}` reports `audible` and
is the gate, not the import's `ok`.

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
- **The drop must be the loudest point (gate).** `mix_report {fromPlan:true}` returns
  `loudnessGates`: each drop section's RMS against the build preceding it (pass at ≥ 0.9×, override
  with `dropBuildRatio`). A build reading RMS-hotter than its drop FAILS — thin the build cells
  (fewer arp notes / shorter riser, the musical fix) or lift the drop layers; never "fix" it with
  master gain. `audit_song_structure` carries the structural proxy (`dropsThinnerThanBuild`).
- **Never modulate pitch/ratio on melodic parts.** `psy_fm` param 0..5 are `OP1..OP6 Ratio` — an
  LFO or lane on pid 100+0 sweeps the carrier off-integer and reads as discord. Same for sub_synth
  semitone/pitch and sampler Transpose. Static detune ≈≤10 cents is fine; moving pitch is not.
- **Volume-lane authority (fader writes can be ignored):** an ENABLED Volume
  automation lane makes automation authoritative for that track, so `set_track_volume`
  afterwards is overridden (this masked a whole round of gain corrections).
  `audit_modulation_coverage` reports it per track (`faderOverridden`,
  `volumeLanes.enabled`) and in `summary.faderOverriddenIds`; call
  `set_fader_authoritative {trackId, authoritative:true}` before gain staging
  when a movement plan (which writes/enables Volume lanes) has already run.

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

- `export_audio`'s `trackIds` is a TRACK-INDEX filter that shipped 2026-09-08 — the
  handler compares each value against the track's position in `TRACK_LIST`
  (`McpExportTool.cpp:92-104`), so `trackIds: [2]` renders that track alone and any
  non-index value (a role name, e.g. `"PAD"`) matches nothing and mutes EVERY track.
  Omit the arg for a full-project render; serialize calls
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
  `analyze_targeted.py`, `lib_analyze.py` (the `--library` entry point).
- **Deliverables:** `.tmp_dnb_theme/` (antinomy_*, psytrance_production_v3/4,
  psytrance_darkforest_v5.wav + .hdaw projects).
