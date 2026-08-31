#!/usr/bin/env python3
"""Psytrance packs on E:\\samples NOT covered by the 2026-08-27 batch
(analyze_psytrance.py): stride-sample each folder, run the DSP->CLAP->LLM
pipeline once (models loaded once), write per-file .timbre.json sidecars,
then register each folder as an HDAW audio library. Library names use the
same short-id convention as prior batches.
"""

import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lib_analyze as LA
import llm_stage as LS

FOLDERS = [
    ("/mnt/e/samples/Prism - Psytrance - Zenhiser", "Prism-Psytrance", 14),
    ("/mnt/e/samples/Zenhiser Studio Essentials - Psytrance", "Zenhiser-StudioPsy", 14),
    ("/mnt/e/samples/Zenhiser Atom Psytrance", "Zenhiser-Atom", 14),
    ("/mnt/e/samples/Zenhiser Disorder Psytrance", "Zenhiser-Disorder", 14),
    ("/mnt/e/samples/Antinomy Psytrance Sounds", "Antinomy-Psy-Vol1", 14),
    (
        "/mnt/e/samples/Sonicspore - CYBER FLOWS Psytrance Grids Collection",
        "Sonicspore-CyberFlows",
        14,
    ),
    (
        "/mnt/e/samples/Function Loops Psytrance Syndicate",
        "FunctionLoops-PsySyndicate",
        14,
    ),
    (
        "/mnt/e/samples/Ableton Live Psytrance Project - Full Night 3",
        "Ableton-FullNight3",
        14,
    ),
    ("/mnt/e/samples/Function Loops EDM x Psytrance", "FunctionLoops-EDMxPsy", 14),
    ("/mnt/e/samples/KICK-ASS PsyTrance Kicks", "KickAss-PsyKicks", 14),
    (
        "/mnt/e/samples/Psytrance Elements by Inside Mind Vol.1",
        "InsideMind-PsyVol1",
        14,
    ),
    (
        "/mnt/e/samples/Psytrance Elements by Inside Mind Vol.2",
        "InsideMind-PsyVol2",
        14,
    ),
    (
        "/mnt/e/samples/Function Loops - Free Psytrance Samples - June 21",
        "FunctionLoops-FreePsy",
        14,
    ),
    ("/mnt/e/samples/Kampfer Audio Psytrance Kicks 2", "Kampfer-PsyKicks2", 14),
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
        print(
            f"{name}: {len(got)} audio files, sampling {len(sel)} (strided)", flush=True
        )
        info.append((folder, name))
        files.extend(sel)
    print(f"total: {len(files)} files", flush=True)
    t0 = time.perf_counter()
    try:
        recs = LA.run_all(files, use_llm=True, sidecars=True)
    finally:
        LS.close()
    errs = {k: v for k, v in recs.items() if "error" in v}
    print(
        f"done: {len(recs)} records, {len(errs)} errors, {time.perf_counter() - t0:.0f}s",
        flush=True,
    )
    if errs:
        try:
            with open("/tmp/analyze_psy_batch2_errors.json", "w") as fh:  # noqa: S108 — /tmp path mandated by batch convention (sibling analyze_*.py); WSL-side script
                json.dump(errs, fh, indent=1)
        except OSError as e:
            print(f"warning: could not write error report: {e}", flush=True)
    for folder, name in info:
        subprocess.run(
            [
                sys.executable,
                os.path.join(HERE, "register_library.py"),
                "--path",
                folder,
                "--name",
                name,
            ],
            check=False,
        )


if __name__ == "__main__":
    main()
