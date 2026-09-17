# Vavra (microQ) plugin-param → single-dump byte offset map

**Status: branch (a) — mapping rule VERIFIED** (static citation chain + empirical
validation against 305 real sidecar/syx pairs from
`/mnt/d/pdf/rhythm-lab.com_waldorf_micro_q/`).

Scope: map the mqJucePlugin (Vavra) parameter names in
`source/mqJucePlugin/parameterDescriptions_mq.json` (~823 entries) onto the
mqLib microQ **single-program dump** (392 bytes, header `F0 3E 10 00 10`,
name at 370, category at 386, params 7..369).

## The rule

    dump_byte_offset = 7 + linear_json_index
    value            = one 7-bit byte at that offset (no packing, no 14-bit pairs)

- `linear_json_index` is the JSON `"index"` field **exactly as written in the
  file** (0..382). It is not per-page: the parser splits it into (page =
  index >> 7, index & 0x7f) only for the wire format, and the receiver
  recombines the two bytes before adding the base offset. Single-program
  params occupy indices 0..362 (bytes 7..369); indices 363..378 are the 16
  name bytes and 379..382 the 4 category bytes.
- The dump is stored **verbatim**: the mqLib state IS the raw byte array;
  there is no convert/repack step in either direction.
- Applies to single programs only. Multi params (JSON `"page":100+`) address
  the 393-byte Multi dump and are out of scope here.

## Citation chain (gearmulator-2.2.9; retromulator-main identical for every cited file — `diff -rq` shows no divergence in `mqstate.*`, `mqmiditypes.h`, `wState.*`)

1. **Base offset 7** — `IdxSingleParamFirst = 7`, `source/mqLib/mqmiditypes.h:198`
   (also `IdxSingleBank = 5`, `IdxSingleProgram = 6`, lines 196–197).
2. **Raw-copy state** — `source/wLib/wState.h:12-22`: `convertTo()` is a plain
   `std::copy` of the sysex into the state array (both directions);
   `source/mqLib/mqstate.h:52-57`: `using Single = std::array<uint8_t, 392>`;
   `Dumps[]` Single row carries `firstParamIndex = IdxSingleParamFirst`
   (`mqstate.h:65`).
3. **Receiver addressing** — `source/mqLib/mqstate.cpp:416`:
   `i = dump.firstParamIndex + ((IndexH << 7) | IndexL)`; `modifySingle`
   writes the value byte straight into `*p` (`mqstate.cpp:343-350`).
   Param-change wire layout: cmd@4 (`IdxCommand = 4`,
   `synthLib/midiTypes.h:17`), buffer@5 (`IdxBuffer = 5`, line 20), IndexH@6,
   IndexL@7, value@8 (`mqmiditypes.h:200-204`).
4. **JSON index parsing** — `source/jucePluginLib/parameterdescriptions.cpp:350`
   `readPropertyInt("index")`; lines 352-355:
   `while(index >= 128) { index -= 128; ++d.page; }` — i.e. the JSON index is
   the **linear storage index**, split into (page, 7-bit) only for
   transmission. Explicit `"page"` in the JSON exists only for multi params
   (100+).
5. **Plugin send path** — `source/mqJucePlugin/mqController.cpp:477-481`:
   single params send `Page=desc.page`, `ParameterIndex=desc.index`,
   `ParameterValue=v` as `SingleParameterChange` (multi path with
   `desc.page >= 100` at lines 449-466).
6. **Anchors**
   - Name: `mq::g_singleNameOffset = 370` — `source/mqLib/mqmiditypes.h:76`
     (the `q::` variant 371/387 at lines 84-88 applies only to the 393-byte
     `SingleQ` dump of the big Q). The JSON itself agrees: `Name00..Name15`
     at indices 363..378 → bytes 370..385. Plugin reader:
     `source/mqJucePlugin/mqPatchBrowser.cpp:43` reads the name at
     `dumpMq.firstParamIndex + 363` (= 7 + 363 = 370).
   - Category: `mq::g_categoryOffset = 386` — `mqmiditypes.h:78`; JSON
     `Category00..03` at indices 379..382 → bytes 386..389;
     `mqPatchBrowser.cpp:44`.
   - Version: `isValid(Single)` requires `dump[7] == 1` — `mqstate.h:174`;
     JSON index 0 is `"Version", min 1, max 1`.
7. **Full layout** — `F0 3E 10 00 10` (hdr) | bank@5 | prog@6 | params 7..369
   (indices 0..362) | name 370..385 | category 386..389 | checksum@390 (low 7
   bits of the sum over bytes 4..389 — `mqstate.cpp updateChecksum`, ~line
   100) | `F7`@391. Total 392.

## Empirical validation (305 sidecar/syx pairs)

For all 305 `*.vavra.json` sidecars and their raw `.syx` dumps under
`/mnt/d/pdf/rhythm-lab.com_waldorf_micro_q/*/`:

- 305/305: header `F0 3E _ 00 10`, `dump[7] == 1` (Version), name@370 equals
  the sidecar `"name"`, category@386 equals the sidecar `"category"`.
- Sidecar `params` keys are byte offsets 7..369; **0 mismatches** between
  sidecar values and raw `.syx` bytes across all 305 × 363 bytes.
- Range spot-checks vs JSON `min`/`max` (all OK): O1/O2/O3 `FmSource`
  0..4 (≤11), O1/O2/O3 `PwmSource` ≤10 (≤13), O1/O2/O3 `Shape` ≤6,
  `PitchModSrc` 0..12 (≤13), `NoiseModeF1/F2` 0..3, `F1Type` 0..10,
  `F1ModSource` 0..11 (≤13), `FX1Type` 0..5 (≤6), `FX2Type` 0..8 (≤10),
  `PitchModAmount` / `RingModLevel` / `RingModBalance` full 0..127.
- Known noise (does not affect the rule): 12/305 files (a few CJ Bass/Lead/
  Atmo patches) carry values >13 at bytes 116/121 (`F2ModSource` /
  `F2PanModSource`) — junk the real device tolerates. Aliased FX bytes
  range-check only under the active FX type (e.g. idx 154 is
  `Fx2FlangerPolarity` 0..1 for flanger but `Fx2VocoderAttack` 0..127 for
  vocoder).

## FX-block aliasing (matters for naming matrix/FX presets)

Bytes 137..150 (FX1) and 153..166 (FX2) hold **one** byte whose meaning
depends on the selected FX type (`FX1Type`@135 / `FX2Type`@151). The JSON
lists one alias per type, all sharing the same index (first = primary name
used in `vavra_offset_map.json`):

| byte | idx | aliases |
|------|-----|---------|
| 135 | 128 | FX1Type |
| 136 | 129 | FX1Mix |
| 137 | 130 | Fx1ChorusSpeed / Fx1FlangerSpeed / Fx1PhaserSpeed / FiveFX1ChorusSpeed / Fx1VocoderBands / Fx1ReverbSize |
| 138 | 131 | Fx1ChorusDepth / Fx1FlangerDepth / Fx1PhaserDepth / Fx1OverdriveDrive / FiveFX1ChorusDepth / Fx1VocoderAnalysisSignal / Fx1ReverbShape |
| 139 | 132 | Fx1OverdrivePostGain / FiveFX1Delay / Fx1VocoderAnalysisFreqLow / Fx1ReverbDecay |
| 140 | 133 | Fx1ChorusDelay / FiveFX1ChorusDelayL / Fx1VocoderAnalysisFreqHigh / Fx1ReverbPreDelay |
| 141 | 134 | Fx1FlangerFeedback / Fx1PhaserFeedback / FiveFX1SampleAndHold / Fx1VocoderOffsetS |
| 142 | 135 | Fx1PhaserCenter / Fx1OverdriveCutoff / FiveFX1Overdrive / Fx1VocoderOffsetHi / Fx1ReverbLowpass |
| 143 | 136 | Fx1PhaserSpacing / FiveFX1RingModSource / Fx1VocoderBandwidth / Fx1ReverbHighpass |
| 144 | 137 | FiveFX1RingModLevel / Fx1VocoderResonance / Fx1ReverbDiffusion |
| 145 | 138 | Fx1FlangerPolarity / Fx1PhaserPolarity / Fx1VocoderAttack / Fx1ReverbDamping |
| 146 | 139 | Fx1VocoderDecay |
| 147 | 140 | Fx1VocoderEQLevelLow |
| 148 | 141 | Fx1VocoderEQBandMid |
| 149 | 142 | Fx1VocoderEQLevelMid |
| 150 | 143 | Fx1VocoderEQLevelHigh |
| 151 | 144 | FX2Type |
| 152 | 145 | FX2Mix |
| 153 | 146 | Fx2ChorusSpeed / Fx2FlangerSpeed / Fx2PhaserSpeed / FiveFX2ChorusSpeed / Fx2VocoderBands / Fx2ReverbSize / Fx251DelayDelay / Fx251ClockDelayDelay |
| 154 | 147 | Fx2ChorusDepth / Fx2FlangerDepth / Fx2PhaserDepth / Fx2OverdriveDrive / FiveFX2ChorusDepth / Fx2VocoderAnalysisSignal / Fx2ReverbShape / Fx251DelayFeedback |
| 155 | 148 | Fx2OverdrivePostGain / FiveFX2Delay / Fx2VocoderAnalysisFreqLow / Fx2ReverbDecay / Fx251DelayLfeLP |
| 156 | 149 | Fx2ChorusDelay / FiveFX2ChorusDelayL / Fx2VocoderAnalysisFreqHigh / Fx2ReverbPreDelay / Fx251DelayInputHP |
| 157 | 150 | Fx2FlangerFeedback / Fx2PhaserFeedback / Fx2DelayFeedback / FiveFX2SampleAndHold / Fx2VocoderOffsetS / Fx251DelayDelayML |
| 158 | 151 | Fx2PhaserCenter / Fx2DelayCutoff / Fx2OverdriveCutoff / FiveFX2Overdrive / Fx2VocoderOffsetHi / Fx2ReverbLowpass / Fx251DelayFSLVolume |
| 159 | 152 | Fx2PhaserSpacing / FiveFX2RingModSource / Fx2VocoderBandwidth / Fx2ReverbHighpass / Fx251DelayDelayMR |
| 160 | 153 | FiveFX2RingModLevel / Fx2VocoderResonance / Fx2ReverbDiffusion / Fx251DelayFSRVolume |
| 161 | 154 | Fx2FlangerPolarity / Fx2PhaserPolarity / Fx2DelayPolarity / Fx2VocoderAttack / Fx2ReverbDamping / Fx251DelayDelayS2L |
| 162 | 155 | Fx2DelayAutopan / Fx2VocoderDecay / Fx251DelayCenterSVolume |
| 163 | 156 | Fx2VocoderEQLevelLow / Fx251DelayDelayS1L |
| 164 | 157 | Fx2VocoderEQBandMid / Fx251DelayRearSLVolume |
| 165 | 158 | Fx2VocoderEQLevelMid / Fx251DelayDelayS1R |
| 166 | 159 | Fx2VocoderEQLevelHigh / Fx251DelayRearSRVolume |

## Mapped params (vavra_offset_map.json — 86 entries)

Keys are **dump byte offsets** (same 7-based convention as the sidecars'
`params` keys); values are the primary (first-file-order) name.

| byte | idx | param | min | max |
|------|-----|-------|-----|-----|
| 13 | 6 | O1FmSource | 0 | 11 |
| 14 | 7 | O1FmAmount | - | - |
| 17 | 10 | O1PwmSource | 0 | 13 |
| 18 | 11 | O1Pwm | - | - |
| 29 | 22 | O2FmSource | 0 | 11 |
| 30 | 23 | O2FmAmount | - | - |
| 33 | 26 | O2PwmSource | 0 | 13 |
| 34 | 27 | O2Pwm | - | - |
| 45 | 38 | O3FmSource | 0 | 11 |
| 46 | 39 | O3FmAmount | - | - |
| 49 | 42 | O3PwmSource | 0 | 13 |
| 50 | 43 | O3Pwm | - | - |
| 57 | 50 | PitchModSrc | 0 | 13 |
| 58 | 51 | PitchModAmount | - | - |
| 68 | 61 | O1Level | - | - |
| 69 | 62 | O1Balance | - | - |
| 70 | 63 | O2Level | - | - |
| 71 | 64 | O2Balance | - | - |
| 72 | 65 | O3Level | - | - |
| 73 | 66 | O3Balance | - | - |
| 74 | 67 | NoiseLevel | - | - |
| 75 | 68 | NoiseBalance | - | - |
| 78 | 71 | RingModLevel | - | - |
| 79 | 72 | RingModBalance | - | - |
| 82 | 75 | NoiseModeF1 | 0 | 3 |
| 83 | 76 | NoiseModeF2 | 0 | 3 |
| 84 | 77 | F1Type | 0 | 10 |
| 85 | 78 | F1Cutoff | - | - |
| 87 | 80 | F1Resonance | - | - |
| 88 | 81 | F1Drive | - | - |
| 93 | 86 | F1KeyTrack | - | - |
| 94 | 87 | F1EnvMod | - | - |
| 95 | 88 | F1VelMod | - | - |
| 96 | 89 | F1ModSource | 0 | 13 |
| 97 | 90 | F1CutoffMod | - | - |
| 98 | 91 | F1FmSource | 0 | 11 |
| 99 | 92 | F1FmAmount | - | - |
| 100 | 93 | F1Pan | - | - |
| 101 | 94 | F1PanModSource | 0 | 13 |
| 102 | 95 | F1PanMod | - | - |
| 104 | 97 | F2Type | 0 | 10 |
| 105 | 98 | F2Cutoff | - | - |
| 107 | 100 | F2Resonance | - | - |
| 108 | 101 | F2Drive | - | - |
| 113 | 106 | F2KeyTrack | - | - |
| 114 | 107 | F2EnvMod | - | - |
| 115 | 108 | F2VelMod | - | - |
| 116 | 109 | F2ModSource | 0 | 13 |
| 117 | 110 | F2CutoffMod | - | - |
| 118 | 111 | F2FmSource | 0 | 11 |
| 119 | 112 | F2FmAmount | - | - |
| 120 | 113 | F2Pan | - | - |
| 121 | 114 | F2PanModSource | 0 | 13 |
| 122 | 115 | F2PanMod | - | - |
| 135 | 128 | FX1Type | 0 | 6 |
| 136 | 129 | FX1Mix | - | - |
| 137 | 130 | Fx1ChorusSpeed | - | - |
| 138 | 131 | Fx1ChorusDepth | - | - |
| 139 | 132 | Fx1OverdrivePostGain | - | - |
| 140 | 133 | Fx1ChorusDelay | - | - |
| 141 | 134 | Fx1FlangerFeedback | - | - |
| 142 | 135 | Fx1PhaserCenter | - | - |
| 143 | 136 | Fx1PhaserSpacing | - | - |
| 144 | 137 | FiveFX1RingModLevel | - | - |
| 145 | 138 | Fx1FlangerPolarity | 0 | 1 |
| 146 | 139 | Fx1VocoderDecay | - | - |
| 147 | 140 | Fx1VocoderEQLevelLow | - | - |
| 148 | 141 | Fx1VocoderEQBandMid | 0 | 24 |
| 149 | 142 | Fx1VocoderEQLevelMid | - | - |
| 150 | 143 | Fx1VocoderEQLevelHigh | - | - |
| 151 | 144 | FX2Type | 0 | 10 |
| 152 | 145 | FX2Mix | - | - |
| 153 | 146 | Fx2ChorusSpeed | - | - |
| 154 | 147 | Fx2ChorusDepth | - | - |
| 155 | 148 | Fx2OverdrivePostGain | - | - |
| 156 | 149 | Fx2ChorusDelay | - | - |
| 157 | 150 | Fx2FlangerFeedback | - | - |
| 158 | 151 | Fx2PhaserCenter | - | - |
| 159 | 152 | Fx2PhaserSpacing | - | - |
| 160 | 153 | FiveFX2RingModLevel | - | - |
| 161 | 154 | Fx2FlangerPolarity | 0 | 1 |
| 162 | 155 | Fx2DelayAutopan | 0 | 1 |
| 163 | 156 | Fx2VocoderEQLevelLow | - | - |
| 164 | 157 | Fx2VocoderEQBandMid | 0 | 24 |
| 165 | 158 | Fx2VocoderEQLevelMid | - | - |
| 166 | 159 | Fx2VocoderEQLevelHigh | - | - |

(`min`/`max` `-` = full 0..127 byte range; reserved single indices not named
by the JSON — 14-16, 30-32, 44-49, 52-60, 69-70, 73-74, 79, 82-85, 99,
102-105, 124-127, etc. — map 1:1 to the same-numbered dump bytes and simply
carry no name.)

## Validation recipe

Quick artifact check (already executed, passes):

    python3 -c 'import json;d=json.load(open("timbre-lib/matrix_presets/vavra_offset_map.json"));print(len(d),d["135"],d["151"],d["96"])'
    # 86 FX1Type FX2Type F1ModSource

Full anchor check against any sidecar + its `.syx`:

    import json, re
    sc = json.load(open(sidecar_path)); dump = open(syx_path,'rb').read()
    assert len(dump) == 392 and dump[0] == 0xF0 and dump[1] == 0x3E
    assert dump[7] == 1                          # Version, idx 0
    name = dump[370:386].decode('latin1')        # == sc['name']
    cat  = dump[386:390].decode('latin1').rstrip()  # == sc['category']
    assert all(dump[int(o)] == v for o, v in sc['params'].items())
    # rule: param with JSON index N lives at dump byte 7+N

## Search notes (for the record)

- `mqLib` has **no** sound-parameter name table (no `SingleParameter` enum,
  unlike `xtLib`); the only name source is the plugin's
  `parameterDescriptions_mq.json` — hence this mapping.
- `retromulator-main` has no `mqJucePlugin`; plugin-side citations are from
  gearmulator-2.2.9. All mqLib/wLib files cited above are byte-identical
  between the two trees (`diff -rq`), so either copy can be used.
- No engine/DSP files were touched; deliverables live only in
  `timbre-lib/matrix_presets/`.
