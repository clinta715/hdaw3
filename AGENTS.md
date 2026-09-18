# AGENTS.md

**MANDATORY:** Before ANY code change in this project, invoke the `hdaw-guard` skill:

```
skill: "hdaw-guard"
```

Skill file: [`docs/skills/hdaw-guard/SKILL.md`](docs/skills/hdaw-guard/SKILL.md).
This skill enforces plan-first development, guards against the 16 recurring pitfalls, requires dependency analysis, and alerts on anti-patterns. It is non-negotiable for every task.

**Sound-engine stability rule (standing):** Bug fixes to the sound engine proceed without prior discussion, and transparent/reversible/low-blast-radius performance improvements proceed as well. Changes with wide blast radius — anything touching `processBlock`, DSP chains, render/export, playback paths, plugin isolation, or internal/external FX contracts — require discussion with the user FIRST, with effort + risk notes BEFORE implementing. Engine changes carry outsized debugging costs (lessons 3/5/7/8/11–23) for features that can often be achieved another way (score-level generation, MCP/ValueTree wiring, or parameters of existing FX); default to non-engine implementations. Rendering and playback stability outrank new features.

Project-specific lessons learned. Read this before working on the timeline,
the project model, or the frontend — these are the pitfalls that cost real
debugging time.

**Current scope**: HDAW is a JUCE 8 desktop DAW at version **0.36.0** with a
**React 19 + TypeScript frontend** (Zustand, Vite). The frontend runs in two
contexts: system browser (default) or Electron shell. The C++ engine exposes
state via JSON-RPC 2.0 over WebSocket (port 8766) and serves the bundled React
SPA over HTTP (port 8765). The core engine (project model, transport, routing,
JUCE plugin hosting, internal FX) and the frontend (track headers, timeline,
mixer, piano roll, FX chain, automation) work end-to-end. For the feature
history and roadmap see `README.md`; per-version changes live in the git log.

## Documentation Directory

Detailed documentation is split into domain-specific files. For a specific
pitfall, search the relevant file; for architecture start with
`docs/architecture.md`; for realtime constraints see `docs/realtime-safety.md`.

| File | Contents |
| ------ | ---------- |
| [`docs/architecture.md`](docs/architecture.md) | Build, version management, key classes, GUI-engine decoupling, frontend architecture, timestretch, JUCE 9 migration, **beats-vs-seconds unit convention** |
| [`docs/realtime-safety.md`](docs/realtime-safety.md) | Audio-thread safety rules, hardening lessons, diagnostic pattern, plugin process isolation (default ON), **transport-stopped early-out (audio buzz) + idle-child stall-detector false-positives, auto-stop / projectEndSample staleness + play() re-entry race, message pump for headless/test processes, AudioProcessorGraph thread-safety / pump-park, DSP-state listener races, latency evaluation, quality/fidelity evaluation** |
| [`docs/pitfalls-juce.md`](docs/pitfalls-juce.md) | VST3 scan blacklisting, default project samples, DBG macro collision, build pipeline (MOC/PDB), AudioProcessorGraph bus layout, **setProperty no-op on unchanged value, notify.transport dedup**, **internal FX param clamping (reverb roomSize=900 → NaN export silence)** |
| [`docs/pitfalls-frontend.md`](docs/pitfalls-frontend.md) | Stale closures after async, optimistic placement + syncSnapshot conflict, drag double-movement, store vs prop reads, **vertical fader `direction: reverse` invalid** |
| [`docs/testing-mcp.md`](docs/testing-mcp.md) | GTest suite, TransportLoopback test seam, MCP server architecture, MCP tool safety, file browser audio preview |
| [`docs/valuetree-listener-contract.md`](docs/valuetree-listener-contract.md) | ValueTree listener registration contract, orphan prevention, ReadModel alternative, audit checklist, **delta-sync cannot compute derived state** |
| [`docs/postmortem-silent-clap-export.md`](docs/postmortem-silent-clap-export.md) | Multi-layer root-cause writeup of the silent-WAV-export bug (no message pump → bake-race ordering → stale-`.obj` build trap → teardown race → mutation-race crash family) — the canonical reference for lessons 11–15 |
| [`docs/adr-automation-model.md`](docs/adr-automation-model.md) | ADR: track-based automation as the primary model (clip-based/relative deferred), beats-vs-seconds implication |
| [`docs/bitwig-reference.md`](docs/bitwig-reference.md) | Bitwig Studio UI/architecture design reference with HDAW-side takeaways |
| [`docs/hardware-va-suite.md`](docs/hardware-va-suite.md) | Hardware VA suite (gearmulator CLAPs): devices, patch libraries + pipelines, loader status per device, host-param reality, modulation matrices, transitional-effect recipes, modulation-first policy |
| [`docs/psytrance-composition-guide.md`](docs/psytrance-composition-guide.md) | Psytrance composition via MCP: style canon, sample pipeline, score grammar, FX/LFO/automation recipes, **FM synthesis (PsyFm engine, presets, modulation targets 300–308)**, slicing/timestretch tools, mix + verification, contract traps — distilled from the 2026-08-26/27 composition sessions and 2026-09-01 FM integration (recipes: `psytrance_composition_stress_test.cpp`) |
| [`docs/skills/psy-song-session/`](docs/skills/psy-song-session/SKILL.md) | Agentic song-writing pipeline: five scoped role playbooks (Curator, Pattern Researcher, Sound Selector, Arranger, Mix Verifier) + Song Brief schema + orchestrator dispatch rules |
| [`docs/handoffs/`](docs/handoffs/) | Session handoff notes (one file per handoff; completed-work context, not live specs) |
| [`docs/archive/superpowers/`](docs/archive/superpowers/) | Historical plans/specs (Jun–Aug 2026). Completed work — context only, not live specs. Current plans live in `docs/plans/` |

## Knowledge Graph (graphify)

`graphify` indexes this repository into a persistent knowledge graph (functions,
classes, files, calls, docs) in `graphify-out/` — `graph.json` (queryable),
`graph.html` (interactive), and `GRAPH_REPORT.md` (God Nodes / community hubs).
Use it as the FIRST tool for blast-radius analysis, dependency tracing, and code
discovery — it answers structural questions faster than grep.

**Available as pi tools:** `graphify_query` (BFS natural-language traversal),
`graphify_path` (shortest path between two nodes), `graphify_explain` (node
explanation), `graphify_build` / `graphify_update` (rebuild), plus the
`/graphify` skill / CLI. Kept current automatically: a `post-commit` hook
rebuilds after each commit and a `--watch` background process (auto-started by
the `.pi/extensions/graphify-watch.ts` pi extension) rebuilds on file changes.

### Workflow

1. **Check freshness** — `GRAPH_REPORT.md` records the build date + commit
   (`Built from commit: <hash>`); compare to `git rev-parse HEAD`. If stale,
   refresh with `graphify update .` (incremental, AST-only, no LLM cost).
2. **Discover code / blast radius** — `graphify query "What depends on X and
   what does X depend on?"` (BFS, natural language). Use it over grep for "who
   calls X", "what does X touch", "trace the data flow".
3. **Map a specific path** — `graphify path "A" "B"` for a caller/callee chain
   or dependency path between two nodes.
4. **Read a definition** — `graphify explain "X"` for a plain-language
   explanation of a node and its neighbors; cross-check the `source_location`
   with a read before assuming.
5. **Architecture at a glance** — `GRAPH_REPORT.md` lists God Nodes
   (high-degree hubs) and community hubs (de-facto modules). A node spanning
   multiple communities is an architectural seam — verify its interface contract.

### Rules

- **Query, don't rebuild.** `graphify_query` / `graphify_path` /
  `graphify_explain` never modify the repo; only `graphify_update` /
  `graphify_build` write `graphify-out/` (and only when the graph is stale).
- **Never invent an edge.** If the graph shows no connection, verify with
  grep/read before assuming.
- **The graph is a snapshot.** Code added since the last build is missing —
  cross-check critical paths with grep/read.
- **Refresh after structural changes.** After adding new files, RPC methods, or
  classes, run `graphify update .` so the completion contract (per
  hdaw-guard §Completion Contract) checks against current topology. The
  `post-commit` hook + `--watch` keep it current automatically.

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

## Performance rules: batch RPCs, walk the tree incrementally

Standing rules for any code that mutates or reads the project. These are what
keep the arrange view smooth and avoid the "black screen" cliff (lesson 6, now mitigated by incremental routing).

1. **Consolidate RPC calls — one batch, not N loops.** A set of related
   mutations must be a single batch RPC (`addClips`, `removeClips`, `moveClips`,
   `duplicateClips`, `paintClips`, `mergeClips`) or one
   `beginTransaction`/`endTransaction` block — never N separate `await rpc.call`
   in a loop. This is the most efficient path, and the win is *not* just fewer
   round-trips: every engine mutation fires the root `ValueTree` listeners
   synchronously, so one batched call lands in a single message-loop tick and the
   16 ms `TreeDeltaAccumulator` + `AsyncUpdater` coalesce it into **one** delta
   broadcast and **one** graph rebuild (and one atomic undo unit). N separate
   calls are N round-trips that can span ticks → N rebuilds, N deltas.
2. **Prefer incremental deltas over full re-serialization.** Express a change as
   a clip/track delta whenever possible so `notify.treeChanged` carries a minimal
   payload and the frontend `applyDelta` patches the snapshot in place (stable
   object references → minimal React re-render). Reserve `fullSync` (whole
   `read.snapshot` re-fetch) for changes that restructure the tree or touch
   non-clip/track entities. Derived state (`effectiveMuted`/`effectiveSoloed`)
   can't be deltaed (lesson 4) — that's the exception, not the default.
3. **Don't re-walk the whole `ValueTree` to touch one node.** Use indexed access
   / `getChildWithProperty` and held references instead of full-tree scans per
   mutation. `rebuildRoutingGraph()` and `ReadModel` snapshot building are
   O(project) and are the hot path to keep incremental (the incremental routing
   path, default ON, avoids `rebuildRoutingGraph` for clip add/remove/move).

## MCP feature parity

The MCP server is a first-class client of the engine, not a secondary surface.
**Any feature available to the user through the UI must also be available
through the MCP** — if a human can do it from the frontend, an MCP tool must be
able to do it too. When you add a user-facing capability (a command, edit op,
transport action, or project mutation), expose it as an MCP tool in the same
change; when you audit a feature gap, the MCP side counts as unfinished until
it's reachable. The UI and MCP share the same RPC/command layer, so this is
usually wiring a tool onto an existing command rather than new engine work.

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
  comes from re-seeded content, never from structure drift.

- **Hardware VA suite (gearmulator CLAPs)** — OsTIrus (Virus TI), Osirus
  (Virus A/B/C), Vavra (microQ), Xenia (Microwave), JE8086 (JP-8000),
  NodalRed2x (Nord Lead 2x), Dexed (DX7) run as isolated CLAPs with their real
  firmware (installed in `C:\Program Files\Common Files\CLAP\` with ROMs).
  Injection tools: `send_fx_midi` (PC/CC/note/sysEx), `load_virus_preset`
  (CC0 bank + PC), `load_dexed_cartridge` (.syx). Per-plugin **matrix presets + morph chains** exist
  for all five devices (`timbre-lib/matrix_presets/`); apply them via the
  `list_matrix_presets` / `apply_matrix_preset` MCP tools (xenia/nord/je8086
  verified live; per-engine measured status in `docs/hardware-va-suite.md` §9).
  Audition workflow:
  inject → `save_project` → `export_audio` → measure — the preset lives in the
  live plugin state; the save persists it into the tree for offline renders.
  Constraint: the serializer's size-regression guard protects plugin states
  across load→save cycles (see docs/plans/2026-09-12-plugin-state-durability.md).
  **Patch pipelines:** every device with a bank library has a decoder writing
  searchable sidecars — `virus_patch.py` (`.virus.json`), `nl2x_patch.py`
  (`.nl2x.json`), `je8086_patch.py` (`.je8086.json` + an exploded per-patch tree),
  `microq_patch.py` (`.vavra.json`); FileLibraryManager ingests all four (register
  the folder as a *patch* library). **Loaders are evidence-gated:** `load_nord_bank`
  queues and changes bytes but by EAR the renders stayed near-identical across 14 real
  patches (2026-09-18 ear pass — delivery gap, see
  docs/handoffs/2026-09-18-gearmulator-custom-builds.md), `load_virus_preset` (CC0+PC)
  queues but does NOT change Osirus renders on the current build (preset-load ext
  absent; finding F-A — under investigation), JE8086 DT1 dumps
  do NOT apply (its 461 parameters do), Vavra exposes no host parameters and its
  SysEx injection is MEASURED NOT APPLYING (2026-09-16/17: queued but state
  unchanged; channel filter excluded — see
  docs/plans/2026-09-16-matrix-preset-engine-fixes.md). **Prefer the device over a plugin for movement:**
  own modulation matrix → onboard FX → HDAW automation/track LFO → HDAW internal FX →
  third-party plugin last (plugin FX add CPU, latency, isolation and state-round-trip
  risk). Device matrix, per-device FX recipes and caveats:
  `docs/hardware-va-suite.md`.

**Guideline: when adding a feature, ask whether the generative/random/modulation
toolkit applies.** New note or parameter editing should offer humanize/randomize;
new content types should consider a generative path; new modulatable parameters
should be wired as modulation targets. Prefer extending these shared utilities
over one-off randomness, so behavior (and its MCP/RPC surface) stays consistent.

## Build (summary)

- Configure/build: `cmake --build build --config Debug`
- Outputs: `build/Debug/HDAW.exe`, `build/Debug/HDAW_headless.exe`, `build/Debug/hdaw_tests.exe`
- Do NOT run `build/Release/HDAW.exe` — stale binary, contains none of the fixes.
- **Two launch modes:** Default (browser), Headless (Electron).
- **Frontend build:** `cd frontend; npm run build`, then rebuild the C++ project.
- See [`docs/architecture.md`](docs/architecture.md) for full build details.
- **Pre-build time sync (WSL/Windows clock drift):** before ANY build/compile
  in this repo (`cmake --build`, `build-fast.bat`, `frontend\build.bat`, bare
  `ninja`, `npm run build`), invoke `skill: "pre-build-time-sync"` — it snaps
  the WSL clock to the Windows host (`sudo ntpdate -b time.windows.com`) so
  ninja/MSBuild never misjudge file mtimes through drvfs/9p under WSL2 clock
  drift (lesson 15 / the WSL-side-edit sync recipe). `build-fast.bat` and
  `frontend\build.bat` call `scripts\time-sync.cmd` automatically, and CMake
  adds a `hdaw_time_sync` ALL target covering bare `cmake --build`; direct
  `ninja` / `npm run build` runs need the explicit `scripts/time-sync.sh`.
  The hook is a fast no-op outside WSL and NEVER fails a build. See
  `docs/archive/plans/2026-09-05-time-sync-build-hook.md` and
  `docs/skills/pre-build-time-sync/SKILL.md`.

### Shell: PowerShell only (no `&&` or `&`)

This development system runs **Windows PowerShell 5.1**, where `&&` and `&` (as a command separator) are **not valid**. Every command in AGENTS.md, scripts, and docs must use PowerShell-native syntax:

| Goal | Use this | Not this |
| ------ | ---------- | ---------- |
| Run commands sequentially (fail on error) | `cmd1; if ($?) { cmd2 }` | `cmd1 && cmd2` |
| Run commands sequentially (ignore errors) | `cmd1; cmd2` | `cmd1 & cmd2` |
| Run in subshell / change dir | Use the `workdir` parameter on tool calls, or `Set-Location` | `cd dir && cmd` |
| Background jobs | `Start-Job { ... }` | `cmd &` |
| Boolean AND / OR | `if ($?) { ... }` / `if ($LASTEXITCODE -eq 0) { ... }` | `&&` / ` | | ` |

**When writing new commands in this project**, always prefer PowerShell-compatible forms. Existing references to `&&` in documentation (including this file, `README.md`, and `docs/`) are legacy from bash-originated docs and should be updated on sight.

### How frontend changes reach the running app (the stale-frontend trap)

The React frontend is delivered three ways, and **a plain `cmake --build`
updates NONE of them**. If a frontend fix "doesn't take effect after
rebuilding," this is almost certainly why:

| Run mode | Binary | Frontend source | To pick up frontend changes |
| ---------- | -------- | ----------------- | ------------------------------ |
| **Packaged Electron** | `frontend/release/win-unpacked/HDAW.exe` | Frozen in `resources/app.asar` | **Repackage:** `frontend\build.bat` (or `npm run build; if ($?) { npm run package:dir }`). Ctrl+Shift+R does nothing here. |
| **Browser (standalone exe)** | `build/Debug/HDAW.exe` | Embedded via `frontend.qrc` | `frontend\build.bat` forces a clean C++ rebuild when `dist/` is newer (AUTORCC under the VS generator does NOT treat changed `dist/` as a rebuild trigger). |
| **Vite dev server** | `npm run dev` (+ engine for WS on 8766) | Live from `frontend/src` | Hard-refresh the browser (Ctrl+Shift+R). No build needed. |

**The packaged Electron app is the one users run.** Its frontend is baked into
`app.asar` at packaging time - editing source, rebuilding `dist/`, or
refreshing the window has zero effect until you repackage. `frontend\build.bat`
rebuilds the SPA, the C++ engine, runs the tests, and repackages Electron in one
command. Both build scripts detect an obsolete `app.asar` and fail/warn loudly,
so you can't silently iterate against a stale `.asar`.

**The packaged app's ENGINE comes from `build/RelWithDebInfo/`** (see
`electron-builder.yml` `extraResources`), NOT `build/Debug/`. A bare
`npm run package:dir` re-ships whatever `RelWithDebInfo` happens to contain —
on 2026-08-16 that was an 11-day-old engine (8/5) with none of the
respawn-storm fixes, so the app kept crashing plugins while every Debug-mode
verification looked clean. `frontend\build.bat` builds the engine too; if you
repackage by hand, run `cmake --build build --config RelWithDebInfo` FIRST and
verify the binary (string-search the shipped `resources\engine\HDAW_headless.exe`
for a fix marker) before trusting the package.

## Testing

- **C++ engine tests (gtest):** `build/hdaw_tests.exe` (flat Ninja RelWithDebInfo layout — there is no `build/Debug/`; `build-fast.bat test` builds it, `build-fast.bat all` also builds `hdaw_plugin_host.exe` which the PluginIsolation/CrashRecovery suites require)
  - Filter: `--gtest_filter=SuiteName.*`
  - Full suite: ~1328 tests, ~13 min. Fast iteration tier: `run_fast_tests.bat` (~3.3 min; excludes the render/recipe/spawn-heavy suites — run the full suite before delivery).
  - Current baseline (2026-09-02, post DISABLED-test rewrite pass): 0 failed; 4 RealtimeSafety detector tests SKIP in release configs (`BufferCheck` is `#if JUCE_DEBUG`-only by design); 0 DISABLED — every formerly `DISABLED_` test is either re-enabled against current contracts (PluginIsolation ×4, ExportVolumeBypass.RealProjectVolumeSensitivity, TrackFXSlotShowEditor — see `docs/archive/plans/2026-09-02-seven-failure-baseline-fix.md`) or re-enabled after its fix (`ExportAudioWithMultipleIsolatedInstances`, commit abf8a3d).
  - Build sequentially: two concurrent `build-fast` invocations on the same `build/` dir overwrite each other's `.ninja_log`, and the next build re-runs as near-full. One build at a time.
  - WSL-side edits must be synced for the Windows compiler (drvfs/9p attribute cache shows stale content/mtimes for minutes): after editing from WSL, `cp <file> /mnt/c/temp/sync_tmp.cpp`, then from Windows `Copy-Item C:\temp\sync_tmp.cpp -> <D: path> -Force`, then touch `(Get-Item <path>).LastWriteTime = Get-Date`, and verify with PowerShell `Select-String`/`Get-Content` (never findstr through bash→cmd quoting). Symptom if skipped: ninja rebuilds "succeed" against stale sources. Verified recipe — see `docs/archive/plans/2026-09-02-seven-failure-baseline-fix.md` outcome.
  - 1015→1328 tests across 182→216 suites: MCP tools/server, transport, tracks, clips,
    notes, FX, automation, undo, save/load, phrase generation, slicing, merge,
    ripple delete, ghost clips, stretch, markers, error conditions, batch ops,
    plugin isolation, audio pool, streaming, arranger, session, library,
    note operators, tempo points, sends, MIDI FX.
  - **Engine change test discipline:** when modifying the C++ core, RPC surface,
    or JUCE interfaces, assess test impact before finishing: (1) identify gtest
    suites that exercise the changed code (RPC handlers → MCP tool tests,
    ValueTree mutations → track/clip/note tests, audio-thread logic → transport
    tests) and update them if signatures/return shapes/behavior changed; (2) if
    the change adds a new RPC method, command, or JUCE-facing interface with no
    coverage, add a gtest — the suite is the contract the frontend and MCP
    server rely on; (3) run `build/Debug/hdaw_tests.exe` to confirm no
    regression. An engine change with no test consideration is incomplete.
- **Frontend unit tests (Vitest):** `cd frontend; npm test`
  - ~177 tests: Zustand stores (transport, ui, project, notify, meter, browser),
    hooks (useTimelineDrag), utils (rowLayout, theme, grooveUtils), and
    components (StatusBar, Toaster, BottomTabs, MidiFxChain, WaveformCanvas,
    MidiThumbnailCanvas, StepSequencer, TimelineContextMenu, MixerStrip,
    TrackHeaders, Icons).
  - Watch: `npm run test:watch` · Coverage: `npm run test:coverage`
- **Frontend E2E tests (Playwright):** `cd frontend; npm run test:e2e`
  - ~197 tests in `e2e/*.spec.ts`. `app.spec.ts` = render smoke; the rest are
    user-journey regressions that drive the real app (click/drag/keyboard) and
    assert on DOM/canvas/snapshot state — the layer that catches the recurring
    interaction bugs unit tests miss (drag stale-closures, rubber-band
    hit-testing, waveform display, selection→editor opening, context menus).
  - The `webServer` in `playwright.config.ts` auto-starts the engine
    (`build\Debug\HDAW.exe`, with `HDAW_NO_BROWSER=1`) plus the Vite dev server
    (port 5173); tests run against the **live** frontend, so frontend changes
    are picked up with no rebuild/repackage. Requires a current
    `build/Debug/HDAW.exe` and Playwright browsers (`npx playwright install chromium`).
  - `workers: 1` — the engine is a singleton serving one project, so tests run
    serially; each calls `startApp()` (clicks "New Project") for a clean state.
  - Test seams: `window.rpc` for RPC setup, `data-clip-id` on `.tl-clip` for
    targeting clips, `HDAW_NO_BROWSER`. Shared helpers in `e2e/helpers.ts`
    (`startApp`, `rpcCall`, `addMidiClip`/`addAudioClip`, `dragClip`, `writeSineWav`).
  - **Clip-position assertions must poll.** After an RPC that shifts clips
    (insert silence, duplicate region, move), the tree-change notification is
    debounced (~16 ms) so the DOM doesn't update immediately. Assert positions
    with Playwright's `expect.toPass()` polling, not a one-shot read:
    `await expect(async () => { expect(await clipLeft(...)).toBeGreaterThan(...); }).toPass({ timeout: 10000 });`

[Showing lines 1-693 of 911 (50.0KB limit). Use offset=694 to continue.]