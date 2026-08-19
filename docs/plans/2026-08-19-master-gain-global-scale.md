# Plan — master gain + global-scale gain staging (handoff item #1)

Date: 2026-08-19
Status: approved for implementation

## Goal

FM instruments clip at fader 1.0 (preL up to 8); `autoGainToTarget` clamps and
has no fallback. Fix in two steps:
- **Task A — master gain infrastructure:** real master-bus gain — persisted root
  property, live path, restore-on-rebuild, `setMasterGain` command + RPC + MCP,
  snapshot exposure, and wiring of the ALREADY-EXISTING-but-dead frontend
  master fader (`Mixer.tsx` fabricates a Master strip whose fader currently
  calls `project.setTrackVolume` with trackIndex −1 → silent no-op).
- **Task B — global-scale step in `autoGainToTarget`:** when `clamped` AND a
  full-mix render clips, optionally scale the master bus down and raise the
  target fader proportionally (into the headroom just created), re-render to
  verify. Opt-in via `allowGlobalScale` (default false → zero behavior change).

## Success Gates

### Task A
- [ ] A1: `MasterBusProcessor` applies gain in `processBlock` — realtime-safe:
      `std::atomic<float>` written off-thread, `SmoothedValue` (multiplicative)
      loaded+applied on the audio thread (ClipSourceProcessor.h:429 idiom).
      Meter reads POST-gain.
- [ ] A2: `IDs::masterGain` root property; stamped 1.0 in
      `createDefaultProject`; persisted free by the whole-tree serializer;
      `ProjectModel::getMasterGain()` helper.
- [ ] A3: **Gate 1/10 restore:** `RoutingManager::rebuildFromValueTree` sets
      the freshly created MasterBusProcessor's gain from the tree; gtest
      mutates gain → `rebuildRoutingGraph()` → asserts LIVE
      `getMasterBus()->getGain()` (not ReadModel).
- [ ] A4: Live path — root-property branch in
      `AudioEngine::valueTreePropertyChanged` (beside the `IDs::tempo` branch
      at AudioEngine.cpp:778) drives the live master bus (null-guarded).
- [ ] A5: `ProjectCommands::setMasterGain(float)` (undoable tree write, mirrors
      setTrackVolume) + RPC `project.setMasterGain` + MCP `set_master_gain`
      (parity) + `masterGain` in `ProjectSnapshot` (ReadModel.h struct,
      ReadModelImpl::snapshot, FrontendRpc.h toJson, TS type).
- [ ] A6: Frontend master strip wired: `Mixer.tsx` passes
      `volume: snapshot.masterGain ?? 1`; `MixerStrip` commits
      `project.setMasterGain` when `isMaster` (and hides the pan fader for
      master). Vitest covers the master commit path.
- [ ] A7: Render-attenuation proof: an export/tree-copy render at master 0.5
      measures ~half the peak of the same render at master 1.0 (pattern:
      `tests/unit/engine/export_volume_bypass_test.cpp`).
- [ ] A8: Save/load round-trip preserves masterGain.
- [ ] A9: Full suite green; frontend `npm run build` + `npm test` green.

### Task B (after Task A is committed)
- [ ] B1: `autoGainToTarget(trackIndex, targetRms, windowSeconds, verify,
      bool allowGlobalScale = false)` — when clamped AND allowGlobalScale:
      full-mix render (renderTrackWindow, soloMuteOthers=false, applyFader at
      1.0) → if mixPeak ≥ 1.0: `scale = 1/mixPeak`, new master =
      current·scale, fader = min(unclampedTargetFader, mixPeak) — the fader
      rises into the created headroom toward its target but never past the
      original unclamped target. ONE transaction ("Auto gain stage") writes
      fader + master gain (atomic undo). Re-render full mix to verify.
- [ ] B2: `GainStageResult` + `globalScale` (1.0 = none), `masterGain`,
      `mixPeak` fields; RPC/MCP responses extended; `allowGlobalScale` plumbed
      through `InstrumentPartParams` / `composition.addInstrumentPart` /
      `add_instrument_part` / `auto_gain_to_target`.
- [ ] B3: gtest: loud fm_synth part (fixed seed) with high targetRms →
      clamped=true, globalScale<1, masterGain<1, verified post-scale mixPeak
      < 1.0; default path (allowGlobalScale=false) leaves masterGain=1.0.
- [ ] B4: Reuse renderTrackWindow ONLY — no new render loop (handoff rule).
- [ ] B5: Full suite green.

## Dependency Map (verified)

- `MasterBusProcessor` (src/engine/MasterBusProcessor.h) — passthrough+meter
  today; created in `RoutingManager::rebuildFromValueTree` (RoutingManager.cpp:92)
  on EVERY rebuild → restore point. `RoutingManager::getMasterBus()` exists
  (RoutingManager.h:94); meters already read via it (MainAudioProcessor.cpp:723).
- Root-property precedent: `IDs::tempo` — root tree prop, listener branch at
  AudioEngine.cpp:778-782, serializer persists whole tree
  (ProjectSerializer.cpp:103 `toXmlString`), load copies all root props (:138).
- Track-volume live precedent: tree write (AudioEngine.cpp:531) → listener →
  SPSC → Track smoothed gain. Master has ONE processor → direct atomic set,
  no SPSC needed.
- Export/tree-copy renders go through the same graph incl. master bus
  (ExportManager.cpp:391 comment: Track → MasterBus → AudioOutput) → the
  gain-stage/verify renders hear master gain automatically.
- Frontend: `Mixer.tsx:35-49` fabricated master strip (volume hardcoded 1);
  `MixerStrip.tsx:31-34` commitVolume → `project.setTrackVolume` (dead for
  index −1). `MixerStrip.test.tsx:61` has a master-strip test.
- God nodes touched: none (AudioEngine listener gets one additive branch).
- Projections: ReadModel snapshot (new field), audio graph (master gain).
  Delta/fullSync: root-property change → fullSync (existing rule) → master
  strip resyncs after RPC.

## Pitfall Gates Triggered

- **Gate 1/10 (THE key gate):** master gain is processor state → restore in
  rebuild + live-processor test (A3).
- Gate 2: full path RPC→tree→listener→processor→AUDIBLE effect; A7 proves the
  audible link (render attenuation), A3 the rebuild survival.
- Gate 3: atomic + SmoothedValue only in processBlock; no alloc/lock/I-O.
- Gate 13: listener writes a scalar atomic on MasterBusProcessor — no DSP
  object recreation, no stateLock needed (documented in code comment).
- Gate 4/15: rebuild + binary timestamp check before tests.
- Gate 5 (frontend): MixerStrip local state pattern unchanged — matches the
  existing track-strip convention.
- Lesson 2: setMasterGain with an unchanged value is a legitimate no-op
  (nothing to drive) — listener-driven is safe here; the command does NOT
  depend on listener side-effects for correctness (restore path covers
  rebuilds).
- Beats-vs-seconds: not touched (windowSeconds already seconds).

## Task A — exact edits

1. `src/model/ProjectModel.h` — `DECLARE_ID(masterGain)` (near scaleRoot:214);
   `src/model/ProjectModel.cpp` — stamp `masterGain=1.0` in
   createDefaultProject (near scaleRoot:67); add `float getMasterGain() const`
   (root prop, default 1.0; mirror getScaleRoot:82 style).
2. `src/engine/MasterBusProcessor.h` — add:
   ```cpp
   void setGain(float g) { gain.store(std::max(0.0f, g), std::memory_order_relaxed); }
   float getGain() const { return gain.load(std::memory_order_relaxed); }
   ```
   members: `std::atomic<float> gain{1.0f};` +
   `juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmooth;`
   In `prepareToPlay`: `gainSmooth.reset(sampleRate, 0.02);` (20 ms ramp) +
   `gainSmooth.setCurrentAndTargetValue(gain.load(...));`
   In `processBlock` AFTER the scratch copy-back, BEFORE `meter.update`:
   `gainSmooth.setTargetValue(gain.load(std::memory_order_relaxed));` then per
   channel/sample multiply by `gainSmooth.getNextValue()` (single per-sample
   gain shared across channels — compute once per sample, apply to both).
   Comment: realtime-safe idiom per ClipSourceProcessor.h:429.
3. `src/engine/RoutingManager.cpp` `rebuildFromValueTree` — after
   `masterBus = ...` (line ~94): `masterBus->setGain(projectModel.getMasterGain());`
4. `src/engine/AudioEngine.cpp` `valueTreePropertyChanged` — in the
   `hasType(IDs::PROJECT)` branch (line ~778), beside `IDs::tempo`:
   ```cpp
   else if (property == IDs::masterGain)
   {
       float g = treeWhosePropertyHasChanged.getProperty(IDs::masterGain, 1.0);
       if (mainProcessor != nullptr)
           if (auto* rm = mainProcessor->getRoutingManager())
               if (auto* mb = rm->getMasterBus())
                   mb->setGain(g);
   }
   ```
5. `src/common/ProjectCommands.h` — `virtual void setMasterGain(float gain) = 0;`
   (near setTrackVolume:20); `src/engine/AudioEngineCommands.h` override;
   `src/engine/AudioEngineCommands_Tracks.cpp` impl beside setTrackVolume(:61):
   `engine_.getProjectModel().getTree().setProperty(IDs::masterGain, (double)gain, &um);`
   with a beginTransaction/endTransaction "Set master gain" wrapper IF that is
   how setTrackVolume does it (read it first — match exactly).
6. Snapshot: `src/common/ReadModel.h` ProjectSnapshot + `float masterGain = 1.0f;`;
   `src/engine/ReadModelImpl.cpp` snapshot() + `snap.masterGain = model_.getMasterGain();`;
   `src/frontend/FrontendRpc.h` toJson(ProjectSnapshot) + `{ "masterGain", ... }`;
   `frontend/src/rpc/types.ts` ProjectSnapshot + `masterGain?: number;`.
7. RPC: `Router_Project.cpp` — `m == "setMasterGain"` case (requireFloat gain,
   mirror setTrackVolume case); find the setTrackVolume case and copy shape.
8. MCP: `McpTools_Project.cpp` — `set_master_gain` tool (schema gain number
   min 0, required) + include masterGain in whatever read tool surfaces project
   reads IF one exists for track volumes (check `read_project`-like tool around
   line 60-70 that emits volume — extend only if trivially analogous).
9. Frontend: `Mixer.tsx` master strip `volume: snapshot?.masterGain ?? 1`;
   `MixerStrip.tsx` commitVolume → `project.setMasterGain` when isMaster;
   hide pan fader when isMaster (master has no pan). Keep M/S/R hidden.
10. Tests:
    - NEW `tests/unit/engine/master_gain_test.cpp` (+ tests/CMakeLists.txt):
      * `MasterGain.SnapshotAndCommand` — default 1.0; setMasterGain(0.5) →
        snapshot().masterGain == 0.5; undo() → back to 1.0.
      * `MasterGain.SurvivesRebuild` (Gate 1/10) — set 0.5 →
        `engine.getMainProcessor()->rebuildRoutingGraph()` →
        `getRoutingManager()->getMasterBus()->getGain()` ≈ 0.5.
      * `MasterGain.SaveLoadRoundTrip` — set 0.25 → saveProject(temp) →
        newProject → loadProject(temp) → getMasterGain() ≈ 0.25 (use
        ProjectCommands save/load; temp file cleanup).
      * `MasterGain.RenderAttenuation` — compose a deterministic loud-ish
        MIDI part on fm_synth (fixed seed), render the same window via the
        export path at master 1.0 and 0.5 (pattern:
        export_volume_bypass_test.cpp peakOf/waitForExport), assert
        peak(0.5) ≈ 0.5·peak(1.0) within 10%.
    - `tests/unit/frontend/frontend_server_test.cpp` —
      `FrontendServer.SetMasterGainRpc`: project.setMasterGain 0.5 →
      read.snapshot masterGain ≈ 0.5.
    - `tests/integration/mcp/mcp_server_test.cpp` — `McpServer.SetMasterGainTool`.
    - Frontend Vitest: extend `MixerStrip.test.tsx` — master strip commit
      calls `project.setMasterGain` (mock rpc like neighboring tests).

## Task B — exact edits (after Task A committed)

1. `src/common/ProjectCommands.h` — GainStageResult + `float globalScale = 1.0f;
   float masterGain = 1.0f; float mixPeak = 0.0f;`; autoGainToTarget +
   `bool allowGlobalScale = false` trailing param; InstrumentPartParams +
   `bool allowGlobalScale = false;`.
2. `src/engine/AudioEngineCommands_Composition.cpp` autoGainToTarget —
   restructure: raw render → fader calc (unchanged) → if
   `result.clamped && allowGlobalScale`: mix render
   `renderTrackWindow(engine_, trackIndex, windowSeconds, 1.0f, true, false)`
   → if `mix.peak >= 1.0f`: `scale = 1.0f / mix.peak;` `newMaster =
   model.getMasterGain() * scale;` `fader = std::min(targetRms / raw.rms,
   mix.peak);` (unclamped target, capped at compensation) — then ONE
   transaction writing fader + master gain. Else existing single-write path.
   Verify pass (when verify OR global scale applied): full-mix re-render
   `renderTrackWindow(..., fader, true, false)` → result.mixPeak; keep
   existing solo-verify semantics for `measuredRms`/`peak` as today.
   Delete all temp files on all paths.
3. addInstrumentPart — pass `params.allowGlobalScale` to autoGainToTarget.
4. RPC Router_Composition.cpp — autoGainToTarget case + `allowGlobalScale`
   optBool; response + globalScale/masterGain/mixPeak; addInstrumentPart case
   + param; gain JSON + new fields.
5. MCP McpTools_Project.cpp — auto_gain_to_target + add_instrument_part
   schemas/args + output fields.
6. Tests:
   - `tests/unit/engine/auto_gain_global_scale_test.cpp` (or extend the file
     that already hosts autoGainToTarget tests — grep first):
     * `GlobalScale.ClippedMixScaledDown` — loud part (find a seed/targetRms
       that clamps AND clips the mix deterministically; fm_synth, fixed seed):
       allowGlobalScale=true → clamped, globalScale<1, masterGain<1,
       mixPeak<1.0 after; master gain restored check NOT needed here (A3 has it).
     * `GlobalScale.DefaultLeavesMasterUntouched` — same part,
       allowGlobalScale=false → masterGain stays 1.0.
     * `GlobalScale.NonClippingMixUntouched` — quiet part (low targetRms… or
       part that clamps but mix < 1.0 — single quiet track can still clip the
       mix with FM peaks; use a gentle seed/target) → allowGlobalScale=true →
       globalScale==1.0.
   - RPC + MCP round-trips extend existing autoGainToTarget tests (grep
     `AutoGainToTargetRpc` / `AutoGainToTargetTool` equivalents).

## Out of scope

- Track-fader UI range > 1.0 display (engine allows volume > 1; UI fader
  clamps visually — note only).
- Master automation, master pan, master FX.
- Plugin on/off RMS delta (verifyPart wishlist).
