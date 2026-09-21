#!/usr/bin/env python3
"""Stamp every xenia matrix preset with a complete, injectable 265-byte single
dump -- the device-native route (same shape as vavra_matrix_sysex.py).

Microwave XT single dump: F0 3E 0E <device> 10 <bank> <program> ... checksum F7
(265 bytes; checksum = sum[7:263] & 0x7F -- xenia_dump.py). The parent dump is
resolved FROM THE BANK CORPUS and BYTE-VERIFIED against the preset's own named
values (xenia_dump.resolve_parent_dump, which also handles the harvest's +2
labeling quirk on .mid sidecars); the preset's values are then written at their
mapped offsets and the checksum recomputed.

Why the dump route: the sheet's values are the device's patch vocabulary, while
apply_matrix_preset's host-param path only reaches the public subset (the wrapper
collapses same-index params into single host params with derived children, as the
microQ does). apply_matrix_preset injects a preset-carried "sysex" through the
validated Waldorf path, so the emulated OS applies the whole patch natively.

Usage:  python3 timbre-lib/xenia_matrix_sysex.py [--check]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHEET = os.path.join(HERE, "matrix_presets", "xenia.json")
OFFSETS = os.path.join(HERE, "matrix_presets", "xenia_offset_map.json")
BANK_ROOTS = ["/mnt/d/pdf/microwave", "D:/pdf/microwave"]

sys.path.insert(0, HERE)
import xenia_dump as xd  # noqa: E402


def main(argv):
    check = "--check" in argv
    sheet = json.load(open(SHEET, encoding="utf-8"))
    name_to_off = xd.name_to_offset(OFFSETS)
    bank_root = next((r for r in BANK_ROOTS if os.path.isdir(r)), None)
    if bank_root is None:
        raise SystemExit("microwave bank corpus not found (tried %s)" % BANK_ROOTS)
    print("bank root: %s | offset map: %d names" % (bank_root, len(name_to_off)))

    stamped = skipped = 0
    total_values = 0
    shifted = 0
    for pr in sheet.get("presets", []):
        try:
            base, res = xd.resolve_parent_dump(pr, bank_root, name_to_off)
        except xd.DumpError as e:
            print("  ! %s: %s" % (pr.get("id"), e))
            skipped += 1
            continue
        named = xd._named_values(pr, name_to_off)      # skips None / unmapped
        if not named:
            print("  ! %s: no mappable named params" % pr.get("id"))
            skipped += 1
            continue
        shift = -2 if res.get("how") == "vocab-shifted" else 0
        if shift:
            dump = bytearray(base)
            for _name, off, value in named:
                dump[off + shift] = xd._param_byte(value, _name)
            shifted += 1
        else:
            overrides = {n: v for n, _o, v in named}
            dump = bytearray(xd.patch_dump(base, overrides, name_to_off=name_to_off))
        # FRAMING: the parent comes from a bank image, so it carries that bank's
        # slot numbers -- writing there stores the patch in a RAM slot and the
        # CURRENT sound does not change (measured: render delta 0.00017). The
        # audible route targets the edit buffer, bank 0x20 program 0 (the same
        # framing the verified compositions/xenia-ab dumps use).
        dump[xd.IDX_BANK] = 0x20
        dump[xd.IDX_PROGRAM] = 0x00
        dump[xd.IDX_CHECKSUM] = xd.compute_checksum(dump)
        sysex = bytes(dump)
        pr["sysex"] = list(sysex)
        pr["appliesVia"] = "waldorf_dump"
        pr["baseBank"] = res["bank"]
        pr["basePatch"] = res["patch"]
        pr["baseHow"] = res["how"]
        pr["sysexOverrides"] = len(named)
        stamped += 1
        total_values += len(named)

    print("stamped %d/%d presets (%d skipped, %d vocab-shifted), %d values, %.1f avg"
          % (stamped, len(sheet.get("presets", [])), skipped, shifted,
             total_values, (total_values / stamped) if stamped else 0.0))
    if not check:
        json.dump(sheet, open(SHEET, "w", encoding="utf-8"), indent=1, sort_keys=True)
        print("wrote", SHEET)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
