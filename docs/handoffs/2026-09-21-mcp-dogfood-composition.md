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

## P1 — SHIPPED (2026-09-21, same session)

| # | Fix | Where | Gate |
| --- | --- | --- | --- |
| 1 | A fill that wrote nothing now says so: `noCells:true` + warning when no cell recipes exist (the state a FAILED `set_cells` leaves), `nothingToDo:true` + warning when the call matched nothing | the fill/reroll payload was extracted into `src/common/SongPlanView.cpp` (`cellFillBatchJson`), replacing two hand-written builders — MCP `fill_cells`/`reroll` and RPC `composition.fillCells`/`rerollCells` now share it | `SongPlanRpcTest.FillCellsFlagsNoCellsWhenNoneDefined` + `...NothingToDoOnRefill` (each also asserts the MCP and RPC payloads are identical) |
| 2 | `mix_report` carries an explicit `clipping` verdict (derived from `peak` with the engine's documented 0.999 blast threshold — `HDAW::MixReport` itself has no clipping member, only `BlastReport` does) | both builders: `McpTools_AudioRead.cpp` + `Router_Audio.cpp` | `McpCoverageTest.MixReport*` (field present and a bool) |
| 3 | `add_track_with_fx` returns compact JSON `{trackId, routed, fxType}` like `add_track` (was a text line), and its `fxType` enum now includes `sub_synth` (it disagreed with `add_fx`) | `src/mcp/McpTools_Track.cpp` | `McpCoverageTest.AddTrackWithFx` + the three plugin-diagnostic regexes migrated to the JSON shape |
| 4 | `add_instrument_part` can choose the internal instrument: new `fxType` param (fm_synth default; psy_fm / growl_bass / psyarp / sampler / sub_synth), validated BEFORE any mutation | `ProjectCommands::InstrumentPartParams::fxType`, `AudioEngineCommands::addInstrumentPart` (slot creation), MCP tool + RPC parse | `InstrumentPart.ExplicitFxTypeSelectsTheInstrumentSlot` (selects psy_fm; an unknown type errors **and creates no track**) |
| 5 | Output shapes documented in the descriptions that cost round trips: `list_tracks` (bare array), `list_clips` (bare array), `apply_song_brief` (argument name, returns, and the `drop`→`mainB` alias), `fill_cells`/`reroll` (full per-cell shape + the guards), `add_track_with_fx` | `McpTools_Read.cpp`, `McpTools_SongPlan.cpp`, `McpTools_Track.cpp`, `McpTools_CompositionInstrument.cpp` | descriptions only (no schema change) |

### New findings from implementing P1

- **`mix_report`'s two JSON builders have drifted — and the code claims otherwise.**
  `Router_Audio.cpp` carries the comment "Same JSON shape as the MCP mix_report tool", but the
  RPC payload adds `bandLabels` while the MCP adds `measurementSuspicious` (+ richer per-section
  fields: `boundaryPeak`, a band-energy object). `clipping` was added to both as the interim
  fix; the real fix is ONE shared builder in `src/common/` — a slice of its own, which also
  closes the parity hole for `audio.mixReport`.
- **`fill_cells`'s guards are layered, and that is worth knowing:** with NO song plan at all it
  already refuses (`"no song plan set"`), so the new `noCells` signal is specifically the
  *plan present, no cells defined* state — exactly what the failed `set_cells` produced in this
  run. Both layers are pinned in the test.
- **The new gate caught a real ordering bug in my own first cut:** the `fxType` validation
  originally sat AFTER `addTrack(...)`, so a rejected call left a track behind (breaking the
  command's "nothing is written on failure" contract). It now validates in the pre-mutation
  block and the test asserts the track count is unchanged — keep that assertion.

### Verification caveat for this session

The wider sweep (213 tests / 10 suites) reported 26 failures, all **environmental**, of two
characterised classes: the deviceless pattern (`getTrack()/tr == nullptr`, lessons 9/17 — this
box currently has no usable audio route) and transient `server->start(0)` bind failures under
load. Both were re-run solo: the `FrontendServer.AddInstrumentPart*` bind failures pass solo
(221/187 ms), and the deviceless ones reproduce solo (so they are not mine). Every
P1-specific gate passed inside that sweep.

## P2 — status (2026-09-21)

| # | Item | Status |
| --- | --- | --- |
| 6 | Gateway double-wraps every result | **UPSTREAM** — the `hdaw` gateway (v2.7.4) is not on this filesystem: nothing in this repo references `hdaw_invoke_command` (only this doc and the session log do). The engine emits ONE envelope; the gateway nests that envelope as a STRING inside its own, so any JSON consumer needs two unwraps. Client-side workaround (what the dogfood script did): unwrap `content[0].text` twice. Ask: return the downstream result structurally, or document the nesting in the gateway's tool descriptions. |
| 7 | `describe` flattens array-typed params | **UPSTREAM (gateway)** — the ENGINE is not the culprit: `McpServer` serializes `t.inputSchema` verbatim (`McpServer.cpp`, tools/list) and the tool registrations DO declare nested `items` (e.g. `set_cells.cells` has a full item schema with `required`). The flattening happens in the gateway's `hdaw_describe_commands`, so the richest argument in the batch workflow is invisible from the schema. Ask: pass item schemas through. |
| 8 | Enum drift between siblings (`add_track_with_fx` missing `sub_synth`) | **FIXED in P1** — its `fxType` enum now matches `add_fx`. |
| — | **`mix_report` had two builders, and they had drifted** (found while doing P1) | **FIXED — see below.** |

### mix_report: one builder for both surfaces (SHIPPED)

The two payloads differed in **shape** (`bands` object vs array + `bandLabels`; sections with
`boundaryPeak` + a band-energy object vs neither) and in **content** — the RPC lacked the
file-visibility guard entirely (a just-finished export's writer can hold the file with
unflushed data, so another handle reads zeros for real audio: the trap that burned the
2026-09-15 session, surfaced here as `measurementSuspicious`). The RPC's own comment claimed
"Same JSON shape as the MCP mix_report tool".

- **`src/common/MixReportJson.{h,cpp}`** (new) is now the single builder:
  `buildMixReportPayload(filePath, windows, bpm)` (file-visibility guard + retry, window
  clamping/dropping with `clampedSections`, the one payload shape incl. `clipping`) and
  `applyDropVsBuildGate(...)` (was MCP-local).
- The MCP tool delegates (its local `applyDropVsBuildGate` and ~92-line analyzer are gone —
  the file shrank by 136 lines); the RPC router delegates AND now gets the guard, the
  clamping, and the same plan-derived `structure` + `loudnessGates` extras, so `fromPlan`
  payloads match too.
- **Gate:** `McpCoverageTest.MixReportPayloadMatchesRpcTwin` — asserts the two payloads are
  **equal** for explicit windows and for `fromPlan`, plus the shared shape (`bands` is an
  object, sections carry `boundaryPeak`, both carry `clipping`), with `structure` and
  `loudnessGates` present on both. The four pre-existing `MixReport*` shape tests and the
  `McpJobs` sync/async job contract stayed green, which is what proves MCP behaviour was
  preserved by the extraction.

## P3 — gain-staging batch (SHIPPED) + verdict (pending)

### 9. Batch gain-staging — SHIPPED (2026-09-21)

`auto_gain_tracks` (MCP) / `composition.autoGainTracks` (RPC): stage MANY tracks to their own
target RMS in **ONE undo unit and one round trip**. The dogfood mix (peak 1.0) previously cost
one `auto_gain_to_target` call and one undo entry per track — undoing a gain pass took N undos.

It reuses the single-track path **verbatim** (measurement, clamping, the global-scale probe) and
only suppresses its per-track undo transaction via the `gainBatchActive_` flag, so the batch and
single paths cannot resolve to different faders (a full refactor into measure/apply halves was
avoided for that reason). A failing target (bad index, silent track, render error) is recorded
per row and does not abort the batch.

Gate: `InstrumentPart.BatchGainStagingIsOneUndoUnit` — two tracks staged to *different* targets
(both attenuating, so the write is observable), each fader matches its own target and its own
track volume, then **one `undo()` reverts BOTH**. Two things it flushed out: a target *above* a
track's raw RMS legitimately clamps the fader at 1.0 and writes nothing (the first draft of the
test asserted a change and failed — the test now pins the unclamped case explicitly), and the
ratchet gate `RpcParityRatchet.EveryLiveToolIsClassified` failed until the ledger was
regenerated — i.e. the classification ratchet behaved exactly as designed.

### 11. Brief alias → section kind — DONE in P1 #5

The description now states that alias `drop` resolves to kind `mainB` (and that an explicit
`kind` should be passed when it matters).

### 10. A single release-readiness verdict — SHIPPED (2026-09-21)

`mix_verdict` (MCP) / `audio.mixVerdict` (RPC) returns ONE verdict —
`{ok, file, gates{...}, issues[], warnings[]}` — over a rendered file:

| gate | source (every one already shared) |
| --- | --- |
| `audible` | `rms > 1e-4` (silence masks every other gate — lesson 25) |
| `clipping` | the `clipping` verdict the mix_report payload now carries (P1) |
| `loudness` | `applyDropVsBuildGate` over the plan's sections (only with `fromPlan`) |
| `structure` | `audit_song_structure`'s gates (boredom spans, drop backbeats, first-drop motif, drop-vs-build load) |
| `introBlast` | `MixReportAnalyzer::analyzeBlast` over the first `introSeconds` (default 2) |

`fromPlan: true` derives the windows AND the structure + loudness gates from the current plan;
`issues[]` carries actionable text (e.g. "clipping: peak reaches full scale — run
`auto_gain_tracks`, then re-render"); `warnings[]` carries harness caveats (`measurementSuspicious`,
or a gate that could not be evaluated — never silently dropped).

**Modulation coverage is now INCLUDED** (2026-09-21, same session): the audit moved out of the
MCP layer into `src/common/ModulationCoverage.{h,cpp}`, gained an RPC twin
(`modulation.coverage` — a new namespace, so the namespace-coverage gate now requires its
dispatch branch) and the verdict takes its payload as the `modulation` gate: uncovered sounding
tracks → `ok:false` plus an actionable issue pointing at `apply_movement_plan`, and an enabled
Volume lane adds the `set_fader_authoritative` warning. Gate:
`McpCoverageTest.ModulationCoverageMatchesRpcTwinAndFeedsTheVerdict` (identical MCP/RPC
coverage payload, the uncovered track is flagged, the verdict carries the gate and the RPC
verdict matches).

The parity ratchet ledger now records two VERIFIED aliases found here:
`audit_modulation_coverage → modulation.coverage` and `list_lfos → read.getModulationLfos`,
which is what moved the review queue 101 → 99 (`node tools/rpc_parity_map.mjs`).

Gate: `McpCoverageTest.MixVerdictFlagsClippingAndMatchesRpcTwin` — a full-scale render fails the
clipping gate and produces issues while a quiet render passes it, and the RPC twin returns the
IDENTICAL verdict. `src/common/MixVerdict.{h,cpp}` composes only existing shared pieces (no new
analysis maths).

## Artifacts

- `compositions/mcp-dogfood-2026-09-21.wav` (render) and `.hdaw` (saved project) — gitignored
  by the repo's `/compositions/` rule, left on disk for inspection.
