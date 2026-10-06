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

**Buses and sends ARE on the agent surface (corrected 2026-09-23):** the old
"no agent surface" limitation is false. `add_bus` / `add_send` / `remove_bus` /
`remove_send` (commit 70ab519) create and tear down the routing; `list_buses`
(3eed79e) plus `list_bus_fx_params` read it back; `set_bus_fx_param` and
`set_bus_target` shape a return (including re-parenting one behind a high-pass
`filter` bus); and the setters `set_track_send_level` / `set_track_send_mode` /
`set_track_send_bypassed` (with `get_track_sends`) are live — they are exactly what
the mixer's send strip drives. So the shared-return architecture (all sources → one
delay bus + one reverb bus, ridden per phrase) IS buildable: `add_bus`, `add_send`,
set the return's Mix to 1.0, then ride the send level per phrase. **Corrected
2026-09-23 — send levels ARE automatable.** A send level's automation paramID is
`2000 + sendIndex`, and a bus FX param's is `3000 + busID*8 + paramIndex`, so a
per-phrase throw is now a lane-ridable gesture: `add_automation_lane {trackId,
laneName, paramID: 2000+sendIndex}` then `automation_preset {preset:'delayThrow',
...}` rides the send itself, exactly like a track FX lane (the send must already
exist; `remove_send` remaps the lane pids in the same undo unit). Per-track FX plus
the gesture lanes above remain a valid alternative when the effect should stay
local to the track or survive send re-routing. Recipes and measurements:
`docs/composition-toolkit.md` §"Bus/send architecture — reachable
since 2026-09-22".

## Surface area
`list_device_params` (device parameter map — engines, intent vocabulary, tier,
durability; call this FIRST to pick a target), `list_fx_chains`, `load_fx_chain`,
`add_fx`, `remove_fx`, `set_fx_param`, `list_fx` (read the final chain shape:
`slotIndex`/`fxType`/`paramCount`/`bypassed` + a compressor's sidechain fields),
`set_fx_sidechain` (ROUTED kick→bass ducking on a compressor slot),
`save_fx_chain` (persist a tuned variant),
`apply_movement_plan` (batch section-aware movement across tracks in ONE undo unit),
`set_internal_fx_param`, `set_fx_params` (BATCH param writes), `list_fx_params`,
`capture_fx_snapshot`,
`swap_fx_snapshot`, `add_automation_lane`, `set_automation_points`,
`automation_preset`, `set_automation_enabled`, `list_automation_lanes`,
`remove_automation_lane`, `psy_fm_set_mod_route`, `psy_fm_get_analysis`,
`apply_sub_synth_mod_preset`,
`add_bus`, `add_send`, `remove_bus`, `remove_send`, `list_buses`,
`list_bus_fx_params`, `set_bus_fx_param`, `set_bus_fx_params` (BATCH),
`set_lfo_params` (BATCH LFO writes: waveform/rate/depth/targetParamID/…),
`add_lfo`, `set_lfo_param`, `set_bus_target`,
`set_track_send_level`, `set_track_send_mode`, `set_track_send_bypassed`,
`get_track_sends`,
`set_fader_authoritative`, `verify_part`, `verify_window` (render the WHOLE
project, gate ONE beat window's promoted stats), `query_notes` / `query_clips`
(read back what is actually in the window you automated), `tool_help`, `whoami`,
`list_tracks`, `list_clips`,
`get_project_summary`

**For a multi-param voicing/movement pass use the BATCH forms** —
`set_fx_params` / `set_lfo_params` / `set_bus_fx_params` — ONE call, ONE undo unit,
partial-apply (per-write `errors`); do NOT loop the single-write tools.

FORBIDDEN: all note/clip generators and mutators (`add_notes`, `place_patterns`,
`generate_arrangement*`, `add_instrument_part`, ...), `export_audio`/`mix_report`/
`analyze_tuning` (Mix Verifier), sampler/instrument replacement (Sound Selector).

## Procedure
0. **Matrix presets first**: for a core-synth track, read
   `timbre-lib/matrix_presets/<engine>.json` before inventing chains or adding
   plugin FX; pick a harvested preset, apply it via its `appliesVia` path (or
   `apply_matrix_preset`), then audition and re-verify (lookup-first rule + live
   per-engine status: `docs/va-suite-status-log.md` § "9. Per-plugin matrix presets
   (harvested)").
1. **Read the state + layer ledger**: `list_tracks`, `list_automation_lanes`
   per track, plus `get_layer_handoffs` (the project-native ledger written by the
   layer agents — `compositions/<song>/layers.json` is only the human mirror). Know
   each layer's declared soundIntent/patternIntent/modulation before adding
   anything. Never stack a second cutoff lane on the same pid. For gearmulator
   synths (Osirus/OsTIrus/Vavra/Xenia/JE8086), the INTERNAL FX are automatable
   CLAP params — chorus/delay/phaser/distortion/EQ movement recipes with exact
   pids live in `psytrance-composition-guide.md` §4D summary — full "Gearmulator
   internal-FX recipes" in `psytrance-va-and-production.md` §4D; prefer those over `send_fx_midi` CC sweeping.
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
   The **routed** alternative is a real kick→bass sidechain — `add_fx
   {trackId:bass, fxType:'compressor'}` then `set_fx_sidechain {trackId:bass,
   slotIndex:<that slot>, sourceTrackId:kick, level:1.0}`. Prove it with PAIRED
   renders (`param_verity` sweeps a param and cannot toggle a routing property):
   render the bass with the route active and again after `enabled:false`, compare
   with `mix_diff` / `verify_part` — the duck shows as an RMS dip on kick-aligned
   windows. Use ONE of the
   two pump mechanisms per track — a Volume-lane pump AND a sidechain on the same
   bass fight. Recipe: `docs/psytrance-va-and-production.md` §5 "Routed
   sidechain".
5. **Riser curves**: riser preset across each build window; `sine` wobble on pad
   cutoff in breakdowns (slow, one cycle per 4 beats).
6. **Verify**: for each lane, `verify_part {trackIndex}` with the lane enabled —
   solo rms must differ from the pre-automation capture (or the lane is a no-op:
   fix depth or delete the lane). Keep `verify_part` evidence per changed track.
   To prove a gesture lands in the RIGHT bars, gate the window itself with
   `verify_window {startBeat, endBeat, targets?|expect?}` — it renders the WHOLE
   project and gates ONE window's PROMOTED stats, so it costs about one full
   export and is a localiser, not an iteration toy (a windowed render does not
   predict the full render). `query_notes`/`query_clips` read back what is
   actually inside that window (absolute project beats, interval overlap, span
   clamped to the clip) when a measurement disagrees with your intent.
7. **Checkpoints**: save after each track's processing (engine deaths must not
   lose FX/automation work).

**Windows are unit-tagged (2026-09-28).** `automation_preset` and
`apply_movement_plan` take a bare `start`/`end` window (default BEATS) PLUS the
`startBeat`/`endBeat` spellings and their `*Sec` twins, read per an optional
`unit: "beats"|"seconds"`; two spellings that DISAGREE are REFUSED
(`conflicting window units: … disagree`) rather than silently picked. Seconds
convert at the project BPM. See `../reference.md` § "Unit-tagged time windows".

## Gates (all must hold)
- [ ] Every palette track carries its factory chain (or a justified custom one).
- [ ] Every sounding palette track has modulation (audible movement preferred;
      subtle fallback allowed and named).
- [ ] Every musical automation lane HEARABLE: verify_part solo rms moved vs pre-pass capture.
- [ ] A gesture that must land in named bars is gated by `verify_window` on THAT
      window (promoted window stats + `targetChecks`), not by the whole-file numbers.
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
