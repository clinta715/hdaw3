# Handoff: what remains after the mixer-return / automation / key work (2026-09-23)

Context for the next session. Everything listed here was verified against the tree today;
"documented" means a repo file already records it, "reported only" means it exists only in
conversation and still needs writing down.

## What landed (so it is NOT open)

Seven commits, tree clean at handoff:

| Commit | Delivers |
| --- | --- |
| `70ab519` | bus/send **creation** (`createBus`/`createSend`, MCP + RPC twins) |
| `45de9cf` | session findings: the graphify refresh gotcha, test-infra traps |
| `4c51ec9` | the corrected return-ride recipe (send levels are not automatable) |
| `3eed79e` | bus FX params + `list_buses` + **the shared delay DSP** (real Feedback/Mix/SyncToTempo) |
| `ab78da0` | **`set_bus_target`** (re-parent) + the two-phase bus rebuild it exposed |
| `584983a` | the stale test baseline + the `aether_dub_returns` ledger entry |
| `382f4eb` | **`export_audio` can no longer silently succeed** + **`key_check`** |

Also fixed in passing: two *pre-existing* engine defects (an unprepared
`FxBusProcessor::processBlock` corrupting memory; `getSampleRate()` being 0 so rebuilds never
re-prepared freshly added nodes), a latent routing mis-wire (a bus whose parent sat later in
`BUS_LIST` silently fell back to master), and dead `RoutingManager::addBus` removal.

## Open — needs your approval first (stability rule: FX/plugin/DSP contract)

1. **`*FX` CLAP editions silently silence a track.** A qualified id (`CLAP-VavraFX-…`) loads,
   reports `pluginFormat` and params, and kills the audio (proven by bypass A/B: `0.0595 RMS
   audible=1` → `0/0`). A *bare* id (`"VavraFX"`) leaves an inert slot with `pluginFormat: ""`
   and **no error**. Fix is either making them process audio or dropping them from `kind:effect`
   so no agent can pick one. Reported + partly written up; **not** in the path trees.
2. **The compressor default is destructive**: `Threshold -20 dB / Ratio 4.0` on a hot synth
   clamped a bassline to 1/6 of its level (the pad with no compressor measured 3.6× louder at the
   same pitches). Mentioned in `docs/paths/dub_electro.json` and the ledger; no fix proposed.
3. **No filter *inside* the delay's feedback loop.** The new `filter` bus darkens the whole
   return statically; the classic dub move (repeats getting progressively darker) still needs a
   filter in the loop. This is the one musical gap left from the returns work.

## Open — contract/surface gaps (documented, unfixed)

4. **Send levels are not automatable.** `setSendLevel` is reachable only from the command and the
   graph build; the automatable paramID space is `1/2/3 / ≥100 track FX / ≥1000 MIDI FX`. A
   per-phrase throw must still come from a per-track delay slot. Trap:
   `send-levels-not-automatable`. Likely fix: a send paramID range (e.g. `2000 + sendIndex`,
   mirroring the `≥1000` convention).
5. **Bus FX params are not automatable either** — bus `param_N` has no paramID, so no lane can
   ride a bus's delay time or reverb size. Explicit non-goal of slice C; the param-index space was
   deliberately left open for it.
6. **Legacy send routes disagree across surfaces**: `setTrackSendLevel/_Mode/_Bypassed` take
   `trackIndex` on RPC but `trackId` on MCP (a real argument-name contract break; the new routes
   use `trackId` on both). Trap: `legacy-send-arg-mismatch`.
7. **Positional track and send ids.** `remove_track` shifts ids above it, so held references go
   stale; `removeSend` shifts sends. Documented (trap `one-lane-per-param`'s neighbourhood + the
   returned id-shift note); stable ids remain unfixed.

## Open — latent engine / tool issues

8. **The dropped HTTP response on rebuild-triggering commands.** `add_bus`, `add_send` and
   `set_bus_target` intermittently lose their response while **completing the work** (observed
   across three commands, including once *not* reproducing — so it is timing-dependent, not
   deterministic). Traps: `rebuild-commands-drop-response`, `bus-response-dropped`. Root cause
   (rebuild running on the HTTP handler thread) **not investigated**.
9. **`list_fx` reports `paramCount: 0` for a plugin that exposes 6939 params.** Reported only —
   **not written into any repo doc**; needs an entry (it makes `paramCount` useless as a "did the
   slot load" signal).
10. **`verify_part` cost scales with plugin instances, not window length** (two attempts exceeded
    120 s on a one-CLAP project, killing the cheap window-probe recipe). Trap:
    `probe-cost-scales-with-plugins`. The fallback (render once at a guessed-safe master and
    rescale) works but wastes a render; a solo-window-only mode is unbuilt.
11. **`audit_song_structure`'s `allDropsHaveBackbeat` is psytrance-shaped** — a faithful dub
    one-drop fails it by design. Documented in both path trees + `2026-09-21-mcp-dogfood-composition.md`.
12. **Engine-launch friction (process hazard).** A manual copy of the engine to `%TEMP%` died with
    exit `0x7FFFFFFF` even with `PATH` set, so live tests launched from `build/` — which locks the
    exes and causes `LNK1104` on the next build (hit twice today). The launcher's own
    `mcp-launch.bat` copy-and-verify path is the correct route and should be used instead of
    re-deriving it.

## Open — test infrastructure (documented, unfixed)

13. **`McpServer.HttpRoundTrip` binds a fixed port 18765** (`mcp_server_test.cpp:115,122`), so it
    fails whenever a live engine holds it — measured: 4 failures with an engine on the port, 2
    without. Should use an ephemeral port.
14. **`RespawnPath.RealPathPassesThrough` is deterministically red on Windows** — it asserts
    `resolveRespawnPath("/usr/lib/MyPlugin.clap")` round-trips unchanged, in
    `tests/unit/proxy/crash_recovery_test.cpp:655` (untouched since 2026-08-20). Independent of
    any change; either fix the expectation for Windows or gate it by platform.

## Open — stale doc to fix (one line)

15. **`no-bus-read-tool` is now FALSE.** The psydub trap still says no tool lists buses, but
    `list_buses` shipped in `3eed79e`. It is actively misleading (it tells the next agent the tool
    does not exist). Retire/rewrite it the way `export-dir-must-exist` was retired.

## Open — creative / path work

16. **`movement-plan` is the only never-walked branch** in the ledger, and the new **`rides`**
    node's three options (`arc-pass`, `return-ride`, `staging-pass`) have never been walked *as a
    node* — only `arc-pass`'s mechanism was proven, and only `shared-return` is in
    `branch_stats.measured_ok`.
17. `docs/paths/dub_electro.json` has a trap named `no-sends` that predates this work and may
    need re-examining now that sends exist (unverified today).

## In flight at handoff

- **Full confirmation suite** → `build/full_suite3.log` (244 run / 0 failed when this was
  written; expected to end like the 09-23 baseline: 1 pre-existing `RespawnPath` failure).
- **Knowledge-graph refresh** — `KeyConflict` is absent from `graphify-out/graph.json` because the
  post-commit hook's rebuild is cache-driven (documented trap in `AGENTS.md`). Run
  `python -m graphify update . --force`, then `graphify explain KeyConflict` to confirm.

## How to re-verify this work

```
build/hdaw_tests.exe --gtest_filter=BusSetTarget.*:KeyConflict*:KeyCheck*:McpCoverageTest.ExportAudio*:BusFxParam.*:BusSendCreate.*:BusSendRpcTest.*:TrackFxDelay.*
```
(13 + the bus/filter/delay suites; last run 0 failures.) Parity ledger after any new tool:
`node tools/rpc_parity_map.mjs` — currently 305 tools / 393 methods / mapped 197.
