# Role Dispatch Preamble Template
# Copy this block into every role subagent's dispatch prompt, replacing
# the placeholders in <angle brackets>.

## ENGINE ACCESS — copy this block verbatim into every dispatch

The engine serves MCP over HTTP at the address configured in .mcp.json.
EVERY engine call goes through the mcp proxy:

```typescript
await mcp({ server: 'hdaw-http', tool: '<toolName>', args: { ... } });
```

Host variants — same tool names, two call shapes: pi-hosted agents use the mcp
proxy above; omp-harness subagents write a JSON args object to the mounted tool
device `xd://mcp__hdaw_<toolName>`. `.mcp.json` carries a harness-owned
direct-stdio `hdaw` entry (`mcp.startupTimeoutMs: 0`) — it is launched by the
harness host, not by you.

NEVER list or launch stdio hdaw servers — that kills the shared engine (the
harness-owned entry above does not lift this rule for agents).
NEVER call load_project / save_project unless the role playbook allows it.

## Argument contract
Prefer the stable `trackID` key on the fx/automation/plugin tool families when
you hold a stable id (`add_track`/`add_track_with_fx` return `trackID`); these
families accept it optionally and resolve via `resolveTrackRef` (present →
strict: the stable `trackID` is authoritative when supplied; if a positional
argument is also supplied it must resolve to the SAME track, otherwise the call
errors; absent →
legacy positional). Positional-only by design: `slotIndex`, `paramIndex`, `laneName`,
`programIndex`, batch/clip tools, `read.getTrack`/`getTrackMeter`/
`pluginParam.getParamText`, and `audition_plugin`/`verify_part` (`trackIndex`
only). NEVER cache a positional track index across `moveTrack`/`removeTrack`.

## Common tool names (verify with mcp describe/search if a name errors)

Reads: get_project_summary, list_tracks, list_clips, list_notes,
  list_fx_params, list_midi_fx_params, get_song_plan, get_cells,
  snapshot_project, engine_info
Measurements: export_audio {outputPath, wait:false} + poll_job,
  mix_report {filePath, fromPlan} + poll_job,
  mix_verdict (final render + plan — composed release gate),
  analyze_tuning {wavPath, role} + poll_job,
  verify_part {trackIndex, windowSeconds, startBeat?, endBeat?},
  get_waveform_peaks {path}
Writes: add_track_with_fx, add_fx, add_midi_fx, remove_fx,
  set_internal_fx_param, set_fx_param, set_midi_fx_param,
  set_master_fx_param, set_master_fx_bypassed,
  set_track (volume/mute/pan), set_fader_authoritative,
  add_midi_clip, add_notes, set_note_velocities, set_note_chance,
  set_cells (batch set_cell — N cell recipes in ONE undo unit),
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
