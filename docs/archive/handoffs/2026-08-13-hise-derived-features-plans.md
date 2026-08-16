# HISE-Derived Features — Plans Written, Ready to Execute (Continuation Prompt)

> This file is a **paste-able continuation prompt** for a fresh agent context.
> Copy everything from the `## CONTINUATION PROMPT` heading to the end, paste it
> into the new session, and the new agent will have full context to continue.
> See `docs/plans/2026-08-13-hise-derived-features-master-plan.md` for the
> roadmap this sits under.

---

## CONTINUATION PROMPT

You are resuming a planning task on the **HDAW** JUCE 8 desktop DAW at
`D:\pdf\roo projects\hdaw3`. A previous session researched HISE's clip
streaming, realtime-safety instrumentation, audio-pool dedup, non-realtime
export streaming, and sampler-loop crossfade code, then wrote a master roadmap
and TWO detailed TDD plans. **No engine code has been written yet** — the plans
are complete and self-reviewed, and the next step is to execute them.

### Mandatory first action

Read and follow `AGENTS.md` at the repo root (especially the Lessons Learned,
Performance rules, MCP feature parity, and testing sections). Invoke the
`hdaw-guard` skill before ANY code change:

```
skill: "hdaw-guard"
```

Also read the referenced docs before touching the relevant area:
`docs/architecture.md` (beats-vs-seconds convention!), `docs/realtime-safety.md`,
`docs/pitfalls-juce.md`, `docs/valuetree-listener-contract.md`.

### Where things stand

**Plans written and self-reviewed (all three):**
1. `docs/plans/2026-08-13-hise-derived-features-master-plan.md` — roadmap:
   Subsystem A = clip disk streaming, B = realtime-safety instrumentation,
   C = audio-pool dedup, D = non-realtime export streaming, E = sampler loop
   crossfade. Execution order **B → A → C → D → E** (B is independent; D is
   blocked on A). Priorities: streaming/instrumentation **P0**, pool **P1**,
   export NR **P1**, crossfade **P2**.
2. `docs/plans/2026-08-13-clip-disk-streaming.md` — Subsystem A, 5 tasks, TDD,
   success gates G1–G8, dependency map. **Fully self-reviewed to a
   position-driven contract** (see below).
3. `docs/plans/2026-08-13-realtime-safety-instrumentation.md` — Subsystem B,
   5 tasks, TDD, gates G1–G6. Self-review done (a `getSampleRate()` note was
   corrected: `AudioProcessor::getSampleRate()` is public/inherited, no member
   needed).

The internal sampler these plans reuse (the shipped `fxType="sampler"` from
0.21.0) is fully implemented; see
`docs/handoffs/2026-08-13-internal-sampler-continuation.md` if you need its
state.

### Key design decisions already made (do not re-litigate)

For the streaming plan (Subsystem A):
- **Position-driven contract.** `StreamingClipSource::readNextBlock(out,
  sourcePos)` — the caller passes the exact source sample position; the
  streamer NEVER auto-advances an internal read pointer. This makes seeks,
  loops, and clip offsets all work. `readPos_` was deliberately removed.
- **Sliding-window double buffer**, NOT a ring buffer. Each side stores a
  contiguous `[base, base+fill)` window published via `baseA_/baseB_/fillA_/fillB_`
  atomics before `activeBuffer_` flips. Never index by `pos % bufLen` (that
  yields a zero-size window at the wrap boundary — the original bug).
- **`startPlayback` prefill:** side 0 at position 0 is filled synchronously in
  BOTH realtime and non-realtime modes so the first blocks never starve.
- **`setNonRealtime(true)` stops and joins the background reader thread** (no
  dual writers); non-realtime `readNextBlock` refills the active side
  synchronously on the export thread (deliberate — export is offline).
- **Starvation → silence + `starvedCount_`** (never blocks/asserts; matches
  HISE's contract). Instrumentation (Subsystem B) can drain the counter.
- **`ClipSourceProcessor` integration:** `activeBuffer == 2` = streamer source.
  `prepareToPlay` REPLACES the whole-file preload for long files (streaming OR
  preload, never both — that's the double-read memory bug). The streaming
  branch goes in the FILL block at `ClipSourceProcessor.h:264-283` (AFTER the
  audible `numToRead` clamp at `:256-289`), NOT in the buffer-selection block
  at `:226-246` — placing it there gets its output wiped by the
  `buffer.clear(); return;` at `:285-289`.
- **`fillBuffer` clamps `available` to ≥ 0** and zeroes the tail beyond a
  partial fill so a side's stale data is never audible; the audio-thread copy
  loop guards `off + s < fill`.
- **Test escape hatch:** `ClipSourceProcessor::setStreamingEnabled(bool)` (default
  ON) forces the legacy preload path — the E2E reference uses it.
- **Export wiring:** `ExportManager.cpp::renderThreadFunc` already calls
  `renderGraph.setNonRealtime(true)` at `:219`. Add
  `routingManager.setClipSourcesNonRealtime(true)` right after; the method
  iterates `audioClipSources` (`std::map<std::pair<int,int>, ClipSourceProcessor*>`,
  `RoutingManager.h:99`). `ClipSourceProcessor::setNonRealtime` stores the flag
  AND pushes to the live streamer immediately (the export graph is already
  built — a stored-flag-only route is too late).
- **Test conventions to follow:** transport is advanced via
  `tm.setCurrentSample(x)` (see `tests/unit/common/commands_test.cpp:67`); a
  fresh processor has `duration` 0 → `processBlock` clears early (silence).
  Realtime background-read tests need pacing (`juce::Thread::sleep(2)` every
  ~8 blocks) so the reader thread is scheduled.
- The plan's class reference implementation and all tests are written to the
  position-driven contract — read them before writing code.

### What's left to do

**Immediate next step (in this new context):**
1. Confirm you can build: `cmake --build build --config Debug` and run
   `build\Debug\hdaw_tests.exe` (baseline green before any change).
2. Execute Subsystem **B (realtime-safety instrumentation)** first — it is
   independent, smallest blast radius, and gives the streaming work its
   diagnostics. Implement task-by-task per its plan (5 tasks), using
   `subagent-driven-development` or `executing-plans`.
3. Then execute Subsystem **A (clip disk streaming)** per its plan (5 tasks).
4. If you reach them, C (audio-pool dedup) and D (non-realtime export
   streaming) pair with A; E (sampler crossfade) last.

**Per-task discipline (from the plans and AGENTS.md):**
- TDD: write the failing test first, watch it fail, implement, watch it pass.
- Every engine change: identify affected gtest suites and update/add tests; run
  `build\Debug\hdaw_tests.exe` (250+ tests, ~35 suites).
- Every audio-engine change: evaluate latency AND quality/fidelity explicitly
  (lessons 7/8). The streaming plan's gates G4/G5 encode this.
- MCP parity: any user-facing capability must also be an MCP tool (streaming is
  internal, so N/A; if you add anything user-facing, wire it).
- Version bump on completion: `CMakeLists.txt` AND `frontend/package.json` in
  sync (0.21.0 → 0.22.0).
- Refresh the knowledge graph after structural changes:
  `codebase-memory` → `index_repository` (project `D-pdf-roo-projects-hdaw3`,
  mode `full` when new files/RPC methods were added).
- Commit per task with `git add <explicit paths>` only — do NOT sweep the
  dirty tree. Note: recent work is on feature branches (e.g. the sampler's
  `feat/internal-sampler`); use the `finishing-a-development-branch` skill
  before merging.

### Key files

- `docs/plans/2026-08-13-hise-derived-features-master-plan.md` — roadmap
- `docs/plans/2026-08-13-clip-disk-streaming.md` — Subsystem A (execute 2nd)
- `docs/plans/2026-08-13-realtime-safety-instrumentation.md` — Subsystem B (execute 1st)
- `src/engine/ClipSourceProcessor.h` — streaming adoption seam (preload
  `:122-162`, buffer selection `:226-246`, audible clamp `:256-289`, envelope
  loop `:341-414`, `setSourceFile` `:25`, `switchToSourceFile` `:30-56`)
- `src/engine/RoutingManager.h` / `.cpp` — `audioClipSources` map (:99),
  `rebuildClipsForTrack` (:507), export build path
- `src/engine/ExportManager.cpp` — render loop, `setNonRealtime(true)` at `:219`
- `tests/unit/engine/clip_streaming_e2e_test.cpp` (new) and
  `tests/unit/engine/streaming_clip_source_test.cpp` (new) — created per plan
- HISE references: `D:\pdf\HISE-4.1.0\hi_streaming\hi_streaming\StreamingSamplerVoice.cpp`
  (`SampleLoader` :39-615, `setIsNonRealtime` :139, starvation :1147, 16-bit :897),
  `hi_core\hi_core\DebugLogger.h/.cpp`, `hi_core\hi_core\UtilityClasses.h`

### Success gates (completion contract)

- All success gates in each plan checked off WITH evidence (test output, not
  intent).
- Full `build\Debug\hdaw_tests.exe` passes (no regressions).
- Latency + quality evaluated for every engine change; sample-exact
  streamed==preloaded within int16 quantization.
- Version bumped in both places; knowledge graph refreshed.
- Report back which gates you verified and the exact commands/outputs.

If a plan's step is ambiguous once you're reading the real code, resolve the
ambiguity against the design decisions above and the actual source — do not
silently change the contract. If a decision genuinely can't be made from the
plan, stop and ask rather than improvise.