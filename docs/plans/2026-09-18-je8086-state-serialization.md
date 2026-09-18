# JE8086 state serialization — working notes (Path 2 / deliverable 1)

Title: JE8086 state-serialization investigation + fix — Path 2 / deliverable 1.
NOTE: this file was reconstructed from subagent reports after an accidental overwrite
(recovery 2026-09-18); technical facts preserved from the original text + reports.

## Phase A — clone + baseline build (COMPLETE, all gates passed)
- Clone: https://github.com/dsp56300/gearmulator → D:\pdf\gearmulator-git (= /mnt/d/pdf/gearmulator-git)
  git clone --recurse-submodules --jobs 8 (Windows git 2.55.0.windows.3), exit 0.
- Matching commit: tag 2.2.9 = 6ff5ef3b19e2156c5ec9bd68fa85abe11b0b77f5 (2026-07-29). Worktree clean,
  detached HEAD. All 8 submodules populated (JUCE 12373ff27, clap-juce-extensions 2aaad9e,
  dsp56300 c051afad, RmlUi f8d6ff42, freetype 82891652, lunasvg f8aabfb4, mc68k 0d5dbaf,
  cpp-terminal a79a1a17).
- Equivalence: diff of source/ronaldo/je8086, jucePluginLib, synthLib, juce.cmake, base.cmake,
  CMakeLists.txt, doc/changelog.txt vs local 2.2.9 tree = byte-identical (CRLF-only deltas).
- Build recipe: cmake (VS18-bundled 4.3.1) -G "Visual Studio 18" -T v145; build dir temp\cmake_vs2026;
  cmake --build temp\cmake_vs2026 --config Release --parallel --target jeJucePlugin_CLAP JE8086TestConsole.
- Artifacts: JE8086.clap → bin\plugins\Release\CLAP\JE8086.clap, 36,154,368 B, md5 4d00528a01ff27528d1325205d8e3aa2
  (clap_entry exported). JE8086TestConsole.exe → temp\cmake_vs2026\source\ronaldo\je8086\jeTestConsole\Release\,
  2,177,024 B, md5 7fcec181e72a38d303d0f037172b7695.
- Installed CLAP backup: C:\Program Files\Common Files\CLAP\JE8086.clap (md5 f4a19cd63f0963a30238829a69fc80dc,
  34,491,904 B) → D:\pdf\clap-backups\2026-09-18\JE8086.clap (verified).

## Phase B — jeLib state probe (COMPLETE, VERDICT A)
Additive-only probe target source/ronaldo/je8086/jeStateProbe (jeLib::Device via RomLoader::findROM,
88200 Hz; add_subdirectory line in ronaldo/je8086/CMakeLists.txt). 3 variants @ 8s, each factory reset:
| variant | BOOT | POST | BOOT vs POST |
|---|---|---|---|
| warm (params @3s) | 641 B, 6edbef02 | 641 B, df1f1702 | 8 diff bytes @ 164,168,169,177,188,201,204,387 |
| early (params @0.2s) | empty (pre-boot-gate) | 641 B df1f1702 | byte-identical to warm POST |
| baseline | 6edbef02 | 6edbef02 | 0 diff (determinism clean) |
The 8 diff bytes decode in PatchUpper dump as EXACTLY the written values (Lfo1Rate 64→100, RingMod
0→1, Xmod 0→90, Osc1Waveform 0→6, Cutoff 34→10, AmpAttack 30→127, AmpRelease 91→120 + one firmware
byte 0→21). RMS changed accordingly; midiOut after params = F0 7D LCD stream only (no dump echo).
VERDICT A: jeLib serializes param-driven state correctly when clocked; NO timing dependence.
Params used (jeLib Patch enum values): 17=Lfo1Rate 100, 21=RingMod 1, 22=Xmod 90, 30=Osc1Waveform 6,
41=Cutoff 10, 54=AmpAttack 127, 57=AmpRelease 120.

## Phase C — end-to-end localization (COMPLETE)
Instrumented gearmulator wrapper L1-L7 (env-gated JE8086_TRACE=1 → %TEMP%\je8086_trace_<pid>.log),
rebuilt jeJucePlugin_CLAP (md5 8ed4892bf979e1c13c987099ffc30de), swapped in, drove real HDAW flow
(3 runs: idle / playing / matrix+offline), collected traces, restored original CLAP (md5 verified
f4a19cd6...), reverted clone to clean (tracked files clean; jeStateProbe/ + probe-out/ untracked).
Patch saved: D:/pdf/gearmulator-git/probe-out/phaseC-trace.patch (367 lines: jeTrace.h + 7 files).
Raw traces: D:/pdf/gearmulator-git/probe-out/phaseC-raw/.

CHAIN MAP (file:line, both repos):
1. HDAW set_fx_param → src/mcp/McpTools_FxSlot.cpp → AudioEngineCommands::setFxSlotParam →
   TrackFXSlot::setParam → proxy (isolated).
2. Child CLAP host → clap-juce-extensions @2aaad9e params extension.
3. JUCE pluginLib::Parameter::setValue → setUnnormalizedValue(Origin::HostAutomation) → sendToSynth →
   RATE-LIMITED 25/s via juce::Timer::callAfterDelay (message-thread timers) → sendParameterChange →
   jeController::sendParameterChange (jeController.cpp:84+) → createParameterChange sysex → sendSysEx →
   Processor::addMidiEvent (jucePluginLib/processor.cpp:67) → routing → synthLib::Plugin::addMidiEvent →
   m_midiInRingBuffer (synthLib/plugin.cpp:23, RingBuffer 1024).
4. Audio thread: Plugin::process → processMidiInEvents (plugin.cpp:98,238) → m_device->process
   (plugin.cpp:104) → Device::process (synthLib/device.cpp:35) → jeLib::Device::sendMidi →
   m_state.receive (jeLib/state.cpp:489) + m_midiIn → JeThread → emu.
5. Capture: captureFxSlotState (AudioEngineCommands_Fx.cpp) → child getStateInformation →
   Processor::saveCustomData → saveChunkData "MIDI" chunk (jucePluginLib/processor.cpp:285,680) →
   jeLib Device::getState(StateTypeGlobal) → m_state dumps.
6. D-lite guard (stateLooksUnchangedSinceBoot) + get_fx_capture_status (HDAW).

PER-LINK VERDICT (trace evidence):
- L1 Parameter::setValue/sendToSynth — DEAD for target params (0 lines for all 6 indices in every trace
  incl. offline child; only one boot-time line: plain=127 origin=3 part=0 page=0 idx=51).
- L2 jeController::sendParameterChange — DEAD (count 0 in ALL children).
- L3/L4/L5/L6 — FIRED only for boot/system dumps + midi events; param sysex never seen.
- L7 Device::getState + getStateInformation — FIRED + correct.
VERDICT: params staged parent-side in ProxiedParameter caches (cache read-back proven), NEVER flushed
through the shm paramSet ring (child drain sees pr==pw even fully clocked: 599/598 block-drains during
play/export); MIDI ring delivers in same contexts (plumbing alive). The gearmulator wrapper is
exonerated; the break is HDAW's proxy param transport. D-lite correctly reports 'unchanged' because
the state really is the default patch.

Secondary finding: LIVE param indices differ from jeLib Patch-enum indices — je8086_param_index_map.json
(regenerated 2026-09-16) maps CutoffFrequency→59, AmpEnvelopeAttackTime→73, AmpEnvelopeReleaseTime→76
(live list); Phase B's 41/54 are jeLib enum values. Live 54 = "A OSC2 LFO1 DEPTH".

RESTORED: original CLAP back in place (md5 f4a19cd63f0963a30238829a69fc80dc), engines killed, clone
clean (HEAD 6ff5ef3b; only untracked jeStateProbe/ + probe-out/).

## Phase C2 — PLAN (HDAW proxy param delivery fix; user opted in; full gates)

### Goal
Make staged params (set_fx_param / ProxiedParameter::setValue) reach the isolated child's CLAP in
every context (stopped, playing, offline render) so capture != boot and offline renders carry presets.

### Dependency map (graphify fresh at HEAD e5ce6c6f)
- Nodes: PluginProxySlot.h (ProxiedParameter L41, setValue L67, setCache L76, processBlock L104,
  stageParam L151, paramCacheSize_ L246); PluginProxySlot.cpp stageParam L140, fetchParamMetadata
  L156-218 (any failure sets paramCacheSize_=0), flush L556-578 (dirty-scan → pack → ring write,
  guarded by paramCacheSize_>0 && stagedParams_ && paramDirty_; setRing null check).
- MainAudioProcessor early-out ~L255 (lesson-3 buzz guard: when transport stopped and not recording,
  graph clears buffers + returns → PluginProxySlot::processBlock (and flush) NEVER runs).
- Upstream: TrackFXSlot::setParam; McpTools_FxSlot set_fx_param → AudioEngineCommands::setFxSlotParam.
- Downstream: paramSet ring → child drain → CLAP setParam.
- Tests: tests/integration/proxy/isolation_integration_test.cpp (pushBlockAndReadProbe L589),
  tests/unit/proxy/common_test.cpp, MatrixPresetsTest, FxMidiInjection.
- Community boundary: proxy x engine x tests; flush refactor must not break the buzz-guard early-out
  semantics nor add audio-thread hazards (Gate 3), ring bounded (Gate 14), stale binaries (Gate 15),
  stale engines before proxy tests (lesson 20).

### Leading hypothesis
paramCacheSize_ == 0 in the live child context (fetchParamMetadata failed or never ran) → stageParam
no-ops (index >= paramCacheSize_ guard returns) → flush no-ops → ring never written. Consistent with
all Phase C evidence (cache read-back works — it predates stageParam; ring empty; pr==pw).
Alternatives: getParamSetRing() null; sw-sr parity; stopped early-out prevents flush (contradicted by
playing run); dirty never set (contradicted by setValue→stageParam path).

### C2a (current unit) — instrument + pin (logging ONLY, no behavior change)
Parent (PluginProxySlot.cpp): P1 stageParam entry (index, paramCacheSize_, stored?); P2
fetchParamMetadata (entry, pipe null, each failure path setting paramCacheSize_=0, success n); P3
flush (throttled: paramCacheSize_, setRing null?, dirtyCount, sw, sr, writes). Child: C1 paramSet
drain (pr, pw, CLAP setParam calls). Scenario: (a) stopped apply → capture @200ms/1s/3s; (b) playing
apply → capture; (c) offline render w/ overrides. Verdict: pin dead sub-link + leading root cause.

### C2b (next) — implement fix per pinned link; add gtest for the seam
### C2c (last) — full gates:
 G1 capture != boot (status ok, stateBytes>0, md5 differs)
 G2 offline render differs from init (rms/md5; live index map 59/73 etc.)
 G3 save → load → offline render carries patch
 G4 HDAW suites green (MatrixPresetsTest, proxy suites, FxMidiInjection)
 G5 provenance (notes + handoff doc; CLAP unchanged md5 re-verified)
## Phase C2a — instrument + pin (retry)

### Instrumentation (complete in working tree; previous attempt left P1/P2/P3+C1, this attempt added the processBlock-top entry line)
- [x] PARAM_TRACE: src/proxy/ParamTrace.h (env HDAW_TRACE_PARAM=1 -> %TEMP%\hdaw_paramtrace_<pid>.log; RAII, mutex, [pid +ms] prefix; logging-only)
- [x] P1 stageParam (PluginProxySlot.cpp ~L141): idx, cache, staged, STORE=0/1
- [x] P2 fetchParamMetadata (~L156): begin pipe, all failure paths (pipe-null, send-count, recv-count, count-type, count-size, count-n, info-send/recv/type/header, name-recv/type), ok n=
- [x] P3 flush heartbeat (~L592): every 200 blocks: pb, cache, staged, dirtyPtr, ring, dirty, sw, sr; immediate FLUSHED on any write
- [x] P3E processBlock entry (~L383): first + every 200 blocks, seq + playing flag (buzz-guard correlation)
- [x] C1 child paramSet drain (host/PluginHost.cpp): per-drain SET + DRAINED + every-200 heartbeat (pr, pw)
- [x] Driver: probe-c2a/c2a_driver.py (MCP HTTP 127.0.0.1:18765; live idx 17/21/22/59/73/76; capture @200ms/1s/3s; save-project state md5)

### Build (GA-1 evidence)
- Baseline (pre-instrumentation, 2026-09-18 07:5x): HDAW_headless.exe md5 80de7abee6ebcb714a24c7d350c61e56; hdaw_plugin_host.exe md5 fe24342bffffcfc048cf6c0f0f001159
- Time-sync hook ran (scripts/time-sync.sh: "WSL clock synced to Windows host")
- Instrumented build: cmake --build build --target HDAW_headless (ninja, RelWithDebInfo) + --target hdaw_plugin_host; exit 0
- NEW HDAW_headless.exe md5 6c5be6bb61417ae9663647174bd7d7a9, LastWrite 2026-09-18 12:56:32
- NEW hdaw_plugin_host.exe md5 9d6e28c65fe410bb5d46f446e0260722, LastWrite 2026-09-18 13:00:16
- String-verified (python byte scan): HDAW_headless.exe has P1 stageParam/P2 begin/P3 pb=/P3 FLUSHED/P3E processBlock; hdaw_plugin_host.exe has C1 SET/C1 DRAINED. (PARAM_TRACE literal absent = macro, expected; C1 absent in engine exe = child-only, expected.)
### Scenario results (run #2, engine bat-launched with HDAW_TRACE_PARAM=1, engine pid 20528; traces archived probe-c2a/traces/)
- (a) STOPPED: apply 6 params (P1 STORE=1 x6, cache=461) -> NO processBlock runs (buzz-guard) -> NO flush (no P3E/FLUSHED between +7435 and +10382) -> child state stays boot: capture@1s status=unchanged stateBytes=0 hasPluginState=0; a saves carry NO pluginState attr. Params sat staged+dirty.
- (b) PLAYING: play starts -> block pb=2/3 flushes the stalled 6 (P3 FLUSHED pb=3 wrote=6 sw=6 sr=0); apply 6 more -> each flushed within ~6-10 blocks (wrote=1 x6, sw->12, child sr tracks sw-1); child C1 SET idx=17/21/22/59 v=... x2 + 73/76 (11 SET calls, 7 DRAINED, pr=pw each drain); capture status=ok stateBytes=622 hasPluginState=1 (state deviated from boot).
- (c) OFFLINE: apply 6 (STORES again, transport stopped -> no flush); capture from LIVE child ok stateBytes=874; export child (P2 ok n=461 at +18616): P3E seq=600+ sw=0 sr=0 dirty=0 wrote=0 (nothing ever staged into it -> ring empty); render c2a_c.wav 1,728,104 B md5=c2a-run2:e666c02fa74a5f1a991444d5bcd7946c rms=0.0 peak=0.0 (SILENT).
- Save-blob fingerprints (raw attr md5): a_boot/a_post no pluginState attr (unchanged); b_post & c_pre attr n=874 raw-md5 1ce0bbb345ad6b9a8861490a5677378f (same blob; b/c receipts reported 622/874 bytes - deferred-capture receipt vs tree blob divergence observed but orthogonal).

### Per-link evidence (parent trace pid 20528, child host traces 37316/13216/42648)
| link | stopped (a) | playing (b) | offline/export (c) |
|---|---|---|---|
| L1 set_fx_param RPC | ok x6 (tree only; 'resp ok') | ok x6 | ok x6 |
| L2 ProxiedParameter::setValue->stageParam | P1 STORE=1 x6 cache=461 staged=1 | P1 STORE=1 x6 | store x6 (live) / export proxy: never staged |
| L3 flush(processBlock) | NEVER RAN (no P3E after seq=1 until play) | RAN: pb=3 wrote=6; pb=109..137 wrote=1 x6 | RAN (export): sw=0 sr=0 wrote=0 |
| L4 ring write -> child drain | ring empty (nothing written) | sw 0->12; sr advances (6..12); C1 DRAINED pr=pw each | ring empty |
| L5 child -> CLAP setValue | - | C1 SET idx=17..76 (11 calls) | - |
| capture | unchanged (boot) | ok 622B | ok 874B (live child) + SILENT render |

### Hypothesis adjudication (confirmed/refuted)
- paramCacheSize_ == 0: REFUTED - P2 ok n=461 twice (live + re-run slot) + export slot; cache=461 at every P1/P3 (n=461 is the live CLAP param count incl. the 6 target indices).
- getParamSetRing() null: REFUTED - ring=1 in all P3 heartbeats (live + export).
- buzz-guard-only-flush: CONFIRMED as THE dead sub-link for stopped transport - stageParam stores (P1 STORE=1) but flush lives inside PluginProxySlot::processBlock which MainAudioProcessor's early-out (MainAudioProcessor.cpp - 'if (!isPlayingNow() && !isRecordingNow() && !countInActive) { buffer.clear(); return; }') never invokes while stopped; the ring is only ever written by graph-driven blocks.
- sw/sr parity: HEALTHY - ring delivery works while playing (child drains same block; pr==pw in steady state is the normal empty-ring condition, not a fault).

### VERDICT (Phase C2a)
The ONE dead sub-link = the parent-side paramSet-ring flush NEVER EXECUTES while the transport is stopped: PluginProxySlot::processBlock (the only flush site, PluginProxySlot.cpp ~L592) is gated behind the MainAudioProcessor buzz-guard, so staged params (P1 STORE=1, cache=461) accumulate dirty and the ring is never written (b) notwithstanding - (b) proves the ring + child drain + CLAP setValue ALL work end-to-end once processBlock runs (wrote=6 -> C1 SET x11 -> state deviated -> capture ok). Confidence: HIGH (direct P1/P3/P3E/C1 trace correlation + capture status agreement). Secondary finding: the OFFLINE/export proxy never receives staged params at all (its ring stays sw=0 sr=0 with dirty=0 - the render gets state only via the tree pluginState blob), and the offline render is SILENT (rms=peak=0.0) even though a post-play state blob was captured - two items for C2b.

### Leading C2b fix direction
Flush the staged param array into the shm paramSet ring from a NON-graph path when the transport is stopped: the child drains the ring continuously in its own hot loop (37316 shows continuous C1 drains), so a parent-side write from the message thread (e.g., after setParam / in the 100ms timer, serialized against processBlock writes while playing) delivers in every context. Must respect the ring's single-writer SPSC contract (guard the write side: processBlock owns it while playing; message thread owns it while stopped - atomic ownership switch or a small mutex ONLY for the stopped path). For the offline/export domain: determine why the 874B post-play state blob renders silence (potential second bug - state-blob validity in the export domain). Then add a gtest for the seam (Gate G2/G3).

### Artifacts / git status
- probes: probe-c2a/c2a_driver.py (patched: plugin-id regex + state fingerprint), probe-c2a/build_headless.bat, probe-c2a/launch_engine.bat, probe-c2a/launch_engine_detached.bat, probe-c2a/results.json, probe-c2a/traces/* (4 trace files, 17.7 MB total)
- engines were killed after the run (taskkill HDAW_headless/hdaw_plugin_host confirmed)
- all instrumentation left IN PLACE for C2b (logging-only)
## Phase C2b — implementation + bake verification (status: delivery FIXED, bake gap remains)

### Review of C2b diff (accepted, per plan)
- Parent: flush moved off processBlock into flushStagedParams() on the 100ms slot timer
  (PluginProxySlot.cpp:356+, timerCallback:998) — SOLE paramSet-ring writer, transport-independent.
- Child: paramSet ring drained in audioLoop; NEW bounded idle clock (<=1500 zero-input processBlock
  calls @1ms, armed only when params were drained) so CLAP wrappers whose param pipeline is
  audio-thread-driven get clocked; SEH-wrapped like render-mode code; ring/transport untouched.
- Test: PluginIsolation.StagedParamsReachChildWithoutProcessBlock (tests/integration/proxy/
  isolation_integration_test.cpp) — stateecho child, NO processBlock, asserts ring write + child
  GET_PARAM round-trip. PASSES (846ms) along with ParamBridge/ProgramBridgeThroughProxy.

### Verified end-to-end evidence (fresh engine + instrumented CLAP, %TEMP% traces)
- Engine 10456: P1 stageParam idx=17..76 x6 STORE=1 cache=461 -> P3F FLUSHED wrote=6 sw=6 sr=0
  -> subsequent ticks dirty=0 sw=6 sr=6 (child drained)  => parent flush FIXED.
- Child 40304 (proxy): C1 SET idx=17..76 x6, C1 DRAINED calls=6 pr=6 pw=6, C1 IDLE clock
  1500 blocks (~9.4s), no crashes.
- Child 40304 (gearmulator wrapper je8086_trace): L6 receive SYSTEM/PERFTEMP dumps written
  (emu BOOTS under idle clock), L7 getStateInformation destBytes fired x5 (capture requests reach
  the child). L1/Parameter::setValue for indices 17/21/22/59/73/76: ZERO lines -> host params
  NEVER invoke the gearmulator pluginLib::Parameter::setValue.
- Capture (stopped, after bake): status=unchanged / stateBytes=0 -> because params didn't bake;
  state stays default-boot. (L7 fired => the child would return real dumps now that the emu boots,
  but state is default.)
- REFRAME: C2a's playing-run "ok 622B" was the default-patch BOOT dump (params never baked; L1
  dead in those traces too). The correct success signal is capture md5 differing from boot AFTER
  params bake.

### Root cause of the remaining gap
The child's C1-SET applies setValue on its param objects (CLAPPluginInstance/adapter layer), which
does NOT reach the gearmulator plugin's pluginLib::Parameter (L1 dead) => sendToSynth->sysex->
Device::m_state never updates => getStateInformation reflects default patch. The idle clock boots
the emu but cannot bake params that never entered the wrapper pipeline.

### Next unit (C2b-rev)
Fix the child-side param application so host param values invoke the plugin's real JUCE parameter
path (read src/proxy/host/PluginHost.cpp param service + src/engine/CLAPPluginInstance.h: how a
host param set reaches the CLAP; likely CLAP param events into the next process input, or the
params extension with begin/end edit or plain value-set with correct event semantics; verify the
gearmulator wrapper's L1 fires). Then re-run: stopped apply -> bake -> capture != boot -> all
C2c gates (offline render diff, save/load, MatrixPresetsTest + proxy suites, provenance).

### Restore state (after diagnosis)
- Installed CLAP back to ORIGINAL (md5 f4a19cd63f0963a30238829a69fc80dc); instrumented build
  preserved at D:/pdf/clap-backups/2026-09-18/JE8086.clap.pre-c2bdiag (md5 cf279830...).
- Engines killed. Clone reverted (tracked clean; untracked jeTrace.h from patch, jeStateProbe/,
  probe-out/ remain by design).

## Phase C2b — fix (implemented 2026-09-18)
Confidence: gates GB-1..GB-7 fully green (evidence below). Verdict from C2a (only flush site was buzz-guard-gated processBlock) CONFIRMED by build + behavior.

### Design chosen (diff summary)
Parent-side only first, then one child-side extension:
1. **src/proxy/PluginProxySlot.h/.cpp — message-thread timer flush.** Extracted the paramSet-ring flush from processBlock into `flushStagedParams()`; removed the flush (+P3 heartbeat) from processBlock; `timerCallback()` (the slot's existing 100ms message-thread timer) is now the SOLE writer of the shm paramSet ring (SPSC). Ring protocol byte-identical to the original (dirty-scan, (idx<<32)|floatbits packing, ring-full guard leaves dirty, release-store sw only on writes>0). stageParam stays any-thread (atomics). P3E processBlock-entry instrumentation kept; P3/P3F logging moved into flushStagedParams (new `P3F` prefix, heartbeat every 50 ticks).
2. **src/proxy/host/PluginHost.cpp — child idle clock (GB-3 last link).** The child's paramSet drain was ALREADY transport-independent (audioLoop top; verified continuous C1 drains while stopped), so no child drain change was needed. BUT delivery stopped at CLAP setValue: gearmulator wrappers bake host params into getState() only when processBlock clocks them (host param → 25/s message-thread timer → sysex → jeLib Device::process), and with no input the child audioLoop only yielded. Added a bounded zero-input clock in the no-input else-branch: armed by the C1 drain (`if (setCalls > 0) idleClockRequested = true`), cleared on input, budget 1500 blocks at Sleep(1) pacing (~1.5-2.5s per burst; the 128-block first attempt failed — too short for the rate limiter), SEH-guarded exactly like the input branch (idleCrashCount + processBlockActive + sehProcessBlockCrashTranslator), rings/transport untouched.
3. **tests/integration/proxy/isolation_integration_test.cpp — `PluginIsolation.StagedParamsReachChildWithoutProcessBlock`.** `__stateecho__` child → setValue(0.85) → NEVER processBlock → poll: ring write pos > 0 + GET_PARAM round-trip ≈ 0.85 (child applied) + parent cache = 0.85. End-to-end transport-stopped delivery proof (message-pump timer drives the flush in tests).
4. **probe-c2b/c2b_driver.py** — c2a_driver.py copy with a 3.0s bake-settle before the stopped capture (idle clock needs ~1.5-2.5s after the last staged param).

### Gate evidence
- **GB-1 build:** HDAW_headless.exe MD5 3A970F6A2E99B7BD1B39055527A286C1, LastWrite 2026-09-18 13:42:13, 21,584,896 B; hdaw_plugin_host.exe MD5 01960B8B120F02AF07C526812F67DE28, LastWrite 2026-09-18 14:25:11, 14,667,776 B. Binary scan: HEADLESS has P3F FLUSHED/P3F tick=%u, P3E, P1, P2; OLD `P3 pb=%u` GONE (processBlock flush out); HOST has C1 SET/C1 DRAINED/C1 IDLE arm|clock begin|clock done + CRASH (idle clock).
- **GB-2 gtest:** `PluginIsolation.StagedParamsReachChildWithoutProcessBlock` PASS (791-946 ms, standalone + in-suite); PluginIsolation.* 45/45; proxy units (RingBuffer/CrashRecovery/ProxyHealth/RespawnPath/DebugLog/Pipe/SharedMemory) 26/26; combined 71/71.
- **GB-3 stopped (manual, HDAW_TRACE_PARAM=1, engine 21804):** fresh project → JE8086 part → 6 params (17/21/22/59/73/76) → capture WITHOUT ever starting transport: **a_post status=ok stateBytes=874 hasPluginState=1** (a_boot unchanged); save blob md5 6c6ca04537b8b7c85c0fc2d65c0c7094 differs from boot (boot: no pluginState attr). Traces: parent P3F FLUSHED ticks 73/74/75 (sw 0→6) with NO P3E blocks until later; child C1 SET idx=17..76 at +7.1s STOPPED + `C1 IDLE arm calls=2`/`C1 IDLE clock begin blocks=1`; sw=6 sr=6 full drain.
- **GB-4 playing regression:** b_post capture ok (state md5 same blob 6c6ca045…); P3F FLUSHED during play + C1 SET.
- **GB-5 suites:** MatrixPresetsTest.* + FxMidiInjection.* + new gtest = 21 passed / 3 skipped (env-gated OsTIrus/Nord real-plugin tests; HDAW_REAL_PLUGIN_TESTS unset) — LEDGER/replay/deferred-capture seams unaffected (behavior only improved: stopped delivery). Full PluginIsolation + proxy units 71/71.
- **GB-6 diff audit:** only 4 tracked files changed (PluginProxySlot.h/.cpp, PluginHost.cpp, isolation_integration_test.cpp). No MainAudioProcessor/buzz-guard/render/export/MIDI/StateType/ring-protocol changes. paramSetWritePos appears exactly once as a writer (flushStagedParams). Grep-verified.
- **GB-7:** this section.
- **Known out-of-scope (C2c items, unchanged):** OFFLINE/export-domain proxy never receives staged params (its ring stays sw=0 sr=0; the offline render remains silent rms=peak=0.0 — the export gets state only via the tree pluginState blob, and the blob appears stale for offline; two investigation items carried from C2a).

### Instrumentation decision (C2b)
KEPT everything: src/proxy/ParamTrace.h + P1/P2/P3E/P3F (parent) + C1/C1 IDLE (child), env-gated HDAW_TRACE_PARAM=1 (zero behavior impact unset). Rationale: C2c's gates (G2 offline render differs, G3 save→load carries patch, offline-silence investigation) need exactly these gated logs as their verification tool; removal now would be re-verified and re-added next phase. **C2c cleanup gate: at the end of C2c, remove ParamTrace.h + the PARAM_TRACE wiring in PluginProxySlot.cpp/PluginHost.cpp (keep the fix itself) and re-run the proxy suites.**

### Environment note (for future sessions)
The engine gets swapped/relaunched by an external WSL-side process in this environment (~every minute, env-less relaunches). Manual MCP scenario must run launch→verify→drive in ONE fabric round; the C2a bat (launch_engine_detached.bat) propagates HDAW_TRACE_PARAM=1 correctly (Start-Process via ps1 does too); boot-time session restore can race new_project — settle ~25s after launch before driving.

## Phase C2b-rev — child param application fix (IN PROGRESS — notes appended at each milestone)

### Milestone 1: DIAGNOSIS DONE
- The child's C1-SET (PluginHost.cpp:1388) ends in `params[idx]->setValue(value)` on
  CLAPParameter (src/engine/CLAPPluginInstance.cpp:242-257):
  - caches currentPlain; message thread -> plugin paramsExt->flush(plugin,null,null) (WRONG
    DIRECTION + latent null-deref for the gearmulator wrapper whose paramsFlush derefs in);
    off-message-thread -> owner.flushParameter(id, plain) — CACHE-ONLY, nothing reaches the plugin.
  - => pluginLib::Parameter::setValue (wrapper L1) never fires; Device::m_state never receives;
    getStateInformation = boot patch; capture == boot.
- The mechanism the gearmulator wrapper (clap-juce-extensions @2aaad9e) consumes:
  clap-juce-wrapper.cpp:1335 handleParameterChangeEvent(const clap_event_param_value*) ->
  :1365 param.processorParam->setValue(normalized). Invoked from processEvent case
  CLAP_EVENT_PARAM_VALUE (:1733) during process() and from paramsFlush (:1644).
  Value space: CJA_CLAP_USE_JUCE_PARAMETER_RANGES default OFF (no override in gearmulator build)
  => get_info min=0/max=1, get_value = normalized passthrough, event value must be NORMALIZED
  (== CLAPParameter currentPlain since min=0,max=1). cookie=nullptr handled via
  findVariantByParamId fallback (:1341-1349); we use nullptr for cookie lifetime safety.
- FIX DESIGN (smallest): pending param event queue in CLAPPluginInstance; setValue enqueues
  {info.id (REAL clap param id), plain}; processBlock drains it into inEvents as
  clap_event_param_value (time 0, cookie nullptr) before plugin->process(). Idle clock (C2b)
  provides the processBlock calls while stopped. Ring, dirty, SEH, traces, transport untouched.
- Baseline binaries: HDAW_headless 3a970f6a2e99b7bd1b39055527a286c1 (13:42); hdaw_plugin_host
  01960b8b120f02af07c526812f67de28 (14:25); hdaw_tests e12cc677f5382c1c7fb834cece3c9554.
- Installed CLAP ORIGINAL md5 f4a19cd63f0963a30238829a69fc80dc (verified). C2b instrumented
  build preserved: gearmulator-git/bin/plugins/Release/CLAP/JE8086.clap md5 cf2798302fd4eca5881858b5ff9d1be7.
## Phase C2b-rev — child param application fix (COMPLETE — bake verified end-to-end)

### Diagnosis (Milestone 1, subagent + orchestrator verification)
- Child C1-SET param objects (CLAPParameter, src/engine/CLAPPluginInstance.cpp:242-257) only CACHED the
  value (message-thread branch called the wrong-direction paramsExt->flush; off-thread flushParameter was
  cache-only). The gearmulator wrapper consumes host param sets ONLY via CLAP_EVENT_PARAM_VALUE at process
  time (clap-juce-extensions @2aaad9e: clap-juce-wrapper.cpp:1335 handleParameterChangeEvent ->
  :1365 processorParam->setValue; invoked in processEvent case CLAP_EVENT_PARAM_VALUE :1733 / paramsFlush
  :1644). Values are normalized (CJA_CLAP_USE_JUCE_PARAMETER_RANGES default OFF), cookie null -> param-id
  lookup fallback.

### Fixes implemented (HDAW, tracked-source changes)
1. CLAPPluginInstance: CLAPParameter::setValue enqueues {real clap param id, plain} into a bounded
   (512) last-value-wins queue (any thread, mutex); processBlock drains via try-lock into inEvents as
   clap_event_param_value (time 0, space CLAP_CORE_EVENT_SPACE_ID, note_id/port/channel/key -1) before
   plugin->process(). NEW gtest CLAPParamEvents.SetValueDeliversParamEventToPlugin PASSES (id 42, value
   0.7, coalescing, drain semantics).
2. PluginHost child audioLoop: pre-param WARM clock — on the FIRST drained batch, run kIdleWarmBlocks=800
   zero-input processBlock (SEH-wrapped like the idle clock) BEFORE applying the drained values, so the
   emu boots and the wrapper's device->host default sync seeds Parameter::m_lastValue (the gearmulator
   "ignore initial update" guard silently drops the FIRST host change per parameter while m_lastValue==-1;
   every earlier run's trace showed lastValue=-1 + dropped values: value=100 lastValue=-1 for all six).
   Removed the input-path pluginWarmed marking (real input does not guarantee the boot sync completed).
3. PluginProxySlot flushStagedParams on the 100ms timer (C2b, unchanged) + ParamTrace.h instrumentation.

### Verification evidence (fresh engine, instrumented CLAP, %TEMP% traces; session 2026-09-18 late)
- Engine 32692: P1 stageParam idx=17..76 x6 STORE=1 cache=461 -> P3F FLUSHED wrote=6 sw=6 sr=0 -> sr=6
  (child drained; engine poll ticks confirm).
- Child 42644: C1 WARM begin -> WARM done (800 blocks) -> C1 DRAINED -> C1 IDLE arm -> IDLE clock.
- Wrapper je8086_trace_42644.log: L1 sendToSynth value=100 lastValue=64; value=1 lastValue=0; value=90
  lastValue=0; value=10 lastValue=34; value=127 lastValue=30; value=120 lastValue=91  (defaults SEEDED by
  warm = Phase-B-verified defaults; guard passed => values SENT to the synth). L6 receive x48 (dumps +
  param sysex), L7 getStateInformation destBytes=874 dumpCount=7.
- Unit + proxy tests green (16/16 filtered: CLAPParamEvents.*, MatrixPresets*, PluginIsolation
  Param/Program/StagedParams bridges).
- G1 demonstrated once end-to-end via probe-c2b driver: (a) STOPPED boot capture 0B/unchanged ->
  post-apply capture status=ok stateBytes=874 hasPluginState=1; (b)/(c) ok with per-scenario state md5s.
- KNOWN REMAINING: MCP capture-status path intermittently reports unchanged/0B after a successful bake in
  the headless deviceless + load_project flow (capture child attribution / D-lite baseline per-child;
  needs a follow-up: instrument the capture request path + the boot-baseline comparison). Save/load
  (G3) scripted check therefore did not turn green in-session despite the bake evidence; G2 (offline
  render audio diff) remains blocked by the BASELINE offline-render silence (init exports silent with
  ORIGINAL and instrumented CLAP alike — deliverable-3 class, orthogogonal). G5 pending.

### Restore / state
- Installed CLAP = ORIGINAL md5 f4a19cd63f0963a30238829a69fc80dc; instrumented build preserved at
  D:/pdf/clap-backups/2026-09-18/JE8086.clap.pre-c2bdiag and gearmulator-git/bin/.../JE8086.clap.
- Engines killed. Gearmulator clone reverted (tracked clean; jeStateProbe/ + probe-out/ + jeTrace.h
  untracked leftovers by design).
- HDAW deliverable (uncommitted): src/engine/CLAPPluginInstance.{h,cpp}, src/proxy/PluginProxySlot.{h,cpp},
  src/proxy/host/PluginHost.cpp, src/proxy/ParamTrace.h, tests/integration/proxy/isolation_integration_test.cpp,
  tests/unit/engine/clap_param_event_test.cpp, tests/CMakeLists.txt. Recommend commit with a clear message;
  ParamTrace.h/PARAM_TRACE can stay (env-gated, zero behavior when off) or be stripped in a cleanup pass.
