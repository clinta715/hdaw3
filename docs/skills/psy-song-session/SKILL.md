---
name: psy-song-session
description: Orchestrates a full psy-song writing session as five scoped subagent roles (Curator, Pattern Researcher, Sound Selector, Arranger, Mix Verifier) over HDAW's MCP surface, with an immutable Song Brief and machine-verifiable gates between roles.
---

# psy-song-session: agentic song-writing pipeline

You are the ORCHESTRATOR. You never write notes or touch FX yourself — you pin
the brief, dispatch role subagents, check gate evidence, and decide redo loops.
Each role has its own playbook with a strict tool surface:

| Role | Playbook | Surface in one line |
| ---- | -------- | -------------------- |
| Curator (offline) | `roles/curator.md` | library ingestion + descriptors, never the project |
| Pattern Researcher (offline) | `roles/pattern-researcher.md` | external MIDI -> pattern library, never the project |
| Sound Selector | `roles/sound-selector.md` | tracks, presets, FX slots, auditions; no notes |
| Arranger (single writer) | `roles/arranger.md` | sections/clips/notes/automation; the only arrangement mutator |
| Mix Verifier (read-mostly) | `roles/mix-verifier.md` | export + measure + fader/master gain only |

Role files resolve against this skill's directory (parent of SKILL.md).

## The Song Brief (the contract)
Pin `compositions/<song>/brief.json` (schema: `brief.schema.json`, this directory)
BEFORE dispatching anything: bpm, keyRoot, scaleMode, style, seed, totalBars,
sections, targets (masterRms, ceilingHitPctMax), and later — role-written —
`palette`/`paletteTrackMap`/`patterns`/`artifacts`. The brief is IMMUTABLE once
pinned: roles read it, only the orchestrator updates it between phases.

All runtime artifacts (brief, renders, reports) live under `compositions/<song>/`
(gitignored). Deterministic generators take the brief's seed; verification uses
the brief's targets, not vibes.

## Dispatch
For each phase, dispatch a subagent (`agents.run`) whose prompt contains:
1. The FULL role playbook text (read the role file — do not paraphrase the gates).
2. The current brief (inline JSON) + its path.
3. The exact tool-call surface the role is allowed (`hdaw_*` MCP command names).
4. The handoff format expected back.
Subagents read `docs/psytrance-composition-guide.md` for recipes when needed.

## Phase order and the single-writer rule
1. **Parallel offline**: Curator and Pattern Researcher run concurrently — they
   never touch the engine's project state, so they parallelize freely.
2. **Sound Selector** (first engine writer): applies tempo/scale, builds the
   palette track map, auditions everything. Engine is now "owned" until handoff.
3. **Arranger** (single writer): writes the song against the palette. No other
   role may hold the engine concurrently — the harness enforces this by only
   dispatching the Arranger while nothing else mutates.
4. **Mix Verifier**: renders + measures async; on FAIL it names the fix and the
   owning role; bounded to 3 render/rework loops before reporting to the user.
5. **Persist** only on a PASS verdict (`save_project`), then stop.

Concurrent dispatch rule: multiple roles may hold the engine ONLY if every one
of them is in a read-only phase (Verifier measuring a finished render, Selector
auditioning). Any mutation => one writer at a time.

## Gate contract
A role handoff WITHOUT gate evidence is rejected — send it back with the failed
gate named. Evidence is machine output (verify_part / mix_report / analyze_tuning
/ audition peaks / waveform peaks), not prose.

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
the five-role split, and record where handoffs needed human help — that list is
the Phase 1 backlog.
