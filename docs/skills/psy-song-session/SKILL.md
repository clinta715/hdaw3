# psy-song-session: agentic song-writing pipeline

Orchestrates a psytrance song as six scoped subagent roles (Curator, Pattern
Researcher, Sound Selector, Arranger, FX & Automation Engineer, Mix Verifier)
plus optional layer-agent dispatches for final-track quality. Immutable Song
Brief, machine-verifiable gates between roles.

**Shared reference**: `reference.md` (modulation-first, depth targets, render
variance, hardware loader status, 10 s timeout discipline). Role files link to
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
engine on launch. Role subagents MUST use the shared HTTP engine's mcp proxy
(`await mcp({server:'hdaw-http', tool:..., args:...})`), never spawn their own
stdio server. Dispatch with `extensions: true` and `tools: [<core>, "mcp"]`.

**Every mutating call must stay under the wrapper's 10 s timeout** — on
timeout the wrapper restarts the engine (exit 42), wiping unsaved state.
Batch small; `save_project` IMMEDIATELY after each mutation group; never
blind-retry a timed-out call. `scripts/crash-diag.ps1 report` gives exit
codes + dump inventory.

**Checkpoint saves**: the Arranger saves immediately after each mutation
group. An engine death between phases must never lose more than one batch.

## Global modulation rule

Every sounding track MUST carry modulation. Prefer musically audible movement.
**Depth is a dial**: sustained layers amDepth ≤ 0.3, rhythmic ≤ 0.6,
percussive ≤ 0.15. Measure with `tone_verity` and re-fit if outside bounds.
Full rule + targets: `reference.md`.

## Phase order and the single-writer rule

1. **Parallel offline**: Curator + Pattern Researcher (never touch the engine)
2. **Sound Selector**: tempo/scale, track map, FX chains, patches, auditioning
3. **Arranger** (single writer): plan/cell tools, fills sections
4. **FX & Automation Engineer**: cross-section movement, modulation audit
5. **Mix Verifier**: export, measure, fix-first loop (max 3), final verdict
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
`audit_modulation_coverage`, `audit_song_structure`. Prose is not evidence.
Render variance (~±2% RMS for emulated synths): use spectral properties for
A/B; the probe's baseline spread is the trust threshold.

## Failure handling

- Role blocker: orchestrator re-routes to the owning role with evidence.
- Verifier FAIL: fix-first loop, max 3 re-renders, then surface to user.
- Engine exit 0x2A (42) = wrapper timeout restart, NOT a crash: re-load the
  checkpoint and continue. Real crashes land a WER dump — check
  `scripts/crash-diag.ps1 report` for exit codes + dumps before debugging.
