
#!/usr/bin/env python3
"""Search a timbre-analyzed sample library (timbre_index.json).
Usage:
    python lib_search.py "<query>" [--lib <folder-or-json>] [--limit N]
                          [--min-dur S] [--max-dur S] [--json]
Scoring: query tokens matched against
    dsp_words (x3), caption text (x2), tag text (x1), prose (x1), filename (x0.5).
Prints a ranked table: name | score | words | win_path.
"""
import argparse, json, os, re, sys

def load_index(lib):
    if os.path.isdir(lib):
        p = os.path.join(lib, "timbre_index.json")
        if not os.path.exists(p):
            for root, _, files in os.walk(lib):
                if "timbre_index.json" in files:
                    p = os.path.join(root, "timbre_index.json")
                    break
    else:
        p = lib
    return json.load(open(p))

TOKEN = re.compile(r"[a-z0-9']+")
WEIGHTS = [("dsp_words", 3.0), ("captions", 2.0), ("tags", 1.0), ("prose", 1.0), ("name", 0.5)]

def score(rec, toks):
    s = 0.0
    for field, w in WEIGHTS:
        text = rec.get(field)
        if not text:
            continue
        if isinstance(text, list):
            text = " ".join(x[0] if isinstance(x, (list, tuple)) else str(x) for x in text)
        text = text.lower()
        for t in toks:
            if t in text:
                s += w
    return s

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("query")
    ap.add_argument("--lib", default="/mnt/d/pdf/roo projects/hdaw3/timbre-lib/samples")
    ap.add_argument("--limit", type=int, default=8)
    ap.add_argument("--min-dur", type=float, default=None)
    ap.add_argument("--max-dur", type=float, default=None)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    index = load_index(a.lib)
    toks = [t for t in TOKEN.findall(a.query.lower()) if len(t) > 1]
    rows = []
    for name, rec in index.items():
        if rec.get("error"):
            continue
        if a.min_dur is not None and (rec.get("durationSeconds") or 0) < a.min_dur:
            continue
        if a.max_dur is not None and (rec.get("durationSeconds") or 0) > a.max_dur:
            continue
        rows.append((score(rec, toks), rec))
    rows.sort(key=lambda r: -r[0])
    rows = rows[: a.limit]
    if a.json:
        print(json.dumps([r[1] for r in rows], indent=1))
    else:
        for s, rec in rows:
            words = rec.get("dsp_words", "")
            cap = rec.get("captions", [["", 0]])[0][0] if rec.get("captions") else ""
            print(f"{s:5.1f}  {rec['name']:24s} {words[:60]:60s} | {rec['win_path']}")

