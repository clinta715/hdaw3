# Hardware VA suite — devices, patches, pipelines, modulation

Reference for the gearmulator CLAPs HDAW hosts (Roland JP-8000, Waldorf microQ,
Access Virus, Clavia Nord Lead 2x, Waldorf Microwave XT, Yamaha DX7) and the preset
pipelines behind them. Everything here is either **verified** (with the evidence
named) or explicitly marked **unverified**.

**Operational companion:** [`docs/core-synths-agentic-guide.md`](core-synths-agentic-guide.md) - which route applies which capability, what is automatable per device, and how to prove it worked.

**Authoritative parameter vocabulary:** every emulation ships a
`parameterDescriptions_*.json` next to its plugin wrapper — `je` (JP-8080),
`mq` (microQ), `TI` and `C` (Virus), `n2x` (Nord Lead 2x), `xt` (Microwave XT) —
under `gearmulator-2.2.9/source/{jeJucePlugin|mqJucePlugin|osTIrusJucePlugin|osirusJucePlugin|nord/n2x/n2xJucePlugin|xtJucePlugin}/`.
Use these (not guesses) when a recipe needs the device's own parameter or modulation
names; the microQ entry above is derived from `parameterDescriptions_mq.json`.

Companion docs: `docs/psytrance-composition-guide.md` §4D (recipes + param numbers),
`timbre-lib/README.md` (decoder usage), `docs/plans/2026-09-16-*.md` (pipeline plans).

## 1. Capability matrix

| Device | Emulation (CLAP) | Patches + pipeline | Loader status | Host params | Internal modulation | HDAW control |
|---|---|---|---|---|---|---|
| Roland JP-8000 | **JE8086** | 46 banks / 4983 entries / 2676 usable patches; `timbre-lib/je8086_patch.py` -> `<bank>.je8086.json` + exploded per-patch `.syx` (3689 files, verified 3689/0) | **DT1 dumps APPLY since 2026-09-20.** A real patch file addresses the UserPatch **bank** (`0x02000000`); neither the emulated OS nor the host state mirror folds a bank write into the sounding temp-performance patch, so injected dumps were inaudible no-ops (the wrapper's `jeController::parseSysexMessage` had an empty `case AddressArea::UserPatch`). `JE8086.clap` now retargets host-sourced DT1s to `PerformanceTemp | PatchUpper` — the transform the plugin's own browser always used (`Controller::sendSingle`) — and `load_je8086_preset` no longer appends a `CC0=1 USER + PC` recall (a JP-8080 PC **loads** the bank program into the current patch and overwrote the applied dump). Gate `FxMidiInjection.Je8086UserPatchDumpChangesOfflineRender`. Param writes work too. **Readback:** confirm a load via `poll_fx_capture` + a render A/B — the host param list and the live child state blob do **not** move (§9). | **461** (`list_fx_params`) | patch-level LFO1/LFO2 + ENV with destination switches (LFO1 dest: OSC1+2 / OSC2 / X-MOD), supersaw detune, onboard multi-FX + delay + tone | `set_fx_param` (works, verified), `send_fx_midi` CC/PC, SysEx dumps (**verified audible 2026-09-20**, §9) |
| Waldorf microQ | **Vavra** | 528 single-sound dumps; `timbre-lib/microq_patch.py` -> `<patch>.vavra.json` (528 sidecars, verify 528/0) | DUMP APPLICATION FIXED 2026-09-19: the 09-16 "NOT APPLYING" was buffer targeting (dumps carried 0x30/0x40+ buffer bytes; the OS plays the single-mode edit buffer 0x20) — `apply_preset` now retargets 392 B dumps to 0x20 + fixes the Waldorf checksum; gate `FxMidiInjection.VavraEditBufferDumpChangesOfflineRender` (rendered via the persisted dump: 0.0094→0.0157, and Δ0.0057 across a rebuild — both sides are fresh export children, not a live child) | **7557 host params since 2026-09-19** (96 curated sound/FX params × 16 parts made `isPublic` in `parameterDescriptions_mq.json` + rebuilt; HDAW proxy cap raised 4096→16384). `set_fx_param`/automation: **CORRECTED 2026-09-21 — writes reach renders.** A `set_fx_param` (or RPC `pluginParam.setParam`) write is persisted into the slot's `IDs::appliedParamOverrides` ledger, which `ExportManager` replays into every fresh export child, so it reaches `export_audio` / `audition_plugin` / `verify_part` (`FxMidiInjection.VavraHostParamPersistedWriteAffectsExport`: base 0.0174; `Ch N AmpVolume`→0 = 0.0026, →1 = 0.0077, monotonic). Writes never persisted are visible to the opt-in `liveParamState` probe (`FxMidiInjection.LiveParamStateProbeReflectsUnpersistedWrite`: 0.0034411 → 0.0289078 rms = 8.4x); `clear_fx_param_overrides` drops the ledger. The old reading ("LIVE writes are a NO-OP on the render", null attributed to a live-path gap) was **wrong**: every render in that harness is an offline export of a TREE COPY into a FRESH child, so a live-only write was never part of its input — by construction, not by delivery failure. The trace shows the write arriving and applying in the live child (`P1 stageParam` → `P3F FLUSHED` → `C1 SET` → `C1 DRAINED`), and the "same-child mode-flip noise" story did not reproduce (two consecutive no-write renders agreed to 4e-07). Use `waldorf_dump` for whole-patch changes, `set_fx_param` for a rendered param override | 3 oscillators, 2 filters, 4 envelopes, LFOs and a **ModMatrix** page (`mqLib/leds.h` pinpoints the pages: Osc1-3, Filters1-2, Env1-4, LFOs, ModMatrix). Verified structure from `mqJucePlugin/parameterDescriptions_mq.json`: **per-destination source+amount pairs** — `PitchModSrc`/`PitchModAmount`, `F1ModSource`/`F1CutoffMod`/`F1EnvMod`/`F1VelMod`/`F1PanModSource`/`F1PanMod` (and F2), plus `RingModLevel`/`RingModBalance`, `NoiseModeF1/F2`, `GlideMode`, `VoiceMode`. Onboard FX pages exist in the same file | `waldorf_dump` single-dump SysEx into the 0x20 edit buffer (retarget 2026-09-19, now applying) + 7557 host params (§9); `send_fx_midi` `EmuButtons`/`EmuRotaries` puppetry remains the route into pages with no host param |
| Access Virus | **OsTIrus / Osirus** | `timbre-lib/virus_patch.py` -> `<patch>.virus.json` + `virus_survey.json` (shipped earlier) | `load_virus_preset` (CC0+PC) **FIXED 2026-09-19** — state now round-trips to offline renders (see §9); dump writer `virus_dump.py` format-verified | **OsTIrus: 6939; Osirus: 3086** exposed. **OsTIrus WORKS offline** (renders audio, gate 2026-09-19). **Osirus (C) FIXED 2026-09-19** — renders audio offline (rms 0.047); gearmulator boot-patch fix, see §9 | matrix at **page 113** (`Assign1 Source`=64, `Assign1 Destination`=65, `Assign2 Source`=67, ...) plus `Lfo1/2/3 Mode`, `Lfo3 Destination`, `LfoN Env Mode`, Vocoder parameters; `Modulation Wheel` is `isPublic:false` (`parameterDescriptions_TI.json`/`_C.json`) | **OsTIrus and Osirus both render audio offline** (gates 2026-09-19). CC0+PC + send_fx_midi for patch selection; set_fx_param reaches the cache but the OS ignores host writes |
| Clavia Nord Lead 2x | **NodalRed2x** | `timbre-lib/nl2x_patch.py` -> `<patch>.nl2x.json` (6841 sidecars) | `load_nord_bank` **works and changes the render** (asserted by `FxMidiInjection.NordBankLoadChangesNodalRed2xRender`) — the one verified bank loader; morph .syx chains VERIFIED AUDIBLE 2026-09-16 (`matrix_presets/nord_morphs/`, written by `nord_dump.py`: 5 pairs x 4 performable steps) | **362 host params since 2026-09-19** (33 curated sound params made `isPublic` in `parameterDescriptions_n2x.json` + rebuilt; HDAW proxy cap already raised). `set_fx_param`/automation verified: `FxMidiInjection.NodalRed2xHostParamsChangeRender` (Cutoff set → same-child render Δ0.00039) | MOD ENV + LFOs with per-parameter sensitivity dials (`parameterDescriptions_n2x.json`); NO onboard FX | `load_nord_bank` + `set_fx_param` (Cutoff, Resonance, FilterEnvAmount, AmpEnv A/D/S/R, ModEnvLevel, Lfo1Rate/Level, Distortion...) + CC/PC; HDAW internal FX |
| Waldorf Microwave XT | **Xenia** | `D:\pdf\microwave` (6 `.µsb` bank images of 256x256 B + 1 SMF bank); `timbre-lib/microwave_patch.py` -> `<bank>.xenia.json` — **1791 patches, verify 7 ok / 0 bad** | edit-buffer SysEx **VERIFIED AUDIBLE** 2026-09-16 (bank 0x20, `xenia_dump.py` — see §9); `apply_preset` WaldorfSysex route added 2026-09-18 (F0 3E 0E: validate+split+queue, tested); patch-level unproven | **2151 host params since 2026-09-19** (66 curated sound/FX params made `isPublic` in `parameterDescriptions_xt.json` + rebuilt). `set_fx_param`/automation verified: `FxMidiInjection.XeniaHostParamsChangeRender` (F1Cutoff 1.0→0.1 via the OS, same-child render Δ>1e-5) | wave-envelope amounts (`W1/W2EnvAmount`), `F1EnvAmount`, `MixRingMod`, `EffectType`/`EffectParamA-C`, own arp | `apply_preset` (F0 3E 0E .syx -> validated SysEx) / `send_fx_midi` edit-buffer dumps; no host params |
| Yamaha DX7 | ~~Dexed~~ **not core** | — | Dexed is retired from the suite: **`fm_synth` / PsyFm is the FM engine** (its own params, presets and modulation targets 300-308); the DX7 .syx cartridge route was never a core path (probed: peak 0, state byte-identical) | n/a | FM operators/envelopes via `fm_synth` | internal `fm_synth` params |

Grid-wide facts worth knowing before choosing a device:

- **Isolation and state.** The whole suite runs as isolated CLAPs (child process). An
  offline export instantiates a **fresh child** and restores `IDs::pluginState` into
  it, so an export matches what you audition **only if** the patch round-trips
  through the plugin's `getStateInformation` **or** the loader persisted the raw dumps
  in `IDs::presetSysex` (replayed into the fresh child by `Track.cpp` — how JE8086 DT1
  dumps and Xenia/Vavra edit-buffer dumps survive). The Nord's bank load round-trips
  through state (its test asserts a render change). Measured on the 2026-08 build
  (before the `JPAR` state chunk): restoring a captured state was a no-op for the
  render (peak identical to 16 digits), so the earlier "capture poisons the render"
  reading was wrong.
- **Engine hygiene (D-lite).** A capture that merely echoes the state an instance
  reported when it appeared is no longer persisted, and the deferred capture reports
  `captureStatus="unchanged"` instead of a fake `ok`. Nothing about that fixes
  hear-not-equal-export; it only stops meaningless state being written.
- **Params or nothing.** Every core engine now publishes host parameters (since
  2026-09-19/20): JE8086 461, Vavra 7557, OsTIrus 6939 / Osirus 3086, Xenia 2151,
  NodalRed2x 362. But a published parameter is not automatically automatable — check
  route + durability with `list_device_params` first. What is still not host-exposed
  (ROM-program selection, non-parameter state) needs MIDI/SysEx.

## 2. Modulation-first policy (prefer the device over a plugin)

When a part needs movement, take the cheapest layer that can produce it:

1. **The device's own modulation** (LFO / envelopes / mod matrix) — no extra CPU, no
   latency, survives state save/load when the plugin's state round-trips, and keeps
   the part playable on the hardware model.
2. **The device's onboard FX** (JP-8080 multi-FX + delay, microQ FX, Virus FX) —
   still in-device, no host round-trip.
3. **HDAW track automation / modulation** (`set_fx_slot_param` / `set_fx_param`,
   track LFOs via `MODULATION_LIST`, movement plans) — use when the device exposes the
   target parameter but has no internal routing to it, and prefer it over inserting a
   plugin effect.
4. **HDAW internal FX** (chorus/delay/reverb/filter/compressor slots) — the right
   answer when the device has *no* onboard FX (Nord Lead 2x) or when the effect is
   arrangement-level (drops, throws, risers) rather than patch-level.
5. **Third-party plugin FX** — last resort: they add CPU, latency, isolation and
   state-round-trip risk (a plugin whose state does not round-trip will sound
   different in the export than in audition).

Corollary: before adding a plugin for "movement", check whether the device's matrix
can do it (the recipes below), and prefer a **parameter write or CC** over an inserted
effect whenever the device already has the effect onboard.

## 3. FX recipes per device (device-internal first)

### JP-8000 / JE8086 (parameter writes + DT1 dumps both verified)
- **Filter movement**: automate cutoff/resonance (params 0x29/0x2A in the dump layout;
  `set_fx_param` by index) instead of adding a plugin filter; pair with LFO2 ->
  FILTER (`Lfo2DepthSelect`) for hands-free motion.
- **Pitch drift**: LFO1 -> OSC1+2 with a tiny `OscLfo1Depth`; keep pitch modulation
  off the bass and stabs (guide §4D rule: no discord).
- **Width without a plugin chorus**: OSC1 = SUPER SAW with `Osc1Control2` (detune)
  plus the onboard MULTI-FX "SUPER CHORUS SLW"; automate `MultiEffectsLevel`.
- **Throws**: automate the onboard delay (type/time/feedback/level) instead of
  inserting a delay plugin; the guide's §4D param numbers (184-187) are the recipe.
- **State round-trips** (2026-09-18 `JPAR` param chunk + 2026-09-20 DT1 retarget): the
  slot carries both `pluginState` (~7 KB JPAR, the 461 params) and `presetSysex` (the raw
  DT1 pages), and a child rebuilt from the tree replays the applied patch — gate
  `FxMidiInjection.Je8086UserPatchDumpChangesOfflineRender` (rebuilt-from-tree rms equals
  the applied patch to 6 digits). Verify by audition **and** export; the 2026-08-era
  "JE8086 state does not carry the patch" caveat is superseded.

### Waldorf microQ / Vavra (7557 host params + dumps — §9)
- **In-device motion** (structure verified in `parameterDescriptions_mq.json`): the
  microQ gives every destination its own **source + amount** pair rather than a fixed
  LFO — `PitchModSrc`/`PitchModAmount`, `F1ModSource`/`F1CutoffMod`/`F1EnvMod`/`F1VelMod`,
  `F1PanModSource`/`F1PanMod` (and F2), so per-voice pitch drift, filter sweep and pan
  motion are all patch-level settings. Add `RingModLevel`/`RingModBalance` and
  `NoiseModeF1/F2` for texture. Use the **ModMatrix** page (and `leds.h`'s page list:
  Osc1-3, Filters1-2, Env1-4, LFOs) for the extra routing slots. The exposed
  counterparts (`F1CutoffMod`, `F1EnvMod`, ...) are host-writable, but prefer the
  patch when the movement must survive a reload.
- **Onboard FX** (chorus / flanger / phaser / delay / reverb): only the type-level
  params (`FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix`) are host-exposed — the sub-params
  are **bit-aliases** (derived-parameter collapse, §9), so set the character inside
  the patch or via a `waldorf_dump`.
- **`set_fx_param` reaches renders (corrected 2026-09-21).** 7557 params are
  reachable and dumps apply (0x20 retarget, 2026-09-19). A `set_fx_param` (or its
  RPC twin `pluginParam.setParam`) write is now **persisted** into the slot's
  `IDs::appliedParamOverrides` ledger, which `ExportManager` replays into every
  fresh export child — so it reaches `export_audio`, `audition_plugin` and
  `verify_part` (`VavraHostParamPersistedWriteAffectsExport`: `Ch N AmpVolume`
  → 0.0 = 0.00357 rms, → 1.0 = 0.00842, monotonic). A write that was never
  persisted is visible to the opt-in **`liveParamState`** render probe
  (`audition_plugin` / `composition.auditionPlugin`, **default OFF**;
  `LiveParamStateProbeReflectsUnpersistedWrite`: 0.0034411 → 0.0289078 rms), and
  `clear_fx_param_overrides` drops the ledger.
  **The old reading was wrong.** "LIVE writes do NOT move the render — the gap is
  the live write path" described a **harness artifact**: every render in that
  harness is an offline export of a TREE COPY into a FRESH child, so a live-only
  write was never part of that child's input (by construction, not by delivery
  failure). The trace shows the write arriving and being applied in the live child
  (`P1 stageParam` → `P3F FLUSHED` → `C1 SET` → `C1 WARM` → `C1 DRAINED`), and
  the "same-child ~2x mode flip" that framed the null did not reproduce — two
  consecutive no-write renders agreed to 4e-07. The sibling-engine "controls" in
  that narrative were artifacts too (Xenia's claimed 6.1e-3 does not reproduce;
  NodalRed2x's 2.7e-4 is not resolvable; Osirus's "0 → 0.047" is the ROM
  boot-patch fix, lesson 25/26). See
  `docs/plans/2026-09-21-vavra-live-param-delivery.md` and
  `docs/plans/2026-09-21-plugin-param-persistence.md`.

### Access Virus / OsTIrus, Osirus (params + CC)
- **Preset selection** is CC0+PC (`load_virus_preset`) — the cheapest way to switch
  character between sections. **F-A RESOLVED 2026-09-20** (boot-patch fix for the
  silent Osirus slot + the `stateSet` SHM ring, §9): the state now round-trips and
  the slot renders audio. Confirm a param→rebuilt-render A/B per build.
- **CC modulation**: automate brightness (CC74) and mod wheel (CC1) with
  `send_fx_midi` to drive the Virus's own matrix routings; this needs no parameters
  and survives as MIDI.
- **Unison + onboard FX** instead of plugin doubling/chorus; automate the onboard
  delay send for drops.

### Clavia Nord Lead 2x / NodalRed2x (bank load verified; no onboard FX)
- **Bank loading works**: `load_nord_bank` is the one verified bank loader in the
  suite, and its test asserts the render changes — use it for patch-level variety.
- **Internal**: MOD ENV -> filter for plucks, LFO2 -> pitch for vibrato, with
  mod-wheel/velocity amounts (the `mod` parameter group) doing the performance part.
- **No onboard FX** -> this is the case where HDAW internal FX (chorus/delay/reverb)
  is the correct layer, and where an FX-chain preset earning its place is justified.

### DX7 / Dexed
- Cartridge injection is ignored; use the internal `fm_synth` (same engine family,
  fully parameterized) and its own envelopes/operator levels for FM movement.

## 4. Modulation matrices (harvested from the emulators' own vocabularies)

Extracted from the `parameterDescriptions_*.json` files named in §1 — these are the
devices' real parameter names, not approximations.

### Waldorf microQ (Vavra) — per-destination source + amount (192 mod parameters)

Control caveat: these are patch-level settings. The microQ remote control accepts only
button/encoder events (see §7 row 5), so from HDAW the matrix is reachable only by
loading a patch that already contains the routing, or by puppeting the front panel.
| Target | Modulation parameters |
|---|---|
| Pitch | `PitchModSrc`, `PitchModAmount` (plus `GlideMode`, `VoiceMode`) |
| Osc 1/2/3 FM | `O1FmSource`, `O2FmSource`, `O3FmSource` |
| Osc 1/2/3 PWM | `O1PwmSource`, `O2PwmSource`, `O3PwmSource` |
| Filter 1 | `F1ModSource`, `F1CutoffMod`, `F1EnvMod`, `F1VelMod`, `F1FmSource` |
| Filter 1 pan | `F1PanModSource`, `F1PanMod` |
| Filter 2 / pan | `F2ModSource`, `F2CutoffMod`, `F2EnvMod`, `F2VelMod`, `F2PanModSource` |
| Texture | `RingModLevel`, `RingModBalance`, `NoiseModeF1`, `NoiseModeF2` |

Recipes: a **source-per-destination** matrix means movement is written into the patch —
`F1ModSource` = an LFO with `F1CutoffMod` amount for a filter that opens on its own;
`F1PanModSource`/`F1PanMod` for per-voice pan drift (width with no host automation);
`O1PwmSource` for PWM shimmer on a pad; `PitchModSrc` micro-drift on a pad/stab only
(no pitch modulation on bass or leads — guide §4D rule).

### Access Virus TI / C (OsTIrus / Osirus) — "X > Y" routing names
`Osc2 HyperSaw/FilterEnv > Pitch`, `Osc2 Wavetable/FilterEnv > FM`,
`Osc2 HyperSaw/FilterEnv > SyncFrequency`, `Filter1 Env Amt` / `Filter2 Env Amt`,
`Filter Env Attack/Decay/Sustain/Sustain Time/Release`, `Modulation Wheel`,
`Ringmodulator Volume`, `Vocoder/Modulator Center Frequency | Frequency Offset |
Q Factor | Frequency Spread` (157 mod-related names in the TI, 106 in the C).

Recipes: env→pitch only where the guide allows it (never bass/lead);
env→FM or env→sync for metallic movement instead of a plugin distortion;
the **vocoder** parameters are worth an FX pass on their own (modulated center
frequency = classic psy vocal/gate texture) and they live in the device, so no plugin
vocoder is needed.

### Roland JP-8080 (JE8086) — fixed destinations, depth per target (298 names)
`Lfo1Waveform/Rate/Fade`, `Lfo1AndEnvelopeDestination` (OSC1+2 / OSC2 / X-MOD),
`OscLfo1Depth`, `PitchLfo2Depth`, `PitchEnvelopeDepth/Attack/Decay`,
`Osc1Control2LFO1Depth`, `Osc2Control2LFO1Depth`, `FilterLfo1Depth`, `FilterLfo2Depth`,
`FilterEnvelopeDepth/ADSR`, `AmpLfo1/2Depth`, `Lfo2DepthSelect`, `RingModulatorSwitch`,
`CrossModulationDepth`.

Note: the plugin's parameter list is **not** the SysEx patch layout (it contains
parameters such as `Osc1Control2LFO1Depth` that have no dump offset), so map by NAME
against `list_fx_params`, never by patch offset. These are HDAW-automatable with
`set_fx_param`, which is the practical route for JP-8080 movement. DT1 patch dumps
apply too since 2026-09-20 (`load_je8086_preset`, wrapper retarget; §9).

### Waldorf Microwave XT (Xenia) — env amounts + velocity sensitivity
`W1EnvAmount`/`W1EnvVelAmount`, `W2EnvAmount`/`W2EnvVelAmount`, `F1EnvAmount`,
`F1EnvVelAmount`, `F1EnvAttack/Decay/Sustain/Release/Trigger`,
`AmpEnvAttack/Decay/Sustain/Release/Trigger`, `MixRingMod`, `GlideMode`, `ArpMode`,
`AllocationMode`.

Recipes: wave-envelope amounts are the XT's signature — use them instead of a plugin
filter/phaser sweep; `MixRingMod` for bell/metallic FX; the arp is in-device, so
sequence it in the patch rather than with a plugin arpeggiator.

### Clavia Nord Lead 2x (NodalRed2x) — MOD ENV + LFOs with sensitivity dials
`ModEnvA`, `ModEnvD`, `ModEnvLevel`, `Lfo1Rate`, `Lfo1Level`, `Lfo2Rate`,
`FilterEnvAmount`, `FilterEnvA/D/S/R`, `AmpEnvA/D/S/R` and per-parameter
**sensitivities**: `FilterEnvAmountSens`, `FilterEnvASens/DSens/SSens/RSens` (84
mod-related names).

Recipes: MOD ENV → filter (`FilterEnvAmount`) with sensitivity set for velocity gives
plucks and stabs without any plugin; the Nord has **no onboard FX**, so HDAW internal
chorus/delay is the correct layer here (§3).

## 5. What to add next (evidence-gated)

- **Xenia (Microwave XT)**: patch DELIVERY works live (verified 2026-09-18 via the
  apply_preset WaldorfSysex route against the real plugin) — the remaining gap is
  the state CAPTURE: make the child serialized state include the edit-buffer
  single so offline exports / save-load / rebuild route (Track.cpp
  reads IDs::pluginState).
- **Virus TI/C**: enumerate the "X > Y" routings into a matrix table (the JSON has
  them; `157`/`106` mod-related names) and check whether SysEx bank loading works the
  way CC0+PC does.
- **microQ**: verify injection by watching the plugin editor's LCD (the emulation's
  State does receive external dumps, unlike the JP-8080) — that is the one open check
  before a `load_vavra_preset` tool.


## 7. Operations on the FX / modulation surfaces

What HDAW can actually *do* with a device's onboard FX and modulation, cheapest layer
first (see §2 for why this order):

| # | Operation | Tooling | Works on |
|---|---|---|---|
| 1 | **Bake it into the patch** — set the device's own FX type/mix/depth and matrix routings | edit the patch (or the plugin's editor), then load/save | all devices; this is the preferred layer |
| 2 | **Live parameter writes** | `set_fx_param {trackId, slotIndex, paramIndex, value}` | **all five param-bearing engines since 2026-09-19/20**: JE8086 461, Vavra 7557 (96 curated sound/FX x 16 parts), OsTIrus 6939 / Osirus 3086, Xenia 2151, NodalRed2x 362 — all made `isPublic` and rebuilt (the old `{"params":[]}` reading predates that). Gates: `Xenia/NodalRed2xHostParamsChangeRender` (exposure + host-side staging + durable round trip — NodalRed2x's Cutoff effect is asserted at 3.1-8.2x separation, Xenia's is **not resolvable**: its same-input spread (0.0056) exceeded the separation (0.0023), so no effect is claimed for it); the Virus param path round-trips to offline renders (F-A phase 2); **Vavra `set_fx_param` writes reach renders through the durable `appliedParamOverrides` ledger** (`VavraHostParamPersistedWriteAffectsExport`) and unpersisted writes through the opt-in `liveParamState` probe (`LiveParamStateProbeReflectsUnpersistedWrite`) — see §9 (the earlier "LIVE writes do not move the render" reading was a harness artifact). Dexed exposes none — not core (use `fm_synth`/PsyFm) |
| 3 | **Automation / movement** — ramps, risers, throws, macro morphs on those parameters | track automation lanes and `apply_movement_plan` (macro events with start/end values) | **any param-bearing engine** — row 2 lists the exposed host params, and HDAW automation lanes / `apply_movement_plan` macros drive them the same way (JE8086 delay-throw = `DelayLevel`, Nord `FilterEnvAmount`, Xenia `F1Cutoff` — not resolvable, see row 2; Vavra: durability via the `appliedParamOverrides` ledger, live-only state via the opt-in `liveParamState` probe, §9). Verified: `NodalRed2xHostParamsChangeRender` (Cutoff 3.1-8.2x); JE8086 param writes live+offline (2026-09-18, §7). Vavra is not in the *live-audibility* list: a live-only write cannot reach a tree-derived render, so its live effect is measured through the opt-in probe instead. The pre-2026-09-19 "**JE8086 only**" note predated the `isPublic` param publishing. Where the device's own LFO/matrix can make the movement, prefer that (patch-level → survives reload) |
| 4 | **MIDI CC / program change** — drive the device's CC-mapped routings and switch ROM presets | `send_fx_midi` (CC, PC, notes) | all devices that respond to MIDI (Virus Modulation Wheel, CC74 brightness, CC0+PC preset select) |
| 5 | **Front-panel puppetry (remote-control SysEx)** — press the device's buttons and turn its encoders, i.e. drive it the way a human does | `send_fx_midi` {kind:"sysEx"} with the emulation's protocol. microQ (verified in source): header `F0 3E 10 00 <cmd>`, then `EmuButtons` = `52 <buttonIdx> <state>` or `EmuRotaries` = `53 <encoderIdx> <amount+64>` (`mqLib/mqsysexremotecontrol.cpp`); the device streams `EmuLCD` / `EmuLEDs` / `EmuLCDCGRata` back for verification. There is **no parameter-address write** — a `SetParam` message is ignored (probed: state byte-identical) | **the only HDAW route into Vavra's FX and matrix pages** (navigate with `EmuButtons`, adjust with `EmuRotaries`), and a fallback for hidden params elsewhere. Unverified from HDAW; verify by watching the plugin editor's LCD |
| 6 | **State / preset operations** — snapshot, save, recall | `capture_fx_snapshot`, `save_fx_chain`, `load_plugin_preset`, `apply_preset`, `save_project` | all, subject to the isolation caveats in §1 (a plugin whose state does not round-trip its patch exports differently from what you audition) |

### Onboard FX inventory (from each device's own parameter vocabulary)

| Device | FX surface |
|---|---|
| microQ (Vavra) | `FX1Type`, `FX2Type`, `FX1Mix`, `FX2Mix` and the per-slot chorus/phaser/delay sub-parameters (`Fx1ChorusSpeed/Depth/Delay`, ...) — 165 FX-related parameters; plus per-filter pan modulation (`F1PanModSource`) |
| Virus TI/C (OsTIrus/Osirus) | `Chorus/Type`, `Ringmodulator Volume`, and a full **Vocoder**: `Vocoder/Carrier` and `Modulator Center Frequency`/`Frequency Offset`/`Q Factor`/`Frequency Spread`, `Vocoder/Bands`, `Attack`/`Release`, `Spectral Balance`, `Vocoder/Balance` |
| JP-8080 (JE8086) | `ChorusType`, `ChorusLevel`, `MultiEffectsLevel`, `DelayType`, `DelayTime`, `DelayFeedback`, `DelayLevel`, `RingModulatorSwitch`, `AmpPan`, `AutoPanManualPanSwitch` + the `Control*` depth parameters |
| Microwave XT (Xenia) | `EffectType`, `EffectParamA/B/C`, `DelayTime`, `ChorusEnabled`, `MixRingMod`, `Pan`/`PanKeytrack`/`DePan`, `Lfo1Delay`/`Lfo2Delay`, `ModDelaySource`/`ModDelayTime` |
| Nord Lead 2x (NodalRed2x) | **no onboard FX** (only `RingMod` and `Distortion`) — this is the device where HDAW internal FX is the correct layer |

Practical consequence: for every device except the Nord, an effect that already exists
onboard should be driven **there** (patch content, a parameter write, a CC, or a macro
plan) rather than re-created with a plugin — and where the device hides the parameter
from the host (Vavra), the honest options are patch-level editing or the
remote-control SysEx route, not `set_fx_param`.


## 8. Transitional effects over time (the modulation matrix as a transition engine)

A modulation matrix is **static routing** — what it cannot do by itself is *change over
time*. Every audible transition in these devices comes from one of five levers; the
trick is knowing which lever each device exposes and which one HDAW can drive.

### The five levers

| Lever | What it gives you | Where it lives |
|---|---|---|
| **1. One-shot envelope shape** routed to a destination | risers (long attack → pitch/filter), downlifters (fast decay → pitch), plucks (short decay → filter) | device patch (matrix amount + env ADSR) |
| **2. Automating the matrix AMOUNT, not the destination** | a filter sweep that *widens* as it rises; a vibrato that appears only in the last bar; a build whose timbre opens progressively | HDAW parameter automation / movement plans — the single most useful transition lever |
| **3. LFO rate/depth ramps** | periodic → transitional: 1/8 gate lifting to 32nds, a wobble that accelerates into the drop | automate the LFO rate/depth parameter (or CC), with the LFO already routed in the matrix |
| **4. FX changes over time** | delay feedback dives, reverb swells, chorus widening, distorted build | device onboard FX (type/mix/time/feedback) via parameter write or CC |
| **5. Discrete patch/bank morphs** | section-to-section character change | Nord 2x bank load (verified), Virus CC0+PC program change, patch load per section |

### Recipe catalogue (device-internal first)

| Transition | Build | Device + encoder |
|---|---|---|
| **Riser (tension build)** | env with long attack → pitch (small amount) + cutoff sweep + filter-env-depth ramp | Virus: `Osc2 Wavetable/FilterEnv > Pitch` + `Assign1 Source/Destination`; Nord: `ModEnvLevel` → filter; JE8086: `Lfo1AndEnvelopeDestination` + `FilterEnvelopeDepth` ramp via `set_fx_param` |
| **Filter sweep that widens** | automate cutoff AND filter-env depth (lever 2) | any device with params: JE8086 `CutoffFrequency`+`FilterEnvelopeDepth`, Nord `FilterEnvAmount`, Xenia `F1EnvAmount`+`F1CutoffMod` |
| **Gate lift (psy staple)** | LFO → amp routed in the matrix, then automate the LFO rate from 1/8 to 1/32 | Virus `Lfo3 Destination` + rate automation/CC; microQ `AmpModSource` + LFO rate; JE8086 `AmpLfo1Depth` + `Lfo1Rate` |
| **Wobble that accelerates** | LFO → filter with a rate ramp (lever 3) | microQ `F1ModSource`/`F1CutoffMod`; Xenia `ModDelaySource`/`ModDelayTime`; Nord `Lfo2Rate` |
| **Downlifter / drop-out** | fast-decay env → pitch (negative amount) at the section boundary | Virus `FilterEnv > Pitch` with a short env; microQ `PitchModAmount` negative; JE8086 `PitchEnvelopeDepth` |
| **Delay dive / throw** | automate FX feedback + level together | JE8086 `DelayFeedback`+`DelayLevel` (guide §4D params 186/187); Virus `Delay` mix; microQ `FX1Mix` (SysEx route only); Xenia `DelayTime` |
| **Reverb swell into a breakdown** | automate the FX mix over the last bar before the break | Virus `Reverb`; microQ `FX2Mix` (SysEx); Xenia `EffectParamA/B/C` |
| **Metallic FM/ring build** | env → FM/ring amount, ramped | Virus `Osc2 Wavetable/FilterEnv > FM`; microQ `O1FmSource`/`RingModLevel`; Xenia `MixRingMod` |
| **Morph between two characters** | load bank B for the next section instead of automating anything | Nord `load_nord_bank` (verified render change); Virus CC0+PC |
| **Vocal/gate texture** | Vocoder with a ramped modulator frequency | Virus `Vocoder/Modulator Center Frequency` + `Vocoder/Bands`, `Attack`/`Release` |

### Rules that keep transitions clean

- **Ramp, never step** — a single parameter jump clicks; automation lanes and movement
  plans interpolate, one-shot writes do not.
- **Automate amounts, not destinations** (lever 2). Rewriting a matrix *destination* per
  block is a step change with no musical shape; ramping the *amount* into a fixed
  destination is a sweep.
- **No pitch modulation on bass or leads** (guide §4D): use pitch envelopes only for
  risers/downlifters, with small amounts.
- **Per-voice LFO pumping is not bus pumping.** A matrix LFO → amp gives per-voice
  tremolo; the psy "sidechain" pump is a *bus* behaviour, so use HDAW's master/track
  compressor or an amplitude automation lane for that instead.
- **Verify on the right instrument.** A render reflects the device only when its state
  round-trips (Nord: verified via `load_nord_bank`; JE8086: verified via the 2026-09-20
  DT1 retarget + `pluginState`/`presetSysex` — the rebuilt child replays the patch). For
  microQ, the FX **sub**-parameters share indexes and collapse into their type-level
  parents (`FX1Type`/`FX2Type`/`FX1Mix`/`FX2Mix`), so per-sub-param automation is
  impossible by design — bake those transitions into the patch/dump or send SysEx.
- **Keep the transition in the project file.** Device-internal movement is saved inside
  the patch (via the plugin state); HDAW automation is saved in the ValueTree. Anything
  driven by hand (SysEx pokes, live CC) disappears on reload — prefer the two durable
  layers.

### Open verification (the next concrete steps)

1. **Vavra: DONE — panel puppetry is the only route.** Verified in
   `mqLib/mqsysexremotecontrol.cpp`: the device accepts `EmuButtons`
   (`52 <buttonIdx> <state>`), `EmuRotaries` (`53 <encoderIdx> <amount+64>`) and
   answers `EmuLCD`/`EmuLEDs`/`EmuLCDCGRata`; **there is no parameter write** — a
   crafted `SetParam` (page 0, index 191) left the captured state byte-identical, as
   the source predicts. So HDAW can *navigate and dial the microQ's front panel*
   (`EmuButtons` to reach the FX/ModMatrix pages, `EmuRotaries` to change values) but
   cannot address parameters; the practical consequence stands: **bake microQ FX/matrix
   movement into the patch**, and treat live control as a future experiment verified via
   the plugin editor's LCD.
2. **Virus matrix table**: its matrix is exposed as `Assign1 Source` / `Assign1
   Destination` pairs (plus `Lfo1/2/3 Mode`, `Lfo3 Destination`, `LfoN Env Mode`) — 107
   matrix-related names harvested; enumerating the Assign1..N pairs and their
   source/destination options is a cheap follow-up that makes Virus transitions
   scriptable.
3. **Xenia patch pipeline** once Microwave banks exist (vocabulary already harvested).

## 7b. Harvesting FX / matrix recipes from the patch libraries

The patches *contain* the effect and modulation recipes, so the libraries are a corpus:
`timbre-lib/harvest_fx_presets.py` mines the sidecars and emits
`timbre-lib/harvested_fx_presets.json` - per engine and per FX/modulation parameter, the
value distribution, how many patches carry it, and example patches that use the most
common value (so a human can audition the source of a recipe).

    py -3 timbre-lib/harvest_fx_presets.py --out timbre-lib/harvested_fx_presets.json \
        --vocab vavra=<...>/parameterDescriptions_mq.json \
        --vocab xenia=<...>/parameterDescriptions_xt.json \
        "D:/pdf/je8086" "D:/pdf/rhythm-lab.com_waldorf_micro_q" "D:/pdf/microwave" "D:/pdf/NL2x Banks"

Sweep 2026-09-16: **11,111 sidecars** (je8086 3735, vavra 528, xenia 7, nodalred2x 6841)
yielding, per engine, the FX/modulation vocabulary actually in use - e.g. 35 named
FX/mod parameters for JE8086 (its `AmpLfo1Depth`, `AutoPanManualPanSwitch`,
`ControlCutoffFrequency` groups), 16 for Xenia (`W2EnvAmount`, `ModDelayTime`, `Depan`)
and 4 for the Nord (`cutoff`, `resonance`, `mix`, `sync_distortion`).

Why this is useful: these recipes can be **re-created on HDAW's internal FX and
movement planes** (the modulation-first policy) on every device — including those
whose FX/matrix pages are dump- or panel-only. Two honest limits: (a) the harvested values
live in each device's own parameter space, so reproducing them is an approximation, not
a byte-exact transfer; (b) microQ parameters in its sidecars are stored as **dump
offsets**, not device parameter indices; the dump layout is now MAPPED (dump byte = 7 +
linear parameter-descriptions index, verified over 305 sidecar/syx pairs), so `vavra`
ships named matrix presets for the 86-key FX/matrix subset — see section 9 (every other
dump slot stays a raw `off_<N>` key; naming the full 363-param block remains open).

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

## 6. Pipeline commands (one line each)

    py -3.14 timbre-lib/virus_patch.py  --sidecars "<Virus bank dir>"
    py -3.14 timbre-lib/nl2x_patch.py   --sidecars "D:\pdf\NL2x Banks"
    py -3.14 timbre-lib/je8086_patch.py --sidecars "D:\pdf\je8086" --explode
    py -3.14 timbre-lib/microq_patch.py --sidecars "D:\pdf\rhythm-lab.com_waldorf_micro_q"

Sidecars land next to the patch files as `<patch>.<engine>.json` (`.virus.`, `.nl2x.`,
`.je8086.`, `.vavra.`); `FileLibraryManager` ingests all four and feeds
`search_library` with engine, role verdict, description and tags. Register each library
folder as a **patch** library once and the presets become searchable.
