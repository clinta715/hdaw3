#!/usr/bin/env python3
"""Psytrance packs added to E:\\samples on 2026-08-27: stride-sample each
folder, run the DSP->CLAP->LLM pipeline once (models loaded once), write
per-file .timbre.json sidecars, then register each folder as an HDAW audio
library. Library names use the same short-id convention as prior batches.
"""
import json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lib_analyze as LA
import llm_stage as LS

FOLDERS = [
    ("/mnt/e/samples/Ascend Psytrance Sample Pack",                            "Ascend-Psytrance",         14),
    ("/mnt/e/samples/Avalon Psytrance Sample Pack",                            "Avalon-Psytrance",         14),
    ("/mnt/e/samples/Batuhan Pehlivan Psytrance Fundamentals Sample Library",  "Batuhan-Psy-Fundamentals", 12),
    ("/mnt/e/samples/FLOW36 Psytrance Sample pack Loops 2025",                 "FLOW36-Psytrance",         12),
    ("/mnt/e/samples/Hipotermic PsyTrance Sample Pack",                        "Hipotermic-PsyTrance",     14),
    ("/mnt/e/samples/Hypnoticum PsyTrance",                                    "Hypnoticum-PsyTrance",     14),
    ("/mnt/e/samples/Santo Grau Records WS Dark Psytrance Sample Pack #2",     "SantoGrau-DarkPsy",        12),
    ("/mnt/e/samples/TerraTech Forest Psytrance Sample Pack Vol.1",            "TerraTech-Psytrance",      12),
]

def stride_sample(files, limit):
    if len(files) <= limit:
        return files
    step = len(files) / limit
    return [files[int(i * step)] for i in range(limit)]

def main():
    files = []
    info = []
    for folder, name, limit in FOLDERS:
        got = LA.collect(folder)
        sel = stride_sample(got, limit)
        print(f"{name}: {len(got)} audio files, sampling {len(sel)} (strided)", flush=True)
        info.append((folder, name))
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
        json.dump(errs, open("/tmp/analyze_psytrance_errors.json", "w"), indent=1)
    for folder, name in info:
        subprocess.run([sys.executable, os.path.join(HERE, "register_library.py"),
                        "--path", folder, "--name", name], check=False)

if __name__ == "__main__":
    main()
