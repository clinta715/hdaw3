---
name: psy-song-session
description: Orchestrates a psytrance song as six scoped subagent roles (Curator, Pattern Researcher, Sound Selector, Arranger, FX & Automation Engineer, Mix Verifier) plus optional layer-agents, around an immutable Song Brief and machine-verifiable gates. Use for end-to-end multi-role song sessions, layered final-track builds, and engine-safe role dispatch.
---

# psy-song-session: agentic song-writing pipeline

Orchestrates a psytrance song as six scoped subagent roles (Curator, Pattern
Researcher, Sound Selector, Arranger, FX & Automation Engineer, Mix Verifier)
plus optional layer-agent dispatches for final-track quality. Immutable Song
Brief, machine-verifiable gates between roles.

**Shared reference**: `reference.md` (modulation-first, depth targets, render
variance, hardware loader status, 10 s timeout discipline, unit-tagged time
windows, the 2026-09-28 mechanization tools, the 2026-10-02 routed
sidechain). Role files link to
it on demand — do NOT include it in dispatch prompts.

## Role files

| Role | File | Surface |
|---|---|---|
| Curator (offline) | `roles/curator.md` | library ingestion + descriptors, never the project |
| Pattern Researcher (offline) | `roles/pattern-researcher.md` | external MIDI → pattern library, never the project |
| Sound Selector | `roles/sound-selector.md` | tracks, presets, FX chains, patches, auditioning; no notes |
| Arranger (single writer) | `roles/arranger.md` | plan/cell tools, sections/clips/notes; only arrangement mutator |
| FX & Automation Engineer | `roles/fx-automation-engineer.md` | FX chains, movement plans, param automation; never notes/clips |
| Mix Verifier (read-mostly) | `roles/mix-verifier.md` | export + measure + fade |
| Layer Agent (optional) | `roles/layer-agent.md` | one layer at a time, replaces bulk fill for final quality |

## The Song Brief (the contract)

Pin `compositions/<song>/brief.json` (schema: `brief.schema.json`) BEFORE
dispatching anything: bpm, keyRoot, scaleMode, style, seed, totalBars,
sections, targets (masterRms, ceilingHitPctMax), and role-written
`palette`/`paletteTrackMap`/`patterns`/`artifacts`. The brief is IMMUTABLE
once pinned. Apply with `apply_song_brief` (sections materialize as typed
arranger regions); read back with `export_song_brief`.

**Seed discipline**: pin a FRESH random seed per song — never reuse across
songs. Vary the section layout per style/brief, not one template.

**Plan/cell workflow**: the PREFERRED arrangement path. `set_cell` +
`fill_cells` fills per-section windows deterministically. Melodic cells
≥ 32 bars need `params.tileBeats` (default: absent = single sparse pass).
`mix_report {fromPlan:true}` verifies the energy arc.

## Dispatch — shared-engine contract (CRITICAL)

The engine is a PER-SESSION SINGLETON: mcp-launch.bat kills any running
engine on launch (Linux box: no `mcp-launch.bat` — kill the old `HDAW_headless`
process, start `./build/HDAW_headless --mcp-http --mcp-http-port <port>`, and point
agents at it with `HDAW_MCP_URL=http://127.0.0.1:<port>/mcp`). Role subagents MUST reach that ONE engine and never spawn
their own stdio server. Three call shapes, same tool names: pi-hosted agents
use the mcp proxy (`await mcp({server:'hdaw-http', tool:..., args:...})`),
omp-harness agents write to `xd://mcp__hdaw_<tool>`, and DSH-hosted agents use
the CLI `python scripts/hdaw_mcp_http.py call <tool> '<json-args>'`. Dispatch
with `extensions: true` and `tools: [<core>, "mcp"]`. Full contract + the
`whoami`/`tool_help` pre-flight pair: `dispatch-preamble.md`.

**Every mutating call must stay under the wrapper's 10 s timeout** — on
timeout the wrapper discards the connection and the next call relaunches the
engine onto a **fresh empty project**, wiping unsaved state. That is NOT exit
42: 42 comes only from the deliberate `engine_restart` tool, never from a
timeout. Batch small; `save_project` IMMEDIATELY after each mutation group; never
blind-retry a timed-out call. `scripts/crash-diag.ps1 report` gives exit
codes + dump inventory.

**Checkpoint saves**: the Arranger saves immediately after each mutation
group. An engine death between phases must never lose more than one batch.

**Multi-step mutation groups** are the Arranger's ONE named undo unit via
`begin_batch {name}` / `end_batch {verify?}` — stdio transport only (refused on
HTTP/CLI), one at a time, and it must be closed on the failure path too.

## Global modulation rule

Every sounding track MUST carry modulation. Prefer musically audible movement.
**Depth is a dial**: sustained layers amDepth ≤ 0.3, rhythmic ≤ 0.6,
percussive ≤ 0.15. Measure with `tone_verity` and re-fit if outside bounds.
Full rule + targets: `reference.md`.

## Phase order and the single-writer rule

1. **Parallel offline**: Curator + Pattern Researcher (never touch the engine)
2. **Sound Selector**: tempo/scale, track map, FX chains, patches, auditioning
3. **Arranger** (single writer): plan/cell tools, fills sections; multi-step
   mutation groups wrapped in `begin_batch`/`end_batch` (stdio) = one undo unit
4. **FX & Automation Engineer**: cross-section movement, modulation audit
5. **Mix Verifier**: export, measure, fix-first loop (max 3), final verdict —
   `verify_window` localises a bad window, `render_and_verify` is the release gate
6. **Persist** only on a PASS verdict

Multiple roles may hold the engine ONLY if all are read-only. Any mutation →
one writer at a time.

## Layered mode (preferred for final tracks)

One layer at a time, each written by a dedicated layer-agent that MEASURES
the cumulative mix BEFORE writing. Fixed order: kick → bass → percussion →
stab → pad → lead → riser. The orchestrator appends each pass to
`compositions/<song>/layers.json` and checkpoint-saves.

Bulk `fill_cells` is the FALLBACK for sketch mode — fast seeded structure to
capture the shape, then rebuilt layer by layer when the track goes final.

## Gate contract

A handoff WITHOUT gate evidence is rejected. Evidence = machine output:
`tone_verity`, `param_verity`, `param_verity_corpus`, `mix_report`,
`mix_verdict`, `verify_window`, `render_and_verify`, `audit_modulation_coverage`,
`audit_song_structure`. Prose is not evidence.
`verify_window` costs about one full export and gates ONE beat window's
PROMOTED stats; `render_and_verify` = full render + a verdict byte-identical to
`mix_verdict`, so it is the release gate. A windowed render does NOT predict the
full render (plugin state re-bakes per window) — windows LOCALISE a problem,
the full render remains the gate.
Render variance (~±2% RMS for emulated synths): use spectral properties for
A/B; the probe's baseline spread is the trust threshold.

## Failure handling

- Role blocker: orchestrator re-routes to the owning role with evidence.
- Verifier FAIL: fix-first loop, max 3 re-renders, then surface to user.
- Engine exit 0x2A (42) = the deliberate `engine_restart` tool, NOT a crash and
  NOT a timeout: re-load the checkpoint and continue. A request TIMEOUT never
  yields 42 — it discards the connection, and the next call relaunches the
  engine onto a fresh empty project. Real crashes land a WER dump — check
  `scripts/crash-diag.ps1 report` for exit codes + dumps before debugging
  (Windows; on Linux a crashed/hung child leaves async-signal-safe text
  reports `*hung*.crash.txt` under `$TMPDIR` — no minidumps).
