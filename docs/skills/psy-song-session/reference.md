# psy-song-session shared reference

Consult on demand — do NOT read this file unless the role file tells you to.
Each section answers a specific question that arises during a session. The one
section worth reading up front: "Engine contract deltas (2026-09-25/26)" —
tool-contract changes since the role files were last synced.

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

## Engine contract deltas (2026-09-25/26)

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
per spawn); real hangs still dump. Fake/test plugin ids are NOT free: a name
containing `osirus`/`ostirus`/`virus` trips the child's name-based warmup (~12 s)
and its hang-watchdog minidump even when the plugin does not exist.
Evidence: 09-26 handoff §2 (+ §4 trap 3).

### Engine access: two host variants
pi-hosted agents call
`await mcp({server: 'hdaw-http', tool: '<tool>', args: {...}})`; omp-harness
subagents write a JSON args object to the mounted tool device
`xd://mcp__hdaw_<tool>`. `.mcp.json` also carries a harness-owned direct-stdio
`hdaw` entry, with `mcp.startupTimeoutMs: 0` (OMP settings) because the 250 ms
default cannot cover the ~4 s cold handshake — agents still NEVER spawn their
own stdio hdaw server (it kills the shared engine). The 10 s wrapper timeout
discipline above is unchanged.
Evidence: 09-25 handoff (`d9a2c57`) + `docs/mcp-server-ops.md`.
