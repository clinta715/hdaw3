# Handoff: core-synth isolated children render SILENCE (2026-09-22) — re-voice pass BLOCKED

## Symptom

During the v5 re-voice pass (re-voicing the 5-minute remix onto the core synths),
both tested gearmulator engines render SILENCE through the live engine:

- **Vavra.clap** (track 1 bass): audition_plugin rms=0/peak=0 across 2 s, 3 s and
  12 s probe windows (not a warmup issue). SysEx device-dump route reports
  `queued` but `get_fx_capture_status` → `status=unchanged stateBytes=0
  hasPluginState=0` (the documented Vavra SysEx gap, still dead).
- **JE8086.clap** (track 1 bass after Vavra swap): same silence, 0 params.
- Both slots: `list_fx_params` → **0 published host params** on the live slot
  (documented: JE8086 461 / Vavra 7557 public params since 2026-09-19/20).
- `debug_audio` shows the slots instantiated, not bypassed, live meters flat.

## What checks out

- Isolated children spawn healthy: `spawnPluginHost: READY received`,
  `FXSlotCtor ... pluginInstance=ok`, `rebuildFXChain ... pluginMgr=ok`.
- Fresh engine + fresh children (twice: 09:20 and after the final
  `engine_restart`) — identical silence, so not a stale-child/zoo effect.
- Not blacklisted. Chain slots bypassed → still silent (chain is not the cause).
- `MatrixPresetsTest.*` 12/12 green (but fixture-based — no real audio).
- The internal-synth elements of the same project render non-silently
  (tone_verity PASS on 8 tracks), so the render path itself works.

## Prior evidence these engines worked

- `compositions/je8086-jpar/` (2026-09-18): JE8086 renders non-silent through the
  same MCP surface, 461 params live.
- `FxMidiInjection.*` real-plugin gates (2026-09-21): Osirus/Vavra etc. audible.
- The engines worked BEFORE today's binary (which contains: ParamVerity pipeline,
  phrase-cell tiling, clearNotes list-swap, movement lane reuse, mix-gate fix).

## Prime suspects

1. **A regression in today's engine build** — the only confirmed change set is
   commits 75f1a4b..8d5ab04 (verification pipeline + fill/clear changes). None
   touch the proxy/plugin code, but the silent-child symptom (params=0 +
   silence) suggests the child's plugin instance never fully initializes.
2. **Environment/asset issue in the %TEMP% execution context** (ROM/asset path
   resolution for the child, e.g. JE8086's JD990 ROM bin), possibly newly broken
   by a path/mtime change.
3. Stale `hdaw_plugin_host.exe` interference is RULED OUT for the final test
   (single fresh child, fresh engine).

## Repro recipe (next session)

1. Build current HEAD; `mcp-launch.bat` to start the engine.
2. `load_project compositions/mcp-dogfood-2026-09-22-5min-remix-restored.hdaw`
3. `audition_plugin {trackIndex:1, slotIndex:3, style:'BassLine'}` → rms 0.
4. Cross-check with the test harness: run `FxMidiInjection.*` real-plugin gates
   (`HDAW_REAL_PLUGIN_TESTS=1`) — if they fail too, bisect the engine build
   between 8284294 (Sep 21, known-good) and HEAD.
5. With `scripts/heap-diag.ps1 pageheap-on`, capture a dump on the child if it
   dies during PREPARE.

## Session state (saved)

- `compositions/mcp-dogfood-2026-09-22-5min-remix.hdaw`: 10 tracks / 41 clips,
  tiled + register-budgeted cells, per-role FX chains loaded, JE8086 slot
  present on track 1 (silent), Bass Glue chain un-bypassed.
- Libraries registered: `_break` (amen breaks), Avalon psytrance one-shots,
  JE8086/microQ/Xenia/NL2x patch libraries (ids in the session log).
- Pre-revoice fallback: `mcp-dogfood-2026-09-22-5min-remix-restored.hdaw`
  (auto-backup 07:59) and `compositions/auto-backups/...`.
- The v3/v4 renders remain the last known-good audio.


## UPDATE 2026-09-22 (later): the 'silent deaths' were exit-42 timeout restarts

Forensics via `scripts/crash-diag.ps1 report`: every engine death after 09:20 exited
with **code 0x0000002A = 42 = the intentional engine_restart exit code**. These were
NOT crashes: the MCP wrapper's 10-second call timeout fired on long operations
(batched fills, library scans, auditions), and the wrapper restarted the engine on
timeout — wiping unsaved state ("empty project" incidents).

- Only ONE genuine engine crash occurred today: 07:58, heap corruption C0000374
  (dumped + analyzed separately).
- Mitigations: keep mutating calls under 10 s (batch small, save immediately);
  fills/exports/scans return immediately when async; `scripts/crash-diag.ps1`
  + WER LocalDumps (full dumps, engine + plugin host) are armed so any REAL crash
  now lands a dump in `%TEMP%\hdaw_crash_captures\wer\` automatically.
- The bare-name resolution fix (commit 9f813c2) resolved the actual silent-children
  bug: track-1 Vavra 7557 params, audible audition, bass tone_verity PASS.
