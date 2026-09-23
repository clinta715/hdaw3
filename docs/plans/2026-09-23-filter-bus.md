# Plan: a `filter` bus type (high-passable returns)

**Status:** approved 2026-09-23 ("ok lets do the filter bus next"). **Risk:** MEDIUM — it
touches the bus FX contract *and* re-uses the DSP that every track's internal filter runs, so
the extraction carries the same behaviour-preservation gate slice C3 had.
**Why:** a return cannot be high-passed today (bus `fxType`s are reverb/delay/eq/compressor and
the eq is a single **peak** filter), so the classic dub move — rolling the lows off the delay
return so the repeats do not muddy the bass — is inexpressible. Measured consequence: adding the
returns to `aether_dub` pushed `bass:high` from 14.1:1 to **15.6:1** (trap
`return-cannot-be-high-passed`).

## The design: chain buses via `busTarget` (no new routing needed)

`createBus` already takes a `busTarget`, and `connectBusToParent` connects a bus to any other
bus. So the HPF-on-a-return recipe is **create the filter bus first (target master), then create
the delay bus with `busTarget = <filter bus>`** — the delay's output then passes through the HPF.
Send targets stay on the delay bus. No new command, no new routing code.

## What already exists (verified 2026-09-23 — reuse, do not reinvent)

| Piece | Where | Note |
| --- | --- | --- |
| Filter param defs | `TrackFXSlot::getParamDefsForType("filter")` (`TrackFXSlot.h:164-169`) | `{0 Cutoff 1000 [20,20000]}, {1 Mode 0 [0,2]}, {2 Resonance 0.7 [0.1,10]}` — Mode is 0=LP, 1=HP, 2=BP |
| The DSP | `TrackFXSlot::ManualSVF` (`TrackFXSlot.h:1646-1685`) | self-contained struct: TPT SVF, per-channel states, `updateCoefficients`/`processSample`/`reset`, **no allocation, no deps**, numerically verified 2026-09-09 |
| Its process site | `TrackFXSlot.h:1231` | per-sample in-place, `filter.processSample(ch, …)` |
| The accepted-type list | `src/common/BusFxDefs.h` `busFxTypes()` / `busFxTypesText()` | shared, so `createBus`'s rejection, `setBusFxParam`'s rejection and both read surfaces name the same set |
| The bus param plumbing | `FxBusProcessor` (`setParam`/`applyParamToDsp`/`applyFromTree`/`resetFxChain`), `BusFxDefs.h` def table | slice C1's shape — a new type is a branch, not a new mechanism |

## Success gates

- **G1 — the type exists and is honest.** `add_bus {busType:"fx", fxType:"filter"}` succeeds;
  `list_bus_fx_params` reports Cutoff / Mode / Resonance with the ranges above; an unknown
  fxType is still rejected *by name*, now listing `filter` too. `BusFxParam.DefTableMatchesTrackFxDefs`
  covers the new type (bus defs == `TrackFXSlot`'s, field for field).
- **G2 — it is AUDIBLE (the point of the slice).** A filter bus in **Mode=Highpass** measurably
  removes lows from what passes through it: A/B render of a send into a filter bus with a high
  cutoff vs a low one, and the low band must differ materially. A param that reads back but does
  not change the audio fails this gate.
- **G3 — the chained recipe works end to end.** `delay bus (busTarget = filter bus) → filter bus
  (Mode=HP, Cutoff≈250 Hz) → master`, with a send into the delay bus: the repeats reach the
  master **high-passed**, and the return no longer drifts the mix bass-heavy. This is the
  deliverable — the return becomes *mixable*.
- **G4 — TrackFXSlot's filter is unchanged (the extraction gate).** The SVF math is copied
  verbatim; the existing filter/automation suites stay green (`AutomationPidRouting` was the
  regression that caught the last SVF bug). If a direct analytic test is feasible (a known
  cutoff's magnitude response), add it; do not re-pin any existing assertion.
- **G5 — clamping at every entry (lesson 23).** Mode clamps to 0..2, Cutoff to 20..20000,
  Resonance to 0.1..10, at the command *and* the processor.
- **G6 — parity.** The new type appears in both surfaces' accepted set and in
  `list_bus_fx_params`; **no new tool**, so the parity ledger must be byte-identical.
- **G7 — blast radius.** Only an additive branch in `FxBusProcessor::resetFxChain`/`processBlock`
  (one enabled flag + one process step); no latency reporting, no export/routing change.

## Interface contract (frozen)

```cpp
// src/engine/InternalFilter.h (new) — extracted from TrackFXSlot::ManualSVF, math verbatim
class InternalFilter {
public:
    enum Mode { Lowpass = 0, Highpass = 1, Bandpass = 2 };
    static constexpr int kNumParams = 3;                 // Cutoff, Mode, Resonance
    static const std::array<ParamDef, 3>& paramDefs();   // same names/ranges/defaults as TrackFXSlot
    static float clampParam(int index, float value);
    void prepare(double sampleRate);                     // states reset, coefficients updated
    void reset();
    void setParam(int index, float value);               // clamped; updates coefficients
    float getParam(int index) const;
    float processSample(int channel, float input);        // per-sample, in-place (the slot's call site)
    void process(juce::dsp::AudioBlock<float>& block);    // the bus call site
};

// src/common/BusFxDefs.h — busFxTypes() gains "filter"; busFxParamDefs("filter") mirrors
// TrackFXSlot's filter defs; busFxTypesText() gains it (drives every rejection message).
```

## Steps

1. **Slice E (code, subagent):** `src/engine/InternalFilter.h` (extracted verbatim);
   `TrackFXSlot` uses it (delete `ManualSVF`); `FxBusProcessor` gains the filter branch (enabled
   flag, member, `resetFxChain`, `applyParamToDsp`, process step); `BusFxDefs.h` gains the type +
   defs; the MCP/RPC descriptions that enumerate the accepted set are updated; tests for G1–G5.
2. **Orchestrator:** the docs/tree content — the `shared-return` recipe gains the chained-HPF
   variant, the `return-cannot-be-high-passed` trap is retired (or marked resolved), and the
   `rides` node references it.
3. **Verification:** build, run the filter/automation + bus suites, then the live G3 — build the
   chained return on `aether_dub_returns`, render, and show the low band drop while the six
   `mix_verdict` gates stay green.

## Evidence log

- 2026-09-23: recon. `ManualSVF` is dependency-free and numerically verified, so extraction (not
  duplication) is the consistent move — the same call as slice C3's `InternalDelay`. The chained
  recipe needs no new routing: `busTarget` already nests buses.
- 2026-09-23: **nesting verified, re-parenting is a gap.**
  `RoutingManager::connectBusToParent` reads `busTarget` and connects to `busNodes[parentID]`
  (falling back to master), so `delay → filter → master` works — **but only if the child is
  created with the right target**, because `busTarget` is set solely by `createBus` and no
  command changes it afterwards. Consequence for the recipe: **create the filter bus FIRST, then
  the delay bus with `busTarget = <filter bus>`**. The default `Reverb` bus (busID 1, target 0)
  therefore cannot be routed through a new HPF bus today.
  **Follow-up (small, recommended): a `set_bus_target` command** — mirror `createBus`'s
  validation (target must exist, no cycles), write `busTarget` under one undo unit, one
  `rebuildRoutingGraph`. That makes the HPF applicable to any bus, including the default return.
- 2026-09-23: **the extraction survived the reboot and was verified by inspection.** All of the
  slice's edits predate the crash; `hdaw_tests.exe` itself was **corrupted by the reboot**
  (`os error 1392`) and had to be rebuilt.
- 2026-09-23: **behaviour preservation confirmed by reading the diff.** The original applied the
  filter as `jlimit(0,2,roundToInt(mode))` + `jlimit(1, sr*0.49, cutoff)`; the new class rounds
  the mode identically and folds the cutoff at Nyquist inside the coefficient solve. The only
  delta: a cutoff stored *outside* the def range (1–20 Hz, reachable only via a hand-edited
  project file) now clamps to the def's 20 Hz — stricter, and aligned with the advertised range.
- 2026-09-23: **G2 verified** (`FilterBusModeAndCutoffChangeTheRenderedAudio` passes): the filter
  bus's measured response — Lowpass @1 kHz passes 100 Hz (≈0.343), Highpass removes it (<5%),
  Highpass passes 5 kHz, Lowpass removes it, and moving Cutoff moves the band. **G3 verified in
  the real engine** (chained `delay → HPF → master` render on `aether_dub_returns`, 16 s of
  drop1): bass **18 627 → 15 505 (−17%)**, sub **5 645 → 4 445 (−21%)**, render finite and
  non-silent. The return is now mixable.
- 2026-09-23: **a real hazard the chained test caught, and its fix.** The test initially failed
  with channel-1 `inf` followed by an **access violation (0xc0000005)**. Root cause: **a bus
  created after the graph was last prepared is never prepared itself** —
  `MainAudioProcessor::rebuildRoutingGraph` re-prepares only `if (getSampleRate() > 0)`, and
  `FxBusProcessor::releaseResources()` shrinks the scratch buffer to 1×1. Processing such a bus
  then indexes the scratch buffer *and* the unallocated delay line out of bounds.
  Two fixes: (1) the test now creates its buses **before** the single prepare and looks the live
  processors up **after** it (a re-prepare rebuilds the RoutingManager, so earlier pointers
  dangle); (2) `FxBusProcessor::processBlock` now **fails safe** — if the scratch buffer cannot
  hold the block it returns early (pass-through) instead of corrupting memory. Reordering alone
  was not enough: the guard is what makes an unprepared bus harmless.
- 2026-09-23: **the guard exposed the real root cause, one level down.** With the guard in
  place, three tests failed instead of crashing — `DelayParamsSurviveRebuildAndDriveTheRebuiltReturn`
  ("the restored return does not repeat"), the chained test, and one RPC twin. The first two
  were the guard *working*: those buses really were unprepared, because **`getSampleRate()`
  returns 0 for anyone who calls `prepareToPlay` directly** — nothing in the codebase calls
  `setRateAndBufferSizeDetails`, so `rebuildRoutingGraph`'s guard `if (getSampleRate() > 0)`
  (MainAudioProcessor.cpp:655) is false in a deviceless harness and **rebuilds never re-prepare
  freshly added nodes**. In production the device manager sets the rate first, which is why the
  live renders were clean.
  **Fix:** `MainAudioProcessor::prepareToPlay` now calls
  `setRateAndBufferSizeDetails(sampleRate, samplesPerBlock)` first, so the processor is
  self-consistent however it was prepared and every later rebuild re-prepares. That turns the
  `processBlock` guard from a bypass into a genuine last-resort net.
- 2026-09-23: **a test-precision bug in the twin** (`AddFilterBusMatchesMcp`): it compared
  Resonance's float-derived `minValue` (0.1f → 0.100000001490116) against `0.1` with
  `EXPECT_DOUBLE_EQ`, which allows only 4 ULPs. Now `EXPECT_NEAR(…, 0.1, 1e-6)` — the honest
  comparison for a value that crosses float→JSON→double.
- 2026-09-23: **third layer — the harness helper itself.** After the rate fix, the chained test
  still failed with `lp == hp` **bit-for-bit** (0.35617712122998996 both), i.e. the filter was a
  no-op pass-through: the bus was *still* unprepared. Cause: `ensureLiveRoutingGraph` opened with
  `if (proc->getRoutingManager() != nullptr) return true;` — and `AudioEngine::initialize()` can
  leave a manager in place while `getSampleRate()` is still 0, so `prepareToPlay` was never
  called and no later rebuild could re-prepare. The helper now **always** prepares (preparing is
  idempotent), which is what "ensure" should have meant. Three layers deep, each one only
  visible because the previous fix removed the noise above it.
