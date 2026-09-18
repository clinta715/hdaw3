# Plan: offline param-override replay (matrix-preset ear-pass enabler, 2026-09-17)

## Goal
Applied matrix-preset parameter overrides must reach offline renders (export_audio /
verify_part) for plugins whose getStateInformation does not serialize param-driven
state (JE8086 measured: captured state bit-identical to boot even with 44 params
applied and the live graph clocking ~100 blocks/s). Mechanism: store the RESOLVED
param overrides (live param index -> normalized value) on the FX_SLOT ValueTree at
apply time; replay them onto the offline slot's param cache in the export path after
prepare/bake and before the first rendered block.

## Mechanism
1. WRITE (McpTools_Matrix.cpp, param apply — applyParamsToSlot callers at :528/:649):
   after a successful applyParamsToSlot, write the resolved overrides to the slot tree:
   slotTree.setProperty(IDs::appliedParamOverrides, <compact JSON {"<liveParamIndex>":
   <normalizedValue>, ...}>) — indices are the LIVE param indexes already resolved by
   the tool (index map / step params); values normalized 0..1. Nulls/unmapped skipped.
   Location for the slotTree: getProjectModel().getTrackListTree().getChild(ti).
   getChildWithName(IDs::FX_CHAIN).getChild(si) — same access the capture receipt uses.
2. REPLAY (ExportManager::renderThreadFunc): after renderGraph.prepareToPlay + the
   bounded bake wait and BEFORE the block loop: for each offline track's plugin slot,
   read IDs::appliedParamOverrides; for each {index, value}: call the slot's
   setAutomationParam(index, value) — TrackFXSlot.h:505 (atomic paramValues cache;
   applyAutomation pushes to the child during process). Guard: only when the slot's
   param cache covers the index (paramValues non-empty); log HDAW_LOG("ParamReplay",...)
   applied count. Access the offline tracks via the offline RoutingManager (same access
   the bake wait uses).
3. IDs::appliedParamOverrides: new juce::Identifier next to IDs::pluginState (find the
   IDs definition the rest of the FX_SLOT properties use).
4. PERSISTENCE: check whether ProjectSerializer preserves extra FX_SLOT child
   properties through save/load. If yes: free persistence, document. If whitelisted:
   add the property to the whitelist (save+load round-trip test), since preset applies
   must survive save/load for the offline render of SAVED projects.

## Success gates
- G1: unit/fixture test — apply writes IDs::appliedParamOverrides with resolved
  indices; captureToTree=false path also writes it (overrides are orthogonal to the
  state capture).
- G2: replay applies before the first rendered block (render-harness or fixture-level
  assertion that setAutomationParam was invoked with the stored pairs; live proof:
  JE8086 ear-pass re-render shows wide rms spread + distinct md5s per preset).
- G3: save/load round-trip preserves the property (or documented as session-scoped
  with the serializer limitation cited).
- G4: build green; MatrixPresetsTest.* + McpCoverageTest.* pass.
- G5: slots WITHOUT the property behave exactly as before (no replay, no log spam).
- G6: no audio-thread allocation/locks (replay runs on the render thread BEFORE the
  block loop; setAutomationParam is a relaxed atomic store — safe).

## Blast radius / guards
- Replay touches the offline render SETUP phase only (before the block loop); the
  block loop, isolation lifecycle, proxy protocol, and live graph are untouched.
- Slots without the property: zero behavior change (G5).
- Live path unchanged (setParam already works live).
- Serialization: read ProjectSerializer.cpp's FX_SLOT handling (line ~41/75-94 region)
  and mirror how pluginState is persisted for the new property if a whitelist exists.

## Steps
1. IDs: add appliedParamOverrides identifier.
2. McpTools_Matrix.cpp: write the property in both apply call sites (after successful
   applyParamsToSlot; include ALL resolved {index: value} pairs from the apply).
3. ExportManager.cpp: the replay (step 2 of Mechanism) after the bake wait, before the
   first block.
4. Serializer persistence per gate G3.
5. Tests per G1/G2/G3.
6. build-fast.bat all + MatrixPresetsTest.* + McpCoverageTest.*.
