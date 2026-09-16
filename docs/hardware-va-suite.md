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
| Waldorf microQ | **Vavra** | 528 single-sound dumps; `timbre-lib/microq_patch.py` -> `<patch>.vavra.json` (528 sidecars, verify 528/0) | Injection queues and captures (440 B state) but **no audible change measured** and no way to observe it | **0** (`{"params":[]}`) | 3 oscillators, 2 filters, 4 envelopes, LFOs and a **ModMatrix** page (`mqLib/leds.h` pinpoints the pages: Osc1-3, Filters1-2, Env1-4, LFOs, ModMatrix). Verified structure from `mqJucePlugin/parameterDescriptions_mq.json`: **per-destination source+amount pairs** — `PitchModSrc`/`PitchModAmount`, `F1ModSource`/`F1CutoffMod`/`F1EnvMod`/`F1VelMod`/`F1PanModSource`/`F1PanMod` (and F2), plus `RingModLevel`/`RingModBalance`, `NoiseModeF1/F2`, `GlideMode`, `VoiceMode`. Onboard FX pages exist in the same file | `send_fx_midi` SysEx only (unverified); no params to automate |
| Access Virus | **OsTIrus / Osirus** | `timbre-lib/virus_patch.py` -> `<patch>.virus.json` + `virus_survey.json` (shipped earlier) | `load_virus_preset` (CC0 bank + PC) works; SysEx unverified | exposed (the OsTIrus probe watches its param cache) | modulation vocabulary available via `parameterDescriptions_TI.json` / `_C.json` (matrix + LFO/FX/unison parameters) — **not yet enumerated** in a matrix table | `set_fx_param`, `send_fx_midi` CC/PC (works) |
| Clavia Nord Lead 2x | **NodalRed2x** | `timbre-lib/nl2x_patch.py` -> `<patch>.nl2x.json` (6841 sidecars) | `load_nord_bank` **works and changes the render** (asserted by `FxMidiInjection.NordBankLoadChangesNodalRed2xRender`) — the one verified bank loader | exposed | MOD ENV + LFO2 with mod-wheel/velocity amounts (`parameterDescriptions_n2x.json`, id `mod`); NO onboard FX | `load_nord_bank`, `set_fx_param`, CC/PC |
| Waldorf Microwave XT | **Xenia** | none yet (banks not in the library) | ROM preset only | exposed (untested here) | matrix + its own FX | `load_virus_preset` CC/PC |
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
  host lever is a SysEx dump, which is unverified. Use it for texture and audition,
  and prefer a device that exposes parameters when a part needs automation.

### Access Virus / OsTIrus, Osirus (params + CC)
- **Preset selection** is CC0+PC (`load_virus_preset`) — the cheapest way to switch
  character between sections.
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

## 4. Pipeline commands (one line each)

    py -3.14 timbre-lib/virus_patch.py  --sidecars "<Virus bank dir>"
    py -3.14 timbre-lib/nl2x_patch.py   --sidecars "D:\pdf\NL2x Banks"
    py -3.14 timbre-lib/je8086_patch.py --sidecars "D:\pdf\je8086" --explode
    py -3.14 timbre-lib/microq_patch.py --sidecars "D:\pdf\rhythm-lab.com_waldorf_micro_q"

Sidecars land next to the patch files as `<patch>.<engine>.json` (`.virus.`, `.nl2x.`,
`.je8086.`, `.vavra.`); `FileLibraryManager` ingests all four and feeds
`search_library` with engine, role verdict, description and tags. Register each library
folder as a **patch** library once and the presets become searchable.
