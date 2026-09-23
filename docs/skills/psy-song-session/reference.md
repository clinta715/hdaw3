# psy-song-session shared reference

Consult on demand — do NOT read this file unless the role file tells you to.
Each section answers a specific question that arises during a session.

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
(`docs/testing-mcp.md`) has been observed missing from the live config, so verify
it before assuming a long call is safe. On timeout the wrapper discards the
connection and the next call relaunches the engine onto a **fresh empty project**,
wiping unsaved state. That is NOT exit 42: 42 comes only from the deliberate
`engine_restart` tool, never from a timeout. Batch
small; `save_project` IMMEDIATELY after each mutation group; never
blind-retry a timed-out call (the first is still running engine-side).
`scripts/crash-diag.ps1 report` gives exit codes + dump inventory.
