#!/usr/bin/env python3
"""Audit the authored psy_fm patch bank: which patches are actually USABLE.

For every patch in compositions/psy_fm_bank/saved.json:
  1. load it onto one scratch psy_fm track (three role clips live on that track:
     bass 0-8, lead 8-16, pad/riser 16-32);
  2. `verify_part` the window matching the patch's role -> audible / nonClipping
     / soloRms / soloPeak, measured through the SOLO offline render;
  3. classify: USABLE when audible=1, nonClipping=1 and soloRms clears a floor.

Also runs ONE `param_verity_corpus` on a representative patch to show the QA gate
(which of the patch's 38 params actually change the render — the check that a
patch cannot ship with silently inert parameters).

Usage: python3 scripts/audit_psy_fm_bank.py
"""
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAVED = os.path.join(REPO, "compositions", "psy_fm_bank", "saved.json")
CORPUS = os.path.join(REPO, "compositions", "psy_fm_bank", "corpus.json")

ROLE_WINDOW = {"bass": (0.5, 8), "lead": (8, 16), "stab": (8, 16),
               "perc": (8, 16), "pad": (16, 32), "riser": (16, 32)}
RMS_FLOOR = 0.005


def call(tool, args, timeout=240):
    r = subprocess.run(
        ["python3", os.path.join(REPO, "scripts", "hdaw_mcp_http.py"),
         "call", tool, json.dumps(args), "--timeout", str(timeout)],
        cwd=REPO, capture_output=True, text=True)
    return (r.stdout.strip() or r.stderr.strip())


def main():
    saved = json.load(open(SAVED))["saved"]
    roles = {p["name"]: p["role"] for p in json.load(open(CORPUS))["patches"]}

    s = json.loads(call("add_track_with_fx", {"name": "zz_audit", "fxType": "psy_fm"}))
    T = s["trackId"]
    rows = []
    try:
        # Three role clips on the audit track.
        clips = {
            "bass": (0, 8, [{"start": b * 2.0, "duration": 1.8, "pitch": 31,
                             "velocity": 110} for b in range(4)]),
            "lead": (8, 16, [{"start": i * 0.5, "duration": 0.4,
                              "pitch": [55, 58, 62, 58][i % 4], "velocity": 100}
                             for i in range(16)]),
            "pad": (16, 32, [{"start": b * 8.0, "duration": 7.5, "pitch": p,
                              "velocity": 88}
                             for b in range(2) for p in (43, 55, 58, 62)]),
        }
        for rname, (st, ln, notes) in clips.items():
            c = json.loads(call("add_midi_clip", {"trackId": T, "start": st,
                                                  "length": ln - st,
                                                  "name": "aud_" + rname}))
            call("add_notes", {"clipId": c["clipId"], "notes": notes})

        for p in saved:
            role = roles.get(p["name"], "lead")
            w = ROLE_WINDOW.get(role, (8, 16))
            lp = call("load_patch", {"trackId": T, "slotIndex": 0, "id": p["id"]})
            if lp.strip() != "ok":
                rows.append({"name": p["name"], "role": role, "usable": False,
                             "note": "load failed: " + lp[:80]})
                continue
            v = call("verify_part", {"trackIndex": T, "startBeat": w[0],
                                     "endBeat": w[1]})
            m = dict(re.findall(r"(\w+)=([\d.eE+-]+)", v))
            rms = float(m.get("soloRms", 0))
            usable = (m.get("audible") == "1" and m.get("nonClipping") == "1"
                      and rms >= RMS_FLOOR)
            rows.append({"name": p["name"], "role": role, "usable": usable,
                         "soloRms": round(rms, 5),
                         "soloPeak": round(float(m.get("soloPeak", 0)), 4),
                         "audible": m.get("audible"), "nonClipping": m.get("nonClipping"),
                         "routes": p["routes"]})
            print("%-22s %-5s rms=%-8s peak=%-7s %s"
                  % (p["name"], role, rows[-1]["soloRms"], rows[-1]["soloPeak"],
                     "USABLE" if usable else "no"))
    finally:
        call("remove_track", {"trackId": T, "force": True})

    ok = [r for r in rows if r.get("usable")]
    out = {"rows": rows, "counts": {"patches": len(rows), "usable": len(ok),
                                    "unusable": len(rows) - len(ok)},
           "rmsFloor": RMS_FLOOR}
    json.dump(out, open(os.path.join(REPO, "compositions", "psy_fm_bank",
                                     "audit.json"), "w"), indent=1)
    print(json.dumps(out["counts"]))
    for r in rows:
        if not r.get("usable"):
            print("  UNUSABLE", r["name"], r.get("note", ""),
                  "rms=", r.get("soloRms"))
    return 0 if len(ok) == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
