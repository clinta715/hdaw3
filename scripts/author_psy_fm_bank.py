#!/usr/bin/env python3
"""Author the psy_fm patch bank through save_patch.

Reads compositions/psy_fm_bank/corpus.json, and for each patch:
  1. sets a scratch psy_fm slot's params (batched, real units) + its mod routes,
  2. `save_patch` -> persisted into ~/.config/HDAW/patches/,
  3. records the returned id.
Then VERIFIES the round-trip: for every patch, load it onto a second scratch
slot and compare params + psy_fm_matrix against the source. A patch whose
loaded state differs is reported FAILED (never silently accepted).

Scratch tracks are removed at the end; the session project is left as found.

Usage: python3 scripts/author_psy_fm_bank.py [--verify-only]
"""
import json
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS = os.path.join(REPO, "compositions", "psy_fm_bank", "corpus.json")


def call(tool, args, timeout=180):
    r = subprocess.run(
        ["python3", os.path.join(REPO, "scripts", "hdaw_mcp_http.py"),
         "call", tool, json.dumps(args), "--timeout", str(timeout)],
        cwd=REPO, capture_output=True, text=True)
    return (r.stdout.strip() or r.stderr.strip())


def slots(ti):
    return json.loads(call("list_fx", {"trackId": ti}))


def fxparams(ti, si):
    d = json.loads(call("list_fx_params", {"trackId": ti, "slotIndex": si}))
    return {p["index"]: p["value"] for p in d["params"]}


def routes(ti, si):
    d = json.loads(call("psy_fm_mod_matrix_debug", {"trackId": ti, "slotIndex": si}))
    return sorted((r["source"], r["dest"], round(float(r["depth"]), 4))
                  for r in d.get("routes", []))


def main():
    verify_only = "--verify-only" in sys.argv
    corpus = json.load(open(CORPUS))
    patches = corpus["patches"]

    src = json.loads(call("add_track_with_fx", {"name": "zz_bank_src", "fxType": "psy_fm"}))
    dst = json.loads(call("add_track_with_fx", {"name": "zz_bank_dst", "fxType": "psy_fm"}))
    S, D = src["trackId"], dst["trackId"]
    saved, failed = [], []

    try:
        for p in patches:
            name = p["name"]
            if not verify_only:
                # reset slot 0 to the routing's defaults, then write this patch
                call("psy_fm_load_preset", {"trackId": S, "slotIndex": 0,
                                            "preset": p.get("preset", "growlBass")})
                call("psy_fm_clear_mod_matrix", {"trackId": S, "slotIndex": 0})
                writes = [{"trackId": S, "slotIndex": 0, "paramIndex": int(k),
                           "value": float(v), "mode": "real"}
                          for k, v in p["params"].items()]
                res = call("set_fx_params", {"mode": "real", "writes": writes})
                # Parse, never string-grep: the payload is compact JSON
                # ({"errors":[],"failed":0,...}) and a spaced pattern like
                # '"failed": 0' never matches it.
                try:
                    bad = json.loads(res).get("failed", 1)
                except Exception:
                    bad = 1
                if bad:
                    failed.append((name, "param write: " + res[:160]))
                    continue
                for r in p.get("routes", []):
                    rr = call("psy_fm_set_mod_route",
                              {"trackId": S, "slotIndex": 0, "source": r["source"],
                               "dest": r["dest"], "depth": r["depth"]})
                    if rr.strip() != "ok":
                        failed.append((name, "route: " + rr[:120]))
                sp = call("save_patch", {"trackId": S, "slotIndex": 0, "name": name})
                if sp.startswith("ERROR") or '"id"' not in sp:
                    failed.append((name, "save: " + sp[:160]))
                    continue
                pid = json.loads(sp)["id"]
            else:
                pid = "user/%s.json" % name

            # --- round-trip verification: load onto D and compare ---
            lp = call("load_patch", {"trackId": D, "slotIndex": 0, "id": pid})
            if lp.strip() != "ok":
                failed.append((name, "load: " + lp[:160]))
                continue
            sp_p, dp_p = fxparams(S, 0), fxparams(D, 0)
            sp_r, dp_r = routes(S, 0), routes(D, 0)
            diff = {k: (sp_p.get(k), dp_p.get(k)) for k in set(sp_p) | set(dp_p)
                    if abs(float(sp_p.get(k, 0)) - float(dp_p.get(k, 0))) > 1e-6}
            if diff:
                failed.append((name, "param mismatch: %s" % dict(list(diff.items())[:6])))
            elif sp_r != dp_r:
                failed.append((name, "route mismatch: %s vs %s" % (sp_r, dp_r)))
            else:
                saved.append({"name": name, "id": pid, "role": p["role"],
                              "routes": len(dp_r), "params": len(dp_p)})
            print("%-24s %s" % (name, "OK" if saved and saved[-1]["name"] == name else "FAIL"))
    finally:
        call("remove_track", {"trackId": D})
        call("remove_track", {"trackId": S})

    out = {"saved": saved, "failed": failed,
           "counts": {"patches": len(patches), "ok": len(saved), "failed": len(failed)}}
    json.dump(out, open(os.path.join(REPO, "compositions", "psy_fm_bank", "saved.json"), "w"),
              indent=1)
    print(json.dumps(out["counts"]))
    for f in failed:
        print("  FAILED", f[0], "-", f[1][:200])
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
