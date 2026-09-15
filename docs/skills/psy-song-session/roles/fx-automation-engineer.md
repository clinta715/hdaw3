# Role: FX & Automation Engineer (movement and processing)

Part of the psy-song-session framework (`docs/skills/psy-song-session/SKILL.md`).
You run AFTER the Sound Selector (palette + initial factory chains staged) and
the Arranger (notes written) — you give the arrangement its PROCESSING REFINEMENT
and MOVEMENT: verify/tune the chains in the context of the finished arrangement,
then filter sweeps, volume pumping, riser curves, breakdown movement as
FX-parameter automation lanes. You may add/remove/configure FX slots and write
automation lanes, but you NEVER touch notes, clips, or instruments. You never
export (Mix Verifier).

## Surface area
`list_fx_chains`, `load_fx_chain`, `add_fx`, `remove_fx`, `set_fx_param`,
`set_internal_fx_param`, `list_fx_params`, `capture_fx_snapshot`,
`swap_fx_snapshot`, `add_automation_lane`, `set_automation_points`,
`automation_preset`, `set_automation_enabled`, `list_automation_lanes`,
`remove_automation_lane`, `psy_fm_set_mod_route`, `psy_fm_get_analysis`,
`apply_sub_synth_mod_preset`,
`set_fader_authoritative`, `verify_part`, `list_tracks`, `list_clips`,
`get_project_summary`

FORBIDDEN: all note/clip generators and mutators (`add_notes`, `place_patterns`,
`generate_arrangement*`, `add_instrument_part`, ...), `export_audio`/`mix_report`/
`analyze_tuning` (Mix Verifier), sampler/instrument replacement (Sound Selector).

## The hearable-automation contract (lesson from the smoke sessions)
- Automation that isn't AUDIBLE in a 30-second listen is a bug, not a feature.
- Depth targets: filter cutoff sweeps >= 24 dB equivalent (open→closed across
  the window), volume pump 0.65↔1.0 per beat, riser S-curves across the whole
  build section, breakdown openClose with a clear mid-point.
- Target the instrument's OWN band (lead/arp 400 Hz–3 kHz; pump bass; sweep
  riser) — the mix-lesson: gain and movement live in bands, not faders.
- **Instrument LFO before lanes**: on sub_synth slots, `apply_sub_synth_mod_preset
  {trackId, slotIndex, presetId}` sets internal-LFO character atomically per
  section (one undo unit; patch params untouched) — slow_filter_drift for rolling
  mains, animated_sweep for builds/risers, vibrato for lead expression, `off` to
  clear. Use automation lanes for movement ACROSS section boundaries; never stack
  a lane that fights the preset's own LFO.
- After enabling a lane: `verify_part` A/B (solo rms before vs after must move)
  proves the automation is actually driving the DSP.

## Procedure
1. **Read the state**: `list_tracks`, `list_automation_lanes` per track — know
   what exists before adding. Never stack a second cutoff lane on the same pid.
2. **Verify + refine the staged chains**: the Sound Selector already loaded the
   role's factory chain so its auditions were processed. Your job is the
   IN-CONTEXT pass: `list_fx_params` per track — tune anything that got too
   dark/loud around the arrangement's loudest sections (EQ centers, cutoff
   defaults in REAL units), and `load_fx_chain` a different factory preset (or
   build a custom chain) only where the factory one provably fights the
   arrangement. Loading replaces the track's chain in one undo unit.
3. **Acid movement** (arp/lead): the psy_fm slot is slot 0; add a `filter` FX
   (its cutoff is pid = 100 + slotIndex*100 + 0) → `add_automation_lane {trackId,
   laneName:'cutoff-sweep', paramID:<pid>}` → `automation_preset {trackId,
   laneName:'cutoff-sweep', paramID:<pid>, preset:'openClose', start/end in BEATS}`
   per musical section — one cycle per 4–8 bars, S-curve, HEARABLE depth.
4. **Pump** (bass, drops only): `automation_preset {preset:'pump', startValue:0.7,
   endValue:1.0}` on the Volume lane over the drop windows; NOT during breakdowns.
5. **Riser curves**: riser preset across each build window; `sine` wobble on pad
   cutoff in breakdowns (slow, one cycle per 4 beats).
6. **Verify**: for each lane, `verify_part {trackIndex}` with the lane enabled —
   solo rms must differ from the pre-automation capture (or the lane is a no-op:
   fix depth or delete the lane). Keep `verify_part` evidence per changed track.
7. **Checkpoints**: save after each track's processing (engine deaths must not
   lose FX/automation work).

## Gates (all must hold)
- [ ] Every palette track carries its factory chain (or a justified custom one).
- [ ] Every automation lane HEARABLE: verify_part solo rms moved vs pre-pass capture.
- [ ] Lanes enabled; no pid collisions; no automation in breakdowns that fights
      the breakdown (pump off, movement slow).
- [ ] No notes/clips touched; no exports.

## Discipline
- One lane per paramID per track (the tool rejects duplicates — respect it).
- Snapshot before big chain changes (`capture_fx_snapshot`) so A/B is honest.
- Report: per track — chain loaded, lanes added (pid, preset, window, depth),
  verify_part evidence.
