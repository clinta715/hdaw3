
"""harvest.py — build an index of every Serum 2 preset in a preset tree.

Scans .SerumPreset files (standalone) and .SerumPack zip bundles.
Only the JSON header of each preset is read (fast, safe with 100 MB files).
Output: catalog JSON (metadata, sizes, hash, source) + summary counts.
Optional: dump the raw state bytes of selected presets for injection.

Usage:
    python serum2/harvest.py --root "<presets dir>" --out <workdir> [--dump --max-presets N]
"""
from __future__ import annotations
import argparse, hashlib, json, os, sys, time, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from statecodec import parse_xferjson_head  # noqa: E402


def head(path: str, n: int = 1 << 20) -> bytes:
    with open(path, "rb") as f:
        return f.read(n)


def read_preset_head(path_or_zip: str, member: str | None, n: int = 1 << 20) -> bytes:
    if member is None:
        return head(path_or_zip, n)
    with zipfile.ZipFile(path_or_zip) as z:
        return z.read(member)[:n]


def scan_one(path_or_zip: str, member: str | None, src: str) -> dict | None:
    try:
        data = read_preset_head(path_or_zip, member)
    except Exception:
        return None
    if len(data) < 17:
        return None
    j = parse_xferjson_head(data)
    if j is None:
        return None
    full_member = member or path_or_zip
    return {
        "hash": j.get("hash") or hashlib.md5(data).hexdigest(),
        "name": j.get("presetName") or os.path.basename(full_member),
        "author": j.get("presetAuthor") or "",
        "description": j.get("presetDescription") or "",
        "tags": j.get("tags") or [],
        "product": j.get("product") or "",
        "productVersion": j.get("productVersion") or "",
        "formatVersion": j.get("version"),
        "size": os.path.getsize(path_or_zip) if member is None else None,
        "source": src,
        "path": path_or_zip if member is None else None,
        "pack": path_or_zip if member is not None else None,
        "member": member,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True, help="work/output directory")
    ap.add_argument("--dump", action="store_true", help="also write raw state bytes for every found preset")
    args = ap.parse_args()

    t0 = time.time()
    os.makedirs(args.out, exist_ok=True)
    presets: list[dict] = []
    errors = 0
    n_files = 0
    n_packs = 0

    for dp, dns, fns in os.walk(args.root):
        for fn in fns:
            p = os.path.join(dp, fn)
            low = fn.lower()
            n_files += 1
            if low.endswith(".serumpreset"):
                r = scan_one(p, None, "file")
                if r is None:
                    errors += 1
                else:
                    r["size"] = os.path.getsize(p)
                    presets.append(r)
            elif low.endswith(".serumpack"):
                n_packs += 1
                try:
                    with zipfile.ZipFile(p) as z:
                        for member in z.namelist():
                            if member.lower().endswith(".serumpreset"):
                                r = scan_one(p, member, "pack")
                                if r is not None:
                                    r["size"] = z.getinfo(member).file_size
                                    presets.append(r)
                except Exception:
                    errors += 1
            if n_files % 5000 == 0:
                print(f"  ... {n_files} files, {len(presets)} presets, {time.time()-t0:.0f}s")

    # dedupe by hash
    seen: set[str] = set()
    unique: list[dict] = []
    dup = 0
    for pr in presets:
        if pr["hash"] in seen:
            dup += 1
            continue
        seen.add(pr["hash"])
        unique.append(pr)

    catalog = {
        "root": args.root,
        "scanned_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "files_scanned": n_files,
        "serum_packs": n_packs,
        "preset_records": len(presets),
        "unique_presets": len(unique),
        "duplicates": dup,
        "parse_errors": errors,
        "presets": unique,
    }
    cat_path = os.path.join(args.out, "catalog.json")
    with open(cat_path, "w", encoding="utf-8") as f:
        json.dump(catalog, f, indent=1)

    # size histogram summary (no directory listing)
    sizes = sorted(pr["size"] for pr in unique if pr["size"] is not None)
    def q(pct):
        if not sizes: return 0
        return sizes[min(len(sizes) - 1, int(len(sizes) * pct))]
    print(f"scanned {n_files} files in {time.time()-t0:.0f}s | presets: {len(presets)} "
          f"(unique {len(unique)}, dupes {dup}, errors {errors})")
    print(f"preset size: min={sizes[0] if sizes else 0} p50={q(.5)} p90={q(.9)} p99={q(.99)} "
          f"max={sizes[-1] if sizes else 0} bytes")
    big = sum(1 for s in sizes if s > 10_000_000)
    print(f"presets >10 MB: {big}")
    print(f"catalog: {cat_path}")

    if args.dump:
        state_dir = os.path.join(args.out, "states")
        os.makedirs(state_dir, exist_ok=True)
        from statecodec import SERUM2_MAGIC
        n_ok = 0
        for pr in unique:
            target = os.path.join(state_dir, pr["hash"] + ".serumstate")
            if os.path.exists(target):
                n_ok += 1
                continue
            try:
                if pr["member"] is None:
                    data = head(pr["path"])
                else:
                    with zipfile.ZipFile(pr["pack"]) as z:
                        data = z.read(pr["member"])
                if data[:9] == SERUM2_MAGIC:
                    with open(target, "wb") as f:
                        f.write(data)
                    n_ok += 1
            except Exception:
                pass
        print(f"dumped {n_ok}/{len(unique)} state blobs to {state_dir}")


if __name__ == "__main__":
    main()
