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


## Virus writer + R3 tools: shipped, with three new findings (2026-09-17)

Shipped: virus_dump.py (TI 524B + B/C 267B program writer, checksum recomputed,
byte-fidelity, round-trip tested — 81 tests); virus_morphs.json upgraded to injectable
per-step SysEx (4 B/C pairs + 1 TI pair, B/C-preferred base resolution since OsTIrus is
not installed here); R3 MCP tools list_matrix_presets + apply_matrix_preset
(McpTools_Matrix.cpp; fixture gtest 7/7; McpCoverage 102/102; McpServer+Workflow 43/43).
Live: list_matrix_presets {engine:'virus'} returns all 40 presets + 5 morphs;
apply_matrix_preset dispatched a virus morph step (queued=1 via the SysEx path).

NEW FINDINGS (open):
- F-A: The Osirus slot renders BIT-IDENTICAL digital silence (soloRms 2.18844e-06) under
  EVERY ROM program and every injected dump — it has been inaudible in every session
  today. Engine log shows TWO spawnPluginHost/ctor pairs for Osirus slotId=1 ~500ms apart
  (double child instantiation?) — list/params may target one instance while audio plays
  (nothing) through another. Needs: slot-instance audit for add_instrument_part on Osirus.
- F-B: set_fx_param name resolution diverges from list_fx_params on Osirus: the list
  shows 'Chorus Mix' (x16, per-part), set_fx_param 'Chorus Mix' -> 'unknown paramName'
  although both call PluginParamService::getParams(trackId, pluginId). Repro on the live
  engine; cause unclear (possibly name formatting via getName(128) vs snapshot name, or
  the divergence is a symptom of F-A's double instance).
- F-C: Virus C/TI program dumps: format-verified but live audibility UNRESOLVED pending
  F-A (the A/B cannot run against a silent slot). The writer itself is trusted (checksum +
  round-trip + byte-fidelity tests).


## F-A investigation complete: root-cause chain + probe results (2026-09-17)

READ-ONLY investigation (osirus-silence-investigation agent, full report in session log)
+ two live probes. Summary of the confirmed chain:

1. Every verify_part / export builds a FRESH offline plugin domain and spawns fresh
   isolated children (confirmed: 3 Osirus spawns inside one second during one verify;
   createPluginInstance result: ok at sr=48000 — the old fix-offline-clap-render
   identifier bug is NOT this).
2. The Virus emulator's ctor only completes the DSP boot gate (dspHasBooted, unbounded
   wait for model C); the emulated OS's post-boot bring-up AND its MIDI consumption
   advance ONLY when the host pulls audio (virusLib/device.cpp:598-600), and the isolated
   child paces render-mode blocks at 1x real time (PluginHost.cpp:1444-1465).
3. verify_part schedules the part's notes at the FIRST block of the fresh child's life —
   the 4s window contains at most 4s of OS bring-up with notes queued at the moment the
   OS is least ready.
4. JE8086 differs structurally: its H8S core runs on a dedicated background thread in
   WALL time (jeLib/jeThread.h:29), reaching its held-MIDI flush (~12.8M cycles) in wall
   milliseconds — so its notes play.
5. PROBE RESULT: a 30-second solo export of the Osirus part is EXACT digital zeros
   (every sample) — the OS never turns on. This rules out pure bring-up timing and
   indicates a HARD DROP of the notes. Primary suspect: the Single-mode channel filter
   (virusLib/microcontroller.cpp:431-434: non-SysEx MIDI on channel !=
   m_globalSettings[GLOBAL_CHANNEL] is silently dropped; boot sets GLOBAL_CHANNEL=0x0,
   microcontroller.cpp:230). Secondary suspects: external-MIDI not routed to the OS in
   render mode, or per-note channel mismatch (phrase notes' JUCE channel -> status
   nibble vs the boot global channel).

NEXT PROBE (runtime, cheap): emit phrase notes across several channels (or inject
noteOn sweeps during a live capture) to find a channel the OS accepts; if none, the
drop is upstream of the OS (child MIDI-in routing) — emulator-level, out of HDAW's
hands except via a warmup/ready workaround.

FIX OPTIONS (from the investigation, for the standing stability discussion):
(a) child-local warmup pump after PREPARE (S-M effort, M risk, contained in
    PluginHost.cpp PREPARE case; env knob HDAW_NO_CHILD_WARMUP escape) — fixes bring-up
    timing for all emulated devices but NOT the hard drop;
(b) child->host ready-signal extension (M-L, M — IPC contract churn, low value: no true
    plugin-side readiness exists);
(c) warm-instance reuse across renders (L, HIGH — contradicts the offline-domain design;
    not recommended);
(d) lead-in/retry heuristics at the composition layer (S, S — buys bring-up time,
    cannot fix the hard drop).
Ranked: probe the channel question first (cheap), then (a) if timing-dominated.
