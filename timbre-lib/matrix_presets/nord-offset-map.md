# Nord Lead 2x (NodalRed2x) plugin-param -> single-dump byte offset map

**Status: branch (a) -- mapping rule VERIFIED** (static citation chain +
empirical validation against the full sidecar-carrying corpus under
/mnt/d/pdf/NL2x Banks/: **5,350 files across 19 banks, 353,100 sidecar
param values byte-matched, 0 mismatches**; independent strict re-parse,
0 framing failures, 0 program-field mismatches -- gate floor was >=200
patches across >=10 banks).  Mirror of the verified xenia/microQ pipeline
(matrix_presets/xenia-offset-map.md, vavra-offset-map.md).

Scope: map the nodalred2x matrix-sheet parameter names
(matrix_presets/nodalred2x.json, 40 presets, 17 params) onto the Clavia
Nord Lead 2x **single patch dump** (139 bytes, header `F0 33 <dev> 04`),
and document the writer/morph tooling in `timbre-lib/nord_dump.py` (the
inverse of the unchanged decoder `timbre-lib/nl2x_patch.py`).

## The rule

    dump_byte_offset = 6 + 2*i          (i = SingleParam storage index)
    value            = (dump[off] & 0xF) | (dump[off+1] << 4)

- 66 params, two nibbles each: LOW nibble first, HIGH nibble second
  (little-endian nibble order).  The nibble pair carries **8 bits**
  (0..255), not 7 -- real files exceed 127 (see range caveat).
- Equivalently, in the decoder's unpacked-payload space (dump without the
  leading F0), `payload_offset = 5 + 2*i` -- that is the `offset` recorded
  in nord_offset_map.json; `dumpOffset` is the 6+2i full-syx offset.
- The storage index i is simultaneously the plugin-parameter index for all
  params EXCEPT the packed byte-52 trio (see below).

## Citation chain (gearmulator-2.2.9; the tree HDAW's NodalRed2x CLAP ships from)

1. **Framing constants** -- n2xLib/n2xmiditypes.h:163 `g_sysexHeaderSize = 6`
   (F0, IdClavia, IdDevice, IdN2x, MsgType, MsgSpec), :167
   `g_singleDataSize = 66 * 2`, :171-174 dump sizes 139/149 (single) and
   715/725 (multi); IdClavia=0x33 / IdN2X=0x04 / DefaultDeviceId=0xf (:9);
   `SingleDumpBankEditBuffer = 0x00`, banks A-D = 0x01..0x04 (:11).
2. **Offset rule** -- n2xLib/n2xstate.cpp:501-503
   `getOffsetInSingleDump(param) { return g_sysexHeaderSize + (param<<1); }`
   = 6 + 2i; the write path lands there via changeSingleParameter
   (:437-440).  Same formula for multi params (:505-507).
3. **Nibble order** -- n2xstate.cpp:448-461 createDefaultSingle writes
   `_single[o++] = b & 0xf; _single[o++] = (b>>4) & 0xf` starting at
   IdxMsgSpec+1 (byte 6): lo nibble first, and 66 params starting at dump
   byte 6.  Decoder mirror: nl2x_patch.py:247-250 `_decode_nibbles`, used
   at payload offset `PAYLOAD_HEADER_SIZE = 5` (:59, :262-266).
4. **Receiver addressing** -- n2xstate.cpp:384-423: CC in ->
   g_controllerMap -> SingleParam -> `getOffsetInSingleDump` ->
   packNibbles; the dump IS the state (no conversion layer).
5. **Param names/order** -- three agreeing sources:
   (a) SingleParam enum (n2xmiditypes.h:34-72, indices 0..24 + 50..65);
   (b) parameterDescriptions_n2x.json indices 0..65
   (n2xJucePlugin/, line 18 O2Pitch ... line 88 Lfo2Dest);
   (c) the g_singleDefault comment table (n2xstate.cpp:17-89) in the same
   order.  Indices 25..49 are unnamed in the enum but named
   `*Sens` by (b) and (c) -- the per-destination mod-matrix amounts
   (min -128, toText signed256, isBipolar; line 45 O2PitchSens ...).
6. **Plugin-index exception** -- n2xJucePlugin/n2xController.cpp:356 casts
   `getDescription().index` straight to SingleParam for every param EXCEPT
   Sync/RingMod/Distortion (:348-355).  In the descriptions JSON Sync and
   RingMod SHARE index 52 (:73-74) and Distortion carries the plugin-space
   index 100 (:75) -- its STORAGE index is SingleParam::Distortion = 52
   (n2xmiditypes.h:70).
7. **Packed byte 52** -- n2xController.cpp:459-484
   combineSyncRingModDistortion: `Sync = bit 0`, `RingMod = bit 1`,
   `Distortion = bit 4` (masks ~0x01/~0x02/~0x10); the CC receive path
   (n2xstate.cpp:394-421) agrees: CCSync writes bits 0..1, CCDistortion
   bit 4 ("they end up in the same midi byte").  Bits 2-3/5-7 are
   unallocated; real files carry junk there (observed raw values up to
   240 -- pass through on read, rejected on write).
8. **Multi layout** -- 4 singles + 90 multi params nibble-encoded
   (n2xmiditypes.h:168 -> 715 B); group k starts at byte 6 + 132*k
   (n2xstate.cpp:463-468 copySingleToMulti).  REAL BANK multis are a
   different, larger shape: 1063 B = 8x66 params (docs/plans/
   2026-09-12-nodal-preset-loading.md "Format facts"; decoder accepts
   1063/1073 as multi, nl2x_patch.py:225-227).  The writer overrides land
   in group 0 of the first dump, matching the decoder's patch-0 view.
9. **No checksum** -- no checksum field exists in any NL2x dump shape;
   the container is F0 header, nibble data, optional 10-byte Aura name
   suffix (g_nameLength, n2xmiditypes.h:170), F7.  Nothing to recompute.
10. **Tree identity** -- parameterDescriptions_n2x.json exists ONLY in
    gearmulator-2.2.9 (absent from retromulator-main) and is cited from
    there.  n2xmiditypes.h/n2xstate.cpp DIFFER between the trees only in:
    (a) retromulator adds an Extended-Aura 20-byte name variant
    (g_nameLength=20 + g_singleDumpWithLegacyNameSize=159/735),
    (b) retromulator adds EmuSetMasterTune + MCU master-tune plumbing.
    The storage layout (enum, 6+2i rule, nibble order) is IDENTICAL.  No
    159/735-byte dump exists in the HDAW corpus (5,350-file scan: 5,330x
    139 + 20x 715 among sidecar carriers); the writer rejects unknown
    shapes rather than guessing.

## Full injectable framing (every byte accounted for)

    off  0    F0        SysEx begin
    off  1    33        IdClavia
    off  2    dev       device id (0x0f DefaultDeviceId; 0x00-0x0f seen)
    off  3    04        IdN2X
    off  4    msgType   0x00 SingleDumpBankEditBuffer (audible immediately),
                         0x01-0x04 banks A-D
    off  5    msgSpec   program (single dump); multi: bank slot (>=99 =
                         real-bank slot, program = spec-99)
    off  6..137         params 0..65, two nibbles each at 6+2i / 7+2i
    off  138  F7        SysEx end          -> 139 bytes
    (+10-byte Aura editor name before F7 -> 149 bytes; multi 715/725)

SMF .mid banks wrap the SAME dumps as F0 sysex events whose varlen length
INCLUDES the trailing F7 (payload then starts at 0x33); load_nord_bank
splits/validates exactly these shapes (docs/plans/2026-09-12-nodal-
preset-loading.md: F0 33 <dev> 04 header, F7 termination, 32768 B cap).
.fxb files are VST chunks for the Discovery Pro plugin -- never parsed.

## The sheet's 17 matrix/mod params (nord_offset_map.json, key subset)

| sheet key | idx | payload offset | dumpOffset | access | morph class |
|-----------|-----|----------------|------------|--------|-------------|
| filter_env_amount | 5  | 15  | 16  | byte | continuous |
| fm_depth          | 7  | 19  | 20  | byte | continuous |
| filter_env_attack | 8  | 21  | 22  | byte | continuous |
| filter_env_decay  | 9  | 23  | 24  | byte | continuous |
| filter_env_sustain| 10 | 25  | 26  | byte | continuous |
| filter_env_release| 11 | 27  | 28  | byte | continuous |
| mod_env_attack    | 18 | 41  | 42  | byte | continuous |
| mod_env_decay     | 19 | 43  | 44  | byte | continuous |
| mod_env_level     | 20 | 45  | 46  | byte | continuous |
| lfo1_rate         | 21 | 47  | 48  | byte | continuous |
| lfo1_level        | 22 | 49  | 50  | byte | continuous |
| lfo2_rate         | 23 | 51  | 52  | byte | continuous |
| lfo1_waveform     | 56 | 117 | 118 | byte | discrete (anchored + jumps) |
| lfo1_dest         | 57 | 119 | 120 | byte | discrete (anchored + jumps) |
| mod_env_dest      | 61 | 127 | 128 | byte | discrete (anchored + jumps) |
| lfo2_dest         | 65 | 135 | 136 | byte | discrete (anchored + jumps) |
| sync_distortion   | 52 | 109 | 110 | **bits** | discrete -- PACKED, never interpolated |

Nothing in the sheet is split across non-adjacent bytes; the only packed
param is sync_distortion.  The json adds the other 49 storage params as
verified extras (including the 25 `*_sens` mod-matrix rows, 25..49).

## Range + packing caveats (8-bit container vs 7-bit declarations)

- The nibble pairs carry 0..255; the plugin JSON declares 0..127 for the
  continuous params and the CC path delivers 0..127.  Real files exceed
  the declarations: mod_env_decay observed up to 128, lfo1_waveform 2..8
  vs declared 0..4, sync_distortion 0..240 vs declared 0..1 (same class
  as the xenia aliased-byte caveat).  Values pass through 1:1 on READ;
  the WRITER deliberately clamps overrides to 0..127 (CC-representable,
  render-safe).
- Byte 52 junk bits: 34/6841 sidecar files carry values >= 32 (bits 5-7
  set) -- unallocated bits in real factory junk; bit writes never touch
  them (read-modify-write with explicit masks).

## Writer + morph emission (timbre-lib/nord_dump.py)

- `load_patch(path)` -> the 66 unpacked params of the first dump;
  `load_dumps(path)` -> every dump with kind/msgType/msgSpec.
- `write_patch(base_path, overrides, out_path)` -> re-pack + framing,
  **byte-identical to the base outside the overridden offsets** (other
  dumps, gaps, name suffixes preserved verbatim).  Names: the 66 storage
  names + `sync` / `ringmod` / `distortion` (bit RMW on byte 52) +
  `sync_distortion` (raw byte).  Round-trip guarantee:
  `load_patch(write_patch(...))` reproduces the overrides and the written
  file loads through nl2x_patch unchanged.
- Morph chains reuse morph_presets.py verbatim (imported: distance
  = mean |a-b|/127 over the key union, sha1-based step ids, linear interp
  rounded to 1 decimal, discrete keys anchored to A and listed in
  `jumps`).  nodalred2x classification: continuous tokens
  amount/level/rate/depth/attack/decay/sustain/release; discrete suffixes
  waveform/dest/type/select/switch/mode; sync_distortion always discrete
  (packed).  Each step ALSO writes a real 139-byte .syx (per step:
  `file` basename + `syxParams`, the integer values actually written,
  python round-half-even); `appliesVia` is `load_nord_bank` (the loader
  VERIFIED to change the render, docs/plans/2026-09-12).  `unverified`
  stays true: byte-verified, no render-through-the-plugin confirmation.

## Chosen pairs (5 best by the established distance convention)

Selection rule (deterministic): both parents must own an example .syx
that exists AND byte-matches the preset's 17 params; require >=3 differing
named-continuous keys (Amount/Level/Rate/Depth) with summed |diff| >= 24
and <=2 discrete jumps; rank by distance ascending, direction a<b.
636 of 1,260 resolved pairs qualified; top 5:

| pair | distance | interp keys | jumps | base (parent A example, banks-relative) |
|------|----------|-------------|-------|------------------------------------------|
| 29:32 | 0.063918 | 8 | lfo1_dest, lfo2_dest | Not Sorted/Categories Archive made by Sven Saur/Arpeggio/XYLO 001.syx |
| 16:23 | 0.065771 | 7 | -- | Not Sorted/Categories Archive made by Sven Saur/Piano-Organ/CLAV 008.syx |
| 34:35 | 0.088004 | 8 | lfo1_dest | Not Sorted/Categories Archive made by Sven Saur/Piano-Organ/12 STRING CLAV 001.syx |
| 1:22  | 0.094025 | 11 | mod_env_dest | Not Sorted/Categories Archive made by Sven Saur/Brass/DETUNED BRASS 001.syx |
| 34:39 | 0.096341 | 9 | lfo1_dest, mod_env_dest | Not Sorted/Categories Archive made by Sven Saur/Piano-Organ/12 STRING CLAV 001.syx |

Largest named-continuous diffs driving each chain: 29:32 lfo1_rate -43 /
mod_env_level -17; 16:23 fm_depth -67 / filter_env_decay -28; 34:35
filter_env_release +82 / filter_env_amount +37; 1:22 mod_env_attack -66 /
filter_env_decay +32; 34:39 lfo1_level +60 / lfo2_rate -45.
Artifacts: matrix_presets/nord_morphs/<a-b>/step1..4.syx (20 files, 139 B
each) + matrix_presets/nord_morphs.json (schema
hdaw.matrix.preset.morph.v1, `unverified: true`, bank-relative paths
only).  Re-running the emitter with identical arguments is byte-stable.

## Empirical validation (full sidecar-carrying corpus)

Independent strict re-parse (F0 33 <dev> 04 ... F7 framing validated per
dump; SMF varlen walk for .mid) of every file that carries a .nl2x.json
sidecar, then byte-compare of all 66 unpacked values against the sidecar's
mappedParams:

- 5,350 files, 19 top-level banks, 353,100 values: **0 mismatches**.
- 0 framing failures; msgType histogram 0x00 x5134, 0x01-0x04 x170,
  multi 0x1F x20; dump lens 139 x5330, 715 x20; 0 program-field
  mismatches (single: spec; multi-bank: spec-99).
- pytest fixture: same comparison over a deterministic per-bank sample
  (>=200 files, >=10 banks floor; HDAW_NL2X_GATE_FULL=1 for the whole
  corpus), skipped when the bank root is absent.

Sheet-parent spot check: 99 of the sheet's 200 example names resolve to
.syX files whose decoded 17 params byte-match the preset signature (the
remaining 101 -- e.g. '0404TremeloPad' -- have no .syx stem in the tree,
likely mid-bank/Aura-name references; morph parents were restricted to
the 36 presets with byte-verified examples).

## Validation recipe

    python3 -c 'import json; d=json.load(open("timbre-lib/matrix_presets/nord_offset_map.json")); print(len(d["params"]), d["params"]["filter_env_amount"], d["params"]["sync_distortion"]["bits"])'
    # 66 {'index': 5, 'offset': 15, 'dumpOffset': 16, ...} {'sync': 0, 'ringmod': 1, 'distortion': 4}

Byte check against any patch + sidecar:

    import json, sys
    sys.path.insert(0, "timbre-lib")
    import nord_dump as nd
    p = "/mnt/d/pdf/NL2x Banks/BoBSwanS Random Rework NL2 Patches/BoBs Brassic.syx"
    sc = json.load(open(p + ".nl2x.json"))
    raw = open(p, "rb").read()
    assert all((raw[6+2*int(k)] & 0xF) | (raw[7+2*int(k)] << 4) == v["raw"]
               for k, v in sc["mappedParams"].items())

pytest (writer round-trips, byte-fidelity, map fixture, morph artifacts,
md5 pins of the neighbor artifacts):

    PYTHONPATH=/tmp/hdawpylib python3 -m pytest timbre-lib/test_nord_dump.py -q

No engine/DSP files were touched; deliverables live only in timbre-lib/.
