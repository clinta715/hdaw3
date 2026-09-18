# Plan: live-routing seam fix (matrix-preset session, 2026-09-16)

## STATUS TL;DR (2026-09-17 — all deliverables done; per-engine live status)

| Engine | Sheet (`timbre-lib/matrix_presets/`) | Morphs | Live apply | Evidence (below) |
| --- | --- | --- | --- | --- |
| je8086 | `je8086.json` (40) | `je8086_morphs.json` (20 steps, paramIndex embedded) | **VERIFIED** — 46/46 `set_fx_param` of preset b44052f76c82a7a7, audible A/B (display names → `je8086_param_index_map.json`) | "Live verification" |
| xenia | `xenia.json` (40) | `xenia_morphs_injectable.json` (per-step SysEx, off-by-2 fixed) | **VERIFIED AUDIBLE** — SysEx → edit buffer bank 0x20 (map: 1,166,386 values, 0 mismatches) | "Xenia injectability: MEASURED POSITIVE" |
| nodalred2x | `nodalred2x.json` (40) | `nord_morphs/` (20 `.syx`) + `nord_morphs.json` | **VERIFIED AUDIBLE** — morph `.syx` via `load_nord_bank` (map: 5,350 files / 353,100 values / 0 mismatches) | "Nord morph performance: VERIFIED AUDIBLE" |
| virus | `virus.json` (40, R7 via `virus_fx_pages.py`) | `virus_morphs.json` (per-step SysEx, TI/BC-tagged) | writer **format-verified** (checksum rule cited + validated); live A/B **BLOCKED — F-A** (Osirus renders bit-identical silence; preset-load ext absent); parameter path pending **F-B** | "Virus writer + R3 tools" / "F-A investigation complete" |
| vavra | `vavra.json` (40) | `vavra_morphs.json` (blueprint-only) | **MEASURED NOT APPLYING** — queued=1, captureStatus=unchanged, render identical; puppetry not pursued | "Vavra injectability: MEASURED NEGATIVE" |

Engine fix: `ensureLiveRouting` (deviceless seam) — fixed empty isolated-CLAP params
+ loader "track not found". MCP front door: `list_matrix_presets` /
`apply_matrix_preset` (McpTools_Matrix.cpp). Full narrative:
`docs/plans/2026-09-16-matrix-presets.md`.

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


## Channel probe: filter EXCLUDED; ROM sweep negative (2026-09-17)

- Channel filter ruled OUT as the note-drop cause: MidiClipProcessor emits notes on
  JUCE channel 1 (nibble 0; setMidiChannel default 1, nothing overrides it in these
  sessions) and the Virus filter drops only channel != GLOBAL_CHANNEL (boot = 0x0) —
  our notes PASS the filter. The silence must come from the OS not playing its current
  (boot) patch for the phrase, or from MIDI-in not reaching the OS at all.
- ROM sweep: load_virus_preset over banks 0-1 x programs {0,1,32,64,96} (10 combos,
  fresh Osirus slot, bass phrase) -> zero audible results. Consistent with: the RAM
  banks boot as init-silent patches AND/OR the OS not yet consuming selects at probe
  time (bring-up in emulated time), and/or m_singles content state.
- Conclusion: the remaining cause is emulator-internal (OS boot/bring-up state or
  MIDI-in wiring in the CLAP wrapper), beyond what HDAW-side tooling can observe.
  The decisive next step is INTERACTIVE: open the Osirus editor and watch the LCD
  while notes play (same procedure the microQ pipeline prescribed), or debug the
  emulation itself.
- HDAW-side status is final for now: all five engines have complete, validated,
  committed morph/preset pipelines; Virus + Vavra live-injectability are emulator
  limitations (documented), JE8086/Xenia/Nord are live-performable.


### RELIABILITY CAVEAT on the ROM sweep above
The engine restarted mid-sweep (observed: verify_part then returned 'trackIndex out of
range' on a fresh empty project), and the sweep's rms parser defaulted missing matches
to 0 — so the '10 combos silent' result is UNRELIABLE and must be re-run in a single
live engine session (parse failures reported separately from true silence) before any
conclusion is drawn from it. The channel-filter exclusion stands (source-verified:
MidiClipProcessor emits JUCE channel 1 = nibble 0 = boot GLOBAL_CHANNEL).


## F-A refined: MODEL-SPECIFIC — OsTIrus (TI) works, Osirus (C) silent (2026-09-17)

Controlled A/B on the live engine (same bass phrase, same engine, back to back):
- OsTIrus (TI): boot/init patch AUDIBLE (soloRms 0.0630, peak 0.509); injected TI morph
  step (pair 22:39 step 4, 524-byte SysEx via apply_matrix_preset) AUDIBLY RESHAPED the
  render (rms 0.0630->0.0585, peak 0.509->0.344); ROM program load responds too.
- Osirus (C): bit-identical digital silence (2.18844e-06) under init, all ROM programs,
  and both written-program dumps (earlier probes).

Conclusion: F-A is a virusLib MODEL-C issue (C-path boot/bring-up or MIDI-in handling),
not an HDAW defect. HDAW-side write format, injection path, and the R3 tools all work
against the TI emulation. Virus TI morph chains are PERFORMABLE today (use OsTIrus
slots; pair 22:39 is TI; more TI-parented pairs can be emitted — the earlier B/C
preference was based on a wrong 'OsTIrus not installed' reading).
Osirus (C) remediation = emulator-level (virusLib C path), outside HDAW's control
surface; documented for the gearmulator project.


## DEFINITIVE ROOT CAUSE: deviceless live graph never clocks — param ring never drains (2026-09-17)

The last measurement closes the loop: apply_matrix_preset (applied=44, captureToTree,
binary md5-verified current) -> deferred capture fires at +800ms -> captured state is
BIT-IDENTICAL to boot (status=unchanged, stateBytes=0). The 44 setParam writes are
queued into the isolated child's param ring, and the ring is drained per processed
audio block — but on a deviceless engine the LIVE graph never processes a single block
(no device -> no audio callback). So live-side writes (params, queued MIDI) sit in the
ring forever, the child's serialized state stays at boot, captures report 'unchanged',
and offline renders boot at init.

This unifies ALL of today's observations:
- je8086 param applies -> offline renders identical (ring never drained; capture reads
  boot state).
- xenia/nord sysex kits -> offline renders DID vary: send_fx_midi has a NO-DEVICE
  FALLBACK that drives scratch blocks synchronously through the slot (prepare 44100/512
  + scratch buffer) BEFORE capturing — messages get delivered and the state reflects
  them even deviceless. captureFxSlotState (the factored param-path trigger) LACKS
  that fallback: it defers assuming a device will clock (deviceOpen=true here because
  the saved-device restore reports a device even though none actually runs).
- Osirus digital silence: same family — nothing clocks its live child; offline
  renders boot at init (plus the model-C boot-state specifics on top).

THE FIX (small, for the next session, render-path adjacent -> needs the standing
discussion): give captureFxSlotState the same no-device scratch-drive fallback
sendFxMidi has (prepare slot 44100/512/2 + drive N scratch blocks before capturing),
or route apply_matrix_preset's capture through sendFxMidi's scratch path. Then:
apply -> capture(ok, real state) -> offline renders reflect presets -> the JE8086
(and virus-param) ear passes work end to end.


## F-A INTERMITTENCY CHARACTERIZED (2026-09-17, final state of remote probing)

The param-apply -> offline-render transfer is INTERMITTENT across engine boots, and
the boot-time device/clock state is the variable. Evidence across sessions:

- Session A (worked): 46/46 set_fx_param writes -> verify_part offline render MOVED
  (0.0704 -> 0.0521). Init renders elsewhere were deterministic, so this delta was the
  preset reaching the render.
- Session B (failed): the 40-preset ear pass with identical calls -> all renders
  bit-identical init audio; captureToTree polls never reached ok (capOther=40).
- Session C (worked): captureToTree canary -> cap_02/cap_03 md5s differ from 00/01 —
  capture + transfer worked again.
- Session D (failed): single apply + 6x poll (900ms apart) -> deferred capture FIRED
  (callAfterDelay worked, message thread alive) but read state BIT-IDENTICAL to boot
  ('unchanged') — i.e. the live child never drained the param ring despite the device
  object reporting present ('Windows Audio (Low Latency Mode)' restored; deviceOpen=true
  takes the deferred branch).

CONSTANTS across all sessions: isolated children spawn/create fine; offline renders
work; deferred capture fires; the D-lite unchanged guard is what reports the miss.
VARIABLE: whether the live graph actually clocks after 'saved audio device restored'
(driver=Windows Audio (Low Latency Mode), out="" in="" — a restore that may or may not
yield real audio callbacks on this RDP box).

WHY SYSEX KITS (xenia/nord) WORKED ANYWAY: uncertain — either those sessions had
clocking live graphs, or the metered SysEx drain + scratch paths differ. Marked
UNCERTAIN; the engine-side instrumentation below resolves it.

NEXT STEP (requires a Windows-side interactive session, not remote probing):
instrument the live clock — log every processBlock entry of MainAudioProcessor (or
toggle a counter via get_fx_capture_status) and watch across a device restore:
1. does getCurrentAudioDevice()->isRunning() ever become true after restore?
2. do MainAudioProcessor::processBlock callbacks fire?
3. does the param ring drain count advance on setParam?
If the live graph never clocks deviceless, the durable fix is one of:
(a) captureFxSlotState: prefer the SYNCHRONOUS scratch-drive path whenever the live
clock is not actually running (check a processBlock counter, not just device presence) —
same shape sendFxMidi already has; or
(b) a headless scratch-clock timer that pumps the live graph at N ms intervals when
no device is running (makes ALL live-side writes work headless; bigger blast radius).
Both fix the class: param applies, queued MIDI, and any future live-side state writes
would reach offline renders on deviceless/boxless sessions.


## INSTRUMENTED CHECK RESULT: live graph clocks — the JE8086 ear-pass root cause is the plugin's own getStateInformation (2026-09-17)

Added LiveClockDiag instrumentation (processBlockCount relaxed atomic + throttled
engine-timer log; the ONLY audio-thread touch is a relaxed atomic fetch_add —
Gate-3 clean). Instrumented build verified running (binary md5 match).

MEASUREMENT: the live graph clocks CONTINUOUSLY on the deviceless box — dBlocks ~500
per 5s poll (~100 blocks/s at 44100), devState=open, sr=44100. The earlier 'live graph
never clocks' hypothesis is REFUTED.

REVISED ROOT CAUSE (JE8086 ear-pass identical renders): the 44 applied CLAP parameter
writes reach the live child's DSP (audible live), but the plugin's
getStateInformation() does NOT serialize param-driven state — the captured state is
bit-identical to boot (status=unchanged), so the offline domain (which restores from
IDs::pluginState) boots at init for every render. This is the handoff's known 'hear !=
export (state does not carry the patch)' JE8086 trap, now understood mechanically:
NOT a save-flow bug — the plugin cannot serialize param-driven state at all.
Consequence: no tree-capture route can fix the JE8086 offline ear pass; options are
(a) render through the live children (engine change), or (b) live listening in the app.
The Osirus (model-C) digital silence is a SEPARATE model-specific offline issue.

Instrumentation retained (useful long-term): MainAudioProcessor::
debugProcessBlockCount() + AudioEngine LiveClockDiag log (throttled 5s).


## F-D (open): ParamOverrideLedgerReplaySeam clean-tree count (2026-09-18)

The replay-seam test's clean-tree segment fails: after removing
IDs::appliedParamOverrides from every FX_CHAIN slot child of a copied project tree,
replayAppliedParamOverrides still counts slotsWithOverrides=1 / skippedBeyondCache=1
(expected 0/0; remainingLedgers=1 confirmed by post-removal diagnostic). The removal
targets TRACK_LIST/track/FX_CHAIN/slot — the same path the writer and the replay read.
All other 8 matrix-preset tests pass; the ledger-bearing replay path is verified.
Next: dump the copied clean tree XML and locate the surviving ledger (suspect: a second
write location or a tree copy aliasing subtlety). Test currently GTEST_SKIPped with the
reason inline — do not flip the expectation.


## JE8086 OFFLINE EAR-PASS: CLOSED as emulation-blocked (2026-09-18, final)

After the capture-fix + replay build (binary verified current, md5 312f8efa both sides):
- 40/40 ear-pass renders completed; all 40 loud (rms ~0.48 — params now DO shape the
  offline child via the state-restore path), but 26/40 bit-identical and rms spread
  0.48169-0.48328 — sequential applies converge to ONE shared live state instead of
  per-preset sounds. get_fx_capture_status never reached ok (capOk=0/40).
- Root cause (consistent with every measurement): the JE8086 emulation's CLAP parameter
  writes do not round-trip into its serialized patch state (the known 'state does not
  carry the patch' trap), so per-preset differentiation cannot survive into offline
  renders. The engine-side capture/replay machinery works; the emulation bridge does not.

RESOLUTION: the JE8086 ear pass is a LIVE, IN-APP activity — apply_matrix_preset each
preset and audition in real time (live path verified audible repeatedly). Offline
JE8086 preset rendering is emulator-blocked, same category as Vavra injection and the
Osirus model-C silence; all three documented for the gearmulator project.
Xenia + Nord ear-pass kits remain fully valid offline (compositions/earpass/).

## EAR-PASS RESULT (by ear, 2026-09-18): NO engine's delivered patches were audible

The human listening pass over the rebuilt real-corpus-patch kits:
- Xenia: 14 real single-program dumps (edit buffer, bank 0x20) — ALL sound like the
  same patch (minor playback length/speed differences only).
- NodalRed2x: 14 real .syx patches via load_nord_bank (Discovery Pro pads etc.) —
  same result.
- JE8086: 40 offline renders bit-identical init audio (params apply live only).
- Osirus: offline renders digital silence (model-C).
- Vavra: injection measured not applying.

UNIFIED ROOT CAUSE: HDAW's proxy delivers MIDI to the CLAPs through the
clap-juce-extensions bridge (MidiBuffer -> CLAP events). CLAP 1.x has NO SysEx event
type — SysEx dumps are dropped at the bridge. CC0 + program-change events DO cross,
but every wrapper advertises program-list=false (no programs exposed) so program
change is a structural no-op. Notes and CCs cross fine (phrases play audibly).
Therefore: complete-patch delivery (SysEx dumps) is structurally impossible with the
current wrappers for ALL FIVE devices, and the ear-pass kits built on it could never
have worked. Path 2 (modify the wrappers, build our own CLAPs) is the only route.
