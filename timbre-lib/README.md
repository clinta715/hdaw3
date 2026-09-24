
# TimbreLib ??? analyzed sample library for HDAW

Pipeline: DSP descriptors -> CLAP captions + AudioSet tags -> Qwen2.5-3B prose.

## Patch pipelines at a glance

| Decoder | Engine (sidecar) | Library | Sidecar name | Notes |
| --- | --- | --- | --- | --- |
| `virus_patch.py` | `sub_synth` | Virus patch library | `<patch>.virus.json` | mapped onto the internal sub_synth |
| `virus_fx_pages.py` | (enriches `sub_synth`) | Virus patch library | `<patch>.virus.json` rev 2 | FX/mod-matrix page decode; byte-match stop-gate; `harvest_matrix_presets` clusters it |
| `nl2x_patch.py` | `nodalred2x` | `D:\pdf\NL2x Banks` | `<patch>.nl2x.json` | 6841 sidecars; `load_nord_bank` verified |
| `je8086_patch.py` | `je8086` | `D:\pdf\je8086` | `<bank>.je8086.json` | + exploded per-patch tree; DT1 dumps apply since 2026-09-20 (wrapper retarget) |
| `microq_patch.py` | `vavra` | `D:\pdf\rhythm-lab.com_waldorf_micro_q` | `<patch>.vavra.json` | categories from the dump; no host params |
| `microwave_patch.py` | `xenia` | `D:\pdf\microwave` | `<bank>.xenia.json` | .usb bank images (256x256 B) + SMF SingleDumps; 1791 patches; no host params |

All four suffixes are ingested by `FileLibraryManager` (register the folder as a
*patch* library), so the presets become searchable with engine, role and tags. Device
capability matrix, modulation matrices, loader status and FX recipes:
`docs/hardware-va-suite.md`.

## NL2X patch decoder (NodalRed2x / Nord Lead 2x)

`nl2x_patch.py` mirrors `virus_patch.py` for the Clavia Nord Lead 2x bank
library (`D:\pdf\NL2x Banks`, 6898 files). It parses raw Clavia SysEx
(.syx) and SMF-wrapped banks (.mid; .fxb VST chunks are detected and
skipped), decodes the nibble-encoded 66-parameter single dumps (and the
1063-byte multi dumps), names every parameter via the firmware's
`SingleParam` enum (cutoff/resonance/envs/LFOs/FM/sync/distortion/...),
and writes `<patch>.nl2x.json` sidecars (schema `hdaw.nl2x.patch.v1`,
engine `nodalred2x`) next to each patch file so FileLibraryManager /
`search_library` can index them.

Usage:

    py -3.14 timbre-lib/nl2x_patch.py --dump <file>       # decode one file
    py -3.14 timbre-lib/nl2x_patch.py --survey "D:\pdf\NL2x Banks" \
        --out nl2x_survey.json
    py -3.14 timbre-lib/nl2x_patch.py --sidecars "D:\pdf\NL2x Banks"
    py -3.14 timbre-lib/nl2x_patch.py --sidecars "D:\pdf\NL2x Banks" --role bass

Results (2026-09-13 sweep): 6841/6846 files parsed (99.9%; 57 skipped:
.fxb chunks + unreadable), 29436 dumps = 26630 singles + 2806 multis,
6841 sidecars written. Survey: `nl2x_survey.json`. Tests:
`test_nl2x_patch.py` (15, incl. real-library spot checks that skip when
the bank root is absent). Plan:
`docs/plans/2026-09-13-nl2x-patch-decoder-survey.md`.
All local. Toolchain: python 3.11 venv (torch cu128, transformers, librosa,
scipy, llama-cpp-python CPU wheel), GGUF at ./Qwen2.5-3B-Instruct-Q4_K_M.gguf.

## JE8086 patch decoder + sidecar sweep (Roland JP-8080)

`je8086_patch.py` decodes the JP-8080 bank library (`D:\pdf\je8086`, 46 files:
36 SMF-wrapped `.mid` banks + 10 raw `.syx` dumps) into per-patch records with
named parameters and writes `<bank>.je8086.json` sidecars (schema
`hdaw.je8086.bank.v1`, engine `je8086`) next to each bank.

Format (verified against gearmulator 2.2.9 `jeLib/jemiditypes.h` + all 46 real
files): Roland DT1 `F0 41 10 00 06 12 <a0 a1 a2> <data..> <checksum> F7`
(model 0x0006, 7-bit address bytes, Roland checksum). One address unit = 256 B
(one page) and a patch = 0x200 B = 2 pages, so a 64-patch bank spans 128 pages:
patch area (base `02 00 00`) `bank = (v-base)//128`,
`slot = ((v-base)%128)//2 + 1`; performance area (base `03 00 00`) strided 128
pages with `PatchUpper`/`PatchLower` at pages 64/66. The dump carries ONE
leading byte before the documented patch body, so the 16-char name is
`data[1:17]` and `Patch.<Param> = 0xNN` lives at `data[1+0xNN]` (validated over
3893 name-bearing messages: nine tight-range parameters in range for 99.9% at
`off+1` versus 0.0% at `off`/`off-1`). 78 parameters are decoded (osc / filter /
amp / LFO / FX / delay / portamento plus the CC-control depths), with the
velocity/morph block 0x119-0x16C reported as `unmapped`.

Usage:

    py -3.14 timbre-lib/je8086_patch.py --dump "D:\pdf\je8086\Psytrance.syx"
    py -3.14 timbre-lib/je8086_patch.py --survey "D:\pdf\je8086" --out je8086_survey.json
    py -3.14 timbre-lib/je8086_patch.py --sidecars "D:\pdf\je8086"
    py -3.14 timbre-lib/je8086_patch.py --sidecars "D:\pdf\je8086" --role bass --explode
    py -3.14 timbre-lib/je8086_patch.py --sidecars "D:\pdf\je8086" --with-bytes
    py -3.14 timbre-lib/je8086_patch.py --sidecars "D:\pdf\je8086" --explode
    py -3.14 timbre-lib/je8086_patch.py --verify-exploded "D:\pdf\je8086\exploded"

The default sidecar is a compact metadata index (bank / role / params / labels /
`paramDefs` / `patches[]`); `--with-bytes` embeds each patch's raw DT1 payload,
and `--explode` writes a per-patch `.syx` (byte-identical DT1) + sidecar under
`<DIR>/exploded/<bank>/`.

Sweep results (2026-09-16, after the SMF-varint fix): 46/46 files parsed,
**10368 DT1 messages, 0 checksum failures**, 4983 entries (**2676 usable
patches**, 1086 performance names), 46 sidecars, 3689 exploded patch files,
survey `je8086_survey.json`. Roles: 1345 lead, 1333 pluck, 560 bass, 501 pad,
1088 performance, 91 fx, 8 arp, 55 other. The three psy-named `.syx` banks carry
only 1-2 usable patches each (the rest are `INIT PATCH` placeholders) -- the real
psy content is `Kulshan Mystical Psytrance.mid` (100 usable) and
`Techno 2.syx` (189). Tests: `test_je8086_patch.py`. Plan:
`docs/plans/2026-09-16-je8086-preset-pipeline.md`.

### Exploded per-patch tree (`--explode`)

    py -3.14 timbre-lib/je8086_patch.py --sidecars "D:\pdf\je8086" --explode
    py -3.14 timbre-lib/je8086_patch.py --verify-exploded "D:\pdf\je8086\exploded"

`--explode` writes one DT1 `.syx` per patch unit under `<DIR>/exploded/<bank>/`
(relocate with `--explode-dir`), named "`<ref> <name>.syx`" where the ref is
`bank0-slot25` / `perf015-part1`. The ref makes the filename unique - two
performances that share a patch name and slot can never overwrite each other - and
it encodes the same unit order the loader's `preset` index uses. Each `.syx` gets a
`<file>.je8086.json` sidecar (schema `hdaw.je8086.patch.v1`) and the run writes a
browsable `exploded/index.json` manifest. Re-runs are idempotent: bank discovery
prunes the explode tree, otherwise a second run ingests its own per-patch files as
banks (observed: 3735 "banks" instead of 46, with 3625 verification failures).

`--verify-exploded` round-trips every file (parses to exactly one unit; name, sha1
and parameters equal its sidecar; checksums valid). Sweep 2026-09-16: 46 banks ->
**3689 files, 3689 ok / 0 bad**.

In-app: register `<DIR>/exploded` as a **patch** library and patches become
individually searchable -- searching "LITTLEDIRT" returns
`perf016-part1 LITTLEDIRT.syx` with `patchEngine=je8086`, `roleVerdict=bass` and the
description "JP-8080 bass patch: saw osc, cutoff 20, res 47, amp A 1, amp R 26, filt
env 105, delay 75, fx DISTORTION". `load_je8086_preset` accepts a bank file plus a
1-based `preset` index, or one of these exploded single-patch files directly.

Note: `FileLibraryManager::applyPatchSidecar` now also reads `.nl2x.json` and
`.je8086.json` (engine fallback nodalred2x / je8086), so these sidecars are
searchable in-app.

## MicroQ patch decoder + sidecar sweep (Waldorf microQ / Vavra)

`microq_patch.py` decodes the rhythm-lab microQ library
(`D:\pdf\rhythm-lab.com_waldorf_micro_q`, 528 single-sound files) and writes
`<patch>.vavra.json` sidecars (schema `hdaw.microq.patch.v1`, engine `vavra`) next
to each patch, so `FileLibraryManager` / `search_library` can index them.

Format (gearmulator 2.2.9 `source/mqLib` + all 528 files, 2026-09-16): `F0 3E 10 00
10 <buffer>` + 363 parameter bytes + 16-char name at offset 370 + 4-char category at
386 + checksum + `F7` = exactly 392 bytes, matching
`Dumps[DumpType::Single].dumpSize = 392` (mqstate.h). The name/category offsets are
the emulator's own constants (`mq::g_singleNameOffset = 370`,
`mq::g_categoryOffset = 386`, mqmiditypes.h); the 'q' variant (371/387) does not
occur here (0/528). The emulation validates dump SIZE only (`wLib/wState.h
convertTo`) and never checksum, and these third-party files do not follow the
Waldorf 7-bit-sum rule (249/528 match), so the checksum is recorded as
informational and never rejects a file.

    py -3.14 timbre-lib/microq_patch.py --dump "<patch>.syx"
    py -3.14 timbre-lib/microq_patch.py --survey "D:\pdf\rhythm-lab.com_waldorf_micro_q" --out microq_survey.json
    py -3.14 timbre-lib/microq_patch.py --sidecars "D:\pdf\rhythm-lab.com_waldorf_micro_q"
    py -3.14 timbre-lib/microq_patch.py --verify "D:\pdf\rhythm-lab.com_waldorf_micro_q"

Sweep: **528/528 parsed, 528 sidecars, verify: 528 ok / 0 bad**. Roles come from the
in-file category (Arp 147, Pad+Atmo 122, Lead 81, Bass 79, Poly+Keys 43, FX 27,
other 29); sidecar tags carry `category:<X>` and the pack - the two things worth
searching on. Provenance: "MicroQ Patches by Chris Jones" (rhythm-lab.com), free to
use with credit.

**Loader status (measured, not assumed).** Injecting a dump into a live Vavra slot
works at the transport level (`send_fx_midi {kind:"sysEx", bytes:[...]}` ->
`queued=1`, state captures at 440 B), but the exported audio did not change
measurably (peak 0.02603 -> 0.02591) and **Vavra exposes no host parameters**
(`list_fx_params` -> `{"params":[]}`), so there is no observation channel to confirm
the patch applied. The emulation's own State does receive external dumps
(`mqLib/device.cpp` -> `State::receive(..., Origin::External)`), unlike the JP-8080,
so the remaining check is interactive: open Vavra's editor and watch its LCD after
injecting. No loader tool is shipped until that check passes.

## Matrix preset harvester (named FX/mod-matrix presets per engine)

`harvest_matrix_presets.py` clusters the FX / modulation-matrix parameter
tuples found in the patch sidecars into named, device-applicable presets and
writes one sheet per engine to `matrix_presets/<engine>.json` (schema
`hdaw.matrix.preset.v1`, `unverified: true` until the live apply/ear pass).
Unlike `harvest_fx_presets.py` (which harvests FX value *distributions* for
re-creation with HDAW's internal FX), one preset here is the full matrix/FX
tuple of one canonical patch config. Lookup rule + apply-path table:
`docs/hardware-va-suite.md` §9. Plan: `docs/plans/2026-09-16-matrix-presets.md`.

Usage (roots default to the known engine libraries; stdlib only):

    py -3 timbre-lib/harvest_matrix_presets.py                  # real roots
    py -3 timbre-lib/harvest_matrix_presets.py --out-dir DIR ROOT... \
        --vocab xenia=<parameterDescriptions_xt.json> \
        --offset-map vavra=timbre-lib/matrix_presets/vavra_offset_map.json

`--vocab ENGINE=PATH` overrides an index→name vocabulary, `--offset-map
ENGINE=PATH` a dump-offset→name map (auto-detected for vavra); optional
`--max-presets N` (default 40) and repeatable `--engine`.

Shipped 2026-09-16: je8086 40 (`set_fx_param`), nodalred2x 40
(`load_nord_bank`), xenia 40 (`patch_or_sysex_unverified`), vavra 40
(`state_blob_or_patch_unverified`). Virus shipped 1 (`midi_cc_pc`) + a
recorded `presetsShortfall` on 2026-09-16 (sidecars were tone-param-only);
the fx-pages decode below removed the shortfall on 2026-09-17: virus now
ships **40** (`midi_cc_pc`, 148 rev-2 sidecars over 5,425 dumps) plus
`virus_morphs.json` (5 pairs x 4 steps; `program_writer_pending` superseded
2026-09-17 by `virus_dump.py` — it now carries per-step injectable SysEx,
TI/BC model-tagged).

Vavra naming provenance: `matrix_presets/vavra-offset-map.md` (+ the
machine-readable `vavra_offset_map.json`) verifies **dump byte = 7 + linear
parameterDescriptions index** against the mqLib sources and 305 real
sidecar/syx pairs; the map covers the 86-key FX/matrix subset, so vavra
presets carry those names and keep raw `off_<N>` keys for the other dump
slots.

Tests (pytest 9.1.1 lives in the /tmp/hdawpylib scratch dir in this WSL
environment; recreate with `pip install --target /tmp/hdawpylib pytest` if
missing):

    PYTHONPATH=/tmp/hdawpylib python3 -m pytest timbre-lib/test_harvest_matrix_presets.py -q

## Matrix-preset toolchain — writers, morphs, artifacts, live status

All tools around the harvested sheets live in `timbre-lib/`:

| Tool | Role |
| --- | --- |
| `harvest_matrix_presets.py` | clusters sidecar FX/mod-matrix tuples into the `<engine>.json` sheets (section above) |
| `morph_presets.py` | interpolates two sheet presets into a morph chain (`--sheet/--pairs/--steps/--out/--index-map`); per-engine continuous-vs-discrete classification (je8086 classifies by decoder name); discrete hops recorded per step in `jumps` |
| `virus_fx_pages.py` | Virus TI / B/C FX+mod-matrix page decoder (sidecarRev 2); its byte-match sweep closed the virus shortfall (R7, 2026-09-17: 1.78M values, 0 mismatches) |
| `xenia_dump.py` | Microwave XT 265-byte single-dump writer + injectable morph emitter (checksum holds 3823/3823 real dumps; the emulation validates size only) |
| `nord_dump.py` | Nord Lead 2x writer (66 params x 2 nibbles; packed byte 52 via named read-modify-write; no checksum) + morph emitter |
| `virus_dump.py` | Virus TI 524 B + B/C 267 B program writer; checksum recomputed per the dump-defs rule; byte-fidelity + round-trip tested |
| `vavra_dump.py` | microQ 392-byte single-dump writer (parent checksum byte preserved) + blueprint morph emitter |

MCP front door (R3, `McpTools_Matrix.cpp`): `list_matrix_presets {engine}` and
`apply_matrix_preset {engine, id, trackId, slotIndex}` — they resolve the index
maps and emit/inject the SysEx for you.

Artifacts in `timbre-lib/matrix_presets/`: five 40-preset sheets
(`<engine>.json`); `je8086_morphs.json` (20 steps, paramIndex embedded);
`xenia_morphs.json` + `xenia_morphs_injectable.json` (per-step SysEx, off-by-2
fixed, per-pair provenance); `nord_morphs/` (20 `.syx`) + `nord_morphs.json`;
`virus_morphs.json` (per-step SysEx, TI/BC model-tagged); `vavra_morphs.json`
(blueprint-only); offset maps `{nord,xenia,vavra}_offset_map.json` +
`*-offset-map.md`; `je8086_param_index_map.json` (display-name → index);
`xenia_roles_morphs.md` (post-fix roles + morph parents).

Live apply status (measured 2026-09-16/17; evidence:
`docs/plans/2026-09-16-matrix-presets.md`,
`docs/plans/2026-09-16-matrix-preset-engine-fixes.md`):

| Engine | Sheet | Morphs | Live apply (measured) |
| --- | --- | --- | --- |
| je8086 | 40 | 20 steps, paramIndex embedded | **VERIFIED** — 46/46 `set_fx_param` of preset b44052f76c82a7a7, audible A/B; plugin publishes display names ('A FLT CUTOFF FREQ') → use `je8086_param_index_map.json` |
| xenia | 40 | injectable SysEx | **VERIFIED AUDIBLE** — SysEx → edit buffer (bank 0x20); offset map 1,166,386 values, 0 mismatches |
| nodalred2x | 40 | 20 `.syx` + json | **VERIFIED AUDIBLE** — morph `.syx` via `load_nord_bank`; map 5,350 files / 353,100 values / 0 mismatches |
| virus | 40 (R7) | SysEx, TI/BC-tagged | writer format-verified (checksum rule cited + validated); live A/B **BLOCKED — finding F-A** (Osirus slot renders bit-identical silence under all programs/dumps); parameter path possible (3,086 exposed params) pending finding F-B |
| vavra | 40 | blueprint-only | **MEASURED NOT APPLYING** — queued=1 but captureStatus=unchanged, render identical; puppetry not pursued |

## Analyze a folder
    py -3.14 lib_analyze.py <folder> [--limit N] [--no-llm] [--sidecars]
                             [--out PATH] [--library NAME]
Run it with any interpreter that has the ML stack (numpy/librosa/torch/
transformers; llama-cpp-python optional). `--library NAME` registers <folder>
in the HDAW library registry after the analysis (via register_library.py).
(The old `./analyze.sh` wrapper — WSL venv + TIMBRE_PY — is retired.)
Writes <folder>/timbre_index.json (per-file: dsp, dsp_words, captions, tags,
prose, key, bpm, wsl_path, win_path). Incremental cache in <folder>/.timbre_cache/.
--sidecars also writes <file>.timbre.json next to each audio file; HDAW's
FileLibraryManager ingests tags/description/key/bpm/dsp from it on scan.

key/bpm: filename tag first (Am, F#m, C min, 128 BPM, 126_BPM ...), then
audio estimation (Krumhansl chroma match / onset tempo). Best-effort — fields
may be absent (key) or 0 (bpm) when nothing plausible is found.

Runtime: needs python with numpy/librosa/torch/transformers. Use the Windows
python (`py -3.14 "D:\...\timbre-lib\lib_analyze.py" "E:\samples\<pack>" --no-llm --sidecars`);
the retired WSL wrapper's venv/TIMBRE_PY indirection is gone — you pass the
interpreter yourself.

## Search (timbre layer)
    py -3.14 lib_search.py "dark gritty pad" [--limit N] [--min-dur S] [--max-dur S]
Default library is `timbre-lib/samples` (relative to lib_search.py); `--lib`
takes a folder or a timbre_index.json.
Scores: dsp_words x3, captions x2, tags x1, prose x1, filename x0.5.
Prints Windows paths ready for HDAW MCP.

## Synth probe analyzer (standalone, WAV-in)
    python analyze_probe.py <probe.wav> --role ROLE [--name N] [--plugin P]
        [--gguf PATH] [--no-clap] [--no-llm]

Evaluates a rendered VST/CLAP instrument probe against a psytrance production
role and prints one stable JSON report on stdout. Deterministic role checks
(`role_targets.py`) produce the verdict; the optional local LLM (Qwen via
`llm_stage.py`) only explains measured evidence and can never override a check.
No `.timbre.json` sidecars are written; `lib_analyze.py` and the library-index
workflow are untouched.

- Roles: `kick`, `bass`, `hat`, `snare`, `rim`, `clap`, `lead`, `arp`, `stab`,
  `pad`, `riser`, `fx` (aliases like `hihat`, `sub`, `sfx` accepted).
- Pipeline: validate path/role -> load 48k mono (librosa, scipy fallback) ->
  finite check -> DSP descriptors (`timbre.extract` + `spectral_evolution`) ->
  optional CLAP captions/tags -> optional LLM prose -> `check_role` +
  `build_recommendations`.
- Report: `{input, role, measurements, roleCheck, description,
  recommendations, warnings, error}`. Schema stays valid when CLAP/LLM are
  unavailable (explicit `warnings`). Recommendations are priority-ranked:
  `critical` (silence/clipping/non-finite/severely-wrong-register), `high`
  (failed role check), `medium` (pass near a threshold edge), `low` (creative
  variation).
- Exit 0 for any completed analysis (incl. critical findings); exit 1 for error
  reports (missing/malformed/unreadable/invalid role).
- Test suite (first pytest in repo): `python -m pytest test_analyze_probe.py -q`
  (23 tests: per-role pass/fail signals, bad-input schema, DSP-only fallback,
  stable JSON).

## Virus patch decoder + sub-synth survivability survey (standalone)

    python virus_patch.py --survey "D:\pdf\Virus Presets" --out virus_survey.json
    python virus_patch.py --dump <file.syx|.mid|.vhc|tdm-chunk>
    python virus_patch.py --sidecars "D:\pdf\Virus Presets" [--role bass]

Parses the four Access Virus patch containers found in the preset library
(B/C single sysex, TI bank, Digidesign TDM chunk, Std-MIDI bank, VHC bank),
extracts each patch name + parameters, maps onto the HDAW `sub_synth`
internal FX params (0-23), and reports what survives and what is dropped.
Pure stdlib + numpy; no librosa/ML deps; never writes into the source
library. Survey output goes to the `--out` path only.

- `--sidecars <dir>` writes `<patch>.virus.json` next to each patch file —
  the sidecar HDAW's `FileLibraryManager` ingests for `add_library(type="patch")`
  libraries (name, engine, format, bank/program, roleCheck, mappedParams,
  unmapped, description). With `--role`, `roleCheck` is computed over the
  mapped params as pseudo-measurements (patch bytes, not audio analysis — the
  sidecar carries an explicit `note` saying so). Stable JSON: re-runs are
  byte-identical.
- DX7 sidecars: `python dx7_patch.py --sidecars <dir>` writes
  `<patch>.dx7.json` (name, algorithm, feedback, full VCED params) for single
  (163B) and cartridge (4104B) `.syx` dumps. The `fm_synth` engine loads them
  via `fm_synth_import_sysex`.

- Formats: `bcsingle` (267B), `tibank` (128x524B), `tdm` (DigiVrusSS01
  chunk), `stdmidi` (run-length sysex unwrapped), `vhc` (128x267B).
- Checksum-verified against the documented Virus B/C SysEx spec
  (`(dev + 0x10 + bank + prog + sum(data)) & 0x7F`).
- Mapped: osc1/osc2 wave+level, osc2 detune->cents, sub level/octave,
  cutoff, resonance, drive, amp ADSR, output, legato, portamento, filter
  type, filter env amount + ADSR.
- Unmapped (reported explicitly, never silently dropped): osc2_fm_amount,
  ring_mod, lfo1, lfo2, keytrack, filter_slope_24db, osc_sync, fx_chorus,
  fx_delay, fx_reverb, mod_matrix, noise_level.
- Survey report schema:
  `{generated_at, sources, formats: {fmt: {files, patches, parsed, failed,
  name_ok, mapped_params: {avg,min,max}, top_unmapped}}, totals}`.
- Stable output: identical inputs produce byte-identical
  `json.dumps(report, sort_keys=True)`.
- Tests: `python -m pytest test_virus_patch.py -q` (per-format parsing on
  real fixtures, mapping contract, stable JSON, error paths, survey
  invariants, fx-pages CLI wiring).

## Virus FX / modulation-matrix page decoder (sidecarRev 2)

`virus_fx_pages.py` decodes the FX / mod-matrix bytes of every Access Virus
single dump into NAMED values using the gearmulator vocabularies
(`osTIrusJucePlugin/parameterDescriptions_TI.json` for TI pages 112-115,
`osirusJucePlugin/parameterDescriptions_C.json` for B/C pages 112-113;
payload offset = `(page - 112) * 128 + index`), and re-sweeps the sidecars
to rev 2 (every rev-1 FileLibraryManager key preserved + `sidecarRev`,
`fxModel`, `fxParams`, `fxCoverage`).

    python virus_fx_pages.py --sweep "D:\pdf\Virus Presets" [--role bass] [--verify-only]
    python virus_fx_pages.py --dump <file.syx|.mid|.vhc|tdm-chunk>
    python virus_patch.py --fx-pages "D:\pdf\Virus Presets" [--fx-verify-only]
    python virus_fx_pages.py --morphs --sheet matrix_presets/virus.json \
        --pairs 16:26,9:20,22:39,12:34,0:30 --steps 4 \
        --out matrix_presets/virus_morphs.json

- STOP-GATE (byte-match): every dump is parsed twice (virus_patch and this
  module's own container walker) and the payloads must agree; the vocab
  decode is then rebuilt into a payload and compared byte-for-byte. Corpus
  run 2026-09-17: 5,425 dumps (4,597 TI + 828 B/C), 1,782,572 values
  verified, 0 mismatches, 0 cross-parse disagreements -> PASS. The sweep
  exits 1 on any mismatch.
- Checksum rule, cited from the dump definitions in both vocab files
  (`{"type": "checksum", "first": 5, "last": ...}`):
  `(dev + 0x10 + bank + prog + sum(payload)) & 0x7F` at byte 265 (B/C) /
  522 (TI). 4,950/5,425 dumps carry ok checksums, 104 TDM chunks are
  checksum-NA, and 3 vendor banks (AZS Dream State .syx/.mid, Best Analog
  B/C .mid) store bytes matching no consistent formula while still decoding
  byte-exactly - reported, never gating.
- Anchors (handoff 2026-09-16): Assign1 Source/Destination = page 113 idx
  64/65, Assign2 Source 67, Assign3 Source 72, Assign1 Amount 66;
  Chorus/Type = TI page 112 idx 103; Ringmodulator Volume = B/C 112/38 and
  TI 112/50; Vocoder Mode = 113/39 (both), Vocoder/Carrier Center Frequency
  = TI 112/40. All asserted in `test_virus_fx_pages.py`.
- Coverage is honest: slots with no vocabulary entry stay unnamed holes
  (never invented); the sweep counts them and flags any nonzero hole byte
  (mainly TI pages 114/115 - real data, no public name in the json).
- The SMF walker is deliberately independent of virus_patch's
  `parse_stdmidi`; two real bugs it exposed are regression-tested (MThd
  length lives at offset 4; the 0xFF meta-TYPE byte precedes the length
  VLQ - files with an early port meta lost every message before the fix).
- Morph blueprints follow the repo convention: CONTINUOUS keys
  (amounts/levels/volumes/depths/rates/frequencies) interpolate, DISCRETE
  keys (types/sources/destinations/modes) anchor to A and are listed in
  `jumps`; steps shipped as `appliesVia: program_writer_pending` and were
  upgraded 2026-09-17 to per-step injectable SysEx by `virus_dump.py`
  (live A/B still blocked — finding F-A, see the toolchain section above).
- Tests: `python -m pytest test_virus_fx_pages.py -q` (anchors, checksum
  rule, byte-match, SMF regressions, sidecar contract, determinism, CLI,
  morph builder).

## Use in HDAW (MCP tools)
    1. add_library  {name, path: "D:\\...\\samples", type: "audio"}
    2. scan_library {id}                      # HDAW indexes files natively
    3. search_library {query}                 # filename/bpm/key search
    4. pick a file with lib_search.py, then:
       - sampler:   add_track -> add_fx {fxType:"sampler"}
                    -> sampler_set_sample {trackId, slotIndex, filePath}
       - audio:     add_audio_clip {trackId, start, length, sourceFile}

## Native integration (DONE — FileLibraryManager ingests sidecars)
HDAW's FileLibraryManager reads <file>.timbre.json sidecars during scan:
tags/description feed search() text search, and `key`/`bpm` (filename tag
first, else audio estimation) feed the key/BPM search filters. Rescans are
automatic when a sidecar is newer than its audio. Backfill key/bpm into
already-analyzed sidecars:  py -3.14 backfill_keybpm.py "<folder>" ...

## Manually add samples (daily use)
1. Copy WAV/FLAC/AIFF/MP3 files into `D:\pdf\roo projects\hdaw3\timbre-lib\samples\`
   (or any folder). Subfolders are scanned too.
2. Analyze (Windows python - the ML stack lives there, NOT the WSL agent venv):
   cmd> py -3.14 "D:\pdf\roo projects\hdaw3\timbre-lib\lib_analyze.py" "E:\samples\Some Pack" --no-llm --sidecars
   or a single file. Only new/changed files are processed (per-file cache).
   ~10-25s per file with CLAP; key/bpm-only backfill via backfill_keybpm.py is ~0.2s.
3. Search:          python lib_search.py "dark atmospheric pad"
4. Rescan in HDAW so its native index sees the new files:
   scan_library {libraryId: "f2538111f7cd"}   (or scan all)
5. Use in a track: sampler_set_sample / add_audio_clip (see above).
To re-analyze everything from scratch: delete the folder's .timbre_cache\ and re-run.

