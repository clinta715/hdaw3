# Handoff — Instrument-part composer shipped; remaining composer work (2026-08-19)

## Purpose

The previous session shipped the headline item from the track-tooling agenda:
`composition.addInstrumentPart` + `composition.autoGainToTarget` (RPC + MCP), with
the whole plan at `docs/plans/2026-08-18-instrument-part-composer.md`. This file
is the briefing for a **fresh context** that continues the remaining items IN
PRIORITY ORDER. Each item below is actionable on its own; pick up #1 first.

## Baseline: committed and verified (all on `main`)

- **Commit `be80b2f`** — `feat(engine): composition.addInstrumentPart + autoGainToTarget — one-shot instrument part composer with auto gain staging`. 13 files, +1281/−28.
- Full engine suite **917/917 green** (170 suites, exit 0) — was 908 before.
- **G6 runtime verified live** on a fresh `build/Debug/HDAW.exe`: default project →
  `composition.addInstrumentPart {style:Lead, region×2, targetRms:0.05, verify:true}`
  → track "Runtime Lead" with `fm_synth` slot, 2 clips, 8 notes, fader 0.1111,
  verified `measuredRms` = 0.05 exactly; undo #1 removes the fader, undo #2 removes
  the whole part (composite = two undo units by design).
- Knowledge graph refreshed (`codebase-memory` index, 9711 nodes).
- Working tree clean except untracked `projects/` (kept out of commits).

### What the composer does today

```
composition.addInstrumentPart {
  trackName, style (Standard/Arpeggio/BassLine/ChordStab/Pad/Lead/RandomWalk/Buildup/Euclidean),
  pluginId? (empty = internal fm_synth), lengthBeats, placement ("wholeSong"|"region"),
  startBeat?, count?, scaleRoot?/scaleMode? (-1 = project), density?, noteDuration?,
  lowNote?, highNote?, minVelocity?, maxVelocity?, seed?, targetRms? (>0 = gain stage),
  windowSeconds? (default 4.0), verify? (re-render after fader set)
}
→ { trackIndex, clipIds[], noteCount, gain?{ok, fader, measuredRms, peak, clamped, error?}, error? }

composition.autoGainToTarget { trackIndex, targetRms, windowSeconds?, verify? }
→ { ok, fader, measuredRms, peak, clamped, error? }
```

- **Engine commands:** `ProjectCommands::addInstrumentPart` /
  `autoGainToTarget` (`src/engine/AudioEngineCommands_Composition.cpp`, declared in
  `ProjectCommands.h` + `AudioEngineCommands.h`). One undo unit per command
  ("Add instrument part" / "Auto gain stage"). One `rebuildRoutingGraph()` per
  composite. `addFxSlotInternal` (extracted in `AudioEngineCommands_Fx.cpp`) adds an
  FX slot without a per-op rebuild.
- **RPC:** `Router_Composition.cpp` `dispatchComposition` (cases at lines ~267/333).
- **MCP:** `McpTools_Project.cpp` `registerCompositionTools` — `add_instrument_part`
  (~line 1037), `auto_gain_to_target` (~line 1102). Both call the SAME engine
  commands (parity rule).
- **Tests:** `tests/unit/engine/instrument_part_test.cpp` (`InstrumentPart.*` ×3,
  `AutoGain.*` ×3); MCP tool tests in `tests/integration/mcp/mcp_server_test.cpp`;
  RPC round-trip in `tests/unit/frontend/frontend_server_test.cpp`.

## The remaining agenda (priority order)

### 1. Plugin preset selection / audition — unblock the plugin path (design Q2 / handoff bug #4)

The composer defaults to a plugin's first program, which is hit-or-miss
(Identity/sampler are near-silent; TyrellN6 was found by probing candidates with
8s solo renders + peak check). The program APIs already exist end-to-end; they're
just not wired into any compose flow:

- **RPC/engine:** `plugin.getProgramCount` / `getProgramName` / `setCurrentProgram`
  in `Router_Plugin.cpp:123-135` → `PluginParamService`
  (`PluginParamServiceImpl.cpp:109-131`). MCP twins `list_plugin_presets` /
  `load_plugin_preset` in `McpTools_Audio.cpp`.
- **Gap:** no fast **audition** tool (render 2–3s solo, report peak/character) so
  silent-at-default plugins aren't a blocker, and `addInstrumentPart` accepts no
  `presetName`/`programIndex`.
- **Proposed shape:**
  - Extend `addInstrumentPart` params with `programIndex?` (or `presetName?`) —
    apply via `setCurrentProgram` on the new track's slot BEFORE the gain stage.
  - A `composition.auditInstrumentPart` / `plugin.audition {pluginId, programIndex?}`
    tool: add track + plugin slot (or reuse an existing one) → solo-render 2–3s →
    report `{peak, rms, duration, audible}`. Reuse the exact solo-render+measure
    pattern from `autoGainToTarget` (extract the shared helper first — see #5).
- **Success gates:** gtest asserts program N renders audible while program 0 is
  silent (needs a real scanned plugin — use an env-guarded test like the existing
  polywave-DISABLED pattern), MCP + RPC parity tests, runtime probe on a real
  plugin.

### 2. Global-scale + post-scale gain staging (FM clipping at fader 1.0)

FM instruments clip at fader 1.0 (preL up to 8). A solo-render measures the
track ALONE — it cannot see full-mix master-sum clipping at a dense section. The
handoff's manual workaround: re-render at ×0.85 then post-scale.

- **Gap:** `autoGainToTarget` clamps at fader 1.0 and reports `clamped=true`, but
  has no master-gain fallback.
- **Proposed shape:** when `clamped` AND a full-mix render shows the master
  clipping, add an optional `globalScale` step — lower the master bus gain, raise
  the target fader proportionally, re-render to verify. Wire a
  `setMasterGain` command if none exists (check `MasterBusProcessor` /
  `AudioEngineCommands` first — verify before assuming).
- **Calibration lesson (Q7, already documented):** measure RMS at a
  moderately-dense section, NOT the densest — RMS adds in quadrature, a
  gain-staged plugin moves full-mix RMS only ~1–3% at the climax. Use
  plugin-solo-render evidence for audibility.

### 3. `compose.verify` companion (design Q7)

A composer tool should self-verify: solo peak > threshold, full-mix non-clipping,
band-presence, plugin on/off RMS delta. `verify` on the current command only
re-renders the solo window; it does not verify the part in the mix.

- **Proposed shape:** `composition.verifyPart {trackIndex, windowSeconds?}` →
  renders the solo window AND the full mix at the same window, returns
  `{soloPeak, soloRms, mixPeak, mixRms, nonClipping, audible, bandsPresent}`.
  Reuses the render+measure helper from #5. Success gate: a part composed by
  `addInstrumentPart` with gain staging passes `verifyPart` (nonClipping, audible).

### 4. Part templates + typed track presets (design Q6 + Q8)

`addTrack` creates a generic track. Typed presets (instrument track → default MIDI
channel/note-range/velocity) and saving a composed part as a reusable recipe
(plugin, style, placement, targetRms, fader) would let composition become
"assemble building blocks". Interaction with undo units and the
delta-vs-fullSync split: track add = fullSync, clip add = delta — a template
instantiation is a fullSync like any track add.

- **Proposed shape:** a `TRACK_TEMPLATE` store (project- or app-level), instantiated
  by the same composite. Defer the store; the cheapest first step is a
  `role → defaults` map (Bass/Lead/Chords → fm_synth + default ranges, Drums →
  percussive patch) inside `addInstrumentPart`, exposed as a `role` param.
  Success gate: `addInstrumentPart {role:"Bass"}` produces the same slot/note-range
  defaults as a hand-configured Bass part.

### 5. Extract the shared solo-render+measure helper (prerequisite for 1–3)

`autoGainToTarget` inlines the solo-tree-copy + `startExport` + block-wait +
`measureWav` loop. Items 1–3 all need the same loop. Extract a small helper
(e.g. `AudioEngineCommands::renderTrackWindow(trackIndex, windowSeconds,
applyFader) → {wavPath, measuredRms, peak}`) BEFORE building audition/verify so
there is exactly ONE render-loop implementation. Success gate: `autoGainToTarget`
still passes unchanged after the extraction (refactor-only commit).

### 6. Minor API gap — `pluginFormat` in the RPC snapshot (handoff bug #3)

`read.snapshot` FX-slot JSON has `pluginId` + `name` but not `pluginFormat` (the XML
persists it). Tooling must read the XML to assert VST3-vs-CLAP. Expose it as a
snapshot field (`ReadModelImpl.cpp:481` area, `FxSlotSnapshot` struct in
`ReadModel.h`). One-line change + one gtest. Cheap, do it before any audit tooling.

### 7. Beats-vs-seconds ergonomics (design Q9)

Mostly solved by `wholeSong` placement. Remaining: a `paintToProjectEnd` helper /
uniform bars-beats acceptance so callers don't hand-convert. Low priority — the
composer already accepts beats at the RPC boundary and converts internally.

## Standing technical debt (from AGENTS.md lessons)

- **Lesson 20 guard (permanent fix):** per-run pipe/shm namespace (or a
  held-name skip) so a stale engine's orphaned `hdaw_plugin_host.exe` children
  can't collide with proxy-slot tests (`\\.\pipe\hdaw_plugin_<n>` /
  `hdaw_plugin_shm_<n>`, slot counter restarts at 1 per instance). Cheapest,
  highest test-suite-value item — the "known to fail five" CrashRecovery tests
  fail on slot collision when a stale engine is alive. Also note `mcp-launch.bat`
  kills stale engines/plugin-hosts before copying the binary.
- **Qt 6.11.1:** the disconnect-during-export fix (`2e37eb6`) is a workaround for
  a genuine Qt bug (`QWebSocketPrivate::processData` NULL-deref on re-entrant
  free); re-verify if the engine ever moves off Qt 6.11.1.

## Operational context a fresh session MUST know

- **Audio-device environment:** a machine-wide audio failure appears
  intermittently (empty WASAPI scan → `routingManager` null → ~29 device-dependent
  tests fail with `getTrack(0)==nullptr` + FrontendServer bind failures). It is
  ENVIRONMENTAL, not code. Recovery: elevated `Restart-Service Audiosrv,
  AudioEndpointBuilder` (or reboot). Diagnosis rule: a cluster of fast
  `getTrack(0)==nullptr` failures = audio stack down, not a regression.
- **Message pump:** every non-GUI process starts `MessagePumpThread` before any
  JUCE construction. The block-wait-on-`isExporting()` pattern in the composer
  works BECAUSE the pump is a separate thread delivering the render-sequence bake
  — never "help" it by pumping on the calling thread.
- **Probe hygiene:** never leave HDAW/plugin-host processes running (kills don't
  propagate to children on Windows; a stale engine breaks proxy tests). Probe
  scripts must restart the engine and resume on failure. Always attribute a log
  line to a pid (`date`+`pid` fields) before concluding a fix failed.
- **Beats vs seconds:** RPC/notes/clips are beats, processors/export are seconds.
  The composer converts at the boundary; new code must do the same.
- **The default project ships 3 EMPTY tracks** ("Track 1", "Synth", "Vocals") —
  never hard-code absolute clip counts; scope count assertions to the track you
  control.
- **Stale binary trap:** `build/Release/HDAW.exe` is stale — never test against
  it. After any C++ change verify the Debug binary's timestamp, and remember the
  packaged Electron app ships its engine from `build/RelWithDebInfo/` — repackage
  via `frontend\build.bat` if the packaged app is the target.
- **Plugin isolation default ON** spawns child processes per plugin FX slot. New
  tests that touch real plugins must account for spawn cost and the crash-recovery
  slot-namespace collision (lesson 20).

## Where to look

- Plan for what shipped: `docs/plans/2026-08-18-instrument-part-composer.md`.
- Composer implementation: `src/engine/AudioEngineCommands_Composition.cpp`.
- Composition RPC: `src/frontend/router/Router_Composition.cpp`.
- MCP composition tools: `src/mcp/McpTools_Project.cpp` (`registerCompositionTools`).
- Plugin program APIs: `src/engine/PluginParamServiceImpl.cpp`,
  `src/frontend/router/Router_Plugin.cpp:123-135`, `src/mcp/McpTools_Audio.cpp`
  (`list_plugin_presets` / `load_plugin_preset`).
- Gain-staging render loop to extract: `autoGainToTarget` in
  `AudioEngineCommands_Composition.cpp:263-438`; the measure helper `measureWav`
  at file top.
- Fader restore on rebuild: `Track::restoreMixerState` + `track_mixer_state_test.cpp`.
- Export render/bake: `src/engine/ExportManager.cpp` (`startExport`,
  `computeBakeWaitMs`), `tests/unit/engine/export_bake_timeout_test.cpp`.
- Prior context: `docs/handoffs/2026-08-18-track-tooling-handoff.md` (the 9 design
  questions), `docs/handoffs/2026-08-18-track-tooling-next-steps.md`,
  `docs/handoffs/2026-08-18-track-building-automation.md`.
- Learnings: `docs/realtime-safety.md`, `docs/pitfalls-juce.md`,
  `docs/pitfalls-frontend.md`, `AGENTS.md` lessons 1–22.

## Suggested execution order for the fresh session

1. Commit a refactor-only extraction of the shared solo-render+measure helper (#5) — pure win, unblocks everything.
2. `pluginFormat` in the snapshot (#6) — cheap, one-line + test.
3. Preset audition + `programIndex` in `addInstrumentPart` (#1) — the biggest composer unblock.
4. `compose.verifyPart` (#3) — makes "add a part" trustworthy as a testing tool.
5. Master-gain global-scale/post-scale (#2) — the FM-clipping edge.
6. Role→defaults map / templates (#4) — extensibility.
7. Lesson-20 namespace guard — standing debt, high test-suite value.

Per hdaw-guard: every code change gets a plan with success gates first, dependency
analysis via the knowledge graph (`codebase-memory` project
`D-pdf-roo-projects-hdaw3`), pitfall-gate scan, and subagent dispatch with
verification. MCP parity for every new user-facing capability. Tests for every
change (engine → gtest, UI → Vitest/Playwright).