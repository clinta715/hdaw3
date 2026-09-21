#!/usr/bin/env python3
"""Stamp every vavra matrix preset with a complete, injectable 392-byte single
dump -- the device-native route for the microQ's own patch vocabulary.

Why: the preset values ARE the device's parameters, but part of them cannot be
expressed as host parameters. The FX sub-parameters share indexes (their meaning
is set by Fx2Type/FX1Type), so the wrapper folds them into single host params with
derived children (jucePluginLib/controller.cpp: isDerivedParameter collapse) --
publishing them via isPublic adds no host param (measured: live param count
unchanged at 7557). A 392-byte single dump carries all 363 values natively and is
applied by the emulated OS itself.

Build: parent dump (the preset's first corpus example, resolved under
sysexRoots) + the sheet params mapped to byte offsets via
vavra_offset_map.json (named keys) or taken raw (off_<N> keys).
The full dump is stored on the preset as "sysex" (392 ints) so
apply_matrix_preset can inject it through the validated Waldorf SysEx path.

Usage:  python3 timbre-lib/vavra_matrix_sysex.py [--check]
"""
import glob, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SHEET = os.path.join(HERE, "matrix_presets", "vavra.json")
OFFSETS = os.path.join(HERE, "matrix_presets", "vavra_offset_map.json")
DEFAULT_ROOTS = ["/mnt/d/pdf/rhythm-lab.com_waldorf_micro_q",
                 "D:/pdf/rhythm-lab.com_waldorf_micro_q"]

sys.path.insert(0, HERE)
import vavra_dump as vd  # noqa: E402  (build_dump / load_offset_map / ...)


def resolve_roots(sheet):
    """sysexRoots are repo-relative library names; resolve to a path that
    EXISTS on this host (WSL needs /mnt/d/..., Windows needs D:/...)."""
    raw = sheet.get("sysexRoots") or []
    cands = []
    for r in raw:
        if os.path.isabs(r):
            cands += [r]
        else:
            cands += [os.path.join("/mnt/d/pdf", r), os.path.join("D:/pdf", r), r]
    for r in cands:
        if os.path.isdir(r):
            return [r]
    for r in DEFAULT_ROOTS:
        if os.path.isdir(r):
            return [r]
    return DEFAULT_ROOTS


def build_name_index(roots):
    """Index every library dump by the patch name EMBEDDED in it (bytes
    370..385). The file names carry a category suffix ("Acid bender   CJ
    Arp.syx") while the corpus references the patch name ("Acid bender   CJ"),
    so the embedded field is the authoritative key -- it matches 40/40 of the
    sheet's examples."""
    idx = {}
    for r in roots:
        for p in glob.glob(os.path.join(r, "**", "*.syx"), recursive=True):
            try:
                b = open(p, "rb").read()
            except OSError:
                continue
            if len(b) >= 392 and b[0] == 0xF0:
                idx.setdefault(b[370:386].decode("latin-1").rstrip(), p)
    return idx


def find_base(name, index):
    if name in index:
        return index[name]
    want = " ".join(name.split()).lower()
    for embedded, p in index.items():
        if " ".join(embedded.split()).lower() == want:
            return p
    return None


def main(argv):
    check = "--check" in argv
    sheet = json.load(open(SHEET, encoding="utf-8"))
    roots = resolve_roots(sheet)
    name_to_off = vd.load_offset_map(OFFSETS)
    name_index = build_name_index(roots)
    print("base index: %d library dumps" % len(name_index))
    presets = sheet.get("presets", [])
    stamped = skipped = 0
    total_offsets = 0
    for pr in presets:
        params = pr.get("params") or {}
        examples = pr.get("examples") or []
        base = find_base(examples[0], name_index) if examples else None
        if base is None:
            print("  ! no base dump for %r (examples=%s)" % (pr.get("name"), examples[:1]))
            skipped += 1
            continue
        try:
            by_byte = vd.params_to_bytes(params, name_to_off)
        except vd.DumpError as e:
            print("  ! %s: %s" % (pr.get("id"), e))
            skipped += 1
            continue
        # Only 7..369 are writable parameter bytes; report anything dropped.
        inrange = {o: v for o, v in by_byte.items() if 7 <= int(o) <= 369}
        dropped = len(by_byte) - len(inrange)
        try:
            dump = vd.build_dump(base, inrange, name=(pr.get("name") or "")[:16] or None)
        except vd.DumpError as e:
            print("  ! %s: %s" % (pr.get("id"), e))
            skipped += 1
            continue
        pr["sysex"] = list(dump)
        pr["baseSyx"] = os.path.basename(base)
        pr["sysexRoots"] = sheet.get("sysexRoots") or ["rhythm-lab.com_waldorf_micro_q"]
        pr["appliesVia"] = "waldorf_dump"
        pr["sysexOverrides"] = len(inrange)
        if dropped:
            pr["sysexDroppedOffsets"] = dropped
        stamped += 1
        total_offsets += len(inrange)
    print("stamped %d/%d presets (%d skipped), %d parameter bytes set, %.1f avg"
          % (stamped, len(presets), skipped, total_offsets,
             (total_offsets / stamped) if stamped else 0.0))
    if not check:
        json.dump(sheet, open(SHEET, "w", encoding="utf-8"), indent=1, sort_keys=True)
        print("wrote", SHEET)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
