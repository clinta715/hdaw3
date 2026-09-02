
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

