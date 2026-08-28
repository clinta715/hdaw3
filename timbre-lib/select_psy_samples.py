#!/usr/bin/env python3
"""Select psytrance samples for render deliverables FROM THE HDAW LIBRARIES.

Reads the HDAW library registry (the 8 new psytrance packs registered via
the timbre-lib pipeline), enumerates the ANALYZED set (per-file .timbre.json
sidecars written by lib_analyze.py), classifies each file into a production
role from its DSP descriptors + CLAP tags (kick / bass / lead / hat / pad),
and writes timbre-lib/psy_sample_selection.tsv (role TAB win_path per line).

Run: python3 select_psy_samples.py
"""
import json, os, re, subprocess, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
PSY = {"Ascend-Psytrance", "Avalon-Psytrance", "Batuhan-Psy-Fundamentals",
       "FLOW36-Psytrance", "Hipotermic-PsyTrance", "Hypnoticum-PsyTrance",
       "SantoGrau-DarkPsy", "TerraTech-Psytrance", "Antinomy-Psy-Vol2"}

def default_registry():
    try:
        out = subprocess.run(["cmd.exe", "/c", "echo", "%APPDATA%"],
                             capture_output=True, text=True, timeout=15).stdout.strip()
        m = re.match(r"^([A-Z]):\\(.*)$", out)
        if m:
            # normalize backslashes for the WSL mount (register_library.py bug class)
            return f"/mnt/{m.group(1).lower()}/{m.group(2).replace(chr(92),'/')}/HDAW/libraries/registry.json"
    except Exception:
        pass
    hits = glob_hits if False else []
    return None

import glob as _glob
def _hits():
    return _glob.glob("/mnt/c/Users/*/AppData/Roaming/HDAW/libraries/registry.json")

def load_libs():
    reg = default_registry()
    if not reg and os.path.exists("/mnt/c/Users/hapbt/AppData/Roaming/HDAW/libraries/registry.json"):
        reg = "/mnt/c/Users/hapbt/AppData/Roaming/HDAW/libraries/registry.json"
    if not reg:
        reg = _hits()[0] if _hits() else None
    if not reg:
        print("registry not found", file=sys.stderr); sys.exit(1)
    data = json.load(open(reg, encoding="utf-8"))
    libs = []
    for l in data.get("libraries", []):
        if l.get("type") != "audio": continue
        name = l.get("name", "")
        if name in PSY:
            libs.append(l)
    return reg, libs

def win_to_wsl(p):
    m = re.match(r"^([A-Z]):\\(.*)$", p.replace("/", "\\"))
    return f"/mnt/{m.group(1).lower()}/{m.group(2).replace(chr(92),'/')}" if m else p

def classify(rec):
    """role from sidecar DSP + CLAP tags; returns one of kick/bass/lead/hat/pad/other"""
    d = rec.get("dsp", {})
    dur = rec.get("durationSeconds", 0)
    cent = d.get("centroid", 0)
    crest = d.get("spectral_crest", 0)
    flat = d.get("flatness", 1)
    zcr = d.get("zcr", 0)
    att = d.get("attack_s", 0)
    name = (rec.get("name") or "").lower()
    tags = " ".join(t for t, s in (rec.get("tags") or []) if s > 0.25).lower()

    # Pack filenames are reliable, so NAME keywords win first, then CLAP tags,
    # then DSP gates.
    if any(k in name for k in ("kick", "kik", "boom")): return "kick"
    if any(k in name for k in ("bass", "sub", "808")): return "bass"
    if any(k in name for k in ("lead", "stab", "arp", "pluck", "chord", "melody", "riff", "pad", "atmos", "ambient", "drone", "textur")):
        return "lead" if any(k in name for k in ("lead", "stab", "arp", "pluck", "chord", "melody", "riff")) else "pad"
    if any(k in name for k in ("hat", "shaker", "snare", "clap", "perc", "rex", "cymbal", "crash", "ride")):
        return "hat"
    # CLAP/tag short-circuits. NOTE: "bass drum" is the KICK tag - match it
    # before the generic "bass" key, otherwise every kick becomes a bassline.
    if any(k in tags for k in ("hi-hat", "hat", "shaker", "snap", "rimshot", "clap", "snare")):
        return "hat" if dur <= 1.0 else "other"
    if "kick" in tags or "bass drum" in tags or "boom" in tags:
        return "kick"
    if dur < 0.7 and cent < 3200 and att < 0.06: return "kick"
    if "bass" in tags: return "bass"
    if "percussion" in tags and dur <= 1.0: return "hat"
    if dur <= 0.4 and cent > 3800: return "hat"
    if 0.6 <= dur <= 8 and cent < 1900 and zcr < 0.08: return "bass"
    if dur >= 4.0 and (flat > 0.4 or cent < 7000): return "pad"
    if 0.3 <= dur <= 4.0 and crest > 0.2: return "lead"
    return "other"

def main():
    reg, libs = load_libs()
    print(f"registry: {reg}")
    print(f"psytrance libraries in registry: {len(libs)}")
    by_lib = defaultdict(list)
    for lib in libs:
        root = win_to_wsl(lib["path"])
        if not os.path.isdir(root):
            print(f"  WARN: library path missing: {lib['path']}")
            continue
        for dp, _, fs in os.walk(root):
            for f in fs:
                if f.endswith(".timbre.json"):
                    try:
                        rec = json.load(open(os.path.join(dp, f), encoding="utf-8"))
                    except Exception:
                        continue
                    by_lib[lib["name"]].append(rec)
    total = sum(len(v) for v in by_lib.values())
    print(f"analyzed sidecars found: {total} across {len(by_lib)} libraries")

    roles = defaultdict(list)
    stats = defaultdict(int)
    for lib, recs in by_lib.items():
        for rec in recs:
            role = classify(rec)
            stats[role] += 1
            win = rec.get("win_path") or rec.get("wsl_path")
            if not win or not os.path.exists(win_to_wsl(win)):
                continue
            roles[role].append((lib, win, rec.get("name", "")))
    print("classification:", dict(stats))

    want = {"kick": 2, "bass": 3, "lead": 4, "hat": 3, "pad": 3}
    out = []
    for role, n in want.items():
        pool = roles.get(role, [])
        if len(pool) < n:
            print(f"  WARN: {role} has only {len(pool)} (wanted {n})")
        # spread across libraries: round-robin-ish by taking strided picks
        step = (len(pool) / n) if pool else 1
        picks = [pool[int(i * step)] if pool else None for i in range(n) if pool]
        for lib, win, name in picks:
            out.append((role, win, lib, name))

    tsv = os.path.join(HERE, "psy_sample_selection.tsv")
    with open(tsv, "w", encoding="utf-8") as f:
        for role, win, lib, name in out:
            f.write(f"{role}\t{win}\t{lib}\t{name}\n")
    print(f"\nwrote {len(out)} selections -> {tsv}")
    from collections import Counter
    for role, cnt in Counter(r for r, *_ in out).items():
        print(f"  {role}: {cnt}")

if __name__ == "__main__":
    main()
