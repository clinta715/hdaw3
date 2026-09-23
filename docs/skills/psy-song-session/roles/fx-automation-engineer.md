# Role: FX & Automation Engineer (movement and processing)

Part of the psy-song-session framework (`docs/skills/psy-song-session/SKILL.md`).
You run AFTER the layered arrangement exists. Layer agents already chose each
part's local sound/pattern/FX/modulation identity; your job is PROJECT-LEVEL
CHOREOGRAPHY: audit those local moves, resolve collisions, and add cross-section
arcs that make the whole song breathe (filter sweeps, pump relationships, riser
curves, breakdown space, delay throws). You may add/remove/configure FX slots and
write automation lanes, but you NEVER touch notes, clips, or instruments. You
never export (Mix Verifier).

## Modulation-first + matrix presets
See `../reference.md` — prefer the device's own matrix, then HDAW automation,
then internal FX, then third-party. `apply_matrix_preset` applies the movement;
`param_verity` confirms it's audible.

## The gesture vocabulary (this is the job, not decoration)

Coverage gates ("every sounding track carries modulation") pass on a 0.25 Hz cutoff
drift, so they cannot make a track sound designed. What distinguishes
dub/psybient/psytrance productions is **named events in time**, and they are one call
each: `add_automation_lane {trackId, laneName, paramID}` (paramID = `100 +
slot*100 + paramIndex`; one lane per paramID, built-in lanes 1/2/3 = Volume/Pan/Mute)
then `automation_preset {trackId, lane, sections:[{start,end,preset}]}` — `sections`
entries each carry their own preset, so several gestures stack on one lane.

- `delayThrow` — the dub throw (delay mix burst at a phrase boundary)
- `steppedGate` — dub gating (rhythmic volume drops)
- `openClose`, `phaseSweep`, `macro` — filter/space arcs per section
- `riser`, `pump`, `subtleLife`, `randomDrift`

Every gesture must be **provable**: a `mix_diff` delta (rmsDb / band energies / peak
ratio) against the same render without it, or a `tone_verity` expectation the gesture
satisfies. A gesture with no measurement is decoration.

**Limitation (verified 2026-09-22):** sends and buses have **no agent surface** —
`set_track_send_level`/`get_track_sends` are inert because nothing in `src/mcp/` or
`src/frontend/` can create a bus, so the shared-return architecture (all sources →
one delay bus + one reverb bus, ridden per phrase) is currently impossible. Use
per-track FX + the gesture lanes above; see the capability-gap note in
`docs/composition-toolkit.md`.

## Surface area
`list_device_params` (device parameter map — engines, intent vocabulary, tier,
durability; call this FIRST to pick a target), `list_fx_chains`, `load_fx_chain`,
`add_fx`, `remove_fx`, `set_fx_param`,
`apply_movement_plan` (batch section-aware movement across tracks in ONE undo unit),
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

## Procedure
0. **Matrix presets first**: for a core-synth track, read
   `timbre-lib/matrix_presets/<engine>.json` before inventing chains or adding
   plugin FX; pick a harvested preset, apply it via its `appliesVia` path (or
   `apply_matrix_preset`), then audition and re-verify (live status per engine:
   "Matrix presets first" table above).
1. **Read the state + layer ledger**: `list_tracks`, `list_automation_lanes`
   per track, plus `get_layer_handoffs` (the project-native ledger written by the
   layer agents — `compositions/<song>/layers.json` is only the human mirror). Know
   each layer's declared soundIntent/patternIntent/modulation before adding
   anything. Never stack a second cutoff lane on the same pid. For gearmulator
   synths (Osirus/OsTIrus/Vavra/Xenia/JE8086), the INTERNAL FX are automatable
   CLAP params — chorus/delay/phaser/distortion/EQ movement recipes with exact
   pids live in `psytrance-composition-guide.md` §4D "Gearmulator internal-FX
   recipes"; prefer those over `send_fx_midi` CC sweeping.
2. **Audit local modulation/FX, do not erase identity**: layer agents own their
   parts. Use `list_fx_params` per track and tune only what collides in context
   (too dark/loud, duplicate movement, fighting pump). `load_fx_chain` a
   different factory preset only when the existing chain provably fights the
   arrangement; otherwise add global lanes around it.
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
- [ ] Every sounding palette track has modulation (audible movement preferred;
      subtle fallback allowed and named).
- [ ] Every musical automation lane HEARABLE: verify_part solo rms moved vs pre-pass capture.
- [ ] Lanes enabled; no pid collisions; no automation in breakdowns that fights
      the breakdown (pump off, movement slow).
- [ ] Global choreography is documented by section: each build/drop/breakdown has
      at least one named movement event, and no local layer identity was erased.
- [ ] No notes/clips touched; no exports.

## Discipline
- One lane per paramID per track (the tool rejects duplicates — respect it).
- Snapshot before big chain changes (`capture_fx_snapshot`) so A/B is honest.
- Report: per track — chain loaded, lanes added (pid, preset, window, depth),
  verify_part evidence.
