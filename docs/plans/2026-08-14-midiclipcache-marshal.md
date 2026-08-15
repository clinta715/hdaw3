# rebuildMidiClipCache Marshal (live MIDI-clip cache invalidation race) Implementation Plan

> **For agentic workers:** MANDATORY: invoke the `hdaw-guard` skill before any
> code change. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the last documented live-processor destruction race — the seven
`AudioEngine` listener call sites that invoke
`RoutingManager::rebuildMidiClipCache` directly on the mutating thread — by
routing them through a marshaled `MainAudioProcessor::rebuildMidiClipCache`.

**Root cause (same family as `e68bfc3` / `43554e0`):** MIDI_NOTE / CC_POINT /
CC_LIST listener branches (AudioEngine.cpp:945,958,1070,1115,1170,1228,1238)
call `mainProcessor->getRoutingManager()->rebuildMidiClipCache(clipTree)` on
the listener's thread (command/RPC/MCP/undo/UI). That iterates the
RoutingManager's `midiClipSources` map and mutates each `MidiClipProcessor`
while the pump thread's async `rebuildRoutingGraph` can destroy the whole
RoutingManager — use-after-free.

**Fix:** add `MainAudioProcessor::rebuildMidiClipCache(juce::ValueTree)` that
marshals via the existing `runOnMessageThread` helper (MainAudioProcessor.cpp:76)
and re-looks-up `routingManager` INSIDE the callback; update the seven call
sites to `mainProcessor->rebuildMidiClipCache(clipTree)`. Not added to the
`AudioGraphCommands` virtual interface and not RPC/MCP-exposed — it is an
internal invalidation, not a user-facing capability (no MCP parity rule trip).

**Safety analysis:** identical to the two landed fixes — listener threads are
never the audio thread and never hold a pump park; `callFunctionOnMessageThread`
blocks, preserving synchronous caller semantics; `juce::ValueTree` capture by
value is refcounted/thread-safe and identity comparison inside
`RoutingManager::rebuildMidiClipCache` is unaffected.

**Second unit (independent chore, separate commit):** `.gitignore` — add
`/*.hdaw3` (root-scoped; the existing `/*.hdaw` line does NOT match `.hdaw3`)
and `/compositions/` for the stray test projects / audio test media (handoff §5).

**Tech Stack:** C++17 (JUCE 8), gtest.

---

## Success Gates (all must pass)

- [x] **G1:** `cmake --build build --config Debug --target hdaw_tests` succeeds.
- [x] **G2 (lesson 15):** binary lists 6 `TrackFxRebuildRace` tests (5 + 1 new).
- [x] **G3:** suite passes 3× consecutively (~30 s per run).
- [x] **G4:** affected suites PASS — corrected filter (no `Notes`/`Cc` suites
  exist; real names are `NoteChance/NoteExpression/NoteID/NoteMutations`,
  `MidiClipProcessor`, plus `GhostClips` since ghost propagation rides the
  touched listeners): 119 tests / 15 suites PASS.
- [x] **G5:** full suite **816/816 PASS** (8.7 min; = 815 + 1 new).
- [x] **G6:** diff scan clean; `git status` shows the polyrhythm/compositions
  untracked entries GONE (gitignore effective).
- [x] **G7:** frontend `npm run build` green (`✓ built in 1.63s`).

**Result (2026-08-14):** landed at version **0.22.4**. Deviation: the note
command is `addNote(clipId, pitch, velocity, startBeat, durationBeats)`
(AudioEngineCommands.h:124) — clipId-keyed, no trackIndex+clipIndex overload
exists; test takes the clipId from `addMidiClip`'s return. Post-fix call-graph
inventory: 7 marshaled call sites, zero direct `rm->` calls.

## Dependency Map (verified 2026-08-14, grep)

- **Blast radius:** `MainAudioProcessor.{h,cpp}` (+1 marshaled method),
  7 listener call sites in `AudioEngine.cpp` (mechanical rewrite),
  `MidiClipProcessor.h` (+1 read-only atomic accessor), tests, version triple,
  `.gitignore`.
- **Upstream:** the seven listener branches (mutating threads — verified
  non-audio, non-parked).
- **Downstream:** `RoutingManager::rebuildMidiClipCache` unchanged;
  `MidiClipProcessor::setClipTree` unchanged.
- **God nodes in scope:** `MainAudioProcessor`, `AudioEngine` (hub) —
  mechanical call-site rewrites only, no logic changes.
- **Projections / SPSC:** none touched.
- **Path integrity (Gate 2):** no new wiring — same call graph, one hop added
  (marshal), executing thread changes only.

## Pitfall Gates Triggered

- **Gate 3:** no audio-thread callers (listener sites only).
- **Gate 12 / lesson 18:** marshal-not-park (helper's established rationale).
- **Gate 15:** G2 binary listing evidence.
- **Lesson 9:** test scoped to track 1 (default MIDI "Synth" track, empty
  clip list — our clip is its index 0); baseline-relative note-count asserts.
- **Lesson 20:** orphan check before blaming hangs.
- **Anti-patterns:** accessor is read-only atomic load (existing getNum*
  pattern); no new RPC; no DBG.

## Steps

### Task 1: Engine marshal

**Files:** `src/engine/MainAudioProcessor.h`, `src/engine/MainAudioProcessor.cpp`,
`src/engine/AudioEngine.cpp`

- [ ] `MainAudioProcessor.h`: declare `void rebuildMidiClipCache(juce::ValueTree clipTree);`
  next to `rebuildMidiTrackFX` (NOT in AudioGraphCommands — internal only).
- [ ] `MainAudioProcessor.cpp`: implement — `runOnMessageThread([this, clipTree]
  { if (routingManager != nullptr) routingManager->rebuildMidiClipCache(clipTree); });`
  with the one-line rationale-pointer comment.
- [ ] `AudioEngine.cpp`: replace all 7 `if (auto* rm = …getRoutingManager()) rm->rebuildMidiClipCache(...)`
  sites with `mainProcessor->rebuildMidiClipCache(...)` (drop the now-redundant
  `rm` locals; keep surrounding `isValid()`/type guards unchanged).

### Task 2: Test accessor + regression test

**Files:** `src/engine/MidiClipProcessor.h`,
`tests/unit/engine/track_fx_rebuild_race_test.cpp`

- [ ] `MidiClipProcessor.h`: public `int getNumCachedNotes() const { return noteCount.load(std::memory_order_acquire); }`
  (noteCount is `std::atomic<int>`, MidiClipProcessor.h:463).
- [ ] `TrackFxRebuildRace.RebuildMidiClipCacheSerializedAgainstAsyncGraphRebuild`:
  `AudioEngine engine; engine.initialize();` → `addMidiClip(1, 0.0, 4.0, "raceMidiCache")`
  → ~25 iterations: queue an async graph rebuild (`addAudioClip(2, 0.0, 1.0, <wav>, unique)`
  on track 2 — audio track, keeps the MIDI clip index stable on track 1), then
  add ONE MIDI note to the track-1 clip via the real note command (find the
  exact `addNote`-family signature in `src/engine/AudioEngineCommands.h`; use
  the overload that takes trackIndex + clipIndex), then ASSERT synchronously:
  look up the live processor from the CURRENT routing manager
  (`engine.getMainProcessor()->getRoutingManager()->getMidiClipSources()` keyed
  `{1, 0}`) and `EXPECT_GE(proc->getNumCachedNotes(), i + 1)` — baseline-relative
  (lesson 9). No sleeps.
- [ ] Include `engine/MidiClipProcessor.h` + `engine/RoutingManager.h` as needed.

### Task 3: Version bump 0.22.3 → 0.22.4

**Files:** `CMakeLists.txt`, `frontend/package.json`, `frontend/src/version.ts`

### Task 4: gitignore chore (separate commit, orchestrator)

- [ ] `.gitignore`: under the root-level artifacts block, add `/*.hdaw3` (with
  the same root-scoped rationale) and `/compositions/`.

### Task 5: Verification

- [ ] Build, list-tests, suite 3×, affected suites, report back.

## Follow-ups (out of scope)

- Drain seam (§4.2), streaming handle sharing (§4.3), Subsystem E, §5
  docs-archive commit (needs its own completeness/reference audit).
