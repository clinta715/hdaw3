# Hardware VA suite — devices, patches, pipelines, modulation

Reference for the gearmulator CLAPs HDAW hosts (Roland JP-8000, Waldorf microQ,
Access Virus, Clavia Nord Lead 2x, Waldorf Microwave XT, Yamaha DX7) and the preset
pipelines behind them. Everything here is either **verified** (with the evidence
named) or explicitly marked **unverified**.

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
| Roland JP-8000 | **JE8086** | 46 banks / 4983 entries / 2676 usable patches; `timbre-lib/je8086_patch.py` -> `<bank>.je8086.json` + exploded per-patch `.syx` (3689 files, verified 3689/0) | **DT1 dumps are NOT applied** (param cache byte-identical after injection; `jeLib/device.cpp` routes live MIDI to the DSP thread, the DT1 patch State is not on that path). Param writes DO work. | **461** (`list_fx_params`) | patch-level LFO1/LFO2 + ENV with destination switches (LFO1 dest: OSC1+2 / OSC2 / X-MOD), supersaw detune, onboard multi-FX + delay + tone | `set_fx_param` (works, verified), `send_fx_midi` CC/PC, SysEx dumps (queued, unverified) |
| Waldorf microQ | **Vavra** | 528 single-sound dumps; `timbre-lib/microq_patch.py` -> `<patch>.vavra.json` (528 sidecars, verify 528/0) | Injection queues but **MEASURED NOT APPLYING** (2026-09-16: `captureStatus=unchanged`, renders identical — emulator limitation; see §9) | **0** (`{"params":[]}`) | 3 oscillators, 2 filters, 4 envelopes, LFOs and a **ModMatrix** page (`mqLib/leds.h` pinpoints the pages: Osc1-3, Filters1-2, Env1-4, LFOs, ModMatrix). Verified structure from `mqJucePlugin/parameterDescriptions_mq.json`: **per-destination source+amount pairs** — `PitchModSrc`/`PitchModAmount`, `F1ModSource`/`F1CutoffMod`/`F1EnvMod`/`F1VelMod`/`F1PanModSource`/`F1PanMod` (and F2), plus `RingModLevel`/`RingModBalance`, `NoiseModeF1/F2`, `GlideMode`, `VoiceMode`. Onboard FX pages exist in the same file | `send_fx_midi` SysEx only (measured NOT applying); no params to automate |
| Access Virus | **OsTIrus / Osirus** | `timbre-lib/virus_patch.py` -> `<patch>.virus.json` + `virus_survey.json` (shipped earlier) | `load_virus_preset` (CC0+PC) queues but does NOT change renders — finding F-A, silent Osirus slot (see §9); dump writer `virus_dump.py` format-verified | **3,086** exposed on Osirus post-`ensureLiveRouting`, but `set_fx_param` name resolution diverges from `list_fx_params` — finding F-B (see §9) | matrix at **page 113** (`Assign1 Source`=64, `Assign1 Destination`=65, `Assign2 Source`=67, ...) plus `Lfo1/2/3 Mode`, `Lfo3 Destination`, `LfoN Env Mode`, Vocoder parameters; `Modulation Wheel` is `isPublic:false` (`parameterDescriptions_TI.json`/`_C.json`) | `load_virus_preset` + `send_fx_midi` CC/PC (queue; renders currently silent — F-A); **no param automation** |
| Clavia Nord Lead 2x | **NodalRed2x** | `timbre-lib/nl2x_patch.py` -> `<patch>.nl2x.json` (6841 sidecars) | `load_nord_bank` **works and changes the render** (asserted by `FxMidiInjection.NordBankLoadChangesNodalRed2xRender`) — the one verified bank loader; morph .syx chains VERIFIED AUDIBLE 2026-09-16 (`matrix_presets/nord_morphs/`, written by `nord_dump.py`: 5 pairs x 4 performable steps) | **none exposed** (the same pattern; its verified control path is the bank load) | MOD ENV + LFOs with per-parameter sensitivity dials (`parameterDescriptions_n2x.json`); NO onboard FX | `load_nord_bank` + CC/PC; HDAW internal FX |
| Waldorf Microwave XT | **Xenia** | `D:\pdf\microwave` (6 `.µsb` bank images of 256x256 B + 1 SMF bank); `timbre-lib/microwave_patch.py` -> `<bank>.xenia.json` — **1791 patches, verify 7 ok / 0 bad** | edit-buffer SysEx **VERIFIED AUDIBLE** 2026-09-16 (bank 0x20, `xenia_dump.py` — see §9); patch-level unproven | **not exposed** (same pattern; not measured live) | wave-envelope amounts (`W1/W2EnvAmount`), `F1EnvAmount`, `MixRingMod`, `EffectType`/`EffectParamA-C`, own arp | SysEx edit-buffer dumps via `send_fx_midi`; no host params |
| Yamaha DX7 | **Dexed** | DX7 .syx import path exists | **cartridge injection ignored** (probed: peak 0, state byte-identical) -> use the internal `fm_synth` instead | n/a | FM operators/envelopes via `fm_synth` | internal `fm_synth` params |

Grid-wide facts worth knowing before choosing a device:

- **Isolation and state.** The whole suite runs as isolated CLAPs (child process). An
  offline export instantiates a **fresh child** and restores `IDs::pluginState` into
  it, so an export matches what you audition **only if that plugin's
  `getStateInformation` round-trips its patch**. The JP-8080's 233-byte state does
  not (hear != export); the Nord's bank load does (its test asserts a render change).
  Measured: restoring a captured state is a no-op for the render (peak identical to
  16 digits), so the earlier "capture poisons the render" reading was wrong.
- **Engine hygiene (D-lite).** A capture that merely echoes the state an instance
  reported when it appeared is no longer persisted, and the deferred capture reports
  `captureStatus="unchanged"` instead of a fake `ok`. Nothing about that fixes
  hear-not-equal-export; it only stops meaningless state being written.
- **Params or nothing.** If a device exposes no host parameters (Vavra: zero), HDAW
  cannot automate or even verify it — only MIDI/SysEx can reach it.

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

### JP-8000 / JE8086 (parameter writes verified, dumps not)
- **Filter movement**: automate cutoff/resonance (params 0x29/0x2A in the dump layout;
  `set_fx_param` by index) instead of adding a plugin filter; pair with LFO2 ->
  FILTER (`Lfo2DepthSelect`) for hands-free motion.
- **Pitch drift**: LFO1 -> OSC1+2 with a tiny `OscLfo1Depth`; keep pitch modulation
  off the bass and stabs (guide §4D rule: no discord).
- **Width without a plugin chorus**: OSC1 = SUPER SAW with `Osc1Control2` (detune)
  plus the onboard MULTI-FX "SUPER CHORUS SLW"; automate `MultiEffectsLevel`.
- **Throws**: automate the onboard delay (type/time/feedback/level) instead of
  inserting a delay plugin; the guide's §4D param numbers (184-187) are the recipe.
- **Caveat**: this device's state does not round-trip, so verify by audition, not by
  export (see §1).

### Waldorf microQ / Vavra (no host params — matrix only)
- **In-device motion** (structure verified in `parameterDescriptions_mq.json`): the
  microQ gives every destination its own **source + amount** pair rather than a fixed
  LFO — `PitchModSrc`/`PitchModAmount`, `F1ModSource`/`F1CutoffMod`/`F1EnvMod`/`F1VelMod`,
  `F1PanModSource`/`F1PanMod` (and F2), so per-voice pitch drift, filter sweep and pan
  motion are all patch-level settings. Add `RingModLevel`/`RingModBalance` and
  `NoiseModeF1/F2` for texture. Use the **ModMatrix** page (and `leds.h`'s page list:
  Osc1-3, Filters1-2, Env1-4, LFOs) for the extra routing slots. Nothing here is
  HDAW-automatable, which is exactly why it belongs in the patch.
- **Onboard FX** (chorus / flanger / phaser / delay / reverb) belong to the patch: set
  them inside the patch, since HDAW cannot reach them.
- **Practical limit**: HDAW cannot automate or even observe this device; the only
  host lever is a SysEx dump, which is MEASURED NOT APPLYING (queued but state
  unchanged — §9). Use it for texture and audition, and prefer a device that
  exposes parameters when a part needs automation.

### Access Virus / OsTIrus, Osirus (params + CC)
- **Preset selection** is CC0+PC (`load_virus_preset`) — the cheapest way to switch
  character between sections (currently queues but does not change renders —
  finding F-A, see §9).
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
`set_fx_param`, which is the practical route for JP-8080 movement (dumps do not apply).

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

- **Xenia (Microwave XT)**: a patch pipeline like the others — its vocabulary is
  already available (`parameterDescriptions_xt.json`), only banks are missing.
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
| 2 | **Live parameter writes** | `set_fx_param {trackId, slotIndex, paramIndex, value}` | **JE8086 only** (461 params, measured). OsTIrus/Osirus, Vavra, Xenia and NodalRed2x all report `{"params":[]}` or the equivalent — measured for JE8086, OsTIrus and Vavra |
| 3 | **Automation / movement** — ramps, risers, throws, macro morphs on those parameters | track automation lanes and `apply_movement_plan` (macro events with start/end values) | **JE8086 only** (e.g. the guide's delay-throw automates its DelayLevel). For every other device, "movement over time" must come from CC ramps, patch/bank loads, or the device's own envelopes/LFOs |
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
  round-trips (Nord: verified via `load_nord_bank`; JE8086: state does not carry the
  patch, so verify by audition or by rendering the Nord-style path). For microQ there is
  no host-visible surface at all — its transitions must be baked into the patch or sent
  over the remote-control SysEx.
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
movement planes** (the modulation-first policy), which is the only way to use them on
the devices that publish no host parameters. Two honest limits: (a) the harvested values
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
(Phase D) has since RUN — je8086/xenia/nodalred2x **verified live**, vavra
**measured not applying**, virus **blocked by finding F-A**: see the status in
the apply-path table below.

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
| JE8086 | `set_fx_param` | parameter writes by **name → index** against `list_fx_params` — never by dump offset (the 461-param list is not the SysEx layout) and never as DT1 dumps (they do not apply); names need `je8086_param_index_map.json` (the plugin publishes display names like 'A FLT CUTOFF FREQ') | **VERIFIED live** — 46/46 writes of preset b44052f76c82a7a7, audible A/B; `list_fx_params` readback before/after + ear; **hear != export** (its 233-byte state does not carry the patch, §1) |
| NodalRed2x | `load_nord_bank` | load the preset as a bank/patch file; morph chains (`nord_morphs/`, 20 `.syx` written by `nord_dump.py`) load the same way | **VERIFIED AUDIBLE live** (morph-chain A/B; map 5,350 files / 353,100 values / 0 mismatches) — render assertion |
| Virus | `midi_cc_pc` | writer `virus_dump.py` is format-verified (TI 524 B / B/C 267 B; checksum rule cited + validated); `load_virus_preset` (CC0+PC) queues but does NOT change Osirus renders on the current build (preset-load ext absent — finding F-A); parameter path possible (3,086 exposed params) pending F-B (`set_fx_param` name resolution diverges from `list_fx_params`) | live A/B **BLOCKED by F-A** — the Osirus slot renders bit-identical digital silence under all programs/dumps (root-cause chain + probe results in `docs/plans/2026-09-16-matrix-preset-engine-fixes.md`) |
| Xenia | `sysex_edit_buffer_VERIFIED_AUDIBLE_2026-09-16` | single-dump SysEx to the **edit buffer** (bank 0x20) via `send_fx_midi` / `apply_matrix_preset` — dumps built by `xenia_dump.py`; morph chains performable; patch-level writes remain unproven | **VERIFIED AUDIBLE live** (pair A/B 2026-09-16; offset map 1,166,386 values, 0 mismatches); note `get_fx_capture_status` stays `unchanged` — the capture reads the program, not the edit buffer |
| Vavra | state blob or patch (no host params) | **NO working apply path** — single-dump injection MEASURED NOT APPLYING (2026-09-16: `queued=1` but `captureStatus=unchanged`, renders identical); `vavra_dump.py` + `vavra_morphs.json` remain blueprints; front-panel puppetry not pursued | none — documented emulator limitation (the child's state never moves); do not budget injection time here |

**D-lite round-trip check** (any state-blob route: Vavra, Xenia): apply →
`capture_fx_snapshot` → diff the captured state → render A/B. A capture that merely
echoes the boot state is not persisted (`captureStatus="unchanged"` — read that
field before assuming a capture happened), and a render peak identical to 16 digits
means the state was a no-op.

### Virus shortfall + unblock

Closed 2026-09-17 (R7): `virus_fx_pages.py` decodes the FX / modulation-matrix
pages of every single dump (byte-match stop-gate: 5,425 dumps, 1,782,572 values,
0 mismatches), the sidecars were re-swept to rev 2, and the re-run harvester
shipped the full **40-preset** virus sheet plus `virus_morphs.json` (per-step
SysEx, TI/BC model-tagged, written by `virus_dump.py`). What remains is live
audibility only: finding F-A (the silent Osirus slot) blocks the A/B — see the
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

## 6. Pipeline commands (one line each)

    py -3.14 timbre-lib/virus_patch.py  --sidecars "<Virus bank dir>"
    py -3.14 timbre-lib/nl2x_patch.py   --sidecars "D:\pdf\NL2x Banks"
    py -3.14 timbre-lib/je8086_patch.py --sidecars "D:\pdf\je8086" --explode
    py -3.14 timbre-lib/microq_patch.py --sidecars "D:\pdf\rhythm-lab.com_waldorf_micro_q"

Sidecars land next to the patch files as `<patch>.<engine>.json` (`.virus.`, `.nl2x.`,
`.je8086.`, `.vavra.`); `FileLibraryManager` ingests all four and feeds
`search_library` with engine, role verdict, description and tags. Register each library
folder as a **patch** library once and the presets become searchable.
