
"""verify_roundtrip.py — compare injected vs HDAW-recaptured plugin states.

Reads two .hdaw3 project files, decodes every pluginState, and writes a small
JSON report (no huge prints). Run via bash:  python3 serum2/verify_roundtrip.py
"""
import json, os, re, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from statecodec import from_base64_encoding, to_base64_encoding, parse_xferjson_head  # noqa: E402


def slot_states(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    out = []
    for m in re.finditer(r"<FX_SLOT\b[^>]*>", text):
        ps = re.search(r'pluginState="([^"]*)"', m.group(0))
        nm = re.search(r'name="([^"]*)"', m.group(0))
        pid = re.search(r'pluginID="([^"]*)"', m.group(0))
        enc = ps.group(1) if ps else ""
        data = from_base64_encoding(enc) if enc else b""
        out.append({"name": nm.group(1) if nm else None,
                    "pluginID": pid.group(1) if pid else None,
                    "b64chars": len(enc), "bytes": len(data), "data": data})
    return out


def main():
    t0 = time.time()
    src = sys.argv[1] if len(sys.argv) > 1 else "serum2/test_projects/serum2_test_01.hdaw3"
    svd = sys.argv[2] if len(sys.argv) > 2 else "serum2/test_projects/serum2_test_01_resaved.hdaw3"
    out_p = sys.argv[3] if len(sys.argv) > 3 else "serum2/test_projects/roundtrip_report.json"
    a = slot_states(src)
    b = slot_states(svd)
    rows = []
    for i, (sa, sb) in enumerate(zip(a, b)):
        same = sa["data"] == sb["data"]
        common = 0
        for x, y in zip(sa["data"], sb["data"]):
            if x == y:
                common += 1
            else:
                break
        ja = parse_xferjson_head(sa["data"])
        jb = parse_xferjson_head(sb["data"])
        jdiff = []
        if ja and jb:
            for k in sorted(set(ja) | set(jb)):
                if ja.get(k) != jb.get(k):
                    jdiff.append([k, ja.get(k), jb.get(k)])
        rows.append({
            "track": i, "name": sa["name"], "pluginID": sa["pluginID"],
            "in_bytes": len(sa["data"]), "out_bytes": len(sb["data"]),
            "identical": same, "common_prefix": common,
            "json_in": ja, "json_out": jb, "json_diffs": jdiff,
        })
    report = {
        "src": src, "saved": svd,
        "slots": len(rows),
        "all_identical": all(r["identical"] for r in rows),
        "rows": rows,
        "elapsed_s": round(time.time() - t0, 2),
    }
    with open(out_p, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    for r in rows:
        print(f'[{r["track"]}] {r["name"][:30]:32s} in={r["in_bytes"]:>9d} '
              f'out={r["out_bytes"]:>9d} identical={r["identical"]} '
              f'common_prefix={r["common_prefix"]} json_diffs={r["json_diffs"]}')
    print("report:", out_p, f"({report['elapsed_s']}s)")


if __name__ == "__main__":
    main()
