# HISE-Derived Features for HDAW — Master Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the transferable capabilities from HISE-4.1.0's sampler/streaming stack into HDAW, in priority order — each capability is a scoped subsystem with its own dedicated implementation plan. This document is the roadmap; it scopes each subsystem, records current-state findings, ranks priority, and names the follow-on plans.

**Architecture:** HDAW already owns most of the hard seams these features depend on: a block-boundary atomic buffer-swap idiom (`ClipSourceProcessor::activeBuffer`), a preload-to-memory audio clip path, a non-realtime export render graph, a tagged file logger, and a deltas-vs-fullSync tree watcher. The HISE work is therefore **additive**, slotting into existing seams rather than re-architecting: a background reader thread feeds a double buffer behind the playhead (streaming), a DebugLogger-style instrumentation layer wraps the existing logger, and a sound-pool refcounts shared files. Three subsystems are real work (streaming, instrumentation, pool dedup); the rest are already solved differently, verification items, or out of scope.

**Tech Stack:** C++17 (JUCE 8), Qt JSON for RPC, React 19 + TypeScript + Zustand, gtest, Vitest, Playwright. Reference material: HISE-4.1.0 sources at `D:\pdf\HISE-4.1.0`.

---

## Current-state snapshot (verified 2026-08-13)

| Capability | HDAW today | Verdict |
|-----------|-----------|---------|
| Disk streaming for audio clips | `ClipSourceProcessor` reads the **entire** file into two `HeapBlock<float>` on `prepareToPlay` / `switchToSourceFile` (`src/engine/ClipSourceProcessor.h:30-56,122-158`) — memory = `length × 4B × channels`, unbounded for long/large clips | **Missing — P0** |
| Realtime-safety instrumentation | `DebugLog.h` is a tagged file logger (`HDAW_LOG`, tag filter, mutex); no NaN/DC check, no glitch detector, no priority-inversion assert, no `AudioThreadGuard` (`src/common/DebugLog.h:92-115`) | **Missing — P0** |
| Audio pool dedup / refcount | `ProjectPool` is just `AudioFormatManager` + thumbnail cache (`src/engine/ProjectPool.h:10-35`). Same file referenced by 2 clips is decoded **twice** into two buffers. `FileLibraryManager` is the media browser, separate concept. No hash pool, no refcount | **Missing — P1** |
| Non-realtime export streaming | JUCE side done: `renderGraph.setNonRealtime(true)` (`src/engine/ExportManager.cpp:219`). Loader side (read-synchronously-during-export) missing — only relevant once streaming lands | **Partial — P1** |
| Automation/gain-envelope lookup tables | Already solved **better** than HISE: block-level snapshot + incremental walking segment index (`ClipSourceProcessor.h:300-414`) — one lock per block, no per-sample binary search. HISE's per-sample `getGainAtTime` + `EnvelopeTable` (`ModulatorSamplerSound.h:544-666`) is not an improvement | **Already done** |
| Delta vs fullSync coalescing | `TreeDeltaAccumulator` 16 ms debounce → minimal delta vs fullSync (AGENTS.md). Same idea as HISE `Notifier::Collector` (`ModulatorSamplerData.h:336-430`) | **Already done** |
| Thumbnails-are-playback-contracts | Policy documented in AGENTS.md; `StretchCache` ensures the played buffer is the stretched buffer | **Verify only** |
| Gain-staging multiply chain | Sampler voices already render at unity per-voice (design spec, `2026-08-13-internal-sampler-design.md`) | **Verify only** |
| Precomputed loop crossfades | Sampler loop hard-switches at `loopEnd` → potential click. HISE precomputes equal-power crossfade buffers | **Missing — P2** |
| RoundRobinMap / monolith / HLAC | Multisample + monolith are explicitly out of scope (YAGNI in sampler design) | **Out of scope** |
| JUCE discipline (vendored diffs, extend `var`) | JUCE 8 via CMake; not a fork. The HISE `AudioThreadGuard` patch is a JUCE-fork decision | **Not applicable** |

---

## Priority & dependency order

```
P0.1 Disk streaming (audio clips)          ← the unbounded-memory problem; everything else slots into it
P0.2 Realtime-safety instrumentation        ← cheap, catches regressions in ALL future engine work
P1.1 Audio pool dedup                       ← memory + load-time win; pairs naturally with streaming
P1.2 Non-realtime export streaming          ← blocked by P0.1 (needs the streaming loader)
P2   Sampler loop crossfade precompute      ← quality item, independent
P2   Thumbnail-contract verification        ← audit only
Done Verifications (#5,#6,#8)               ← document as no-op; no code
```

Dependencies: P1.2 requires P0.1's `SampleLoader`/`StreamingClipSource` to exist. P1.1 is independent of P0.1 (works on the existing preload path) but shares the "single decode, many consumers" idea — implement P1.1 after P0.1's buffer abstraction so both can share a `DecodedSoundPool`. P0.2 is fully independent — can land first if preferred.

---

## Subsystem plans (one dedicated plan per subsystem)

### Subsystem A — Disk streaming for audio clips (P0.1)

**Problem:** `ClipSourceProcessor` preloads the whole file into memory. A 1 GB audio file costs 4+ GB of heap (2 × `HeapBlock<float>`, 4 B/sample/ch). Long recordings, film mixes, or multi-gig libraries make the arrange view a memory cliff.

**Approach (adapted from HISE `StreamingSamplerVoice.cpp:39-615` `SampleLoader`):**
- **Preload head + double-buffer behind the playhead.** Read the first `PRELOAD_SIZE` samples synchronously at load; a **background reader thread** (`juce::Thread` or the existing `TimeSliceThread` pattern from `AudioRecorder.h:24`) keeps the buffer ahead of the read position. Double buffering with atomic swap at the block boundary reuses the existing `activeBuffer` idiom (`ClipSourceProcessor.h:226`).
- **Buffer-size contract** (HISE `StreamingSampler.h:233`): each buffer ≥ `samplesPerBlock × MAX_SAMPLER_PITCH` so a pitch-shifted read never outruns the filled window. HDAW has no pitch on clip sources (unity ratio), so `MAX_SAMPLER_PITCH ≈ 1.0` initially — the contract still governs future timestretch-read paths.
- **Pitch-aware whole-file promotion** (HISE `StreamingSamplerVoice.cpp:897`): if the clip is short enough to fit memory entirely, promote to whole-file preload (current behavior) instead of streaming — same code path, bounded memory for the common case.
- **Starvation contract** (HISE `:1147`): if the reader cannot keep up, the clip renders silence for the starved window (never blocking the audio thread, never asserting) and logs via the P0.2 instrumentation.
- **16-bit buffers** (HISE `:897`): the streaming path stores `int16` (halves memory vs `float`); conversion to `float` happens in `processBlock` — HDAW already has this exact pattern for the stretched path (`ClipSourceProcessor.h:276-280`).
- **Voice-count-guarded file handles + background unmapping** (HISE `Unmapper`/`ScopedFileHandler` `:1290`): open readers only while a clip is audible; close on a background thread so the audio thread never touches the filesystem.

**Component:** new `StreamingClipSource` (or `SampleLoader` + adapter into `ClipSourceProcessor`) under `src/engine/`, mirroring the `SamplerEngine` pattern: standalone DSP, no ValueTree dependency.

**Success gates (for the dedicated plan):**
- G1: gtest streaming suite — preload-head covers the first block; background fill keeps ahead; buffer swap is block-boundary-atomic; starvation yields silence (not a crash); short files promote to whole-file preload.
- G2: no allocation/lock/I-O on the audio thread (realtime review, `audio-dsp-review`).
- G3: playback bit-identical (or within 1 LSB) to the current whole-file preload path for the same file — regression gate.
- G4: full engine suite + export produces identical output to pre-streaming for a project with audio clips.
- G5: memory ceiling: a large-file project streams instead of preloading (assert via a memory/`getDiskUsage`-style meter or reader-open-count).
- G6: version bump + graph refresh.

**Pitfall gates:** Gate 3 (audio-thread safety — the reader thread is message-thread-side, processBlock reads swapped buffers), Gate 12 (no graph mutation), Gate 14 (if any cross-process: N/A — no plugin isolation involved), Lesson 8 (quality: 16-bit truncation vs float preload — verify no audible degradation; gate G3).

**Dedicated plan:** `docs/plans/2026-08-13-clip-disk-streaming.md`

### Subsystem B — Realtime-safety instrumentation (P0.2)

**Problem:** Nothing checks that the audio path is actually safe. The 16 lessons in AGENTS.md are all *reactive* fixes; there is no tripwire that fires when a future change reintroduces allocation/locking/NaN/DC on the audio thread.

**Approach (adapted from HISE `DebugLogger` — `DebugLogger.h:191` `checkSampleData`, `:320` `CHECK_AND_LOG_BUFFER_DATA`, `:529` impl, `:626/:643` `checkPriorityInversion`; `UtilityClasses.h:375` `ADD_GLITCH_DETECTOR`):**
- **`checkSampleData`** — debug-build-only scan of every `processBlock` output buffer for NaN/±Inf and DC-offset growth; on detection, `HDAW_LOG("RT", ...)` with block index and track id. Wrapped in a macro so it compiles out in release. Hooks: `Track::processBlock`, `MasterBusProcessor::processBlock`, `ClipSourceProcessor::processBlock`, sampler `render`.
- **`ADD_GLITCH_DETECTOR`** — per-block wall-clock measurement (debug builds); any block exceeding a multiple of the block duration (e.g. > 4×) logs a glitch with the responsible node/track. Cheap `Time::getMillisecondCounterHiRes` delta, no allocation.
- **Priority-inversion assert** — a flag set when the audio thread acquires a `SpinLock::tryEnter` *after* failing it in the same block (or when a realtime lock waits), plus a `SpinLock`-friendly wrapper that logs "audio thread blocked on lock" — this catches the lesson-13 class live.
- **`AudioThreadGuard`** — RAII guard (message-thread-style) that asserts the current thread is/is-not the audio thread; instrument `prepareToPlay`/`releaseResources`/rebuild paths. Do NOT fork JUCE: HISE's guard lives in a 1594-line JUCE patch (`JUCE/working608_patch.diff`); HDAW's equivalent is a lightweight HDAW-side guard asserting against the recorded audio-thread id (the same technique `CLAPHost::audioThreadId` uses — lesson 19).

**Component:** `src/common/RealtimeGuard.h` + `src/common/BufferCheck.h` (or extend `DebugLog.h`), compile-gated by `JUCE_DEBUG`/`HDAW_DEBUG`.

**Success gates:**
- G1: gtest — injected NaN / injected lock-block in a test processor trips the detector; a clean block does not.
- G2: instrumentation compiles out in release (no symbol/overhead — verify via macro guard).
- G3: full engine suite passes with instrumentation ON (no false positives in the existing 250+ tests).
- G4: a glitch/NaN produces a `HDAW_LOG("RT", ...)` line (assert log content in a test).
- G5: version bump + graph refresh.

**Pitfall gates:** Gate 3 itself (the instrumentation must not allocate/lock on the audio thread — measurement uses atomics + preallocated state, logging is fire-and-forget flag → message-thread drain), Lesson 17 (log somewhere visible — the drain writes via `HDAW_LOG`/OutputDebugString).

**Dedicated plan:** `docs/plans/2026-08-13-realtime-safety-instrumentation.md`

### Subsystem C — Audio pool dedup / shared decodes (P1.1)

**Problem:** `ProjectPool` decodes a file every time a clip (or the sampler) references it. Two clips of the same long sample = two full decodes + two buffers. Deleting a clip never frees the decode while a sibling clip still uses it.

**Approach (adapted from HISE `ModulatorSamplerSoundPool` — `ModulatorSamplerSound.h:757/:766`):**
- A `DecodedSoundPool` keyed by **file path + (later) hash + sample-rate** returns a `shared_ptr<const DecodedSound>`; refcount is the shared_ptr. `ClipSourceProcessor::prepareToPlay` and the sampler's `SamplerSound::Builder` both request through the pool.
- Pool eviction: LRU on idle (no clip currently referencing it and memory pressure), message-thread only.
- This is the natural foundation for the **Audio Pool / ProjectPool enhancement** the sampler design deferred (`2026-08-13-internal-sampler-design.md:201`: "Pool-relative paths are a future ProjectPool enhancement").
- Pairs with Subsystem A: when streaming lands, the pool hands out the streaming `SampleLoader` handle for a file so two clips stream from **one** set of file handles instead of opening per clip (HISE's voice-count-guarded handle sharing).

**Success gates:**
- G1: gtest — two clips referencing the same file share one `DecodedSound` (assert `shared_ptr` equality / decode-count == 1); refcount drops when the last clip is removed; pool evicts only unreferenced entries.
- G2: sampler + clip both load via the pool; state survives `rebuildRoutingGraph` (lesson 10 — the pool entry is reacquired, not re-decoded).
- G3: no audio-thread calls into the pool (all decode/evict on message thread).
- G4: full engine suite; version bump; graph refresh.

**Pitfall gates:** Gate 1/10 (rebuild must reacquire from pool without re-decode — test asserts decode-count), Gate 3 (audio thread never touches pool), Gate 6 (interactive correctness ≠ rebuild correctness).

**Dedicated plan:** `docs/plans/2026-08-13-audio-pool-dedup.md`

### Subsystem D — Non-realtime export streaming (P1.2)

**Problem:** Once Subsystem A streams clips, an offline export that fast-forwards/renders must not rely on the background reader racing the render thread.

**Approach (HISE `StreamingSamplerVoice::setIsNonRealtime` `:139`):** the loader exposes `setNonRealtime(true)`; when set, the reader **synchronously reads the whole needed range** on the render thread during `prepareToPlay`, bypassing the background reader. `ExportManager` already calls `renderGraph.setNonRealtime(true)` (`ExportManager.cpp:219`) — this subsystem propagates that flag to the clip/loader so export renders from stable, fully-resident buffers.

**Success gates:**
- G1: gtest — with non-realtime set, the loader reads synchronously (no background thread), render is bit-identical to whole-file preload.
- G2: existing export tests pass; a project with a streamed long clip exports correctly.
- G3: version bump + graph refresh.

**Pitfall gates:** Gate 11 (render thread must run on a pump context — already handled in `ExportManager`), Lesson 16-adjacent (thread-checking: the render thread IS the sanctioned thread for this path).

**Dedicated plan:** `docs/plans/2026-08-13-export-non-realtime-streaming.md` (blocked on Subsystem A).

### Subsystem E — Sampler loop crossfade precompute (P2)

**Problem:** Sampler loop hard-switches at `loopEnd` (`SamplerVoice`), audible click on non-zero-crossing loops.

**Approach (HISE precomputed equal-power crossfade buffers):** precompute a short crossfade (e.g. 5–10 ms) region straddling `loopEnd`→`loopStart` on the message thread at load time, stored on `SamplerSound`; the voice blends across it on each loop pass. No audio-thread allocation — the crossfade table is part of the immutable sound.

**Success gates:**
- G1: gtest — loop crossfade continuity: sample immediately before `loopEnd` blends to `loopStart` without discontinuity (assert no >X dB jump at the wrap).
- G2: realtime review clean; existing sampler suite passes.

**Dedicated plan:** `docs/plans/2026-08-13-sampler-loop-crossfade.md`

### Verification-only items (no code, documented here)

- **Gain staging (#8):** sampler voices render at unity per-voice (design spec); track volume/pan/meter downstream. Verify with a numeric review of `SamplerVoice` — no action expected.
- **Automation lookup tables (#5):** `ClipSourceProcessor` block-snapshot + walking index already beats HISE's per-sample `getGainAtTime`. If Track-level automation ever shows per-sample overhead, revisit with a downsampled lookup table — not now.
- **Delta vs fullSync (#6):** already implemented; no action.
- **Thumbnail contract (#7):** audit that audio clip thumbnails reflect stretch/reverse/gain-envelope transforms (AGENTS.md policy) — E2E assertion only.

---

## Execution order & handoff

1. **Subsystem B (instrumentation)** — independent, cheap, protects all subsequent work. Can start immediately.
2. **Subsystem A (disk streaming)** — the headline feature. The `DecodedSoundPool` abstraction from C should be designed in the same plan so the loader/pool share decode ownership.
3. **Subsystem C (audio pool dedup)** — natural with A; may be folded into A's plan if the pool is the shared foundation, else its own.
4. **Subsystem D (export non-realtime)** — after A.
5. **Subsystem E (crossfade)** — independent, anytime after the sampler is stable.

Each dedicated plan is produced with `writing-plans` (task-by-task, TDD, per-task success gates) and passes `hdaw-guard` pre-flight before dispatch. All implementation runs in subagent tasks per hdaw-guard §Execution Model.

## Dependency Map

- **Blast radius:** `ClipSourceProcessor` (streaming), `RoutingManager`/`Track` (buffer adoption on rebuild — Gates 1/10), `ExportManager` (non-realtime propagation), `ProjectPool`/sampler load path (pool), `DebugLog` (instrumentation), engine test suites (25+ suites, 250+ cases), MCP/RPC surfaces unchanged (no new user-facing command in any subsystem — MCP parity is N/A; the features are internal engine improvements).
- **Upstream:** `RoutingManager::addTrack`/rebuild creates `ClipSourceProcessor` instances (`RoutingManager.h:98` `audioClipNodes`); `TrackFXSlot` sampler variant owns `SamplerEngine` (`TrackFXSlot.h:397`); `ExportManager::renderThreadFunc` builds a standalone `RoutingManager`.
- **Downstream:** `processBlock` consumers of clip audio; export pipeline; sampler render.
- **Projections affected:** none new — clips/tracks unchanged entity types (delta/fullSync behavior unchanged).
- **SPSC paths:** streaming buffer swap = new message→audio atomic handoff (same shape as `activeBuffer`); instrumentation = audio→message flag drain (new, one-way, atomic).
- **God nodes in scope:** `RoutingManager`, `ClipSourceProcessor` (both high-degree). Treat as elevated-risk in each dedicated plan.
- **Path integrity:** each dedicated plan must trace the full chain (load → buffer → swap → audio read → observable playback) with `trace_path` before dispatch.

## Pitfall Gates Triggered (all subsystems)

- **Gate 1/6/10 (rebuild state restore):** streamed/pooled buffers must be reacquired on `rebuildRoutingGraph`, not re-decoded — test asserts decode-count/reader-count after rebuild.
- **Gate 3 (audio-thread safety):** the streaming reader, pool eviction, and instrumentation drain all run off the audio thread; `processBlock` only reads swapped buffers / atomics.
- **Gate 8/11/12/13/14/16:** N/A (no CSS, no new entry points, no graph mutation, no DSP-state writes, no cross-process, no plugin lifecycle).
- **Lesson 8 (quality):** 16-bit streaming buffers vs float preload must not audibly regress (Gate G3 in Subsystem A).
- **Lesson 15 (stale `.obj`):** new `.cpp` files added to CMake source lists; verify binary timestamps.
- **Lesson 17 (visible logs):** instrumentation output must reach `HDAW_LOG`/OutputDebugString, never be swallowed.

## Anti-Pattern Scan

- No N-call RPC loops (no new RPCs). No full-tree walks (streaming/pool are per-clip/file). No `DBG` macro. No raw hex. No audio-thread allocation/locking/I-O in any new code path. No `rebuildRoutingGraph()` per-clip.