# Xenia (Microwave XT) plugin-param -> single-dump byte offset map

**Status: branch (a) -- mapping rule VERIFIED** (static citation chain +
empirical validation against all 34 sidecar/bank pairs under
/mnt/d/pdf/microwave/ -- 1,166,386 sidecar param values byte-matched, **0
mismatches**; checksum formula holds for 3823/3823 real dumps).

Scope: map the Xenia (xtJucePlugin) parameter names in
gearmulator-2.2.9 source/xtJucePlugin/parameterDescriptions_xt.json
(patch-section indices 0..255) onto the xtLib Microwave II/XT **single
program dump** (265 bytes, header F0 3E 0E <dev> 10, 16-char name at 247,
checksum at 263).  Mirror of the verified microQ/vavra pipeline
(matrix_presets/vavra-offset-map.md).

## The rule

    dump_byte_offset = 7 + linear_json_index
    value            = one 7-bit byte at that offset (no packing, no 14-bit)

- The linear JSON index is exactly the "index" field of the patch section
  (0..239 are patch params; 240..255 are the 16 name characters).  The
  (page, 7-bit) split in the JSON parser is wire-format only.
- The dump is stored VERBATIM: xt state IS the raw byte array; there is no
  convert/repack step in either direction.
- Single programs only.  The Multi dump is ALSO 265 bytes (xtState.h:68)
  with its own index space (volume/controls/name/instruments) -- not
  mapped here.

## Sidecar key spaces (what xenia.json was built from)

microwave_patch.py writes <bank>.xenia.json sidecars whose
params.<patchName> dicts are keyed by RAW offsets in the container:

- .mid / .MID / .syx: body offset after the 4-byte prefix 3E 0E <dev> <cmd>:
  key 0 = bank byte, key 1 = program byte, **param N at key N+2**, name at
  body offset 242 (= full 247).
- .usb / <micro>sb: 256-byte bank records: **param N at key N**, name at
  record offset 240.

So: vocab index N <-> mid key N+2 <-> usb key N <-> full-dump byte 7+N.

## Citation chain (gearmulator-2.2.9; tree identity below)

1. **Base offset 7** -- xtLib/xtMidiTypes.h:62-65: IdxSingleBank = 5,
   IdxSingleProgram = 6, IdxSingleParamFirst = 7,
   IdxSingleChecksumStart = IdxSingleParamFirst; param-change wire layout
   IndexH@6 / IndexL@7 / value@8 (lines 71-73; wLib::IdxBuffer = 5,
   wLib/wMidiTypes.h:20).
2. **Raw-copy state** -- source/wLib/wState.h:18-24: convertTo() is a plain
   std::copy guarded by a SIZE check only -- **no checksum validation on
   receive**; xtState.cpp parseSingleDump (~line 361) uses it;
   xtState.h:75-80: Single = std::array<uint8_t, 265> via Dumps[].
3. **Dump table** -- xtLib/xtState.h:67: Dumps[DumpType::Single] carries
   firstParamIndex = IdxSingleParamFirst and dumpSize = 265.
4. **Receiver addressing** -- xtLib/xtState.cpp:495-512 (anonymous
   getParameter): i = dump.firstParamIndex; if IndexH != IndexL:
   i += data[IndexH] << 7; i += data[IndexL] -- i.e. 7 + (IndexH<<7 |
   IndexL); modifySingle (lines 447-452) writes the ONE value byte.
5. **JSON linear index** -- jucePluginLib/parameterdescriptions.cpp:348-356:
   readPropertyInt("index"); while (index >= 128) { index -= 128; ++page; }
   -- the JSON index is the linear storage index, split only for
   transmission.
6. **Name position** -- xtMidiTypes.h:213-214: mw2::g_singleNamePosition =
   247 "in a dump including sysex header" (= param index 240);
   xtState.cpp:343-345 (setSingleName) writes there.
7. **Checksum** -- xtState.h:136-145 (updateChecksum): byte at size-2
   (=263) = sum(start .. size-2) & 0x7F with start = IdxSingleChecksumStart
   = 7; recomputed by append() when the state is serialized
   (xtState.cpp:123-134).  Empirically 3823/3823 real mid-bank dumps
   satisfy the formula.
8. **Anchors** -- SingleParameter::Version = 0 (xtMidiTypes.h:132-135):
   dump[7] == 1 for 3821/3823 dumps (2 carry 127); device byte 0x00 in
   all dumps; bank byte = xt LocationH {0x00: 1888, 0x01: 1920, 0x20: 7
   (single edit buffer), 0x30: 8 (multi-mode edit buffer)}.
9. **Tree identity** -- xtLib/xtState.h, xtLib/xtState.cpp,
   xtLib/xtMidiTypes.h and wLib/wState.h are byte-identical between
   gearmulator-2.2.9 and retromulator-main (cmp).  parameterDescriptions_xt.json
   and jucePluginLib/ exist only in gearmulator-2.2.9 and are cited from
   there (the tree the HDAW Xenia CLAP ships from).

## Full injectable framing (every byte accounted for)

    off  0    F0        SysEx begin
    off  1    3E        IdWaldorf
    off  2    0E        IdMw2 (Microwave II/XT/XTk)
    off  3    dev       device id (0x00 everywhere in the corpus; 0x7F omni per wLib)
    off  4    10        SysexCommand::SingleDump
    off  5    bank      xt LocationH: 0x00 Bank A, 0x01 Bank B,
                         0x20 single edit buffer (audible immediately),
                         0x30 multi-mode single edit buffer
    off  6    prog      program number (0..127)
    off  7..262         params 0..255, one 7-bit byte each (full[7+N]);
                        240..255 = 16-char name, space padded
    off  263   cksum    sum(full[7:263]) & 0x7F   (NOT validated on receive)
    off  264   F7       SysEx end

Total 265 bytes.  A dump sent to the plugin lands via
State::parseSingleDump at the LocationH/program addressed by bytes 5/6;
bank 0x20 addresses the live edit buffer.

## Empirical validation (34 sidecar/bank pairs)

Corpus /mnt/d/pdf/microwave: 34 *.xenia.json sidecars (8 top-level +
26 under factory_1997/1998/2000, hans_heerooms/*, paul_nagle/*, the_lab/*,
xt_usersoundset*/), 5,358 strict-parsed patches, 3,823 mid dumps.

- Independent strict SMF/record re-parse (NOT microwave_patch.py's raw
  scan): **1,166,386 sidecar param values byte-matched, 0 mismatches**.
- 4 patches excluded by construction (their sidecar names are only findable
  at a fallback offset -- e.g. fact9712's extra patch has nameOffset 219 --
  or start with a non-printable character; 1 patch per bank in
  Factory.<micro>sb, factory_1997/fact9712.mid, factory_1998/fact9806.mid,
  xt_usersoundset3/usersoundset3.mid + its copy).  Their 1,116 keys were
  the only "mismatches" of a naive pass; no VALUE mismatch exists.
- Checksum: 3823/3823 mid dumps satisfy the source formula.
- The strict parse finds MORE patches than some sidecars recorded
  (frstloop.mid 256 vs 1, key17demosong.mid 256 vs 0): microwave_patch.py's
  scanner skips dumps whose name bytes are not printable, so sidecar
  patchCounts are lower bounds.  The 1,166,386 compared values include
  every sidecar entry whose name the strict parser confirms.

## xenia.json labeling caveat (harvest off-by-2 for .mid sidecars)

harvest_matrix_presets.py labeled sidecar key k directly with
vocab[k]; the correct labeling is vocab[k-2] for .mid sidecars (key k =
param k-2) and vocab[k] for .usb sidecars (key k = param k).  Probe across
all 40 preset clusters x their example patches (24,108 named values):
**correct 11,882, shifted 12,082, neither 144** -- i.e. named
values from .usb banks are correct, values harvested from .mid banks are
shifted by one param slot.  xenia.json itself stays untouched (md5 pinned);
xenia_morphs_injectable.json records per pair which labeling its parent
verified under ("how": "verified" | "vocab-shifted") and writes the step
values at that same labeling, so every injectable chain is internally
consistent with xenia_morphs.json's value space.

## Range caveat (aliased bytes, junk the device tolerates)

Real dumps exceed the plugin JSON's declared maxima at mod-source and
aliased-FX bytes (EffectType declared 0..34 but observed up to 127;
Slot*Source 0..31 vs 127; Mod*Type 0..15 vs 35+; ChorusEnabled 0..1 vs
112) -- the same class as the vavra FX-alias noise.  Bytes pass through
1:1 (one 7-bit byte each), so injectability is unaffected; only the
semantic interpretation is aliased.

## Mapped params (xenia_offset_map.json -- 85 entries)

Keys are full-dump byte offsets (7 + linear index); values are the primary
(first-file-order) patch-section name.  "declared" is the plugin JSON's
min..max (see the range caveat).

| byte | idx | param | declared |
|------|-----|-------|----------|
| 35 | 28 | W1EnvAmount | 0..127 |
| 36 | 29 | W1EnvVelAmount | 0..127 |
| 45 | 38 | W2EnvAmount | 0..127 |
| 46 | 39 | W2EnvVelAmount | 0..127 |
| 56 | 49 | MixRingMod | 0..127 |
| 69 | 62 | F1Cutoff | 0..127 |
| 70 | 63 | F1Resonance | 0..127 |
| 73 | 66 | F1EnvAmount | 0..127 |
| 74 | 67 | F1EnvVelAmount | 0..127 |
| 83 | 76 | EffectType | 0..34 |
| 88 | 81 | DelayTime | 0..127 |
| 89 | 82 | ChorusEnabled | 0..1 |
| 90 | 83 | EffectParamB | 0..127 |
| 91 | 84 | Pan | 0..127 |
| 92 | 85 | PanKeytrack | 0..127 |
| 93 | 86 | EffectParamC | 0..127 |
| 119 | 112 | DePan | 0..127 |
| 168 | 161 | Lfo1Delay | 0..127 |
| 175 | 168 | Lfo2Delay | 0..127 |
| 181 | 174 | ModDelaySource | 0..31 |
| 182 | 175 | ModDelayTime | 0..127 |
| 183 | 176 | Mod1Source1 | 0..31 |
| 184 | 177 | Mod1Source2 | 0..31 |
| 185 | 178 | Mod1Type | 0..15 |
| 186 | 179 | Mod1Parameter | 0..127 |
| 187 | 180 | Mod2Source1 | 0..31 |
| 188 | 181 | Mod2Source2 | 0..31 |
| 189 | 182 | Mod2Type | 0..15 |
| 190 | 183 | Mod2Parameter | 0..127 |
| 191 | 184 | Mod3Source1 | 0..31 |
| 192 | 185 | Mod3Source2 | 0..31 |
| 193 | 186 | Mod3Type | 0..15 |
| 194 | 187 | Mod3Parameter | 0..127 |
| 195 | 188 | Mod4Source1 | 0..31 |
| 196 | 189 | Mod4Source2 | 0..31 |
| 197 | 190 | Mod4Type | 0..15 |
| 198 | 191 | Mod4Parameter | 0..127 |
| 199 | 192 | Slot1Source | 0..31 |
| 200 | 193 | Slot1Amount | 0..127 |
| 201 | 194 | Slot1Destination | 0..35 |
| 202 | 195 | Slot2Source | 0..31 |
| 203 | 196 | Slot2Amount | 0..127 |
| 204 | 197 | Slot2Destination | 0..35 |
| 205 | 198 | Slot3Source | 0..31 |
| 206 | 199 | Slot3Amount | 0..127 |
| 207 | 200 | Slot3Destination | 0..35 |
| 208 | 201 | Slot4Source | 0..31 |
| 209 | 202 | Slot4Amount | 0..127 |
| 210 | 203 | Slot4Destination | 0..35 |
| 211 | 204 | Slot5Source | 0..31 |
| 212 | 205 | Slot5Amount | 0..127 |
| 213 | 206 | Slot5Destination | 0..35 |
| 214 | 207 | Slot6Source | 0..31 |
| 215 | 208 | Slot6Amount | 0..127 |
| 216 | 209 | Slot6Destination | 0..35 |
| 217 | 210 | Slot7Source | 0..31 |
| 218 | 211 | Slot7Amount | 0..127 |
| 219 | 212 | Slot7Destination | 0..35 |
| 220 | 213 | Slot8Source | 0..31 |
| 221 | 214 | Slot8Amount | 0..127 |
| 222 | 215 | Slot8Destination | 0..35 |
| 223 | 216 | Slot9Source | 0..31 |
| 224 | 217 | Slot9Amount | 0..127 |
| 225 | 218 | Slot9Destination | 0..35 |
| 226 | 219 | Slot10Source | 0..31 |
| 227 | 220 | Slot10Amount | 0..127 |
| 228 | 221 | Slot10Destination | 0..35 |
| 229 | 222 | Slot11Source | 0..31 |
| 230 | 223 | Slot11Amount | 0..127 |
| 231 | 224 | Slot11Destination | 0..35 |
| 232 | 225 | Slot12Source | 0..31 |
| 233 | 226 | Slot12Amount | 0..127 |
| 234 | 227 | Slot12Destination | 0..35 |
| 235 | 228 | Slot13Source | 0..31 |
| 236 | 229 | Slot13Amount | 0..127 |
| 237 | 230 | Slot13Destination | 0..35 |
| 238 | 231 | Slot14Source | 0..31 |
| 239 | 232 | Slot14Amount | 0..127 |
| 240 | 233 | Slot14Destination | 0..35 |
| 241 | 234 | Slot15Source | 0..31 |
| 242 | 235 | Slot15Amount | 0..127 |
| 243 | 236 | Slot15Destination | 0..35 |
| 244 | 237 | Slot16Source | 0..31 |
| 245 | 238 | Slot16Amount | 0..127 |
| 246 | 239 | Slot16Destination | 0..35 |

(EffectParamA alias: index 81 ships as **DelayTime** -- the specific name
wins, mirroring the harvest's alias rule; EffectParamB@90 and
EffectParamC@93 stay generic.  Dropped as aliases: none.)

## Morph injectability (xenia_morphs_injectable.json)

Derived from xenia_morphs.json (pairs/steps/interpolated values reused
verbatim; that file's md5 is pinned by the test suite).  Per pair the
parent-A preset is byte-verified against the bank corpus and each step
carries "sysex": the 265 ints of the parent dump with the step's mappable
named values at their mapped offsets:

| 17:21 | verified | 'Gow2          DN' (Factory.µsb) | 84/84 bytes |
| 0:3 | vocab-shifted | 'Init Sound V1.1' (fact2000-08.mid) | 84/84 bytes |
| 17:32 | verified | 'Gow2          DN' (Factory.µsb) | 84/84 bytes |
| 12:28 | vocab-shifted | 'Simpl FM' (tibetarr.mid) | 84/84 bytes |
| 3:9 | vocab-shifted | 'Tom 1    M10 SCD' (XT usersoundsetV12.MID) | 84/84 bytes |

All five parents byte-verify at 84/84 named params.  Pairs 17:21 / 17:32
resolve under the CORRECT labeling; the other three only under the SHIFTED
labeling (.mid banks -- the shifted space is what xenia_morphs.json's
values live in for mid-derived clusters; see the caveat above).  "unverified" stays true: the dumps are byte-verified, but
no render-through-the-plugin confirmation exists yet.

## Validation recipe

    python3 -c 'import json; d=json.load(open("timbre-lib/matrix_presets/xenia_offset_map.json")); print(len(d), d["83"], d["199"], d["245"])'
    # 85 EffectType Slot1Source Slot16Amount

Byte check against any .mid bank + sidecar:

    import json, sys
    sys.path.insert(0, "timbre-lib")
    import xenia_dump as xd
    rows = xd.load_bank("/mnt/d/pdf/microwave/boele_gerkes.mid")
    sc = json.load(open("/mnt/d/pdf/microwave/boele_gerkes.mid.xenia.json"))
    fmap = {r["name"]: r["dump"] for r in rows}
    assert all(fmap[n][5 + int(k)] == v
               for n, kv in sc["params"].items() if n in fmap
               for k, v in kv.items())
    # and the rule: param with JSON index N lives at full[7+N]

pytest (includes the full-corpus mapping fixture, skipped when the bank
root is absent):

    PYTHONPATH=/tmp/hdawpylib python3 -m pytest timbre-lib/test_xenia_dump.py -q

## Search notes (for the record)

- Like mqLib, xtLib has NO sound-parameter name table; names come from the
  plugin's parameterDescriptions_xt.json.  Sections (patch, multi,
  multi-instrument, ...) REUSE the same index space; the patch section comes
  first, so first-occurrence-per-index yields patch names (multi aliases
  like MName12@28 / MI0Pan@8 share indices with W1EnvAmount / O1 params).
- Every param is one 7-bit byte -- including the wavetable select
  (SingleParameter::Wavetable = 25, xtMidiTypes.h:132-135);
  getWavetableFromSingleDump reads that single byte (xtState.cpp:~355).
- No engine/DSP files were touched; deliverables live only in timbre-lib/.
