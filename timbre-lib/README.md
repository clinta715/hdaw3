
# TimbreLib ??? analyzed sample library for HDAW

Pipeline: DSP descriptors -> CLAP captions + AudioSet tags -> Qwen2.5-3B prose.
All local. Toolchain: python 3.11 venv (torch cu128, transformers, librosa,
scipy, llama-cpp-python CPU wheel), GGUF at ./Qwen2.5-3B-Instruct-Q4_K_M.gguf.

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

