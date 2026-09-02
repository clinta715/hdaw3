
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
try:
    import llm_stage as LS
except ImportError:   # llama-cpp-python optional — --no-llm runs without it
    LS = None

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


# ── key / bpm estimation ─────────────────────────────────────────────────────
# Priority: filename tag (vendors tag most loops) -> audio analysis. Both are
# best-effort; failures return None/0 and the fields stay out of the record.

_NOTE_RE = r"[A-G](#|b|s)?"
# Filename-safe boundaries: \b fails next to underscores, which vendors love.
_L = r"(?<![A-Za-z0-9])"; _R = r"(?![A-Za-z0-9])"

def parse_key_from_name(name):
    """Filename key tag: 'F#m', 'Am', 'C min', 'G# Minor', 'Bb Maj' ...
    Spelling is preserved as written (G# stays G#, Bb stays Bb)."""
    m = re.search(_L + rf"({_NOTE_RE})\s*(minor|min)" + _R, name, re.I)
    if m: return _canon(m.group(1), True)
    m = re.search(_L + rf"({_NOTE_RE})\s*(major|maj)" + _R, name, re.I)
    if m: return _canon(m.group(1), False)
    m = re.search(_L + rf"({_NOTE_RE})m(?!aj)" + _R, name)   # tight: Am, F#m
    if m: return _canon(m.group(1), True)
    m = re.search(_L + rf"({_NOTE_RE})" + _R + r"(?![pP][mM])", name)  # bare = major (skip the B in BPM)
    if m: return _canon(m.group(1), False)
    return None

def parse_bpm_from_name(name):
    m = re.search(r"(?<!\d)(\d{2,3})[\s_]*bpm(?![A-Za-z0-9])", name, re.I)
    if m:
        v = float(m.group(1))
        if 40.0 <= v <= 220.0: return v
    return None

def _canon(note, minor):
    """Validate + normalize a note token; keeps #/b spelling, 's' -> '#'."""
    n = note.upper()
    if len(n) > 1:
        n = n[0] + ("#" if n[1] == "S" else "b" if n[1] == "B" else n[1:])
    table = {"C":0,"C#":1,"Db":1,"D":2,"D#":3,"Eb":3,"E":4,"F":5,"F#":6,
             "Gb":6,"G":7,"G#":8,"Ab":8,"A":9,"A#":10,"Bb":10,"B":11}
    if n not in table: return None
    return n + ("m" if minor else "")

def detect_key_audio(x, sr):
    """Chroma template match (Krumhansl-Kessler). Returns 'F#m'-style or None."""
    try:
        import numpy as np, librosa
        chroma = librosa.feature.chroma_cqt(y=x, sr=sr)
        pc = chroma.mean(axis=1); pc = pc / (pc.sum() + 1e-9)
        # Krumhansl-Kessler probe profiles (rotated per candidate root).
        maj = np.array([6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88])
        mn  = np.array([6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17])
        names = ["C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"]
        best, best_r = None, -2.0
        for r in range(12):
            v = np.roll(pc, -r)
            for prof, suffix in ((maj, ""), (mn, "m")):
                p = prof / prof.sum()
                rho = np.corrcoef(v, p)[0, 1]
                if rho > best_r:
                    best_r, best = rho, names[r] + suffix
        return best if best_r > 0.5 else None   # weak match -> unknown
    except Exception:
        return None

def detect_bpm_audio(x, sr):
    """Beat-track tempo estimate. Returns 0.0 when nothing plausible."""
    try:
        import librosa
        onset = librosa.onset.onset_strength(y=x, sr=sr)
        try:
            tempo = librosa.feature.rhythm.tempo(onset_envelope=onset, sr=sr)
        except AttributeError:
            tempo = librosa.beat.tempo(onset_envelope=onset, sr=sr)
        t = float(tempo[0]) if hasattr(tempo, "__len__") else float(tempo)
        if 40.0 <= t <= 220.0: return round(t, 2)
    except Exception:
        pass
    return 0.0

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
            # Backfill key/bpm for records analyzed before those fields existed.
            if "key" not in rec or "bpm" not in rec:
                try:
                    import librosa
                    base = os.path.splitext(os.path.basename(path))[0]
                    x, sr = librosa.load(path, sr=48000, mono=True)
                    rec.setdefault("key", None)
                    if not rec.get("key"):
                        rec["key"] = parse_key_from_name(base) or detect_key_audio(x, sr)
                    if not rec.get("bpm"):
                        rec["bpm"] = parse_bpm_from_name(base) or detect_bpm_audio(x, sr)
                    json.dump({k: v for k, v in rec.items() if k != "_cached"}, open(cpath, "w"))
                    if sidecars:
                        json.dump({k: v for k, v in rec.items() if k != "_cached"},
                                  open(path + ".timbre.json", "w"), indent=1)
                except Exception:
                    pass  # best-effort backfill — keep the cached record as-is
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
            base = os.path.splitext(os.path.basename(path))[0]
            rec["key"] = parse_key_from_name(base) or detect_key_audio(x, sr)
            rec["bpm"] = parse_bpm_from_name(base) or detect_bpm_audio(x, sr)
            d = TB.extract(x, sr)
            rec["dsp"] = {k: round(v, 6) if isinstance(v, (int, float)) else v for k, v in d.items()}
            rec["dsp_words"] = TB.summarize(d)
            aemb = CS.audio_embed(path)
            rec["captions"] = CS.rank_captions(aemb, CS._CAPTIONS, top_k=4)
            rec["tags"] = CS.tag_audioset(aemb, LABELS, top_k=6, threshold=0.04)
            if use_llm and LS is not None:
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
        recs = run_all(files, use_llm=not a.no_llm and LS is not None, sidecars=a.sidecars)
    finally:
        if LS is not None:
            LS.close()  # free the GGUF model while modules are still healthy
    base = a.folder if os.path.isdir(a.folder) else os.path.dirname(a.folder)
    out_path = a.out or os.path.join(base, "timbre_index.json")
    json.dump(recs, open(out_path, "w"), indent=1)
    print(f"done -> {out_path} ({len(recs)} entries)")
