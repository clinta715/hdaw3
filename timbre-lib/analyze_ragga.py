#!/usr/bin/env python3
"""Ragga/dub packs in E:\samples\_Reggaeton and Dancehall: analyze the ragga
essentials - chants, phrases (named with BPM+key), 808s, riddim stems, full dub
riddims, loops. Writes .timbre.json sidecars. Registration already done via MCP
(engine running). Run: python3 timbre-lib/analyze_ragga.py
"""
import glob, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lib_analyze as LA
import llm_stage as LS

BASE = "/mnt/e/samples/_Reggaeton and Dancehall"

def stride(files, limit):
    if len(files) <= limit: return files
    step = len(files) / limit
    return [files[int(i * step)] for i in range(limit)]

def main():
    picked = []
    # Ragga Stashkit Jayf: per-subfolder quotas (ragga essentials)
    sk = f"{BASE}/Ragga Stashkit Jayf"
    quotas = {"CHANTS": 13, "PHRASES": 15, "808": 10, "PERCS": 8, "CLAPS": 6,
              "SNARES": 6, "KICKS": 5, "HI HATS": 4, "OPENHATS": 4, "XTRAS": 8}
    for sub, limit in quotas.items():
        fs = sorted(glob.glob(f"{sk}/{sub}/*.mp3"))
        picked += stride(fs, limit)
    print(f"Ragga Stashkit: {len(picked)} selected", flush=True)
    for folder, name, limit in [
        (f"{BASE}/Full Dub Riddims Big Reggae Sample Pack",   "DubRiddims1", 14),
        (f"{BASE}/Full Dub Riddims Big Reggae Sample Pack 2", "DubRiddims2", 12),
        (f"{BASE}/Rasta Vibrations 6",                        "RastaVibes6", 40),
        (f"{BASE}/Loops",                                     "RaggaLoops",  12),
    ]:
        got = LA.collect(folder)
        sel = stride(got, limit)
        print(f"{name}: {len(got)} mp3, sampling {len(sel)}", flush=True)
        picked += sel
    print(f"total: {len(picked)} files", flush=True)
    t0 = time.perf_counter()
    try:
        recs = LA.run_all(picked, use_llm=True, sidecars=True)
    finally:
        LS.close()
    errs = {k: v for k, v in recs.items() if "error" in v}
    print(f"done: {len(recs)} records, {len(errs)} errors, {time.perf_counter()-t0:.0f}s", flush=True)
    if errs:
        json.dump(errs, open("/tmp/analyze_ragga_errors.json", "w"), indent=1)

if __name__ == "__main__":
    main()
