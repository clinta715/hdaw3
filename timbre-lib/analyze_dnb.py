#!/usr/bin/env python3
"""DnB/jungle packs added 2026-08-29 to E:\samples: stride-sample each folder,
run the DSP->CLAP->LLM pipeline once (models loaded once), write per-file
.timbre.json sidecars. Registration happens via MCP add_library (engine is
running; script registry writes would be clobbered). Run:
    python3 timbre-lib/analyze_dnb.py
"""
import json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lib_analyze as LA
import llm_stage as LS

FOLDERS = [
    ("/mnt/e/samples/100 Amen Breaks By Veak - Volume 2",   "AmenBreaks-Veak2",  14),
    ("/mnt/e/samples/Bass Shots",                            "DnB-BassShots",     12),
    ("/mnt/e/samples/Basses",                                "DnB-Basses",        14),
    ("/mnt/e/samples/Breaks",                                "DnB-Breaks",        12),
    ("/mnt/e/samples/Drums",                                 "DnB-Drums",         12),
    ("/mnt/e/samples/Drums - Full Drum Loops",               "DnB-FullDrumLoops", 12),
    ("/mnt/e/samples/Drums - HiHat Loops",                   "DnB-HiHatLoops",     5),
    ("/mnt/e/samples/Drums - Shaker Loops",                  "DnB-ShakerLoops",    5),
    ("/mnt/e/samples/Drums - Top Loops",                     "DnB-TopLoops",      12),
    ("/mnt/e/samples/Full_Drums",                            "DnB-FullDrums",     12),
    ("/mnt/e/samples/Hihat Loops",                           "DnB-HihatLoops",     4),
    ("/mnt/e/samples/Rumble Loops",                          "DnB-RumbleLoops",   10),
    ("/mnt/e/samples/drum_loops",                            "DnB-drum-loops",    12),
    ("/mnt/e/samples/lingoturbo Mini Drum Pack",             "DnB-Lingoturbo",    12),
    ("/mnt/e/samples/Perc",                                  "DnB-Perc",          12),
]

def stride_sample(files, limit):
    if len(files) <= limit: return files
    step = len(files) / limit
    return [files[int(i * step)] for i in range(limit)]

def main():
    files = []
    for folder, name, limit in FOLDERS:
        got = LA.collect(folder)
        sel = stride_sample(got, limit)
        print(f"[{name}] {len(got)} audio files, sampling {len(sel)} (strided)", flush=True)
        files.extend(sel)
    print(f"total: {len(files)} files", flush=True)
    t0 = time.perf_counter()
    try:
        recs = LA.run_all(files, use_llm=True, sidecars=True)
    finally:
        LS.close()
    errs = {k: v for k, v in recs.items() if "error" in v}
    print(f"done: {len(recs)} records, {len(errs)} errors, {time.perf_counter()-t0:.0f}s", flush=True)
    if errs:
        json.dump(errs, open("/tmp/analyze_dnb_errors.json", "w"), indent=1)

if __name__ == "__main__":
    main()
