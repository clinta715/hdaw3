# psy-song-session shared reference

Consult on demand — do NOT read this file unless the role file tells you to.
Each section answers a specific question that arises during a session. The two
sections worth reading up front: "Engine contract deltas (2026-09-25/26,
2026-09-28)" — tool-contract changes since the role files were last synced —
and "Unit-tagged time windows (2026-09-28)".

## Modulation-first rule

Prefer the device's own matrix/envelopes/onboard FX, then HDAW parameter
automation, then HDAW internal FX, then a third-party plugin — plugin FX add
CPU, latency, isolation and state-round-trip risk.

## Depth is a dial, not a flag

The 2026-09-22 verification run measured amDepth 18.4 on a growl layer
(near-square-wave amplitude) — theatrical, not musical. Targets:
sustained layers amDepth ≤ 0.3, rhythmic layers amDepth ≤ 0.6, percussive
layers amDepth ≤ 0.15. Measure with `tone_verity` (`amDepth` field) — if a
layer exceeds its target, halve the LFO depth or the movement-plan spread.

## Render variance

Emulated synths (NodalRed2x, OsTIrus, etc.) have free-running oscillator
phase — two exports differ by ~±2% RMS. A/B comparisons should use spectral
properties (centroid, band energies), not sample-level equality. For
parameter audibility the variance floor is MEASURED per-run by the probe's
baseline renders — trust the probe's own spread, not a hardcoded threshold.

## Deviceless engine: live-thread state is NOT evidence (2026-09-29)

**First, check the DEVICE — "deviceless" is a setup step, not a fault.** The
headless engine can start with no output selected: `get_audio_current_setup`
reports `output:""` and the log's `LiveClockDiag` counter is FROZEN (`dBlocks=0`,
so `processBlock` never runs). Then the LIVE graph has no tracks at all —
`MainAudioProcessor::getTrack(N)` is null for every N, so `load_virus_preset`,
`load_nord_bank`, `capture_fx_snapshot` and every other live-slot tool fail with
**`track not found: N`** for tracks `list_tracks` plainly lists (tree tools still
work), and `ensureLiveRouting` does NOT rescue it (its bounded fallback
`rebuildRoutingGraph()` no-ops without a RoutingManager —
`src/engine/AudioEngine.cpp:1877-1909`). The error blames the TRACK, not the
device. Fix: `set_audio_output_device {"name":"<from get_audio_output_devices>"}`
— `dBlocks` starts advancing and the whole live path comes alive (loaders queue,
`list_fx_params` reports real plugin params, `get_fx_capture_status` answers `ok`).

If a session stays deviceless, `processBlock` never runs, so audio-thread state is
stale by construction:
`sampler_get_state.hasSound` is `SamplerEngine::hasSound_`, assigned ONLY inside
`applyPendingSwap()` at the render block boundary (`src/engine/SamplerEngine.cpp:38-54`)
— `hasSound:false` / `activeVoices:0` then mean "no device", NOT "no sound"
(eight correctly-loaded drum samplers reported it in one session). Sync-written
TREE properties (`sampleFile`, `hasSampleFile`, params) ARE valid evidence;
AUDIBILITY needs an OFFLINE RENDER — the export domain runs `processBlock`, which
is why `audition_plugin`'s solo render is audible while `audition_patch` is not.

## Hardware VA per-engine loader status

Measured 2026-09-20, confirmed on 2026-09-22 (silent-children fix landed):

| Engine | Params | DT1/dump loads | `set_fx_param` | Notes |
| --- | --- | --- | --- | --- |
| JE8086 | 461 | ✅ DT1 dumps apply | ✅ verified | wrapper retargets UserPatch → temp perf |
| Vavra | 7557 | ✅ Waldorf dumps (via `apply_preset`) | ✅ via ledger | dumps = F0 3E edit-buffer |
| OsTIrus | 6939 | ✅ ROM CC0+PC | ✅ verified | F-A resolved 2026-09-20 |
| Osirus | 3086 | ✅ ROM CC0+PC | ✅ verified | same engine family |
| Xenia | 2151 | ✅ Waldorf dumps | ✅ verified | |
| NodalRed2x | 362 | ✅ Nord bank loads | ✅ verified | |

`apply_preset` is the agentic front door — dispatches by slot target + file
header. After loading, verify: `get_fx_capture_status` → then `tone_verity`.
A capture receipt "ok" with a silent render means the patch didn't take.

## Probe phrase notes

The sweep's `build_probe_notes` for `bass` = ONE sustained note (root, whole
window). Plucky patches legitimately fail the sustained-bass role check.
Use `--window 3` for quick sweeps; a longer window under-tests plucky
patches. The analyzer's silence gate zeroes spectral features on
mostly-silent renders — a "centroid 0Hz" fail on a quiet patch means the
patch is too quiet for the role, not that the measurement broke.

## The 10-second wrapper timeout (AGENTS.md lesson 29)

Every mutating call must stay under the wrapper's request timeout — lazy-mcp's
`requestTimeout`, **10 000 ms by default**, and the documented override
(`docs/mcp-server-ops.md`) has been observed missing from the live config, so verify
it before assuming a long call is safe. On timeout the wrapper discards the
connection and the next call relaunches the engine onto a **fresh empty project**,
wiping unsaved state. That is NOT exit 42: 42 comes only from the deliberate
`engine_restart` tool, never from a timeout. Batch
small; `save_project` IMMEDIATELY after each mutation group; never
blind-retry a timed-out call (the first is still running engine-side).
`scripts/crash-diag.ps1 report` gives exit codes + dump inventory.

## Engine contract deltas (2026-09-25/26, 2026-09-28)

Tool-contract changes since the role files were written. Role guidance assumes
these.

### Stable track refs (`trackID`)
~49 MCP tools in the fx/automation/plugin families
(`McpTools_{FxSlot,FxChain,FxPreset,FmSynth,Sampler,PsyFm,Matrix,Envelope,Automation,MidiFx}`)
plus the shared `automation_preset` and `apply_movement_plan` entry points accept
an OPTIONAL stable `trackID` key, resolved by
`src/common/StableRefResolve.h` `resolveTrackRef` (the stable `trackID` is authoritative when supplied; if a
positional argument is also supplied it must resolve to the SAME track,
otherwise the call errors).
Parsing is SPLIT: `trackID` PRESENT → strict (unknown id errors `unknown trackID
N`; a positional+id disagreement errors with spelling-preserving text naming the
surface's own positional spelling — `trackId X and trackID Y disagree` on the
tools, `trackIndex X and trackID Y disagree` on the routes that spell
`trackIndex`);
`trackID` ABSENT → byte-for-byte legacy positional. Sentinels survive
(`set_fader_authoritative` -1 = all tracks; `audition_patch` wildcard). STILL
POSITIONAL BY DESIGN: `slotIndex`, `paramIndex`, `laneName`, `programIndex`, the
batch/clip tools, `read.getTrack`/`read.getTrackMeter`/`pluginParam.getParamText`,
AND `audition_plugin` + `verify_part` (their schemas carry `trackIndex` only, no
`trackID`). Prefer `trackID` on the fx/automation/plugin families when you hold a
stable id (`add_track`/`add_track_with_fx` return `trackID`); NEVER cache a
positional track index across `moveTrack`/`removeTrack` (09-25 trap 3).
Evidence: 09-26 handoff §2 + 09-25 handoff §4d RESOLVED.

### The stable `trackID` is **1-BASED** — and now discoverable (2026-09-29; fixed 2026-09-30)

`allocateTrackID()` = `max(existing trackID) + 1`, floor 1
(`src/model/ProjectModel.cpp:157-170`), so a fresh project's first track gets
stable id **1**, not 0: N tracks created in order get stable ids **1..N** while
positional indices are **0..N-1** — **stable trackID = positional index + 1**.
Measured on a 13-track project (indices 0..12), same instant, same slot:

- `list_fx_params {trackId:7}` → psy_fm (`OP1 Ratio…`) = index 7 — CORRECT.
- `list_fx_params {trackID:7}` → a SAMPLER (`Attack…`) = index **6** — WRONG TRACK.
  (`trackID:13` → index 12; `trackID:14` → refused `unknown trackID 14`;
  `trackID:0` → refused `unknown trackID 0` — it used to answer the misleading
  `trackId required`, fixed 2026-09-30.)

A positional index fed into the `trackID` key therefore silently addresses the
**previous** track — a wrong-entity write with no error (lesson 38's class),
visible only as mysterious silence. It IS discoverable now: **`list_tracks` carries
the stable `trackID` in every row, next to the positional `id`**
(`src/common/TrackListJson.h`, fixed 2026-09-30) — `id` is the POSITIONAL index
(an address that shifts), `trackID` the identity read off the TRACK node. The same
pair now rides on every other read payload (all fixed 2026-09-30): the creation
return `{"trackId":<index>,"trackID":<id>}` from `add_track` /
`add_track_with_fx` / `duplicate_track` (`src/common/TrackJson.h`,
`src/common/AddTrackWithFx.h:84`); `snapshot_project` rows carry `trackID` beside
`index`, exactly as `read.snapshot`'s rows already did (`FrontendRpc.h`
`toJson(TrackSnapshot)`); and `get_layer_handoffs` rows carry `trackID` beside the
positional `trackId` (`src/common/SongPlanView.cpp` — the ONE shaper the tool and
`composition.getLayerHandoffs` both call, so the two surfaces cannot drift).
**Safe rule: use positional `trackId`/`trackIndex` when you mean a POSITION; use
`trackID` (from a creation return, a `list_tracks` / `snapshot_project` row, or a
layer-handoff row) when you mean a TRACK.**

### Windowed-render iteration
Iterate music through WINDOWED renders against the running engine over MCP
(`verify_part` with `startBeat`/`endBeat`, short export windows); full-length
renders only at gates and freeze-last. `test/gfreeze` is a regression pin, not a
sketchpad.
Evidence: 09-25 handoff §4f + 09-26 handoff §4 trap 5.

### Breakdown-tail recipe (PsyDub fix)
A breakdown's melodic tail must ring INTO the drop edge: a held note covering the
decay window (e.g. a held tonic at the second-to-last phrase, length reaching the
drop boundary). Reverb room/wet automation is NOT the fix — the fix is
arrangement-level. Tail-window gate in the PsyDub work: beats 636-640 needed
> -40 dBFS.
Evidence: 09-26 handoff §2 + §4c rows.

### Multi-CLAP render budgets
Several CLAP children boot sequentially while the bake budget assumes ONE Virus
warmup — raise `HDAW_RENDER_WINDOW_WAIT_MS` / `HDAW_EXPORT_BAKE_TIMEOUT_MS` for
multi-CLAP renders or exports time out (defaults documented at the
`tests/unit/engine/psytrance_composition_stress_test.cpp` entry).
Evidence: 09-25 handoff §5 trap 4.

### Virus warmup watchdog + fake-id trap
The intentional Virus warmup no longer trips the 1 s-hang minidump (was 330-670 MB
per spawn); real hangs still dump — and since 2026-09-30 the watchdog's dump is
**stack-only** (`MiniDumpNormal`, kilobytes: it answers WHERE processBlock is
stuck; only the SEH crash path keeps the full-memory dump), so the quoted
330-670 MB / 1.5-2 GB figures are pre-fix history. Fake/test plugin ids are NOT
free: a name containing `osirus`/`ostirus`/`virus` trips the child's name-based
warmup (~12 s) and its hang-watchdog minidump even when the plugin does not exist.
Evidence: 09-26 handoff §2 (+ §4 trap 3); P2-b 2026-09-30.

### A BOOTING plugin slot is not a broken one (2026-09-30)
Until an isolated child finishes its boot/warmup it publishes **no parameters**, so
`list_fx_params` answered `{}` and params-dependent calls failed — indistinguishable
from a broken slot (the measured `list_fx_params {}` shape). `list_fx_params` and
its RPC twin `pluginParam.getParams` now run ONE shared bounded wait
(`src/common/PluginBootGate.h`, 1.5 s for a read; `kPluginBootBudgetMs` = 15 s
covers the measured ~12 s warmup for deliberate operations) before reporting an
empty list. **An empty params list right after `add_fx`/a preset load therefore
means the child is still booting (retry) — check `get_fx_capture_status` and the
plugin-host log before concluding the slot is broken.**
Evidence: `PluginBootGate.*` (platform suite) + the `list_fx_params` description.

### Engine access: three host variants (2026-09-29)
pi-hosted agents call
`await mcp({server: 'hdaw-http', tool: '<tool>', args: {...}})`; omp-harness
subagents write a JSON args object to the mounted tool device
`xd://mcp__hdaw_<tool>`; DSH-hosted agents (no `mcp()` proxy tool) use the CLI
`python scripts/hdaw_mcp_http.py {tools|whoami|desc|schemas|call|run}` — e.g.
`python scripts/hdaw_mcp_http.py call whoami '{}'`. All three reach the SAME
per-session singleton engine (`http://127.0.0.1:18765/mcp`, env
`HDAW_MCP_URL`) and take the same tool names; nothing is spawned or killed.
`.mcp.json` also carries a harness-owned direct-stdio `hdaw` entry, with
`mcp.startupTimeoutMs: 0` (OMP settings) because the 250 ms default cannot
cover the ~4 s cold handshake — agents still NEVER spawn their own stdio hdaw
server (it kills the shared engine). `scripts/mcp_call.py` is the STDIO twin and
is therefore NOT a role transport: each invocation spawns a fresh engine.
The 10 s wrapper timeout discipline above is unchanged.
Evidence: 09-25 handoff (`d9a2c57`) + `docs/mcp-server-ops.md` +
`docs/testing-mcp.md` § "Canonical agent workflow".

### Unit-tagged time windows (2026-09-28)
Every window-taking tool accepts the musical spelling, its `*Sec` twin, AND its
own bare key (read per an optional `unit: "beats"|"seconds"`). Two spellings of
the same endpoint that DISAGREE are REFUSED
(`conflicting window units: startSec and start disagree`) — never silently
picked; agreeing spellings are fine. Seconds convert at the project BPM, EXCEPT
`mix_report`/`mix_verdict`/`mix_diff`, which describe a rendered file and use
their own `bpm` argument when present (> 0). The response ECHOES the unit used
(`"unit":"beats"` in a JSON payload; `… unit=beats` appended to
`export_audio`'s status line). `query_notes`/`query_clips`/`verify_window`/
`verify_part` speak BEATS with an added `*Sec` twin; `export_audio` keeps bare
`start`/`end` in SECONDS and adds `startBeat`/`endBeat` + `unit`.
Evidence: `src/common/WindowUnitArgs.h`; `docs/testing-mcp.md` § "Time windows".

### Archaeology, batch edits, discovery (2026-09-28)
Ten tools landed in the mechanization release; all exist in the live
`tools/list` (317 tools, engine v0.39.2).

| Tool | Contract |
| --- | --- |
| `whoami` | engine_info superset + `transport`/`projectPath`/`projectName`/counts + `batchOpen`/`batchDepth`/`batchName` |
| `tool_help {name}` | that tool's EXACT `tools/list` entry; unknown name refused `unknown tool <name>` |
| `query_notes {startBeat, endBeat, trackIndex\|trackID}` | notes that SOUND in the window — INTERVAL OVERLAP in ABSOLUTE project beats, span clamped to the clip (`truncated` flag); rows carry `noteId`/`clipId`/`trackID` |
| `query_clips {startBeat, endBeat}` | clips whose span INTERSECTS the window, absolute beats |
| `set_notes_gain {noteIds:[…], gain}` | per-note gain on many ids, ONE undo unit |
| `set_clips_edit {edits:[…]}` | per-clip PARTIAL edits (`clipId, start?, duration?, gain?, fadeIn?, fadeOut?, name?, looping?`), ONE undo unit |
| `begin_batch {name}` / `end_batch {verify?}` | one named undo unit; STDIO transport only, one at a time |
| `verify_window {startBeat, endBeat, targets?\|expect?, outputPath?, timeoutMs?}` | render the WHOLE project, gate ONE window's promoted stats |
| `render_and_verify {outputPath, fromPlan?, targets?, timeoutMs?}` | full render + a verdict byte-identical to `mix_verdict` |

**Validate-then-apply (both batch editors):** an EMPTY array (`noteIds must not
be empty` / `edits must not be empty`) or ANY unknown id (`unknown noteId N` /
`unknown clipId N`) refuses the WHOLE batch — nothing written, no undo unit.
`edits` items are `additionalProperties:false`, so a TYPO'd key is REJECTED,
not silently dropped (lesson 34).
**Strict expectations:** `verify_window`'s `targets`/`expect` accept only
`rmsMin` (LINEAR RMS floor, same units as the report's `rms`), `masterRms`
(within 5%), `ceilingHitPctMax`, `kickProminenceMin` (0..1), and
`targetDurationSeconds`; any other key is refused `unknown expectation key
<key>` BEFORE any render.
Evidence: `docs/plans/2026-09-28-agent-mechanization.md` §1–§7;
`docs/handoffs/2026-09-28-agent-mechanization-shipped.md` §2–§3.
