
# TimbreLib ??? analyzed sample library for HDAW

Pipeline: DSP descriptors -> CLAP captions + AudioSet tags -> Qwen2.5-3B prose.
All local. Toolchain: python 3.11 venv (torch cu128, transformers, librosa,
scipy, llama-cpp-python CPU wheel), GGUF at ./Qwen2.5-3B-Instruct-Q4_K_M.gguf.

## Analyze a folder
    ./analyze.sh <folder> [--limit N] [--no-llm] [--sidecars]
(plain `python` on this WSL box has no ML toolchain; use ./analyze.sh, or set
TIMBRE_PY to a venv python that has librosa/torch/transformers/llama-cpp)
Writes <folder>/timbre_index.json (per-file: dsp, dsp_words, captions, tags,
prose, wsl_path, win_path). Incremental cache in <folder>/.timbre_cache/.
--sidecars also writes <file>.timbre.json next to each audio file (for the
planned native HDAW FileLibraryManager sidecar ingestion).

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

## Next step (native integration)
Extend FileLibraryManager: read <file>.timbre.json sidecars during scan,
add tags/description to LibraryEntry, match them in search() text search,
surface in search_library results. Requires hdaw-guard skill + rebuild.

## Manually add samples (daily use)
1. Copy WAV/FLAC/AIFF/MP3 files into `D:\pdf\roo projects\hdaw3\timbre-lib\samples\`
   (or any folder). Subfolders are scanned too.
2. Analyze (WSL):   python lib_analyze.py "D:\pdf\roo projects\hdaw3\timbre-lib\samples"
   or a single file: python lib_analyze.py "path\\to\\file.wav"
   Only new/changed files are processed (per-file cache). ~25s per file.
3. Search:          python lib_search.py "dark atmospheric pad"
4. Rescan in HDAW so its native index sees the new files:
   scan_library {libraryId: "f2538111f7cd"}   (or scan all)
5. Use in a track: sampler_set_sample / add_audio_clip (see above).
To re-analyze everything from scratch: delete the folder's .timbre_cache\ and re-run.

