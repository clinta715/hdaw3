# Role Dispatch Preamble Template
# Copy this block into every role subagent's dispatch prompt, replacing
# the placeholders in <angle brackets>.

## ENGINE ACCESS — copy this block verbatim into every dispatch

The engine serves MCP over HTTP at the address configured in .mcp.json.
EVERY engine call goes through the mcp proxy:

```typescript
await mcp({ server: 'hdaw-http', tool: '<toolName>', args: { ... } });
```

Host variants — same tool names, THREE call shapes: pi-hosted agents use the mcp
proxy above; omp-harness subagents write a JSON args object to the mounted tool
device `xd://mcp__hdaw_<toolName>`; DSH-hosted agents (no `mcp()` proxy tool)
use the CLI transport, which POSTs to the SAME address:

```powershell
python scripts/hdaw_mcp_http.py call <tool> '{"...":"..."}'
python scripts/hdaw_mcp_http.py tools [--filter SUBSTR]   # count + names
python scripts/hdaw_mcp_http.py whoami
python scripts/hdaw_mcp_http.py desc NAME1,NAME2          # first 600 chars each
python scripts/hdaw_mcp_http.py schemas [NAME1,NAME2]     # inputSchema <=900 chars
python scripts/hdaw_mcp_http.py run <steps.json>          # ONE engine, N steps
```

It reaches the SAME per-session singleton engine (default
`http://127.0.0.1:18765/mcp`, override `HDAW_MCP_URL`) and takes the same tool
names, so nothing is spawned or killed. `.mcp.json` carries a harness-owned
direct-stdio `hdaw` entry (`mcp.startupTimeoutMs: 0`) — it is launched by the
harness host, not by you.

NEVER list or launch stdio hdaw servers — that kills the shared engine (the
harness-owned entry above does not lift this rule for agents). The stdio client
`scripts/mcp_call.py` is NOT an alternative for a role: each invocation spawns
its OWN fresh engine binary (`HDAW_headless.exe` on Windows /
`HDAW_headless` on Linux), which is exactly the case this rule forbids.
NEVER call load_project / save_project unless the role playbook allows it.

## Pre-flight pair — run BOTH before your first real call

`whoami` then `tool_help`, on whatever transport you were given:

- `whoami` — proves WHICH engine/transport/project/batch you are attached to:
  `runningBinaryPath`/`runningMtime`/`runningSize`, `version`, `exporting`,
  `transport` (`"stdio"`/`"http"`), `projectPath`/`projectName`,
  `trackCount`/`clipCount`, and the edit-batch state
  `batchOpen`/`batchDepth`/`batchName`. A transport or project you did not
  expect — or `batchOpen:true` you did not open — means STOP and re-route, not
  debug. It is a superset of `engine_info` (same args, same values).
- `tool_help {name}` — returns that tool's EXACT `tools/list` entry
  (`{name, description, category, inputSchema}`, with the unit annotations and
  the one-object `examples` array). Use it instead of guessing a schema or an
  argument spelling; an unknown name is refused in-band with `unknown tool
  <name>`.

Cheaper than a failed call: `tool_help` is the answer to "is it `filePath` or
`path`?", and it cannot drift from `tools/list` (both read the same entry).

## Argument contract
Prefer the stable `trackID` key on the fx/automation/plugin tool families ONLY
when you personally hold the id from an `add_track`/`add_track_with_fx` return —
the stable id is **1-based** (= positional index + 1) and NO read tool reports it,
so a positional index put in the `trackID` key silently addresses the PREVIOUS
track. These families accept `trackID` optionally and resolve via `resolveTrackRef`
(present →
strict: the stable `trackID` is authoritative when supplied; if a positional
argument is also supplied it must resolve to the SAME track, otherwise the call
errors; absent →
legacy positional). Positional-only by design: `slotIndex`, `paramIndex`, `laneName`,
`programIndex`, batch/clip tools, `read.getTrack`/`getTrackMeter`/
`pluginParam.getParamText`, and `audition_plugin`/`verify_part` (`trackIndex`
only). NEVER cache a positional track index across `moveTrack`/`removeTrack`.

## Common tool names (verify with `tool_help <name>` if a name errors)

Reads: get_project_summary, list_tracks, list_clips, list_notes,
  list_fx_params, list_midi_fx_params, get_song_plan, get_cells,
  snapshot_project, engine_info, whoami, tool_help,
  query_notes {startBeat, endBeat, trackIndex|trackID},
  query_clips {startBeat, endBeat}
Measurements: export_audio {outputPath, wait:false} + poll_job,
  mix_report {filePath, fromPlan} + poll_job,
  mix_verdict (final render + plan — composed release gate),
  verify_window {startBeat, endBeat, targets?|expect?, outputPath?} (ONE full
  render, gates the WINDOW's stats),
  render_and_verify {outputPath, fromPlan?} (full render + mix_verdict verdict),
  analyze_tuning {wavPath, role} + poll_job,
  verify_part {trackIndex, windowSeconds, startBeat?, endBeat?},
  get_waveform_peaks {path}
Writes: add_track_with_fx, add_fx, add_midi_fx, remove_fx,
  set_internal_fx_param, set_fx_param, set_midi_fx_param,
  set_master_fx_param, set_master_fx_bypassed,
  set_track (volume/mute/pan), set_fader_authoritative,
  add_midi_clip, add_notes, set_note_velocities, set_note_chance,
  set_cells (batch set_cell — N cell recipes in ONE undo unit),
  set_notes_gain {noteIds:[…], gain}, set_clips_edit {edits:[…]} (batch,
  one undo unit; validate-then-apply),
  begin_batch {name} / end_batch {verify?} (Arranger only, STDIO transport only),
  add_automation_lane, set_automation_points, automation_preset,
  add_lfo, set_lfo_param, sampler_set_sample, set_sampler_key_range,
  psy_fm_load_preset, apply_sub_synth_mod_preset,
  load_nord_bank, load_virus_preset,
  send_fx_midi, save_project (orchestrator only)
FORBIDDEN: load_project, save_project (unless allowed),
  remove_track, all note/clip mutations outside your layer

## Edit batches — the Arranger's multi-step mutation groups

`begin_batch {name}` … `end_batch {verify?}` makes every write between them ONE
named undo unit, INCLUDING commands that open their own internal transaction
(one `undo` reverts the whole batch). Three binding rules:

- **stdio only.** `begin_batch` is REFUSED on any other transport (a batch owns
  the process-wide undo transaction). The CLI/HTTP transport therefore cannot
  batch: a DSH-hosted Arranger gets a refusal, not a silent no-op — split into
  the smallest coherent groups and `save_project` between them instead.
- **One at a time, engine-global.** A second `begin_batch` while one is open is
  refused naming the open batch (`a batch is already open (name "…") - call
  end_batch first`); `end_batch` with none open answers `no open batch`. Every
  writer on ANY surface joins the open batch, so keep it short.
- **It is a flag, not a counter, and a command FAILURE does not close it** —
  call `end_batch` on the failure path too. Check `whoami`'s `batchOpen` if you
  are unsure whether one is open.

## Pre-dispatch check (orchestrator runs this, not the agent)
engine_info {expectedVersion: '<version>'} — assert versionMismatch=false
get_project_summary — assert tracks/clips match expectations
