#!/usr/bin/env python3
"""Waldorf Microwave II/XT (HDAW Xenia gearmulator CLAP) patch decoder + sidecar pipeline.

Two containers appear in D:/pdf/microwave:

  * .usb  - a raw 64 KB bank image: 256 records of 256 bytes, the 16-char patch name
            in the last 16 bytes of each record (verified: 6 files, exactly 65536
            bytes, one printable name at record offset 240 in 255-256 of 256 records).
  * .mid  - a Standard MIDI File carrying Waldorf SysEx dumps
            (F0 3E 0E <dev> <cmd> ... F7, IdWaldorf 0x3E, IdMw2 0x0E). Verified against
            xtLib/xtMidiTypes.h: the file holds SingleDump (0x10, 265-byte dumps ->
            patches), WaveDump (0x12), MultiDump (0x11) and WaveCtlDump (0x13) - only
            SingleDump carries patches, the rest are recorded and counted.

Everything is validated against the emulator's own definitions
(gearmulator 2.2.9 source/xtLib/xtMidiTypes.h, xtState.h: Dumps[DumpType::Single]
.dumpSize = 265) and the real library (2026-09-16).

Usage::

    py -3 timbre-lib/microwave_patch.py --dump "D:/pdf/microwave/cobalt.usb"
    py -3 timbre-lib/microwave_patch.py --survey "D:/pdf/microwave" --out microwave_survey.json
    py -3 timbre-lib/microwave_patch.py --sidecars "D:/pdf/microwave"
    py -3 timbre-lib/microwave_patch.py --verify "D:/pdf/microwave"

No third-party dependencies.  Never writes outside the paths it is given.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from typing import Dict, List, Optional, Sequence, Tuple

SCHEMA_BANK = "hdaw.microwave.bank.v1"
SCHEMA_SURVEY = "hdaw.microwave.survey.v1"
ENGINE = "xenia"
SYSEX_SUFFIX = ".xenia.json"

ID_WALDORF = 0x3E
ID_MW2 = 0x0E
CMD_SINGLE_DUMP = 0x10
CMD_WAVE_DUMP = 0x12
CMD_MULTI_DUMP = 0x11
CMD_WAVECTL_DUMP = 0x13
CMD_NAMES = {CMD_SINGLE_DUMP: "SingleDump", CMD_WAVE_DUMP: "WaveDump",
             CMD_MULTI_DUMP: "MultiDump", CMD_WAVECTL_DUMP: "WaveCtlDump"}

USB_RECORD = 256            # 65536 / 256 = 256 records
USB_NAME_OFFSET = 240       # last 16 bytes of the record
NAME_LEN = 16
SINGLE_DUMP_SIZE = 265      # xtState.h Dumps[DumpType::Single].dumpSize
SINGLE_NAME_OFFSET = 242    # 16-char name inside a Single dump body (verified on the
                            # real .mid: 'Beachphasing SCDP', 'Fatt Bass    SCDk', ...)

# Role hints from patch names (the µsb records carry no category field).
ROLE_KEYWORDS = [
    ("bass", ("bass", "sub", "bassline", "acid")),
    ("lead", ("lead", "solo", "saw", "sync", "hoover")),
    ("pad", ("pad", "string", "atmo", "choir", "warm", "wave-premiere", "wash")),
    ("pluck", ("pluck", "stab", "short", "bell", "ping", "arp")),
    ("fx", ("fx", "noise", "sweep", "riser", "formant", "scary", "alien")),
    ("keys", ("piano", "organ", "clav", "e-piano", "rhodes")),
]
UNMAPPED_NOTES = [
    "parameter semantics not decoded (the XT parameter vocabulary is available via "
    "gearmulator parameterDescriptions_xt.json; only names are used here)",
    "the .mid bank also carries WaveDump/MultiDump/WaveCtlDump messages, which are "
    "counted but not indexed as patches",
]


def _printable_name(raw: bytes) -> bool:
    if len(raw) < NAME_LEN:
        return False
    text = raw[:NAME_LEN].decode("latin-1")
    # require at least one real printable character: a pure padding run (spaces or
    # NULs) is not a name, and str.strip() does not remove NULs
    if not any(32 <= ord(c) < 127 for c in text):
        return False
    # space and NUL are both used as name padding (real banks pad with spaces,
    # synthetic/tool-written records may leave NULs); anything else means "not a name"
    return all(32 <= ord(c) < 127 or c == "\x00" for c in text)


def _role_for(name: str) -> str:
    low = name.lower()
    for role, keys in ROLE_KEYWORDS:
        for k in keys:
            if k in low:
                return role
    return "other"


def _find_name(body: bytes, preferred: Optional[int] = None) -> Tuple[str, int]:
    """16-char printable name in the trailing bytes; preferred offset wins if valid."""
    if preferred is not None and preferred + NAME_LEN <= len(body):
        cand = body[preferred:preferred + NAME_LEN]
        if _printable_name(cand):
            return cand.decode("latin-1").rstrip(), preferred
    start = max(0, len(body) - 40)
    for off in range(start, len(body) - NAME_LEN + 1):
        cand = body[off:off + NAME_LEN]
        # a name STARTS with a printable character: without this, a window that
        # begins in padding (15 NULs + "H") wins and returns a garbage name
        if not (32 <= cand[0] < 127):
            continue
        if _printable_name(cand):
            return cand.decode("latin-1").rstrip(), off
    return "", -1


def parse_usb(data: bytes) -> Tuple[List[dict], str]:
    """Parse a .usb bank image into patch records."""
    if len(data) % USB_RECORD != 0 or len(data) == 0:
        return [], "size %d is not a multiple of %d" % (len(data), USB_RECORD)
    patches = []
    for idx in range(len(data) // USB_RECORD):
        rec = data[idx * USB_RECORD:(idx + 1) * USB_RECORD]
        name, off = _find_name(rec, USB_NAME_OFFSET)
        if not name:
            continue
        patches.append({
            "slot": idx + 1,
            "name": name,
            "role": _role_for(name),
            "nameOffset": off,
            "params": {str(i): rec[i] for i in range(USB_NAME_OFFSET)},
            "paramCount": USB_NAME_OFFSET,
            "sha1": hashlib.sha1(rec).hexdigest()[:16],
            "container": "usb-record",
        })
    return patches, ""


def _midi_sysex_messages(data: bytes) -> List[bytes]:
    """SysEx payloads from a raw stream or an SMF (SMF varint prefix stripped)."""
    out = []
    i = 0
    while True:
        j = data.find(b"\xf0", i)
        if j < 0:
            break
        k = data.find(b"\xf7", j)
        if k < 0:
            break
        body = data[j + 1:k]
        n = 0
        if body and (body[0] & 0x80):
            while n < len(body) and (body[n] & 0x80):
                n += 1
            n += 1
        real = body[n:]
        if real:
            out.append(real)
        i = k + 1
    return out


def parse_mid(data: bytes) -> Tuple[List[dict], Dict[str, int], str]:
    """Parse an SMF/raw SysEx bank: SingleDumps become patches, others are counted."""
    counts: Dict[str, int] = {}
    patches: List[dict] = []
    for msg in _midi_sysex_messages(data):
        if len(msg) < 5 or msg[0] != ID_WALDORF or msg[1] != ID_MW2:
            continue
        cmd = msg[3]
        label = CMD_NAMES.get(cmd, "0x%02X" % cmd)
        counts[label] = counts.get(label, 0) + 1
        if cmd != CMD_SINGLE_DUMP:
            continue
        body = msg[4:]
        name, off = _find_name(body, SINGLE_NAME_OFFSET)
        if not name:
            continue
        patches.append({
            "slot": len(patches) + 1,
            "name": name,
            "role": _role_for(name),
            "nameOffset": off,
            "params": {str(i): body[i] for i in range(max(0, off))},
            "paramCount": max(0, off),
            "sha1": hashlib.sha1(msg).hexdigest()[:16],
            "container": "sysex-single-dump",
            "dumpSize": len(msg) + 2,
        })
    return patches, counts, ""


def parse_file(path: str) -> Tuple[List[dict], Dict[str, int], str]:
    with open(path, "rb") as fh:
        data = fh.read()
    ext = os.path.splitext(path)[1].lower()
    if ext in (".usb", ".\u00b5sb"):
        patches, err = parse_usb(data)
        return patches, {"SingleDump": len(patches)}, err
    if ext == ".mid":
        return parse_mid(data)
    # fall back on content sniffing
    if data[:4] == b"MThd":
        return parse_mid(data)
    patches, err = parse_usb(data)
    return patches, {"SingleDump": len(patches)}, err


def describe(patch: dict) -> str:
    return "Microwave XT %s patch (from %s)" % (patch["role"], patch["container"])


def build_bank_sidecar(path: str, patches: Sequence[dict], counts: Dict[str, int]) -> dict:
    stem = os.path.basename(path)
    named = [p for p in patches if p["name"].strip()]
    rolestats: Dict[str, int] = {}
    for p in named:
        rolestats[p["role"]] = rolestats.get(p["role"], 0) + 1
    dominant = max(rolestats.items(), key=lambda kv: kv[1])[0] if rolestats else "empty"
    return {
        "schema": SCHEMA_BANK,
        "engine": ENGINE,
        "name": stem,
        "format": "microwave-usb" if patches and patches[0]["container"] == "usb-record" else "microwave-sysex",
        "description": "Waldorf Microwave bank %s: %d patches%s" % (
            stem, len(named),
            (" -- " + ", ".join("%d %s" % (n, r) for r, n in sorted(rolestats.items(), key=lambda kv: -kv[1])[:4])) if rolestats else ""),
        "roleCheck": {"verdict": dominant, "counts": rolestats,
                      "messageTypes": counts},
        "unmapped": list(UNMAPPED_NOTES),
        # FileLibraryManager turns the first two entries into search tags; patch names
        # are what you would search for, so surface the first two.
        "mappedParams": {
            ("patch1" if i == 0 else "patch2"): {"param": "patch:%s" % p["name"]}
            for i, p in enumerate(named[:2])
        },
        "patchCount": len(named),
        "patches": [{k: v for k, v in p.items() if k != "params"} for p in patches],
        "params": {p["name"]: p["params"] for p in patches},
    }


def _bank_files(root: str) -> List[str]:
    exts = (".usb", ".\u00b5sb", ".mid")
    out = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in sorted(filenames):
            if fn.lower().endswith(exts):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def _write_json(path: str, payload: object) -> None:
    text = json.dumps(payload, indent=1, sort_keys=True, ensure_ascii=False)
    with open(path, "w", encoding="utf-8", newline=chr(10)) as fh:
        fh.write(text + chr(10))


def run_dump(path: str, limit: int, as_json: bool) -> int:
    patches, counts, err = parse_file(path)
    if err:
        print("error: %s" % err, file=sys.stderr)
        return 1
    if as_json:
        print(json.dumps(build_bank_sidecar(path, patches, counts), indent=1, sort_keys=True))
        return 0
    print("%s: %d patches, message types %s" % (os.path.basename(path), len(patches), counts))
    for p in patches[:limit]:
        print("  slot %-4d %-18r %-6s nameOffset=%d params=%d" % (
            p["slot"], p["name"], p["role"], p["nameOffset"], p["paramCount"]))
    return 0


def survey(root: str) -> dict:
    files = _bank_files(root)
    totals = {"files": 0, "patches": 0, "empty": 0}
    per_file = []
    roles: Dict[str, int] = {}
    containers: Dict[str, int] = {}
    names: Dict[str, List[str]] = {}
    for path in files:
        patches, counts, err = parse_file(path)
        totals["files"] += 1
        totals["patches"] += len(patches)
        if not patches:
            totals["empty"] += 1
        per_file.append({"file": os.path.basename(path), "patches": len(patches),
                         "messageTypes": counts, "error": err})
        for p in patches:
            roles[p["role"]] = roles.get(p["role"], 0) + 1
            containers[p["container"]] = containers.get(p["container"], 0) + 1
            names.setdefault(p["name"].strip().lower(), []).append(os.path.basename(path))
    duplicates = sorted(({"name": n, "files": sorted(set(f))}
                         for n, f in names.items() if len(set(f)) > 1),
                        key=lambda d: -len(d["files"]))
    return {
        "schema": SCHEMA_SURVEY,
        "engine": ENGINE,
        "root": os.path.basename(os.path.normpath(root)),
        "totals": totals,
        "roles": dict(sorted(roles.items(), key=lambda kv: -kv[1])),
        "containers": containers,
        "perFile": per_file,
        "duplicateNames": duplicates[:40],
        "unmapped": list(UNMAPPED_NOTES),
    }


def run_sidecars(root: str, role: Optional[str]) -> int:
    files = _bank_files(root)
    written = 0
    for path in files:
        patches, counts, err = parse_file(path)
        if err and not patches:
            continue
        if role is not None:
            patches = [p for p in patches if p["role"] == role]
            if not patches:
                continue
        _write_json(path + SYSEX_SUFFIX, build_bank_sidecar(path, patches, counts))
        written += 1
    print("sidecars: %d banks written (%d files scanned)" % (written, len(files)))
    return 0


def verify(root: str) -> int:
    ok = 0
    bad = []
    for path in _bank_files(root):
        sidecar_path = path + SYSEX_SUFFIX
        if not os.path.isfile(sidecar_path):
            bad.append((os.path.basename(path), "sidecar missing"))
            continue
        with open(sidecar_path, encoding="utf-8") as fh:
            sidecar = json.load(fh)
        patches, _counts, err = parse_file(path)
        if err and not patches:
            bad.append((os.path.basename(path), err))
            continue
        if sidecar.get("patchCount") != len(patches):
            bad.append((os.path.basename(path), "patchCount %s != %d" % (sidecar.get("patchCount"), len(patches))))
            continue
        # patch NAMES repeat heavily in these banks (e.g. 256 records named
        # "u101 new"), so compare the ordered (name, sha1) sequence instead of a
        # name-keyed dict, which would compare the wrong record.
        want = [(e.get("name"), e.get("sha1")) for e in sidecar.get("patches", [])]
        have = [(p["name"], p["sha1"]) for p in patches]
        if want != have:
            idx = next((i for i, (a, b) in enumerate(zip(want, have)) if a != b), min(len(want), len(have)))
            mismatch = "patch list differs at index %d (sidecar %r vs re-parsed %r)" % (
                idx, want[idx] if idx < len(want) else None, have[idx] if idx < len(have) else None)
        else:
            mismatch = None
        if mismatch:
            bad.append((os.path.basename(path), mismatch))
            continue
        ok += 1
    print("verify: %d ok, %d bad (%s)" % (ok, len(bad), root))
    for fn, why in bad[:20]:
        print("   %-40s %s" % (fn, why))
    return 0 if not bad else 1


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Waldorf Microwave II/XT (Xenia) patch decoder / sidecar sweep")
    ap.add_argument("--dump", metavar="FILE", help="decode one bank and print its patches")
    ap.add_argument("--limit", type=int, default=6, help="entries to print in --dump")
    ap.add_argument("--json", action="store_true", help="--dump as JSON")
    ap.add_argument("--survey", metavar="DIR", help="scan a bank directory and report")
    ap.add_argument("--out", metavar="FILE", help="write the survey JSON here")
    ap.add_argument("--sidecars", metavar="DIR", help="write <bank>.xenia.json sidecars next to banks")
    ap.add_argument("--role", help="only sidecar patches whose role matches")
    ap.add_argument("--verify", metavar="DIR", help="check every sidecar against its bank")
    args = ap.parse_args(argv)

    if args.dump:
        return run_dump(args.dump, args.limit, args.json)
    if args.survey:
        if not os.path.isdir(args.survey):
            print("not a directory: %s" % args.survey, file=sys.stderr)
            return 2
        data = survey(args.survey)
        if args.out:
            _write_json(args.out, data)
        print(json.dumps(data["totals"], indent=1, sort_keys=True))
        print("roles: %s" % json.dumps(data["roles"]))
        print("containers: %s" % json.dumps(data["containers"]))
        for row in data["perFile"]:
            print("  %-16s patches=%-5d %s" % (row["file"], row["patches"], row["messageTypes"]))
        if args.out:
            print("survey -> %s" % args.out)
        return 0
    if args.sidecars:
        if not os.path.isdir(args.sidecars):
            print("not a directory: %s" % args.sidecars, file=sys.stderr)
            return 2
        return run_sidecars(args.sidecars, args.role)
    if args.verify:
        if not os.path.isdir(args.verify):
            print("not a directory: %s" % args.verify, file=sys.stderr)
            return 2
        return verify(args.verify)
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
