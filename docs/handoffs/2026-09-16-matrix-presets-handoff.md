EXECUTED 2026-09-16/17 — see docs/plans/2026-09-16-matrix-presets.md and docs/plans/2026-09-16-matrix-preset-engine-fixes.md (all deliverables done; virus R7 closed; live apply verified for je8086/xenia/nord; vavra/virus injectability documented as emulator limitations).

# Handoff: per-core-plugin MATRIX PRESETS (harvest → curate → apply on the same plugin)

Written 2026-09-16 to be executed from a **fresh context**. Self-contained: everything
needed (paths, evidence, measured facts, gates, traps) is below. Read
`docs/hardware-va-suite.md` first — it is the reference this builds on.

## Goal (what the user asked for)

For **each core synth plugin** (the gearmulator CLAPs), harvest the **FX / modulation-matrix
configurations that already exist inside its own patch corpus**, turn them into **named matrix
presets**, and apply them **back onto that same plugin**. Then wire the FX workflow so that
when FX work is done for a core plugin, the agent **checks for that plugin's matrix presets
first** (before inventing chains or reaching for plugins).

Explicitly **not** the same as the existing harvester: `timbre-lib/harvest_fx_presets.py`
aggregates value *distributions* for re-creation with HDAW's internal FX. This task is about
concrete, named, **device-applicable** configs (per plugin), e.g. "je8086: trance delay
throw", "microq: phaser + LFO->filter swell", "virus: vocoder + env->FM".

## Starting inventory (measured 2026-09-16 — do not re-derive)

| Device (CLAP) | Patch library + sidecars | Patches | Host params | Matrix/FX vocabulary source |
| --- | --- | --- | --- | --- |
| JP-8080 / **JE8086** | `D:/pdf/je8086` -> `<bank>.je8086.json` + exploded per-patch tree | 2676 usable (4983 entries) | **461** (the only one) | `ronaldo/je8086/jeJucePlugin/parameterDescriptions_je.json` |
| microQ / **Vavra** | `D:/pdf/rhythm-lab.com_waldorf_micro_q` -> `<patch>.vavra.json` | 528 | **0** | `mqJucePlugin/parameterDescriptions_mq.json` (866 names) |
| Microwave XT / **Xenia** | `D:/pdf/microwave` -> `<bank>.xenia.json` | 1791 | **0** | `xtJucePlugin/parameterDescriptions_xt.json` |
| Virus / **OsTIrus, Osirus** | virus sidecars (`.virus.json`) | shipped earlier | **0** (measured) | `osTIrusJucePlugin/parameterDescriptions_TI.json`, `osirusJucePlugin/parameterDescriptions_C.json` |
| Nord Lead 2x / **NodalRed2x** | `D:/pdf/NL2x Banks` -> `<patch>.nl2x.json` | 6841 | **0** | `nord/n2x/n2xJucePlugin/parameterDescriptions_n2x.json` |

Emulator sources are at `/mnt/d/pdf/gearmulator-2.2.9/source/` (also a retromulator-main
copy that has files the gearmulator copy lacks, e.g. `mqLib/mqsysexremotecontrol.cpp`).
The vocabularies are JSON5-ish (comments/trailing commas) — some resist a strict parse, so
pair `"index"` and `"name"` with a regex (that is what the existing harvester does).

## What a "matrix preset" is per engine (from the vocabularies — verified)

- **JE8086**: LFO1/LFO2 (waveform, rate, fade, `Lfo2DepthSelect`), per-target depths
  (`AmpLfo1Depth`, `AmpLfo2Depth`, `FilterLfo1Depth`, `FilterLfo2Depth`, `PitchLfo2Depth`,
  `OscLfo1Depth`, `Osc1Control2LFO1Depth`, `Osc2Control2LFO1Depth`), envelope depths and ADSR
  (`PitchEnvelope*`, `FilterEnvelope*`, `AmpEnvelope*`), routing switch
  `Lfo1AndEnvelopeDestination`, `CrossModulationDepth`, `RingModulatorSwitch`,
  `AutoPanManualPanSwitch`, `CutoffFrequency`/`Resonance`/`CutoffSlope`, tone control, and the
  FX block (`MultiEffectsType`, `MultiEffectsLevel`, `DelayType`, `DelayTime`,
  `DelayFeedback`, `DelayLevel`, `ChorusType`, `ChorusLevel`).
- **Vavra (microQ)**: FX1/FX2 (`FX1Type`, `FX2Type`, `FX1Mix`, `FX2Mix` + per-slot
  chorus/phaser/delay sub-params) and **per-destination source+amount pairs**:
  `PitchModSrc`/`PitchModAmount`, `F1ModSource`/`F1CutoffMod`/`F1EnvMod`/`F1VelMod`/`F1FmSource`/`F1PanModSource`/`F1PanMod`,
  `F2`* likewise, `O1/O2/O3FmSource`, `O1/O2/O3PwmSource`, `RingModLevel`/`RingModBalance`,
  `NoiseModeF1/F2`.
- **Xenia (Microwave XT)**: `EffectType` + `EffectParamA/B/C`, `DelayTime`, `ChorusEnabled`,
  `MixRingMod`, `Pan`/`PanKeytrack`/`DePan`, `ModDelaySource`/`ModDelayTime`,
  `Lfo1Delay`/`Lfo2Delay`, wave-envelope amounts (`W1/W2EnvAmount`, `W1/W2EnvVelAmount`),
  filter modulation (`F1EnvAmount`, `F1EnvVelAmount`, `F1Cutoff`, `F1Resonance`).
- **Virus**: `Chorus/Type`, `Ringmodulator Volume`, the Vocoder block
  (`Vocoder/Carrier|Modulator Center Frequency`, `Frequency Offset`, `Q Factor`,
  `Frequency Spread`, `Bands`, `Attack`/`Release`, `Spectral Balance`/`Balance`), the matrix
  (`Assign1 Source` = **page 113 index 64**, `Assign1 Destination` = 65, `Assign2 Source` = 67,
  `Assign3 Source` = 72, ...), `Lfo1/2/3 Mode`, `Lfo3 Destination`, `LfoN Env Mode`.
- **Nord (NodalRed2x)**: `FilterEnvAmount` + per-stage sensitivities
  (`FilterEnvA/D/S/R Sens`), `ModEnvA`/`ModEnvD`/`ModEnvLevel`, `Lfo1Rate`/`Lfo1Level`,
  `Lfo2Rate`.

## How a preset can be APPLIED per engine (the part that decides the design)

| Engine | Apply path | Status |
| --- | --- | --- |
| JE8086 | `set_fx_param` by **name -> index** (461 params) | works today (verified). **Never map by dump offset** — the plugin's list is not the SysEx layout |
| Nord (NodalRed2x) | `load_nord_bank` (a preset = a bank/patch file) | **verified to change the render** |
| Virus | `load_virus_preset` (CC0+PC) + CC via `send_fx_midi` for matrix routings | CC/PC verified; SysEx unverified |
| Vavra (microQ) | patch-level (bake into the patch) OR front-panel puppetry `EmuButtons` (`52 <buttonIdx> <state>`) / `EmuRotaries` (`53 <encoderIdx> <amount+64>`) after the Waldorf header `F0 3E 10 00 <cmd>` | puppetry verified **in source** only, not from HDAW. There is **no parameter write** (`SetParam` is ignored — state byte-identical) |
| Xenia | patch-level, or SysEx | unverified |

**Recommended storage/apply mechanism — reuse what exists:** HDAW already has FX snapshots
and chain files (`capture_fx_snapshot`, `save_fx_chain`, `swap_fx_snapshot`,
`load_plugin_preset` / `list_plugin_presets` / `search_plugin_presets`). A matrix preset can
therefore be stored as **the plugin state blob captured after applying it**, which gives
undo, save/load and an existing apply path for the no-param devices. **Caveat to verify per
engine:** a state only round-trips if the plugin's `getStateInformation` carries the patch —
measured: the JP-8080's 233-byte state does **not** (hear != export), the Nord's bank load
does. Use the D-lite methodology to check: apply → diff the captured state → render A/B
(peak identical to 16 digits means the state is a no-op). *(Superseded for JE8086
2026-09-20/21: its dumps now persist via the `IDs::presetSysex` replay and exports
reproduce the patch — see `docs/handoffs/2026-09-20-je8086-userpatch-dt1-fix.md`. The
general rule stands: prefer the dump-replay persistence wherever `getStateInformation`
does not carry the change.)*

## Deliverables

1. `timbre-lib/harvest_matrix_presets.py` — per engine: extract the matrix/FX parameter tuples
   from the sidecars (using the vocabularies as the name map where the sidecars store numeric
   indices), de-duplicate/normalise, cluster by role, and emit **one file per engine**:
   `timbre-lib/matrix_presets/<engine>.json` with schema `hdaw.matrix.preset.v1`:
   `{"engine":"je8086","presets":[{"id","name","role","params":{...},"appliesVia":"set_fx_param","examples":[patches],"evidence":"..."}]}`
2. **A curated shortlist per engine** (10-20 presets each) with names a human would use,
   derived from the clusters and checked by ear (the numbers tell you which configs are
   common; only listening tells you which are good).
3. **An apply/verify path per engine** (see the table) — verified, not assumed:
   - JE8086: name->index apply + a render/audition check.
   - Nord: preset files loadable with `load_nord_bank` + a render assertion.
   - Virus: CC/PC + CC matrix test.
   - Vavra/Xenia: state-blob (snapshot) route if the round-trip verifies, else patch-level only.
4. **The FX workflow hook**: when FX work starts for a core plugin, look up that plugin's
   matrix presets **first**. Document it in `docs/skills/psy-song-session/roles/fx-automation-engineer.md`
   and `docs/hardware-va-suite.md`; optionally add an MCP tool
   (`list_matrix_presets {engine|pluginId}` / `apply_matrix_preset`) so it is mechanical, not
   a doc-only convention.
5. **microQ naming unblock**: the Vavra sidecars store **dump offsets** (bytes 0..369), not
   device parameter indices, which is why the existing harvester produces no named vavra
   recipes. Map the dump layout onto the device vocabulary (the 866-name vocabulary is already
   harvested; the dump's own layout is documented in the JP-8080-style decoder work) so microQ
   matrix presets can be named too.

## Success gates

- [ ] G1: each engine yields >= 10 presets with an evidence trail (counts + example patches);
      no preset invented without a corpus citation.
- [ ] G2: every preset in a shipped shortlist **applies** on its engine and is verified —
      JE8086 by parameter readback (`list_fx_params` before/after) and ear; Nord by render
      assertion; Virus by CC/PC + state change; Vavra/Xenia by the state-blob route **or**
      explicitly marked patch-level-only.
- [ ] G3: the FX workflow doc names the lookup step, and (if the MCP tool is added) a gtest
      asserts the tool registers and returns the harvested presets for an engine.
- [ ] G4: no engine/DSP change — this is tooling + MCP/ValueTree wiring only (per AGENTS.md
      the sound-engine stability rule: anything touching processBlock / DSP chains / render /
      playback needs a discussion first; nothing here should).
- [ ] G5: microQ mapping either solved (G2 satisfied for vavra) or explicitly deferred with
      the reason recorded.

## Traps carried forward (all measured this session — do not rediscover them)

1. **4 of the 5 engines publish no host parameters** (OsTIrus and Vavra both measured at
   `{"params":[]}`). Parameter-level application is JE8086-only; the other engines need
   snapshot/CC/patch/SysEx paths.
2. ~~**JP-8080 DT1 patch dumps are never applied** by the emulation (its patch State is not on
   the live MIDI path); parameter writes are.~~ **SUPERSEDED 2026-09-20:** the root cause was
   the empty `case AddressArea::UserPatch`; the wrapper now retargets host DT1s to the temp
   performance, so dumps apply (see `docs/handoffs/2026-09-20-je8086-userpatch-dt1-fix.md`).
3. **Map by NAME, never by dump offset** for JE8086 (the plugin's 461-param list is not the
   SysEx patch layout).
4. ~~**hear != export** for JE8086 (its state does not carry the patch). Verify by audition, and
   do not promise a rendered result from a curated JE8086 patch.~~ **SUPERSEDED 2026-09-20/21:**
   exports do carry the patch — the raw DT1 dumps persist in `IDs::presetSysex` and are replayed
   into fresh children (a rebuilt child reproduces the patch to `|Δrms| 5.8e-08`). Audition-only
   verification is no longer required.
5. **A state capture that merely echoes the boot state is no longer persisted** (D-lite), and
   the deferred capture reports `captureStatus="unchanged"` — read that field before assuming a
   capture happened.
6. **microQ accepts only button/encoder events**, not parameter writes; its FX are patch
   content.
7. **Isolation**: every core plugin runs as an isolated child; a bank/state apply needs the
   child booted (the loaders already pace the delivery).
8. **Naming/curation is not automatic**: cluster *frequency* is not musical quality — the
   shortlist needs a listening pass.

## Pointers (where the evidence lives)

- `docs/hardware-va-suite.md` — capability matrix (§1), modulation-first policy (§2),
  per-device FX recipes (§3), harvested modulation matrices (§4), operations catalogue (§7),
  the existing recipe harvester (§7b), pipeline commands (§6).
- `docs/plans/2026-09-16-{je8086,microq,microwave}-preset-pipeline.md` — pipeline plans + the
  bugs each one caught.
- `timbre-lib/{je8086,microq,microwave,nl2x,virus}_patch.py` + their surveys —
  the decoders, the sidecars, the role shortlists.
- `timbre-lib/harvest_fx_presets.py` + `timbre-lib/harvested_fx_presets.json` — the existing
  distribution harvester (11,111 sidecars, 4 engines) and its output.
- `docs/handoffs/2026-09-16-terra-signal-reimagined.md` — a worked example of applying device
  patches + movement plans to a real track (and the mix lessons).
- Emulator sources: `/mnt/d/pdf/gearmulator-2.2.9/source/` (vocabularies + device protocols),
  `/mnt/d/pdf/retromulator-main/source/` (the fuller microQ/Waldorf copies).
