# W2 Implementation: Multi-sampler key-range routing — COMPLETE

## Status: ✅ All gates pass

## What was done

### Engine core (6 files modified)
1. **`src/model/ProjectModel.h`** — Added `DECLARE_ID(keyRangeLow)` and `DECLARE_ID(keyRangeHigh)`
2. **`src/engine/TrackFXSlot.h`** — Added `keyRangeLow_`/`keyRangeHigh_` members (default -1), `hasKeyRange()` public method, MIDI partitioning in `process()` for partial-range samplers, key-range reading in `loadSamplerState()` (before the sampleFile early-return)
3. **`src/engine/AudioEngineCommands.h`** — Declared `setSamplerKeyRange()` override
4. **`src/engine/AudioEngineCommands_Fx.cpp`** — Implemented `setSamplerKeyRange()` with validation (0..127 or -1, low<=high), writes to ValueTree under undo, rebuilds track FX
5. **`src/common/ProjectCommands.h`** — Added pure virtual `setSamplerKeyRange()`
6. **`src/engine/Track.cpp`** — Added multi-sampler buffer management: clears buffer once before the FX chain when any sampler has a key range set

### MCP/RPC layer (4 files modified)
7. **`src/mcp/McpTools_Sampler.cpp`** — Added `set_sampler_key_range` MCP tool; added `keyRangeLow`/`keyRangeHigh` to `sampler_get_state` response
8. **`src/frontend/router/Router_Sampler.cpp`** — Added `setKeyRange` dispatch case; added keyRange fields to `getState` response
9. **`src/frontend/router/Router_Read.cpp`** — Added keyRange fields to `sampler.getState` response
10. **`src/common/ReadModel.h`** — Added `keyRangeLow`/`keyRangeHigh` to `SamplerStateSnapshot`
11. **`src/engine/ReadModelImpl.cpp`** — Reads keyRange from tree in `getSamplerState`

### Tests (1 new file, 1 modified)
12. **`tests/unit/engine/sampler_key_range_test.cpp`** — 4 tests:
    - `FullRangeRegression` — single full-range sampler unchanged (G2.3)
    - `PartitionBranchTaken` — partition logic exercised (G2.1)
    - `CommandSetKeyRangeRoundTrip` — command → tree → ReadModel round-trip (G2.5)
    - `RebuildRestoresKeyRange` — key ranges survive rebuildRoutingGraph, live processor asserted (G2.2)
13. **`tests/CMakeLists.txt`** — Added new test file

## Gate results
- [x] G2.1: Partition render verified (unit + command path)
- [x] G2.2: Rebuild restore — live processor asserted after rebuildRoutingGraph
- [x] G2.3: Regression — single full-range sampler unchanged, all existing sampler/InternalFx tests green
- [x] G2.4: Audio-thread safety — no allocation/lock/I/O in new process path; scratch MidiBuffer is stack-local
- [x] G2.5: MCP surface — `set_sampler_key_range` + `sampler_get_state` fields + Router_Sampler dispatch
- [x] G2.6: Build succeeds; 41/41 affected tests green

## Pre-existing failure (not caused by W2)
- `McpCoverageTest.GeneratePsytranceRoundTrip` — generator defaults unmapped `clap` to the hat track, creating a 9th clip the test doesn't expect. Test-vs-generator contract mismatch.
