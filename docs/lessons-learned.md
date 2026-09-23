# HDAW lessons learned (full narratives)

Moved out of AGENTS.md (2026-09-22) to keep the agent working set lean.
AGENTS.md carries the one-line rule per lesson; the full narratives,
evidence and rules live here. Lessons 1-27 reference the per-topic docs
(pitfalls-juce.md, realtime-safety.md, valuetree-listener-contract.md,
postmortem-silent-clap-export.md, hardware-va-suite.md) — those stay the
canonical deep-dives; this file is the chronological narrative.

# The lessons

## Lessons learned

These cost real debugging time — read before touching the relevant area:

1. **Beats vs seconds is the #1 data-convention bug source.** Frontend speaks
   beats; the clip ValueTree (`startTime`/`duration`) and the processors speak
   seconds. Every boundary-crossing command must convert. See
   `docs/architecture.md` → "Time-unit convention".
2. **`ValueTree::setProperty` is a silent no-op when the value is unchanged.**
   Any command relying on the listener side-effect (transport
   rewind/stop/play/seek) must drive the manager directly or nudge the value.
   See `docs/pitfalls-juce.md`.
3. **`processBlock` must early-out when the transport is stopped**, or clips at
   the current position replay the same block every callback (audible buzz).
   See `docs/realtime-safety.md`.
4. **The delta-sync path can't compute derived state** (`effectiveMuted`/
   `effectiveSoloed`); mute/solo changes escalate to fullSync. See
   `docs/valuetree-listener-contract.md` §6.
5. **`projectEndSample` (auto-stop) goes stale on SPSC timing edits** because
   those don't rebuild the graph; it's recomputed in `processBlock` when a
   timing param changes. See `docs/realtime-safety.md`.
6. **`rebuildRoutingGraph()` is O(project) per call — incremental path avoids it.**
   The full-rebuild path (`rebuildRoutingGraph`) tears down and re-instantiates
   every clip/plugin. The **incremental path** (default ON, `HDAW_FORCE_INCREMENTAL_ROUTING`)
   uses `RoutingManager::addClip/removeClip/updateClipPlacement` with
   `UpdateKind::none` and one end-of-batch `graph.rebuild()`, keeping per-op
   drain under ~2 s for 128 clips (was ~80 s). For batched multi-clip ops that
   need slicing (ripple delete, insert-silence/duplicate-region), call the
   model-level `ProjectModel::sliceClipAtTimes` directly — it does NOT rebuild —
   and do one `rebuildRoutingGraph()` at the end; the command-layer
   `sliceClipAtTimes` wrapper rebuilds per call, which defeats the coalescing.
7. **Every audio engine change affects latency — evaluate it explicitly.**
   Any modification to `processBlock`, plugin graph topology, bus layout,
   buffer sizes, or signal-path length changes the overall input-to-output
   latency. Before merging an engine change, measure the reported latency
   (`getTotalLatency()`) before and after, and verify that plugin delay
   compensation still aligns all tracks. A regression here causes audible
   phase issues and MIDI timing drift. See `docs/realtime-safety.md`.
8. **Every audio engine change affects quality and fidelity — evaluate it explicitly.**
   Modifications to signal processing (sample rate conversion, timestretch,
   mixing, FX chain, plugin hosting, buffer handling) can introduce artifacts:
   clicks, pops, DC offset, aliasing, clipping, or degraded dynamic range.
   Before merging, A/B test with critical listening on reference material and
   verify no unintended signal degradation. Check for denormalized floats,
   integer overflow in accumulators, and incorrect gain staging. See
   `docs/realtime-safety.md`.
9. **The default project ships ZERO tracks and zero clips (v0.34.0) — tests
   must create every track/clip they use and never assume inherited
   baselines.** `createDefaultProject()` (`ProjectModel.cpp`) builds an empty
   `TRACK_LIST`; the user (or MCP `add_track` /
   `add_instrument_part` / audition keepTrack) creates tracks explicitly.
   (Earlier versions shipped "Track 1"/"Synth"/"Vocals" stub tracks with empty
   `CLIP_LIST`s — and before that, seed `Melody`/`Chords` clips; hardcoded
   baselines silently broke both times.) Tests that need track N must seed
   tracks 0..N explicitly, and any test reading a LIVE processor track
   (`getMainProcessor()->getTrack(i)`) after `addTrack` must call
   `engine.drainPendingRoutingRebuild()` first (the routing projection is
   deferred; lessons 10/12). Never re-add default tracks in production to
   paper over a test failure. Corollary:
   `ProjectModel::sliceClipAtTimes` **reassigns ids** to the pieces (the
   original clip is removed), so across a slice, track clips by
   position/count, not by the original id.

10. **A routing-graph rebuild must restore track state, and projection seams
    need state-preservation tests — not just no-crash smoke tests.** The
    `ValueTree` is the source of truth with **two projections**: the `ReadModel`
    (frontend snapshot) and the audio graph (`RoutingManager`). Both are rebuilt
    wholesale from the tree, but `RoutingManager::addTrack` restored every *clip*
    property while the *track* mixer state (volume/pan/mute) was never restored —
    a fresh `Track` starts at constructor defaults (unity / centre / unmuted). Live
    mute/volume travel the SPSC bridge to the *current* processor, so they work
    until any `rebuildRoutingGraph()` (clip edit, load, tempo change, take switch,
    recording) recreates the processors and silently drops that state: muted tracks
    became audible and tracks played at unity. This was a day-one bug that survived
    because every test asserted the `ReadModel` (always correct — the property really
    is written) and the only rebuild test
    (`AudioGraphSurface.RebuildRoutingGraphDoesNotCrash`) checked merely that it
    doesn't crash. **Rule:** when you add state to a track/clip processor, restore it
    in the rebuild path (`addTrack` uses `Track::restoreMixerState`), and cover the
    seam with a test that mutates state, rebuilds, and asserts the **live processor**
    (`getMainProcessor()->getTrack(idx)`) — see `track_mixer_state_test.cpp`.

11. **Every non-GUI process (headless, tests, offline render) MUST start a JUCE
    message pump before any JUCE construction.** JUCE 8's `AudioProcessorGraph`
    bakes its render sequence asynchronously on the message thread; without a
    pump, `processBlock` takes its `audio.clear()` fallback and every export
    renders **silence**. Worse, the first `MessageManager::getInstance()` caller
    (often the export render thread) wins `messageThreadId` + the hidden window;
    when that thread exits it orphans the queue, and `AudioEngine::shutdown()`'s
    `MessageManagerLock` then waits on an undeliverable `BlockingMessage` →
    **hang forever**. `MessagePumpThread` is started as the first statement of
    `main`/`main_headless`/`test_main`. See `docs/postmortem-silent-clap-export.md`.

12. **`AudioProcessorGraph` is not thread-safe; every graph mutation from a
    non-message thread must park the pump — and a `MessageManagerLock` taken ON
    the message thread self-deadlocks.** The graph's internal `LockingAsyncUpdater`
    dispatches on the pump thread and iterates the **live node list**, concurrent
    with HDAW's own `graph.clear()` + rebuild on the command/MCP/test thread →
    use-after-free of freed nodes. `MainAudioProcessor::rebuildRoutingGraph` is the
    reference: it parks the pump via `MessageManagerLock` for the duration,
    guarded by `!isThisTheMessageThread()` (taking the lock on the message thread
    itself waits for its own dispatch → deadlock). Any new non-message-thread
    graph mutation must follow this pattern. See `docs/postmortem-silent-clap-export.md` §6.

13. **Every DSP-state write from a listener/command must hold `stateLock` — it
    was only safe before the pump because everything ran on one thread.** The
    `valueTreePropertyChanged` FX-slot `param_N` listener wrote `*eq->state`
    while the pump's `Track::prepareToPlay`→`TrackFXSlot::prepare` **recreated
    (and freed)** the EQ DSP under `stateLock` → write-after-free. Fix: the
    stateLock-guarded `Track::setFxSlotInternalParam`. **General rule:** any
    listener or command that touches a processor's DSP objects or vectors (EQ,
    filters, FX chain, automation, modulation) is now a candidate race — guard
    iteration with the `prepareToPlay` `stateLock.tryEnter()` idiom and writes
    with a dedicated lock. See `docs/realtime-safety.md`,
    `docs/valuetree-listener-contract.md`.

14. **Cross-process (isolated-plugin) boundaries silently truncate and race —
    assume fixed-size messages lose data and any cross-thread handle swap races
    the audio thread.** The 256-byte proxy pipe message truncated plugin state to
    244 bytes on every FX rebuild (→ "preset corrupted", persisted into saves);
    `getStateInformation` reading `resp.dataSize` bytes out of a 244-byte buffer
    hung the message thread. Fixes: chunked `STATE_CHUNK` transfer + bounds-check
    `dataSize` on both sides. `migrateToNewSlot` swaps a `shared_ptr` the audio
    thread reads concurrently → must hold `graphLock`. Scratch buffers allocated
    at a constructor default (512) before `PREPARE` set the real block size (441)
    → ~1.16× **pitch-up** — resize on `PREPARE`. Crash-recovery respawn must
    carry `desc.fileOrIdentifier` or the child exits code 1. See
    `docs/realtime-safety.md`, `docs/postmortem-silent-clap-export.md`.

15. **The auto-stop flag and the stale-`.obj` build trap both make "the source
    says X" unreliable.** The audio thread sets `isPlaying=false` +
    `autoStopRequested` immediately, but the ValueTree lags ~50 ms (timer sync);
    a Play pressed in that window was a silent no-op (lesson 2: `setProperty`
    unchanged) then killed by the stale auto-stop — `play()` now consumes the
    pending auto-stop first. Separately: MSBuild skipped recompiling
    `test_main.cpp` because the source was older than its `.obj`, so the linked
    test binary's `main()` never called the pump while the source said it did.
    After editing entry points (`*_main.cpp`) or when a fix "doesn't take,"
    verify the **binary** contains the change (`.obj` timestamps / a breakpoint
    probe), not the source. See `docs/realtime-safety.md`,
    `docs/postmortem-silent-clap-export.md` §4.

16. **CLAP lifecycle calls must run on the host's reported "main thread" —
    the child's pipe/control thread is NOT it, and thread-checking plugins
    (Odin2) `std::terminate` the child if you call them there.** The earlier
    "Odin2 fail-fasts via `noexcept` activate" theory was wrong: Odin2's
    wrapper queries `clap_host_thread_check` during `activate()`/`deactivate()`/
    state calls and aborts when the host answers "not main thread".
    `CLAPHost::threadCheckIsMainThread()` accepts the JUCE message thread OR
    the export render thread (`proxy::isRenderThread()`). Two violations
    existed: (a) the isolated child's `PluginHost::controlLoop` (a pipe
    thread) called `prepareToPlay`/`setStateInformation`/`getStateInformation`
    directly → the child died silently at PREPARE, contained as silence;
    (b) in-process export teardown cleared render mode BEFORE the render
    graph destructor ran `deactivate()` on the worker thread → `abort()`
    (c0000409). Fixes: `PluginHost::runLifecycleOnMessageThread` marshals all
    four child lifecycle calls to the message thread (bounded wait, then the
    existing try/catch + `pluginFailed`); `ExportManager::renderThreadFunc`
    scopes the render graph so its destructor runs while render mode is still
    set. The control-thread try/catch remains as a second line of defense
    (`PluginIsolation.ControlThreadPluginExceptionContained`,
    `__throwprepare__` sentinel). **Rule:** any new control-thread plugin call
    in `PluginHost::controlLoop` must be marshaled via
    `runLifecycleOnMessageThread` AND wrapped the same way; any new
    main-thread-only CLAP call on a render/pipe thread must pass
    `threadCheckIsMainThread()`. This recovered Odin2 (isolated export
    `peak≈0.5`, removed from `kKnownSilent`). See
    `docs/archive/plans/2026-08-09-forward-transport-playhead-to-isolated-children.md`.

17. **Audio-device init must degrade to output-only, and device errors must be
    logged somewhere visible.** `initialiseWithDefaultDevices(2, 2)` fails the
    WHOLE open when no capture device exists (RDP sessions expose a render-only
    "Remote Audio" endpoint; headless/CI boxes may have none) → no device →
    `prepareToPlay` never runs → `routingManager` stays null → every
    `getTrack()`-style consumer nulls. `AudioEngine::initialize()` now retries
    output-only `(0, 2)` via the `initDefaultDevice` lambda (both init sites).
    Also: `juce::Logger::writeToLog` goes to OutputDebugString on Windows —
    NEVER stderr, never `hdaw_debug.log` — so device errors were invisible in
    captured stderr; the new fallback also emits `HDAW_LOG`. **Rule:** engine
    startup must survive zero input devices; when diagnosing "the device never
    started", capture OutputDebugString or breakpoint
    `juce::Logger::writeToLog` (`da poi(@rcx)` under cdb) — stderr captures
    prove nothing.

18. **Never instantiate plugins while the message pump is parked — two-phase
    rebuild.** `rebuildRoutingGraph` parks the pump via `MessageManagerLock`
    (lesson 12), but JUCE plugin instantiation OFF the message thread
    (`AudioPluginFormat::createInstanceFromDescription`) dispatches TO the
    message thread and blocks → the park deadlocks its own dispatch. This was
    invisible while `routingManager` was null (no device); it fires on any
    non-message-thread rebuild with in-process plugins (e.g. MCP `load_project`
    with `HDAW_NO_PLUGIN_ISOLATION=1`). Fix: `RoutingManager::prebuildTracks()`
    builds Track processors + FX plugin instances BEFORE the park (only when
    `needsPark && !isolationEnabled`); `addTrack` adopts prebuilt tracks.
    Isolated mode needs no message thread (child spawn) and keeps the
    single-phase path. **Rule:** any new code added inside the parked section
    of `rebuildRoutingGraph` must not call JUCE plugin/format APIs that hop to
    the message thread; do such work in the pre-park phase.

19. **The CLAP audio thread is the thread running `process()` — nothing else.**
    `CLAPHost::threadCheckIsAudioThread()` used to return
    `!isThisTheMessageThread()`, which classifies the Qt main thread (and any
    other thread) as "audio". Since JUCE's message thread is the pump thread,
    plugin window-proc code on the Qt main thread counted as audio-thread, and
    a legal `clap_params_request_flush` call from there (spec:
    `[thread-safe,!audio-thread]`) tripped clap-helpers
    (`MisbehaviourHandler::Terminate, CheckingLevel::Maximal`) →
    `pluginMisbehaving` → `std::terminate` → `abort()` (rc=3, MSVC dialog spam).
    Fix: `CLAPHost::audioThreadId` recorded in `CLAPPluginInstance::processBlock`;
    the check compares against it. **Rule:** thread-check predicates must report
    real thread identities (recorded ids), never "not X" complements; any new
    CLAP host callback added under `CheckingLevel::Maximal` inherits terminate
    semantics — verify its thread contract against `clap/ext/*.h` `[thread]`
    annotations before wiring it.

20. **Orphaned `hdaw_plugin_host.exe` children from a stale engine block the
    proxy tests — check for live engines before blaming the suite.** The
    "known to fail" five (`CrashRecovery.AutoRespawnAfterCrash`,
    `CrashRecovery.RespawnDuringActiveProcessing`,
    `CrashRecovery.DestroyedProxyIsDeregistered`,
    `CrashRecovery.OfflinePluginDomainIsolatedFromLive`,
    `PluginIsolation.UniqueSlotIdPerInstance`) are NOT flaky code — they fail
    or hang (30s READY wait) because a long-lived `HDAW_headless_mcp.exe`
    (or `HDAW.exe`) from a previous session still runs and its orphaned
    children hold the named pipes/shm for the slots the tests use
    (`\\.\pipe\hdaw_plugin_<n>` / `hdaw_plugin_shm_<n>`, n = 1..N — the
    `PluginManager` slot counter restarts at 1 per instance, so slot 1 is
    guaranteed to collide). Symptoms: spawn fails in ~28ms with "Failed to
    spawn isolated plugin process", or the READY wait times out; the tests
    pass when run alone IF no stale engine is alive, and the failure comes
    and goes as engines start/stop. Diagnosis: `Get-CimInstance
    Win32_Process -Filter "Name='hdaw_plugin_host.exe'"` and look for
    children whose parent (`HDAW_headless_mcp.exe`/`HDAW.exe`) has been
    running for hours/days. **Permanent guard (v0.23.2):** every
    `ProxyProcessManager` instance auto-generates a unique namespace prefix
    (`<pid-hex>_<instance-counter>_`) in its constructor via
    `makeUniqueNamespacePrefix`, so no two managers in a process ever share
    pipe/shm names — even bare-`PluginManager` tests with no explicit prefix.
    Offline/export domains call `setProxyNamespacePrefix("export_")` which
    now appends the unique suffix (`export_<pidhex>_<n>_`), so overlapping
    exports across processes can never collide. `spawnPluginHost` also
    retries with a bumped slot id (up to 8 times) when a name is detected as
    held (pipe create fails with ERROR_PIPE_BUSY, shm create fails with
    ERROR_ALREADY_EXISTS), making the system resilient to any external
    squatter. `KillGraceful` now waits for child termination
    (`WaitForSingleObject`) to ensure same-slot re-spawn never hits a
    lingering mapping from the prior child. **Rule:** before debugging any
    proxy-spawn failure, check for live engines; if the run's slot ids
    could collide with a running engine, either stop it or expect these
     tests to fail.

21. **Render sequence pins old graph after `graph.clear()` until next
    `processBlock` re-bake — load with stopped transport leaked all previous
    plugin children (100% CPU each).** `graph.clear()` removes nodes from the
    live list but the render sequence (baked during playback) still holds
    `Node::Ptr`s → Tracks → FX slots → plugin proxies → child processes. The
    sequence is only re-baked by `graph.processBlock`, which
    `MainAudioProcessor::processBlock` early-outs before when transport is
    stopped. Every loaded project's children leaked, each spinning its audio
    loop at 100% CPU → saturation → `processBlock` hangs (>1 s minidumps) →
    health-flag → infinite ~2 s respawn storm (no global circuit breaker).
    **Fixes:** (a) synchronous `graph.rebuild()` + scratch `processBlock` drive
    at end of full `rebuildRoutingGraph` to close the JUCE
    `RenderSequenceExchange` handshake (outside `graphLock`, outside pump-park);
    (b) global sliding-window respawn budget (8/30 s default, env-overridable);
    (c) respawn path re-resolution for stale identifier strings; (d)
    flag-reason logging in `checkAllChildren`.
    **Second follow-up (respawn-storm termination):** the "persisting storm"
    after fixes a–d was a **pre-fix orphan engine interleaving in the shared
    `%TEMP%\hdaw_debug.log`** — log lines now carry `"date"` + `"pid"`; always
    attribute a line to a pid before concluding a fix failed. Storm-termination
    contract: crash flags in `checkAllChildren` fire **once per child death**
    (`ChildInfo::crashNotified`, reset on successful respawn);
    `onSlotCrashed` preserves the attempt ladder for existing entries (a
    re-flag can no longer reset `attemptCount`, so `kMaxAttempts` give-up
    terminates the slot); `respawnIsolatedSlot` **cancels the recovery entry**
    when the proxy is gone (logs + `crashRecovery->cancel` instead of
    retrying silently forever); `mcp-launch.bat` kills stale engine/plugin-host
    holders before copying and verifies the copied size — a lingering engine
    held the target exe locked, `copy /Y` failed silently, and every
    bat-launched MCP session reused the pre-fix binary (the stale-`.obj` trap
    of lesson 15, in script form).

22. **JUCE 8's Windows WASAPI never calls `CoInitialize` itself — the host
    thread must initialize COM, or the WASAPI scan silently returns empty
    (cached forever) and `AudioDeviceManager` falls back to DirectSound:
    "only DirectSound devices selectable" + choppy/stuttering audio.**
    JUCE's `ComSmartPtr::CoCreateInstance` jasserts `hr != CO_E_NOTINITIALIZED`
    with the comment "trying to call from a thread which hasn't been
    initialised with CoInitialize()" (`juce_ComSmartPtr_windows.h:133`); the
    WASAPI device type contains **zero** `CoInitialize` calls, and the failed
    first scan is cached (`hasScanned = true` → empty `devices` list for the
    process lifetime). HDAW's `QApplication`→`QCoreApplication` refactor
    (dd76505) removed Qt's `OleInitialize` on the main thread, and the engine
    never added its own — so `initialiseWithDefaultDevices(2,2)` fell through
    to DirectSound (emulated, ~58 ms latency, jittery callbacks → audible
    stutter) while `getDeviceTypes()` still advertised the WASAPI types.
    **Fixes:** (a) `HDAW::ScopedComInit` (RAII `CoInitializeEx`,
    `COINIT_MULTITHREADED`, `src/common/ScopedComInit.h`) is the first
    statement of `main`/`main_headless`/`test_main` — it covers both the
    startup default-init AND the `audio.*` RPCs, which `QWebSocketServer`
    dispatches on the main Qt event loop; (b) the saved-device restore in
    `AudioEngine::initialize` now switches the driver type FIRST, re-fetches
    the setup after, and applies a saved device name only when it exists in
    the new type's device list — the pre-fix order captured the setup under
    DirectSound ("Primary Sound Driver"), applied those names under WASAPI,
    got "No such device: Primary Sound Driver", and re-fell to DirectSound.
    **Rule:** any new Windows entry point that touches `AudioDeviceManager`
    (or any JUCE COM path) must construct `ScopedComInit` before anything
    else; when diagnosing "only DirectSound devices show up", suspect COM
    state before blaming the device manager. Note: in an RDP session the
    WASAPI endpoint set is session-scoped (render-only "Remote Audio"),
    which is correct behavior, not a regression. See `docs/pitfalls-juce.md`.

23. **Internal FX params reach the DSP unclamped — one out-of-range value
    silenced every export at exactly 0.6s.** A saved project carried reverb
    `param_0` (Room Size, valid [0,1]) = **900.0**; `TrackFXSlot` pushed it
    raw into `juce::dsp::Reverb` (Freeverb) → comb feedback ≈ `0.7 +
    0.28×900` ≈ 252 → the export render diverged exponentially (RMS 0.02 →
    1098 → 7e13 → `inf` → `NaN` in 0.6s of audio) and the WAV writer wrote
    NaN as **zeros**: correct-length exports, healthy audio, then hard
    silence "regardless of content" (the runaway track poisons the master
    sum). Three unclamped sites: `prepare()`, `loadParamsFromTree()`,
    `setInternalParam()`; fixed (v0.25.1) with defs-driven
    `clampToParamDef()` at all three plus a write-side clamp in
    `AudioEngineCommands::setFxSlotParam` before the `param_N` property
    write (covers RPC + MCP, which share the command layer). Diagnostic
    signature: export cuts at an EXACT sample mid-block with full-scale
    clipping right before the cut → check the per-block RMS trace for
    `inf`/`NaN` before blaming bounds checks or transport. **Rules:** (a)
    any value reaching recursive DSP (comb/feedback networks) must be
    clamped to its param def at EVERY entry point — one unclamped path
    poisons saved projects; (b) a root-cause narrative written without
    rebuilding + reproducing is speculation — this bug's first "root
    cause" (clip bounds check) was disproven by the project file alone
    (no 0.6s clips existed; 301s clips died at 0.6s too). See
    `docs/pitfalls-juce.md`, `docs/handoffs/2026-09-17-export-silence-investigation.md`.

24. **Audition/session states persist through autosave — verify the SAVED
project state before blaming the engine for a broken render.** Stem-audition
mute/solo/fader states (and imported-clip source offsets) are written into the
`.hdaw` by autosave; a later "broken" render was actually a project whose
musical source tracks were all muted while one saturated layer was audible —
RMS pinned at the masterGain ceiling, then digital silence, looking exactly
like an overdriven blast + static + silence. Before diagnosing export
silence/distortion, read the persisted track mute/volume/FX-bypass state and
each clip's `offset`/`sourceDuration` from the project file. Related: RAVE
imports must set `timelineAligned:true` (or `sourceOffsetBeats`) for
full-timeline rendered stems — offset 0 plays the stem's silent first segment.
See `docs/handoffs/2026-09-09-rave-virus-engine-bugs.md` (Resolution).

25. **A silent render makes every A/B comparison equal — prove audibility, then
    compare.** The Osirus (Virus C) slot rendered exact silence for an entire
    investigation (`rms == 0`, sometimes `3.09e-06` float dust) because the
    emulator booted from an all-zeros edit buffer: `virusLib`'s
    `createDefaultState()` dumps the value-initialized `m_singleEditBuffer{}`
    (512 zero bytes) into the OS edit buffer, so oscillator levels, envelopes and
    channel volume all sat at 0. Every "X does not change the render" result
    measured against that slot was 0-vs-0 — including documented finding **F-A**
    (`load_virus_preset` "queues but does not change renders"), which was a
    **false conclusion manufactured by the silence**; the same trap invalidated a
    "parameter awakening" experiment (the param cache moved 0 -> 1, the render
    could not). Fix (gearmulator `virusLib/device.cpp`): load ROM factory patch
    A-0 into the edit buffer at boot, guarded by `if (!m_rom.isTIFamily())`;
    Osirus went `0` -> `rms 0.047` (gate
    `FxMidiInjection.OsirusBootPatchAwakening`). Cross-lib audit: `n2xLib` builds
    real defaults (`State::createDefaultSingle`), `xtLib`/`mqLib` keep no zeroed
    edit buffer — virusLib was the only offender. **Rules:** (a) before
    concluding "this input does not affect the output", assert the baseline
    output is non-silent (`rms > 0`) — silence masks every delta; (b) when a
    device produces nothing, inspect the **boot patch / edit buffer** before
    blaming DSP timing, the OS, or the host wrapper; (c) never value-initialize a
    patch buffer that is dumped to a device verbatim — seed it from a real patch.
    F-A was subsequently RESOLVED (2026-09-20) — see lesson 26 for the decisive
    fix (`docs/hardware-va-suite.md` §9 CORRECTION).

26. **Isolated-child plugin state must not travel over the control pipe — the
    child's control thread blocks during the OS warmup.** F-A's last blocker:
    `PluginProxySlot::setStateInformation` chunked the state into ~140 (34 KB) to
    ~600 (146 KB) pipe messages with a 3 s bounded send. The child's control
    thread — the pipe reader — was blocked by the **12 s real-time-paced Virus OS
    warmup** (`PluginHost` PREPARE handler: `virus warmup: 1200 blocks`) and
    starved by the CPU-bound offline render, so `sendStateInternal` timed out,
    the retry worker died with the render domain, and the restored state never
    reached the plugin — the render silently played the boot patch. Signature:
    the parent logs `SET_STATE … bytes=N` with **neither** `verified` nor
    `verify mismatch` (the early-return branch is unlogged), and the wrapper's
    `setState`/`loadChunkData` never run. **Fix:** the `stateSet` SHM ring
    (`STATE_RING_SIZE` 1 MiB, `SHM_MAGIC` bumped) — the parent publishes
    `[uint32 size][bytes]` lock-free and the child applies it from its **audio
    loop**, marshaled to its message thread (lesson 16), deferred while
    `warmupActive`. Delivery went from timing out to ~30 ms. **Rules:** (a) never
    move bulk state (or anything the child must apply promptly) over the control
    pipe — the control thread is not guaranteed to be reading (warmups, heavy
    processBlock, lifecycle marshals); use an SHM ring like `paramSet`; (b) log
    the FAILURE branch of every bounded send — a silent early return is
    indistinguishable from success in the logs; (c) verify a state transfer
    against the CHILD's reported state, never assume it. Wrapper trap found
    alongside: `setCurrentPartPreset()` ends with `requestSingle(EditBuffer)`,
    which makes the host push its stale cached edit buffer over a just-selected
    ROM program — re-assert selections with the selection-only path.

27. **Every audit render is an export of a TREE COPY into a FRESH child — a
    live-only write is never part of its input, and a parent-local param-cache
    readback is NOT evidence that the child applied anything.** Both halves of this
    produced false conclusions on 2026-09-21 (see
    `docs/plans/2026-09-21-plugin-param-persistence.md`): (a) "Vavra host params do
    not move the render, so the live write path is broken" — every render
    (`audition_plugin`, `verify_part`, `export_audio`) is
    `renderTrackWindow` → `ExportManager::startExport` on a **copy** of the tree in
    a **fresh child**, so a write that only reached the live child was never in
    that child's input; the trace shows it arriving and applying
    (`P1 stageParam` → `P3F FLUSHED` → `C1 SET` → `C1 DRAINED`); (b) the old
    "`|Δ|>1e-5` proves the param is audible" gates — `ProxiedParameter::setValue`
    calls `setCache()` **before** `stageParam()`, so a
    `PluginParamService::getParams` readback is a parent-local echo of the host's
    own write and proves nothing about the child. The fixed contracts: plugin param
    writes are **durable** via `IDs::appliedParamOverrides` (replayed into every
    fresh child by `ExportManager::replayAppliedParamOverrides`); a live-**only**
    write is visible through the opt-in `liveParamState` probe
    (`AuditionParams::liveParamState` / `AuditionResult::usedLiveParamState`,
    default OFF so the default probe keeps matching `export_audio`);
    `clear_fx_param_overrides` drops the ledger; `appliedParamOverrides` writes
    trigger **no** graph rebuild (the FX_SLOT listener early-returns) — keep it so.
    **Rules:** (a) never judge a live-only write against a tree-derived render;
    (b) never present a render A/B as audibility proof unless the separation beats
    the harness's own variance — measured same-input spreads are 4.1e-07 (Vavra) up
    to 0.0056 on ~0.05 RMS (Xenia), and one Xenia render moved ~17% between runs, so
    only multi-x separations count (NodalRed2x 3.1-8.2x, Vavra ledger 2.4x, Vavra
    live probe 8.4x); (c) when a gate's claim and its assertion disagree, fix the
    claim — four headers here had drifted into asserting what their bodies could not
    measure.

28. **Bare plugin identifiers ('Vavra.clap') must resolve against the scan
    database — a .clap/.vst3 suffix is NOT a path.** `resolveIdentifierToPath`
    skipped knownList resolution for any identifier ending in .clap/.vst3
    (reading the suffix as "already a real path"), so the MCP `add_fx` callers'
    bare names reached the isolated child unresolved; the child's
    `findAllTypesForFile` resolved against its own cwd, found nothing, and
    silently fell back to a passthrough: 0 params, 12-byte stub state, silence —
    while `fileMightContainThisPluginType` (extension check) and READY still
    reported healthy. Fixed (2026-09-22): bare names resolve by identifier,
    filename-tail, then format+name against the scan DB;
    `PluginHost::loadPluginByPath` logs path/format/mightContain/typeCount/error
    at every step (the failure branch was completely silent — lesson 26b again).
    **Rule:** agent-facing pluginId arguments are bare names; every surface that
    spawns children must resolve them, and every load failure must log WHY.
    Tests that pass absolute paths hide this bug — regression
    `PluginPathResolution.*` uses bare names.

29. **Check the process exit code BEFORE debugging a crash: 0x2A (42) is the
    intentional `engine_restart` exit; the MCP wrapper separately restarts the
    engine on its call timeout (lazy-mcp `requestTimeout`, default 10 s, and the
    documented override was found ABSENT from the live config on 2026-09-22).**
    A whole day of "engine crashed and respawned with an empty project" incidents
    was actually the wrapper timing out on long calls (batched fills, library
    scans, auditions > 10 s) and restarting the engine — wiping unsaved state.
    Only ONE event was a real crash (heap corruption C0000374, dumped).
    **Corrected 2026-09-22:** 42 is NOT the timeout signature — the only path that
    exits 42 is the `engine_restart` tool (`McpTools_Engine.cpp:137`;
    `main_headless`/`main` return `app.exec()`, and `mcp-launch.bat` has no 42
    branch). A timeout shows up as a discarded connection plus a relaunch (exit
    0/1 once the engine is killed or hits stdin EOF); reading 42 as "the wrapper
    killed me" misattributes an explicit restart. **Rules:** (a) read procdump's "Process Exit"
    line first: 42 = a caller invoked `engine_restart`; 0/1 after a long call =
    timeout/EOF (state loss, not a crash); (b) keep
    mutating calls under the wrapper timeout — batch small, checkpoint-save
    immediately, and never blind-retry a timed-out call (the first is still
    running engine-side); (c) arm WER LocalDumps (full, engine + plugin host —
    `scripts/crash-diag.ps1 wer-on`) so fail-fast/abort paths procdump can miss
    still land dumps; procdump `-k` (kill after dump) is now on so a
    heap-corrupted process cannot keep serving sessions (lesson 21's respawn
    storm analysis applies to it); (d) `scripts/crash-diag.ps1 report` gives
    exit codes + dump inventory per capture dir.

30. **Batch tree surgery at the LIST level, not the child level.**
    `clearNotes` used `removeAllChildren(&um)` on a 2300-note clip: JUCE fires
    the ValueTree listener PER CHILD, so each removal ran
    `rebuildMidiClipCache` + ghost-scan + an undo record — and the engine died
    mid-loop. Fixed (2026-09-22): swap the whole MIDI_NOTE_LIST node (one
    listener event, one undo record). **Rule:** any bulk child removal under a
    listener-heavy parent must replace the container node, not iterate
    removeChild.

31. **Patch selection needs a variety mechanism — deterministic similarity
    ranking always returns the same top hit.** `related_samples` is a pure
    ranking; agents that take the top result pick the same patch every session.
    `select_patch` (FileLibraryManager::selectPatch) closes the loop:
    cluster-stratified (role-filtered clusters), seeded RNG, per-role
    recently-used ledger (32-entry ring, auto-cycle reset on pool exhaustion).
    Corpus quality: patch sidecars need rendered-probe dsp vectors (20 keys,
    `kDspFeatureKeys`) for timbre clustering — text/filename clustering is the
    fallback; `sweep_dx7_patches.py --engine vavra_plugin` sweeps a corpus
    through the isolated child and writes them (capture-wait after apply_preset
    REQUIRED: the state capture is deferred ~800 ms and an immediate export
    races it, rendering the init patch for every patch).

