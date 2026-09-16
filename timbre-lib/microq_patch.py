#!/usr/bin/env python3
"""Waldorf microQ (HDAW Vavra gearmulator CLAP) sound-dump decoder + sidecar/survey pipeline.

Turns the rhythm-lab microQ library (528 single-sound SysEx files) into per-patch
records with names, categories and raw parameters, writes '<patch>.vavra.json'
sidecars (schema hdaw.microq.patch.v1, engine vavra) next to each patch so
FileLibraryManager / search_library can index them, and emits a library survey.

Wire format (verified against gearmulator 2.2.9 source/mqLib + mqJucePlugin and all
528 real files, 2026-09-16):

  F0 3E <IdMicroQ> <IdDeviceOmni> 10 <buffer> | 363 parameter bytes | name | cat | F7
     |  |            |              |  |
     |  |            |              |  +-- SysexCommand::SingleDump = 0x10
     |  |            |              +----- IdDeviceOmni = 0x00
     |  |            +-------------------- IdMicroQ = 0x10
     |  +--------------------------------- wLib::IdWaldorf = 0x3E
     +----------------------------------- F0

  * Dump size 392 bytes: 'Dumps[DumpType::Single].dumpSize = 392' (mqstate.h) matches
    every file exactly; the 393-byte twin is DumpType::Multi, not this library.
  * Name: 16 chars at offset 370 ('mq::g_singleNameOffset = 370',
    'mq::g_singleNameLength = 16', mqmiditypes.h). The 'q' variant (offset 371) does
    not appear in this library (0/528 filename matches).
  * Category: 4 chars at offset 386 ('mq::g_categoryOffset = 386',
    'mq::g_categoryLength = 4'), written by State::setCategory (mqstate.cpp:261).
    The files carry real categories (Arp 147, Pad 86, Lead 81, Bass 79, ...), which is
    a much stronger role signal than a name heuristic.
  * Parameters: bytes 7..369 (SysexIndex::IdxSingleParamFirst = 7, mqmiditypes.h), a
    dense parameter block. Their semantic names are NOT in mqLib (no sound-parameter
    enum there), so they are reported raw and listed under 'unmapped' - never dropped.
  * Checksum: the emulation does NOT verify it - convertTo() only compares the size
    (wLib/wState.h:18-24), and State::parseSingleDump stores the dump as-is. These
    third-party files therefore do not follow the Waldorf 7-bit-sum rule the emulator
    itself uses when *sending* (mqstate.cpp:754-757); the decoder records it as
    informational only and never rejects a file over it.
  * Provenance: 'MicroQ Patches by Chris Jones' (rhythm-lab.com), free to use with
    credit (see Readme.txt).

Usage::

    py -3 timbre-lib/microq_patch.py --dump "D:/pdf/rhythm-lab.com_waldorf_micro_q/Bass/....syx"
    py -3 timbre-lib/microq_patch.py --survey "D:/pdf/rhythm-lab.com_waldorf_micro_q" --out microq_survey.json
    py -3 timbre-lib/microq_patch.py --sidecars "D:/pdf/rhythm-lab.com_waldorf_micro_q"
    py -3 timbre-lib/microq_patch.py --verify "D:/pdf/rhythm-lab.com_waldorf_micro_q"

No third-party dependencies.  Never writes outside the paths it is given.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Dict, List, Optional, Sequence, Tuple

SCHEMA_PATCH = "hdaw.microq.patch.v1"
SCHEMA_SURVEY = "hdaw.microq.survey.v1"
ENGINE = "vavra"
SYSEX_SUFFIX = ".vavra.json"

ID_WALDORF = 0x3E
ID_MICROQ = 0x10
ID_DEVICE_OMNI = 0x00
CMD_SINGLE_DUMP = 0x10
DUMP_SIZE = 392                 # Dumps[DumpType::Single].dumpSize
PARAM_FIRST = 7                 # SysexIndex::IdxSingleParamFirst
NAME_OFFSET = 370               # mq::g_singleNameOffset
NAME_LEN = 16
CAT_OFFSET = 386                # mq::g_categoryOffset
CAT_LEN = 4
PARAM_LAST = NAME_OFFSET - 1    # parameters run up to the name field

# Category -> the shared role vocabulary used by the other preset pipelines.
CATEGORY_ROLE = {
    "arp": "arp", "bass": "bass", "lead": "lead", "pad": "pad", "atmo": "pad",
    "poly": "chord", "keys": "chord", "fx": "fx", "perc": "other", "misc": "other",
    "nrz": "other",
}
PROVENANCE = "MicroQ Patches by Chris Jones (rhythm-lab.com); free to use with credit"
UNMAPPED_NOTES = [
    "parameter semantics not decoded (no sound-parameter name table in mqLib); bytes "
    "7-%d are reported raw under 'params'" % PARAM_LAST,
    "checksum is informational only: the emulation validates dump SIZE, not checksum "
    "(wLib/wState.h convertTo)",
]


def _printable(raw: bytes) -> bool:
    try:
        text = raw.decode("latin-1")
    except Exception:  # pragma: no cover - latin-1 never raises
        return False
    return all(32 <= ord(c) < 127 or c == "\x00" for c in text)


def parse_dump(data: bytes) -> Tuple[Optional[dict], str]:
    """Parse one microQ Single dump. Returns (patch, error)."""
    if len(data) != DUMP_SIZE:
        return None, "unexpected size %d (Single dumps are %d bytes)" % (len(data), DUMP_SIZE)
    if data[0] != 0xF0 or data[1] != ID_WALDORF or data[2] != ID_MICROQ:
        return None, "not a Waldorf microQ SysEx (expected F0 3E 10)"
    if data[4] != CMD_SINGLE_DUMP:
        return None, "not a SingleDump (command 0x%02X, expected 0x10)" % data[4]
    if data[DUMP_SIZE - 1] != 0xF7:
        return None, "dump is not F7-terminated"
    name_raw = data[NAME_OFFSET:NAME_OFFSET + NAME_LEN]
    if not _printable(name_raw):
        return None, "name field is not printable ASCII"
    cat_raw = data[CAT_OFFSET:CAT_OFFSET + CAT_LEN]
    name = name_raw.decode("latin-1").rstrip()
    category = cat_raw.decode("latin-1").rstrip() if _printable(cat_raw) else ""
    params = list(data[PARAM_FIRST:PARAM_LAST + 1])
    role = CATEGORY_ROLE.get(category.strip().lower(), "other")
    patch = {
        "name": name,
        "category": category,
        "role": role,
        "params": {str(PARAM_FIRST + i): v for i, v in enumerate(params)},
        "paramFirstIndex": PARAM_FIRST,
        "paramCount": len(params),
        "device": data[3],
        "buffer": data[5],
        "command": data[4],
        "size": len(data),
        "checksumStored": data[DUMP_SIZE - 2],
        "checksumWaldorfSum": sum(data[4:DUMP_SIZE - 2]) & 0x7F,
        "sha1": __import__("hashlib").sha1(data).hexdigest()[:16],
    }
    patch["checksumMatchesWaldorfRule"] = patch["checksumStored"] == patch["checksumWaldorfSum"]
    return patch, ""


def describe(patch: dict) -> str:
    p = patch["params"]
    bits = []
    if patch["category"]:
        bits.append("category %s" % patch["category"])
    for key, label in (("40", "osc1 shape"), ("49", "filter"), ("57", "amp env"),
                       ("70", "lfo"), ("90", "fx")):
        if key in p:
            bits.append("%s=%d" % (label, p[key]))
    return "microQ %s patch: %s" % (patch["role"], ", ".join(bits))


def build_sidecar(patch: dict, source_file: str) -> dict:
    return {
        "schema": SCHEMA_PATCH,
        "engine": ENGINE,
        "name": patch["name"],
        "format": "microq-single-dump",
        "category": patch["category"],
        "description": describe(patch),
        "roleCheck": {"verdict": patch["role"], "category": patch["category"]},
        "unmapped": list(UNMAPPED_NOTES),
        # FileLibraryManager turns the first two mappedParams entries into search
        # TAGS, so they carry the things worth searching on: the category (a real
        # signal in these dumps) and the pack. The raw parameter block stays under
        # 'params' (numeric keys would only produce tags like "7, 8").
        "mappedParams": {
            "category": {"param": "category:%s" % (patch["category"].strip() or "unknown")},
            "pack": {"param": "pack:%s" % PROVENANCE.split("(")[0].strip()},
        },
        "params": {k: v for k, v in sorted(patch["params"].items(), key=lambda kv: int(kv[0]))},
        "source": {"file": os.path.basename(source_file), "pack": PROVENANCE,
                   "size": patch["size"], "sha1": patch["sha1"]},
        "checksum": {"stored": patch["checksumStored"], "waldorfSum": patch["checksumWaldorfSum"],
                     "matches": patch["checksumMatchesWaldorfRule"], "enforced": False},
    }


def _syx_files(root: str) -> List[str]:
    out = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in sorted(filenames):
            if fn.lower().endswith(".syx"):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def _write_json(path: str, payload: object) -> None:
    text = json.dumps(payload, indent=1, sort_keys=True, ensure_ascii=False)
    with open(path, "w", encoding="utf-8", newline=chr(10)) as fh:
        fh.write(text + chr(10))


def run_dump(path: str, as_json: bool) -> int:
    with open(path, "rb") as fh:
        data = fh.read()
    patch, err = parse_dump(data)
    if patch is None:
        print("error: %s" % err, file=sys.stderr)
        return 1
    if as_json:
        print(json.dumps(patch, indent=1, sort_keys=True))
        return 0
    print("%s: %s [%s] zone=%s params=%d checksum %s" % (
        os.path.basename(path), patch["name"], patch["category"] or "?", patch["role"],
        patch["paramCount"], "ok" if patch["checksumMatchesWaldorfRule"] else "informational"))
    print("  params: %s" % " ".join("%d:%d" % (int(k), v)
                                    for k, v in list(sorted(patch["params"].items(), key=lambda kv: int(kv[0])))[:24]))
    return 0


def survey(root: str) -> dict:
    files = _syx_files(root)
    totals = {"files": 0, "parsed": 0, "failed": 0, "checksumMatches": 0}
    categories: Dict[str, int] = {}
    roles: Dict[str, int] = {}
    folders: Dict[str, int] = {}
    names: Dict[str, List[str]] = {}
    for path in files:
        totals["files"] += 1
        with open(path, "rb") as fh:
            data = fh.read()
        patch, err = parse_dump(data)
        if patch is None:
            totals["failed"] += 1
            continue
        totals["parsed"] += 1
        totals["checksumMatches"] += 1 if patch["checksumMatchesWaldorfRule"] else 0
        key = patch["category"].strip() or "(none)"
        categories[key] = categories.get(key, 0) + 1
        roles[patch["role"]] = roles.get(patch["role"], 0) + 1
        folder = os.path.relpath(os.path.dirname(path), root).split(os.sep)[0]
        folders[folder] = folders.get(folder, 0) + 1
        names.setdefault(patch["name"].strip().lower(), []).append(os.path.basename(path))
    duplicates = sorted(({"name": n, "files": sorted(set(f))}
                         for n, f in names.items() if len(set(f)) > 1),
                        key=lambda d: -len(d["files"]))
    return {
        "schema": SCHEMA_SURVEY,
        "engine": ENGINE,
        "root": os.path.basename(os.path.normpath(root)),
        "provenance": PROVENANCE,
        "totals": totals,
        "categories": dict(sorted(categories.items(), key=lambda kv: -kv[1])),
        "roles": dict(sorted(roles.items(), key=lambda kv: -kv[1])),
        "folders": dict(sorted(folders.items(), key=lambda kv: -kv[1])),
        "duplicateNames": duplicates[:40],
        "unmapped": list(UNMAPPED_NOTES),
    }


def run_sidecars(root: str, role: Optional[str], with_bytes: bool = False) -> int:
    files = _syx_files(root)
    written = 0
    failed = 0
    for path in files:
        with open(path, "rb") as fh:
            data = fh.read()
        patch, err = parse_dump(data)
        if patch is None:
            failed += 1
            continue
        if role is not None and patch["role"] != role:
            continue
        sidecar = build_sidecar(patch, path)
        if with_bytes:
            sidecar["sysexHex"] = data.hex()
        _write_json(path + SYSEX_SUFFIX, sidecar)
        written += 1
    print("sidecars: %d written, %d skipped (unparseable)%s"
          % (written, failed, (", %d files" % len(files))))
    return 0


def verify(root: str) -> int:
    """Re-read every sidecar and check it against its .syx."""
    ok = 0
    bad = []
    for path in _syx_files(root):
        sidecar_path = path + SYSEX_SUFFIX
        if not os.path.isfile(sidecar_path):
            bad.append((os.path.basename(path), "sidecar missing"))
            continue
        with open(sidecar_path, encoding="utf-8") as fh:
            sidecar = json.load(fh)
        with open(path, "rb") as fh:
            data = fh.read()
        patch, err = parse_dump(data)
        if patch is None:
            bad.append((os.path.basename(path), err))
            continue
        if sidecar.get("name", "").strip() != patch["name"].strip():
            bad.append((os.path.basename(path), "name mismatch"))
            continue
        if sidecar.get("category", "").strip() != patch["category"].strip():
            bad.append((os.path.basename(path), "category mismatch"))
            continue
        if {k: int(v) for k, v in (sidecar.get("params") or {}).items()} != {
                k: int(v) for k, v in patch["params"].items()}:
            bad.append((os.path.basename(path), "parameter mismatch"))
            continue
        if (sidecar.get("source") or {}).get("sha1") != patch["sha1"]:
            bad.append((os.path.basename(path), "sha1 mismatch"))
            continue
        ok += 1
    print("verify: %d ok, %d bad (%s)" % (ok, len(bad), root))
    for fn, why in bad[:20]:
        print("   %-52s %s" % (fn, why))
    return 0 if not bad else 1


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Waldorf microQ (Vavra) sound-dump decoder / sidecar sweep")
    ap.add_argument("--dump", metavar="FILE", help="decode one .syx and print its patch")
    ap.add_argument("--json", action="store_true", help="--dump as JSON")
    ap.add_argument("--survey", metavar="DIR", help="scan the library and report")
    ap.add_argument("--out", metavar="FILE", help="write the survey JSON here")
    ap.add_argument("--sidecars", metavar="DIR", help="write <patch>.vavra.json sidecars next to patches")
    ap.add_argument("--role", help="only sidecar patches whose role matches (bass/lead/pad/chord/arp/fx/other)")
    ap.add_argument("--with-bytes", action="store_true", help="embed the raw dump hex in the sidecar")
    ap.add_argument("--verify", metavar="DIR", help="check every sidecar against its .syx")
    args = ap.parse_args(argv)

    if args.dump:
        return run_dump(args.dump, args.json)
    if args.survey:
        if not os.path.isdir(args.survey):
            print("not a directory: %s" % args.survey, file=sys.stderr)
            return 2
        data = survey(args.survey)
        if args.out:
            _write_json(args.out, data)
        print(json.dumps(data["totals"], indent=1, sort_keys=True))
        print("categories: %s" % json.dumps(data["categories"]))
        print("roles: %s" % json.dumps(data["roles"]))
        if data["duplicateNames"]:
            print("duplicate names: %d" % len(data["duplicateNames"]))
        if args.out:
            print("survey -> %s" % args.out)
        return 0
    if args.sidecars:
        if not os.path.isdir(args.sidecars):
            print("not a directory: %s" % args.sidecars, file=sys.stderr)
            return 2
        return run_sidecars(args.sidecars, args.role, args.with_bytes)
    if args.verify:
        if not os.path.isdir(args.verify):
            print("not a directory: %s" % args.verify, file=sys.stderr)
            return 2
        return verify(args.verify)
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
