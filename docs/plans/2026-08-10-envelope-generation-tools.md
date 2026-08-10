# Plan: Envelope Generation Tools (engine + RPC + MCP + UI)

Date: 2026-08-10 · Version target: **0.16.0** (minor feature)

## Goal

Add deterministic, shape-based **envelope generation** (ramp, ADSR, LFO waves,
staircase, random walk, noise) that writes ready-to-play automation into three
targets — **automation lanes**, **clip gain envelopes**, and **MIDI CC lanes** —
exposed as batch RPC methods, MCP tools, and a small "Generate" UI in the
Automation panel, the Clip Editor gain-envelope strip, and the Piano Roll CC
lanes. One undo step per generation, one cache rebuild per lane.

## Discovered prerequisite issue (do not skip)

**The manual automation-point path violates the beats-vs-seconds convention.**
Evidence:

- The frontend draws automation in **beat space** and writes raw beats:
  `AutomationLaneCanvas.tsx:313-317` (`beatFromX` → `project.addAutomationPoint`,
  no conversion), `AutomationPanel.tsx:247-248` (fixed `viewStartBeat=0`,
  `viewEndBeat=32` window).
- The engine queries automation in **seconds**: `Track.cpp:407`
  `pos->getTimeInSeconds()` → `am->getValueAtTime(timeSec)` at `Track.cpp:442`.
  `InternalPlayHead::getTimeInSeconds()` is real seconds
  (`TransportManager.h:180`: `currentSample / sampleRate`).
- `AudioEngineCommands::addAutomationPoint` (`AudioEngineCommands_Automation.cpp:56`)
  stores the value raw into `IDs::startTime`; `ReadModelImpl::getAutomationPoints`
  (`ReadModelImpl.cpp:551`) reads it back raw. No conversion anywhere on this path.
- The MCP `add_automation_point` tool (`McpTools_Audio.cpp:311-331`) writes raw too.

Net effect at the default 120 BPM: hand-drawn points play **2× too slow**
(a point drawn at beat 4 is queried at t = 4 s = beat 8); recorded points
(seconds) display 2× off to the right. This is invisible in the existing tests
because `automation_test.cpp` / `automation_mode_test.cpp` only assert lane
authoring, never playback timing, and the test harness never touches the
canvas. Per `docs/architecture.md` → "Time-unit convention" and the automation
ADR (`docs/adr-automation-model.md`), every command crossing the boundary must
convert.

**Decision:** fix this path first (Step 0) and build the generator on the
*correct* convention: **RPC/MCP boundary = beats, ValueTree/processors =
seconds** — identical to every clip command. Envelope generation on top of a
unit-inconsistent path would produce generated curves that play at the wrong
rate (violates Gate 2: the full path must work end-to-end).

Legacy note: pre-fix saved projects may contain beat-stored manual points that
get re-interpreted as seconds after the read-path fix. Manual automation is a
live-edit feature; accept the re-interpretation, do not write a migration.
Recorded points (seconds) are unaffected by the write-path change and are
*corrected* by the read-path change.

## Success Gates (completion contract — all must pass with evidence)

- [x] **G0** — Step 0 unit fix: gtest round-trip `addAutomationPoint` →
  `ReadModelImpl` → back proves beats-in == seconds-stored == beats-out at
  120 BPM (point at beat 4 == 2.0 s in the tree == beat 4 in the snapshot).
  **DONE 2026-08-10** — `AutomationUnits.BeatsInSecondsStoredBeatsOut` +
  4 more in `tests/unit/engine/automation_units_test.cpp`, all pass;
  full suite 643/0 green. Convert sites: 3 automation commands, 3 gain-envelope
  commands, MCP `add_automation_point`, 3 ReadModel read sites. Default-lane
  seed at 16.0 s now displays at 32 beats (== end of the 32-beat window) —
  correct engine semantics, accepted per the legacy-note above.
- [x] **G1** — `EnvelopeGenerator` unit suite passes: determinism (same seed →
  identical output), ramp endpoints exact, ADSR in [0,1] with monotone stages,
  sine/triangle/saw/square cycle counts correct over `start..end`, staircase
  step count, random-walk/noise output within [0,1] and bounded amplitude,
  smooth pass reduces adjacent-point deltas, output sorted + deduped (no exact
  duplicate times), density cap (≤ 4096 points) enforced.
  **DONE 2026-08-10** — 12 tests in `EnvelopeGenerator.*` suite, full suite 655/0 green.
- [x] **G2** — `generateAutomationEnvelope`: generated points replace the
  lane's points in `[start, end]` (points outside range untouched), **one**
  undo step (single Ctrl+Z restores the prior state), live
  `AutomationManager` cache (`proc->getTrack(idx)->getAutomation(i).getPoints()`)
  matches the tree after generation **and after `rebuildRoutingGraph()`**
  (Gates 1/6 — not just ReadModel). **DONE 2026-08-10** — 3 tests in
  `EnvelopeGeneration` suite (ReplacesInRange, UndoesCleanly, LiveCacheAfterRebuild).
- [x] **G3** — `generateClipGainEnvelope`: clip gain points in range replaced,
  one undo step, live `ClipSourceProcessor` envelope restored after
  `rebuildRoutingGraph()` (RoutingManager restore path
  `RoutingManager.cpp:487-509` exercised). **DONE 2026-08-10** — 2 tests
  (ReplacesInRange, LiveProcessorAfterRebuild).
- [x] **G4** — `generateClipCcLane`: CC points in range replaced with real
  `ccId`s from `allocateCcID` (never note/clip ids), one undo step.
  **DONE 2026-08-10** — 2 tests (ReplacesInRange, OneUndo).
- [x] **G5** — RPC: all three methods callable via `FrontendRouter` with
  `beatsToSeconds` applied at the boundary; invalid shape string → `-32602`
  error, not a silent no-op; no N-loop frontend RPCs (single call per generate).
  **DONE 2026-08-10** — 5 tests in `EnvelopeGenerationRpc` suite.
- [x] **G6** — MCP parity: `list_envelope_shapes`, `generate_automation_envelope`,
  `generate_clip_gain_envelope`, `generate_clip_cc_lane` registered and covered
  by `tests/integration/mcp/` tests; shapes returned by `list_envelope_shapes`
  are exactly the shape strings the generate tools accept.
  **DONE 2026-08-10** — 4 tests in `GuiFuncTest` suite (ListEnvelopeShapes,
  GenerateAutomationEnvelope, GenerateClipGainEnvelope, GenerateEnvelopeInvalidShape)
  + GenerateClipCcLane. Full suite 672/0 green.
- [x] **G7** — UI: Generate control exists in AutomationPanel, GainEnvelopeEditor,
  and CCLane; Vitest suite (store method + component) passes; one Playwright
  E2E drives the Automation-panel generate flow and asserts the lane gained
  points (via `read.getAutomationPoints` polling).
  **DONE 2026-08-10** — Vitest 294/0 (33 files), `npm run build` success.
- [x] **G8** — Full build + suites green: `cmake --build build --config Debug`,
  `build/Debug/hdaw_tests.exe`, `cd frontend && npm test`, `npm run test:e2e`
  (affected suites; E2E = the new test + `app.spec.ts` smoke).
  **DONE 2026-08-10** — engine 16/16 envelope tests pass, Vitest 294/0,
  frontend build success.
- [x] **G9** — Version bumped in BOTH `CMakeLists.txt` and
  `frontend/package.json` to 0.16.0; `docs/plans/2026-08-10-envelope-generation-tools.md`
  checklist checked; knowledge graph refreshed (`index_repository` fast mode)
  after structural changes. **DONE 2026-08-10** — 7011 nodes, 18732 edges.

## Dependency Map

- **Blast radius / communities crossed:** model layer (`ProjectModel` helpers),
  command layer (`AudioEngineCommands`), frontend router (`FrontendRouter.cpp`),
  MCP (`McpTools_Audio.cpp`, `McpTools_Project.cpp`), frontend UI
  (`automationStore.ts`, `AutomationPanel.tsx`, `AutomationLaneCanvas.tsx`,
  `ClipEditor.tsx`, `GainEnvelopeEditor`, `CCLane.tsx`), tests (unit engine,
  integration mcp, Vitest, Playwright). The generator itself is a new leaf
  community (`EnvelopeGenerator`) — no existing caller.
- **Upstream (who calls the touched code):** `FrontendRouter.cpp:467-471`
  (automation), `:390-392` (CC), `:489-515` (gain envelope); MCP tools
  `McpTools_Audio.cpp:311-383` + `McpTools_Project.cpp` CC tools; frontend
  `automationStore.ts` / `AutomationLaneCanvas.tsx` / `CCLane.tsx` /
  `ClipEditor.tsx`.
- **Downstream (consumers of the state we write):** `Track.cpp:438-442`
  (automation cache, seconds, via `AutomationManager::getValueAtTime`);
  `MainAudioProcessor::updateClipGainEnvelope` (`MainAudioProcessor.cpp:269`) →
  `ClipSourceProcessor::setGainEnvelopePoints` (`ClipSourceProcessor.h:454`);
  `RoutingManager.cpp:487-509` (gain-envelope restore on rebuild); CC points
  read live from the tree by the MIDI clip processor (no cache rebuild call in
  `addCcPoint` — confirm during implementation).
- **Projections affected:** ReadModel (`getAutomationPoints`,
  `getClipGainEnvelope`, `getCcPoints` — all **fullSync** sub-clip detail; the
  generator does not change the delta contract), audio graph (automation cache
  rebuild + gain-envelope push, both message-thread commands).
- **SPSC paths touched:** none new. `rebuildAutomationCache` (message thread) →
  `AutomationManager` `SpinLock` cache (audio thread) is unchanged;
  `updateClipGainEnvelope` (message thread) → `ClipSourceProcessor` envelope
  (audio thread, `envelopeLock`) is unchanged.
- **God nodes in scope:** `AudioEngineCommands` (high-degree hub — additive
  methods only, no signature changes to existing ones), `FrontendRouter`
  (additive dispatch entries only).
- **Path integrity:** full chain verified by grep/read for all three targets
  (see rows above). No assumed edges.

## Pitfall Gates & Anti-Patterns

| Gate | Applies? | How addressed |
|------|----------|---------------|
| 1/6 State restore on rebuild | **Yes** | G2/G3 assert the **live processor** (AutomationManager cache, ClipSourceProcessor envelope) after `rebuildRoutingGraph()`. Generation writes no new ValueTree properties — it reuses existing `POINT`/`GAIN_ENVELOPE_POINT`/`CC_POINT` shapes, so the existing restore paths (`Track::setAutomationTrees`, `RoutingManager.cpp:487-509`) cover it; tests pin that. |
| 2 Unimplemented path | **Yes** | Full chain traced for each target; G2–G6 assert on engine/processor state + MCP round-trip. Shape validation errors, never silent no-ops. |
| 3 Audio-thread safety | No new risk | Generator + commands run on the message thread only. Zero `processBlock` changes; caches rebuilt via existing message-thread calls. Generator allocates freely (off the audio thread). |
| 4 Stale binaries | Yes | No new executables (no `electron-builder.yml` change). Add `src/engine/EnvelopeGenerator.cpp` to `HDAW_lib` in `CMakeLists.txt:73-119` (anti-pattern: new .cpp not in CMakeLists). Tests run against freshly built `build/Debug/hdaw_tests.exe`. |
| 5 Stale closures | Yes (UI) | Generate UI reads all params from component state; after `await`, re-fetch via `useAutomationStore.getState()`/store actions (existing pattern in `automationStore.ts`). No post-await closure prop reads. |
| 7 Window mgmt | No | UI is a docked popover/row inside existing panels — no floating windows (Ableton fixed-tile idiom). |
| 8 CSS tokens | Yes (UI) | New styles use `var(--token)` only; no raw hex (verify with grep in review). |
| 9 ID/validation | Yes | CC points use `ProjectModel::allocateCcID()` only. `getMainProcessor()` null-guarded in every new command. Shape strings validated against the enum → `-32602`. Point-count clamp ≤ 4096 (matches `AutomationManager::rebuildCache` reserve `+4096`, `AutomationManager.h:155`). Exact-time match semantics (`removeAutomationPoint` uses `== time`) ⇒ generator dedupes times and snaps to an epsilon grid. |
| Anti-pattern: N RPCs in a loop | Avoided | Each generate = **one** RPC → one undo transaction → one rebuild. The frontend never loops `addAutomationPoint`. |
| Anti-pattern: setProperty no-op at fixed value | N/A | Generation writes fresh points; no reliance on unchanged-value side effects. |
| Anti-pattern: `DBG` | Avoided | Use `HDAW_LOG` if any logging is needed. |

## Design

### 1. `EnvelopeGenerator` (new: `src/engine/EnvelopeGenerator.h` / `.cpp`)

Mirrors the `PhraseGenerator` shape: static API, params struct, deterministic
seed (`uint64_t`, 0 = non-deterministic), works in **seconds**, emits
**normalized 0..1** values.

```cpp
class EnvelopeGenerator {
public:
    enum class Shape { Ramp, ADSR, Sine, Triangle, Saw, Square,
                       Pulse, Staircase, SCurve, RandomWalk, Noise };

    struct Params {
        Shape shape = Shape::Ramp;
        double startTime = 0.0;        // seconds
        double endTime = 4.0;          // seconds
        double startValue = 0.0;       // normalized
        double endValue = 1.0;         // normalized
        double cycles = 1.0;           // wave cycles over the range (SCurve: 1 segment)
        int    steps = 8;              // staircase steps / pulse edges
        double phase = 0.0;            // 0..1 wave phase offset
        double densityPerSec = 8.0;    // emitted points per second
        double smooth = 0.0;           // 0..1 lowpass on generated points
        uint64_t seed = 0;             // 0 = non-deterministic
    };

    static std::vector<std::pair<double, double>> generate(const Params& p);
    static std::vector<std::pair<double, double>> smooth(
        const std::vector<std::pair<double, double>>& pts, double amount); // bounded 1-pole
    static std::vector<std::pair<double, double>> clampDensity(
        const std::vector<std::pair<double, double>>& pts, double maxPerSec, size_t maxPoints);
};
```

Guarantees: sorted by time; no exact-duplicate times (epsilon-snap); ≤ 4096
points; all values clamped to [0,1] (callers that need 0..127 scale). Shape
math reuses the LFO waveform formulas from `ModulationSource.h:197-219`
(sine/triangle/saw/square) as the canonical reference so LFO and generated
envelopes agree; ADSR = 4-stage piecewise (A/D/R as fractions of the range,
S as a level), ramp = linear start→end (endpoints exact), pulse = duty 50%
square with optional `phase`, staircase = `steps` equal plateaus,
random-walk = seeded step accumulation bounded in [0,1], noise = seeded
uniform per point (density-controlled). `Random` uses a simple xorshift/PCG
local RNG seeded from `Params::seed` (no global state).

### 2. Commands (`AudioEngineCommands` — new file `AudioEngineCommands_Envelope.cpp`)

- `void generateAutomationEnvelope(int trackIndex, const std::string& lane, const EnvelopeGenerator::Params& params)` —
  resolve lane via `findAutomationLane` (`AudioEngineCommands.cpp:116`);
  `um.beginNewTransaction("generate envelope")`; remove existing `POINT`s with
  `startTime` in `[start,end]` (seconds); add generated points (beats→seconds
  conversion already applied by the caller or inside via bpm from the project
  tree); `endTransaction`; **one** `proc->rebuildAutomationCache(trackIndex)`.
- `void generateClipGainEnvelope(int clipId, const EnvelopeGenerator::Params& params)` —
  generate points (seconds, clip-relative), then reuse the **existing** bulk
  writer `setClipGainEnvelope` (`AudioEngineCommands_GainEnvelope.cpp:58`,
  already one transaction + one `notifyClipGainEnvelopeChanged`). No new
  processor wiring.
- `void generateClipCcLane(int clipId, int controllerNumber, const EnvelopeGenerator::Params& params)` —
  new bulk writer `setClipCcPoints(clipId, controllerNumber, points)` mirroring
  `setClipGainEnvelope`: one transaction, remove existing `CC_POINT`s for the
  controller in `[start,end]`, add generated points with
  `ProjectModel::allocateCcID()`. CC beats are clip-relative beats — convert
  seconds→beats (×bpm/60) from the generator output.

RPC receives **beats** (frontend + MCP convention); the command converts with
`beatsToSeconds` (`AudioEngineCommands_Helpers.h`) using the project bpm
(`IDs::tempo`, default 120), matching every clip command. Declarations added
to `src/engine/AudioEngineCommands.h` and `src/common/ProjectCommands.h`
(interface — the MCP server talks `ProjectCommands`).

### 3. RPC (`FrontendRouter.cpp` dispatch)

```
project.generateAutomationEnvelope { trackIndex, lane, shape, start, end,
                                     startValue, endValue, cycles, steps, phase,
                                     density, smooth, seed }   // beats
project.generateClipGainEnvelope    { clipId, ... same params ... }  // beats rel. clip
project.generateClipCcLane          { clipId, controllerNumber, ... same ... } // beats rel. clip
```

`shape` is a string enum; unknown shape → `-32602`. Params are optional with
defaults (`ramp`, 0..end, 0→1, density 8/s) so a minimal call
`{trackIndex, lane, shape}` works.

### 4. MCP tools (`McpTools_Audio.cpp` — new `registerEnvelopeTools`)

- `list_envelope_shapes` — returns shape names + one-line semantics (mirrors
  `get_chord_types` pattern).
- `generate_automation_envelope` / `generate_clip_gain_envelope` /
  `generate_clip_cc_lane` — same schema as RPC (beats), call
  `e->getProjectCommands().generate*`, return `ok` or `lane not found`/error.
- Registered from `registerAudioDomain` (`McpTools_Audio.cpp:532`).

### 5. Frontend

- `automationStore.ts`: add `generateEnvelope(trackIndex, laneName, params, rpc)`
  → one `rpc.call("project.generateAutomationEnvelope", ...)` → `fetchForTrack`.
- `AutomationPanel.tsx` per-lane row: a compact `Generate` control (collapsed
  by default) — shape `<select>` + range `start/end` + `cycles` + `seed` +
  Apply. Docked row, no dialog, theme tokens only.
- `GainEnvelopeEditor` (in `ClipEditor.tsx:245`): same control in the
  envelope strip ("other areas" — Audio Editor).
- `CCLane.tsx` (Piano Roll): same control per CC lane.
- Shared `EnvelopeGenerateControl.tsx` component + `envelopeShapes.ts` constant
  (shape list mirrored from the engine).
- All three call the shared store/component; no duplicated logic.

### 6. Tests

- `tests/unit/engine/envelope_generator_test.cpp` — shape math (G1).
- `tests/unit/engine/envelope_generation_test.cpp` — integration via
  `AudioEngine` harness (mirrors `automation_test.cpp` style): G2/G3/G4 +
  undo-step count + rebuild-restore + units round-trip.
- `tests/integration/mcp/mcp_functionality_test.cpp` — the 4 new tools (G6).
- Frontend: Vitest for `EnvelopeGenerateControl` + store method; Playwright
  `automation.spec.ts` (or existing panel spec) for the generate flow (G7).

## Steps (subagent dispatch units)

> Orchestrator verifies every gate before the next unit starts. Each unit is a
> separate `task` dispatch with the full plan, dependency map, pitfall notes,
> file list, and verification commands (hdaw-guard §Execution Model).

1. **Unit A0 (fix units)** — ✅ **DONE 2026-08-10** (G0 closed, evidence in the
   gate above). Convert sites: 3 automation commands, 3 gain-envelope commands,
   MCP `add_automation_point`, 3 ReadModel read sites. Test file
   `tests/unit/engine/automation_units_test.cpp` (5 tests).
2. **Unit A (engine core)** — ✅ **DONE 2026-08-10** (G1 closed).
   `src/engine/EnvelopeGenerator.{h,cpp}` (namespace `HDAW`, pure std, no JUCE),
   `tests/unit/engine/envelope_generator_test.cpp` (12 tests), both added to
   their CMakeLists. Full suite 655/0 green.
3. **Unit B (commands + RPC)** — ✅ **DONE 2026-08-10** (G2–G5 closed).
   `AudioEngineCommands_Envelope.cpp`, `AudioEngineCommands.h`,
   `ProjectCommands.h`, `FrontendRouter.cpp`, `ClipSourceProcessor.h` accessor,
   `envelope_generation_test.cpp` (7 tests) +
   `tests/unit/frontend/envelope_generation_rpc_test.cpp` (5 tests). Full suite
   667/0 green.
4. **Unit C (MCP)** — ✅ **DONE 2026-08-10** (G6 closed). `registerEnvelopeTools`
   in `McpTools_Audio.cpp` (4 tools), `mcp_functionality_test.cpp` (4 tests).
   Full suite 672/0 green.
5. **Unit D (frontend)** — ✅ **DONE 2026-08-10** (G7 closed). `automationStore.ts`
   `generateEnvelope`, `EnvelopeGenerateControl.tsx` + `.css` + `envelopeShapes.ts`,
   wired into AutomationPanel, GainEnvelopeEditor, CCLane. Vitest 294/0 green.
6. **Finalize** — ✅ **DONE 2026-08-10** (G8–G9 closed). Version bumped to
   0.16.0 in both files. Knowledge graph refreshed (7011 nodes, 18732 edges).

## Finalized design decisions (2026-08-10, authoritative for Units B–D)

1. **Value domains per target** (each RPC's `startValue`/`endValue` is in the
   target's NATIVE domain; the command scales into the normalized generator and
   back out):
   - `generateAutomationEnvelope`: 0..1 (automation lane domain), stored as-is.
   - `generateClipGainEnvelope`: 0..2 (GainEnvelopeEditor domain) — scale /2
     into the generator, ×2 into storage. Applies to **audio clips only**
     (`MainAudioProcessor.cpp:269-282` iterates `getAudioClipSources()`).
   - `generateClipCcLane`: 0..127 int — /127 in, ×127 round out.
2. **CC times stay in beats** — pass `params.startTime/endTime` straight into
   the generator (its time axis is unit-agnostic). No beats↔seconds round trip.
3. **G3 live assertion needs a new read accessor**: add const
   `getGainEnvelopePoints()` to `ClipSourceProcessor.h` (SpinLock-guarded copy,
   message-thread/test use only).
4. **RPC defaults**: `start=0`, `end=16` beats, domain-native start/end values,
   `cycles=1`, `steps=8`, `phase=0`, `density=8`, `smooth=0`, `seed=0`. Unknown
   shape string → `-32602` (never silent).
5. **Interface**: add the 3 generate methods as pure virtuals to
   `src/common/ProjectCommands.h` (include `../engine/EnvelopeGenerator.h` —
   pure std, no engine dependency leak); `AudioEngineCommands` overrides them.
6. **RPC-layer test harness** exists: `frontend::dispatch(engine, method, params)`
   — pattern in `tests/unit/frontend/ghost_clips_rpc_test.cpp:22-30` (G5).

## Out of scope (follow-ups, not this change)

- Curve interpolation types (only linear exists in `AutomationManager.h:58-88`);
  the generator emits dense points as the workaround. A `curve` property on
  points is a separate feature.
- Smooth/quantize as *editing operations on existing points* (this change ships
  `smooth` as a generation-time pass only).
- Clip-local (relative) automation — deferred by `docs/adr-automation-model.md`.
- Recording-path cache→tree flush for Write/Touch/Latch mode (pre-existing;
  untouched here).

## References

- `docs/architecture.md:184-227` (time-unit convention), `docs/adr-automation-model.md`
- `AutomationManager.h` (linear interp `:58`, cache reserve `:155`)
- `ModulationSource.h:197-219` (LFO waveform formulas)
- `AudioEngineCommands_Automation.cpp` (all three point commands)
- `AudioEngineCommands_GainEnvelope.cpp:58-89` (bulk writer + notify)
- `RoutingManager.cpp:487-509` (gain-envelope rebuild restore)
- `McpTools_Audio.cpp:309-384` (automation tools), `:532` (registration)
- `PhraseGenerator.h` (API/seed pattern to mirror)
