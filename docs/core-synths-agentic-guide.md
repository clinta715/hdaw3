# Core synths, parameters and effects — the agentic workflow guide

**Scope.** This is the *operational* guide for the "core synths" — the
hardware-VA emulations plus HDAW's internal instruments — written for an agent
driving HDAW over MCP. It answers three questions per capability: **which route
applies it**, **what is actually automatable**, and **how to prove it worked**.

Companion docs: `docs/hardware-va-suite.md` (the device/capability reference and
per-engine evidence), `docs/psytrance-composition-guide.md` (style canon and
recipes), `docs/skills/psy-song-session/` (role playbooks).

---

## 1. The mental model: three layers, always in this order

1. **Device-native** — the emulated hardware's own patch, matrix and FX. Preferred:
   it costs no host CPU, survives save/export, and matches what the hardware does.
2. **HDAW automation / modulation** — automation lanes, track LFOs
   (`add_lfo`), movement plans. These drive **host-exposed parameters** only (§3).
3. **HDAW internal FX** — the internal FX slots (eq / compressor / reverb / delay /
   sampler / …). Use for devices with no onboard FX, or for anything the device
   cannot do (a Nord has no reverb; a DX7 has no FX at all).

Standing policy (hardware-va-suite §2): **device matrix → onboard FX → HDAW
automation/track LFO → HDAW internal FX → third-party plugin last.** Third-party
plugin FX add CPU, latency, isolation and state-round-trip risk.

---

## 2. The core synths at a glance

| Device | Engine (CLAP) | Host params | Onboard FX | Patch library | Best route to change its sound |
| --- | --- | --- | --- | --- | --- |
| **Osirus** | Virus A/B/C (dsp56300) | **3086** | Chorus, RingMod, **Vocoder** | `virus_patch.py` sidecars; `virus.json` matrix sheet (40) | `set_fx_param` (name→index) · CC0+PC ROM presets (`load_virus_preset`) · `apply_matrix_preset` |
| **OsTIrus** | Virus TI (dsp56300) | **6939** | same + TI arrangement | same | `set_fx_param` · CC0+PC |
| **Vavra** | Waldorf microQ | **7557** (96 curated sound/FX × 16 parts) | FX1/FX2 chorus, flanger, phaser, delay, overdrive, vocoder, ring mod | `microq_patch.py` (528 sidecars); `vavra.json` matrix sheet (40, **dump-bearing**) | **device dump (392 B)** via `apply_matrix_preset` · type-level params (`FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix`) for automation |
| **Xenia** | Waldorf Microwave XT | **2151** (66 curated) | `EffectType` + A/B/C, delay, chorus, ring mod, pan, LFO delays | `microwave_patch.py` (1791 patches); `xenia.json` matrix sheet (40, **dump-bearing**) | **device dump (265 B, bank 0x20)** via `apply_matrix_preset` · `set_fx_param` for the decoded subset |
| **NodalRed2x** | Clavia Nord Lead 2x | **362** (33 curated) | **none** (only RingMod + Distortion) | `nl2x_patch.py` (6841 sidecars); `nord_morphs` | `load_nord_bank` (banks + morph chains) · `set_fx_param`; use HDAW internal FX for effects |
| **JE8086** | Roland JP-8080 | **461** | Chorus, Multi-Effects, Delay, RingMod, Pan/AutoPan | `je8086_patch.py` (3689 `.syx`); `je8086.json` (40) | `set_fx_param` **by name** (`je8086_param_index_map.json`) · `apply_matrix_preset` · `load_je8086_preset` (DT1 dumps — **applied since 2026-09-20** via the wrapper's UserPatch→temp-performance retarget) |
| **fm_synth / PsyFm** | internal FM | its own params + **modulation targets 300–308** | internal | `fm_synth_load_preset` / `psy_fm_load_preset` | `set_fx_param`/automation. **Dexed is NOT core** — `fm_synth`/PsyFm is the FM engine |
| **sub_synth** | internal | its own params + internal LFO | internal | `apply_sub_synth_mod_preset` (6 factory mod presets) | params / automation |
| **sampler · drum machine** | internal | own params | internal FX chain | file library | `set_fx_param`, sample load |

Everything else (a third-party synth) is out of scope for "core" and carries the
extra costs listed in §1.

---

## 3. Parameters and automation — what is really automatable

**The one rule:** automation lanes, `apply_movement_plan`, `add_lfo` and
`set_fx_param` can only move **host-exposed parameters**. Always confirm first:

```
list_fx_params { trackId, slotIndex }   → a non-empty parameter list is the licence to automate
```

Exposure is per-device and was expanded 2026-09-19/20 (all made `isPublic` in the
wrapper's `parameterDescriptions_*.json` and rebuilt):

| Device | Params | Proof gate |
| --- | --- | --- |
| JE8086 | 461 | `list_fx_params` (measured) |
| Vavra | 7557 | `VavraHostParamPersistedWriteAffectsExport` (durable ledger replay: AmpVolume 0 -> 0.00357, 1 -> 0.00842 rms, monotonic) + `LiveParamStateProbeReflectsUnpersistedWrite` (opt-in live probe: 0.0034411 -> 0.0289078 rms) + `VavraHostParamsLiveReachability` (host-side staging + flush evidence; asserts nothing about renders) |
| Xenia | 2151 | `XeniaHostParamsChangeRender` (exposure + host-side staging + durable round trip; the render effect is **not resolvable** — the same-input spread exceeded the separation) |
| Osirus / OsTIrus | 3086 / 6939 | `OsirusPresetChangeReflectsInRender` (phase 2) |
| NodalRed2x | 362 | `NodalRed2xHostParamsChangeRender` (Cutoff effect asserted at 3.1-8.2x separation via the durable channel) |

### Three structural limits (learned 2026-09-21 — do not fight them)

1. **Values that are not parameters are not automatable.** A whole patch or a ROM
   program selection lives in the device, not in the host parameter model — use the
   preset route (`apply_preset`, `load_virus_preset`, `load_nord_bank`) or the
   dump route.
2. **Derived-parameter collapse.** Parameters that share an `(index, part)` fold
   into **one** host parameter with *derived* children
   (`jucePluginLib/controller.cpp: isDerivedParameter`). The microQ's FX
   sub-parameters deliberately share indexes 146–155 (`Fx2ChorusSpeed` /
   `Fx2FlangerSpeed` / `Fx2PhaserSpeed` are all index 146; the meaning comes from
   `Fx2Type`), so they **cannot** become separate host parameters — publishing them
   was measured to change nothing (live param count stayed 7557). Automate the
   *type-level* parameters instead, or apply a whole dump.
3. **A live-only param write is not part of a tree-derived render — by
   construction, not by a delivery bug (corrected 2026-09-21).** Every render the
   audit surface uses (`audition_plugin`, `verify_part`, `export_audio`) is an
   offline export of a **TREE COPY** into a **fresh child**, so a write that only
   ever reached the live child was never part of that child's input. The old
   framing — "Vavra live host-param writes are inaudible / a live-path gap" — was a
   harness artifact, and the "same-child ~2x mode flip" behind it did not reproduce
   (two consecutive no-write renders agreed to 4e-07). Durability is a separate,
   explicit channel:
   * `set_fx_param` (and RPC `pluginParam.setParam`) persist into
     `IDs::appliedParamOverrides`, which is replayed into every fresh export child
     — so the write **does** reach renders (`VavraHostParamPersistedWriteAffectsExport`);
   * unpersisted live-only state is visible through the **opt-in**
     `liveParamState` render probe (`audition_plugin`, default OFF);
   * `clear_fx_param_overrides` drops the ledger.
   Budget `set_fx_param` automation for movement again; use `waldorf_dump` when the
   change belongs in the patch itself (it then survives reload as patch state rather
   than as a ledger entry).

### The automatable FX surface, per device

* **JE8086** — `ChorusType`, `ChorusLevel`, `MultiEffectsLevel`, `DelayType`,
  `DelayTime`, `DelayFeedback`, `DelayLevel`, `RingModulatorSwitch`, `AmpPan`,
  `AutoPanManualPanSwitch` + the `Control*` depths.
* **Virus (Osirus/OsTIrus)** — `Chorus/Type`, `Ringmodulator Volume`, and the full
  Vocoder set (`Vocoder/Carrier`, …).
* **Vavra / Xenia** — the slot *type* and *mix* level
  (`FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix`; `EffectType`, `EffectParamA/B/C`,
  `DelayTime`, `ChorusEnabled`, `MixRingMod`).
* **NodalRed2x** — `RingMod` and `Distortion` only (everything else belongs in
  HDAW internal FX).

### Movement tooling

`generate_automation_envelope` · `add_automation_lane` / `add_automation_point` ·
`apply_movement_plan` (macro events with start/end values) · `automation_preset` ·
`audit_modulation_coverage` (proves what is actually targeted).

---

## 4. Effects — the three routes, in priority order

1. **Patch-level (preferred).** Put the FX in the patch so it travels with it:
   device dumps (Vavra/Xenia), CC0+PC ROM presets (Virus), bank/morph loads
   (NodalRed2x), by-name parameter writes (JE8086).
2. **The harvested matrix presets** — 40 per engine, corpus-cited, each with its own
   apply route:
   ```
   list_matrix_presets { engine }                  → presets[] + morphs[]
   apply_matrix_preset { engine, id, trackId, slotIndex }
   ```
   `appliesVia` decides the mechanics: `set_fx_param` (je8086, virus),
   `load_nord_bank` (nodalred2x), `waldorf_dump` (vavra, xenia — the sheet carries a
   complete 392 B / 265 B dump). Morph chains are applied as `<pair>:step<k>`.
3. **Live automation of the exposed FX parameters** (§3) for movement over time.

Anything left over → **HDAW internal FX** (the correct first choice for a Nord).

---

## 5. Preset management and how state survives to an offline render

**Patch libraries** are the per-device sidecar corpora (`timbre-lib/*_patch.py`
→ `<patch>.<engine>.json`, 1791–6841 sidecars per device). Register the folder as
a *patch* library and the file browser surfaces them.

**Loaders and their evidence status:**

| Device | Route | Status |
| --- | --- | --- |
| Virus | `load_virus_preset` (CC0 bank + PC) | **works** — state round-trips to offline renders (F-A fixed 2026-09-20) |
| Vavra / Xenia | `apply_preset` (Waldorf SysEx) | **works** — Vavra dumps retargeted to the 0x20 edit buffer; Xenia framed to bank 0x20 |
| NodalRed2x | `load_nord_bank` (.syx / .mid) | **works** (render verified); per-patch *character* still wants an ear pass |
| JE8086 | `load_je8086_preset` (DT1 SysEx) | **applies since 2026-09-20** — the wrapper retargets UserPatch dumps onto the sounding temp performance, and no `CC0=1 USER+PC` recall is sent any more (a JP-8080 PC *loads* the bank program and overwrote the dump); `set_fx_param` by name also works |
| DX7/Dexed | — | not core — use `fm_synth`/`PsyFm` presets |

**The three persistence mechanisms** that make an offline render / save hear what
you auditioned:

* `IDs::pluginState` — the serialized child state, restored at every rebuild. Works
  when the wrapper round-trips it (Virus: `OBST`+`PRGS`+`JPAR` chunks; JE8086:
  `JPAR`).
* `IDs::presetSysex` — raw injected dumps replayed into fresh children. The durable
  route for dump-based devices (Xenia, Vavra, Nord).
* `IDs::appliedParamOverrides` — the ledger replayed for parameter applies.

**Confirm a capture landed** — never assume:
`capture_fx_snapshot` / `get_fx_capture_status` → `status=ok` (with bytes) means
the state reached the tree; `unchanged` means it equalled the boot baseline and was
deliberately not persisted; `failed:<reason>` names the problem.

**Delivery mechanics worth knowing (2026-09-20/21):** plugin state travels over a
**shared-memory `stateSet` ring**, not the control pipe — the pipe send timed out
while the child's control thread sat in the 12 s Virus OS warmup, which is how a
restored patch silently became the boot patch. Queued MIDI is flushed while the
transport is stopped, and the render/export budget now includes the warmup.

---

## 6. The agentic workflow — concrete sequences

**Give a part a character**
```
add_instrument_part { pluginId, role }          → track + slot, params exposed
list_fx_params { trackId, slotIndex }           → confirm the automatable surface
set_fx_param / apply_matrix_preset / load_virus_preset | apply_preset
get_fx_capture_status                           → confirm the capture
audition_plugin / export_audio                  → hear it offline
mix_report · analyze_tuning                     → check level and pitch
```

**Automate movement over a section**
```
list_fx_params (prove exposure) → apply_movement_plan   (or add_automation_lane +
generate_automation_envelope) → audition_plugin → mix_diff
```

**Apply a harvested effects preset**
```
list_matrix_presets { engine } → apply_matrix_preset { engine, id, trackId, slotIndex }
→ get_fx_capture_status → audition_plugin (A/B against the pre-apply render)
```

**Recall a whole patch**
`apply_preset` (auto-detects the format from the slot type + file header) or the
specific loader (`load_virus_preset`, `load_nord_bank`, `load_je8086_preset`).

**Compose at song scale**
`set_song_plan` → `fill_cells` → `reroll` → `apply_song_brief` → `audit_song_structure`;
`mix_report { fromPlan: true }` to check the whole plan.

**Worked example — Virus.** `add_instrument_part { pluginId: Osirus }` →
`load_virus_preset { bank: 0, program: 40 }` (ROM preset) → `apply_matrix_preset
{ engine: "virus", id: <mod-matrix preset> }` (66/66 params apply) → automate the
Vocoder cutoff with `apply_movement_plan` → `export_audio` and A/B against the boot
render (expect a large delta — the reference measurement is 0.0015–0.019 RMS).

**Worked example — microQ.** `add_instrument_part { pluginId: Vavra }` →
`apply_matrix_preset { engine: "vavra", id: <dump-bearing preset> }` — that injects a
complete 392-byte single dump (all 363 values, including the FX block that has no
host parameters) → `get_fx_capture_status` → `audition_plugin` (reference delta ≈ 0.0088).

---

## 7. Verification discipline (this is what keeps the claims honest)

1. **Prove audibility before comparing.** A silent slot makes every A/B equal —
   that trap manufactured the false finding "F-A: preset load does not change
   renders" (the Osirus was rendering *exact* silence; root-caused 2026-09-19).
   Assert `rms > 0.001` before drawing any conclusion from a delta.
2. **Use a measured noise floor, not a fixed threshold.** Render the *same* state
   twice, then require the applied change to beat 3× that jitter (and 1e-4
   absolute). Fixed thresholds of 1e-5…1e-4 sat at or below run-to-run variation and
   produced phantom passes/flakes.
3. **Some engines are jittery by nature.** The Microwave XT and microQ carry
   free-running state, so two renders of the same boot patch can differ by ~1e-2 RMS
   (measured 0.0129 / 0.0154). For those, assert **effect only** (`delta > 1e-4`) and
   say so; do not pretend a floor-based test resolved it.
4. **Delivery ≠ audibility, and the host param list is not a receipt.**
   `poll_fx_capture` + a render A/B are the evidence. SysEx/PC patch loads do **not**
   echo into the host param cache for these emulations (JE8086's 461-param cache stayed
   byte-identical across an audibly-applied `.syx`; same finding for OsTIrus). The live
   child state blob (`GET_STATE`) is *usually* a good delivery probe (the
   `Osirus…childState…` / `OsTIrusPresetChangeReflectsInChildParams` gates) **but not for
   JE8086** — it stayed constant across the load (2026-09-21). For JE8086 the durable
   readback is the persisted `IDs::presetSysex` dump replay: `Track.cpp` replays the raw
   DT1s into every fresh child, so a rebuilt child reproduces the patch exactly. Verify a
   load with `poll_fx_capture` + render.
5. **Name the gate when you claim a capability.** `FxMidiInjection.*` is the
   contract: `OsirusBootPatchAwakening`, `OsirusPresetChangeReflectsInRender`,
   `OsTIrusRenderAudibility`, `OsTIrusInjectionCapturesToTreeAndSurvivesRebuild`,
   `Xenia/Vavra/NodalRed2x HostParamsChangeRender`,
   `Xenia/VavraEditBufferDumpChangesOfflineRender`,
   `NordBankLoadChangesNodalRed2xRender`, `MatrixPresetAudibilityVirusVavra`,
   `Je8086UserPatchDumpChangesOfflineRender`.
   Run them with `run-tests-sharded.ps1 -Shards N -Filter <suite>` or `run_fast_tests.bat`.
6. **"The source says X" is not evidence.** Two incidents: an unclamped internal FX
   parameter that silenced every export at exactly 0.6 s, and a truncated
   `build/.ninja_deps` that made a no-op build take 285 s (both 2026-09). Verify the
   observable (render, timing, binary), not the source.

---

## 8. Known limits, and how to extend the corpus

**Current limits**

* **Virus preset-load for the TI/Osirus** works through CC0+PC; a per-patch *character*
  ear pass on the harvested virus matrix sheet is still outstanding (it was blocked by
  the silence bug).
* **NodalRed2x** bank loads are render-verified, but 14 real patches sounded
  near-identical in the 2026-09-18 ear pass — treat patch *delivery* as proven and
  patch *voice selection* as needing a listen.
* **JE8086** DT1 dumps *and* parameters apply: dumps via the 2026-09-20 wrapper retarget
  (`load_je8086_preset`), parameters via `set_fx_param` by name (461). A loaded patch is
  **not** visible in the param list or the live state blob — confirm via
  `poll_fx_capture` + render (see §7 item 4).
* **Per-sub-parameter automation of polymorphic FX slots** is impossible by design
  (derived-parameter collapse) — use type-level params or a whole-patch dump.
* **Vavra's non-public params** cannot be published (measured); the remaining 277
  sheet values are covered by the dump route instead.

**Extending** — the corpus pipeline that produces all of the above:

1. **Decode** a new library into sidecars (`timbre-lib/<device>_patch.py` →
   `<patch>.<engine>.json`).
2. **Curate** into a matrix sheet with corpus citations (`<engine>.json`:
   `{id, name, role, params, appliesVia, examples, evidence}`) — never invent a
   preset without a source patch.
3. **Build the offset map** (`<engine>_offset_map.json`: dump byte ↔ device name).
4. **Stamp injectable dumps** when the values exceed the host-param model —
   `timbre-lib/vavra_matrix_sysex.py` (392 B, parent resolved by the dump's embedded
   patch name) and `timbre-lib/xenia_matrix_sysex.py` (265 B, parent byte-verified by
   `xenia_dump.resolve_parent_dump`, re-framed to bank `0x20`). Both set
   `appliesVia: waldorf_dump` + `sysex`; `apply_matrix_preset` then injects them with
   no code change.
5. **Record the evidence** in `docs/va-suite-status-log.md` (moved from
   `docs/hardware-va-suite.md` §9, 2026-09-24) and add a gate.
