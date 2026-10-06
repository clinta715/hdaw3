# Plan — batched parameter writes (+ bug fixes along the way)

Drafted 2026-10-05. Status: **LANDED 2026-10-05** (slices 1-4 + bug fixes). Evidence
base: the `mangrove_dub` session (2026-10-05) +
`compositions/mangrove_dub/session-report.md`.

## Close-out (2026-10-05)

- **Slice 1** — `src/common/FxParamBatchJson.h` (strict parser),
  `ProjectCommands::{FxParamWrite,writeFxParam,setFxParams,setBusFxParams,setLfoParams}`,
  engine impls, `paramBatchActive_` at the `transactionBoundary` choke point.
  9 engine tests (`FxParamBatch.*`).
- **Slice 2** — MCP `set_fx_params` / `set_bus_fx_params` / `set_lfo_params` +
  RPC `project.setFxParams` / `setBusFxParams` / `setLfoParams`, sharing one
  payload shaper (byte-identical). 18 twin tests (`ParamBatchRpcTest.*`).
- **Slice 3** — `src/common/FxParamResolve.h`; `writeFxParam` + all four single
  tools now share ONE resolver+writer. **Byte parity proven** (`texts_not_preserved:
  NONE`; 11 new literal `EXPECT_EQ` parity tests).
- **Slice 4 (bugs)** — `verifyPart` `nonClipping = (mixMeasured ? mixPeak :
  soloPeak) < 1.0f`; `set_notes_gain` clamps 0..2 with a visible `clamp` field;
  `verify_window` ceilingHitPctMax prose corrected; `masterRms` unit wording in
  both brief schemas.
- **Parity**: `node tools/rpc_parity_map.mjs` → 325 tools / 311 mapped / 14
  mcp-only / **0 unresolved** (323/309 before the `add_buses`/`add_sends` follow-up).
- **Live smoke** (fresh engine): batch mixed-addressing write (3/3); partial-apply
  (`written:2, failed:1` with a per-write error row); typo'd key refused; a single
  `undo` restored a 2-write batch; `verify_part {soloOnly:true}` now reports
  `nonClipping=1` for a clean part.

### Not done / deliberately deferred
- `add_sends` / `add_buses` batch creators (the 11-call win) — **LANDED
  2026-10-05** (follow-up): command layer `ProjectCommands::createBuses` /
  `createSends` (`{ok, applied, error}` + per-item `busIDs`/`sendIndexes` + `errors`
  by original index, ONE undo unit via `beginBatch`/`endBatch`), the shared strict
  parser + payload shaper `src/common/BusSendBatchJson.h`, MCP `add_buses` /
  `add_sends` + RPC `project.addBuses` / `project.addSends` (byte-identical),
  twin tests `tests/unit/frontend/bus_send_batch_rpc_test.cpp`.
- `increment_fx_params` (delta writes) — deferred (no use case surfaced).

## Goal

Replace the N-call parameter-write workflow with ONE batched MCP tool + JSON-RPC
twin, so a whole voicing pass is a handful of calls in ONE undo unit on every
transport. Measured need: that session made **266 individual MCP calls**, of
which **213 (80%) were single-value parameter writes** —
`set_internal_fx_param` ×153, `set_lfo_param` ×60, `set_bus_fx_param` ×13.

Fix four defects found in the same session (see §Bug fixes): `verify_part`
soloOnly booleans, `verify_window` docs (`ceilingHitPctMax:0`), `set_notes_gain`
docs, and the `masterRms` unit wording.

## Non-goals

- No change to DSP, render, or `processBlock`: these are command/parse surfaces
  (AGENTS.md sound-engine rule not triggered — confirm in review).
- No frontend (Electron) work: the client is not a delivery target.
- No change to the SINGLE-write tool payloads: `set_fx_param` /
  `set_internal_fx_param` / `set_bus_fx_param` / `set_lfo_param` keep byte-identical
  responses (pinned by new twin tests).

## Dependency map (verified by reading, not assumed)

Upstream (callers of the command layer):
- MCP: `src/mcp/McpTools_FxSlot.cpp` (`set_fx_param` @423, `set_internal_fx_param` @825),
  `src/mcp/McpTools_Send.cpp` (`set_bus_fx_param` @279),
  `src/mcp/McpTools_Modulation.cpp` (`set_lfo_param` @48).
- RPC: `src/frontend/router/Router_Project.cpp` (`project.setFxSlotParam` etc.),
  `FrontendRpc.h` namespace constants, gated by `RpcNamespaceCoverage`.
Downstream (command → engine):
- `AudioEngineCommands::setFxSlotParam` (`AudioEngineCommands_Fx.cpp:614`) —
  clamps to `TrackFXSlot::getParamDefsForType` then `slot.setProperty("param_<i>")`.
- `AudioEngineCommands::setBusFxParam` (`AudioEngineCommands.cpp:519`).
- `AudioEngineCommands::setLfoParam` (`AudioEngineCommands_Modulation.cpp:106`).
- Plugin path: `setPluginParam` → live `PluginParamService` + durable
  `appliedParamOverrides` ledger.
Undo choke point: `AudioEngineCommands::transactionBoundary`
(`AudioEngineCommands_Undo.cpp:53`) — `if (batchActive_) return;`. **This is the
key**: a command-layer batch that sets `batchActive_` via `beginBatch` collapses
internally-transactional writes into ONE unit **regardless of transport**.
Verified: `set_notes_gain`/`set_clips_edit` already achieve this on both surfaces.
Projections: ReadModel param snapshots; no audio-graph topology change.
Anti-patterns to avoid: N `setProperty` in a loop WITHOUT a surrounding
transaction; per-write `beginNewTransaction`; a new `.cpp` not in CMakeLists.

## Pitfall gates

- Gate 2 (unimplemented path): each new route must have a twin test that observes
  the VALUE, not a "ok" string.
- Gate 9 (id namespace/validation): writes address tracks positionally +
  stable `trackID`; reuse `resolveTrackRef`/`trackIndexArg`; bounds-check
  `slotIndex`/`paramIndex`.
- Lesson 23 (clamp at every entry point): the batch MUST keep the write-side
  clamp in `setFxSlotParam`.
- Lesson 38 (silent-acceptance): strict key parsing — an unknown write key or a
  non-integral index is REFUSED (shared parser, byte-identical both surfaces).
- Lesson 34 (dropped key): every accepted key must be parsed.

---

## Slice 1 — shared resolver + command-layer batch (foundation)

New `src/common/FxParamResolve.h` (header-only, JUCE+Qt) — ONE resolution +
write path used by every surface:

```cpp
struct FxParamWrite { int trackIndex; int slotIndex; int paramIndex; int paramIndexValid;
                      QString paramName; QString intent; double value; bool normalized; };
enum class FxWriteKind { internal, plugin };
struct FxWriteOutcome { bool ok; FxWriteKind kind; float writtenValue; int overrides; QString error; };
FxWriteOutcome applyFxParamWrite(ProjectCommands&, const FxParamWrite&);
```

Resolution order preserved verbatim: `paramIndex` > `paramName` > `intent`
(shared `resolveInternalFxIntent`); plugin + intent refused with the existing
`pluginIntentRefusalText()`.

Add `src/common/FxParamBatchJson.h` — strict parse of `writes:[…]` mirroring
`BatchEditJson.h` (per-write `additionalProperties:false`, shared refusal bytes).

Command layer (`ProjectCommands.h` + `AudioEngineCommands_Fx.cpp`):

```cpp
virtual BatchResult setFxParams(const std::vector<FxParamWrite>& writes,
                                std::vector<std::string>* errors) = 0;
```

Semantics: **partial-apply with per-write errors** (the `set_cells` precedent —
a bad recipe does not drop the other 150 writes), all inside ONE
`beginTransaction`/`endTransaction`; return `{ok, written, failed}` + parallel
`errors[]`. (Contrast `set_notes_gain`'s validate-then-apply: an id batch must
be all-or-nothing because ids reference structure; a config batch is not.)

MCP `set_fx_params`:
```
{tracks?no, mode?: "real"|"normalized" (default "real"),
 writes: [{trackId|trackID, slotIndex, paramIndex?, paramName?, intent?, value, mode?}]}
```
`mode` per-write overrides the top-level. Plugin slots always normalized (as the
single tool). Response: `{ok, written, failed, errors:[…]}`.

RPC twin `project.setFxParams` (same arg names, `Router_Project.cpp`), sharing
the parser so refusal bytes match by construction.

## Slice 2 — bus + LFO batch wrappers
- `set_bus_fx_params {writes:[{busID,paramIndex,value}]}` → `setBusFxParams`
  (one transaction; `AudioEngineCommands.cpp`).
- `set_lfo_params {trackId|trackID, lfoIndex, params:{waveform?,rate?,rateSync?,
  depth?,bipolar?,phaseOffset?,targetParamID?,enabled?}}` → `setLfoParams`
  (one transaction; `AudioEngineCommands_Modulation.cpp`).
- `add_sends {sends:[{trackId|trackID,busTarget,level?,isPreFader?}]}` and
  `add_buses {buses:[…]}` (batch creators, one transaction each).
- Incremental variants (`increment_fx_params` `{deltas[]}`) deferred — state the
  deferral; add only if a real use appears.

## Slice 3 — unify the singles (kills the second resolution path)
Route `set_fx_param` / `set_internal_fx_param` / `set_bus_fx_param` /
`set_lfo_param` handlers through the shared resolver, keeping their exact output
texts (`"ok"`, `"ok (paramIndex N clamped: a -> b)"`, `"ok overrides=N"`). The
resolver returns structured data; each surface renders its own text. Pinned by
twin tests asserting byte-identical payloads before/after.

## Slice 4 — bug fixes (see §Bug fixes)

---

## Bug fixes

1. **`verify_part {soloOnly:true}` reports `nonClipping=0` / `bandsPresent=0`
   for a clean part.** Reproduced: soloPeak 0.0901 → `nonClipping=0` with
   `mixMeasured=false`; same call `soloOnly:false` → `nonClipping=1`. Fix:
   compute the booleans from whichever render exists (solo when no mix), or omit
   them when not measured — never 0. Test both modes.
2. **`verify_window` docs vs validation.** Description says
   `ceilingHitPctMax (percent of frames … >= 0.999, at most)` and "a
   `targets:{ceilingHitPctMax:0}` check passes when the clamps are OUTSIDE the
   window" — but 0.00002% of 298 s is ~3 frames, so `:0` fails on any real
   render and contradicts the prose. Either align prose to semantics
   (document that `:0` means literally zero clamp frames) or accept a frame
   tolerance. Decide + fix text.
3. **`set_notes_gain` description omits the valid range.** Live `gain: 4` was
   accepted. Confirm the intended clamp (per-note gain 0.0–2.0 per
   `ProjectCommands.h`) and either clamp with a visible report or state the
   range in the description.
4. **`masterRms` unit wording.** `brief.schema.json` says "e.g. 0.16"; make it
   explicit: linear mono-downmix `(L+R)/2` RMS, same units as `mix_report`'s
   `rms`. Also add the free-loudness note: a ~1.15 peak-to-RMS mix needs peak
   ≈0.104 to hit 0.09 (the `mangrove_dub` −12.8 dB master result).

## Success gates

- [ ] `set_fx_params` writes N params in ONE undo unit on BOTH surfaces
      (twin test: two surfaces equal payload; one undo restores every value).
- [ ] `mode:"real"` and `mode:"normalized"` both round-trip through
      `get_internal_fx_param` (the written value reads back).
- [ ] Unknown track / slot / param, typo'd key, non-integral index → refused
      with identical bytes on both surfaces (or a per-write error row).
- [ ] `set_bus_fx_params` / `set_lfo_params` / `add_sends` twins green.
- [ ] Singles byte-identical after Slice 3 (twin tests before/after).
- [ ] `verify_part` soloOnly fix: quiet part → `nonClipping=true`; clipping
      part → `false`; full-mix mode unchanged.
- [ ] `node tools/rpc_parity_map.mjs` regenerated; ratchet test green.
- [ ] `build-fast.sh test` + the four `hdaw_tests_*` suites pass at the new
      baseline; `everything` count == intended.
- [ ] Live smoke on the shared engine: a `mangrove_dub`-style voicing pass in
      ≤ ~10 calls (vs 153), one `undo` reverts it.

## Slice order (each = one subagent, per hdaw-guard)

1. `FxParamResolve.h` + `FxParamBatchJson.h` + `ProjectCommands::setFxParams` + engine impl + gtest (command-level).
2. MCP `set_fx_params` + RPC `project.setFxParams` + twin test.
3. Slice 2 wrappers + twins.
4. Slice 3 unify-singles + byte-parity twins.
5. Slice 4 bug fixes + tests.
6. Parity-map regen + docs (`tool_help` examples, AGENTS wording) + full build + smoke.

## Risks

- **Byte-parity of the singles** (Slice 3) is the highest-risk step — a moved
  clamp or reordered resolution changes behaviour. Mitigation: twin tests pin
  payloads before the refactor; keep resolution order literal.
- **Partial vs all-or-nothing** semantics differ from `set_notes_gain`; document
  the deliberate divergence in both descriptions.
- Undo granularity: verify `undoDepth()` +1 exactly (the `set_notes_gain`
  precedent test).
