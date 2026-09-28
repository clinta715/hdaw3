# Agent mechanization — compressing the agent's mechanical repetition

**Date:** 2026-09-28 · **Status:** IMPLEMENTED — all slices (S1, S1b, S2, S3,
S4, S5, S6, S7) + review-driven follow-ups (S2b, S3b, S3d, S4b, S6-followups,
S7b) shipped. **Final ledger: 317 tools / 419 RPC methods, mapped 303 /
mcp-only 14 / unresolved 0** (`node tools/rpc_parity_map.mjs`). Close-out
handoff: [`docs/handoffs/2026-09-28-agent-mechanization-shipped.md`](../handoffs/2026-09-28-agent-mechanization-shipped.md). ·
**Origin:** the v0.39.2 close-out session (B1–B8 + B7) — every item below is a
repeated cost measured during that work, with the evidence named.

**Parity rule binds:** every new tool = shared `src/common/` shaper + MCP/RPC
twins + `node tools/rpc_parity_map.mjs` regeneration (AGENTS.md).

## 1. Tool introspection — `tool_help {name}`

**Pain:** learning `load_project`'s arg name (`filePath`, not `path`),
`export_audio`'s unit convention, and `set_clip`'s fields took
`tools/list` → schema dump → description dump (3+ round trips) plus one failed
call. **Slice S1a (central unit annotation + 1 example per tool, shipped):**
put `units` into every numeric field's schema description and 1 example per tool.
**Slice S1b (implemented, `src/mcp/McpTools_Engine.cpp`):** `tool_help {name}`
returns that tool's `tools/list` entry verbatim — `{name, description,
category, inputSchema (units included)}` — because both read the SAME
stored `McpToolDef` (the schema was enriched once at registration), so the
payload cannot drift from `tools/list`
(asserted by `ToolRegistry.ToolHelpReturnsTheExactToolsListEntry`). An unknown
name is refused in-band with the shared text `unknown tool <name>` (MCP_ONLY
ledger row — it describes the MCP tool registry, which the RPC surface does not
have, the `engine_info`/`whoami` precedent).

**ITEM B — the example lives IN the schema.** The per-tool shape example is the
STANDARD JSON Schema annotation `inputSchema.examples` (an ARRAY of one object),
injected by `McpServer::registerTool` from `toolunits::buildToolExample`; there
is NO ad-hoc top-level `example` key on a `tools/list` entry (a strict MCP client
may strip/ignore a non-standard top-level key). A ZERO-ARG tool (no `properties`)
carries `examples: [{}]` — always exactly one example object.

## 2. Project archaeology — `query_notes` / `query_clips`

**Pain:** "which notes sound at beat 657.5, on which track, with which IDs"
required four regex passes over the `.hdaw` XML (note-tag vocabulary miss,
attribute-order miss, multi-line tags). **Implemented (S2)** in ONE shared
shaper `src/common/ProjectQuery.{h,cpp}`, behind the MCP tools AND their twins
`read.queryNotes` / `read.queryClips` (which share `HDAW::readBeatWindowArgs`):
`query_notes {startBeat, endBeat, trackIndex|trackID}` and
`query_clips {startBeat, endBeat}` returning structured rows in PROJECT
(absolute) beats (`noteId, clipId, trackIndex, trackID, clipName, absBeat,
endBeat, localBeat, durationBeats, pitch, velocity, occurrenceIndex, truncated`;
`clipId, trackIndex, trackID, name, type, startBeat, endBeat, durationBeats,
muted, gain`) — the window is an INTERVAL OVERLAP
(`absStart < endBeat && absEnd > startBeat`), so a note that starts before the
window but sustains into it is returned, its span clamped to its clip
(`truncated` when the clip cut the tail). The agent asks in beat-space, gets
edit-ready IDs.

## 3. Batch mutations — `set_notes_gain` / `set_clips_edit`

**Pain:** 52 single-item `set_clip`/`set_note_gain` calls per verification run
× 3 runs. The repo's own performance rule #1 is "batch RPCs, not N loops";
the MCP surface lacks the batch verbs.

**Implemented (S3).** Both verbs run through ONE ProjectCommands entry point
(`setNotesGain` / `setClipsEdit`), so the MCP tool and the RPC twin
(`project.setNotesGain` / `project.setClipsEdit`) share one implementation and
one undo unit:

* `set_notes_gain {noteIds:[…], gain}` — per-note gain on every id.
* `set_clips_edit {edits:[{clipId, start?, duration?, gain?, fadeIn?, fadeOut?, name?, looping?}…]}`
  — a per-clip PARTIAL edit; an unset field leaves that property untouched.
  `start`/`duration` are beats, `fadeIn`/`fadeOut` seconds, `gain` a scalar.

Both return `{ok, applied}` and share one surface-neutral refusal text. The
whole batch is validate-then-apply: an EMPTY array (`noteIds must not be empty`
/ `edits must not be empty`) or ANY unknown id (`unknown noteId N` /
`unknown clipId N`) refuses the batch — nothing written, NO transaction opened
(no undo unit) — before any `beginTransaction`. The `edits` item schema
declares its properties AND `additionalProperties:false`, so a typo'd key is
REJECTED rather than silently dropped (lesson 34); the RPC route reproduces the
validator's bytes via the shared parser (`src/common/BatchEditJson.h`).

## 4. Unit-tagged fields (lesson 1's class at the tool boundary)

**Pain:** `list_notes` speaks beats, `export_audio` seconds, `mix_report`
sections seconds — every call needed a manual ×60/140 conversion.

**Implemented (S6).** ONE resolver, adopted by BOTH surfaces through the SAME
choke points, plus a central schema injection so the accepted keys and the
resolver cannot drift.

* **The resolver** — `src/common/WindowUnitArgs.h`. For each endpoint it accepts
  BOTH explicit spellings plus the tool's own key: the MUSICAL spelling
  (`startBeat`/`endBeat`, `lengthBeat`, `durationBeat`, `timeBeat`, `timesBeat`,
  `newStartBeat`, `positionBeat`, `loopStartBeat`/`loopEndBeat`, `startTimeBeat`,
  `noteDurationBeat`, `startGteBeat`/`startLtBeat`, `fadeInBeat`/`fadeOutBeat`, …),
  the WALL-CLOCK twin (`*Sec`/`*Seconds`), and — for the tools whose window key
  is bare — that bare key, read per the optional top-level
  `unit: "beats"|"seconds"` (default: the tool's documented unit). Seconds are
  converted at the project BPM; the canonical internal window is BEATS, and the
  resolved value is written back into the key each handler already reads (so no
  handler changed). The `*Beat`/`*Sec` spellings are DECLARED into the stored
  schema by the same table, so the MCP validator accepts them.
* **The conflict rule.** Any two present spellings of the SAME endpoint whose
  converted beats differ by more than 1e-9 are REFUSED
  (`conflicting window units: <keyA> and <keyB> disagree`, −32602 on the route,
  the same string as the MCP tool error) — never silently picked. Agreeing
  spellings are accepted. A `unit` value other than `beats`/`seconds` is refused
  (`invalid unit: <v> (expected "beats" or "seconds")`).
* **Conversion source.** Every spec converts with the project BPM, EXCEPT
  `mix_report` / `mix_verdict` / `mix_diff` (`useArgsBpm`): they describe a
  RENDERED FILE and take their own `bpm` argument, so when it is present and > 0
  that tempo is used (a beat window for a file rendered at another tempo must not
  be converted with the project tempo); without it the project BPM applies.
* **Two surfaces, one resolver, route-keyed specs.** The MCP path runs it in
  `McpServer::handleToolsCall` BEFORE schema validation (so an accepted ALIAS
  spelling also satisfies a schema that still REQUIRES the canonical key — the
  resolver writes the resolved value into the key the handler reads, and the
  validator's own refusal text is untouched), then runs the handler; the
  JSON-RPC path runs the SAME function in `FrontendRouter::dispatch` (before the
  namespace routers). Where a route names its window differently from the tool,
  the route gets its OWN spec keyed on the route's argument names
  (`project.addMidiClip`/`addAudioClip` bare `start`/`duration` +
  `startBeat`/`startSec`/`durationBeat`/`durationSec`; `project.moveClip` and
  `moveClipWithOverlap` `newStart` + `newStartBeat`/`newStartSec`;
  `project.importAudioFile` `start` + `startBeat`/`startSec`; `project.addNote`
  `startBeat`/`durationBeats` + `startSec`/`durationSec`; `project.setNoteStart`
  / `setNoteDuration`; `project.setClipStart`/`setClipDuration`/`setClipFadeIn`/
  `setClipFadeOut`; the composition generators
  `startBeat`/`lengthBeats`/`durationBeats` + `*Sec`). Specs carry
  `surfaceMcp`/`surfaceRpc` flags, so a route-only spec is never injected into
  the MCP schema and an MCP-only spec never runs on the route. Routes with NO
  time window (read.getNotes, project.duplicateClip) get none — stated, not
  invented; route-only bulk/composite methods (addClips, paintClips, moveClips,
  duplicateClipTo, …) are not tool twins and keep their existing spelling.
* **The echo.** A tool whose response is a JSON object gains `"unit"` (the unit
  the caller's window was actually interpreted in); a plain text response is
  left untouched unless the tool opts in with `echoText` — `export_audio`'s
  status line appends `… unit=beats`. The four tools whose payload was a bare
  id / status line were CUT OVER to a JSON object so they carry it like the
  rest: `add_arranger_region` → `{"regionID":<id>,"unit":<u>}`;
  `set_arranger_region_bounds` / `transport` / `seek` → `{"ok":true,"unit":<u>}`
  (each tool's bare key defaults to SECONDS, so a bare call echoes "seconds" and
  the `*Beat` spelling echoes "beats"; `transport` with NO loop argument has no
  window to report and stays the bare `{"ok":true}`).
* **Ordering.** The resolver can refuse `end <= start` (shared
  `startBeat/endBeat invalid: endBeat must be greater than startBeat`); it is
  enabled ONLY on the three envelope generators, which had no ordering refusal
  of their own. Tools that already carry one (`automation_preset`'s
  `bad window`, `query_notes`/`query_clips`' `beat window invalid`, export_audio's
  `end <= 0 = project end` sentinel) keep theirs — byte-pinned by tests.
* **Adoption.** The `unit`/twin keys are declared from the SAME spec table
  (`HDAW::injectWindowUnitProperties`, called once in
  `McpServer::registerTool` before the S1 unit annotation), so the validator
  accepts exactly the keys the resolver understands and the S1 gates A/E stay
  green (`kExpectedUnits` carries a row per new spelling).
* **Migrated tools (57 MCP specs + 11 route specs).** The six ambiguous tools
  (`automation_preset`, `apply_movement_plan`, `generate_automation_envelope`,
  `generate_clip_gain_envelope`, `generate_clip_cc_lane` — bare `start/end`,
  default BEATS — and `export_audio` — bare `start/end`, default SECONDS, plus
  `startBeat`/`endBeat` and `unit`); the already-explicit beat windows
  (`verify_window`, `verify_part`, `query_notes`, `query_clips`,
  `create_section`, `duplicate_region`, `ripple_delete`, `insert_silence`,
  `param_verity`, `param_verity_corpus`, `tone_verity`, `place_patterns`,
  `import_audio_file`, `add_instrument_part` — each gained the `*Sec` twin);
  the bare-window clip/note/composition verbs (`add_midi_clip`, `add_audio_clip`,
  `set_clip`, `move_clip`, `duplicate_clip`, `add_note`, `add_notes`, `set_note`,
  `generate_chord`, `generate_phrase`, `generate_progression`,
  `generate_rhythm_pattern`, `generate_psytrance`, `add_automation_point`,
  `set_automation_points`, `slice_clip_at_times`, `add_cc_point`, `set_cc_point`,
  `audition_plugin`, `list_notes`, `remove_notes`, `set_note_velocities`); and
  the seconds-default analysers (`mix_report`, `mix_verdict`, `mix_diff`
  sections, `add_arranger_region`, `set_arranger_region_bounds`, `transport`
  loop, `seek`).
* **Deliberate exclusions (each with its reason).** Rate/fraction values
  (`repeatRate`, `arpeggioRate`, `set_lfo_param`'s string rate) are not
  positions; `timeoutMs`/`waitTimeoutMs`/`attackMs*` are machine time;
  `durationMin`/`durationMax` (library file-duration filters), `bpmMin`/`bpmMax`
  (metadata filters), `sampleRate`, `set_audio_buffer_size.size` (frames),
  `tone_verity.f0Hz`/`f0CentsMax` (pitch) have no musical-position twin;
  generation BAR counts (`generate_arrangement.bars`, `set_song_plan.bars`,
  `generate_rhythm_pattern.bars`, …) are bar-quantized generation extents, not
  caller-side windows; `place_patterns.startBar`/`lengthBars` stay bars (its
  `startBeat`/`startSec` twin IS present); `seek.position` and the `transport`
  loop keys are documentary UI positions (MCP-only, no RPC twin) — they accept
  the `*Beat`/`*Sec` spellings and now echo the unit on their status payload
  (`{"ok":true,"unit":<u>}`), but have no RPC twin to mirror it.
* **Tests:** `WindowUnitParityTest.*` / `WindowUnitResolver.*` — the conflict
  refusal byte-identical on both surfaces (each disagreeing PAIR: bare↔`*Sec`,
  bare↔`*Beat`, `*Beat`↔`*Sec`), all three spellings agreeing, the `query_notes`
  byte-equality and the `automation_preset` count-equality, the `export_audio`
  `unit:"beats"` mode and its seconds default, the JSON/text echo, the unchanged
  bare defaults on the six tools, the new inverted-window refusal, the
  `tools/list` assertion that every accepted spelling is DECLARED,
  `project.addMidiClip`/`project.moveClip` route twins (three spellings + a
  refused conflict, MCP bytes == route bytes), the four S6c payload cutovers
  (`add_arranger_region` / `set_arranger_region_bounds` value+unit, byte-identical
  on both surfaces; `transport` / `seek` value+unit following the caller's
  spelling), and the per-argument-BPM conversion for the mix_* sections.
* **Ledger:** `node tools/rpc_parity_map.mjs` → 317 tools / 419 RPC methods,
  mapped=303, mcp-only=14, unresolved=0 (argument-only changes need no regen).
* **Docs:** `docs/testing-mcp.md` § "Time windows: ask in beats or seconds".

## 5. Render→measure→compare loops — `verify_window` / `render_and_verify`

**Pain:** 5 verification iterations = export + measure + parse each; the
decisive per-channel ceiling check had to run OUTSIDE the engine, and the
windowed renders said "0 clamps" while the full render carried 32.

**Implemented (S4).** `verify_window {startBeat, endBeat, targets?|expect?,
outputPath?, timeoutMs?}` (RPC `composition.verifyWindow`) renders + measures +
compares in one call; `render_and_verify {outputPath, targets?, timeoutMs?}`
(RPC `export.renderAndVerify`) = full render + `mix_verdict` inline.

* **Full render, window measurement — never a window-only render.** Both tools
  render the WHOLE project (0 .. `calculateProjectDuration`) through the export
  path on a tree COPY, and then measure only the requested window. A windowed
  render is NOT predictive: plugin state re-bakes per window, and the v0.39.2
  close-out measured **0 clamps** on a windowed render of a file whose full
  render carried **32 exact-FS frames**
  (`docs/handoffs/2026-09-28-v0.39.2-backlog-closeout.md` §3) — a window-only
  render would have reported a false pass for the exact metric the loop exists
  to check.
* **The WINDOW's stats are what get gated.** `buildWindowReportPayload`
  (`src/common/MixReportJson.{h,cpp}`) runs the SAME analyzer over the window
  and promotes the window's `duration/peak/rms/bands/kickProminence/
  ceilingHitPct/ceilingHitFrames` to the payload ROOT, so `applyTargetGates`
  gates the window. Feeding the full-render WAV plus one window to
  `buildMixReportPayload` would have gated the WHOLE SONG's numbers instead.
  Measured live: window beats 0→4 (clamped clip) `ceilingHitPct` 72.75 /
  `targetsOk` false; window beats 8→12 (quiet clip) 0 / true; `mix_report` on
  the SAME full render 16.17 / false. `buildMixReportPayload` itself is
  untouched — `mix_report`/`mix_verdict` bytes are unchanged.
* **Cost.** A `verify_window` is about ONE full export (it WAITS for the render
  — measure-and-answer, bounded by `timeoutMs`, default 600000). Renders are
  launched by the ONE shared helper `HDAW::launchProjectRender` /
  `renderProjectAndWait` (`src/common/RenderLaunch.h`), which `export_audio` and
  `export.audio` also call, so the launch path cannot drift between surfaces.
* **The WAV is kept** (the plan wants the path for A/B) and is the CALLER's to
  clean up; an omitted `outputPath` lands in the OS temp dir. Payload
  `{ok, wavPath, window:{startBeat,endBeat,startSec,endSec,durationSec},
  report, targetChecks, targetsOk}`.
* **`verify_window` expectation keys are STRICT.** `targets`/`expect` accept
  ONLY `rmsMin`, `masterRms`, `ceilingHitPctMax`, `kickProminenceMin` and
  `targetDurationSeconds`. `rmsMin` is a LINEAR RMS floor — the SAME units as
  the report's root `rms` and as `masterRms` — checked at-least; `masterRms` is
  the linear mono-downmix RMS within 5%; `ceilingHitPctMax` a percent ceiling;
  `kickProminenceMin` 0..1 at-least; `targetDurationSeconds` seconds within 2s.
  An unknown key is refused with
  one shared `unknown expectation key <key>` (-32602) on BOTH surfaces, BEFORE
  any render (`src/common/RenderToolArgs.h`); the permissive
  `applyTargetGates` (`src/common/MixReportJson.cpp`) is deliberately UNCHANGED
  for `mix_report`/`mix_verdict`, whose `targets` come from the song brief.
  `rmsMin` is the one new gate (a positive floor fails a silent window's rms 0;
  0.0 passes). Pinned by `VerifyWindowParity.RmsMinFloorsTheWindowRms`,
  `VerifyWindowParity.RmsMinFailsSilenceAndPassesAudible`,
  `VerifyWindowParity.UnknownExpectationKeyIsRefusedBeforeAnyRender`.
* **`render_and_verify` = render + the SAME `mix_verdict` composition.**
  `render_and_verify {outputPath, targets?, timeoutMs?, fromPlan?, dropBuildRatio?=0.9,
  introSeconds?=2}` runs ONE shared implementation
  (`src/common/RenderAndVerify.h`) — full render through the same launcher, then
  the verdict inputs resolved by the SAME helper `mix_verdict` uses
  (`src/common/MixVerdictInputs.h`: windows + structure audit + loudness map +
  modulation coverage + intro window + targets). Its `fromPlan` default is FALSE,
  MIRRORING the `mix_verdict` tool/route default, so `render_and_verify
  {outputPath}` (no extra args) equals `mix_verdict {filePath: outputPath}` (no
  extra args) even on a project WITH a song plan; pass `fromPlan:true` to BOTH to
  gate the plan. The `verdict` is BYTE-IDENTICAL to
  `mix_verdict {filePath: <produced>, fromPlan, ...}` — the loudness /
  structure-variety / modulation gates are no longer omitted. With
  `fromPlan:true` on a plan-less project it FALLS BACK to the whole-file verdict
  (it has already rendered) rather than refusing. Pinned by
  `RenderAndVerifyParity.VerdictEqualsMixVerdictWithAPlan` /
  `RenderAndVerifyParity.VerdictEqualsMixVerdictWithoutAPlan`.
* **Ledger:** `node tools/rpc_parity_map.mjs` → 317 tools / 419 RPC methods,
  mapped=303, mcp-only=14, unresolved=0 (argument-only changes need no regen).
* **Ledger fix (part of S4, ITEM 0).** `begin_batch`/`end_batch` used to ALIAS
  `project.beginTransaction`/`endTransaction` — an overstatement, because the
  batch adds the collapse flag and the MCP tool is stdio-gated. The RPC routes
  `project.beginBatch`/`project.endBatch` now call the SAME
  `ProjectCommands::beginBatch`/`endBatch` entry points, so those rows are exact
  matches; the note records the ONE deliberate asymmetry (the MCP tool
  additionally refuses on a non-stdio transport; the route does not).
  Tests: `BatchEditRpcTest.RpcBeginBatchHasNoTransportGateWhileTheToolHas`,
  `BatchEditRpcTest.BatchTwinRefusalsShareTheExactBytes`,
  `BatchEditRpcTest.BeginEndBatchRpcRouteCollapsesIntoOneUndoUnit`;
  `VerifyWindowParity.*` / `RenderAndVerifyParity.*` are the render-tool twins.
* **Ledger:** `node tools/rpc_parity_map.mjs` → 317 tools / 419 RPC methods,
  mapped=303, mcp-only=14, unresolved=0.
* **Docs:** `docs/testing-mcp.md` § "Render → measure → compare".


## 6. Lifecycle bootstrap — `whoami` + `--project`

**Pain:** the transport dance (adapter vs HTTP vs stdio), crash-capture
wrapper failures, stale-binary traps (`test` builds only `hdaw_tests`, `all`
covers headless), copy-on-launch semantics — several probes to answer "what's
running?".

**Implemented (S5).**

`whoami` (`src/mcp/McpTools_Engine.cpp`, registered by `registerWhoamiTool`)
returns ONE compact JSON object:

* the whole `engine_info` payload — `runningBinaryPath`, `runningMtime`
  (epoch seconds), `runningSize`, `exporting`, `version`, plus
  `buildBinaryPath`/`buildMtime`/`buildSize`/`stale` when the optional
  `buildBinaryPath` arg is given and
  `expectedVersion`/`actualVersion`/`versionMismatch` when `expectedVersion`
  is given;
* `transport` — the name each `McpServer` construction site sets
  (`setTransportName`): `"stdio"` (main.cpp / main_headless.cpp MCP-stdio),
  `"http"` (`AudioEngine::startMcpHttp`), `"unknown"` when never set;
* `projectPath` — the file loaded/saved THIS session (`""` when none), from
  `AudioEngine::getProjectFilePath()` → `ProjectCommands::getProjectFilePath()`
  (set on a successful `saveProject`/`loadProject`, cleared by `newProject()`;
  message-thread only, never persisted);
* `projectName`, `trackCount`, `clipCount`.

Both tools share ONE builder (`mcp::buildEngineInfoPayload` in
`src/mcp/McpTools_Engine.{h,cpp}`), and a test
(`EngineTools.WhoamiMatchesEngineInfoForSameArgs`) requires every `engine_info`
key to appear in `whoami` with an equal value for the same args — the two
payloads cannot drift. `whoami` takes the same two optional string args, so the
S1 unit gates need no new rows.

`HDAW_headless --mcp-stdio --project <file>` loads that project right after the
deferred `engine.initialize()` and the plugin-scan block, via
`engine.getProjectCommands().loadProject(path)`:

* BOTH `--project <file>` and `--project=<file>` are accepted
  (`src/common/HeadlessArgs.{h,cpp}`, argv parsing isolated from the existing
  flag helpers);
* a `--project` with no usable value (nothing after it, another `--…` flag, or
  an empty `--project=`) is a HARD error: `main` logs the reason and returns 2
  before any engine work — never a silent skip into an empty project;
* a load failure logs `--project: FAILED to load <path>` and calls
  `QCoreApplication::exit(2)`, so the caller sees a non-zero exit instead of a
  silently empty project;
* `--project` without `--mcp-stdio` logs that it is ignored.

`scripts/mcp_call.py` gained `--engine-args "<extra>"` (extra engine spawn argv
appended after `--mcp-stdio`, so the bootstrap is verifiable end-to-end) and
now reports an engine that exited before answering (exit code + debug-log hint)
instead of hanging.

**Ledger:** `whoami` is an `MCP_ONLY` row (mirroring `engine_info`):
"MCP server introspection: engine binary/version (superset of engine_info) plus
the MCP transport it is serving on — the RPC surface cannot report its own
transport, and the session project fields are covered by `read.snapshot`."
(`node tools/rpc_parity_map.mjs` → final 317 tools / 419 RPC methods,
mapped=303, mcp-only=14, unresolved=0.)

## 7. Workflow transactions — `begin_batch` / `end_batch`

**Pain:** stateless helper invocations forced re-load + re-apply of 52 edits
per iteration.

**Implemented (S7).**

* `begin_batch {name}` / `end_batch {verify?: {targets?, outputPath?}}`
  (`src/mcp/McpTools_Transport.cpp`, registered by `registerBatchTools`) delegate
  to `ProjectCommands::beginBatch` / `endBatch` — the SAME entry points the
  `project.beginBatch` / `project.endBatch` RPC twins use (S4 §5 turned those
  from aliases of the raw `beginTransaction`/`endTransaction` pair into exact
  twins). Both verbs run through ONE shared body: `src/common/BatchEnd.h` parses,
  SEALS, then (optionally) verifies, so the tool and the route cannot drift.
* **One undo unit + the optional verify hook.** `end_batch` (both surfaces)
  accepts an optional `verify:{targets?, outputPath?}`: the batch is SEALED FIRST
  (`endBatch()`), and only then does the SHARED `HDAW::renderAndVerify`
  (`src/common/RenderAndVerify.h`) render the whole project and compose the
  release verdict (cost: one full render). A verification FAILURE never un-seals
  the batch — the response keeps `{ok:true, sealed:true}` and reports the failure
  in `verificationError`; on success it adds `verification:{wavPath, verdict}`.
  With no `verify` the payload is exactly `{ok:true, sealed:true}`. Argument
  errors are refused BEFORE the seal (the MCP validator pre-empts them, so the
  route must not seal either). Pinned by
  `BatchEditRpcTest.EndBatchWithoutVerifyCarriesNoVerificationKey`,
  `BatchEditRpcTest.EndBatchVerifyProducesAByteIdenticalVerdict`,
  `BatchEditRpcTest.FailedVerifyStillSealsTheBatch`,
  `BatchEditRpcTest.EndBatchArgsRefusedBeforeSealingOnBothSurfaces`.
* **The real guarantee:** while a batch is open, EVERY undo boundary a command
  draws — the command layer's own `beginTransaction`/`endTransaction` pair, and
  every internal transaction a command opens — is suppressed by the ONE choke
  point `AudioEngineCommands::transactionBoundary`
  (`src/engine/AudioEngineCommands_Undo.cpp`), so all the batch's writes land in
  ONE named undo unit: a single `undo` reverts the whole batch. Measured by
  `BatchEditRpcTest.BatchCollapsesInternallyTransactionalCommandsIntoOneUndo`
  (three internally-transactional commands, one undo) — that test would fail
  without the choke point. The audit of every `beginNewTransaction` in `src/`
  routed them all through the choke point, the deliberately-UNPAIRED
  `createBus`/`createSend` pair included: `createSend` still joins `createBus`'s
  unit when no batch is open, pinned by
  `BatchEditRpcTest.NoBatchBehaviourIsUnchanged`.
* **State is a flag, never a counter** (`batchActive_` / `batchName_`): an
  unpaired-by-design boundary cannot leak it, so a batch always closes.
* **Isolation:** engine-global and ONE-AT-A-TIME. `begin_batch` is refused
  unless the server's transport is `stdio` (the stdio process owns a dedicated
  engine with no WebSocket frontend branch, so its one client is the only
  writer); a second `begin_batch` while one is open is refused naming the open
  batch; `end_batch` with none open answers `no open batch`; `whoami` reports
  `batchOpen` / `batchDepth` / `batchName`. A command FAILURE does not close the
  batch — call `end_batch` on the failure path too. (Relaxing the stdio gate
  would need a per-request ownership token; deliberately not built.)
  `project.beginTransaction`/`endTransaction` stay the RPC grouping path and are
  not batch-gated — an RPC group opened while a batch is open joins it
  (engine-global state, documented).
* **Docs:** the long-lived stdio session is the canonical workflow mode — see
  `docs/testing-mcp.md` § "Canonical agent workflow".
* **Ledger:** `begin_batch` → `project.beginBatch`, `end_batch` →
  `project.endBatch` — TRUE twins (S4, §5): the routes call the SAME
  `ProjectCommands::beginBatch`/`endBatch` entry points and share the refusal
  texts; the ONE deliberate asymmetry (the tool additionally refuses on a
  non-stdio transport) is recorded in the ledger note. Final ledger after all
  slices + follow-ups (`node tools/rpc_parity_map.mjs`): 317 tools / 419 RPC
  methods, mapped=303, mcp-only=14, unresolved=0.

## Slices (priority order)

| Slice | Items | Effort |
|---|---|---|
| S1 | #1 units in schemas + 1 example per tool (in `inputSchema.examples`) — injected by ONE central hook (`McpServer::registerTool`) + a coverage-gate test; 'trivial' means 'no per-tool edits', not 'no work' | trivial |
| S2 | #2 query_notes / query_clips | small (command layer) |
| S3 | #3 batch mutations | small |
| S4 | #5 verify_window + render_and_verify | medium |
| S5 | #6 whoami + `--project` | small |
| S6 | #4 unit-tagged fields (schema migration) | medium (touches many tools) |
| S7 | #7 batch transactions | medium |
