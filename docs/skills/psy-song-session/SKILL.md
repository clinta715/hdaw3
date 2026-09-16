---
name: psy-song-session
description: Orchestrates a full psy-song writing session as six scoped subagent roles (Curator, Pattern Researcher, Sound Selector, Arranger, FX & Automation Engineer, Mix Verifier) over HDAW's MCP surface, with an immutable Song Brief and machine-verifiable gates between roles.
---

# psy-song-session: agentic song-writing pipeline

You are the ORCHESTRATOR. You never write notes or touch FX yourself — you pin
the brief, dispatch role subagents, check gate evidence, and decide redo loops.
Each role has its own playbook with a strict tool surface:

| Role | Playbook | Surface in one line |
| ---- | -------- | -------------------- |
| Curator (offline) | `roles/curator.md` | library ingestion + descriptors, never the project |
| Pattern Researcher (offline) | `roles/pattern-researcher.md` | external MIDI -> pattern library, never the project |
| Sound Selector | `roles/sound-selector.md` | tracks, presets, FX slots, initial factory chains, auditions; no notes |
| Arranger (single writer) | `roles/arranger.md` | sections/clips/notes; arrangement-internal automation (clip gain/CC envelopes) — the only arrangement mutator |
| FX & Automation Engineer | `roles/fx-automation-engineer.md` | FX chain refinement + FX-parameter automation lanes (movement); never notes/clips/instruments |
| Mix Verifier (read-mostly) | `roles/mix-verifier.md` | export + measure + fader/master gain only |

Role files resolve against this skill's directory (parent of SKILL.md).

## The Song Brief (the contract)
The brief is now ENGINE STATE: the Arranger applies it with `apply_song_brief`
as its first step (sections materialize as typed arranger regions), and the
plan/cell workflow (`set_cell` / `fill_cells` / `reroll` — composition guide §4)
is its PREFERRED arrangement path: structure stays pinned, content re-rolls by
seed, and `mix_report {fromPlan: true}` verifies the energy arc without
re-typing section windows. `export_song_brief` reads the plan back.
Pin `compositions/<song>/brief.json` (schema: `brief.schema.json`, this directory)
BEFORE dispatching anything: bpm, keyRoot, scaleMode, style, seed, totalBars,
sections, targets (masterRms, ceilingHitPctMax), and later — role-written —
`palette`/`paletteTrackMap`/`patterns`/`artifacts`. The brief is IMMUTABLE once
pinned: roles read it, only the orchestrator updates it between phases.
Seed discipline (repetition guard): pin a FRESH random seed per song — never
reuse a seed across songs (deterministic generators render the same seed
identically, so a reused seed is a remixed arrangement, not a new song).
Vary the section layout per style/brief (intro/build/drop bar counts and
kinds), not one template for every track.

All runtime artifacts (brief, renders, reports) live under `compositions/<song>/`
(gitignored). Deterministic generators take the brief's seed; verification uses
the brief's targets, not vibes.

## Dispatch — shared-engine contract (CRITICAL)
The engine is a PER-SESSION SINGLETON: mcp-launch.bat kills any running engine
on launch ("one engine per session"). If a role subagent boots its own stdio
hdaw server, it KILLS the orchestrator's engine and everything unsaved is lost.
The working architecture (verified 2026-09-09/10):

1. **Orchestrator owns the engine** through its stdio `hdaw` session.
2. **The engine must serve MCP over HTTP** (one-time: registry
   HKCU\Software\HDAW\HDAW\mcp httpEnabled=true, or launch once with
   `--mcp-http`). Endpoint: `POST http://127.0.0.1:18765/mcp` (JSON-RPC;
   full tool registry).
3. **Role subagents NEVER list the stdio `hdaw_*` tools.** A subagent whose
   adapter launches the stdio server kills the shared engine mid-phase
   (observed twice). Role calls go through the adapter's `mcp` proxy:

   `await mcp({ server: 'hdaw-http', tool: 'get_project_summary', args: {} });`

   Dispatch with `extensions: true` and `tools: [<core>, "mcp"]` — subagents
   run without user extensions by default; without this they have no `mcp`
   proxy at all ("Unknown Fabric action").
4. **Checkpoint saves**: the Arranger saves immediately after generation
   (an engine death between phases must never lose the arrangement). The
   Verifier re-loads the checkpoint when it finds a respawned engine.

Each dispatch prompt contains:
1. The FULL role playbook text (read the role file — do not paraphrase the gates).
2. The current brief (inline JSON) + its path.
3. The exact tool-call surface the role is allowed (`hdaw_*` MCP command names) —
   the same names passed in `tools`.
4. The handoff format expected back.
Subagents read `docs/psytrance-composition-guide.md` for recipes when needed.

## Global modulation rule

Every sounding track/layer MUST carry modulation. Prefer musically audible
movement (filter cutoff, phase, wavetable/FM amount, tremolo, delay feedback/mix,
pump, macro sweeps). If no appropriate musical modulation target exists for a
role, add a subtle-to-nearly-indistinguishable safe modulation instead; the rule
is presence of life on every layer, not theatrical movement everywhere. Handoff
evidence must name the modulation target/preset/depth for each layer.

## Ownership model: local identity + global choreography

The workflow is HYBRID, not one giant project pass and not one subagent per
note. A layer agent owns the local identity of its layer: final sound choice
from the palette/shortlist, pattern/musical behavior, role FX, and mandatory
local modulation. The project-level roles own constraints and coordination:
Sound Selector prepares the palette/shortlists/default chains; FX & Automation
Engineer choreographs cross-section movement after layers exist; Mix Verifier
rejects static/boring spans and routes fixes back to the owning layer or the
global choreography pass.

Rule of thumb: **layer agent = what this part is; FX Automation = how the song
moves as a whole**.

**Project-native ledger (tools):** handoffs persist IN THE PROJECT, not only in
files — `set_layer_handoff` (role/soundIntent/patternIntent/modulation/verify per
track, ONE undo unit, survives save/load), `get_layer_handoffs`, `clear_layer_handoff`,
and the mechanical gate `audit_modulation_coverage` (flags any sounding track with
no enabled LFO, no movable automation lane, and no sub_synth internal LFO), plus
`apply_movement_plan` (batch build/drop/breakdown automation arcs across tracks
in ONE undo unit; lanes are auto-created/reused by paramID, never stacked), and
`audit_song_structure` (boredom/static-span gates: ≥8 bars of no-melodic/no-backbeat
spans, drop backbeat presence, first-drop motif — embedded in `mix_report` as the
`structure` block when fromPlan=true), plus
`diagnose_intro_blast` (windowed intro analysis of a rendered master: clipping /
loud-transient / NaN-poison / DC-offset / saturation-then-silence classification,
with per-track solo-render attribution of the blast window — the recurring loud
intro bug class). Agents verify handoff evidence with the audit tools, not by
parsing JSON files.

## Phase order and the single-writer rule
1. **Parallel offline**: Curator and Pattern Researcher run concurrently — they
   never touch the engine's project state, so they parallelize freely.
2. **Sound Selector** (first engine writer): applies tempo/scale, builds the
   palette track map, auditions everything. Engine is now "owned" until handoff.
3. **Arranger** (single writer): writes the song against the palette. No other
   role may hold the engine concurrently — the harness enforces this by only
   dispatching the Arranger while nothing else mutates.
4. **FX & Automation Engineer** (global choreography pass): runs over the
   FINISHED layered arrangement — audits the per-layer modulation/FX that layer
   agents already wrote, resolves collisions, and adds cross-section movement
   arcs (cutoff sweeps, pump, riser curves, delay throws, space changes). Single
   writer while dispatched; never notes, clips, or instruments; no exports.
5. **Mix Verifier**: renders + measures async; on FAIL it names the fix and the
   owning role; bounded to 3 render/rework loops before reporting to the user.
6. **Persist** only on a PASS verdict (`save_project`), then stop.

Concurrent dispatch rule: multiple roles may hold the engine ONLY if every one
of them is in a read-only phase (Verifier measuring a finished render, Selector
auditioning). Any mutation => one writer at a time.

## Layered mode (preferred for final tracks)

Layered mode is the PREFERRED path for FINAL tracks: one element at a time, each
written by a dedicated layer-agent that MEASURES the cumulative mix (the four
gates in `roles/layer-agent.md`) BEFORE writing, so every voice lands in a
register/band the mix has left open. Each layer-agent owns that layer's final
sound/pattern/FX/local modulation decision, using the Sound Selector's palette
as starting material rather than a cage. Bulk cell fill (the plan/cell workflow
of `fill_cells`) remains the FALLBACK for SKETCH mode — fast seeded structure
to capture the shape, then rebuilt layer by layer when the track goes final.

**Fixed layer order** — each layer is a SEPARATE dispatch, strictly sequential,
single writer, never overlapping. Two modes:

**layerMode: "full-build"** (default) — the seven-layer sequence:
1. kick
2. bass
3. hats/snare/clap/down — the percussion bed in ONE layer-agent pass, but the
   pass MUST build a VARIED bed (offbeat hats, backbeat clap, down-beat role,
   ghost notes); a mono straight 4x4 with a single hat loop fails
4. stab
5. pad
6. lead — THE one high part (the register budget's usual holder)
7. riser

**layerMode: "post-hoc"** — for adding ONE layer to an existing arrangement
(e.g. the acidvar revision on Modular Dawn). The dispatch prompt should say
"this is a REVISION, not a first-pass layer" and include:
- The layers.json ledger (the register budget and cumulative rms are contract inputs)
- The existing track's fx chain state (what to keep, what to replace)
- Which sections the new layer enters/exits
- The G1 pre-measure can be skipped (the committed mix IS the baseline)

**Orchestrator responsibilities per layer:**
- (a) Dispatch with the FULL layer-agent playbook (`roles/layer-agent.md`) plus
  the `compositions/<song>/layers.json` ledger so far — register budget and
  cumulative rms are CONTRACT INPUTS, not optional context.
- (b) After each PASS handoff: append the layer entry to
  `compositions/<song>/layers.json` (create if absent) and checkpoint-save via
  `save_project` to `compositions/<song>/neon-layer-<N>.hdaw` (or the song's
  usual checkpoint name).
- (c) NEVER let the next layer start before the previous layer's gate output is
  recorded.

**Gate contract addition:** a layer handoff WITHOUT `beforeRms` / `afterRms`
and `verify_part` numbers is rejected — same rule as the generic gate
contract: evidence is machine output, not prose.

Note: `layers.json` is orchestrator-owned RUNTIME state — like `brief.json`
it is updated ONLY between phases; it is NOT part of the immutable brief.

## Gate contract
A role handoff WITHOUT gate evidence is rejected — send it back with the failed
gate named. Evidence is machine output (verify_part / mix_report / analyze_tuning
/ audition peaks / waveform peaks), not prose.

**Render variance note:** the emulated synths (NodalRed2x, OsTIrus, etc.) have
free-running oscillator phase — two exports of the same project differ by ~±2%
RMS. Gate margins must tolerate this. A/B comparisons should use spectral
properties (centroid, band energies), not sample-level equality. See
`docs/realtime-safety.md` for the full documentation.

## Session lessons that bind every role
- Verify the SAVED project state (mutes, faders, offsets) before diagnosing a
  bad render (AGENTS.md lesson 24).
- FX params in REAL units everywhere (`set_internal_fx_param`, `list_fx_params`).
- Batch mutations; one coherent change = one undo unit.
- Long renders/transforms: async + poll, never through a blocking bridge call.
- Gain is band-targeted; filtered melodic lines need an octave/7th stack basis.
- RAVE is DEPRECATED (v0.32.0): none of these playbooks may use rave_* tools.

## Failure handling
- A role reports a blocker outside its surface: orchestrator re-routes the task
  to the owning role with the evidence attached.
- Verifier FAIL: fix-first loop, max 3 re-renders, then surface to the user with
  the full gate table.
- Engine dies mid-phase (bridge timeout respawn): the Arranger re-reads state
  from the saved project; the Verifier re-exports. Never assume engine state.

## First run (smoke)
Pin a minimal 96-bar brief (known-good corpus seeds from
`compositions/psytrance_corpus_fulltracks.tsv`), run the pipeline end to end on
the pipeline end to end on
the six-role split, and record where handoffs needed human help — that list is
the Phase 1 backlog.
