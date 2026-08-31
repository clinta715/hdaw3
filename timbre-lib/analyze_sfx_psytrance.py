#!/usr/bin/env python3
"""SFX packs from E:\\samples\\_soundfx most relevant to psytrance sound design:
dark atmosphere, sci-fi, bio-organic textures, cinematic FX, feedback, designed FX.
Stride-sample each folder, run DSP->CLAP->LLM pipeline once, write sidecars,
register as HDAW audio libraries.
"""
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lib_analyze as LA  # noqa: E402 — must follow sys.path.insert
import llm_stage as LS  # noqa: E402

FOLDERS = [
    ("/mnt/e/samples/_soundfx/3100.Cinematic.Sound.Effects_", "Sfx-Cinematic3100", 12),
    ("/mnt/e/samples/_soundfx/Khron Studio Biofluid", "Sfx-Biofluid", 10),
    ("/mnt/e/samples/_soundfx/SampleTraxx Sonikscape WAV", "Sfx-Sonikscape", 10),
    ("/mnt/e/samples/_soundfx/ethereal2080 Sound Design Tools", "Sfx-Ethereal2080", 10),
    ("/mnt/e/samples/_soundfx/Dopamine Frame Sound Design Pack V1", "Sfx-Dopamine", 10),
    ("/mnt/e/samples/_soundfx/Hudsonfilms Sfx Pack Vol.3", "Sfx-Hudsonfilms", 10),
    ("/mnt/e/samples/_soundfx/FROGS", "Sfx-Frogs", 10),
    ("/mnt/e/samples/_soundfx/Sound Ideas - Extreme Sci Fi Sound Effects [Flac]", "Sfx-ExtremeSciFi", 10),
    ("/mnt/e/samples/_soundfx/Androids and Robots Sound Effects", "Sfx-AndroidsRobots", 10),
    ("/mnt/e/samples/_soundfx/Analog Samples Creepy Soundscapes", "Sfx-CreepyScapes", 10),
    ("/mnt/e/samples/_soundfx/Analog Samples Cinematic Horrors", "Sfx-CinematicHorrors", 10),
    ("/mnt/e/samples/_soundfx/Analog Samples Liminal Spaces", "Sfx-LiminalSpaces", 10),
    ("/mnt/e/samples/_soundfx/Analog Samples Feedback Frenzy", "Sfx-FeedbackFrenzy", 10),
    ("/mnt/e/samples/_soundfx/Analog Samples Deep Spaces", "Sfx-DeepSpaces", 10),
]


def stride_sample(files, limit):
    if len(files) <= limit:
        return files
    step = len(files) / limit
    return [files[min(int(i * step), len(files) - 1)] for i in range(limit)]


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
    print(
        f"done: {len(recs)} records, {len(errs)} errors, {time.perf_counter() - t0:.0f}s",
        flush=True,
    )
    if errs:
        try:
            with open("/tmp/analyze_sfx_errors.json", "w") as fh:  # noqa: S108 — /tmp convention (sibling analyze_*.py)
                json.dump(errs, fh, indent=1)
        except OSError as e:
            print(f"warning: could not write error report: {e}", flush=True)
    for folder, name in info:
        subprocess.run(
            [sys.executable, os.path.join(HERE, "register_library.py"),
             "--path", folder, "--name", name],
            check=False,
        )


if __name__ == "__main__":
    main()
