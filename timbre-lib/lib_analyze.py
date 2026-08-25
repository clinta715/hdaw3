
#!/usr/bin/env python3
"""Build/search a timbre-analyzed sample library for HDAW.
Usage:
    python lib_analyze.py <folder> [--limit N] [--no-llm] [--sidecars]
Reads every audio file in <folder> (recursive), runs:
    stage 1: DSP descriptors           (timbre.py, numpy/scipy, fast)
    stage 2: CLAP captions + tags      (CUDA CLAP via transformers)
    stage 3: LLM prose                 (Qwen2.5-3B GGUF via llama.cpp CPU)
Incremental cache: <folder>/.timbre_cache/<md5>.json (skips unchanged files).
Writes <folder>/timbre_index.json with per-file records including both
WSL and Windows paths (so HDAW MCP tools on the Windows side can use them).
With --sidecars, also writes <file>.timbre.json next to each audio file
(for the planned native HDAW FileLibraryManager sidecar ingestion).
"""
import argparse, hashlib, json, os, re, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import timbre as TB
import clap_stage as CS
import llm_stage as LS

AUDIO_EXTS = {".wav", ".flac", ".aiff", ".aif", ".mp3", ".ogg"}
GGUF = os.path.join(HERE, "Qwen2.5-3B-Instruct-Q4_K_M.gguf")
LABELS = os.path.join(HERE, "audioset_labels.txt")

def wsl_to_win(p):
    # /mnt/d/... -> D:\...
    m = re.match(r"^/mnt/([a-zA-Z])/(.*)$", p)
    if m:
        return f"{m.group(1).upper()}:\\" + m.group(2).replace("/", "\\")
    return p.replace("/", "\\")

def file_key(path):
    st = os.stat(path)
    return hashlib.md5(f"{path}:{st.st_size}:{int(st.st_mtime)}".encode()).hexdigest()

def run_all(files, use_llm=True, sidecars=False):
    cache_dir = os.path.join(os.path.dirname(os.path.abspath(files[0])) or ".", ".timbre_cache")
    os.makedirs(cache_dir, exist_ok=True)
    out = {}
    t0 = time.perf_counter()
    for i, path in enumerate(files, 1):
        name = os.path.splitext(os.path.basename(path))[0]
        key = file_key(path)
        cpath = os.path.join(cache_dir, key + ".json")
        rec = None
        if os.path.exists(cpath):
            _c = json.load(open(cpath))
            if "error" not in _c:
                rec = _c
        if rec is not None:
            rec["_cached"] = True
            out[name] = rec
            if sidecars and not os.path.exists(path + ".timbre.json"):
                json.dump(rec, open(path + ".timbre.json", "w"), indent=1)
            print(f"[{i}/{len(files)}] {name}: cached", flush=True)
            continue
        rec = {"name": name, "wsl_path": path, "win_path": wsl_to_win(path),
               "size": os.path.getsize(path),
               "mtime": int(os.path.getmtime(path)),
               "durationSeconds": None, "sampleRate": None, "channels": None,
               "format": os.path.splitext(path)[1].lstrip(".").lower()}
        try:
            import numpy as np, librosa
            x, sr = librosa.load(path, sr=48000, mono=True)
            rec["durationSeconds"] = round(len(x) / sr, 3)
            rec["sampleRate"] = sr
            rec["channels"] = 1
            d = TB.extract(x, sr)
            rec["dsp"] = {k: round(v, 6) if isinstance(v, (int, float)) else v for k, v in d.items()}
            rec["dsp_words"] = TB.summarize(d)
            aemb = CS.audio_embed(path)
            rec["captions"] = CS.rank_captions(aemb, CS._CAPTIONS, top_k=4)
            rec["tags"] = CS.tag_audioset(aemb, LABELS, top_k=6, threshold=0.04)
            if use_llm:
                ev = LS.build_evidence(rec["dsp_words"], rec["tags"], rec["captions"])
                rec["prose"] = LS.run_llm(ev, GGUF, max_tokens=220, temperature=0.4)
            if sidecars:
                json.dump(rec, open(path + ".timbre.json", "w"), indent=1)
            json.dump(rec, open(cpath, "w"))
        except Exception:
            rec["error"] = traceback.format_exc()[-500:]
        out[name] = rec
        dt = time.perf_counter() - t0
        print(f"[{i}/{len(files)}] {name}: {rec.get('prose', rec.get('error','ERR'))[:70]} ({dt:.0f}s)", flush=True)
    return out

def collect(folder):
    found = []
    for r, _, fs in os.walk(folder):
        for f in fs:
            if os.path.splitext(f)[1].lower() in AUDIO_EXTS:
                found.append(os.path.join(r, f))
    return sorted(found)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", help="folder to scan, or a single audio file")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--no-llm", action="store_true")
    ap.add_argument("--sidecars", action="store_true")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    files = [a.folder] if os.path.isfile(a.folder) else collect(a.folder)
    if a.limit:
        files = files[:a.limit]
    print(f"{len(files)} files", flush=True)
    try:
        recs = run_all(files, use_llm=not a.no_llm, sidecars=a.sidecars)
    finally:
        LS.close()  # free the GGUF model while modules are still healthy
    base = a.folder if os.path.isdir(a.folder) else os.path.dirname(a.folder)
    out_path = a.out or os.path.join(base, "timbre_index.json")
    json.dump(recs, open(out_path, "w"), indent=1)
    print(f"done -> {out_path} ({len(recs)} entries)")
