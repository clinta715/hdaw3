# Plan: forward transport clock (playhead) to isolated plugin children

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Task 1 is a gate** — verify the in-process tick + playhead propagation before touching production code.

**Goal:** Isolated CLAP children never receive the transport playhead, so clock-reliant instruments (ShinRonin, Odin2, Gneiss, Retrospect, NodalRed2x) render silence on isolated export while free-running instruments (Vital, Dexed, JC303, Identity, Altitude) work. Fix: forward the playhead from the parent (`PluginProxySlot`) to the child (`PluginHost`) over the existing SHM, remove the fixed plugins from `kKnownSilent` in `DiagnosticClapExportMatrix`.

**Companion doc:** `docs/plans/2026-08-09-investigate-isolated-export-silence.md` — the state/lifecycle discovery plan. Its H1–H5 hypotheses are **refuted**: the proxy-fidelity commit c05ec4b fixed state/MIDI/params/programs, and the current-build binary probe (user-run, this session burned `ShinRonin.wav`) still shows **peak = 0** for ShinRonin. Root cause is **H6** below.

**Evidence update (this session):** NodalRed2x is **NOT** a transport-clock case — an unhandled SEH dump shows it **hard-crashes inside its own `process()`** (`NodalRed2x+0x72cc96` ← child stack: `PluginHost::audioLoop` → `CLAPPluginInstance::processBlock+0x2902` → `NodalRed2x` frames → `UnhandledExceptionFilter`), with **no playhead present**. The isolation containment converted the crash into silent output (parent `buffer.clear()` fallback). NodalRed2x remains in `kKnownSilent` permanently with this dump as evidence. During the Task-6 sweep, record whether NodalRed2x's crash *changes* once a playhead IS forwarded (transport-sensitive vs unconditional) — a one-run observation, not a fix.

## RESOLVED — full investigation outcome (2026-08-09)

**Sweep evidence (this session, WIP + `/EHa` child):**
| Probe | ShinRonin | Gneiss | Retrospect | NodalRed2x |
|---|---|---|---|---|
| isolated export, transport ON | peak=0, hung=0 | peak=0 | peak=0 | peak=0, hung=0 (crash contained by /EHa) |
| isolated export, transport OFF (`HDAW_NO_TRANSPORT_EVENT=1`) | peak=0, **zero crashes** | peak=0 | peak=0 | peak=0 |
| in-process (`HDAW_NO_PLUGIN_ISOLATION=1`) | **AV 0xC0000005, kills the test process** | — | — | — |

**Confirmed facts:**
1. **Input delivery is correct and complete**: child snapshots show the playhead flowing (`CLOCK child snapshot rev=455→500 playing=1 bpm=120`); `ClapFeed` shows notes (`[N0 k=50 on]`…) with playhead fields present; `ClapDiag` shows `activated=1 processing=1`, healthy status (1/2), **`outPeak=0.000000` in the plugin's own output**.
2. **The silence is the plugins' own**: with correct input, process succeeds and outputs zeros — factory-default patches that produce no audio. This is the discovery plan's **H2** (default-patch silence), not the proxy and not the transport.
3. **The transport event triggers crashes** (plugin-side defect): with transport ON, a child crash-loops every block from ~call 249 (~2.9 s — consistent with `song_pos_seconds`-as-unbounded-index semantics; NodalRed2x crashes from its first block and pre-/EHa produced 668 MB dumps + hang). With transport OFF: zero crashes anywhere.
4. **Containment fix verified**: `/EHa` on `hdaw_plugin_host` ⇒ the `SIL CRASH` catch fires; NodalRed2x degrades to silent-survive (hung=0) instead of a dump storm.
5. **Transport forward itself works end-to-end** (parent pack → shm fields → `ChildPlayHead` snapshot → `__transportprobe__` round-trip test in `isolation_integration_test.cpp`).

**DISPOSITION:** the transport forward ships (correct host behavior, needed by clock-reliant instruments, proven by the probe test); all five stay in `kKnownSilent` **permanently** with this evidence (H2 default-patch silence; transport-event AVs contained). Follow-up (out of scope): preset-load sweep to test whether these instruments sound once a real patch is loaded.

## SOURCE-LEVEL ROOT CAUSE — NodalRed2x (gearmulator 2.2.9, `source/nord/n2x`)

User provided the NodalRed2x source (gearmulator Nord 2x port). Two independent, source-proven defects make it permanently unusable in HDAW:

1. **Silence by design — no presets shipped.** `n2xdevice.cpp:91-94` drops program-change messages: *"we do not have any valid presets in the device, this will select garbage"*. The default boot state is a silent patch; the matrix feeds no state/preset → `outPeak=0` with healthy `status=1/2`, always. The OS ROM (`nord_lead_2x.bin`, 512 KB = `g_romSize`) **is** present next to `NodalRed2x.clap`, so the DSP boots — silence is not a boot failure.
2. **Crash by wrapper defect — 4-channel device behind a 2-channel CLAP entry.** `n2xPluginProcessor.cpp:33-35` declares two stereo output buses (Out AB + Out CD); the CLAP form exposes only the first → hosts give it 2 channels. But `n2xhardware.cpp:157-163` **unconditionally writes 4 channels** into `_outputs[0..3]` → `_outputs[2]/[3]` are nullptr → AV in the output copy whenever the DSP produces frames (voice activity). Any stereo-only host (not just HDAW) would crash identically; the port is built for 4-channel hosting. Consistent with the `NodalRed2x+0x72cc96` fault.

**Implication:** nothing in HDAW can make NodalRed2x audible (needs a preset the port doesn't ship) or safe (needs a 4-channel bus its CLAP entry doesn't declare). `kKnownSilent` is permanent with this evidence; `/EHa` containment is the correct mitigation. The transport-event correlation from the sweep runs was run-state noise — the crash mechanism is channel-count, transport-independent.

**Tech Stack:** C++17, JUCE 8, CLAP SDK, plugin process-isolation (`hdaw_plugin_host.exe`), GTest, Win32 SHM atomics.

---

## Root cause (H6 — confirmed by elimination)

- In-process `CLAPPluginInstance::processBlock` builds transport clock events (tempo/ppq/timeSig) from `getPlayHead()`; the `transport_clock` + `MidiClockLatchingTest` gtest suites prove clock events reach plugins in-process → **instrument state is not the problem**.
- `src/proxy` has **zero playhead producers**: grep for `playHead`/`setPlayHead`/`transport` across `src/proxy` matches only unrelated stubs. The child's plugin is created without a playhead and none is ever supplied.
- The child's `CLAPPluginInstance` shares ~100% of the in-process code path — the **only missing input is the `AudioPlayHead`**.
- Empirical split: all 5 silent instruments schedule/gate from the transport clock; all 5 audible ones are free-running oscillators. Odin2/ShinRonin sequencers step only when transport runs.

---

## Design

**Parent side — `PluginProxySlot::processBlock` (`src/proxy/PluginProxySlot.cpp:318`):**
- Per block, read `auto* ph = getPlayHead(); auto pos = ph ? ph->getPosition() : nullptr;`.
- If non-null, pack into new `ShmHeader` fields (`src/proxy/ProxyCommon.h:67`): `transportPlaying` (u32), `transportTempoBits` (u32 IEEE), `transportSecondsBits` (u64 IEEE), `transportPpqBits` (u64 IEEE), `transportTsigN/D` (u32, default 4/4), then **release-store** `transportRevision` bump (u32, wraps naturally). If null → skip (revision unchanged).
- Cost on the live audio thread: ~8 atomic stores + one `PositionInfo` construction. `InternalPlayHead::getPosition` (`src/engine/TransportManager.h:173`) only reads `TransportManager` atomics and builds a value class — **no allocation, no locks** (mirrors what `Track::processBlock` already does live). Verify this before finalizing.
- Playhead reachability: `AudioProcessorGraph::setPlayHead` propagates to node processors in JUCE (verify in the JUCE source during Task 1); `MainAudioProcessor.cpp` sets it on the live graph (line 67) and `ExportManager.cpp` on the render graph (both before `prepareToPlay`). If propagation does NOT reach graph nodes, fallback: `TrackFXSlot::prepareToPlay`/`prepare` calls `pluginInstance->setPlayHead(...)` — check the TrackFXSlot seam first.

**Child side — `src/proxy/host/PluginHost.cpp`:**
- Add a minimal `ChildPlayHead : public juce::AudioPlayHead` (mirrors `InternalPlayHead` semantics: tsig 4/4, isPlaying, bpm, ppq, timeInSeconds, timeInSamples derived from seconds × sampleRate; null-ish defaults when fields absent).
- Call `plugin->setPlayHead(&childPlayHead)` **once at load** (control thread, before `audioLoop` starts).
- In `audioLoop`, before `instrument->processBlock`: load `transportRevision` (acquire); if changed since last snapshot, copy the header fields into the plain member fields of `ChildPlayHead`. After load, **only the child audio thread touches them** (plugin `processBlock` is called from `audioLoop` on the same thread) → plain fields, no atomics needed child-side.
- No change to `TrackFXSlot`, `Track`, `ValueTree`, or the RPC layer.

**Not doing:** MIDI (PPQ/SMPTE) clock emulation — the CLAP clock event is the correct mechanism; don't add a second transport path. No new messages (SHM beats a new pipe message: zero creation, zero jitter).

---

## Tasks

- [ ] **Task 1 (gate):** Verify (a) `CLAPPluginInstance::processBlock` emits the transport clock event from `getPlayHead()` only when a playhead is present; (b) `AudioProcessorGraph::setPlayHead` propagates to nodes in the exact JUCE version in use; (c) `InternalPlayHead::getPosition` is alloc-free/lock-free. Record findings; if (b) is false, adopt the `TrackFXSlot` fallback.
- [ ] **Task 2:** Add the transport fields + `transportRevision` to `ShmHeader` (`src/proxy/ProxyCommon.h`) with a doc comment. Update any size-dependent code/tests (shm capacity math must remain valid — `hdr->capacity` is authoritative; check `shm_test` for exact-size assertions).
- [ ] **Task 3:** Parent-side packing in `PluginProxySlot::processBlock` (lock-free, no allocation, release-store revision last). Live and export both run this path.
- [ ] **Task 4:** Child-side `ChildPlayHead` + snapshot in `audioLoop` + `setPlayHead` at load. Null-playhead fallback keeps existing behavior for any path that never forwards.
- [ ] **Task 5 (test seam):** Add a gtest that covers the transport handoff at the cheapest level available (prefer: child-side diagnostic — extend an existing diagnostic processor with a "report last-seen transport" param, or a brute-force read of the shm header fields after a cycle). Only if that is disproportionate, rely on the sweep (Gate 2 is the assertion) + keep the diff isolated.
- [ ] **Task 6:** Remove **ShinRonin** (primary target) from `kKnownSilent`; re-run the matrix; remove others (Odin2, Gneiss, Retrospect, NodalRed2x) only if they now pass. Keep any genuinely-still-silent plugin in the set with an evidence note.
- [ ] **Task 7:** If durable, add AGENTS.md lesson 16 (lesson-14 family: cross-process transport data must be forwarded explicitly) pointing at this plan.

---

## Pitfall Gates

| Gate | Why | Mitigation |
|------|-----|------------|
| **Gate 2 (unimplemented path)** | A "fix" that silently no-ops | `DiagnosticClapExportMatrix` `EXPECT_GT(peak, ...)` after removing from `kKnownSilent` is the proof |
| **Lesson 14 / Gate 5 (proxy boundary)** | Transport fields cross a boundary that already truncated state once | Fixed-size atomics with bit-pattern values (no pointers, no chunks); revision protocol with equality (wrap-safe) comparison; bounds-checked reads |
| **Gate 3 (audio-thread safety)** | Parent packs on the live audio thread | No locks, no allocation, no new state — verified in Task 1 (c); ~8 atomic stores per block |
| **Gate 1 (state restore)** | Not applicable — no DSP state added; no ValueTree changes (no `setProperty` landmine) | State update to plan if the fallback hooks `TrackFXSlot` |
| **Lesson 15 (stale binary)** | Manifest builds may skip the recompile | Verify the **sweep output** (the binary jointly proves freshness); check `.obj` timestamps if the sweep contradicts the source |
| **Lesson 13 (DSP-state writes)** | Parent only writes shm atomics, never plugin DSP objects | No `stateLock` requirement introduced |
| **Lesson 6 (rebuild cost)** | No routing-graph rebuild added — none at all | N/A |
| **Lesson 1 (beats vs seconds)** | Values are copied verbatim from the engine's native units (ppq/seconds/tempo) | No conversions in this change |
| **Shm size change** | Header grows → ring capacity changes | Computed from `hdr->capacity` everywhere; run `shm_test` + the full suite |

## Anti-Pattern Scan

- No `rebuildRoutingGraph()` calls added.
- No locks/allocation on any audio thread (parent or child).
- No messages added to the proxy protocol; SHM atomics only.
- Do not loosen `DiagnosticClapExportMatrix` assertions.

---

## Success Gates (all must pass)

- [ ] Task 1 findings recorded (playhead propagation + clock-event gating confirmed).
- [ ] Build: `cmake --build build --config Debug` → 0 errors.
- [ ] **Sweep:** isolated export of ShinRonin now **non-silent (peak > 0.05)**; Odin2/Gneiss/Retrospect/NodalRed2x verified and removed from `kKnownSilent` if non-silent (or remain with evidence); the 5 previously-working plugins stay within ~20% of their pre-fix peaks (no fidelity regression).
- [ ] Full `build\Debug\hdaw_tests.exe` green except the known `PluginIsolation.*` baseline; `TransportClock*` / `MidiClock*` suites still green (in-process path untouched).
- [ ] Live path reasoning recorded: identical `processBlock` code runs live and in export, so the live isolated-plugin transport is fixed by the same change (manual live check on ShinRonin if a runnable binary is available).
- [ ] No xrun/perf regression visible in live playback when a track with an isolated plugin is playing.

## Verification commands

```
cmake --build build --config Debug
build\Debug\hdaw_tests.exe --gtest_filter=McpServer.DiagnosticClapExportMatrix
build\Debug\hdaw_tests.exe
```

## Note

If a plugin proves clock-independent but still silent (e.g., it needs program/state AND clock), keep it in `kKnownSilent` with evidence — the shared transport-clock fix still ships for the others, and the plan's premise (child missing playhead) is proven by whichever plugins recover.