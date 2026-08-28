#!/usr/bin/env python3
"""Multi-folder analyze driver: loads the CLAP/LLM models ONCE and analyzes
a bounded, evenly-strided sample of audio files from several folders,
writing per-file .timbre.json sidecars. Batch 2: 8 more d:/projects folders.
"""
import json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lib_analyze as LA
import llm_stage as LS

# folder (WSL), library name, per-folder limit
FOLDERS = [
    ("/mnt/d/projects/042119",   "projects-042119",  14),
    ("/mnt/d/projects/102599",   "projects-102599",  14),
    ("/mnt/d/projects/123115",   "projects-123115",  12),
    ("/mnt/d/projects/112102",   "projects-112102",  12),
    ("/mnt/d/projects/090916",   "projects-090916",  12),
    ("/mnt/d/projects/061410",   "projects-061410",  12),
    ("/mnt/d/projects/050703",   "projects-050703",  12),
    ("/mnt/d/projects/010514",   "projects-010514",  12),
]

def stride_sample(files, limit):
    if len(files) <= limit:
        return files
    step = len(files) / limit
    return [files[int(i * step)] for i in range(limit)]

def main():
    files = []
    for folder, name, limit in FOLDERS:
        got = LA.collect(folder)
        sel = stride_sample(got, limit)
        print(f"{name}: {len(got)} audio files, sampling {len(sel)} (strided)")
        files.extend(sel)
    print(f"total: {len(files)} files", flush=True)
    t0 = time.perf_counter()
    try:
        recs = LA.run_all(files, use_llm=True, sidecars=True)
    finally:
        LS.close()
    errs = {k: v for k, v in recs.items() if "error" in v}
    print(f"done: {len(recs)} records, {len(errs)} errors, {time.perf_counter()-t0:.0f}s")
    if errs:
        json.dump(errs, open("/tmp/analyze_batch2_errors.json", "w"), indent=1)

if __name__ == "__main__":
    main()
