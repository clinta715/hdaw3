# Composition toolkit — generative, randomization, modulation, hardware VA

Moved out of AGENTS.md (2026-09-22). The canonical deep-dive for the
hardware VA suite is hardware-va-suite.md (§9 = per-engine measured
status — moved 2026-09-24 to va-suite-status-log.md); the psytrance recipes live in psytrance-composition-guide.md.
This file preserves the toolkit overview + loader status verbatim.

## Generative composition, randomization & modulation

**Render output convention (standing):** all composition renders — final
track exports, verification windows, and the WAVs fed to `mix_report` /
`analyze_tuning` — go to the repo-root `compositions/` directory
(`D:\pdf\roo projects\hdaw3\compositions\`, gitignored via `/compositions/`).
Do not write render output into `tools/`, the home dir, or other scratch
locations; MRT2 one-shot *sound design* samples (the raw sound palette)
stay in `tools/mrt2/sounds/`, but anything rendered from a project goes to
`compositions/`.

HDAW is a *generative* DAW, not just a recorder. Assisted creation is a core
product pillar and should be reached for wherever it fits:

- **Generative composition** lives in `PhraseGenerator` (`src/engine/PhraseGenerator.h`):
  scale-aware phrase styles (Standard, Arpeggio, BassLine, ChordStab, Pad, Lead,
  RandomWalk, Buildup), single-chord and chord-progression generation, scale
  modes, chord types/voicings/inversions. Exposed over RPC as
  `composition.generatePhrase/generateChord/generateProgression` (and matching
  MCP tools), surfaced in the UI by the **Compose tab** (TransportBar 🎵 /
  Ctrl+Shift+G; a docked bottom-panel tab — the old `PhraseGeneratorDialog`
  modal was retired into it, no more auto-close-on-generate).
- **Rhythm / drum patterns** come from `RhythmPatternGenerator`
  (`src/engine/RhythmPatternGenerator.h`): two euclidean pulses
  (polyrhythm) plus a rhythm-DSL voice (`E(k,n[,rot])`, groups).
  Exposed over RPC as `composition.generateRhythmPattern` (and MCP
  `generate_rhythm_pattern`), surfaced in the UI by the "Rhythm" mode of the
  Compose tab. A **corpus-derived drum phrase bank**
  (`src/engine/RhythmPatternBank.h`, 62 multi-bar phrases across
  kick/snare/clap/hats/perc/ride) feeds `generatePhrase(id)` /
  `applyPhrase(...)` factories (RPC `phrase`/`phraseRole`/`phraseIndex`;
  MCP mirrors). Markov percussion (`PercussionEngine` hat/snare theme voices)
  can source from the bank via opt-in `percCorpusPhraseProb` on
  `generate_psytrance_markov` (default 0). The bank is grown by the reusable
  `tools/` corpus pipeline (`extract_phrase_bank.mjs` single-instrument,
  `extract_kit_phrases.mjs` role-from-pitch full-kit, `curate_bank.mjs` →
  C++ rows).
- **Randomization / humanization** — note timing, velocity, and pitch
  humanize in the piano roll (`NoteGrid`) and clip editor (`ClipEditor`).
- **Modulation** — a per-track LFO system (`ModulationManager` /
  `LFOModulationSource`, track `MODULATION_LIST` ValueTree, `rebuildModulation`)
  that modulates parameters in the audio engine. The sub_synth's internal LFO
  additionally ships six factory **mod presets** applied atomically
  (`apply_sub_synth_mod_preset` MCP / `project.applySubSynthModPreset` RPC /
  FX Chain Mod button).
- **Song plan + cells** (plan/cell workflow —
  `docs/plans/2026-09-11-song-composition-workflow.md`) — deterministic
  structure, seeded content: a root `SONG_PLAN` ValueTree node + section-typed
  arranger regions pin the skeleton (section kinds = `PsytranceSectionKind`,
  4/4); cell recipes (phrase/rhythm/break/pattern/harvest) fill per-section
  windows in ONE undo unit with clip **provenance** (`genTool/genSource/
  genSeed/genParams`). Surfaces: MCP `set_song_plan`/`apply_song_brief`/
  `set_cell`/`fill_cells`/`reroll`, matching `composition.*` RPC, and the
  Compose tab ▸ **Song Plan** panel; `mix_report` accepts `fromPlan: true`;
  section templates persist under `AppData/HDAW/section-templates`. Variation
  comes from re-seeded content, never from structure drift. **A brief section's
  kind is its explicit `kind` when given, else its `type`** — `apply_song_brief`
  reads `kind` first (`peak`→mainA, `outro`→finale, `drop`→mainB on the `type`
  fallback, anything else passed through to the kind table, and an unknown name
  rejected at the trust boundary), and a section whose `type` and `kind` resolve
  to DIFFERENT kinds is REFUSED rather than silently picking one; `set_song_plan`
  reads `kind` too, so both surfaces agree.

- **Hardware VA suite (gearmulator CLAPs)** — OsTIrus (Virus TI), Osirus
  (Virus A/B/C), Vavra (microQ), Xenia (Microwave), JE8086 (JP-8000),
  NodalRed2x (Nord Lead 2x), Dexed (DX7) run as isolated CLAPs with their real
  firmware (installed in `C:\Program Files\Common Files\CLAP\` with ROMs).
  Injection tools: `send_fx_midi` (PC/CC/note/sysEx), `load_virus_preset`
  (CC0 bank + PC). (The Dexed `.syx` route `load_dexed_cartridge` was removed
  2026-09-14 — use the internal `fm_synth` + `fm_synth_import_sysex`.) Per-plugin **matrix presets + morph chains** exist
  for all five devices (`timbre-lib/matrix_presets/`); apply them via the
  `list_matrix_presets` / `apply_matrix_preset` MCP tools (xenia/nord/je8086
  verified live; per-engine measured status in `docs/va-suite-status-log.md` — moved
  from `hardware-va-suite.md` §9, 2026-09-24).
  Audition workflow:
  inject → `save_project` → `export_audio` → measure — the preset lives in the
  live plugin state; the save persists it into the tree for offline renders.
  Constraint: the serializer's size-regression guard protects plugin states
  across load→save cycles (see docs/plans/2026-09-12-plugin-state-durability.md).
  **Patch pipelines:** every device with a bank library has a decoder writing
  searchable sidecars — `virus_patch.py` (`.virus.json`), `nl2x_patch.py`
  (`.nl2x.json`), `je8086_patch.py` (`.je8086.json` + an exploded per-patch tree),
  `microq_patch.py` (`.vavra.json`); FileLibraryManager ingests all four (register
  the folder as a *patch* library). **Loaders are evidence-gated:** JE8086 DT1 dumps
  **do** apply since 2026-09-20 (wrapper retargets UserPatch → temp performance;
  `load_je8086_preset`), as do its 461 parameters — confirm a dump via
  `get_fx_capture_status` + render, not the param list. **(SUPERSEDED 2026-09-20:** the
  earlier readings — `load_nord_bank` renders "near-identical across 14 real patches"
  (docs/handoffs/2026-09-18-gearmulator-custom-builds.md); `load_virus_preset` (CC0+PC)
  "does NOT change Osirus renders … finding F-A — under investigation"; Vavra "exposes
  no host parameters" and its SysEx "MEASURED NOT APPLYING"
  (docs/plans/2026-09-16-matrix-preset-engine-fixes.md) — are all corrected: NodalRed2x
  is VERIFIED AUDIBLE live, F-A is RESOLVED, and Vavra exposes 7557 host params
  (2026-09-19) with dumps that apply (a buffer-targeting bug, not an emulator limit).
  Per-engine detail: `docs/va-suite-status-log.md` §9.)**
  **Prefer the device over a plugin for movement:**
  own modulation matrix → onboard FX → HDAW automation/track LFO → HDAW internal FX →
  third-party plugin last (plugin FX add CPU, latency, isolation and state-round-trip
  risk). Device matrix, per-device FX recipes and caveats:
  `docs/hardware-va-suite.md`.

- **Device parameter maps (2026-09-30)** — `list_device_params` now serves ALL
  21 engines: the 5 VA CLAPs (corpus route) AND the 16 internal fxTypes
  (`eq, compressor, reverb, delay, chorus, flanger, phaser, filter, saturator,
  sampler, fm_synth, growl_bass, psyarp, psy_fm, sub_synth, drum_synth`), whose
  maps are generated straight from the in-source static C++ def tables (verbatim
  default/min/max + per-param `source` file:line citation + documented enums;
  `appliesVia: set_internal_fx_param`, `durability: valuetree`). Regenerate with
  `python timbre-lib/build_device_map.py` (`--check` pins determinism); never
  write blind `paramIndex` values — query the map first.

**Bus/send architecture — reachable since 2026-09-22** (this section previously
documented a capability gap; it is now closed). `add_bus {busType:"fx"|"group", name,
fxType, busTarget}` creates a bus and returns its `busID`; `add_send {trackId, busTarget,
level, isPreFader}` routes a track into it; `remove_bus` / `remove_send` tear down, and
`remove_bus` cascades (every send targeting it goes in the same undo unit — one `undo`
restores bus + sends). `fxType` must be one of `FxBusProcessor`'s five —
`reverb`, `delay`, `eq`, `compressor`, **`filter`**; anything else is rejected by name (an
unknown type would build a silent passthrough). `filter` is the state-variable filter a track's
internal filter slot runs (`src/engine/InternalFilter.h`), with `Cutoff` / `Mode` (0=LP, 1=HP,
2=BP) / `Resonance` — and it is what makes a return **high-passable**, which the peak-only `eq`
cannot express.

**High-passing a return = routing it through a filter bus.** Two ways: **create the filter bus
first** (`add_bus {fxType:"filter", busTarget:0}`) then the child with `busTarget = <filter bus>`,
**or re-parent an existing bus** with `set_bus_target {busID, busTarget}` — which works on the
default `Reverb` return too (self-targeting, the master, unknown ids and any target that would
close a cycle are refused). Measured 2026-09-23 (`aether_dub`, 16 s of drop1): chained delay
return → bass **18 627 → 15 505 (−17%)**, sub **5 645 → 4 445 (−21%)**; re-parenting the *reverb*
return behind a 250 Hz HPF → sub **4 604 → 3 698 (−20%)**. Note a bus created *after* the graph
was last prepared is never prepared itself; `FxBusProcessor::processBlock` now fails safe
(pass-through) rather than corrupting memory. The pre-existing `set_track_send_level` /
`_mode` / `_bypassed` / `get_track_sends` shape and read an existing send. **Corrected
2026-09-23:** those four legacy send routes now take `trackId` on BOTH surfaces — the RPC
half previously took `trackIndex` — and the retired key is rejected on both
(`BusSendRpcTest.LegacySendRoutesRejectTrackIndexOnBothSurfaces`). A send level is
automatable too: `add_automation_lane {paramID: 2000 + sendIndex}` rides it (a bus FX
param is `3000 + busID*8 + paramIndex`). RPC twins:
`project.addBus` / `removeBus` / `addSend` / `removeSend`. Full plan + gates:
`docs/plans/2026-09-22-bus-send-surface.md`.

Measured 2026-09-22 (aether_dub, 16 s of drop1, send 1.0 vs 0.0 into an `fxType:"delay"`
bus): **rms 0.0832 → 0.1156 (+39%, +2.85 dB), bass band 14 094 → 28 662 (+103%), body
3 905 → 8 715 (+123%)** — the return reaches the master, so the dub idiom (one shared
delay/reverb return, ridden per phrase) is now buildable. The bus read/param gap those
measurements walked into is closed: **`list_buses`** (RPC `read.listBuses`) lists every bus
with its `fxType`, and **`list_bus_fx_params` / `set_bus_fx_param`** (RPC
`read.listBusFxParams` / `project.setBusFxParam`) read and shape an fx bus's parameters
(`index` / `name` / `minValue` / `maxValue` / `defaultValue` / `value` vocabulary, shared with
`list_fx_params`) — plan: `docs/plans/2026-09-22-bus-fx-params.md`.

**The delay return is a real feedback delay** (slice C3 of that plan, 2026-09-22): the bus's
`delay` chain now uses the *same* DSP as a track's internal delay — the class `InternalDelay`
(`src/engine/InternalDelay.h`), extracted verbatim from `TrackFXSlot` and shared by both, so a
track delay renders exactly what it rendered before (asserted analytically: taps
1.0 / 0.5 / 0.25 / 0.125 at 1x/2x/3x/4x the delay time for feedback 0.5). Its five params are
real and settable: **Delay Time** (0.01-5 s), **Feedback** (<= 0.99 — the runaway clamp),
**Mix**, **SyncToTempo**, **Division** (0=1/8, 4=dotted-1/8, 6=1/4; derived as
`beats x 60/bpm`, the bus reading the project BPM from the playhead so it follows tempo live
*and* in export). Measured on aether_dub, 16 s of drop1, send at 1.0: Feedback **0.7 vs 0.0**
-> last-2s rms 0.1466 vs 0.1159 (**+26% tail energy**, peak 0.664 vs 0.530) = repeats instead
of one tap; Division **1/8 vs 1/4** -> last-2s rms 0.1373 vs 0.1088, so sync really moves the
taps.

**Set Mix = 1.0 on a send return.** The default 0.5 re-adds the bus input — a doubled dry
signal on top of the track's own dry. 1.0 makes the return pure echo (the classic send-return
wiring). The same applies to a reverb return.

The dropped-response caveat is **fixed (2026-09-23)**: `TransportHttp::start` raises
Qt's 15 s default keep-alive to **900 s** (`src/mcp/McpTransportHttp.cpp`) — the
buffered response of a long synchronous routing rebuild (`add_bus`, `add_send`) is no
longer discarded when the completion-to-flush heartbeat window elapses (regression
test `HttpTransport.AdvertisesKeepAliveTimeoutAtLeast900`). Keep the general hygiene
anyway: after any transport hiccup, **re-read state** (`list_buses` / `get_track_sends`)
rather than blind-retrying a mutation — a blind retry double-applies it. Two verified
caveats remain: **sends are positional** (index = position in the track's `SEND_LIST`,
so removing one shifts the rest — `remove_send` returns the `shifted` pairs and remaps
send-level lanes in the same undo unit); and `export_audio`'s `start`/`end` are
**seconds** while `verify_part` takes `startBeat`/`endBeat` — check the rendered
duration before trusting an A/B.

Still available (and often the cheaper choice): per-track FX plus **gestural lane
automation** — `add_automation_lane {trackId, laneName, paramID}` (paramID = `100 +
slot*100 + paramIndex`; built-in lanes are 1 Volume / 2 Pan / 3 Mute, one lane per
paramID) followed by `automation_preset`, whose presets are the gesture vocabulary:
`delayThrow` (the dub throw), `steppedGate` (dub gating), `openClose`, `phaseSweep`,
`macro`, `riser`, `pump`, `subtleLife`, `randomDrift`. `sections[]` entries each carry
their own `preset`, so one call can layer several gestures on one lane
(measured: 768 points from `openClose`+`phaseSweep`; 152 points of `delayThrow` per
track). Note an enabled Volume lane makes automation authoritative for that track —
it then appears in `audit_modulation_coverage`'s `faderOverriddenIds` and
`set_track` volume writes are overridden.

**Guideline: when adding a feature, ask whether the generative/random/modulation
toolkit applies.** New note or parameter editing should offer humanize/randomize;
new content types should consider a generative path; new modulatable parameters
should be wired as modulation targets. Prefer extending these shared utilities
over one-off randomness, so behavior (and its MCP/RPC surface) stays consistent.

## Agent mechanization — discovery, archaeology, batch edits, verification

Ten tools shipped 2026-09-28 (`docs/plans/2026-09-28-agent-mechanization.md`, close-out
`docs/handoffs/2026-09-28-agent-mechanization-shipped.md`; operational sections in
`docs/testing-mcp.md` — "Discovery", "Multi-edit atomicity", "Render → measure → compare",
"Time windows"). They compress the mechanical repetition of a composition loop. No engine
or DSP path was restructured: every one is a new `src/common/` shaper plus an MCP tool
and/or an RPC route calling it, and `mix_report` / `mix_verdict` / `mix_diff` bytes are
unchanged.

**Discovery — `tool_help {name}` / `whoami`.** `tool_help` returns that tool's exact
`tools/list` entry — `{name, description, category, inputSchema}` — in one call instead of
the `tools/list` → schema dump → description dump round trips (plus the one failed call
from a guessed argument name); both read the SAME stored `McpToolDef`, so the payload
cannot drift from `tools/list` (`ToolRegistry.ToolHelpReturnsTheExactToolsListEntry`). The
schema carries the `x-unit` annotation on every numeric field and the tool's one shape
example in the STANDARD `inputSchema.examples` array (a zero-arg tool carries `[{}]` —
there is no ad-hoc top-level `example` key for a strict MCP client to strip). An unknown
name is refused in-band with the shared text `unknown tool <name>`. `whoami` answers "what
is running?" in one call: the whole `engine_info` payload (running binary path/mtime/size,
version, exporting — plus the `buildBinaryPath` staleness check and the `expectedVersion`
cross-check when those optional args are given) PLUS `transport`
(`"stdio"` / `"http"` / `"unknown"`), the session project (`projectPath` — the file
loaded/saved THIS session, `""` when none — `projectName`, `trackCount`, `clipCount`) and
the edit-batch state (`batchOpen` / `batchDepth` / `batchName`). It shares ONE builder
(`mcp::buildEngineInfoPayload`) with `engine_info`, and a test requires every `engine_info`
key to appear in `whoami` with an equal value, so the two payloads cannot drift
(`EngineTools.WhoamiMatchesEngineInfoForSameArgs`). **Both are MCP-only by design** (no RPC
twin — the `engine_info` precedent): the RPC surface has no tool registry, and cannot
report its own transport.

**Archaeology — `query_notes` / `query_clips`.** "Which notes sound at beat 657.5, on
which track, with which IDs" used to take four regex passes over the `.hdaw` XML
(note-tag vocabulary miss, attribute-order miss, multi-line tags). `query_notes
{startBeat, endBeat, trackIndex|trackID}` and `query_clips {startBeat, endBeat}` return
structured rows in PROJECT (absolute) beats — notes carry `noteId, clipId, trackIndex,
trackID, clipName, absBeat, endBeat, localBeat, durationBeats, pitch, velocity,
occurrenceIndex, truncated`; clips carry `clipId, trackIndex, trackID, name, type,
startBeat, endBeat, durationBeats, muted, gain`. The window is an INTERVAL OVERLAP
(`absStart < endBeat && absEnd > startBeat`), so a note that starts before the window but
sustains into it is returned, its span clamped to its clip (`truncated` when the clip cut
the tail). The agent asks in beat-space and gets edit-ready IDs. ONE implementation
(`src/common/ProjectQuery.{h,cpp}`) sits behind both the MCP tools and their twins
`read.queryNotes` / `read.queryClips` (which share `HDAW::readBeatWindowArgs`), so the two
surfaces cannot diverge.

**Batch edits — `set_notes_gain` / `set_clips_edit`.** 52 single-item
`set_clip`/`set_note_gain` calls per verification run × 3 runs is the cost these remove
(the repo's own performance rule #1: batch RPCs, not N loops). `set_notes_gain
{noteIds:[…], gain}` sets per-note gain on every id; `set_clips_edit
{edits:[{clipId, start?, duration?, gain?, fadeIn?, fadeOut?, name?, looping?}…]}` is a
per-clip PARTIAL edit — an unset field leaves that property untouched (`start`/`duration`
beats, `fadeIn`/`fadeOut` seconds, `gain` a scalar). Both return `{ok, applied}` and run
through ONE `ProjectCommands` entry point (`setNotesGain` / `setClipsEdit`), so the MCP
tool and the RPC twin (`project.setNotesGain` / `project.setClipsEdit`) share one
implementation and one undo unit. The whole batch is **validate-then-apply**: an EMPTY
array (`noteIds must not be empty` / `edits must not be empty`) or ANY unknown id
(`unknown noteId N` / `unknown clipId N`) refuses the batch — nothing written, NO
transaction opened, so no undo unit — before any `beginTransaction`. An `edits` item
declares its properties AND `additionalProperties:false`, so a typo'd key is REJECTED
rather than silently dropped (lesson 34); the RPC route reproduces the validator's bytes
via the shared parser (`src/common/BatchEditJson.h`).

**Atomicity — `begin_batch {name}` / `end_batch {verify?}`.** A batch is ONE named undo
unit: while it is open, EVERY undo boundary a command draws — the command layer's own
`beginTransaction`/`endTransaction` pair and every internal transaction a command opens —
is suppressed by the ONE choke point `AudioEngineCommands::transactionBoundary`
(`src/engine/AudioEngineCommands_Undo.cpp`), so a single `undo` reverts the whole batch
(measured by `BatchEditRpcTest.BatchCollapsesInternallyTransactionalCommandsIntoOneUndo`).
That state is a FLAG (`batchActive_` / `batchName_`), **never a depth counter**, because
JUCE's boundaries are often deliberately UNPAIRED (`createSend` joins `createBus`'s unit)
and a counter would leak. It is engine-global and **one at a time**: a second `begin_batch`
while one is open is refused naming the open batch (`a batch is already open (name "…") -
call end_batch first`), and `end_batch` with none open answers `no open batch`. **Only the
stdio transport may open one** (refused over HTTP): the stdio process owns a dedicated
engine with no WebSocket frontend branch, so its one client is the only writer. Every write
from ANY surface while it is open joins the batch — keep batches short — and a command
FAILURE does not close it, so call `end_batch` on the failure path too. `end_batch
{verify:{targets?, outputPath?}}` **SEALS FIRST** and only then optionally renders the whole
project + composes the release verdict; a verification FAILURE never un-seals — the payload
stays `{ok:true, sealed:true}` and reports the failure in `verificationError` (success adds
`verification:{wavPath, verdict}`; with no `verify` the payload is exactly
`{ok:true, sealed:true}`, and argument errors are refused BEFORE the seal). The RPC twins
`project.beginBatch` / `project.endBatch` call the SAME entry points — exact ledger twins,
not aliases of the raw `beginTransaction`/`endTransaction` pair — with the same refusal
texts and ONE recorded asymmetry: the route is not transport-gated.
`project.beginTransaction` / `endTransaction` stay the RPC grouping path and are not
batch-gated, so a group opened while a batch is open joins it (engine-global state,
documented).

**Verification — `verify_window` / `render_and_verify`.** Five verification iterations of
export + measure + parse (with the decisive per-channel ceiling check run OUTSIDE the
engine) collapse into one call. `verify_window {startBeat, endBeat, targets?|expect?,
outputPath?, timeoutMs?}` (RPC `composition.verifyWindow`) renders the WHOLE project on a
tree copy through the shared launcher (`src/common/RenderLaunch.h` — the same tree-copy /
`trackIds` filter / `ExportManager::startExport` path `export_audio` and `export.audio`
use) and WAITS for it, then measures ONLY `[startBeat, endBeat)`. `buildWindowReportPayload`
promotes the WINDOW's stats (`duration/peak/rms/bands/kickProminence/ceilingHitPct/
ceilingHitFrames`) to the payload ROOT, so `targets` gates the window and not the whole
song; payload `{ok, wavPath, window:{startBeat,endBeat,startSec,endSec,durationSec},
report, targetChecks, targetsOk}`. Its expectation keys are **STRICT** — `targets`/`expect`
accept ONLY `rmsMin`, `masterRms`, `ceilingHitPctMax`, `kickProminenceMin`,
`targetDurationSeconds`; an unknown key is refused (`unknown expectation key <key> (valid: <the
accepted keys>)`,
−32602 on BOTH surfaces) BEFORE any render (`src/common/RenderToolArgs.h`). `rmsMin` is a
LINEAR RMS floor in the SAME units as the report's root `rms` and as `masterRms`;
`masterRms` is the linear mono-downmix RMS within 5%; `ceilingHitPctMax` a percent of
frames with any channel |sample| ≥ 0.999; `kickProminenceMin` 0..1 at-least;
`targetDurationSeconds` seconds within 2 s. `render_and_verify {outputPath, targets?,
timeoutMs?, fromPlan?, dropBuildRatio?=0.9, introSeconds?=2}` (RPC
`export.renderAndVerify`) is render + a verdict BYTE-IDENTICAL to `mix_verdict
{filePath:<produced>, …}` — both resolve their inputs through `src/common/MixVerdictInputs.h`
— with `fromPlan` defaulting FALSE like `mix_verdict`, so the no-args calls agree even on a
project WITH a song plan (pass `fromPlan:true` to BOTH to gate the plan; on a plan-less
project it falls back to the whole-file verdict rather than refusing, having already
rendered). **Cost + trap:** a `verify_window` costs about ONE full export (bounded by
`timeoutMs`, default 600000), and a WINDOWED render does NOT predict the full render —
plugin state re-bakes per window, and the v0.39.2 close-out measured **0 clamps** on a
windowed render of a file whose full render carried **32 exact-FS frames**. Windows
localise a problem; **the full render stays the release gate.** The rendered WAV is kept
for A/B and is the CALLER's to delete (an omitted `outputPath` lands in the OS temp dir; a
caller-supplied path is reused, not deleted) — deliverable renders still follow the
repo-root `compositions/` convention at the top of this file.

**Time windows in either unit.** Every window-taking tool/call accepts three spellings: the
musical one (`startBeat`/`endBeat`, `lengthBeat`, `durationBeat`, `timeBeat`,
`newStartBeat`, `positionBeat`, `loopStartBeat`/`loopEndBeat`, …), its wall-clock
`*Sec`/`*Seconds` twin, and — for the tools whose window key is bare — that bare key read
per an optional top-level `unit: "beats"|"seconds"` (default: the tool's documented unit).
Two present spellings of the SAME endpoint whose converted beats differ by more than 1e-9
are **REFUSED** (`conflicting window units: <keyA> and <keyB> disagree` — −32602 on the
route, the same string as the MCP tool error) rather than silently picked; agreeing
spellings are accepted, and any other `unit` value is refused (`invalid unit: <v> (expected
"beats" or "seconds")`). Seconds convert at the project BPM, EXCEPT `mix_report` /
`mix_verdict` / `mix_diff`, which describe a RENDERED FILE and therefore use their own
`bpm` argument when it is present and > 0. The response echoes the unit actually used — a
JSON payload gains `"unit"`, `export_audio`'s status line appends `… unit=beats`,
`add_arranger_region` answers `{"regionID":<id>,"unit":<u>}`, and
`set_arranger_region_bounds` / `transport` / `seek` answer `{"ok":true,"unit":<u>}`
(`transport` with no loop argument reports no window and stays the bare `{"ok":true}`). ONE
resolver, `src/common/WindowUnitArgs.h`, runs at BOTH dispatch choke points
(`McpServer::handleToolsCall` BEFORE schema validation, so an accepted alias also satisfies
a schema that still requires the canonical key, and `FrontendRouter::dispatch` before the
namespace routers) and **DECLARES** every accepted spelling into the stored schema — so the
validator and the resolver cannot drift. Pinned by `WindowUnitParityTest.*` /
`WindowUnitResolver.*`.

**Probing the surface: the PowerShell `ConvertFrom-Json` trap (measured 2026-09-28).** The
engine's `tools/list` body carries keys that differ only in CASE: **68 of the 317 tools
declare BOTH `trackID` and `trackId` in one `inputSchema.properties` object** (`set_track`,
`trigger_sampler_slice`, `get_fx_capture_status`, …), and Windows PowerShell's
`ConvertFrom-Json` fails on the WHOLE document:

```
Cannot convert the JSON string because it contains keys with different casing. Please use
the -AsHashTable switch instead. The key that was attempted to be added to the existing key
'trackID' was 'trackId'.
```

Parse with `-AsHashtable` (PowerShell 7; engine v0.39.2 answers 317 tools), or skip parsing
the raw body and drive the running engine through the helper:

```powershell
python scripts\hdaw_mcp_http.py tools                                  # count + every tool name
python scripts\hdaw_mcp_http.py schemas query_notes,set_clips_edit
python scripts\hdaw_mcp_http.py whoami
python scripts\hdaw_mcp_http.py call tool_help '{"name":"verify_window"}'
```

`hdaw_mcp_http.py` talks to an ALREADY-RUNNING engine (MCP over HTTP,
`http://127.0.0.1:18765/mcp`), so nothing is spawned or killed; the stdio twin
`scripts/mcp_call.py` starts a FRESH stateless engine per `call` — use `run <steps.json>`
for a multi-step proof in one engine lifetime.

