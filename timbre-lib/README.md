
# TimbreLib ??? analyzed sample library for HDAW

Pipeline: DSP descriptors -> CLAP captions + AudioSet tags -> Qwen2.5-3B prose.

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

## Analyze a folder
    ./analyze.sh <folder> [--limit N] [--no-llm] [--sidecars]
(plain `python` on this WSL box has no ML toolchain; use ./analyze.sh, or set
TIMBRE_PY to a venv python that has librosa/torch/transformers/llama-cpp)
Writes <folder>/timbre_index.json (per-file: dsp, dsp_words, captions, tags,
prose, key, bpm, wsl_path, win_path). Incremental cache in <folder>/.timbre_cache/.
--sidecars also writes <file>.timbre.json next to each audio file; HDAW's
FileLibraryManager ingests tags/description/key/bpm/dsp from it on scan.

key/bpm: filename tag first (Am, F#m, C min, 128 BPM, 126_BPM ...), then
audio estimation (Krumhansl chroma match / onset tempo). Best-effort — fields
may be absent (key) or 0 (bpm) when nothing plausible is found.

Runtime: needs python with numpy/librosa/torch/transformers. The WSL
prime-agent kernel venv is NOT for this — use the Windows python
(`py -3.14 "D:\...\timbre-lib\lib_analyze.py" "E:\samples\<pack>" --no-llm --sidecars`)
or point TIMBRE_PY at a venv that has the stack.

## Search (timbre layer)
    ./search.sh "dark gritty pad" [--limit N] [--min-dur S] [--max-dur S]
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
- Tests: `python -m pytest test_virus_patch.py -q` (34 tests: per-format
  parsing on real fixtures, mapping contract, stable JSON, error paths,
  survey invariants).

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

