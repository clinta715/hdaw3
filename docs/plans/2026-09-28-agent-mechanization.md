# Agent mechanization — compressing the agent's mechanical repetition

**Date:** 2026-09-28 · **Status:** PROPOSAL (slices ready to schedule) ·
**Origin:** the v0.39.2 close-out session (B1–B8 + B7) — every item below is a
repeated cost measured during that work, with the evidence named.

**Parity rule binds:** every new tool = shared `src/common/` shaper + MCP/RPC
twins + `node tools/rpc_parity_map.mjs` regeneration (AGENTS.md).

## 1. Tool introspection — `tool_help {name}`

**Pain:** learning `load_project`'s arg name (`filePath`, not `path`),
`export_audio`'s unit convention, and `set_clip`'s fields took
`tools/list` → schema dump → description dump (3+ round trips) plus one failed
call. **Slice S1 (docs-only, zero engine code):** put `units` into every
numeric field's schema description and 1 example per tool. **Slice S2:**
`tool_help {name}` returning schema + units + example in one call.

## 2. Project archaeology — `query_notes` / `query_clips`

**Pain:** "which notes sound at beat 657.5, on which track, with which IDs"
required four regex passes over the `.hdaw` XML (note-tag vocabulary miss,
attribute-order miss, multi-line tags). **Proposal:**
`query_notes {startBeat, endBeat, track?}` and
`query_clips {startBeat, endBeat}` returning structured rows
(`noteId, track, clipId, absBeat, pitch, velocity`) — the agent asks in
beat-space, gets edit-ready IDs.

## 3. Batch mutations — `set_notes_gain` / `set_clips_edit`

**Pain:** 52 single-item `set_clip`/`set_note_gain` calls per verification run
× 3 runs. The repo's own performance rule #1 is "batch RPCs, not N loops";
the MCP surface lacks the batch verbs. **Proposal:**
`set_notes_gain {noteIds:[…], gain}` and
`set_clips_edit {edits:[{clipId, fadeIn?, gain?}…]}` — one undo unit, one
round trip.

## 4. Unit-tagged fields (lesson 1's class at the tool boundary)

**Pain:** `list_notes` speaks beats, `export_audio` seconds, `mix_report`
sections seconds — every call needed a manual ×60/140 conversion. **Proposal:**
explicit `startBeat`/`startSec` fields everywhere + responses echoing the
unit; a `unit:"beats"` mode on `export_audio` alone would have removed a
dozen conversions in one session.

## 5. Render→measure→compare loops — `verify_window` / `render_and_verify`

**Pain:** 5 verification iterations = export + measure + parse each; the
decisive per-channel ceiling check had to run OUTSIDE the engine, and the
windowed renders said "0 clamps" while the full render carried 32. **Proposal:**
`verify_window {startBeat, endBeat, expect:{rmsMin, ceilingHitPctMax}}` →
render + measure + compare in one call, returning the WAV path for A/B
(extends `verify_part` with ceilingHitPct + expectations); and
`render_and_verify {outputPath, targets?}` = full render + `mix_verdict`
inline (the "re-render + re-verdict" loop as one tool).

## 6. Lifecycle bootstrap — `whoami` + `--project`

**Pain:** the transport dance (adapter vs HTTP vs stdio), crash-capture
wrapper failures, stale-binary traps (`test` builds only `hdaw_tests`, `all`
covers headless), copy-on-launch semantics — several probes to answer "what's
running?". **Proposal:** `whoami` → version + binary mtime + transport +
loaded project + stale flag (the lesson-15/29 checks as one tool); and
`HDAW_headless --mcp-stdio --project <file>` for one-shot session bootstrap.

## 7. Workflow transactions — `begin_batch` / `end_batch`

**Pain:** stateless helper invocations forced re-load + re-apply of 52 edits
per iteration. **Proposal:** document the long-lived stdio connection as the
canonical workflow mode, and add `begin_batch`/`end_batch` (one undo unit +
optional verify hook) so multi-edit workflows are atomic and reviewable.

## Slices (priority order)

| Slice | Items | Effort |
|---|---|---|
| S1 | #1 docs-only (units in schemas + examples) | trivial |
| S2 | #2 query_notes / query_clips | small (command layer) |
| S3 | #3 batch mutations | small |
| S4 | #5 verify_window + render_and_verify | medium |
| S5 | #6 whoami + `--project` | small |
| S6 | #4 unit-tagged fields (schema migration) | medium (touches many tools) |
| S7 | #7 batch transactions | medium |
