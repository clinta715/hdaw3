# MCP dogfood: compose a song, record what the surface needs (2026-09-21)

Written after actually composing a psytrance sketch **through the MCP surface only** (via the
project's `hdaw` MCP gateway), to find out where the agent-facing surface fights the user.
Everything below is observed, not theorised — commands, outputs and the exact payloads are
quoted where it matters.

## What was composed

32 bars @ 145 BPM, A natural minor, style `full-on`, seed 20260921 — sections
intro(8) / build(8) / drop(16), five internal-synth role tracks (no samples in a fresh
project), ten section×role cells, filled to ten clips:

1. `new_project`
2. `apply_song_brief` (bpm/keyRoot/scaleMode/style/seed/totalBars/sections) → `regionsCreated:3`
3. `add_track_with_fx` ×5 — Kick `fm_synth`, Bass `growl_bass`, Hats `fm_synth`, Arp `psyarp`, Pad `psy_fm`
4. `set_cells` (batch, 10 cells) → `{ok:true, count:10, failed:0}`
5. `fill_cells {mode:"all"}` → 10 clips, 18–136 notes each, per-cell `seedUsed`
6. `audit_song_structure` → `boredomSpans:1`, `firstDropHasMotif:true`,
   `allDropsHaveBackbeat:false` (`dropsMissingBackbeat:["drop"]`) — for a MIDI-only sketch
   with no clap/snare cell, correct and useful
7. `audit_modulation_coverage` → all 5 tracks `needsAttention` (`no-lfo`, `lanes-disabled`)
8. `export_audio {wait:true}` → *worked in a deviceless session* (offline render of the tree copy)
9. `mix_report {fromPlan:true}` → intro 0.109 / build 0.190 / drop 0.242 RMS,
   `loudnessGates.ok:true` (drop/build 1.27 ≥ 0.9)
10. `analyze_tuning {role:"bass"}` → `pass:true`, centroid 173.8 Hz, "within target"
11. `save_project` → `compositions/mcp-dogfood-2026-09-21.hdaw`

**The pipeline is real.** Ten cells became ten clips with provenance and seeds in two calls,
the audits gave actionable findings, and the verification verbs (`mix_report` from the plan,
`analyze_tuning` per role) are the right abstractions. The rest of this note is what got in
the way.

## P1 — small, concrete surface fixes (recommended next slice)

1. **`fill_cells` / `reroll` report success when there is nothing to do — including the
   dangerous case.** `fill_cells {mode:"unfilled"}` with nothing left returns
   `{cells:[],failed:0,filled:0,ok:true,skippedLocked:0}`. The *same* shape comes back when
   **no cells exist at all** — which is what a failed `set_cells` leaves behind. In this run
   that silently produced a "song" whose audit showed `soundingRoles: []` for every section.
   *Proposal:* when zero cell recipes are defined, return an explicit `warning`
   ("no cell recipes defined — did set_cells fail? nothing was written") or `noCells:true`;
   keep `ok` semantics. A one-line guard would have caught my mistake immediately.
2. **`mix_report` reports `peak: 1` but no clipping verdict.** Top-level keys are
   `bands, duration, kickProminence, measurementSuspicious, peak, rms, sampleRate, sections`
   — an agent must interpret the float to notice the mix slams the ceiling. (The `issues`
   array exists but only inside `loudnessGates`, i.e. build-vs-drop, so its name over-promises.)
   *Proposal:* a top-level `clipping` boolean (peak ≥ ~1.0) and/or move the general issues to
   the top level.
3. **Inconsistent output formats between sibling tools.** `add_track` returns compact JSON
   (`{"trackId":N,"routed":1}`) while `add_track_with_fx` returns a text line
   (`trackId=0 routed=1 fxType=fm_synth`). Same family, two formats; agents must special-case.
   *Proposal:* emit the `add_track` JSON shape plus `fxType`.
4. **`add_instrument_part` cannot choose the instrument.** Its schema has no
   `instrument`/`fxType` — only `pluginId` — and the default instrument is undocumented, so
   the "one command per part" path cannot select `fm_synth` / `growl_bass` / `psyarp` /
   `psy_fm` / `sub_synth` / `sampler`. *Proposal:* add the enum (+ document the default),
   reusing the same fxType list as `add_fx`/`add_track_with_fx`.
5. **Output shapes are undiscoverable via `hdaw_describe_commands`.** It returns input
   schemas only, so I guessed twice and lost two round trips: `list_tracks` returns a **bare
   array** (my `.tracks` key found nothing), and `apply_song_brief`'s argument is `brief`
   (not `songBrief`/`plan`). *Proposal:* a one-line `returns` summary per command, or an
   `outputSchema` for the read/list family.

## P2 — gateway / metadata (needs the gateway owner, or local doc)

6. **The `hdaw` gateway double-wraps every result.** `hdaw_invoke_command` returns an MCP
   envelope whose `content[0].text` is the engine's *own* envelope JSON **as a string**, so
   any structured consumption needs two unwraps. Text-only clients never notice; anything
   that parses JSON (agents, scripts) must know the nesting. *Proposal (gateway side):*
   return the downstream result structurally, or document the nesting in the gateway's tool
   descriptions. (Gateway reports version 2.7.4; not this repo.)
7. **`describe` flattens array-typed params.** `set_cells.cells` is rendered as bare
   `{"type":"array"}` although the tool declares full item properties — so the richest
   argument in the batch workflow is invisible from the schema. *Proposal:* pass item
   schemas through.
8. **Enum drift between siblings.** `add_track_with_fx.fxType` lists 14 types and omits
   `sub_synth`; `add_fx`'s description includes it. *Proposal:* one shared source for the
   fxType list feeding both registrations.

## P3 — workflow shape (worth a design conversation, not a quick patch)

9. **No batch gain-staging.** With `peak:1` the fix is `auto_gain_to_target` per track — five
   round trips and five undo units for one musical intent. We already have the batch
   precedent (`apply_movement_plan`). *Proposal:* `auto_gain_tracks {targets:[{trackId,
   targetRms}], allowGlobalScale}` in one undo unit, reusing the per-track command.
10. **No single "is this mix release-ready?" verdict.** The loop has four separate verifiers
    (structure, modulation coverage, spectrum/loudness, tuning) and the Mix Verifier role's
    checklist lives in prose. *Proposal:* a `mix_verdict` (or `verify_song`) that composes the
    existing four into one report with an overall `ok` + `issues[]`, so "did I finish?" is one
    call instead of four with hand-written thresholds.
11. **Brief alias → section-kind mapping is undocumented.** `apply_song_brief` says aliases
    (peak/outro/drop) map onto canonical kinds but not onto *which*: I passed
    `type:"drop"` and got kind `mainB` (the docs' examples suggest `mainA` for a first drop).
    *Proposal:* document the alias table in the description, or accept an explicit `kind`.

## What is already good (don't regress it)

- **Errors are precise and actionable.** The gateway's failure told me the exact parameter
  names; `set_cells` reported `"trackId required"` *per cell* without aborting the batch, so
  the fix was obvious and the other cells' status was unambiguous.
- **The plan/cell pipeline is high-leverage**: structure pinned by `apply_song_brief`,
  content by one batch `set_cells`, execution by one `fill_cells` — and per-cell `seedUsed`
  makes variation reproducible (`reroll` gets deterministic new seeds).
- **`mix_report {fromPlan:true}` removes beat math from the agent** and the build-vs-drop
  loudness gate is exactly the kind of musical judgement a tool should own.
- **Offline render worked in a deviceless session**, and `analyze_tuning` returned a
  pass/fail plus a reason — both keep the loop usable on a headless box.

## Artifacts

- `compositions/mcp-dogfood-2026-09-21.wav` (render) and `.hdaw` (saved project) — gitignored
  by the repo's `/compositions/` rule, left on disk for inspection.
