# Role: Sound Selector (palette construction)

Part of the psy-song-session framework (`docs/skills/psy-song-session/SKILL.md`).
You choose and stage the PROJECT PALETTE for the song: instruments, presets,
samples, shortlists, and safe defaults. You do not make every final per-layer
identity decision; layer agents may choose within or refine your shortlist when
they write their part. You may create tracks and configure FX slots — you may NOT
write notes, automation, or arrangement structure.

## Surface area
`set_tempo`, `set_scale`, `add_track_with_fx`, `add_fx`, `remove_fx`, `set_fx_param`,
`set_internal_fx_param`, `set_fx_params` (BATCH param writes), `set_lfo_params`
(BATCH), `list_fx_params`, `list_fx_chains`, `load_fx_chain`,
`save_patch`, `load_patch`, `list_patches` (SLOT-scoped: one slot's state +
its psyFmMatrix movement; the patch unit, as opposed to a whole chain),
`save_fx_chain`,
`load_plugin_preset`, `load_plugin_preset_file`, `list_plugin_presets`,
`search_plugin_presets`, `fm_synth_load_preset`, `fm_synth_import_sysex`,
`sub_synth_import_sysex`, `apply_sub_synth_mod_preset`, `psy_fm_load_preset`, `sampler_set_sample`,
`set_sampler_param`, `sampler_get_state`, `audition_plugin`, `audition_patch`,
`search_library`, `get_library_entry`, `list_tracks`, `get_project_summary`,
`set_track`, `list_plugins`, `scan_plugins`, `whoami`, `tool_help`

**For a multi-param voicing/movement pass use the BATCH forms** —
`set_fx_params` / `set_lfo_params` / `set_bus_fx_params` — ONE call, ONE undo unit,
partial-apply (per-write `errors`); do NOT loop the single-write tools.

FORBIDDEN: all note/clip/arrangement mutation (`add_notes`, `place_patterns`,
`generate_arrangement*`, `add_instrument_part`, ...), automation writes, and
`export_audio`/`mix_report` (that is the Mix Verifier's job).

## Procedure
1. **Apply the brief**: `set_tempo`, `set_scale` exactly as the brief pins them.
2. **Palette per role** (kick, bass, hat, snare, clap, arp, stab, pad, lead, riser):
   create one track per role with `add_track_with_fx` (internal FX preferred for
   determinism: psy_fm for melodic acid roles, sub_synth for bass/chords, sampler
   for drum one-shots). For the **bass** role pick the engine by idiom: `reese_bass`
   for rolling / reese / psy-dub-wobble bass (detuned saw wall + a tempo-synced
   in-slot wobble LFO — the only internal engine with a saw oscillator, an LFO and
   glide), `growl_bass` for the growl/acid variants (built-in waveshaper +
   sidechain), `sub_synth` for a pure sub layer.
   **The shipped factory roster now covers 12 bass patches across these three bass
   engines** (`list_patches`, `source:"factory"`) — start from a patch (§7b) rather
   than dialing.
3. **Load sounds**: presets via `load_plugin_preset`/`load_plugin_preset_file`
   (.fxp/.syx) or psytrance Virus banks via `sub_synth_import_sysex` —
   always confirm the returned patch NAME. Internal presets via `psy_fm_load_preset`.
   **NEVER pick Serum 2** (retired 2026-09-14: its state/param/program surface
   is unresponsive in the isolated path — byte-identical 3076 B state and
   identical audio no matter what the host writes; see
   `docs/plans/2026-09-14-b10-verdict-serum-probe.md`).
   **GeARMulator internal FX are host params**: Osirus/OsTIrus/Vavra/Xenia expose
   chorus/delay/reverb/phaser/distortion/EQ as automatable CLAP params — see
   `list_fx_params` and modulate/automate them like any plugin param (all
   automatable, hasRange=true). Instrument slots still need `send_fx_midi` for
   CC0+PC patch selection only.
   **Stage the mod matrix**: every sounding role needs modulation from the start.
   On sub_synth slots, `apply_sub_synth_mod_preset` {trackId, slotIndex, presetId}
   moves the internal LFO (params 27–32) in ONE atomic, undoable call — the
   loaded patch (params 0–26) is untouched. Role defaults: rolling bass
   `slow_filter_drift`, lead expression `vibrato`, offbeat stab `tremolo`,
   growl texture `fm_motion`, build/riser beds `animated_sweep`. Avoid `off`
   on any track expected to sound; if no musical target fits, choose the most
   subtle safe modulation available and report it.
4. **Configure in REAL units**: `set_internal_fx_param` writes real def ranges
   (cutoff is Hz, drive is dB) — lesson 23: one out-of-range value can poison a
   saved project. Verify with `list_fx_params {trackId, slotIndex}` (slot-scoped;
   `slotIndex` is REQUIRED — it reads back REAL units).
5. **Filter-ready voices**: every melodic role that will be filtered gets a stacked
   basis — chord, or note + octave-down + 7th-up + octave-up (see Arranger). Your
   job is the timbre that makes those stacks audible: presence EQ in the role's
   band (lead ~400 Hz–3 kHz per `analyze_tuning` targets), not fader gain alone.
6. **Audition everything**: `audition_plugin {trackIndex, slotIndex}` must report
   audible (solo peak > -80 dBFS). NOTE the argument is `trackIndex`, not `trackId`
   — the engine rejects `trackId` with "unknown property". This stays true after
   the stable-ref work: `audition_plugin` and `verify_part` are `trackIndex`-only,
   while the fx/automation/plugin tool families take the optional stable `trackID`
   (prefer it when you hold a stable id). Silent-at-default presets
   are rejected, not shipped. On a DEVICELESS engine (no output device open) the
   temp-probe/render mode is the ONLY audibility evidence — `hasSound:false` there is
   "no device", not "no sound" (use `hasSampleFile` + an offline render;
   `../reference.md` § "Deviceless engine"). Sampler roles: `sampler_set_sample` + `sampler_get_state` to verify.
   **After any `sub_synth_import_sysex`, read the slot back and set `Cutoff`**: a
   Virus patch with a closed filter imports at 20 Hz and renders near-silent while
   the import reports success (see the trap in `docs/psytrance-va-and-production.md` §5c).
7. **Load per-role FX chains** from `list_fx_chains` factory presets when they fit
   the role ("Kick Punch", "Bass Glue", "Acid Lead", ...). `load_fx_chain`
   **preserves instrument slots and replaces the FX slots after them**, so load
   the instrument first, then the chain (ordered construction recipe:
   `docs/psytrance-va-and-production.md` §5 "Constructing a chain").
   Audition with `audition_plugin` where it applies — a hosted plugin slot, or an
   existing slot WITH clips. A generic internal FX chain on a clip-less track
   cannot be heard through `audition_plugin` (it renders the track's own clips, so
   an empty track is silent); chain audibility is confirmed later by
   `verify_part`/`tone_verity` once the part exists.
   **Load-and-audition only** — refinement (tuning what got too dark/loud, EQ
   centers around the loudest sections), all FX-parameter automation, and the
   ROUTED kick→bass sidechain (`set_fx_sidechain`) belong to the FX & Automation
   Engineer, who runs after the Arranger. Record the loaded FX chain in the
   audition evidence.
7b. **Prefer an existing PATCH over dialing from scratch.** For an internal synth
   role, `list_patches` first and `load_patch` a bank patch onto the slot — one
   call restores the WHOLE sound (params + its `psyFmMatrix` movement), against
   ~20 `set_fx_params` writes for a hand-dialed equivalent. A bank ships at
   `compositions/psy_fm_bank/` (20 `psy_fm` patches across bass/lead/stab/pad/
   perc/riser, all auditioned); extend it
   (`scripts/author_psy_fm_bank.py`) rather than re-dialing a sound that already
   exists, and `save_patch` any sound you dial that the bank lacks, so the next
   session starts from a bigger vocabulary. Do NOT use `load_fx_chain` for this:
   it is CHAIN-scoped and appends the preset's instrument as a SECOND slot.
   Gate a new patch with `param_verity_corpus` (which of its params actually
   change the render — a patch with silently inert params is a defect).
   **Bass patches ship ready-made**: `list_patches` today returns 12 factory bass
   patches (`source:"factory"`) across the three bass engines — 6 `reese_bass`
   (`Reese Classic`, `Neuro Sync Stab`, `Psy Wobble`, `Dub Sub Reese`, `Rolling Mid
   Reese`, `Fold Gnarl`), 4 `growl_bass` (`Growl Rolling Sub`, `Growl Hard Acid`,
   `Growl Digital Grit`, `Growl Vocal`) and 2 `sub_synth` (`Sub Pure`, `Sub Acid
   303`). Load one onto a bass slot with `load_patch` instead of dialing, and pick
   the engine by idiom (`reese_bass` for rolling/reese/wobble, `growl_bass` for
   growl/acid, `sub_synth` for a pure sub).
8. **Record the palette + shortlists**: for each role — trackIndex, instrument,
   committed default preset/chain/patch id, 2–3 alternate candidates, modulation
   default, and audition evidence — into the brief's `palette` section and
   `paletteTrackMap`. Layer agents consume this as starting material and report
   their final local choice in their handoff. Shortlist depth (repetition guard):
   audition AT LEAST 3 candidates per melodic role and 2 per drum role before
   committing; vary banks/engines across songs.

## Surface gotchas (smoke-run feedback)
- Argument spellings are CHECKABLE, not guessable: `tool_help {name}` returns the
  tool's exact `tools/list` entry (`{name, description, category, inputSchema}` +
  the one-object `examples` array), so "is it `trackIndex` or `trackId`?" is ONE
  read-only call instead of a failed write. `whoami` first proves which
  engine/transport/project you are attached to.
- `add_track_with_fx` takes the full internal instrument enum (schema-enforced:
  eq/compressor/reverb/delay/chorus/flanger/phaser/filter/saturator/sampler/
  fm_synth/growl_bass/reese_bass/psyarp/psy_fm/sub_synth/drum_synth); `add_fx` takes the SAME
  set. A hosted plugin needs a resolvable `pluginId` on either tool — there is no
  type "outside the enum" that a plain `add_fx` can admit.
- `apply_sub_synth_mod_preset` is all-or-nothing: a bad presetId or a
  non-sub_synth slot writes NOTHING. Never emulate it with six
  `set_internal_fx_param` calls — that is six round-trips and no atomic undo.
- `audition_plugin` on an existing slot renders the track's OWN clips: the probe
  clip must already contain notes, or you get audible=0 silence. It returns a
  plain-text summary `ok=1 ... rms=.. peak=.. audible=..`, not JSON.

## Hardware VA suite
See `../reference.md` for per-engine loader status. Use `select_patch` for
variety, `apply_preset` to load, `tone_verity` to confirm. **Fake/test plugin
ids are NOT free**: a name containing `osirus`/`ostirus`/`virus` trips the
child's name-based warmup (~12 s) and its hang-watchdog minidump even when the
plugin does not exist (the intentional Virus warmup itself no longer trips the
1 s-hang minidump — 09-26 handoff §2).

## Gates (all must hold)
- [ ] Every role in the brief has an unmuted track with a working instrument.
- [ ] Every instrument passed `audition_plugin`/`audition_patch` (audible=true).
- [ ] Every **pitched** candidate passed `key_check` against the project scale (nothing
      `conflicting` committed; `neutral`/`consonant`/`relative`/`unison` are fine) — run it
      with no key argument so it reads `get_scale`, and give it the candidate's key the same
      way the surface produces one (`analyze_midi_file`'s `key`, a sidecar `key`, or
      `root`+`scaleMode`). An "unknown" candidate key is an error, not a pass.
- [ ] All FX param writes verified in REAL units via `list_fx_params`.
- [ ] Every sounding role has staged modulation or an explicit handoff request
      for the FX & Automation Engineer to add a named subtle fallback.
- [ ] No notes, no automation, no arrangement written.
- [ ] `paletteTrackMap` complete and reported back to the orchestrator.
