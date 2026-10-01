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
    fix (`docs/va-suite-status-log.md` CORRECTION — formerly `hardware-va-suite.md` §9).

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

32. **An enabled Volume automation lane owns the parameter in the offline
    render — audit isolation must use mute, not `setTrackVolume`.** The enabled
    paramID-1 `Volume` lane rewrites the parameter every block in the offline
    render (`src/engine/Track.cpp:557-561` playback branch;
    `src/engine/AutomationManager.h:59-77` returns `points.front().second`
    before a lane's first point), so a static fader write (`setTrackVolume` /
    `IDs::volume`) is silently overridden — the 2026-09-24 "exports 3+ ignore
    live tree changes" report was exactly this (the audit soloed with the fader
    on tracks carrying a Volume lane; only `kick`, which had no lane, isolated).
    **Rule:** isolate with mute (`setTrackMuted`) or disable the lane on the
    offline copy. Pinned by
    `ExportVolumeBypass.VolumeAutomationOverridesTreeFader` /
    `ExportVolumeBypass.MultiExportRereadsLiveTree`.
33. **A per-slot loop must not clear a SHARED buffer — and property-only health
    reads (`hasSound`) can mask a no-sound state.** In an FX chain with two or
    more `sampler` slots, every sampler slot called `buffer.clear()` on the
    shared chain buffer before `SamplerEngine::render()` (which is itself
    clear-then-voice-add), so each sampler slot erased all earlier slots' audio
    — only the LAST sampler slot survived. A later sampler with zero in-range
    notes wiped the chain to exact silence. Discovered via the psy-song-session
    vector-bloom build: a 2-slot hats chain (closed KR 42-45, open KR 46-49)
    with only pitch-42 notes rendered silent. Fix (2026-09-26,
    `src/engine/TrackFXSlot.h` + `src/engine/Track.cpp`): chain-level
    accumulate — the FIRST engaged sampler keeps the legacy replace path
    verbatim (single-sampler chains byte-identical), every LATER engaged sampler
    preserves the running sum across `render()`'s clear (PREPARE-time scratch
    `samplerPreserve_`) and adds its voices on top in chain order; bypassed
    slots early-return as before. Zero new realtime-unsafe ops
    (`copyFrom`/`addFrom` arithmetic only). Risk check (hdaw-guard graph gate):
    TrackFXSlot is God Node #5 (165 edges, community TrackFXSlot) and Track is
    God Node #9 (136, community Track) — both directly modified; blast confined
    to the sampler branch + the chain loop (other ActiveTypes byte-identical;
    export and live share the same `Track::processBlock`). Secondary finding,
    fixed the same day: `sampler_get_state.hasSound` was property-only
    (non-empty sampleFile string), so a slot with no decoded sound read "has
    sound". Now `hasSound` is the LIVE `currentSound()` check and
    `hasSampleFile` the property-only companion, emitted by the ONE shared
    shaper `src/common/SamplerStateJson.h` on the MCP tool and both RPC
    routes (`GuiFuncTest.SamplerGetStateHasSoundIsLiveNotPropertyOnly`,
    `FrontendServer.SamplerGetStateLiveHasSoundPlusHasSampleFile`). **Rules:** (a) when a per-slot loop shares one buffer, the first
    contributor replaces and later ones accumulate — never let a slot clear a
    buffer it does not own; (b) a health read taken from a property alone can
    mask the real state. Pinned by `MultiSamplerChain.*`
    (`tests/unit/engine/sampler_key_range_test.cpp`) — 4 deterministic cases
    through the real `Track::processBlock` chain loop (first-slot-only audible,
    last-slot-only audible, both sum in chain order, none silent); full sampler
    batch 40/40
    (`MultiSamplerChain.*:SamplerKeyRange.*:SamplerFxSlot.*:SamplerEngine.*:SamplerVoice.*:SamplerSound.*:AudioPoolDedup.*`).
34. **An accepted-arg-dropped key is a silent no-op — every parse shape must
    parse every key.** `automation_preset` accepts two request shapes
    (top-level window, or `sections[]`). The sections parse read
    `startValue`/`endValue` (with top-level fallback) but never
    `cycles`/`midPoint` — neither per-section nor as fallback — so a
    sections-form sine `{cycles:6}` silently became the len/4 default (24
    cycles over the 96-beat breakdown) and the pad-filter section collapsed to
    near-silence (−88.6% RMS; the earlier `cycles:4` attempt via the
    top-level form worked, which pointed the hunt at a generator that was in
    fact healthy — simulation proved the pure math spans 0..1 for every cycle
    count; the drop lived in the parse). Fix (2026-09-26,
    `src/common/AutomationPresetRequest.h`): per-section cycles/midPoint +
    the same top-level fallback startValue/endValue already had; tool schema +
    description updated. Pinned by
    `AddFxParityTest.SectionsFormCyclesReachThePlanOnBothSurfaces` (exactly 6
    rising 0.9-crossings on BOTH surfaces) and
    `Automation.SinePresetCyclesSpanAllCycleCounts` (cycles 1..8 → exactly
    `cycles` crossings, full span). **Rules:** (a) a request with multiple
    shapes must parse every key in every shape — test one key per shape; (b) a
    success payload (`pointsAdded`) cannot prove an argument landed — assert
    the OBSERVABLE effect (here, the written lane's oscillation count); (c)
    the same silent-loss class hollowed `import_pattern` (three hand-rolled
    preset-serialization copies; PatternPreset carried only the envelope, so
    notes/role/descriptor were dropped) — fixed the same day with a verbatim
    `extraJson` passthrough + ONE builder for save/import/export
    (`PatternLibraryTest.ImportExportRoundTripsPayload`); the pattern load
    paths now share `src/common/PatternPresetJson.h` (the RPC copy had also
    lost category/author/createdAt).

35. **A Windows macro can collide with a JUCE enum name — include ORDER is part
    of the contract.** `rpcndr.h` (pulled by Qt's Windows headers) defines
    `small` as `char`, while `juce_PushNotifications.h` declares
    `enum BadgeIconType { none, small, large }`. In a `src/common` translation
    unit that included Qt FIRST and then a header dragging in engine/JUCE
    headers, the JUCE declaration expanded to `enum BadgeIconType { none, char,
    large }` and the build died — the error pointed at the JUCE header, not at
    the Qt include that poisoned the macro. This is the same family as the
    `slots`/`signals`/`emit`/`foreach` Qt-keyword mangles
    (`docs/pitfalls-juce.md`) — a macro leaking into a third-party identifier.
    **Fix pattern (the reference is `src/common/BatchEnd.{h,cpp}`):** keep the
    shared header Qt-LIGHT and JUCE-FREE (it exposes only the composition entry
    point + a Qt payload struct), and put the heavy JUCE/engine includes in the
    `.cpp` FIRST. **Rule:** when a new `src/common` piece must host both Qt and
    JUCE/engine types, split it: light header + heavy `.cpp`, includes ordered
    JUCE/engine before Qt. See `docs/pitfalls-juce.md`.

36. **JUCE undo boundaries are often deliberately UNPAIRED — batch atomicity
    must be a FLAG plus a choke point, never a depth counter.**
    `beginNewTransaction`/`endTransaction` are not always a matched pair:
    `createBus`/`createSend` are written so the send JOINS the bus's unit, and
    other commands open-and-close around a helper. A naive "depth++ on begin,
    depth-- on end" batch would therefore never return to zero and would leak
    (the batch never closes; every later write silently joins it). The safe
    design: a boolean `batchActive_` + ONE boundary choke point,
    `AudioEngineCommands::transactionBoundary`
    (`src/engine/AudioEngineCommands_Undo.cpp`), which the command layer's own
    begin/end pair AND every internal command boundary route through — while a
    batch is open the boundary is a no-op, so all the batch's writes land in
    one named undo unit. **Rules:** (a) never model batch state as a nesting
    depth when the boundaries you are collapsing are not guaranteed
    balanced — use a flag; (b) route EVERY undo boundary through the one choke
    point (an audit of all `beginNewTransaction` call sites is required);
    (c) keep the no-batch behaviour byte-identical
    (`BatchEditRpcTest.NoBatchBehaviourIsUnchanged`), and prove the collapse
    with an internally-transactional command
    (`BatchEditRpcTest.BatchCollapsesInternallyTransactionalCommandsIntoOneUndo`).

37. **A windowed render does not predict the full render — measure the window
    OUT of a full render, then promote its stats before gating.** Plugin state
    re-bakes at each window boundary, so a render of `[start,end)` can report
    "0 clamps" for a file whose full render carries exact-FS frames
    (`docs/handoffs/2026-09-28-v0.39.2-backlog-closeout.md` §3: 0 in-window vs
    32 in the full render). A window-only verification would have reported a
    false pass for the exact metric the loop exists to check. Two rules fall
    out: (a) **render the WHOLE project** (`0 .. calculateProjectDuration`
    through the shared export launcher, `src/common/RenderLaunch.h`) and
    measure only the requested window; (b) **promote the window's own metrics
    to the payload root** — `buildMixReportPayload` puts whole-file metrics at
    the root, so gating a window requires
    `buildWindowReportPayload` (`src/common/MixReportJson.cpp`), which runs the
    SAME analyzer over the window and lifts `duration/peak/rms/bands/
    kickProminence/ceilingHitPct/ceilingHitFrames` to the root. Verified live:
    window beats 0→4 gates on the window's `ceilingHitPct` 72.75 while
    `mix_report` on the same file reports 16.17.

38. **The tool boundary has a silent-acceptance class — refuse unknown keys and
    non-integral numbers with SHARED bytes.** Three sub-classes surfaced while
    hardening the mechanization surface, all "accepted but wrong": (a)
    `requireInt`/`optInt` TRUNCATED a non-integral id (1.5 → note 1), silently
    aiming the write at the wrong entity; (b) `verify_window`'s expectation
    object silently IGNORED an unknown key, so a typo'd gate never ran; (c) the
    parity ledger ALIASED `begin_batch` to `project.beginTransaction`, an
    overstatement that read as verified equivalence. **Fix pattern:** ONE shared
    parser per request, called by BOTH surfaces, whose refusal text is the
    validator's own bytes — `src/common/BatchEditJson.h` (integer-array ids),
    `src/common/RenderToolArgs.h` (strict expectation keys, refused BEFORE any
    render), and real RPC entry points (`ProjectCommands::beginBatch`/`endBatch`)
    instead of an alias. **Rules:** (a) a request with multiple shapes/keys must
    refuse the ones it does not understand, not ignore them; (b) an argument
    that must be an integer is refused when non-integral, with no mutation and
    no undo unit; (c) a ledger row may only claim `mapped` when both surfaces
    call the SAME entry point — alias rows overstate equivalence.

39. **A `this`-capturing worker must be stopped and JOINED before the destructor
    tears anything down.** Symptom: an intermittent `0xC0000005` minutes into a
    long plugin-heavy test (`PsytranceComposition.PsyDubFiveMinutes`), sometimes
    with no gtest output at all, and a dead test shard. CDB pinned it to
    `proxy::PluginProxySlot::getStateInformation+0x9c` re-reading `this->slotId`
    (`mov r15,rcx` at entry saves `this`; `mov edx,[r15+1A0h]` faults) — the slot
    object had already been freed. Root cause: `startStateRetryWorker` launches a
    `std::jthread` capturing `[this]` that sleeps up to ~31 s and then calls
    `publishStateToRing` / `sendStateInternal` / `verifyStateApplied` →
    `getStateInformation` (3 × multi-second bounded pipe attempts), while
    `~PluginProxySlot` neither stopped nor joined it: the destructor BODY killed
    the child, released resources and dropped the shm handle, and
    `std::jthread`'s implicit join only runs during MEMBER destruction — after
    that body — with members declared after the thread (`crashed`, `childAlive`,
    …) destroyed before it. A `detach()`ed editor watcher captured `this` the
    same way. **Rules:** (a) the destructor stops the JUCE timer, sets a
    `stopping_` flag, `request_stop()`s and JOINS every `this`-capturing worker
    FIRST — before any child/resource teardown; (b) NEVER `detach()` a
    `this`-capturing thread — join it (a plain `std::thread` terminates the
    process if it is still joinable at member destruction); (c) take the stop
    token as a lambda PARAMETER, never re-read the member `jthread` from inside
    its own thread; (d) make the worker's I/O stop-aware so the join is bounded
    by ONE in-flight bounded operation (~100 ms in the normal case) rather than
    the whole retry budget; (e) declare the flags BEFORE the threads and the
    threads LAST, so reverse destruction kills the threads first and the flags
    they read last. Pinned deterministically by
    `PluginIsolation.DestroyWhileStateRetryWorkerRuns` /
    `DestroyWhileEditorWatcherRuns` (`tests/integration/proxy/`), because the
    wild failure is intermittent.

40. **Shared ownership guarantees the OBJECT, never the HANDLE — give a resource exactly
    one closer, and cancel instead of closing against in-flight I/O.** The proxy pipe had
    two independent hazards behind one symptom class: `getPipe` handed out a RAW
    `PipeServer*` owned by an erasable `ChildInfo` (so a kill could free it under the
    slot's own retry worker / editor watcher — fixed with `shared_ptr` leases), and
    `PipeServer::stop()` closed the handle with no synchronization while bounded
    `overlappedRead/Write` re-read the member handle after multi-second waits (fixed by:
    `hPipe` atomic and loaded ONCE into a local per operation, `stop()` = `stopped_` flag +
    `CancelIoEx` only, and `~PipeServer` as the sole closer — which now runs at the LAST
    lease release, so by construction nothing is in flight against a closed handle).
    **Rules:** (a) a lease is required for the whole exchange, but it does not keep a
    handle valid — only the single closer's lifetime does; (b) `stop()` must NOT clear or
    close the handle (clearing it leaks: the destructor's exchange then sees INVALID;
    closing it double-closes / races the I/O); (c) make shared flags atomic and gate every
    I/O entry on the stop flag; (d) prove it with a deterministic concurrent test
    (`StopRacesInFlightBoundedRead` via `__slowstate__`) and a handle-count leak gate
    (`GetProcessHandleCount` over N spawn→kill cycles) — a leak shows as a steady climb.

41. **An exchange lock fixes CONCURRENCY, not STALENESS.** One proxy pipe carries several
    concurrent users per slot (message-thread timer `pollProgramCount`, UI editor calls, the
    background state-retry worker, the editor watcher) and `PipeServer` had no exchange-level
    serialization, so `A-send, B-send, A-receive` could consume each other's replies. Fixed with
    ONE `Exchange` guard spanning each full transaction (all sends + all replies, `STATE_CHUNK`
    continuations included), `try_lock`-per-iteration for the unrequested-await loop, a one-way
    path for heartbeats, and a documented lock order (take the LEASE first; never call back into
    `ProxyProcessManager` while holding the guard; `stop()` never takes it; never block a
    periodic/message-thread path on it — `try_lock` + skip).
    **What that does NOT fix:** a bounded receive that times out leaves its reply QUEUED (by
    design — "a late response is still consumable"), and `ProxyMessage`/`ProxyResponse` are
    exactly 256 bytes with NO correlation id and NO end-of-response marker (a `GET_STATE` answer
    is a header plus N chunks whose count lives in the header). So after a timeout the number of
    queued messages is unbounded, and a stale reply of the SAME type is indistinguishable from the
    fresh one. Type-matching + a `desynced_` flag + logged discards (plus routing an unsolicited
    `EDITOR_CLOSED` to its callback) is best-effort, NOT a guarantee. Real fixes: (i) add a
    correlation id echoed by the child (shrinks `data[244]`→`[240]`, touches every message path and
    the chunk math) or (ii) make a timeout fatal/desyncing and restart the connection before the
    next exchange. Choose one deliberately; do not ship a drain window as "fixed".
    **(LANDED the same day: option (i)** — `requestId` in both structs (`data[244]`→`[240]`, plus
    `static_assert(sizeof(...)==256)`), allocated PER REQUEST in `Exchange::sendRequest`, stamped
    into every chunk by `sendContinuation`, echoed by the child through ONE `sendResponse` helper
    (all 24 sites), matched by id in `receiveReplyImpl` (id 0 = unsolicited, routed only for
    `EDITOR_CLOSED`), with `kProtocolVersion = 2` advertised in READY plus a legacy-v1 READY
    signature guard so a stale child fails IMMEDIATELY with a named diagnosis instead of a bare
    READY timeout. The expected-type heuristic is superseded: a mismatched id is now a CORRECT
    discard. The shrink found and fixed a real OOB (`pipe_test.cpp` looping 244 over a 240-byte
    array). Pinned by `LateSameTypeReplyIsDiscarded`, `SlowChildLateReplyIsDiscardedById`,
    `ProxyProtocol.VersionGateRejectsStaleChild` and the pipe's legacy-READY fixture.)

42. **Two-process protocol: a version guard must be able to DECODE the reply it guards** (diagnosed
    2026-09-29). The READY handshake version check (`READY.result` vs `kProtocolVersion`) only works
    on a reply the CURRENT framing can parse. A genuine stale child is built against the OLD framing,
    so its READY arrives in the old layout (`{type, result=1, dataSize=0, data[244]}`) and the new
    parent decodes it as `{requestId=1, result=0, dataSize=0}` — an id no requestless await owns, so
    correlation-id matching DISCARDS it and the spawn dies by BARE TIMEOUT with no hint of the cause.
    Fix: recognise that exact misparse (`isLegacyV1ReadyReply`) in the requestless READY await, flag
    the pipe, and have `spawnPluginHost` fail IMMEDIATELY (`PROTOCOL VERSION MISMATCH: stale v1
    hdaw_plugin_host.exe (rebuild with dsh-build-fast.bat all)`, terminate + close + return false).
    Pinned by an in-process pipe fixture that writes the RAW v1 bytes (`LegacyV1ReadyIsReportedNotTimedOut`)
    plus its negative control (`CurrentV2ReadyIsNotFlaggedAsLegacy`), and verified end-to-end by
    swapping a stub v1 child in as `hdaw_plugin_host.exe` (spawn fails in ~25 ms instead of the 8 s
    READY budget). A `--protocol` negotiation flag is NOT a fix: the old child IGNORES unknown
    arguments and answers READY anyway — never claim a guard the stale binary would ignore.

43. **`ScopedJuceInitialiser_GUI` is a process-wide KILL SWITCH — the message pump must PIN it**
    (diagnosed 2026-09-29). JUCE's `ScopedJuceInitialiser_GUI` calls `shutdownJuce_GUI()` when its
    LAST instance dies, which runs `DeletedAtShutdown::deleteAll()` (deleting the `ShutdownDetector`,
    which STOPS JUCE's TimerThread) and `MessageManager::deleteInstance()`. The next
    `MessageManager::getInstance()` then re-creates the manager on WHICHEVER thread asks first, and
    the `InternalMessageQueue` (hidden window + queue) is rebound to that thread: the HDAW
    `MessagePumpThread`'s `GetMessage` loop can never see another JUCE message, so EVERY `juce::Timer`
    and `AsyncUpdater` in the process stops firing PERMANENTLY. Symptom seen: two
    `PluginIsolation.StagedParams*` tests failing ("paramSet ring was never written") in combined
    gtest filters only — because several test files hold their own `ScopedJuceInitialiser_GUI`, and
    the pump lost the re-creation race after one of their scopes. `MessagePumpThread` now constructs
    and DELIBERATELY LEAKS a `ScopedJuceInitialiser_GUI` on the pump thread (right after
    `MessageManager::getInstance()`), so the initialisation reference count can never reach zero;
    deleting that pin at static-destruction time instead produced `STATUS_HEAP_CORRUPTION`
    (0xC0000374) at process exit in single-test runs. Pinned by
    `MessagePumpThread.JuceInitialiserScopeDoesNotTearDownThePumpQueue` (fails without the pin:
    `getInstanceWithoutCreating()` becomes NULL and a probe timer never ticks). Diagnostic recipe
    that found it: a `HDAW_TRACE_PARAM`-style env-gated trace of the pump loop (heartbeat +
    `MessageManager::isThisTheMessageThread()`) and of the JUCE timer lifecycle — the pump reported
    `isMsgThread=0` and never ticked again for the rest of the run.

44. **A clamped WAV export hides the true float peak — size headroom from the RENDER, not the file.**
    A full-kit unison of the new `drum_synth` measured `peak 1.0000 / clipping true` in `mix_report`
    on an exported WAV, while the true float peak was **3.558× unity (+11 dB)** — the WAV writer
    clamps to ±1.0 on write, so the file can never report more than 1.0. A headroom fix was sized
    twice from that clamped number and was wrong both times: first a linear trim (arithmetically
    unable to serve a 1-voice level and an 11-voice unison at once), then a knee placed *below* a
    single voice's own raw sum, which compressed every normal hit. **Rules:** (a) to size a
    level/headroom change read the peak from the render (in-process gtest buffer, or `verify_part`'s
    solo peak) — never from an exported WAV; (b) a WAV pinned at exactly 1.0000 with a nonzero
    `ceilingHitPct` means the float signal EXCEEDED unity by an unknown amount — read 1.0 as
    "≥1.0"; (c) prefer a property assertion (e.g. halving `Output Level` must exactly halve the
    peak) over an absolute peak band, which breaks on any benign voice tweak.

45. **An N-voice instrument summed into ONE slot cannot be linearly bounded.**
    The `drum_synth` kit's single kick peaks at ~0.51 output at default params, so eleven of them
    sum to ~3.56× unity; bounding that linearly needs a trim of ~0.27, which drops a single kick to
    ~0.2 (−14 dB) — unusable. **Rule:** give a multi-voice internal instrument a **memoryless soft
    ceiling above the normal-voice range** (knee above what 1–2 voices produce, asymptote below
    unity) applied to the VOICE SUM before the user's output-level param, so ordinary playing stays
    bit-identical and only genuinely simultaneous hits are shaped; and place it before the output
    param so the user's overdrive range survives. Pinned by
    `DrumSynthEngineTest.SimultaneousFullKitStaysUnderUnity` (11 voices on one sample < 1.0; single
    kick linear under a halving check).

46. **The MCP engine answering your calls is a COPY in `%TEMP%`, and its image name differs — so
    `taskkill /IM HDAW_headless.exe` misses it; and the identity fields are CORRECT, so
    `engine_info`'s `stale: true` is real, not a false positive.** `mcp-launch.bat` (and the
    launcher) copy `build/HDAW_headless.exe` to `%TEMP%\HDAW_headless_mcp.exe` and run THAT, so
    `taskkill /F /IM HDAW_headless.exe` does not kill it (and the launcher copy is another
    session's backend — do not kill it blindly). The designed staleness check is
    `engine_info {buildBinaryPath: "<your build>"}`: it reports the path/mtime of the process
    ACTUALLY answering and sets `stale: true` when that process is older than your build — **trust
    it**. Measured 2026-10-01: a `--mcp-http` spawn of `build\HDAW_headless.exe` could not bind the
    port (the launcher's `%TEMP%` copy already held it) and **kept running mute**; `engine_info`
    therefore correctly reported the `%TEMP%` copy's path/mtime and correctly said `stale: true` —
    that is exactly what it means, and the earlier claim that the identity fields were unreliable
    was a **misdiagnosis (retracted)**. **The one-step diagnostic that settles it:** if a spawn's
    OWN log says `MCP HTTP start failed: failed to listen on ...` (already in use), then YOUR
    process is not serving and another engine is answering every call — and the reason such a
    spawn could not take a private port in the first place is **lesson 48** (its `--mcp-http-port`
    was silently dropped, so it re-tried the persisted 18765 rather than the private port).
    **Rules:** (a) trust
    `whoami.runningBinaryPath`, `whoami.runningMtime` and `engine_info.stale`; (b) **confirm** by
    content when you want a second signal — call a tool whose output you know changed in the new
    build (`tool_help <name>` for a changed description, or `list_fx_params` for a changed param
    count) and compare against the source/binary; (c) or check **OS process identity** with
    `Get-CimInstance Win32_Process -Filter "Name like 'HDAW%'" | Select ProcessId,Name,ExecutablePath`;
    (d) `taskkill /F /IM HDAW_headless.exe` does NOT kill `HDAW_headless_mcp.exe`; (e) for an
    isolated fresh-binary smoke use `python scripts/mcp_call.py run <steps.json>`, which spawns
    `build/HDAW_headless.exe` over stdio with no port — the only path guaranteed to exercise the
    build you just made; (f) a private `--mcp-http-port` alone is not sufficient: the headless
    frontend WS port must also be free (`--port`) or the engine exits 1 — and since 2026-10-01 an
    explicit `--mcp-http` **fails fast** (non-zero exit) when the requested port is not actually
    served (lesson 48), so a mute spawn can no longer masquerade as healthy.

47. **A windowed offline render never delivers a note-on that falls BEFORE the window start** — so
    a one-shot/percussive part whose only hits precede the window reports silence. A `drum_synth`
    clip with all 11 GM hits at beat 0 measured `soloPeak=0 / audible=0` via `verify_part` for
    windows starting at 0.01 and 0.25 beats, while the full 2-beat export of the same project peaked
    at 0.457. **Rules:** (a) put the window start at or before the first hit (or place the hits
    inside the window) when verifying one-shot parts; (b) `verify_part` requires `startBeat > 0`, so
    place the notes a hair after beat 0 rather than trying `startBeat: 0`; (c) a zero `soloPeak` on
    a part you can hear in the full export is a windowing artifact first and an engine bug second —
    check the window before the DSP.

48. **A CLI value round-tripped through a settings store you cannot write is silently DROPPED — so
    the flag is accepted and has no effect.** `main_headless.cpp`/`main.cpp` parsed
    `--mcp-http-port` / `--mcp-http-host` correctly, wrote them into `QSettings`
    (`mcp/httpEnabled|httpHost|httpPort`), and `AudioEngine::initialize()` →
    `syncMcpHttpFromSettings()` (`AudioEngine.cpp:388`) then read the persisted values BACK and
    bound those — the command line was routed **through a store** instead of being applied
    directly. On this box that store is **not writable**: a standalone Qt6 probe wrote
    `mcp/httpPort=18899` into `HKCU\Software\HDAW\HDAW`, called `sync()`, and read back the OLD
    `18765` (`fileName=\HKEY_CURRENT_USER\Software\HDAW\HDAW`, `format=NativeFormat`); a direct
    Python `winreg` write to that key returns **`PermissionError [WinError 5] Access is denied`**
    (the sandbox's restricted token). So the write never landed. Measured 2026-10-01:
    `build\HDAW_headless.exe --mcp-http --mcp-http-port 18841 --port 18843` bound the **persisted
    18765**, not 18841; a headless run logged `MCP HTTP listening on 127.0.0.1:18871`; and the
    registry value `HKCU\Software\HDAW\HDAW\mcp\httpPort` stayed `0x494d` (18765) *while the process
    ran*. The engine **stayed alive anyway** (it logged the bind failure only), so a mute spawn
    looked healthy — which is what produced the earlier misdiagnoses. **Rules:** (a) **pass
    CLI/API-supplied values to the API directly — never round-trip them through a store you cannot
    prove is writable**; the command line is authoritative (`engine.setMcpHttpConfig(true, host,
    port, &err)` after `initialize()`, then verified against `getMcpHttpConfig()`); (b) a value
    accepted with no effect is the **lesson-38 silent-acceptance class** — assert the *observable*
    effect (which port the engine actually serves), never just the parse; (c) an explicit
    `--mcp-http` now **fails fast** (non-zero exit) when the requested port is not actually served,
    so a mute spawn can no longer masquerade as healthy; (d) a config query must report **LIVE**
    state for a running server, not a persisted snapshot — `AudioEngine::getMcpHttpConfig()`
    returns the live `host`/`port`/`enabled` while a server runs, falling back to the persisted
    snapshot only when stopped. Pinned by `HeadlessMcpHttpPort.ServesOnTheCliPort` and
    `HeadlessMcpHttpPort.ExitsNonZeroWhenTheCliPortIsTaken`
    (`tests/integration/mcp/headless_mcp_http_port_test.cpp`). **SECONDARY (NOT the cause here):**
    Qt's default-constructed `QSettings` needs a `QCoreApplication` **INSTANCE** for the default
    `organizationName()`/`applicationName()` to resolve, so construct `QSettings(org, app)` with
    explicit names when an instance is not guaranteed — the entry points now do
    (`QSettings(QStringLiteral("HDAW"), QStringLiteral("HDAW"))`). That is a correctness habit, not
    the root cause of this drop: the store was unwritable regardless of the names.
