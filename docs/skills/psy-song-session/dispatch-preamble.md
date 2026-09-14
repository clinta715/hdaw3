# Role Dispatch Preamble Template
# Copy this block into every role subagent's dispatch prompt, replacing
# the placeholders in <angle brackets>.

## ENGINE ACCESS — copy this block verbatim into every dispatch

The engine serves MCP over HTTP at the address configured in .mcp.json.
EVERY engine call goes through the mcp proxy:

```typescript
await mcp({ server: 'hdaw-http', tool: '<toolName>', args: { ... } });
```

NEVER list or launch stdio hdaw servers — that kills the shared engine.
NEVER call load_project / save_project unless the role playbook allows it.

## Common tool names (verify with mcp describe/search if a name errors)

Reads: get_project_summary, list_tracks, list_clips, list_notes,
  list_fx_params, list_midi_fx_params, get_song_plan, get_cells,
  snapshot_project, engine_info
Measurements: export_audio {outputPath, wait:false} + poll_job,
  mix_report {filePath, fromPlan} + poll_job,
  analyze_tuning {wavPath, role} + poll_job,
  verify_part {trackIndex, windowSeconds, startBeat?, endBeat?},
  get_waveform_peaks {path}
Writes: add_track_with_fx, add_fx, add_midi_fx, remove_fx,
  set_internal_fx_param, set_fx_param, set_midi_fx_param,
  set_master_fx_param, set_master_fx_bypassed,
  set_track (volume/mute/pan), set_fader_authoritative,
  add_midi_clip, add_notes, set_note_velocities, set_note_chance,
  add_automation_lane, set_automation_points, automation_preset,
  add_lfo, set_lfo_param, sampler_set_sample, set_sampler_key_range,
  psy_fm_load_preset, apply_sub_synth_mod_preset,
  load_nord_bank, load_virus_preset,
  send_fx_midi, save_project (orchestrator only)
FORBIDDEN: load_project, save_project (unless allowed),
  remove_track, all note/clip mutations outside your layer

## Pre-dispatch check (orchestrator runs this, not the agent)
engine_info {expectedVersion: '<version>'} — assert versionMismatch=false
get_project_summary — assert tracks/clips match expectations
