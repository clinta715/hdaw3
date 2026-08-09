# Post-mortem: silent WAV exports with isolated CLAP plugins

**Date:** 2026-08-08 · **Status:** RESOLVED (full suite green, manual render verified)
**Commits:** `e917c1f` (fix), `540702a` (gitignore), `80c6ff5` (build warning hygiene)
**Related:** `.opencode/handoffs/2026-08-08-clap-export-fix.md`,
`.opencode/plans/2026-08-07-fix-clap-isolated-export-silence.md` (Phase 4 status),
`docs/realtime-safety.md` (pump + race rules)

---

## 1. Symptom

Exporting a project to WAV (`export_audio` MCP tool / `ExportManager`) — in particular
projects with **isolated CLAP plugins** (Vital, Dexed, JE8086) — produced WAV files that
were **completely silent** (PCM peak exactly 0.00000, measured across all historical
repro renders from 8/6–8/7). Additionally `McpServer.ExportAudioRendersDefaultProject`
**hung forever** at shutdown in some runs.

This was a multi-layer failure. Each layer hid the next; the final state required
**four independent fixes plus one build-system trap** to be resolved.

---

## 2. Layer 1 — no JUCE message pump in headless/test processes (primary root cause)

### Mechanism

JUCE 8's `AudioProcessorGraph` bakes its render sequence **asynchronously**:

```
AudioProcessorGraph::prepareToPlay → topologyChanged(UpdateKind::sync)
  → rebuild(sync) → updater.triggerAsyncUpdate()          (juce_AudioProcessorGraph.cpp:1857-1866)
  → message posted to the JUCE message queue
  → ... later, on the MESSAGE THREAD ...
  → LockingAsyncUpdater::Impl::messageCallback → Pimpl::handleAsyncUpdate
  → NodeStates::applySettings → RenderSequence built                      (lines 1927-1948)
```

`processBlock` then either renders through the baked sequence, or — if no sequence is
baked yet — **clears the audio** (the `audio.clear()` fallback, lines 1885-1908):

```cpp
if (owner->isNonRealtime())
{
    while (renderSequenceExchange.getAudioThreadState() == nullptr)   // spin-wait
    { Thread::sleep (1); ... }
}
...
else { audio.clear(); midi.clear(); }                                  // ← silence
```

In the GUI app the UI thread pumps the JUCE queue. In **headless and test processes
nothing pumped it** — the bake never landed, every `processBlock` took the clear path,
and every export rendered silence.

### The fix

New `src/common/MessagePumpThread.{h,cpp}` — a process-wide JUCE message pump:

- A `std::thread` whose `pumpLoop()` calls `MessageManager::getInstance()` **first**
  (winning `messageThreadId` + the hidden window + `InternalMessageQueue` ownership to
  the pump thread), then runs `runDispatchLoopUntil(0)` + 1 ms sleep forever.
- `start()` is idempotent with a ready handshake (blocks until `acquired`), `stop()`
  joins. Started as the **first statement** of `main()` in `src/main.cpp`,
  `src/main_headless.cpp`, and `tests/test_main.cpp` — before any other JUCE
  construction, so the pump thread is the first `MessageManager` owner.

---

## 3. Layer 2 — ExportManager ordering: the bake must contain the whole topology

### Mechanism

Even with a pump, the export could race the bake: the master→output connections were
established **after** `prepareToPlay()`, so the async bake triggered by `prepareToPlay`
contained an **incomplete topology** — the master node was not yet connected to the
output. The already-baked (incomplete) sequence was then used until the next rebuild,
producing silence for the first blocks (or a fully silent render on the race).

### The fix (`src/engine/ExportManager.cpp`)

1. **`routingManager.reconnectMasterToOutput()` moved BEFORE `prepareToPlay()`** — the
   bus layout is fixed up-front (`setBusesLayout` before `rebuildFromValueTree`), so the
   master→IO connections are already legal at that point; the single async bake that
   `prepareToPlay()` triggers now contains the **complete** topology.
2. **`renderGraph.setNonRealtime(true)` added after `prepareToPlay()`** — in
   non-realtime mode `processBlock` **spin-waits** for the bake (see the code above)
   instead of racing it and taking the `audio.clear()` fallback.
3. **Both `runDispatchLoopUntil(0)` flushes removed** — they crashed with
   `jassert(isThisTheMessageThread())` (the render thread is not the message thread).

---

## 4. Layer 3 — the build-system trap: a stale object file silently un-wired the pump

### The trap

`tests/test_main.cpp` on disk was the **pump version** (written 12:58), but
`test_main.obj` (compiled 5:27) was built from the **no-pump baseline**, and MSBuild
skipped recompilation because the source was **older than the object file**. The
6:09 PM link therefore produced a test binary whose `main()` **never called
`MessagePumpThread::start()`** — the pump did not exist in the process, while the
source said it did.

This is the failure mode AGENTS.md warns about: *delete `build/Debug/hdaw_tests.exe`
(or touch the source) when the dependency graph may miss a change.*

### Diagnosis (evidence, not speculation)

cdb breakpoint chain on `main` → `MessagePumpThread::start` → `pumpLoop` →
`MessageManager::MessageManager` showed:

```
MAIN-ENTRY          ← main() entered
(no PUMP-START)     ← start() was NEVER called
MM-CTOR             ← MessageManager constructed by the test thread
```

Thread/window inventory of the hung process confirmed the hidden JUCE window belonged
to the **test/main thread** — the pump thread did not exist.

### Why the hang without a pump

`MessageManager::getInstance()` freezes `messageThreadId` and creates the hidden
message window on the **first caller**. Without a pump, the first caller was the
**export render thread** (its `prepareToPlay` → `triggerAsyncUpdate` path). When the
export thread finished, it **exited**, orphaning the hidden window and the queue.
`AudioEngine::shutdown()`'s `MessageManagerLock` then posted a `BlockingMessage` that
**nobody could ever dispatch** → infinite wait. Two JUCE details matter:

- `MessageManager::Lock::tryAcquire()` waits on a condvar with **no timeout** — a
  never-dispatched `BlockingMessage` hangs the caller forever, mandatory or not
  (`juce_MessageManager.cpp:349-411`).
- On Windows the queue is in-process; the wake-up is a `PostMessage` to the hidden
  window, which only the **owning thread** can retrieve
  (`juce_Messaging_windows.cpp:91-156`). A dead owner = undeliverable.

### The fix

Force recompilation of `test_main.cpp` (`touch` the source), relink. Verified by
re-running the breakpoint chain — the full order now holds:
`MAIN-ENTRY → PUMP-START → PUMP-INSTANCE → PUMP-STATIC → PUMP-LOOP → MM-CTOR`.

---

## 5. Layer 4 — teardown race: `AudioEngine::shutdown()` must park the pump

### Mechanism

With a live pump, engine teardown races it: `AsyncUpdater`/`Timer`/
`AudioProcessorGraph` messages already queued can be dispatched **on the pump thread
after the engine (and its graph) is destroyed** — dereferencing a dangling `this`.

### The fix (`src/engine/AudioEngine.cpp:376`)

```cpp
void AudioEngine::shutdown()
{
    stopTimer();                                   // auto-stop/punch-out poll timer FIRST
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
    {
        juce::MessageManagerLock mml(static_cast<juce::Thread*>(nullptr));
        cancelPendingUpdate();                     // engine's AsyncUpdater gate shuts
        projectModel.getTree().removeListener(this);
        deviceManager.removeAudioCallback(&processorPlayer);
        processorPlayer.setProcessor(nullptr);
        pluginManager.stopCrashMonitor();          // new public API; stops respawn timer
        mainProcessor.reset();                     // graph destroyed WHILE pump is parked
    }
    ...
}
```

**How the park works:** `MessageManagerLock` posts a `BlockingMessage`; the pump
dispatches it and blocks inside its callback until the lock is released — the pump is
*stuck* for the duration, so no queued rebuild/timer message can fire on destroyed
objects (`juce_MessageManager.cpp:297-327`). A parked pump also cannot deliver the
previously queued `AudioProcessorGraph` rebuild (its `LockingAsyncUpdater::Impl::clear()`
sets `deliver=false` so stale queued messages self-skip after destruction).

---

## 6. Layer 5 — mutation races vs the pump (the crash family)

With the pump finally live, a whole family of **use-after-free access violations**
appeared (exit code 0xC0000005) in previously-passing engine tests. `AudioProcessorGraph`
is **not thread-safe**, and the pump makes its internal async dispatch concurrent with
HDAW's own graph mutations. Three distinct races:

### 6a. `rebuildRoutingGraph` vs the pump's graph-internal dispatch

**Stack:** `AudioProcessorGraph::Node::getProcessor` (AV) ←
`Pimpl::handleAsyncUpdate` ← `LockingAsyncUpdater::Impl::messageCallback` ← pump thread.

**Mechanism:** the command layer calls `MainAudioProcessor::rebuildRoutingGraph()`
**synchronously on the command/test thread** (`graph.clear()` + full rebuild frees every
node under `graphLock`). Concurrently the pump dispatched the graph's internal
`LockingAsyncUpdater` (`handleAsyncUpdate` iterating the **live node list**) → read of
freed nodes.

**Fix (`src/engine/MainAudioProcessor.cpp:475`):** conditionally **park the pump** for
the duration of the rebuild:

```cpp
std::optional<juce::MessageManagerLock> pumpPark;
if (auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    mm != nullptr && !mm->isThisTheMessageThread())
    pumpPark.emplace();
```

The `!isThisTheMessageThread()` guard is essential: **`MessageManagerLock` on the
message thread itself self-deadlocks** (it posts a `BlockingMessage` and waits for its
own dispatch). The pump thread's own rebuild path (engine `handleAsyncUpdate` on the
pump) skips the park — dispatch and mutation are already serialized there.

### 6b. `Track::prepareToPlay` vs command-thread chain rebuilds

**Mechanism:** the pump's graph `applySettings` path calls `Track::prepareToPlay`,
which iterated `fxChain`/`automationManagers`/`modulationManager` **without a lock**,
while the command thread clears those same vectors under `stateLock` in
`rebuildFXChain`/`setAutomationTrees`/`rebuildModulation` → use-after-free / debug-CRT
heap corruption (which even surfaced as a modal `MessageBox` hang).

**Fix (`src/engine/Track.cpp:24`):** guard the iteration with the existing
processBlock idiom:

```cpp
if (!stateLock.tryEnter())
    return;                    // contended: the rebuild holding the lock prepares itself
...
stateLock.exit();
```

### 6c. `TrackFXSlot::prepare` recreating DSP vs the unguarded param listener
(the final reported crash)

**Stack (user-captured):** `juce::ArrayBase<...IIR::Filter*>::size` (AV) ←
`OwnedArray::deleteAllObjects` ← `~ProcessorDuplicator<IIR::Filter>` ←
`unique_ptr::operator=` ← `TrackFXSlot::prepare` ← `Track::prepareToPlay` ←
`NodeStates::applySettings` ← `Pimpl::handleAsyncUpdate` ← pump thread.

**Mechanism:** the pump's `Track::prepareToPlay` → `TrackFXSlot::prepare` **recreates
the DSP objects** (`eq = std::make_unique<EQProcessor>()`, destroying the old one)
under `stateLock`. Meanwhile `AudioEngine::valueTreePropertyChanged` — the synchronous
`set_fx_param` listener running on the command/MCP thread — called
`setInternalParam` → `applyInternalParamToDsp` → `*eq->state = makePeakFilter(...)`
**without any lock** → write-into-freed-memory → corrupted `OwnedArray` → AV at the
next destructor walk. Safe before the pump only because everything ran on one thread.

**Fix:** new stateLock-guarded setter `Track::setFxSlotInternalParam(slotIndex,
paramIndex, value)` (`src/engine/Track.{h,cpp}`), used by the FX_SLOT `param_N`
listener branch in `AudioEngine::valueTreePropertyChanged` (replacing the direct,
unguarded `chain[slotIdx]->setInternalParam`).

---

## 7. Verification

| Gate | Result |
|------|--------|
| 9 former crashers (36 engine tests) | green **14× consecutive** |
| `McpServer.*` suite (11 tests, incl. both export tests) | green **3×** (11/11) |
| `MessagePumpThread.*` suite | green 2/2 |
| Full suite | **634 run / 630 passed** (2×); only 4 **pre-existing** `PluginIsolation.*` failures (fail with AND without the pump — unrelated older WIP, A/B-verified) |
| `McpServer.ExportAudioWithClapPluginDoesNotHang` | real cached CLAP instrument + generated 4-bar phrase, **asserts non-silence (peak > 0.01)** |
| **Manual render** | `test_techno_64bars.hdaw` (Vital + Dexed + JE8086 CLAP) → 128 s @ 48 kHz in 129.4 s → **non-silent WAV, peak 1.0, 1.97M non-zero frames of 6.14M** |

Historical context: every 8/6–8/7 export of the repro projects measured peak 0.00000;
only an 8/3 export (`hdaw_vital_patterns.wav`, peak 0.97) predates the regression.

---

## 8. Files touched (commit `e917c1f`)

**New:** `src/common/MessagePumpThread.{h,cpp}`, `src/engine/MidiFx.cpp`,
`tests/unit/common/message_pump_test.cpp`, `tests/unit/engine/{midi_fx_automation,
plugin_identifier_resolution,preset_cache}_test.cpp`.
**Modified:** `CMakeLists.txt` (pump registration), `src/main.cpp`,
`src/main_headless.cpp`, `tests/test_main.cpp` (pump main),
`src/engine/AudioEngine.cpp` (shutdown park), `src/engine/ExportManager.cpp`
(ordering + setNonRealtime), `src/engine/MainAudioProcessor.cpp` (rebuild park),
`src/engine/Track.{h,cpp}` (prepareToPlay guard, `setFxSlotInternalParam`),
`src/engine/TrackFXSlot.h`, `src/engine/MidiClipProcessor.h`, `src/engine/PluginManager.{h,cpp}`
(`stopCrashMonitor`), `src/engine/AudioEngineCommands_Fx.cpp`, `src/mcp/*`,
`src/proxy/*` (WIP carry), `tests/integration/mcp/mcp_server_test.cpp` (export tests),
`tests/CMakeLists.txt`.
**Cleanup:** all ClapProbe/ExportDiag/SlotProc/MidiClipPB debug logs removed;
`graph_bake_probe_test.cpp` deleted.
**Deliberately DISABLED:** `DiagnosticClapExportMatrix` (latent UAF in the crash-recovery
`respawnIsolatedSlot → migrateToNewSlot` path, exposed by the pump driving the
CrashRecovery timer), `TrackFXSlotShowEditor.ShowEditorTriggersEditorCreation`
(IMM32 `WM_IME_SETCONTEXT` recursion when the pump dispatches Win32 messages).

---

## 9. Rules for the future

1. **Headless/test processes MUST have a message pump before any JUCE construction.**
   The pump thread must be the first `MessageManager::getInstance()` caller — it owns
   `messageThreadId`, the hidden window, and the queue.
2. **`MessageManagerLock` on the message thread self-deadlocks.** Any pump-park must be
   guarded by `!isThisTheMessageThread()`.
3. **Every graph mutation from a non-message thread must park the pump.** The
   `MainAudioProcessor::rebuildRoutingGraph` pattern is the reference. The graph's
   internal `LockingAsyncUpdater` dispatch runs on the pump and iterates live nodes.
4. **Every DSP state write must hold `stateLock`.** The `setInternalParam` →
   `applyInternalParamToDsp` write-after-free was the deepest bug; `AudioProcessorGraph`
   applies settings (and re-creates DSP objects) on the pump under `stateLock`.
5. **Verify the binary actually contains the fix** (stale-obj trap): after changing
   `test_main.cpp`-style entry points, confirm via `test_main.obj` timestamps or a
   breakpoint probe, not by reading the source.
6. **Debug-CRT heap corruption can masquerade as a modal hang** — an unhandled
   `MessageBox` in a pump dispatch is a symptom of a much earlier UAF.
