# Plan: bus FX params + `list_buses` (slice C)

**Status:** planned. Owner: agent session 2026-09-22. **Risk:** LOW (additive; no
`processBlock` restructuring — see the explicit non-goal).
**Predecessor:** `docs/plans/2026-09-22-bus-send-surface.md` (bus/send creation, shipped).

## Goal

Make a bus return **shapable** and **discoverable**: expose the FX parameters the bus DSP
actually honors, persist them so they survive a rebuild, and add the missing read tool
(`list_buses`) that the predecessor's dropped-response finding made urgent.

## What already exists (verified 2026-09-22 — reuse, do not reinvent)

| Piece | Where | Use |
| --- | --- | --- |
| Bus FX chain + hardcoded defaults | `FxBusProcessor::resetFxChain` (`FxBusProcessor.h:78-115`), called from **`prepareToPlay`** and `setFxType` | The thing to parameterize. Note `resetFxChain` on prepare means **params must come from the tree**, not from transient state. |
| The bus-side param pattern | `MasterBusProcessor`: `setSlotParam` (clamps to the def, lesson 23), `applyFromTree` ("Gate 1 restore helper … clamps every value"), `param_<i>` persistence | **Mirror this exactly** — it is the existing convention for a bus. |
| Param defs for the four types | `TrackFXSlot::getParamDefsForType` (`TrackFXSlot.h:89+`) — reverb/compressor/eq/delay with names + ranges (and delay's SyncToTempo/Division semantics documented) | Source of names/ranges so bus and track FX agree. |
| Shared-def precedent | `src/common/MasterFxDefs.h` (`masterFxParamDefs`, `clampMasterFxParam`) | The file to mirror for `BusFxDefs.h` (src/common = MCP/RPC parity by construction). |
| Tree listener already routes `param_N` | `AudioEngine.cpp:1009,1253-1257` (handles `param_N` incl. MASTER_FX children) | The persistence path already exists. |

## Success gates

- **G1 — real params, no fakes.** `set_bus_fx_param {busID, paramIndex, value}` changes the
  rendered output of the bus return: reverb Room Size/Damping/Wet/Dry/Width, eq
  Frequency/Q/Gain, compressor Threshold/Ratio/Attack/Release, delay Delay Time. Each is
  proven by an A/B render delta on a send feeding that bus, not by reading back the value.
- **G2 — survival (Gates 1/6/10).** Values written via `set_bus_fx_param` are present in
  the **tree** (`param_N` on the BUS node) and re-applied after `rebuildRoutingGraph()`
  **and** after `prepareToPlay` re-runs `resetFxChain()`. Assert on the live processor.
- **G3 — clamping at every entry (lesson 23).** Out-of-range values clamp to the def's
  min/max at the command AND the processor; an unknown `paramIndex`/`fxType` is rejected
  with a clear error and no mutation.
- **G4 — `list_buses`.** Returns every bus (`busID`, `name`, `busType`, `fxType`,
  `busTarget`) from the tree; matches what a `save_project` shows. RPC twin identical.
- **G5 — `list_bus_fx_params`** returns the defs **the bus DSP honors** for that bus's
  `fxType` (name/min/max/default/current) — it must NOT advertise params the DSP ignores.
- **G6 — parity.** `node tools/rpc_parity_map.mjs` regenerated; the three new tools
  `mapped`, not `unresolved`; twin test asserts identical payloads and identical failures.
- **G7 — blast radius.** No `processBlock` restructuring anywhere; the existing suites and
  the shipped bus/send tests stay green.

## Non-goals (explicit — this is the part that needs a separate decision)

**The bus delay's `Feedback` / `Mix` / `SyncToTempo` / `Division` are NOT in this slice.**
`FxBusProcessor`'s delay is a bare `juce::dsp::DelayLine` driven by
`ProcessContextReplacing` — a **single 100%-wet tap with no feedback path**, so those four
defs would be fake params (unacceptable). (100% wet is *correct* for a send return — the dry
already reaches the master; what is missing is **feedback**, i.e. repeats, and tempo-sync.)
The DSP that does this properly exists but is **inline in `TrackFXSlot`** (see the runaway
guard at `TrackFXSlot.h:719`), so making it real is a DSP swap/extraction — a DSP-chain
change the repo's stability rule gates on prior discussion. Therefore: the delay bus exposes
**Delay Time only**, and `list_bus_fx_params` reports exactly that. Recommended follow-up
(needs the user's go): extract the internal delay DSP into a shared class and use it in
`FxBusProcessor`, which turns the return into a real dub delay (feedback ride + tempo
division) — the last piece the dub idiom needs.

Also out of scope: automatable **lanes** for bus params (a feedback *ride* is a lane, not a
value). The param-index space is designed so lanes can be added later without renumbering.

## Interface contract (frozen)

```cpp
// src/common/BusFxDefs.h — mirrors MasterFxDefs.h (header-only, juce_core only)
struct BusFxParamDef { const char* name; float def; float min; float max; };
const std::vector<BusFxParamDef>& busFxParamDefs(const juce::String& fxType);
float clampBusFxParam(const juce::String& fxType, int paramIndex, float value);
// fxType -> reverb{5}, eq{3}, compressor{4}, delay{1 = "Delay Time"}; unknown -> empty.
// Names/ranges must equal TrackFXSlot::getParamDefsForType's for the same type.

// src/common/BusInfo.h — shared shaping so MCP and RPC agree by construction
struct BusInfo { int busID; std::string name, busType, fxType; int busTarget; };
std::vector<BusInfo> readBuses(const juce::ValueTree& busList);          // sorted by busID
std::string shapeBusesJson(const std::vector<BusInfo>&);                 // the one payload
std::string shapeBusFxParamsJson(const juce::ValueTree& busTree);        // defs + current

// ProjectCommands (additive)
virtual bool setBusFxParam(int busID, int paramIndex, float value, std::string& error);
```

Surfaces (camelCase-derived so the ratchet maps them): MCP `list_buses` → `read.listBuses`,
`list_bus_fx_params` → `read.listBusFxParams`, `set_bus_fx_param` → `project.setBusFxParam`.
MCP argument names are the contract and must be mirrored exactly on the RPC side.

## Steps

1. **Slice C1 (engine, subagent):** `src/common/BusFxDefs.h` + `src/common/BusInfo.h`;
   `FxBusProcessor::setParam`/`getParam`/`applyFromTree` (clamped, atomics, mirroring
   `MasterBusProcessor`); `RoutingManager::addBus` applies the BUS node's `param_N` after
   construction (so `resetFxChain` defaults are overridden by tree values);
   `AudioEngineCommands::setBusFxParam` (tree write + live apply); engine tests.
2. **Slice C2 (surfaces, subagent, after C1):** the three MCP tools + RPC twins + twin tests
   + ledger regen.
3. **Orchestrator acceptance:** relaunch onto the fresh binary, then A/B renders proving G1
   (reverb Room Size and eq Gain are the clearest deltas), a rebuild-survival check (G2), and
   a `list_buses`/`save_project` cross-check (G4).

## Evidence log

- 2026-09-22 **slice C1 (engine) landed.** New `src/common/BusFxDefs.h` (def table +
  `clampBusFxParam`) and `src/common/BusInfo.h` (read shaping + `findFxBusForRead`), both
  header-only inline (no CMake change). `FxBusProcessor` gained
  `setParam`/`getParam`/`paramDefs`/`applyFromTree`/`applyParamToDsp` with `processBlock`
  **unchanged**; `RoutingManager::addBus` applies the BUS node's `param_N` after construction;
  `setBusFxParam` command added; 7 tests in suite `BusFxParam`.
  - **Deliberate deviation 1:** `juce::dsp::Reverb` has no `setWidth` — params are applied via
    `getParameters()`/`setParameters()` on the whole struct (verified against the JUCE source).
  - **Deliberate deviation 2:** the eq default was a *bug* — the old literal passed a LINEAR
    gain of 0.0 to `makePeakFilter` (a degenerate notch), the def's `0.0 dB` now maps through
    `decibelsToGain` to a flat 1.0. No shipped project can be affected: `createDefaultProject`
    stamps only master + a `reverb` bus, and eq buses only became creatable today.
  - **Bug avoided:** the delay line's capacity was a fixed 44100 samples, which would have
    silently capped `Delay Time` at 1.0 s @44.1 k (0.46 s @96 k) — i.e. the def's 5 s top would
    have been a lie. Capacity is now sized from the def max in prepare (~1.7 MB per delay bus).
- 2026-09-22 **slice C2 (surfaces) landed.** MCP `list_buses` / `list_bus_fx_params` /
  `set_bus_fx_param` + RPC `read.listBuses` / `read.listBusFxParams` /
  `project.setBusFxParam`; `dispatchRead` gained a `busList` parameter (one call site,
  `FrontendRouter.cpp`); ledger regenerated — **303 tools / 391 methods, mapped 192 → 195**,
  all three rows `mapped`, idempotent, ratchet-compatible. Both surfaces call C1's shared
  shaping, so the payloads are identical by construction; the RPC side returns the parsed
  structure and the MCP side the compact document. C2 also corrected the now-stale
  "no tool lists buses" text in `docs/composition-toolkit.md`.
- **Threading note (design decision, recorded deliberately):** C1's `setParam` pushes into the
  live DSP from the message/command thread. That is the *existing* convention for HDAW internal
  FX — `TrackFXSlot` applies its internal-FX params (including the allocating `makePeakFilter`
  and `delay->setDelay`) the same way at `TrackFXSlot.h:1853`/`:1870`; its `paramDirty`
  audio-thread deferral at `:655-670` is for *plugin* params only. C3 keeps that convention
  rather than introducing a buses-only deferral (a second convention), and the benign-tear
  class is recorded as a known limitation.
- 2026-09-22: recon. Def tables inventoried (`MasterFxDefs.h` = eq/comp/limiter;
  `TrackFXSlot::getParamDefsForType` = all four bus types incl. delay feedback/tempo-sync);
  `MasterBusProcessor` identified as the pattern to mirror; `FxBusProcessor`'s delay proven
  to be a bare `DelayLine` (hence the explicit non-goal above).

---

# Slice C3 — extract the internal delay DSP into a shared class (**approved DSP change**)

**Status:** approved by the user 2026-09-22 ("extract the internal delay DSP into a shared
class and use it in FxBusProcessor"). **Risk: MEDIUM-HIGH** — it is the one change in this
plan that touches a DSP chain *shared with every track*, and the repo's rule is that
rendering/playback stability outranks new features. Sequencing: lands **after** C1/C2 so it
owns `FxBusProcessor.h` + `BusFxDefs.h` without conflict.

## Why

C1 exposes `Delay Time` only, because `FxBusProcessor`'s delay is a bare
`juce::dsp::DelayLine` with no feedback path — a dub return with no repeats. The real DSP
already exists inline in `TrackFXSlot`, so the fix is extraction, not invention.

## The exact DSP to extract (verified 2026-09-22)

| Piece | Where | Detail |
| --- | --- | --- |
| Delay line | `TrackFXSlot.h:742` (`make_unique<juce::dsp::DelayLine<float>>(sampleRate)`), member `:1654` | capacity = sample rate; prepared, never allocated in `process` |
| Per-sample loop | `TrackFXSlot.h:1227-1234` | `in = x[s]; delayed = delay->popSample(ch, delaySamps); delay->pushSample(ch, in + delayed*fb); x[s] = in*dryMix + delayed*wetMix;` |
| Derived time (tempo sync) | `:1775-1788` | `sec = kDelayDivisionBeats[division] * 60 / bpm` (bpm ≤ 0 → 120), clamped to the Delay Time range |
| Tempo source | `:1719` `std::atomic<float> tempoBpm{120}`; `setTempo()` `:441`; pushed from `Track.cpp:584` (`slot->setTempo(bpm)`) | buses need the same push (C3 wiring) |
| Params | `:112-127` | Delay Time 0 / Feedback 1 (max **0.99**) / Mix 2 / SyncToTempo 3 / Division 4 |
| Runaway protection | `:719` | the whole param vector is clamped to the defs before the DSP push — **feedback ≤ 0.99 is what stops inf/NaN**. Must be preserved at every entry (lesson 23). |

## Success gates (C3)

- **G-C3-1 — behaviour preserved for tracks (the critical gate).** `TrackFXSlot`'s delay
  output is unchanged by the extraction, proven **analytically**: an impulse through a delay
  slot with known time/feedback/mix must produce taps at the expected sample offsets with the
  expected amplitudes (e.g. delay 0.1 s, fb 0.5, mix 1.0 → taps 1.0 / 0.5 / 0.25 at
  0.1/0.2/0.3 s). No "golden hash" — assert the behaviour, not the bytes. Plus: the existing
  suites covering internal delay stay green.
- **G-C3-2 — the bus delay now really repeats.** A send into a `delay` bus with
  `Feedback 0.7` produces measurably more energy and a longer decay tail than
  `Feedback 0.0` over the same window (A/B render, the predecessor's G3 method).
- **G-C3-3 — all five delay defs are real.** `busFxParamDefs("delay")` returns the same five
  names/ranges as `TrackFXSlot::getParamDefsForType("delay")`; `SyncToTempo`/`Division`
  actually move the tap spacing (measured, not just stored).
- **G-C3-4 — audio-thread safety (pitfall 3).** No allocation, lock or `juce::String` work in
  the process path; the delay line is allocated in prepare only; `ScopedNoDenormals` is
  retained; feedback stays clamped so a hostile project file cannot produce inf/NaN.
- **G-C3-5 — no latency change (lesson 7).** A delay effect reports no PDC latency; assert
  the bus introduces no change to the project's reported latency.
- **G-C3-6 — survival.** Bus delay params come from the tree (`param_N`) and are re-applied
  after `rebuildRoutingGraph()` **and** `prepareToPlay` (C1's G2, extended to the new params).

## Steps

1. `src/engine/InternalDelay.h` — the shared class: `prepare(spec)`, `reset()`,
   `setParam(index, value)` (clamped), `setTempo(bpm)`, and a per-block process over a
   `dsp::AudioBlock`. Move `kDelayDivisionBeats` into it. Copy the arithmetic **verbatim** —
   the extraction must not "improve" interpolation, ordering or clamping.
2. `TrackFXSlot` uses it (delete its inline delay loop + delay line + division table).
3. `FxBusProcessor`'s delay branch uses it (replacing the bare `DelayLine`), fed by the same
   clamped `setParam` path as the other types, and given the project BPM by the same route
   `Track` uses (`Track.cpp:584` → find the bus equivalent in `RoutingManager`/tempo change).
4. `busFxParamDefs("delay")` → the five defs (matching `TrackFXSlot`).
5. Tests: the analytic impulse test (G-C3-1), bus feedback A/B (G-C3-2), tempo-sync movement
   (G-C3-3), clamping (G-C3-4).

## Rollback

The slice is confined to `InternalDelay.h` (new), `TrackFXSlot.h`, `FxBusProcessor.h`,
`BusFxDefs.h` and the bus tempo push — reverting those five files restores the previous
behaviour exactly, with no tree/save-format change (delay params were already `param_N`).

## C3 evidence log (landed 2026-09-22)

The slice was interrupted mid-run (its job was lost) and completed unverified in the tree; the
orchestrator verified it end to end rather than re-running it.

- **`InternalDelay`** (`src/engine/InternalDelay.h`, 9.3 KB): the per-sample loop, the
  feedback/push ordering, the tempo-sync derivation and the `1e-4 s` recompute gate copied
  verbatim; allocation only in `prepare` (`setMaximumDelayInSamples(5 s × sr)` *before*
  `prepare`); feedback clamped to 0.99 at every entry; params/tempo as atomics. It also became
  the **one source of truth for the delay's defs** — `TrackFXSlot` derives its param table from
  `InternalDelay::paramDefs()` and `BusFxDefs.h` is pinned to it by a test, so the DSP's clamp
  and the advertised surface range cannot drift.
- **Tempo wiring (better than planned):** instead of a push at the tempo-change site, the bus
  reads BPM from the **playhead** in `processBlock` (`FxBusProcessor::currentBpm()`), the same
  route `Track` uses — so it follows tempo live *and* in offline export with no wiring, with a
  120 fallback.
- **Test change audited:** C3 replaced `TrackFxDelay.SyncDivisionDerivesDelayTime` with three
  stronger analytic tests (`ShortDelayTapsStayAnalytic`, `MixBlendsDryAndWet`,
  `SyncDivisionAndTempoMoveTheTaps`). **No assertion was removed or re-pinned** (checked the
  diff), and the new tap positions (2756 / 5513 / 11025 samples) were re-derived independently
  from the spec and match.
- **Gate results:** `TrackFxDelay.*` + `BusFxParam.*` + `BusSendCreate.*` + `BusSendRpcTest.*`
  = **38/38 pass**. Live A/B: Feedback 0.7 vs 0.0 → last-2s rms 0.1466 vs 0.1159 (**+26%**),
  peak 0.664 vs 0.530 = repeats; Division 1/8 vs 1/4 → 0.1373 vs 0.1088 (sync live). No
  latency API anywhere in the path (G-C3-5 by construction; no MCP surface exposes latency).
- **Findings raised while running it:**
  1. The dropped HTTP response is **not `add_bus`-specific** — `add_send` dropped too. Correct
     characterization: mutating commands that trigger a routing rebuild intermittently lose
     their response while completing the work (recorded as the `rebuild-commands-drop-response`
     trap).
  2. **A bus return's default `Mix 0.5` re-adds the bus input** — on a send that is a doubled
     dry signal on top of the track's own dry. **`Mix 1.0`** is the correct send-return setting
     (delay and reverb); recorded as the `bus-return-mix` trap and folded into the tree's
     `shared-return` recipe.
- Knowledge graph refreshed (`python -m graphify update . --force`) → 20873 nodes;
  `InternalDelay` resolves as its own community hub (degree 31).
