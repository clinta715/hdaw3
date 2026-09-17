# Plan: live-routing seam fix (matrix-preset session, 2026-09-16)

## Goal
Make every live-graph consumer (send_fx_midi family incl. load_nord_bank / load_virus_preset;
PluginParamService getParams/getParamText/setParam) resilient to an unbuilt live routing
projection: settle it on demand instead of failing.

## Root cause (proven live)
Headless MCP engine + no working audio device (RDP: "No driver", output-only retry
failed) -> prepareToPlay never runs -> the LIVE MainAudioProcessor track array stays
null even though ValueTree/ReadModel/offline-render paths work. Every consumer reading
proc->getTrack(i) fails: sendFxMidi => "track not found: N" (all loaders),
PluginParamServiceImpl::getParams => {} (=> list_fx_params/set_fx_param empty).
`load_project` (full rebuild) fixed both live — proving the seam.

## Success gates
- [ ] G1: new gtest(s) prove: after add_track/add_fx WITHOUT an explicit drain,
      sendFxMidi resolves the track and a plugin slot lists non-empty params
      (these fail on pre-fix code per the lesson-9 deferral repro).
- [ ] G2: existing suites touched by the change pass (MCP coverage fx tests,
      PluginIsolation params tests, commands tests).
- [ ] G3: build succeeds (flat Ninja RelWithDebInfo; build-fast.bat all).
- [ ] G4: live verification on the MCP engine after rebuild+relaunch: list_fx_params
      returns 461 JE8086 params right after add_instrument_part (no load_project),
      and load_nord_bank queues dumps.
- [ ] G5: zero audio-thread/processBlock changes; diff limited to the files below.

## Dependency map
- Blast radius: command/service layer only. No DSP, no processBlock, no SPSC, no
  ValueTree schema. The drain/full-rebuild paths invoked are the existing serialized
  ones (lesson 12/18 idioms already inside them).
- Upstream: MCP tools (loaders, list/set_fx_param), frontend RPC (same commands).
- Downstream: verify_part/export unaffected (offline graph).

## Pitfall gates
- Lesson 12/18: never hand-roll graph mutation; reuse drainPendingRoutingRebuild()
  (message-thread-safe per its contract) and the existing full-rebuild wrapper
  (AudioEngineCommands::rebuildRoutingGraph in AudioEngineCommands_Undo.cpp or
  MainAudioProcessor::rebuildRoutingGraph — the pump-park reference).
- Lesson 2: no setProperty side-effect reliance.
- Lesson 15: verify the BINARY after build (mtime/md5) before live testing.

## Steps
1. AudioEngine: add `bool ensureLiveRouting(int trackIndex)` — true when
   getMainProcessor()->getTrack(trackIndex) != nullptr after: (a) immediate check,
   (b) drainPendingRoutingRebuild() + retry, (c) one full rebuild via the existing
   serialized path + retry. Bounded: at most one full rebuild per call.
2. PluginParamServiceImpl: add an injectable `std::function<bool(int)> ensureLive_`
   (default no-op); AudioEngine wires it at construction. Call it in resolveInstance
   when getTrack returns null, then retry once.
3. AudioEngineCommands_Fx.cpp sendFxMidi: when proc->getTrack() is null, call
   engine_.ensureLiveRouting(params.trackIndex) and refetch; keep the error path
   unchanged if still null.
4. Tests per G1 (follow the lesson-9 deferral repro pattern used by existing tests),
   plus a test that ensureLiveRouting is a no-op when the graph is already settled.
5. Build + run targeted suites; report filter names + outputs.


## Live verification (post-fix, engine relaunch 20:39 from rebuilt binary)

- **Bug A (workaround via paramIndex + name map):** `list_fx_params` returns the full
  461-param table on a fresh add_instrument_part (no load_project needed post-rebuild;
  the ensure-seam also makes it self-healing on engine restarts). `set_fx_param` by
  NAME still rejects decoder names — the plugin publishes display names like
  "A FLT CUTOFF FREQ" / "A AMP ENV ATTACK" (R5). Token-fold matcher
  (/tmp/match_preset.py; persisted map: timbre-lib/matrix_presets/je8086_param_index_map.json)
  matches **46/48** params of preset b44052f76c82a7a7 ("fx mode 2 + heavy cross-mod +
  bright + routed LFO/env destination"); AutoPanManualPanSwitch has no published param
  and MultiEffectsType is genuinely unpublished (only MULTI-FX LEVEL exists) — both
  recorded as R5 residue. Applied **46/46 set_fx_param calls OK**; A/B: soloRms
  0.0704->0.0521, soloPeak 0.3838->0.2427, mixPeak 0.316->0.557 (audible, non-clipping).
- **Bug B (fixed):** load_nord_bank queued 111 SysEx dumps (24391 B); load_virus_preset
  queued bank=0 program=12 — both immediately after add_instrument_part, no manual drain.
- **Demo v2:** compositions/matrix-presets-demo-v2.wav (42 s, peak 0.251, rms 0.0141,
  pumpDepth 10.1, bands bass+body dominant, no clipping) + matrix-presets-demo-v2.hdaw3.
  Tracks: JE8086 lead (matrix preset b44052f76c82a7a7 + tempo-synced delay), Osirus bass
  (ROM preset A-12), NodalRed2x pad (Andi Vax bank 1 program 0).
- Test evidence: LiveRoutingSeam 5/5 (incl. real-plugin regression with
  HDAW_REAL_PLUGIN_TESTS=1); targeted FX/PluginIsolation regressions 9/9.


## Vavra injectability: MEASURED NEGATIVE (2026-09-16, live engine)

The dump-writer (`timbre-lib/vavra_dump.py` + `matrix_presets/vavra_morphs.json`, 5 pairs
x 4 steps, each step a full 392-byte single-dump SysEx built from the parent patch's
original .syx with interpolated offsets) was tested against the live Vavra slot:

- `send_fx_midi` accepted both dumps (`queued=1`).
- `verify_part` solo renders before / after step 1 / after step 4: soloRms 0.00723 /
  0.00708 / 0.00721, peaks 0.0828 / 0.0821 / 0.0836 — identical within render noise.
- `get_fx_capture_status` after both: `status=unchanged stateBytes=0 hasPluginState=0`
  (per the D-lite convention, an unchanged capture means the child state did not move).

Conclusion: **the Vavra emulation does not apply external single-dump SysEx** — the
transport queues it, but the child's patch state stays at boot. This closes the microQ
pipeline's 'remaining check' question with a measurement instead of a hope. The morph
JSON + dump writer remain useful as blueprints (and for any future firmware/emulation
that wires `State::receive(Origin::External)`); the ONLY untested vavra apply path left
is front-panel puppetry (EmuButtons/EmuRotaries remote-control SysEx).


## Xenia injectability: MEASURED POSITIVE (2026-09-16, live engine)

The xt mapping was verified at microQ-grade rigor (1,166,386 sidecar values byte-matched
against raw dumps, 0 mismatches; rule identical: dump byte = 7 + linear param index;
checksum = sum(7..262)&0x7F, never validated on receive) — see
timbre-lib/matrix_presets/xenia-offset-map.md. Deliverables: xenia_offset_map.json
(85 entries), xenia_dump.py (writer), xenia_morphs_injectable.json (the 5 xenia morph
pairs, each step now carrying a full 265-byte SysEx).

Live test (fresh Xenia slot, pair 17:21 bright->squelch, injected to BANK 0x20 = single
edit buffer for instant audibility):
- pre:            soloRms 0.02928, soloPeak 0.4432
- after step 1:   soloRms 0.03489 (+19%), soloPeak 0.4742
- after step 4:   soloRms 0.02948, soloPeak 0.4559
Deltas are far outside the vavra render-noise band (+/-2%) and track the morph
semantics (cutoff up -> brighter). Compare Vavra: identical renders + captureStatus
unchanged. Caveats: (a) get_fx_capture_status stays 'unchanged' — the deferred capture
reads the program, not the edit buffer, so tree-capture does not reflect edits; (b)
~50% of .mid-derived named values in xenia.json are off-by-2 (harvest labeled mid keys
directly with vocab names) — xenia_morphs_injectable.json records per-pair provenance
('verified' vs 'vocab-shifted'); fixing the labeling is a follow-up to the harvester.


## Nord morph performance: VERIFIED AUDIBLE (2026-09-16, live engine)

The Nord Lead 2x writer shipped with decisive map validation: 5,350 files re-parsed by
an independent strict parser — 353,100 sidecar values byte-matched, 0 mismatches.
Packing: `dump[6+2i]` lo-nibble + `dump[7+2i]`<<4 (payload at 5+2i); byte 52 is a packed
flag byte (Sync bit0 / RingMod bit1 / Distortion bit4); no checksum anywhere.

Deliverables: nord-offset-map.md (17-param offset table + 66-entry json map),
nord_dump.py (load/patch/write, byte-fidelity outside overrides, bit-RMW for byte 52),
nord_morphs/ (5 pairs x 4 steps as 139-byte .syx) + nord_morphs.json. Tests: 24 new +
decoder suite = 37 passed.

Live A/B (fresh NodalRed2x slot, pair 34:39 d=0.096, injected via load_nord_bank):
- baseline (boot):     soloRms 0.02517, soloPeak 0.2177
- after step 1 (25%):  soloRms 0.02827, soloPeak 0.6205  (2.9x — resonant character)
- after step 4 (100%): soloRms 0.02561, soloPeak 0.2192  (parent 39's quieter voice)
Each step loads as 3 Clavia messages and audibly reshapes the patch — the Nord morph
chains are performable: write .syx -> load_nord_bank -> play the chain.
