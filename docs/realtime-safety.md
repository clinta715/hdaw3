# HDAW Realtime Safety Reference

Domain-specific documentation split from AGENTS.md.
For the original combined file, see `../AGENTS.md`.

Sections: Audio-Thread Safety Rules, Hardening Lessons, Diagnostic Pattern,
Codebase Hardening, Plugin Process Isolation.

## Realtime / Audio-Thread Safety

The audio render path (`MainAudioProcessor::processBlock`,
`Track::processBlock`, `ClipSourceProcessor::processBlock`,
`MidiClipProcessor::processBlock`, `AutomationManager::getValueAt`)
runs on the audio thread. Violating realtime safety causes
dropouts, xruns, and hard-to-reproduce crashes.

**Rules for audio-thread code:**

- **Never allocate.** No `new`, `malloc`, `std::vector::push_back`,
  `std::string` construction, or `juce::String` construction inside
  `processBlock` or any function called from it.
- **Never lock.** No `std::mutex`, `juce::SpinLock` (write path),
  `juce::CriticalSection`, or any blocking wait. The
  `AutomationManager` uses `SpinLock` only for the *write* path
  (ValueTree listener on UI thread); the read path in
  `getValueAt` is lock-free.
- **Never call UI code.** No `emit`, no `QMetaObject::invokeMethod`,
  no `juce::MessageManager::callAsync` from the audio thread.
- **Use the SPSC bridge for UI→audio parameter updates.** The
  `SPSCBridge` (`src/engine/SPSCBridge.h`) is a single-producer
  single-consumer queue. UI code pushes `ParamUpdate` structs;
  the audio thread pops them at the top of `processBlock`.
  This is the only sanctioned cross-thread path for parameter
  changes.
- **Level meters use atomics.** `LevelMeter` (`src/engine/LevelMeter.h`)
  stores peak/RMS as `std::atomic<float>`. The audio thread writes;
  the UI thread (VUMeter timer) reads. No locks.
- **Beware of indirect violations.** A realtime-safe function
  becomes unsafe if you call a non-realtime-safe function from it.
  Check the full call chain. Common traps: `juce::Logger::outputDebugString`
  allocates; `juce::ValueTree::getProperty` is safe for reads but
  listeners that trigger writes must not fire on the audio thread.

## Hardening lessons learned

Lessons from the v0.3.x codebase hardening pass (Phases 1–5, 23
tasks). These are *not* deferred work — they document tradeoffs and
contracts that future contributors should know about.

- **Audio-preload memory cost (Task 1).** `ClipSourceProcessor`
  preloads the entire audio file into two `HeapBlock<int>` buffers
  in `prepareToPlay` (`src/engine/ClipSourceProcessor.h:88-114`),
  eliminating audio-thread disk I/O. Memory cost is ≈ file size
  per clip (2 channels × `lengthInSamples` × 4 bytes). Acceptable
  for v0.3.x desktop use; a background-streaming ring buffer is
  the documented v0.4 follow-up (see "Hardening follow-ups"
  below). Do not regress to streaming from disk on the audio
  thread; the original code was a known xrun source.

- **SPSC paramID scheme (Task 3).** UI→audio parameter changes
  cross threads via `SPSCBridge`. The `ParamUpdate` struct uses a
  small integer `paramID` to identify which track/clip and which
  field is being updated. The contract:
  - `1=volume`, `2=pan`, `3=isMuted` (TRACK)
  - `10=gain`, `11=fadeIn`, `12=fadeOut` (CLIP)
  - `13=startTime`, `14=duration`, `15=offset`, `16=looping` (CLIP,
    added in Task 3 to forward MCP/GUI clip-position writes)

  The audio-thread dispatcher (`MainAudioProcessor::processBlock`)
  and the UI-thread sender (`AudioEngine::valueTreePropertyChanged`)
  must agree on the same map. When adding a new paramID: pick the
  next free number, extend both the SPSC send side and the
  receive side's switch (`RoutingManager::updateClipParam`), and
  add a new `ClipSourceProcessor::setX` setter that writes to an
  atomic the audio thread polls. Numbers 4–9 and 17+ are
  currently unallocated; reserve ranges for future track-side vs.
  clip-side additions to keep the numbering legible.

- **Plugin param automation (v0.5.0).** Plugin FX parameters
  use compound paramIDs `>= 100`:
  `paramID = 100 + slotIndex * 100 + paramIndex`.
  `TrackFXSlot` stores a `std::unique_ptr<std::atomic<float>[]>`
  `paramValues` array (one per automatable param), written by
  the UI thread and read lock-free by the audio thread.
  `TrackFXSlot::applyAutomation()` is called before each
  `slot->process()` in `Track::processBlock`. The
  `AutomationLaneWidget` `+` button opens a menu listing all
  automatable parameters (track-level + per-slot plugin params).

- **`ClipSourceProcessor` gain-loop bound (Task 18).** The audio-
  thread gain smoother loop iterates only `numToRead` samples —
  the audible portion — not the full `numSamples` block
  (`src/engine/ClipSourceProcessor.h:156`). The smoother's
  internal state therefore advances at audible-sample rate, not
  wall-clock. When `numToRead < numSamples` (clip tail beyond
  the source file), the smoother drifts slightly relative to a
  hypothetical wall-clock version. The drift is sub-millisecond
  and only affects invisible state (smoother internals); audible
  output is identical. If a future feature needs sample-accurate
  wall-clock smoother advancement, decouple the smoother step
  from the audible read (e.g. always advance `numSamples` even
  when reading fewer).

## Diagnostic pattern when something is mysteriously wrong

When a fix doesn't take effect despite clear evidence the binary
contains it, add `HDAW_LOG` calls at the *exact* points where the
problem-state is decided. The pattern is:

1. One log at construction / data-setup time, showing the input state.
2. One log in the rendering or event-handling path, showing what the
   widget actually sees at runtime.
3. Cross-reference the two. If the data is correct but the
   rendering is wrong, the bug is in the rendering path. If the
   data is wrong, trace upstream.

This is what surfaced the QGraphicsView scroll-position bug. A
similar `paintEvent` log on `TrackHeaderWidget` revealed that
`scrollOffset=737` at first paint, immediately pointing at the
QGraphicsView's internal layout-time scroll commit.

**The log file is the first place to look, not the source code.**
When a user reports "nothing changed visually" or "X is broken,"
read `%TEMP%/hdaw_debug.log` directly. The `TSCtor`,
`TSRebuild START/DONE`, `MWAddTrk`, `TIEvt`, `TIPress`,
`TIDblClk` lines (pre-existing) give a complete picture of what
the app did during startup and on each user interaction. If a
fix is supposed to take effect and the log doesn't show the
expected state, the fix isn't running — probably a stale
binary (see "Build pipeline" above) or a missed compile of the
edited file.

## `processBlock` must early-out when the transport is stopped (audio buzz)

**Symptom (v0.13.1):** a steady buzz/hum at the buffer-rate frequency
(~86 Hz for a 512-sample block at 44.1 kHz) whenever playback is
stopped, even with no clips under the playhead.

**Root cause:** `MainAudioProcessor::processBlock` ran the whole
`AudioProcessorGraph` on every callback regardless of transport state.
When stopped, `TransportManager::advance()` is a no-op, so the position
never moves. Any clip whose start sits at the current position (very
common at position 0) reads the **same block of source samples every
callback** — the DAC replays an identical 512-sample buffer at the
callback rate, which is audible as a buzz.

**The fix** — silence the graph and skip processing when not playing,
recording, or counting in:

```cpp
// MainAudioProcessor::processBlock, after draining the SPSC bridge:
if (transportManager != nullptr
    && !transportManager->isPlayingNow()
    && !transportManager->isRecordingNow()
    && !countInActive.load())
{
    buffer.clear();
    midiMessages.clear();
    return;
}
```

**Rules:**
- The SPSC bridge must be drained **before** this guard, so UI knob
  changes don't back up while stopped.
- This is also why per-clip bounds checks alone are insufficient — a
  looping clip, or a clip straddling position 0, still feeds the graph.
  The top-level guard is the authoritative silence.

## Transport auto-stop and the `projectEndSample` staleness trap

There is no JUCE concept of "end of project"; the transport advances
forever unless told to stop. v0.13.1 added auto-stop:
`TransportManager` holds an atomic `projectEndSample`, and `advance()`
stops playback (clamping position to the end) once the playhead passes
it. `AudioEngine::timerCallback` (50 ms) polls `consumeAutoStopRequested()`
and writes `isPlaying=false` back to the ValueTree so the frontend
updates.

**The staleness trap:** `projectEndSample` is recomputed in
`rebuildRoutingGraph()`, which only runs on clip/track **add/remove**
(coalesced via `AsyncUpdater`). But clip **timing** edits (move, resize,
toggle-loop) arrive over the `SPSCBridge` (paramIDs 13–16) and do **not**
trigger a graph rebuild — so `projectEndSample` would go stale and
auto-stop would fire at the wrong place.

**The fix:** after draining the SPSC bridge in `processBlock`, if any
timing param (13–16) changed this block, recompute `projectEndSample`
from the live clip sources. Looping clips are excluded from the end
(they don't bound the project). Keep this recompute cheap — it iterates
the clip-source maps, which is fine on the audio thread for normal
project sizes.

**The auto-stop window race:** the audio thread sets
`TransportManager::isPlaying=false` + `autoStopRequested` immediately,
but the ValueTree still says `isPlaying=true` until the 50 ms timer
syncs it. A Play pressed in that window used to be swallowed:
`setProperty(isPlaying, true)` was a silent no-op (value unchanged —
see pitfalls-juce.md), and the timer then applied the stale auto-stop.
`play()` therefore **consumes the pending auto-stop first** (applying
the timer's effects itself: sample 0, tree stopped) before setting
`isPlaying=true`. Any new command that races the auto-stop flag must do
the same.

## Isolated plugins die silently if the stall detector kills idle children

**Symptom (2026-08-04):** playback works for a few play/stop cycles,
then produces no sound. The log shows `respawnIsolatedSlot ... path=`
(empty) repeating after every stop, then `CrashRecovery: gave up`.

**Root cause — two defects compounding:**

1. **False-positive stall kill.** The stopped-transport early-out above
   means `graph.processBlock` never runs while stopped, so
   `PluginProxySlot::processBlock` never writes to the child's input
   ring and the child's `audioBlocksProcessed` counter freezes. The
   health monitor (`ProxyProcessManager::checkAllChildren`) treated any
   frozen counter as a hang and killed the healthy child ~4 s into
   every stop.
2. **Respawn always failed.** `createPluginInstance` constructed the
   `PluginProxySlot` without passing `desc.fileOrIdentifier`, so
   `pluginPathForRecovery` was empty. The respawned child sent READY
   (spawn "succeeded"), then exited with code 1 because it couldn't
   load a plugin from an empty path. After 3 failed attempts
   `CrashRecoveryManager` gives up permanently → dead slot → silence.

**The fix / rules:**
- `checkAllChildren` only treats a frozen block counter as a hang when
  **input is pending** (`inputWritePos != inputReadPos`). An idle child
  with an empty ring is healthy — reset the stall timer instead of
  flagging it. (A genuinely hung child during playback leaves
  unconsumed input in the ring, so real hangs are still detected.)
- The proxy slot must always receive the plugin path
  (`desc.fileOrIdentifier`) so crash recovery can respawn it.
  `respawnIsolatedSlot` refuses to spawn with an empty path.
- Regression tests: `ProxyHealth.IdleChildNotKilledByStallDetector`,
  and `CrashRecovery.AutoRespawnAfterCrash` (which additionally waits
  3 s idle after recovery to prove the respawned child stays alive —
  this only passes when both fixes are in place).

## Codebase hardening (v0.3.x, 2026-06-30)

The codebase hardening pass addressed 23 tasks across 6 phases:
audio-thread safety, shutdown correctness, undo coalescing, shared
helpers, optimizations, and structural refactors.

**Key architectural decisions:**
- **Audio preload**: `ClipSourceProcessor` now preloads entire audio
  files into `HeapBlock<int>` in `prepareToPlay`, eliminating
  audio-thread disk I/O. See the "Hardening lessons learned"
  section for the memory-cost tradeoff and the v0.4 follow-up.
- **SPSC param forwarding**: mute (paramID 3) and clip position
  (paramIDs 13–16: startTime, duration, offset, looping) are now
  forwarded from the UI thread to the audio thread via the existing
  `SPSCBridge` queue, matching the volume/pan/gain/fade pattern.
  See the "Hardening lessons learned" section for the full
  paramID contract.
- **Undo coalescing**: all continuous drags (clip move/trim/fade,
  note drag, volume/pan fader, gain slider) call
  `beginNewTransaction()` on press so each drag is one undo step.
- **Factory dedup**: `ProjectModel::createAudioClip`,
  `createMidiClipEmpty`, `createMidiNote` are public statics that
  replaced 8 hand-written clip/note construction sites.
- **`trackIndexAtY`**: `TimelineScene::trackIndexAtY(double y)`
  replaces 3 duplicated hit-test loops in `TimelineInteraction` and
  `TimelineView`.
- **`getTrackOfClip`**: `ProjectModel::getTrackOfClip(clip)` replaces
  `clip.getParent().getParent()` at 2 sites.
- **Cached QSettings**: `PreferencesDialog::settings()` returns a
  function-local-static `QSettings&`, replacing 26 constructions.
- **MCP tool split**: `registerAllTools` is now a 10-line aggregator
  calling 8 domain-specific registrars (`registerReadTools`, etc.).
  The `export_audio` handler is extracted to `McpExportTool.{h,cpp}`.
- **Import extraction**: `onImportAudio`/`onImportMIDI` bodies moved
  to `AudioImport.{h,cpp}` / `MidiImport.{h,cpp}` free functions.
- **MainWindow split**: `setupLayout` is now ~30 lines calling
  `setupBottomPanel()`, `connectSignals()`, `restoreWindowGeometry()`.
  The `clipSelected` lambda is the named slot `onClipSelected`.
- **Context-menu split**: `TrackHeaderWidget::contextMenuEvent`
  dispatches to `buildEmptyAreaMenu`/`buildTrackMenu`.
  `TimelineView::eventFilter` dispatches to
  `handleContextMenu`/`handleKeyPress`/`handleDrop`.

### Hardening follow-ups (v0.4 candidates)

Work the hardening pass surfaced but did not finish — deferred to
v0.4. Lessons-learned (tradeoffs and contracts, not deferred work)
live in the "Hardening lessons learned" section above.

- **No per-clip audio editor.** Double-clicking an audio clip
  currently routes the user to the global Mixer panel, not to a
  per-clip waveform/properties editor. Adding one is a
  ~200-400-line feature, not a bug fix. (Main v0.4 candidate.)

- **ValueTree listener gaps in `MixerWidget` / `FXChainWidget`.**
  `TrackHeaderWidget` and `MixerStripWidget` are now
  `juce::ValueTree::Listener`s (attached to the project root tree,
  filtering by `IDs::TRACK` identity — see the "GUI-Engine
  Decoupling" section below). ~~The remaining gap is `FXChainWidget`,
  which still relies on the `rebuildAllUI()` calls in `MainWindow`
  for FX-slot add/remove/bypass. Closing the `FXChainWidget` gap is
  the last v0.4 follow-up here.~~ **Resolved (v0.10.0+):**
  `FXChainWidget` is now a `juce::ValueTree::Listener` on the project
  root tree. It filters for `FX_SLOT`/`FX_CHAIN` child additions,
  removals, reorders, and property changes on the current track
  (ignoring `pluginState` writes from the audio thread). Any external
  mutation (MCP, undo/redo, "Add Track with Plugin") now triggers
  `rebuildUI()` automatically. The "+ Add FX" button also shows a
  popup menu listing scanned plugins (grouped by Instruments/Effects)
  so users can add plugins directly without changing a combo box.

- **`TimelineScene::valueTreeChildRemoved` handles `IDs::TRACK`
  via incremental removal** (`src/ui/TimelineScene.cpp:180-217`).
  `removeTrackRow()` uses the `index` parameter from the callback to
  locate the removed track's Y position, removes only the affected
  clips via `clipItemMap`, and shifts remaining clips upward — mirroring
  the `removeClipItem()` pattern. Full `rebuildFromValueTree()` is no
  longer called on track removal.

- **`MixerStripWidget::paintEvent` no longer reads model state
  directly** (resolved). The widget is now a
  `juce::ValueTree::Listener`; `paintEvent` reads only the cached
  `name`/`volume`/`pan`/`muted`/`soloed` members, which are seeded in
  the ctor and refreshed by `valueTreePropertyChanged` (filtered to
  the strip's `trackTree` by identity). Same pattern applied to
  `TrackHeaderWidget::paintEvent`, which reads from the per-track
  `header.tree` handle captured in `rebuild()` instead of calling
  `getTrackListTree()` on every paint. Both widgets attach the
  listener to the project root tree (not the child) to survive
  project rebuilds — see "ValueTree listener orphans" above.

- **MCP server v1 follow-ups** (tracked in
  `docs/superpowers/specs/2026-06-29-hdaw-mcp-server-design.md`
  §10):
  - HTTP authentication for non-loopback exposure (loopback-
    only is current; auth is needed before any
    non-`127.0.0.1` binding).
  - `resources/*` and `prompts/*` (the v1 surface is
    tools-only).
  - `export_audio` worker-thread + per-block cancellation
    (the v1 implementation is synchronous on the main thread
    with a cancel-watcher thread; full async is a v1
    follow-up).

## Plugin Process Isolation (v0.4 candidate)

VST3/CLAP plugins run in a separate child process so crashes don't
take down the DAW. Enabled by default; disable with `-DHDAW_PLUGIN_ISOLATION=OFF`.

**Architecture:**
- `hdaw_plugin_host.exe` — child process that loads and runs a
  single plugin. Entry: `src/proxy/host/main.cpp`. Exits with
  `GRACEFUL_EXIT_CODE` (`0xC0DE0001`) on SHUTDOWN so the parent
  distinguishes graceful shutdown from crash.
- `PluginHost` — loads plugin via JUCE `AudioPluginFormatManager`,
  runs control loop (pipe listener) and audio loop (shared-memory
  ring buffer reader/writer). Increments `audioBlocksProcessed`
  watchdog counter per block.
- `ProxyProcessManager` — DAW-side process lifecycle. Spawns,
  monitors, kills child processes via `KillMode::KillGraceful`
  (sends SHUTDOWN, waits 2s, sentinel TerminateProcess) or
  `KillMode::KillHard` (immediate TerminateProcess). Crash detection
  via exit code + `audioBlocksProcessed` stall watchdog.
- `PluginProxySlot` — `juce::AudioPluginInstance` wrapper. Holds
  `std::shared_ptr<ShmRegion>` (refcounted; graphLock provides
  mutual exclusion with the audio thread). `migrateToNewSlot()`
  swaps in a new child/shm for in-place respawn.
- `ProxyEditor` — lightweight UI card (plugin name, bypass, open
  editor, crash-restart button). Bounded IO (2s timeout).
- `CrashDialog` — Qt dialog shown on crash. Restart button calls
  `CrashRecoveryManager::requestRespawn()`.
- `CrashRecoveryManager` (`src/engine/CrashRecoveryManager.{h,cpp}`)
  — auto-respawn loop. 500ms grace period, 3 retries with
  exponential backoff (1s, 2s, 4s). Tick driven by
  `PluginManager::timerCallback` (250ms). On respawn: kills old
  child, spawns new, restores state from temp file, calls
  `migrateToNewSlot`.

**IPC Protocol:**
- **Control pipe:** Named pipe (`\\.\pipe\hdaw_plugin_N`). Fixed
  256-byte `ProxyMessage`/`ProxyResponse` structs. No heap
  allocation. Bounded IO via `sendMsgBounded`/`receiveRespBounded`.
- **Audio:** Shared-memory SPSC ring buffers. `ShmHeader` has
  atomic read/write positions for input, output, MIDI rings, plus
  `audioFramesProduced`/`audioBlocksProcessed` watchdog counters.
  `SHM_MAGIC` = `0x48444158`.
- **Health:** `childAlive` atomic in `ShmHeader`. `checkAllChildren`
  runs every 2s, detects crashes via process exit code (non-sentinel
  = crash) and `audioBlocksProcessed` stall (no progress for >2s
  while process alive = hang).
- **EDITOR_CLOSED:** Async message from child when user closes plugin
  editor window. Relayed to `TrackFXSlot::remoteEditorOpen` via
  `PluginProxySlot::setEditorClosedCallback`.

**Recovery flow:**
1. Child crashes/hangs → health monitor detects within 2s.
2. `onChildCrashed()` sets `crashed=true`, saves state to temp file.
3. `CrashRecoveryManager::onSlotCrashed()` registers the slot.
4. Audio thread sees `crashed==true` → returns silence.
5. After 500ms grace, `tick()` runs respawn: kill old child, spawn
   new, restore state, `migrateToNewSlot()`.
6. `crashed=false`, audio resumes.
7. If respawn fails 3×: `giveUp()` logs and slot stays silent.

**Shared types:** `src/proxy/ProxyCommon.h` — `MessageType`,
`ProxyMessage`, `ProxyResponse`, `ShmHeader`, `MidiEvent`,
`GRACEFUL_EXIT_CODE`.

**Build flag:** `-DHDAW_PLUGIN_ISOLATION=OFF` to disable (default ON). Guards
`ProxyProcessManager`, `PluginProxySlot`, `ProxyEditor`,
`CrashRecoveryManager`, and the `hdaw_plugin_host` target.

**Spec / plan:**
- `docs/superpowers/specs/2026-06-30-plugin-process-isolation-design.md`
- `docs/superpowers/specs/2026-08-03-plugin-isolation-fixes-design.md`
- `docs/superpowers/plans/2026-08-03-plugin-isolation-fixes.md`
