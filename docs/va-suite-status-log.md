# Hardware VA suite — per-plugin status log

Split verbatim from [`hardware-va-suite.md`](hardware-va-suite.md) on 2026-09-24
(former §9, "Per-plugin matrix presets (harvested)"). A §-reference without a file
name points at the reference doc (`docs/hardware-va-suite.md`) — §2 policy, §7b
harvesting live there; §9 references now land here.

## 9. Per-plugin matrix presets (harvested)

`timbre-lib/harvest_matrix_presets.py` turns the §7b mining into concrete, named,
device-applicable configs — one sheet per engine at
`timbre-lib/matrix_presets/<engine>.json`, schema `hdaw.matrix.preset.v1`.
Shipped 2026-09-16: **je8086 40, nodalred2x 40, xenia 40, vavra 40** (vavra named
for the 86-key FX/matrix subset via the verified offset map below; its other
dump slots stay raw `off_<N>` keys); the **virus shortfall was removed
2026-09-17 (R7)** — `virus_fx_pages.py` decodes the TI/B/C FX+mod pages, so
virus ships **40** too (1.78M-value byte-match gate; see below). Sheets still
carry the corpus flag `"unverified": true`, but the live apply/ear pass
(Phase D) has since RUN and the last two engines were closed 2026-09-21:
je8086/xenia/nodalred2x **verified live**; **vavra and virus now verified live**
too — their sheets declared `appliesVia` values the tool refuses
(`state_blob_or_patch_unverified` / `midi_cc_pc`) and the param path had no
name resolution for their vocabulary, so they were relabelled `set_fx_param`
and the tool now resolves sheet names against the LIVE slot's own params.
A/B (gate `FxMidiInjection.MatrixPresetAudibilityVirusVavra`, noise-floor aware):
virus preset0 **66/66 params applied**, render delta **0.00152** (measured noise
0); vavra preset0 now takes the **device-native dump route** (below) and applies
**363/363 values** with delta **0.00940** — ~10x the 0.00088 the host-param route
managed, and 94x the threshold.

### File format

A sheet is `{schema, engine, patchCount, scannedSidecars, sourceRoots, presets,
unverified}` (plus `presetsShortfall` when an engine yields fewer than 10). Each
preset is `{id, name, role, params, appliesVia, examples, evidence}` — `params`
carry the device's own parameter names (JE8086 `FilterLfo1Depth`, Virus
`mod_matrix`, ...), `examples` name the corpus patches the config came from, and
`evidence` is the count trail ("943 patches carry this config"). No preset is
invented without a corpus citation.

### Lookup-first workflow rule

When FX/modulation work starts for a core plugin, look up
`timbre-lib/matrix_presets/<engine>.json` **before** inventing chains or reaching
for plugin FX — this is step 0 of the §2 policy. Apply the preset through its
`appliesVia` path — or just use the `list_matrix_presets` / `apply_matrix_preset`
MCP tools, the mechanical front door that resolves index maps and emits/injects
SysEx — then fall through §2's order only for what it does not cover.

### Apply paths + Phase D verification checklist

| Engine | `appliesVia` | Apply | Verify (Phase D) |
|---|---|---|---|
| JE8086 | `set_fx_param`, `load_je8086_preset` | parameter writes by **name → index** against `list_fx_params` — never by dump offset (the 461-param list is not the SysEx layout); DT1 patch dumps now apply as well (2026-09-20 wrapper retarget, §9); names need `je8086_param_index_map.json` (the plugin publishes display names like 'A FLT CUTOFF FREQ') | **VERIFIED live + offline with custom JPAR CLAP (2026-09-18)** — 46/46 writes of preset b44052f76c82a7a7, `list_fx_params` readback + ear; custom JE8086 state chunk now carries param presets into offline export/save-load (see provenance below). |
| NodalRed2x | `load_nord_bank` | load the preset as a bank/patch file; morph chains (`nord_morphs/`, 20 `.syx` written by `nord_dump.py`) load the same way | **VERIFIED AUDIBLE live** (morph-chain A/B; map 5,350 files / 353,100 values / 0 mismatches) — render assertion |
| Virus | `midi_cc_pc` | writer `virus_dump.py` is format-verified (TI 524 B / B/C 267 B; checksum rule cited + validated); `load_virus_preset` (CC0+PC) — the old "queues but does NOT change renders" reading is finding **F-A, now RESOLVED** (2026-09-20: the boot-patch fix + the `stateSet` SHM ring make the state round-trip; the original null was measured against an all-zeros edit buffer, and silence masks every delta — lesson 25/26); parameter path possible (3,086 exposed params) | **BOTH AUDIBLE 2026-09-19**: OsTIrus (TI) renders audio offline (rms 0.042, gate OsTIrusRenderAudibility); Osirus (C) **FIXED** — the emulator booted from an all-zeros edit buffer, so gearmulator `virusLib/device.cpp` now loads ROM factory patch A-0 at boot and Osirus renders **rms 0.047** (was exact 0 / 3.09e-06 dust; gate OsirusBootPatchAwakening). F-A was measured against that silent slot (silence masks every delta — lesson 25) and is **RESOLVED**: see the CORRECTION in §9.
| Xenia | `apply_preset` WaldorfSysex (added 2026-09-18) + **preset-sysex replay (2026-09-18)** | `.syx` of F0 3E 0E dumps -> validated + queued via `send_fx_midi`; dumps built by `xenia_dump.py` (265 B, bank 0x20 edit buffer, checksummed); morph chains performable | **FULL GATE GREEN 2026-09-18** (`FxMidiInjection.XeniaEditBufferDumpChangesOfflineRender`, real Xenia.clap md5 71687993… + real cobalt-bank dumps): LIVE application verified (boot→dumpA Δ0.0058 rms; dumpA→dumpB Δ0.0057), dumps persisted on the slot (`presetSysex`), and **fresh children rebuilt from the tree REPLAY the dumps** (Δ0.0067 vs factory) + offline export differs (Δ0.0013) — save/load/rebuild/export all hear the injected patch. The XT single cache is editor-request-driven, so the plugin-state capture route stays a boot stub (`captureStatus=unchanged`, `pluginStateLen=0` by design); the raw dumps are replayed instead (Track.cpp restore; works for any sysex-loadable plugin, incl. Vavra once its emulator applies dumps) |
| Vavra | `apply_preset` WaldorfSysex + **edit-buffer retarget (2026-09-18)** | `.syx` of F0 3E 10 dumps -> validated + **retargeted to the single-mode edit buffer (0x20/0x00, Waldorf checksum fixed)** + queued via `send_fx_midi`; microQ single = 392 B (`mqstate.h Dumps`), params at [7..369], name at [370..385] | **NOW APPLYING — GATE GREEN 2026-09-18** (`FxMidiInjection.VavraEditBufferDumpChangesOfflineRender`, real Vavra.clap + real rhythm-lab dump): the 2026-09-16 "NOT APPLYING" was a **buffer-targeting bug, not an emulator limitation** — real bank dumps carry 0x30 (multi-edit) or 0x40+ (bank) buffer bytes the single-mode OS never plays; retargeting to 0x20 makes the OS load the edit buffer (same as mqController::sendSingle). live injected rms 0.00945→0.01512 (D0.0057), presetSysex persisted, rebuilt-from-tree replay D0.0054 vs boot |

**D-lite round-trip check** (any state-blob route: Vavra, Xenia): apply →
`capture_fx_snapshot` → diff the captured state → render A/B. A capture that merely
echoes the boot state is not persisted (`captureStatus="unchanged"` — read that
field before assuming a capture happened), and a render peak identical to 16 digits
means the state was a no-op.

**JE8086 custom CLAP provenance + validation (2026-09-18):** installed patched `JE8086.clap` from `/mnt/d/pdf/gearmulator-git` commit `6ff5ef3b` (Release target `jeJucePlugin_CLAP`) with wrapper `JPAR` v1 parameter-state chunk. Installed md5 `15001c1fe9139f5fa83f4edcff5d7750`; previous binary backed up under `C:\Program Files\Common Files\CLAP\backup-hdaw-20260918-je8086\` (md5 `f4a19cd63f0963a30238829a69fc80dc`). HDAW MCP HTTP validation: 461 params exposed; preset b44052f76c82a7a7 applied 46 params, capture `status=ok stateBytes=5419`; offline init vs applied vs save/load renders are distinct (`1dc838f6...` rms 0.03215 -> `d2615e25...` rms 0.01587 -> `1de2d97a...` rms 0.01890), and second preset 47e01d5cf2091704 rendered distinct (`d425fefe...`, rms 0.009998). Artifacts: `compositions/je8086-jpar/`. **Superseded for DT1 patch dumps 2026-09-20** — that build still had the empty `case AddressArea::UserPatch`, so patch files did not apply; the now-installed binary is md5 `84427AEA4EE5F95E7E8CA8639C90DD20` (§9).

### Virus shortfall + unblock

Closed 2026-09-17 (R7): `virus_fx_pages.py` decodes the FX / modulation-matrix
pages of every single dump (byte-match stop-gate: 5,425 dumps, 1,782,572 values,
0 mismatches), the sidecars were re-swept to rev 2, and the re-run harvester
shipped the full **40-preset** virus sheet plus `virus_morphs.json` (per-step
SysEx, TI/BC model-tagged, written by `virus_dump.py`). What remains is live
audibility only: the silent Osirus slot that made F-A unmeasurable is FIXED (§9), so the A/B can now be re-run — see the
apply-path table above.

### Vavra naming — the verified offset map

microQ sidecars store single-program **dump offsets** (bytes 7..369), not device
parameter names. `timbre-lib/matrix_presets/vavra-offset-map.md` (+ the
machine-readable `vavra_offset_map.json`) verifies the rule **dump byte = 7 +
linear parameterDescriptions index** (one 7-bit byte per parameter, no packing;
single programs only) against the `mqLib` sources and 305 real sidecar/syx pairs.
Applying the map to the vavra sheet's offset keys yields device names
(`F2ModSource`, `FX1Type`, ...) for the 86-key FX/matrix subset; the shipped
sheet carries those names and keeps raw `off_<N>` keys for every other dump
slot — this closes the vavra gap named in §7b.

### Osirus (Virus C) offline silence — FIXED 2026-09-19 (gearmulator boot-patch fix)

**Symptom.** The Osirus (Virus C) slot rendered exact digital silence offline
(`rms == 0`, or `3.09e-06` float dust) regardless of MIDI note, patch or
parameter writes, while OsTIrus (TI) rendered `rms 0.042` through the same
wrapper.

**Root cause (dsp56300 / emulator boot state).** `virusLib/microcontroller.cpp`
`createDefaultState()` writes the boot patch by dumping `m_singleEditBuffer`
verbatim to the OS edit buffer, and that member is *value-initialized*
(`TPreset m_singleEditBuffer{}` — `microcontroller.h:118`), i.e. **512 zero
bytes**. The OS therefore boots on an all-zeros patch: every oscillator level,
envelope level, filter cutoff and channel volume is 0, so the DSP runs and
produces silence. This also explains the earlier "parameter awakening" failure
(setting `Ch 1 Channel Volume` to max changed the param cache but not the
render): unmuting one channel cannot make sound from oscillators at level 0. It
further explains why *every* comparison returned delta 0 — silence equals
silence, which is what made finding F-A look like a preset-load fault.

**Cross-lib audit (why only the Virus was hit).**
`nord/n2x/n2xLib/n2xstate.cpp` builds real defaults
(`State::createDefaultSingle()` copies `g_singleDefault` into the dump;
`createDefaultMulti()` likewise), which is why NodalRed2x boots audible. `xtLib`
(Xenia) and `mqLib` (Vavra) have no value-initialized edit buffer at all.
virusLib was the **only** lib booting from zeroed state.

**Fix** (`gearmulator-git/source/virusLib/device.cpp`, Device ctor, after
`createDefaultState()`): load a real factory patch from the ROM into the edit
buffer — `m_rom.getSingle(0, 0, romPatch)`, then
`m_mc->writeSingle(BankNumber::EditBuffer, SINGLE, romPatch)`. Guarded by
`if (!m_rom.isTIFamily())` so the TI boot path stays byte-identical (TI already
boots audible via its own init sequence — minimal blast radius).

**Evidence (A/B, gate `FxMidiInjection.OsirusBootPatchAwakening`).**

| build | offline render RMS |
| --- | --- |
| pre-fix (gates 1-2) | `3.09492e-06` (float dust) |
| pre-fix (gates 3-6) | `0` (exact silence) |
| post-fix (gate 7, final) | **`0.0471329`** (audible) |

Installed CLAP md5 `60ad7cf8c3bf50d867d60dd37194f437`
(`C:\Program Files\Common Files\CLAP\Osirus.clap`). Gate result:
`[ OK ] FxMidiInjection.OsirusBootPatchAwakening` and
`[ OK ] FxMidiInjection.BootStateBaselineGuard`.

**Follow-on.** Because the former comparisons were 0-vs-0, finding F-A
(`load_virus_preset` does not change renders) is **no longer a valid
conclusion** and must be re-measured on the now-audible build.

### F-A re-test (2026-09-19, audible build) — REPRODUCED, mechanism identified

With the slot audible the A/B became meaningful, and finding F-A is confirmed
with a precise mechanism. Gate
`FxMidiInjection.OsirusPresetChangeReflectsInRender` (diagnostic; prints a capture
receipt timeline and a phase-2 param probe):

| probe | child param cache | serialized state | offline render |
| --- | --- | --- | --- |
| CC0 bank A + PC 40 (`load_virus_preset`) | no change | `status=unchanged`, 0 B | `rms 0.0468474` -> identical |
| `set_fx_param` `Ch 1 Cutoff` 1.0 -> 0.0 | `1 -> 0` (reached the cache) | `status=unchanged`, 0 B | `rms 0.0468474` -> identical |

**Reading (corrected 2026-09-21):** that last cell is **expected by construction**, not a delivery failure. The probe's render is an offline export of a TREE COPY into a fresh child, and a raw `PluginParamService::setParam` write only reaches the LIVE child — it was never persisted, so the render could not see it. The public `set_fx_param` route persists into `appliedParamOverrides` and DOES change renders (`VavraHostParamPersistedWriteAffectsExport`), and the opt-in `liveParamState` probe surfaces unpersisted live-only writes (`LiveParamStateProbeReflectsUnpersistedWrite`). See §9 (Vavra `set_fx_param`).

`status=unchanged` is the boot-echo guard firing: the child's serialized state is
byte-identical to the boot baseline, so nothing is persisted into
`IDs::pluginState` and the offline domain re-renders the boot patch. Note the
phase-2 row: even a param write that visibly reaches the cache leaves the
serialized state untouched.

**Mechanism (source level).** Every gearmulator wrapper inherits
`jucePluginLib/Processor::getStateInformation`, which serializes
`g_saveMagic` + `g_saveVersion` + `saveCustomData()`. The JE8086 wrapper overrides
that with a **`JPAR` v1 chunk carrying every exposed parameter's unnormalized
value** (`jePluginProcessor.cpp:118` save / `:142` load, replayed with
`Parameter::Origin::PresetChange`) — which is exactly why JE8086 param applies
round-trip to offline renders and saves. The Virus wrapper has **no equivalent
chunk**, so neither host-param writes nor OS-side ROM program changes appear in
`getStateInformation`: the state never differs from boot, so **no injection can
round-trip to an offline render or a save**. Same defect class the JE8086 `JPAR`
fix closed.

**Also confirmed:** there is no SysEx-file route into the gearmulator Virus
plugins — `PresetRouteKind` offers only `VirusRom` (CC0+PC) for them, while
`SubSynthVirus` targets HDAW's *internal* `sub_synth`. So the `IDs::presetSysex`
persist/replay path that rescues Xenia/Vavra (Xenia/Vavra edit-buffer dumps) is
not reachable for Virus today. The on-disk Virus dump library is TI-only
(524 B, `timbre-lib/demo_libs/virus/`), i.e. OsTIrus material — there is no
model-C (267 B) dump set.

### F-A RESOLVED (2026-09-19) — Virus preset/param state round-trips offline

**Superseded by the CORRECTION at the end of this section:** the deltas quoted
here (0.000176) were **render jitter** (the boot render varies ~6e-4 run-to-run,
so the gate's 1e-4 threshold was below the noise). The wrapper chunks and the
HDAW capture fixes below are all still required, but they were not the blocker.

1. **Wrapper state chunks** (`virusJucePlugin/VirusProcessor`): serialized state
   is now `ROM` + `OBST` (OS arrangement, ~4.8 KB via `getState(CurrentProgram)`)
   + `PRGS` (active ROM program selection per part, from a new
   `Processor::addMidiEvent` override that taps the raw CC0+PC the host sends —
   the controller's catalog name-lookup can miss in an isolated host) + `JPAR`
   (all exposed parameter values, 141 KB total) + the base MIDI/routing chunks.
   Restore replays OBST into the OS, re-selects PRGS via
   `setCurrentPartPreset`, and syncs JPAR into the host model — bypassing the
   param pipeline's initial-value guard (`sendToSynth` absorbs the first value
   per param while `m_lastValue == -1`) and its timer-paced sends.
2. **HDAW capture/save self-baselining fix**: `noteStateSample` used to adopt the
   very sample being judged, so the first capture/save always reported
   "unchanged" and discarded the state. `hasBootBaseline()` + guards in the
   deferred + deviceless capture paths and in `ProjectSerializer` persist real
   state; `TrackFXSlot::prepare` seeds the true boot baseline for slots built
   outside `rebuildFXChain`; the deferred capture retries over a warm-clock
   budget (~5 s) and tolerates a mid-boot/restart empty snapshot.
3. **Stopped-transport MIDI flush** (`AudioEngineCommands::sendFxMidi`): the
   queued CC0+PC sat in the slot's pending queue forever while the transport was
   stopped (the audio graph's buzz-guard early-outs before the proxy slot, so
   `PluginProxySlot::processBlock` — the only writer of the SHM `midiIn` ring —
   never ran). `sendFxMidi` now drives 24 scratch blocks (command thread, sole
   producer while stopped) to flush the events into the ring; the child's audio
   loop delivers them to the wrapper (the tap fires) and the OS.

Installed CLAPs: Osirus `73cbacc6…` (md5; `backup-hdaw-20260919-virustap` has
all prior iterations). Regression: `OsirusBootPatchAwakening` still green
(rms 0.0471). Note: the host-param *value* still only reaches the OS if the
live child's param pipeline applied it first (C2b warm clock + timer sends);
PRGS/OBST carry the ROM-program + arrangement truth, JPAR keeps the model in
sync.

### CORRECTION (2026-09-20) — the real blocker: plugin state never reached the isolated child

The `0.000176` deltas above were jitter. Instrumenting the child (its logs go to
OutputDebugString, invisible to stderr) proved the real chain:

| Stage | Evidence |
| --- | --- |
| Tap + chunks | `flushstate PRGS=1 OBST=1 JPAR=1`; capture persists (`receipt ok bytes=34245/146495`) |
| Rebuild restore call | `[Track] rebuild slot … stateLen=45666 / 195334 isolated=1` |
| **State reaching the plugin** | wrapper traces `[FA] setState recv=` / `[FA] loadChunkData entered` — **zero occurrences** |
| Parent pipe send | `SET_STATE slot=1 bytes=N` logged, then **neither** `verified` nor `verify mismatch` → `sendStateInternal()` returned false (a 3 s `sendMsgBounded` timed out) |

**Root cause:** the state travelled as ~140 (34 KB) to ~600 (146 KB) chunked
**pipe** messages, and the child's control thread — the pipe reader — is blocked
by the **12 s real-time-paced Virus OS warmup** (`PluginHost.cpp` PREPARE
handler: `virus warmup: 1200 blocks (12s audio)`), then starved further by the
CPU-bound offline render. The pipe filled, the bounded send timed out, and the
background retry worker died with the render domain — so the render played the
boot patch.

**Fix — the `stateSet` SHM ring** (mirror of the C2b `paramSet` ring):
`STATE_RING_SIZE` (1 MiB) byte ring after the param rings, `SHM_MAGIC` bumped to
`0x4844415D`. The parent publishes a length-prefixed record
(`[uint32 size][bytes]`, `PluginProxySlot::publishStateToRing`) — lock-free, no
pipe; the child polls it from its **audio loop**
(`PluginHost::applyPendingRingState`, deferred while `warmupActive`) and applies
it marshaled to its message thread (lesson 16). The pipe path remains the
fallback. Wrapper correctness fix alongside it: the deferred re-assert uses
`Controller::reassertPresetSelection` (bank/program select **only**), because
`setCurrentPartPreset()` ends with `requestSingle(EditBuffer)`, which makes the
host push its stale cached edit buffer over the just-selected ROM program.

**Verified (deterministic, 3/3 runs):** `SET_STATE published to shm ring` →
`state ring applied` in ~30 ms; wrapper `[FA] setState recv` →
`loadChunkData entered` → `PRGS stash` / `OBST stash`; gate deltas
`0.0192275 / 0.019202 / 0.0192869` (≈40 % level change: boot 0.0468 → restored
0.0276) with `VERDICT: preset load CHANGED` + `PHASE2 VERDICT: … ROUND-TRIPS`.
`FxMidiInjection` suite **16/19** (the ring also fixed
`OsTIrusInjectionCapturesToTreeAndSurvivesRebuild`); the remaining 3 are the
pre-existing OsTIrus pair, `OsTIrusRenderAudibility` (reproduces with the
pristine pre-session `OsTIrus.clap`) and `NordBankLoadChangesNodalRed2xRender`
(receipt now honestly reports `unchanged` for n2x states that do not change).

### Virus warmup vs the render budget — FIXED 2026-09-20

`OsTIrusRenderAudibility` failed with `render timed out`: the first audition
rendered audio (rms 0.0423) but the third (`fresh` probe) was cancelled while
its newly spawned TI child was still in the mandatory **12 s real-time OS
warmup**. The render-window watchdog waited only
`computeBakeWaitMs(tree) + windowSeconds*1000 + 5000` (= bake + 7 s), which is
shorter than one warmup even before CPU contention from the other TI children.

**Fix:** `ExportManager::computeBakeWaitMs` now adds a warmup allowance —
`HDAW_CHILD_WARMUP_SECONDS` (default 12) × the number of virus-family slots in
the tree (matched on the slot `pluginID`: osirus/ostirus/virus), added **after**
the 120 s cap so it is never clipped, and skipped entirely when
`HDAW_NO_CHILD_WARMUP=1`. It is a ceiling, not a delay: both waiters
(`renderTrackWindow` and the export's internal `kMaxBakeWaitMs`) still exit as
soon as the export completes.

**Verified:** `FxMidiInjection.OsTIrusRenderAudibility` now **PASSES**
(219 s), with `OsTIrusInjectionCapturesToTreeAndSurvivesRebuild`,
`OsirusBootPatchAwakening`, `OsirusPresetChangeReflectsInRender` and
`BootStateBaselineGuard` all green in the same run. **Test-instrument fix (2026-09-20):**
`OsTIrusPresetChangeReflectsInChildParams` used to assert a change in the child's
**param cache** after CC0+PC. The TI's OS does not echo PC-loaded patch
parameters back to the host, so that probe is invalid for this plugin — the
diagnostic prints `paramCacheChanged=0 stateChanged=1 (state 263387 -> 263406)`
(the +19 B is the `PRGS` chunk). The test now asserts the child's **serialized
state** (the authoritative observable) and keeps the cache as a diagnostic; it
PASSES.

**Nord gate retarget (2026-09-20):** `NordBankLoadChangesNodalRed2xRender`
asserted `pluginState` non-empty + `captureStatus == "ok"`. For the n2x that is
the wrong surface: its serialized child state does not reflect the volatile
patch RAM the bank dumps land in, so the D-lite guard honestly reports
`unchanged` and persists no state blob. The test now asserts what the
`load_nord_bank` pipeline actually relies on — the injected dumps persisted for
replay — mirroring the Xenia/Vavra gates: `[NordBank] captureStatus=unchanged
pluginStateLen=0 presetSysexLen=33114` with `EXPECT_GT(presetSysexLen, 0)`, a
settled (non-`pending`, non-`failed`) receipt, and the offline-render delta
(which passes). **The whole `FxMidiInjection` suite is green** — 20 gates since
`FxMidiInjection.MatrixPresetAudibilityVirusVavra` was added (2026-09-21).
Note: run alone, the audibility test can still stall — several heavy TI children
plus the real-time-paced warmup saturate the CPU.

### Vavra + Xenia matrix presets — the device-native dump route (2026-09-21)

**Why host-param publishing does not work for the microQ FX block.** The vavra
sheet carries the device's own patch vocabulary (363 values: 86 named + 277 raw
`off_<N>` dump offsets). 91 of its FX-related params were *not* public, so
flipping them to `isPublic` and rebuilding looked like the fix — measured, it
adds **no** host parameter (live count unchanged at 7557 across a verified
re-embed). The reason is in the wrapper: `jucePluginLib/controller.cpp`
**collapses params that share an `(index, part)` into one host parameter with
*derived* children**, and the microQ's FX sub-parameters deliberately share
indexes 146-155 (`Fx2ChorusSpeed` / `Fx2FlangerSpeed` / `Fx2PhaserSpeed` are all
index 146 — their meaning is set by `Fx2Type`). Publishing them can never create
a new host param; the type-level params (`FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix`)
are already public and remain the automatable surface. The change was reverted.

**The route that works: sheet → 392-byte single dump → Waldorf SysEx.**
`timbre-lib/vavra_matrix_sysex.py` stamps every vavra preset with a complete,
ready-to-inject dump:

* **parent** = the preset's first corpus example, resolved by the patch name
  **embedded in the dump** (bytes 370-385) — library *filenames* carry a category
  suffix (`Acid bender   CJ Arp.syx`) while the corpus names the patch
  (`Acid bender   CJ`); 40/40 examples match that way, 511 dumps indexed;
* **overrides** = the preset's params mapped to byte offsets via
  `vavra_offset_map.json` for named keys and taken raw for `off_<N>`, then applied
  by `vavra_dump.build_dump` (392 bytes, `F0 3E 10 00 10 …`);
* the dump is stored on the preset as `sysex` (392 ints) with `appliesVia:
  waldorf_dump`, `baseSyx` and `sysexOverrides` recorded.

Result: **40/40 presets stamped, 363/363 parameter bytes verified byte-exact
(0 mismatches)**. `apply_matrix_preset` gained a preset-level dump branch that
injects it through the same validated SysEx path as the morph steps (checked:
F0/F7 framing), so `apply_matrix_preset {engine: vavra, id: …}` now applies the
whole patch natively — the emulated OS does the work, exactly like a real patch
transfer. The branch is inert for the other sheets (only `vavra.json` carries
preset-level `sysex`; je8086/virus stay on `set_fx_param`, nodalred2x/xenia
unchanged).

**Xenia (Microwave XT) — same route, 265-byte dumps.**
`timbre-lib/xenia_matrix_sysex.py` reuses `xenia_dump.py`'s primitives:
`resolve_parent_dump()` **byte-verifies** the parent against the preset's own
named values (it also handles the harvest's +2 labeling quirk on `.mid`
sidecars), the preset's values are written at their mapped offsets and the
checksum is recomputed. 40/40 presets stamped (0 skipped, 0 vocab-shifted),
3360 values, 84.0 avg; preset 0 verified 84/84 byte-exact.

**FRAMING PITFALL (both engines).** A parent taken from a *bank image* carries
that bank's slot numbers, so injecting it verbatim writes the patch into a RAM
slot and the **current sound does not change** — measured on Xenia: render delta
**0.00017**. The stamped dump must therefore be re-framed to the **edit buffer
(bank `0x20`, program 0)** with the checksum recomputed — the framing the
verified `compositions/xenia-ab` dumps use. Effect: delta **0.00017 -> 0.01459
(86x)**.

**Gate:** `FxMidiInjection.MatrixPresetAudibilityVirusVavra` (all three engines):
virus `applied=66/66`, delta **0.00152** (noise 0); vavra
`route=device_dump bytes=392 base=Technodoodah  CJ Arp.syx`, delta **0.0088**;
xenia `route=device_dump bytes=265 base=upawbnk.mid / Tablescan11 HH`, delta
**0.0146**.

**Assertion caveat (documented in the gate output).** The XT and microQ
emulations carry free-running state, so two renders of the *same* boot patch can
differ by ~1e-2 RMS (measured: vavra 0.0129, xenia 0.0154 in one run). A
noise-floor-based A/B cannot resolve a patch change there, so the gate falls back
to an **effect-only** assertion (`delta > 1e-4`) and prints
`(JITTERY engine: effect-only assertion)`. The virus path (deterministic, noise
0) keeps the strict 3x-floor check.

### JE8086 UserPatch DT1 dumps — FIXED 2026-09-20 (wrapper retarget + route recall removal)

**Symptom.** `load_je8086_preset` / `send_fx_midi` with a real JP-8080 `.syx` queued and
validated, but the render stayed at the boot patch — the old note blamed
"`jeLib/device.cpp` routes live MIDI to the DSP thread, the DT1 patch State is not on
that path". **Wrong:** the dump *is* parsed, but a real patch file carries the
**UserPatch bank** address (`0x02000000`), and `jeController::parseSysexMessage` had an
empty `case AddressArea::UserPatch` — the write went nowhere and the sounding
temp-performance patch was untouched.

**Evidence (additive jeLib console probe `jeUserPatchProbe`, deterministic, repeated).**
`jp-8080 trance bank.syx` patch 1 = 2 DT1 messages (`0x02000000` len 254,
`0x02000172` len 18). A1→A3 baselines give the drift floor (rms 0.00340–0.00432, peak
0.01180–0.01303):

| Window | rms | verdict |
|---|---|---|
| B verbatim dump | 0.00405 | **NOCHANGE** (Δ 0.000267, below drift; state mirror byte-identical, 641 B) |
| C dump retargeted to `PerformanceTemp\|PatchUpper` | 0.00578 | **CHANGE** (Δ 0.00146 rms / 0.01350 peak ≈ 11× drift; mirror now 661 B) |
| E `CC0=1 USER + PC` recall only | 0.01329 | **CHANGE from boot** — the recall alone loads a different (louder) bank program |
| D recall *after* the retarget | 0.01326 | Δ vs C 0.00765, Δ vs E **0.0000245** → lands back on E: the recall **discards the applied dump** |
| F retarget re-applied | 0.00552 | rms restored to C (last write wins) |

**Fixes (both shipped).**
1. **Wrapper** (`D:\pdf\gearmulator-git`, `jeController.cpp`): the empty
   `case AddressArea::UserPatch` now retargets host-sourced DT1s through the existing
   `sendSingle(_sysex, part)` path (split → `PerformanceTemp|PatchUpper` with the
   intra-block offset preserved → checksum → `sendTempPerformanceRequest`) — exactly what
   the plugin's own patch browser has always done. Device-origin output is excluded
   (`_source != Device`) and only `CommandIdDataSet1` (0x12) is retargeted; RQ1 passes
   through untouched. Built via `temp/cmake_vs2026` (target `jeJucePlugin_CLAP`,
   Release). Installed `JE8086.clap` md5 `84427AEA4EE5F95E7E8CA8639C90DD20`; pre-fix
   backup `backup-hdaw-20260920-je8086-userpatch\` (md5 `15001C1FE9139F5FA83F4EDCFF5D7750`).
2. **HDAW route** (`src/mcp/PresetRoute.h` + `McpTools_FxSlot.cpp`): the
   `CC0=1 USER + PC` recall is **removed** (and the `recall` tool arg deleted). Per the
   probe it overwrote the dump, and it would also have made the persisted capture store
   the recalled bank program instead of the imported patch.

**Verified (HDAW integration, 2026-09-20/21).**
`FxMidiInjection.Je8086UserPatchDumpChangesOfflineRender`: fresh-boot rms 0.00257 vs
patch 1 0.00384 (peak 0.0757) vs patch 33 0.00131 — i.e. the dump *content* drives the
sound; the route reports `presetSysexLen=383 pluginStateLen=7099`; and a child rebuilt
from the tree re-renders patch 33 to `|Δrms| = 5.8e-08` (asserted). Full
`FxMidiInjection.*` suite: 20 OK / 1 pre-existing Xenia SKIP / 0 FAILED.

**Readback caveat (2026-09-21).** Confirming a JE8086 patch load is **not** possible
through the host param list (`get_plugin_params` shows no change) nor through the live
child state blob (`GET_STATE` stayed constant across the load) — neither is JE8086
specific, the param cache never echoes SysEx patch loads for these emulations (same
note in `OsTIrusPresetChangeReflectsInChildParams`). The durable readback is the
persisted slot state: `sendFxMidi` stores the raw DT1 dumps in `IDs::presetSysex` and
`Track.cpp` **replays** them into every fresh child at rebuild/restore, so the patch
survives export/save-load. Confirm with `poll_fx_capture` then a render A/B. Probe plan
+ full evidence: `docs/plans/2026-09-20-je8086-userpatch-dt1-probe.md`.

