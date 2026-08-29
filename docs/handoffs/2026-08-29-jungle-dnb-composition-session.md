# Handoff — Jungle/DnB composition: knowledge, HDAW feature gaps, bugs (2026-08-29)

Session: three original jungle tracks composed end-to-end via the hdaw MCP
(amen core, ragga, atmospheric), after reading Wikipedia's Jungle music
article for style research. This handoff captures (1) what we learned that
should become *documentation* for future jungle/dnb composition, (2) the HDAW
feature gaps that would replace manual note/automation placement, (3) every
bug hit this session with status.

## 0. Session inventory

- Deliverables in `.tmp_dnb_theme/` (all 175 BPM, 640 beats ≈ 3:42, 48k/24-bit WAV + reloadable .hdaw):
  - `jungle_amen_fmin_v1` — core amen jungle, F minor, sub pump + filter sweeps
  - `jungle_ragga_v1` — ragga jungle: reggae organ skank (Rasta stem −2), chants, 808s, 136 BPM riddim breakdown
  - `jungle_atmos_v1` — ambient/intelligent (LTJ-Bukem): dub-riddim bed, D-minor pads, washed breaks, melodic lead
- New libraries registered+scanned+analyzed (sidecars): 15 dnb packs (`/tmp/dnb_lib_ids.json`,
  `timbre-lib/analyze_dnb.py`, 160 sidecars) + 5 ragga packs (`/tmp/ragga_lib_ids.json`,
  `timbre-lib/analyze_ragga.py`, 142 sidecars; mp3 pipeline verified; `_Reggaeton and Dancehall`).
- All packs clustered (`cluster_library` → breaks/drums, dark rumble bass, gritty bass shots, tops,
  hats/shakers; ragga: vocal/phrase, riddim, drum, 808 clusters).
- MCP launcher fixed (see B1) — 193 hdaw tools live; driver scripts `analyze_dnb.py`/`analyze_ragga.py`
  committed in `timbre-lib/` (uncommitted; see §4).

## 1. Jungle/DnB composition knowledge — fold into `docs/jungle-dnb-composition-guide.md`

### 1.1 Style canon (Wikipedia "Jungle music", verified applicable)
- Roots: early-90s UK rave + Jamaican sound-system culture; out of breakbeat hardcore.
- Sound: rapid breakbeats, heavily syncopated percussive loops (the AMEN break above all),
  deep basslines, samples + synth FX, dub/reggae/dancehall melodies & vocals, hip-hop/funk.
  Reynolds: "rhythm as melody", "bass quake pressure", "postmodern dub on steroids".
- Subgenres to target: ragga jungle (reggae/dancehall vocals, organ, 808s), jump-up (bass-line-led),
  ambient/intelligent (LTJ Bukem: atmosphere over re-sequenced breaks), darkcore (industrial stabs).
- Classic-reference texture: "Valley of the Shadows", "Original Nuttah", "Burial", "The Helicopter Tune".

### 1.2 Verified recipes (all measured in rendered audio)
- **Core amen jungle** (`jungle_amen_fmin_v1`): 175 BPM F minor; amen 1-bar loops per bar (different
  loop/section; finale double-layers a +2-semitone-pitched loop via rootNote 62); sparse kick on 1 of
  every 2 bars (1+3 in finale); snare on 2/4; offbeat rim pings; 16th hats; sub offbeat 8ths on
  F-Eb-Db-Eb with octave drops at turnarounds; rumble drop-swells; per-beat triangle pump 0.68-1.0 on
  the sub's built-in Volume lane (43.7% per-beat RMS depth); HP-sweep + cutoff-sweep + breakdown
  close-then-open envelopes via `generate_automation_envelope` (sCurve) + `set_automation_points`
  (always `set_automation_enabled=true`). Arc (RMS): intro .020 / build .102 / mainA .121 / mini .036
  (filtered amen + quiet hats, NOT silent) / mainB .122 / breakdown .032 (true break 0.27x) / fin .144
  (loudest 1.19x). Kick prominence rule from psytrance v2: when bass lifts an octave into 147-320 Hz it
  masks the kick; mainB stays in the D2 register + kick Volume-lane boost (1.12-1.22x) = kick stays king.
- **Ragga jungle** (`jungle_ragga_v1`): same core + reggae organ STAB = Rasta Vibrations 6 organ STEM
  (136 BPM Gmn) as one-shot, transposed −2 (Gmn→Fmn: rootNote 55, send 53), skank 2-4 stabs/bar with
  dub delay (fb .5 wet .32) + delay-wet automation (paramID 100+slot*100+idx; slot ordering matters);
  CHANTS (Ragga Stashkit) as drop toasts; 808s as sub accents; REGGAE RIDDIM BREAKDOWN: Rasta
  organ+bass+drums+perc stems layered at their NATURAL 136 BPM = 5.148 beats/bar @175 (quantize by stem
  bar length, not the 175 grid) → true break 0.23x. Organ density bug: per-BEAT offsets = 1558 stabs
  (mush) → per-BAR skank = 420 (correct).
- **Atmospheric jungle** (`jungle_atmos_v1`): 175 BPM D minor, Dm-Bb-F-C per 4-bar phrases; dub-riddim
  bed (`Peace Love And Unity`) as intro/breakdown/finale wash; Antinomy Atmosphere pads (D3 + third +
  fifth, chorus+reverb+eq) with slow filter swell; Rasta lead stem Gmn→Dmn (−5) as floating melody with
  reverb+.6+delay-wet; Vixage `ClassicVix Dmin` melodic bass (octave lift in mainB); BW_D bass one-shot
  as sub; amen at gentle velocities (48-78) with HEAVY reverb (mix .55 size .38) = "distant drums";
  gentler pump (0.82-1.0, 0.5-beat res). Arc: .013/.069/.118/.026/.116/.016 (deepest break 0.135x)/.121;
  finale climaxes by BAND ENERGY (11-37% above mainA — sub/mid/high) not RMS (1.02x) — atmospheric
  genre; don't chase RMS.
- **Faders** (starting point, all three): breaks .74-.85, sub .62-.75, bass .68, hats .62-.78,
  snare .64-.85, pads .45-.72, dub bed .55, chants .75, 808 .70.
- **RootNotes by role** (the #1 mixing mistake otherwise): kick 36 (no pitch), breaks/one-shots 60,
  F-key bass 41, D-key bass 38, D pad 50, Gmn stems 55 (send 53 for Fmn, 50 for Dmn), BW-D shot 48 (send
  38 = sub octave).

### 1.3 Library/toolchain workflow (with the traps that WILL recur)
1. `analyze_<theme>.py` driver (lib_analyze + llm_stage, strided, sidecars ON) → mp3 fine.
2. Register via MCP `add_library` (engine running: registration via MCP, NOT scripts — registry clobber).
3. `scan_library` → **RE-SCAN AFTER analysis** (sidecars land after the first scan; tags only appear on
   the re-scan). Confirm via `search_library` returning `tags`.
4. **Future-mtime trap**: copied packs can carry future file mtimes (2027-12-29); the incremental scanner
   only applies a sidecar when the sidecar is NEWER than the audio → tags silently never ingest. Fix:
   `os.utime(sidecar, future)` then re-scan (see B2).
5. `cluster_library`/`related_samples` are index-scoped: un-scanned libraries are invisible (register +
   scan first).
6. Units/cheat-sheet deltas for this genre: add_midi_clip/add_notes beats, clip-local note starts;
   export_audio seconds, full-project only; internal-FX params via `set_internal_fx_param` are REAL units
   (EQ Frequency Hz, Gain dB; Delay Time seconds; Chorus/Flanger Rate Hz; reverb mix 0 /~size 2/);
   automation lane values NORMALIZED 0..1; per-track built-in Volume/Pan/Mute lanes (1/2/3) exist — pump
   automation goes on the built-in Volume lane via `set_automation_points … mode:"replace"`.
7. MCP tool names: engine's `mcp` binding can be shadowed → `import rlm.mcp as mcp`; `add_track` returns
   text `trackId=N`, `list_tracks` returns `id` field.

## 2. HDAW feature gaps — what would eliminate the manual placement/automation work

Prioritized; each removes a concrete step we did BY HAND in every render.

### P1 (style-defining; hit every track)
1. **MCP LFO exposure — NONE exists.** The engine has per-track LFOs (waveform/rate/depth/target) but
   the 193-tool MCP surface has zero `add_lfo`/`set_lfo_param` tools. Every pump (per-beat duck) and
   wobble was hand-computed as triangle automation points (tri_points() in ~15 lines each time). Add:
   `add_lfo {trackId}` + `set_lfo_param {trackId, lfoIndex, param, value}` mirroring the command layer.
2. **Internal filter TYPES.** The internal `eq` is a PEAK filter (params Frequency/Q/Gain dB) — no
   lowpass/highpass. "Filtered break" breakdowns and LP-sweeps are impossible: automating EQ Frequency
   does NOT attenuate (see B3). Add filter modes (lowpass/highpass/band) + a cutoff param, or a dedicated
   `filter` FX (cutoff + mode + resonance) that automation lanes drive for honest LP/HP sweeps.
3. **Tempo-synced delay division.** Internal delay `Delay Time` is SECONDS (0.01-5 s, default 0.5) — a
   dotted-8th dub at 175 = 0.1286 s must be set by hand per tempo. Add division presets
   (syncToTempo + numerator) so dub delays are one param.
4. **`add_notes` clip-local trap.** Note starts are clip-local (startBeat); every score built with
   absolute section times must subtract clip start before pushing (we build one clip spanning the whole
   track to dodge it). Add a `relative:false` mode (or `clipStart`) so absolute-beat scores are
   accepted and converted.

### P2 (speed)
5. **Break chopper/composer.** `detect_sampler_slices` + `trigger_sampler_slice` exist but there is no
   "make a jungle break pattern" — choosing slice order, ghost fills, 16th/32nd grids, or a classic
   break edit (drop-the-first-beat). One tool: `composition.choppedBreak {clipId|filePath, bars, style
   (amen/2step/halftime/random), sliceCount, velocityRange}` → notes per slice.
6. **Pattern-placement helper.** `analyze_midi_file` returns bar-aligned patterns; placing them across a
   section (tiling, reversal, octave steps, velocity scaling) is hand-Python (`place_patterns`).
   Suggest `composition.placePatterns {patternIds|clipId, startBeats[], octave[], velocityShift[]}`.
7. **Section-aware arrangement + automation preset packs.** `generate_arrangement` lands parts at beat 0
   and is 120-BPM-oriented; we hand-built sections (intro/build/mainA/mini/mainB/brk/fin) with
   per-section fader/pump/sweep envelopes every time. A section-map + preset envelope bank (pump, macro
   sweep, breakdown open/close, riser) set would remove most automation scripting.
8. **Key/scale helpers.** Every score hand-rolled root sequences in key. Add `composition.scaleDegrees
   {key, mode, root}` returning MIDI degrees + have `analyze_midi_file` actually RETURN key/scale/bpm
   (currently `key`/`scale`/`bpm` are None; only `sourceBpm` present — B5).
9. **Server-side mix report.** `verify_part` is instrument-focused; we re-analyze every render in Python
   (RMS arc per section, band energies, per-beat pump depth, kick prominence ratio). Add
   `mix.report {path, sectionMap}` returning those metrics (with genre-aware gates).

### P3 (polish/contract)
10. `add_notes` duplicate guard: accepted 244 duplicate 8th-note hats (4x triggers, louder mini-break —
    see B7); warn or dedupe same-pitch/same-start notes.
11. `add_track` text (`trackId=N`) vs `list_tracks` JSON (`id`) — make add_track return JSON with
    `trackId` for consistency.
12. `related_samples` returns neighbor names/paths but not their library id — include `libraryId` for
    usable cross-library pick flows.

## 3. Bugs found this session

| # | Bug | Status / workaround |
|---|---|---|
| B1 | `mcp-launch.bat` crash-capture leaked into MCP stdio: `Write-Host` status + **ProcDump UTF-16 banner** on stdout broke every MCP client ("Failed to parse JSONRPC"). | **FIXED** (this session): capture now opt-in via `HDAW_CRASH_CAPTURE=1`, status line to stderr; verified clean handshake + capture still works + gtest runner unchanged. Commit pending (§4). |
| B2 | TimbreLib sidecars silently never ingested for copied packs with FUTURE mtimes: incremental scan reuses an entry unless sidecar mtime > audio mtime → `applyTimbreSidecar` never runs → search/cluster lack tags. | **FIXED** (`FileLibraryManager` + `file_library_test`): reuse predicate now also rescans when a `.timbre.json` sidecar exists but the stored entry has no sidecar-derived data (`entryHasTimbreData`); regression pair `FutureMtimeAudioWithOlderSidecarIngestsTags` + `SidecarAddedAfterScanWithOlderMtimeIngestedOnSecondScan` (fail-before/after proven). Workaround no longer needed. |
| B3 | "Filtered break" is an illusion: automating internal EQ Frequency (peak filter) does not attenuate — the mini-break stayed loud (0.085 RMS) until duplicate hats were removed (0.031). It only moves a boost/cut center. | **FIXED** (P1-2): internal `filter` FX (low/high/bandpass, automatable Cutoff); measured 36.9 dB LP attenuation + 91 dB sweep swing; `InternalFx.Filter*` tests. |
| B4 | `add_automation_lane` with paramID 1 fails `lane name or paramID already exists` because every track auto-has Volume/Pan/Mute lanes. | Workaround: target the built-in lane via `set_automation_points {lane:"Volume"}` + `set_automation_enabled`. Improve error to say "built-in lane exists". |
| B5 | `analyze_midi_file` top-level `key`/`scale`/`bpm` are null and patterns lack `id`. | **FIXED** (P2-4): top-level `key`/`scale`/`bpm`/`scaleType` + stable `p<index>` pattern ids; `scale_note` degree helper added. |
| B6 | Headless MCP `sampler_get_state.hasSound=false` for everything, incl. samples that render/export fine (known §2 quirk). mp3 samples load + export fine. | Not a bug; documented — never use hasSound as a render predictor. |
| B7 | `add_notes` accepts duplicate notes (same beat/pitch) — generated 4x-stacked hats by looping offsets per beat; louder than intended. | Workaround: guard note-gen loops per bar. Feature: dedupe/warn (P3.10). |

Also noted (not bugs): `set_lfo_param` does not exist in MCP (P1.1); `search_library` tag confirmation
requires the post-analysis re-scan (workflow §1.3 #3); break loops at 175 (amen pack) vs 136 (ragga
stems) mixing is fine when quantized per-source-bar.

## 4. Suggested follow-ups (priority order)

1. **Write `docs/jungle-dnb-composition-guide.md`** from §1 (mirror `docs/psytrance-composition-guide.md`),
   linking the three .wav/.hdaw deliverables as reference renders.
2. **File P1 feature requests as a plan** (`docs/plans/`): MCP LFO tools, internal filter types +
   cutoff automation, tempo-synced delay divisions, `add_notes` absolute-beat mode.
3. ~~Engine fix B2 (sidecar re-ingest robustness)~~ **DONE** (committed with `file_library_test` regressions; see B2)
   (`FileLibrary.*`: place future-mtime audio + newer sidecar → tags present after scan; and the
   tags-absent-but-sidecar-exists re-ingest case).
4. **Jungle regression test** (mirror `psytrance_composition_stress_test.cpp`):
   `JungleComposition.NewPacksLongRender` — amen + sub + organ recipe at 175 with save/load + renders,
   `HDAW_KEEP_JUNGLE_RENDERS=1` keeps deliverables.
5. **Commit** the uncommitted session work: `timbre-lib/analyze_dnb.py`, `analyze_ragga.py`,
   `mcp-launch.bat` (B1 fix), and this handoff.
