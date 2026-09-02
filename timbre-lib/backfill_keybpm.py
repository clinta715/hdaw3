#!/usr/bin/env python3
"""Backfill `key` and `bpm` into existing .timbre.json sidecars.

For every audio file under the given roots that already has a sidecar:
  - key: filename tag first (Am / F#m / C min / G# Minor ...), else Krumhansl
    chroma template match; keeps vendor spelling.
  - bpm: filename tag first (128 BPM / 126_BPM ...), else onset tempo (40-220).
Sidecars that already carry both fields are skipped. Audio is loaded once per
file, only when a field is missing. Pure librosa — no CLAP/LLM.

Usage (Windows python):
    py -3.14 backfill_keybpm.py "E:/samples/Pack A" "E:/samples/Pack B" ...
"""
import json, os, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from lib_analyze import parse_key_from_name, parse_bpm_from_name, detect_key_audio, detect_bpm_audio

AUDIO_EXTS = {".wav", ".flac", ".aiff", ".aif", ".mp3", ".ogg"}


def collect(folder):
    found = []
    for r, _, fs in os.walk(folder):
        for f in fs:
            if os.path.splitext(f)[1].lower() in AUDIO_EXTS:
                found.append(os.path.join(r, f))
    return sorted(found)


def main(roots):
    files = []
    for root in roots:
        got = collect(root)
        print(f"{root}: {len(got)} audio files", flush=True)
        files.extend(got)

    with_sidecar = [f for f in files if os.path.exists(f + ".timbre.json")]
    print(f"{len(files)} audio files, {len(with_sidecar)} with sidecars", flush=True)

    done = skipped = failed = 0
    t0 = time.perf_counter()
    import librosa
    for i, path in enumerate(with_sidecar, 1):
        spath = path + ".timbre.json"
        try:
            with open(spath, encoding="utf-8") as fh:
                rec = json.load(fh)
            base = os.path.splitext(os.path.basename(path))[0]
            need_key = not rec.get("key")
            need_bpm = not rec.get("bpm")
            if not need_key and not need_bpm:
                skipped += 1
                continue
            key_fn = parse_key_from_name(base)
            bpm_fn = parse_bpm_from_name(base)
            x = sr = None
            # load audio once if any field still needs audio estimation
            if (need_key and not key_fn) or (need_bpm and not bpm_fn):
                x, sr = librosa.load(path, sr=48000, mono=True)
            if need_key and not rec.get("key"):
                rec["key"] = key_fn or (detect_key_audio(x, sr) if x is not None else None)
            if need_bpm and not rec.get("bpm"):
                rec["bpm"] = bpm_fn or (detect_bpm_audio(x, sr) if x is not None else 0.0)
            with open(spath, "w", encoding="utf-8") as fh:
                json.dump(rec, fh, indent=1)
            done += 1
            if done % 10 == 0 or i == len(with_sidecar):
                dt = time.perf_counter() - t0
                print(f"[{i}/{len(with_sidecar)}] done={done} skip={skipped} fail={failed} ({dt:.0f}s)", flush=True)
        except Exception:
            failed += 1
            print(f"FAIL {path}: {traceback.format_exc()[-200:]}", flush=True)
    print(f"ALL DONE: updated={done} skipped={skipped} failed={failed} in {time.perf_counter()-t0:.0f}s", flush=True)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: py -3.14 backfill_keybpm.py <folder> [<folder> ...]")
        sys.exit(1)
    main(sys.argv[1:])
