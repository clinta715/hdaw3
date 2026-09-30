# Plan — 2026-09-30: fix the bugs found in the `ion_rift` / VA-verification session

**Source of the list:** [`docs/handoffs/2026-09-30-ion-rift-and-va-verification.md`](../handoffs/2026-09-30-ion-rift-and-va-verification.md) §2/§4 + the session's bug triage.
**Rule in force:** AGENTS.md — changes touching `processBlock` / DSP chains / render-export / plugin
isolation require **discussion with effort + risk notes FIRST**. B1 and B6 are in that class and are
therefore gated behind explicit sign-off (Phase 2). Everything in Phase 1 is additive diagnostics or
payload work with no DSP path.

## Success gates

**Phase 1 (safe, additive)** — ALL DONE 2026-09-30
- [x] P1-a: a live-slot call on a deviceless engine reports the DEVICE, not just the track — the
      error names `set_audio_output_device` and is unchanged (byte-for-byte) when a device is open.
      CORRECTION (as landed): the FIX is always named; the device NAME is included when a configured
      output exists (`get_audio_current_setup.output` non-empty). With no configured name there is
      nothing to name, so the text names the device STATE instead of inventing one.
- [x] P1-b: `list_tracks` reports the stable `trackID` alongside the positional `id`, sourced from
      the TRACK node (not recomputed), with the tool description updated.
- [x] P1-c: `trackID: 0` (and any unknown stable id) reports an error that names the KEY and the id,
      instead of the previously misleading `trackId required`.
- [x] P1-d: `apply_song_brief` no longer silently ignores `sections[].kind` — an explicit `kind`
      is honoured (the tool's documented promise), `type` remains the fallback, and a section with
      BOTH disagreeing is refused loudly.
- [x] P1-e: `dsh-build-fast.bat test` builds; the affected gtest suites pass; NEW tests cover each
      of P1-a..P1-d.
- [x] P1-f: no payload change reaches a route that has a parity twin without updating the twin.
      CORRECTION (measured 2026-09-30): `list_tracks` is NOT mcp-only — the ledger maps it to
      `read.snapshot` as a FAN-OUT note (`tests/unit/frontend/rpc_parity_map.inc`). There is no
      `read.listTracks` route, so no regeneration was needed and `node tools/rpc_parity_map.mjs`
      leaves the ledger byte-unchanged. The payload work that DID land on twins
      (`snapshot_project` rows, layer-handoff rows) is asserted on BOTH surfaces.

**Phase 2 (gated on sign-off — DSP / plugin isolation)**
- [x] P2-a: master gain ordering decided and implemented, with the loudness-compatibility
      consequence stated in the handoff (see risk notes below).
      CODE DONE 2026-09-30: option (b) as decided below — the enabled limiter's ceiling is re-applied
      AFTER the gain, so a gain above unity pushes into the ceiling instead of past it, and
      limiter-free projects stay bit-identical. `MasterBusFx.PostGain*` / `MasterGain.*` 24/24; the
      misleading comment and the meter claim are corrected; `set_master_gain`'s tool description now
      states the behaviour. **The consequence is now STATED** in
      [`docs/handoffs/2026-09-30-ion-rift-bug-closeout.md`](../handoffs/2026-09-30-ion-rift-bug-closeout.md)
      §3: the clamp binds only for `masterGain > 1` WITH a limiter enabled (loudness preserved,
      clipping removed), and the session's own project is provably unaffected — `ion_rift.hdaw` has
      gain 0.548 < 1, so 0.548 × the limiter's 0.97 ceiling = the 0.53 peak the composition session
      measured, i.e. the clamp cannot bind and its render is unchanged. No render A/B is owed.
- [x] P2-b: the hang-watchdog exemption hole investigated and either fixed or explicitly
      documented as "real hangs and warmup stalls are indistinguishable here".
      FIXED (two distinct defects, both measured — see the B6 block below):
      (1) the WARMUP-EPOCH CARRYOVER that wrote a spurious dump during a healthy warmup #2 — fixed
      with the per-warmup epoch counter the hypothesis specified, pinned by
      `PluginIsolation.BackToBackWarmupsWriteNoHangDump` (proven to fail without the fix);
      (2) the 1.5-2 GB DUMP SIZE — the watchdog now writes a stack-only `MiniDumpNormal` dump
      (measured 70,825 bytes for a real hang vs the pre-fix 330-670 MB class), pinned by
      `DumpPolicy.*` and documented in `docs/build-and-testing.md` +
      `docs/skills/psy-song-session/reference.md`. The render-time class is explicitly documented
      rather than widened (widening the 1 s threshold would hide real hangs).

**Phase 3 (investigation, no fix yet)**
- [x] P3-a: the clean engine exit (0x00000000) mid-render is explained or bounded.
      EXPLAINED: stdin EOF → `McpTransportStdio` queues `QCoreApplication::quit()` →
      `main_headless`'s `app.exec()` returns 0. The engine has NO other clean-exit path (2 = bad
      args / `--project` load failure, 42 = the deliberate `engine_restart`). It was already
      documented in `docs/mcp-server-ops.md`; the transport now logs the reason too
      (`McpStdio: stdin EOF - client closed the transport; quitting (engine exit 0 by design)`).
      The 15-minute `requestTimeout` override was verified present, so the old 10 s
      discard-and-relaunch hazard was not the cause.
- [x] P3-b: a booting plugin slot is distinguishable from a broken one (readiness signal or a
      bounded retry inside the loaders).
      DONE via the SECOND option: one shared bounded wait (`src/common/PluginBootGate.h`, 1.5 s for
      a read / 15 s budget for a deliberate operation) in BOTH param read paths
      (`list_fx_params` + `pluginParam.getParams`), so a warming-up child answers with its real
      parameters instead of `{}`; payload shapes unchanged. The readiness bit on the existing
      HEARTBEAT/READY reply (option 1) was NOT taken — it is a two-process protocol change (both
      binaries + the version guard) — and remains available if the ambiguity ever costs more than
      the wait.

**Verification (2026-09-30, end of session):** `dsh-build-fast.bat test` → rc 0 (plus
`test hdaw_plugin_host` for the child, which `test` mode does not compile); full sharded run
`powershell -File run-tests-sharded.ps1 -Shards 2` → **2130 passed, 0 failed, 2169 of 2169 intended
tests, 15.5 min, exit 0** (every shard `ran == intended`), followed by full-binary re-runs after the
last edits: frontend 281/281, mcp 370/370, platform 217/217 (includes the new epoch pin);
`node tools/rpc_parity_map.mjs` → `tools 317 rpc 419 {"mapped":303,"mcp-only":14}`, ledger `.inc`
byte-unchanged. The tree is UNCOMMITTED (HEAD `e342a3d`).

## Dependency map (graphify + grep)

- **B1 master gain** — `MasterBusProcessor` (community `MasterBusProcessor`) is depended on by
  `RoutingManager` and covered by `tests/unit/engine/master_bus_fx_test.cpp`; the gain write path is
  `AudioEngineCommands::setMasterGain` → `IDs::masterGain` listener (`AudioEngine.cpp:1078-1087`) →
  `MasterBusProcessor::setGain`. No test pins the gain-vs-FX order (grepped `setGain|getGain` in the
  test → 0 hits). God nodes: none. Seam crossed: engine ↔ master bus.
- **B3 track list** — `McpTools_Read.cpp:63` builds a BARE array inline (`{"id", i}` …); there is
  **no RPC twin** (`read.listTracks` does not exist) so it is an MCP_ONLY row; the shared shape
  precedent is `src/common/TrackJson.h` (`{trackId, trackID}`, "identical by construction").
  Consumers: agents only. The stable id lives on the TRACK node as `IDs::trackID`.
- **B4 brief kinds** — `AudioEngineCommands::applySongBrief` (`AudioEngineCommands_Song.cpp:396-445`);
  `briefTypeToKind` (:52-60) is the alias table; `knownSectionKind` (:45-50) is the trust boundary.
  `set_song_plan` already reads `kind`, so the two surfaces disagree today.
- **B2 deviceless diagnostic** — the error is emitted in `AudioEngineCommands_Fx.cpp:444-449` after
  `ensureLiveRouting`; `ensureLiveRouting` is `AudioEngine.cpp:1877-1909`. Other `track not found`
  live sites: `AudioEngineCommands_Automation.cpp`, `AudioEngineCommands_Fx.cpp:449/640`.
- **SPSC / ReadModel / routing:** B1 touches the master bus DSP (RT-safe, atomics only); B3 touches a
  read payload; B4 the plan parser; B2 error text only.

## Pitfall gates

| Gate | Applies | How it is addressed |
|---|---|---|
| 3 audio-thread safety | **B1** | Any change stays allocation/lock-free; the gain is already an atomic + SmoothedValue — do not introduce a lock or alloc. |
| 2 unimplemented path | B4 | Honour `kind` end-to-end and assert the observable plan, not the success payload. |
| 4 stale binaries | all | `dsh-build-fast.bat` then the affected gtest exe; never `build/Release`. |
| 10/1 restore path | B3 | `trackID` must be read from the node on every call (never cached/recomputed). |
| 16/14 plugin isolation | **B6** | Gated; any change to hang detection needs sign-off. |
| 9 id namespaces | B3 | Use the existing `IDs::trackID`; never re-mint. |
| 11/12/13 | no | No new entry point, no graph mutation, no DSP-state write. |

## Anti-patterns to avoid

- Two hand-written serializers for the same payload (use a shared shaper in `src/common/`, as
  `TrackJson.h`/`BusInfo.h`/`SendJson.h` do).
- A new `.cpp` not registered in `CMakeLists.txt` (prefer header-only inline like `TrackJson.h`).
- `setProperty` on an unchanged value expecting a listener side-effect (lesson 2).
- Moving the master gain without stating the loudness consequence.

## Risk notes (the AGENTS.md "discuss first" pair)

**B1 — master gain is applied after the master FX chain (`MasterBusProcessor::processBlock`: chain
147-216, gain 218-227).**
- *Effort:* small (move one block, or add a post-gain clamp), plus tests.
- *Risk:* **behavioural, project-wide.** Every project with `masterGain ≠ 1` changes loudness and
  every project whose limiter is engaged changes character; the master meter's meaning changes; the
  limiter starts actually protecting the output (which is the point). Existing saved projects keep
  their `masterGain` value, so their renders change. Options:
  (a) move the gain **before** the chain — correct signal flow, changes existing renders;
  (b) keep the order and **clamp after the gain** — preserves loudness, keeps the limiter honest;
  (c) keep the order, document it, and fix only the misleading comment + the meter claim.
- *Recommendation:* (b) if compatibility matters, (a) if correctness does — needs the user's call.
- **DECIDED 2026-09-30 (user): option (b) — clamp after the gain.** Keep every existing project's
  loudness identical and stop the gain from clipping: after the gain loop, clamp the output to the
  **enabled limiter slot's effective ceiling** (no limiter enabled → no clamp, current behaviour
  preserved). Also correct the misleading comment at `MasterBusProcessor.h:229-230` ("the meter
  reflects the true output level the limiter is holding at the ceiling" — it does not, once gain > 1).
  New test: gain > 1 with the limiter enabled must not exceed the ceiling; without a limiter the
  output is unchanged.

**B6 — the hang watchdog fires during Virus warmup/render load (1.5–2.0 GB dumps, repeatedly).**
- *Effort:* medium (reproduce, then inspect the exemption window).
- *Risk:* touching hang detection can **hide real hangs**; the exemption already exists because the
  Virus warmup legitimately blocks for ~12 s. Needs a discriminating signal (e.g. "warmup in
  progress" flag) rather than a blanket widening.
- **DECIDED 2026-09-30 (user): investigate first, fix only with evidence.**
- **VERIFIED AND FIXED 2026-09-30.** The hypothesis was right. The watchdog resets its hang counter
  on a `justFinishedWarmup` edge, which requires *sampling* `warmupActive` false between two warmups —
  but it ticks every 250 ms, and two warmups on ONE child can be ~1 ms apart (warmup #1 clears
  `processBlockActive` and the control loop starts #2 immediately). The edge is therefore never
  observed, `hangMs` carries warmup #1's elapsed time into #2, and #2 trips its own
  `warmupExpectedMs + 1000` threshold after only ~1 s — a SPURIOUS dump during a healthy warmup
  (live evidence: warmups 26 ms apart, dump 1.87 s into #2).
  **Fix (exactly as specified): a per-warmup EPOCH counter.** `warmupEpoch` (`PluginHost.h`) is
  bumped before `warmupActive` is raised, and the watchdog resets `hangMs` when the epoch changes, so
  each warmup owns its own budget. It does NOT hide a real hang: a genuinely stuck `processBlock`
  still reaches the threshold inside its own warmup's window (`RealHangWritesHangDump` green).
  **Regression pin:** `PluginIsolation.BackToBackWarmupsWriteNoHangDump` (two back-to-back PREPAREs on
  one Virus-named child) — proven to FAIL without the epoch reset (measured: a 71,065-byte dump during
  warmup #2) and to PASS with it (`dumps=(none)`).
  **Second cause (render-time dumps 08:19/08:32) — DECIDED, documented, deliberately NOT widened:**
  the 1 s non-warmup threshold still fires when an offline-render `processBlock` legitimately blocks
  that long, and widening it would hide real hangs. The cost is now bounded instead: the dump is
  STACK-ONLY (`src/proxy/DumpPolicy.h`) — measured **70,825 bytes** for a real hang, against the
  330-670 MB / 1.5-2 GB pre-fix class.

## Steps

1. Phase 1: B2 + B3 + B4 implemented in ONE subagent (single build — concurrent builds are unsafe),
   with new tests, then verified by me against the gates.
2. Phase 2: on sign-off, B1 and/or B6 in their own subagent + tests.
3. Phase 3: B7/B5 investigation notes.
4. Handoff row + docs update; parity ledger check if any route moved.
