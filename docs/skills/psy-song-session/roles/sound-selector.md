# Role: Sound Selector (palette construction)

Part of the psy-song-session framework (`docs/skills/psy-song-session/SKILL.md`).
You choose and stage the SOUNDS for the song: instruments, presets, samples. You
may create tracks and configure FX slots — you may NOT write notes, automation,
or arrangement structure.

## Surface area
`set_tempo`, `set_scale`, `add_track_with_fx`, `add_fx`, `remove_fx`, `set_fx_param`,
`set_internal_fx_param`, `list_fx_params`, `list_fx_chains`, `load_fx_chain`,
`load_plugin_preset`, `load_plugin_preset_file`, `list_plugin_presets`,
`search_plugin_presets`, `fm_synth_load_preset`, `fm_synth_import_sysex`,
`sub_synth_import_sysex`, `apply_sub_synth_mod_preset`, `psy_fm_load_preset`, `sampler_set_sample`,
`set_sampler_param`, `sampler_get_state`, `audition_plugin`, `audition_patch`,
`search_library`, `get_library_entry`, `list_tracks`, `get_project_summary`,
`set_track`, `list_plugins`, `scan_plugins`

FORBIDDEN: all note/clip/arrangement mutation (`add_notes`, `place_patterns`,
`generate_arrangement*`, `add_instrument_part`, ...), automation writes, and
`export_audio`/`mix_report` (that is the Mix Verifier's job).

## Procedure
1. **Apply the brief**: `set_tempo`, `set_scale` exactly as the brief pins them.
2. **Palette per role** (kick, bass, hat, snare, clap, arp, stab, pad, lead, riser):
   create one track per role with `add_track_with_fx` (internal FX preferred for
   determinism: psy_fm for melodic acid roles, sub_synth for bass/chords, sampler
   for drum one-shots).
3. **Load sounds**: presets via `load_plugin_preset`/`load_plugin_preset_file`
   (.SerumPreset/.fxp/.syx) or psytrance Virus banks via `sub_synth_import_sysex` —
   always confirm the returned patch NAME. Internal presets via `psy_fm_load_preset`.
   **Stage the mod matrix**: on sub_synth slots, `apply_sub_synth_mod_preset`
   {trackId, slotIndex, presetId} moves the internal LFO (params 27–32) in ONE
   atomic, undoable call — the loaded patch (params 0–26) is untouched. Role
   defaults: rolling bass `slow_filter_drift`, lead expression `vibrato`,
   offbeat stab `tremolo`, growl texture `fm_motion`, build/riser beds
   `animated_sweep`, static/reference `off`.
4. **Configure in REAL units**: `set_internal_fx_param` writes real def ranges
   (cutoff is Hz, drive is dB) — lesson 23: one out-of-range value can poison a
   saved project. Verify with `list_fx_params` (reads back REAL units).
5. **Filter-ready voices**: every melodic role that will be filtered gets a stacked
   basis — chord, or note + octave-down + 7th-up + octave-up (see Arranger). Your
   job is the timbre that makes those stacks audible: presence EQ in the role's
   band (lead ~400 Hz–3 kHz per `analyze_tuning` targets), not fader gain alone.
6. **Audition everything**: `audition_plugin {trackId, slotIndex}` must report
   audible (solo peak > -80 dBFS). Silent-at-default presets are rejected, not
   shipped. Sampler roles: `sampler_set_sample` + `sampler_get_state` to verify.
7. **Load per-role FX chains** from `list_fx_chains` factory presets when they fit
   the role ("Kick Punch", "Bass Glue", "Acid Lead", ...).
8. **Record the palette**: for each role — trackIndex, instrument, preset name,
   audition evidence — into the brief's `palette` section and `paletteTrackMap`.

## Surface gotchas (smoke-run feedback)
- `add_track_with_fx` enum EXCLUDES `sub_synth` — create a generic track
  (`fxType:'filter'`) then `add_fx {fxType:'sub_synth'}` + `remove_fx` the carrier.
- `apply_sub_synth_mod_preset` is all-or-nothing: a bad presetId or a
  non-sub_synth slot writes NOTHING. Never emulate it with six
  `set_internal_fx_param` calls — that is six round-trips and no atomic undo.
- `audition_plugin` on an existing slot renders the track's OWN clips: the probe
  clip must already contain notes, or you get audible=0 silence. It returns a
  plain-text summary `ok=1 ... rms=.. peak=.. audible=..`, not JSON.

## Gates (all must hold)
- [ ] Every role in the brief has an unmuted track with a working instrument.
- [ ] Every instrument passed `audition_plugin`/`audition_patch` (audible=true).
- [ ] All FX param writes verified in REAL units via `list_fx_params`.
- [ ] No notes, no automation, no arrangement written.
- [ ] `paletteTrackMap` complete and reported back to the orchestrator.
