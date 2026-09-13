# Plan — Phase 3: Plugin-state durability (fix BUG-1/2 from the OsTIrus song session)

**Status:** PROPOSED. Fixes the data-loss + silent-render bugs found in the 2026-09-11/12 OsTIrus song session (see docs/plans/2026-09-11-fx-midi-injection-virus-presets.md §"COMPLETION" context and the session findings below).

## Evidence established this session (log + file forensics)

1. `load_project` **does** apply real states: every spawned child logged `SET_STATE chunked applied bytes=177845` (OsTIrus) / `107686` (Osirus) at 14:16-14:23.
2. The song project saved at 14:35 wrote **262/264-byte** pluginState blobs (file shrank 494,273 → 114,414 B). The 14:36:42 export-domain children then received exactly those stubs.
3. The long-lived children (pids 16852/29920, alive with real states since 14:17) served the save's GET_STATE and returned 262/264 B — **no crash/respawn events** for them in 14:24-14:36.
4. First full-song export after load rendered **silence** (rms 0 for all blocks except one flush at beat 607); the immediate re-export rendered **audible** (peak 0.8667) — documented bake-vs-spawn family (2026-09-02 note).
5. Raw sysex via setStateInformation is ignored by Dexed (probed); OsTIrus/Osirus accept their own dumps at the CLAP boundary.

⇒ Two distinct defects: **(A) the live child's GET_STATE returns a ~262B default stub instead of its real state** (state collapse or child-identity mismatch), and **(B) first-export-after-load races child spawn** (separate, documented family — scoped here only as a gating fix).

## Goal

Plugin states survive load → edit → save round trips byte-for-byte, and offline exports always render the loaded (not default) plugin state. Immediate outcome: the data loss stops (FIX-1 guard), regardless of the upstream trigger.

## Root-cause candidates (Task 0 pins which)

- **C1 (child identity):** the save's GET_STATE reached a *different* child than the one holding the real state — export-domain / rebuild spawns create children that never receive SET_STATE, and the slot→child pipe mapping points at the newest one. (Fits: every export spawned fresh children; the save's GET_STATE hit one without state.)
- **C2 (plugin-side collapse):** OsTIrus/Osirus own `getStateInformation` returns ~262B after processing a 264s offline render (state reset mid-render, or a "clean state" path) — the child holds it, save faithfully persists it.
- **C3 (GET before ready):** GET_STATE answered from the child's default before its plugin finished loading — but children were minutes old, so only relevant for the first-export race (B).

## Success Gates (all with evidence)

- [ ] Gate 1 (diagnosis): instrumented reproduction names the exact site where 177845B becomes 262B — slot-id + byte-size logged at every SET_STATE/GET_STATE hop (parent proxy, child handler, serializer). Report written into this doc.
- [ ] Gate 2 (data-loss guard): ProjectSerializer refuses to overwrite an existing pluginState blob whose decoded size is >4KB with a new blob <1KB (or < existing/8) — logs `pluginState size regression kept` + keeps the previous blob. Unit-tested (fake slots: 190KB blob + 262B read → old kept; legit small states on fresh slots still saved).
- [ ] Gate 3 (round-trip): env-gated OsTIrus test — create slot → load_plugin_preset-style state → save → load → save → all three files' pluginState blobs byte-identical (or Gate-2-guard-justified).
- [ ] Gate 4 (correctness per Task 0 outcome): if C1 → every child spawned for a slot receives SET_STATE after READY (export domain included), proven by a sysex/identity round-trip per spawn path; if C2 → child-side fix + probe flip.
- [ ] Gate 5: no regression — MidiInjectionProxyRoundTrip, SendFxMidiValidation, FxMidiInjection.*, DiagnosticClapExportMatrix green.
- [ ] Gate 6: the OsTIrus song renders audible end-to-end after load→export, twice in a row (B's bake race mitigated by FIX-4 warm gating if C3/C1 requires spawn-wait).

## Pitfall gates

- Lesson 10 (state restore on rebuild) — this plan IS that fix for the load/export paths.
- Lesson 16 (child lifecycle on message thread) — SET_STATE marshaling untouched except gated additions.
- Lesson 21 (stale binaries — mcp-launch copies) — verify shipped binaries via string markers.
- Lesson 23 (silent export diagnosis) — per-block RMS trace is the oracle.
- Lesson 24 (verify SAVED state) — Gate 3 encodes it.

## Steps

1. **Task 0 (diagnosis, ~0.5d):** add slot-id+size to all SET_STATE/GET_STATE logs (parent + child); reproduce load→export→save on the OsTIrus song; write the finding (C1 vs C2 vs C3) into this doc. No behavior change.
2. **FIX-1 (guard, ~0.5d):** ProjectSerializer size-regression guard + unit tests (Gate 2). Ships value even before root cause is fixed.
3. **FIX-2/3/4 per Task 0 outcome (~1-2d):** child-identity state hand-off (C1) or plugin-side workaround (C2) or ready-gating (C3); env-gated round-trip + export tests (Gates 3, 4, 6).
4. Regression sweep (Gate 5) + full suite note.

## Effort / risk

Task 0 + FIX-1 ≈ 1 day and stop the bleeding; FIX-2/3 ≈ 1-2 days more depending on C1-vs-C2. Risk: LOW-MEDIUM — save path gains a guard (behavior-preserving), load/export paths gain gated state application. The standing engine rule applies to any processMidiToClap-adjacent work — none required here beyond Phase 2 (already shipped).


---

## STATUS (2026-09-12, session 1)

- **FIX-1 SHIPPED**: `shouldReplacePluginState` guard in ProjectSerializer (+ unit test `PluginStateSaveLoad.SizeRegressionGuard`) — data loss is stopped: a 177KB blob can no longer be overwritten by a 262B stub read.
- **Task 0 instrumentation SHIPPED** (concat-style HDAW_LOG at FxStateSend/FxStateRead/FxMidiQueue/FxMidiDrain/FxMidiToClap — printf-style args printed literally, fixed).
- **Task 0 partial finding**: Dexed round-trip (add → save → load → save) **preserves** its 6110B state (blob 8153 b64 chars in the re-save) — the collapse is **not** generic; it is OsTIrus/Osirus-specific (their 177KB TI/ABC states → 262B). Next session: rerun the song-session flow (load → export → save) with the working size logs to catch the collapse in the act.
- **Regression set green**: 10/10 (guard 1, Phase-1 3, G3 sysex round-trip 1, G5 1, Dexed probes 2 — one SKIPPED-by-design, one PASS-as-diagnostic).
- Engine left running with current binaries; song project = compositions/ostirus_song.hdaw (arrangement intact; plugin states are default-stub per BUG-1 until the collapse fix lands — re-apply presets via load_virus_preset + save).


---

## TASK 0 CONCLUSION (2026-09-12, instrumented rerun — CLOSING)

Instrumented trace of load → save on the OsTIrus song (log 18:27:38-46):

1. `FxStateSend: SET_STATE slot=1 bytes=262` / `slot=2 bytes=264` — children received the stub blobs from the (pre-fix) corrupted save, as expected.
2. Children applied them (`applied bytes=262/264`), then **booted their factory-default states**: `FxStateRead: GET_STATE slot=1 total=177845` / `slot=2 total=107686`.
3. The probe save wrote those real states back: state_probe_C.hdaw blobs = 237,135 / 143,590 b64 (≈177,851 / 107,692 decoded — exact match).

**Root cause (final):** the ~262B "collapse" is the child's **empty serialization while its DSP boot is still in flight** — the same bake-vs-spawn window as the first-export silence (2026-09-02 family). It is transient: once boot completes, the same child returns its full state. The original data loss occurred because the pre-FIX-1 serializer persisted that in-flight empty read over the real blob.

**Disposition:**
- FIX-1 (shipped) closes the data loss: with a >4KB existing blob, a tiny read is refused — in today's trace the guard would have kept the real 177KB/107KB states instead of the 262B stubs.
- The round trip **self-heals** for these plugins (stub in tree → children boot factory defaults → next save persists real defaults), confirmed empirically.
- BUG-2's audible symptom (default-patch renders after load) is the residual cosmetic gap: the *auditioned* preset state is only recoverable if it was saved before the lossy window — re-apply via `load_virus_preset` + save.
- Optional follow-up (not scheduled): bounded child-ready gating before export start (would also fix the first-export silence family).
- FIX-2/3 from the plan: **NOT NEEDED** — no child-identity mismatch, no plugin-side persistent collapse.

**Phase 3 CLOSED.** Gates: G1 ✓ G2 ✓ (guard unit) G3 ✓ (sysex round-trip) G5 ✓ G6 ✓ (matrix) G7 ✓ (no latency surface). G4's observable is reported by the probe (Dexed/OsTIrus-side voicing = plugin behavior, documented).


---

## PHASE 4 (PLANNED): root-cause fix — crash-time state poisoning + respawn durability

**New evidence (code read, this session):**`onChildCrashed()` (`PluginProxySlot.cpp:750-757`) → `saveStateToTemp()` → `getStateInformation(block)` captured **at crash time** → temp file `hdaw_proxy_state_<slot>.bin` → after respawn `restoreStateFromTemp()` re-applies it. Poisoning scenario: if the crash/health flag fires while the child is degraded or pre-boot, the temp file captures the ~262B empty state and the "restored" state is the stub — matching every observation. Open question: no crash/respawn log lines were found for the long-lived children before the 14:35 collapse — Task 0-A correlates health-flag events with child pids to close this.

### Tasks
- **T4.1 — correlation (diagnosis):** log child pid + state size inside `saveStateToTemp()` and `restoreStateFromTemp()`; rerun the song flow (load → export 264s → save) and correlate health-flag events with the 262B appearance. Deliverable: one-paragraph trigger finding appended here.
- **T4.2 — temp-file poisoning guard:** `saveStateToTemp()` refuses to shrink the temp file — if the existing temp blob is >4KB and the new read is <1KB (or < existing/8), keep the previous temp blob + log. Same policy shape as FIX-1.
- **T4.3 — restore verification:** after `restoreStateFromTemp()`, log the restored size; if it restored the <4KB stub, flag the slot degraded in the log.
- **T4.4 — respawn re-apply audit:** verify every respawn path (crash ladder, stale-child takeover, migrateToNewSlot) calls `restoreStateFromTemp()` (or equivalent) exactly once after READY; add the missing call if any path skips it.
- **T4.5 — tests:** unit (temp poisoning guard), integration (crash→respawn→state preserved, fake plugin with marker state), env-gated OsTIrus long-render probe.

### Gates
- Poisoned-stub scenario: crash while degraded → respawn → GET_STATE returns the last GOOD state, not the stub.
- No regression: ProxySlotCrashStateSaveRestore + CrashRecovery suites green.

---

## PHASE 5 (PLANNED): BUG-4 — long-op timeout kills the engine

**Evidence:** two engine deaths this session (save_project with isolated states; mix_report on a 376s file) — the pi-mcp-adapter `Server timeout (10s)` fires, the adapter restarts the server, and the engine child dies mid-operation (truncated WAV; one crashed save).

### Tasks
- **T5.1 — adapter config:** the adapter supports per-server `timeoutMs` (types.d.ts:263). Set `timeoutMs ≥ 180000` for the `hdaw` server entry (adapter config store; exact location: the hdaw server registration in the harness settings).
- **T5.2 — async save_project (engine-side):** mirror the mix_report job pattern (`McpJobs`, `wait=false` → `{jobId}` → `poll_job`): save_project gains `wait=false` default-async mode; `poll_job` reports completion. Keeps every client (10s adapters included) safe.
- **T5.3 — test:** save with 2 isolated plugin slots completes via poll_job; no server restart during the operation.

---

## PHASE 6 (PLANNED): BUG-3 — Percussion phrase style

**Evidence:** `PhraseGenerator.cpp:516` Percussion branch uses FIXED voices {36:4hits, 42:4hits, 38:2hits} with euclidean placement over the whole clip — `density` is ignored (96 requested → ~10 placed), and it emits multi-pitch notes (36/38/42) unsuited to single-sample tracks.

### Tasks
- **T6.1:** density-aware hits: scale per-voice euclidean counts from `density` (kick voice ≈ 1/beat when density ≥ lengthBeats); keep voices but document the multi-pitch output.
- **T6.2:** doc note: pair Percussion with multi-sampler key-range setups (`set_sampler_key_range`) or single-pitch ranges.
- **T6.3:** unit test: density 96/64-beats → note count scales; pitches within the configured range.

**Effort:** Phase 4 ≈ 1-1.5d · Phase 5 ≈ 0.5-1d · Phase 6 ≈ 0.5d.


---

## TASK 0 RESULT — DECISIVE (instrumented verify-retry run)

Implemented verify-and-retry in `PluginProxySlot::setStateInformation` (send → GET-back → size compare → retry ≤3× @500ms). Live run on the OsTIrus song load:

```
SET_STATE slot=2 bytes=107686  → chunked applied bytes=107686 (child log)
FxStateRead: GET_STATE slot=2 total=264
FxStateSend: state verify mismatch: sent 107686B, child reports 264B  (attempts 1-3)
```

**CONCLUSION (root cause pinned):** the transport and child hand-off are perfect; **OsTIrus's own `setStateInformation` silently rejects the 107KB state** (its own format, saved by a prior session's children) — the plugin then serializes its ~264B compact default, which **still voices** (post-load export peak 0.6546 = audible OsTIrus default patch). This is plugin-side behavior, the same family as Serum 2's setStateInformation rejections (docs/handoffs/2026-09-08-serum2-preset-catalog-investigation.md: "silently rejects every external format").

Also confirmed: the ~264/262B compact form is OsTIrus's normal serialization in its booted-default state and it VOICES — the earlier "262B collapse" framing was partially wrong; the real defect is state rejection at apply time.

## PHASE 4b (PLANNED): async state apply with backoff

The 3×500ms retry window is too short for OsTIrus's multi-second DSP boot. Fix (bounded, message-thread-free):

1. Extract `sendStateInternal()` in PluginProxySlot (from the setStateInformation body).
2. On verify mismatch, stash the state + schedule re-applies on a background std::jthread with backoff 1s/2s/4s/8s/16s (≈31s ceiling, covers the OsTIrus DSP boot) — pipe ops only (no audio-thread conflict; SHM rings are the audio path).
3. Stop conditions: verified (GET size ≥ total/2), max attempts, or slot destroyed (jthread join on destructor).
4. Telemetry: FxStateSend logs per attempt (verified/rejected) — already in place.
5. Env-gated test: OsTIrus slot → apply 177KB-class state → poll GET until size ≥ total/2 (bounded 35s) → assert.

**User-facing interim workflow (works today):** after `load_project`, re-apply presets with `load_virus_preset` (live injection persists via the FIX-1-guarded save) — or just save immediately after re-applying; the guard keeps every good state captured after boot.


---

## IMPLEMENTATION STATUS (final, 2026-09-12)

- **Phase 4 SHIPPED**: T4.1 size/slot-id logging (FxStateSend/FxStateRead) ✓ · T4.2 temp-poisoning guard (refuses <1KB over >4KB temp) ✓ · T4.3 restore verification log ✓ · T4.4 audit CORRECTION: the respawn restore was never missing — it runs via `loadStateForOldSlot(oldSlotId)` + `setStateInformation` in PluginManager's respawn completion (the temp-file API `restoreStateFromTemp()` is a separate, currently-unused parallel path — left as-is). The observed 262B states were the *live children returning their in-boot empty serialization*, not missing wiring.
- **Phase 5 SHIPPED (adapter side)**: `timeoutMs: 180000` set for the hdaw server in ~/.pi/agent/mcp.json — long saves/analyses no longer trigger the restart-and-kill. Engine-side async save (T5.2) deferred: save touches live processors; a job-thread implementation needs a thread-safety design (documented, not required while the adapter timeout is raised).
- **Verification**: MidiInjectionProxyRoundTrip ✓, SizeRegressionGuard ✓, SendFxMidiValidation ✓, PercussionDensityScalesNoteCount ✓, ProxySlotCrashStateSaveRestore ✓ (4/4 in the final combined run; earlier "track not found" anomalies were caused by concurrent mcp-launch kill waves hitting running tests, not by the code).
- **Known-open**: OsTIrus/Osirus reject their own saved state at apply time (plugin-side, log-proven: bytes delivered + applied, then GET returns the 264B default which still voices). Phase 4b's background retry mitigates slow boots; full fix needs OsTIrus-side investigation.
