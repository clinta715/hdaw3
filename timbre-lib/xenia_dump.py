#!/usr/bin/env python3
"""Waldorf Microwave II/XT (HDAW Xenia gearmulator CLAP) single-dump WRITER
+ injectable morph emitter.

Byte layout is the VERIFIED Microwave XT / mw2 single-program dump (rule,
citations and validation stats in timbre-lib/matrix_presets/xenia-offset-map.md)::

    265 bytes total:
      F0 3E 0E <device> 10 <bank> <program>   header (7 bytes)
      <256 param bytes>                        params 0..255 at full[7+N]
                                               (params 240..255 = 16-char name)
      <checksum>                               full[263]
      F7                                       full[264]

  Param rule (mirror of the verified microQ/vavra rule):

      dump_byte_offset = 7 + linear parameterDescriptions index
      value            = one 7-bit byte (no packing, no 14-bit pairs)

  checksum = sum(full[7:263]) & 0x7F   (xtState.h updateChecksum; holds for
  3823/3823 real dumps in the bank corpus).  The emulator does NOT validate
  the checksum on dump receive (wLib::State::convertTo checks size only) and
  re-stamps it when the state is serialized, but the writer recomputes it
  anyway so files are also valid for real hardware.

Bank containers (read-only decoding):

  * .mid/.MID/.syx - SMF or raw SysEx stream; the 265-byte dumps are stored
    verbatim (name = full[247:263]).
  * .usb/.<micro>sb  - 64 KB bank image, 256 records of 256 bytes; params sit
    at record[0..239] and the name at record[240..255].  Records carry NO
    framing, so the writer synthesizes F0 3E 0E 00 10 00 00 ... F7 around
    them (device 0, bank A, program 0) and computes the checksum.

build_injectable_morphs derives the injectable companion of
xenia_morphs.json: every morph step gains a "sysex" array (the 265
ints of the pair's parent-A dump with the step's interpolated values written
at their mapped offsets).  Parent resolution is byte-verified against the
bank corpus and the labeling mode is recorded per pair ("how"): either
"verified" (preset named value == dump byte at 7+index) or
"vocab-shifted" (== dump byte at 5+index; the xenia.json named values
carry the harvest's +2 labeling quirk for .mid sidecars -- see the .md).

Deterministic and byte-stable: sorted JSON, basenames only, no timestamps,
no absolute paths.  Run::

    python3 timbre-lib/xenia_dump.py \
        --base-bank /mnt/d/pdf/microwave/cobalt.<micro>sb --patch 'Cobalt  Blue' \
        --set EffectType=5,F1Cutoff=90 --name 'My Patch' --out out.syx

    python3 timbre-lib/xenia_dump.py --base-bank BANK --list-patches
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

DUMP_LEN = 265
IDX_F0 = 0
IDX_ID_WALDORF = 1           # 0x3E
IDX_ID_MACHINE = 2           # 0x0E (IdMw2)
IDX_DEVICE = 3
IDX_CMD = 4                  # 0x10 SingleDump
IDX_BANK = 5                 # wLib::IdxBuffer -> xt LocationH
IDX_PROGRAM = 6              # wLib::IdxLocation
IDX_PARAM_FIRST = 7          # xt IdxSingleParamFirst: param N at full[7+N]
IDX_PARAM_LAST = 262         # params 0..255 (240..255 = name chars)
IDX_NAME = 247               # xt mw2::g_singleNamePosition (param 240)
NAME_LEN = 16
IDX_CHECKSUM = 263
IDX_END = 264

ID_WALDORF = 0x3E
ID_MW2 = 0x0E
CMD_SINGLE_DUMP = 0x10
LOCATION_EDIT_BUFFER_SINGLE = 0x20   # xt LocationH::SingleEditBufferSingleMode

PARAM_INDEX_LAST = 239       # 240..255 are the name characters

USB_RECORD = 256
USB_NAME_OFFSET = 240

DEFAULT_BANK_ROOT = "/mnt/d/pdf/microwave"
DEFAULT_OFFSET_MAP = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "matrix_presets", "xenia_offset_map.json")

MIN_PARENT_CHECKS = 10       # named params that must byte-verify (0 errors)


class DumpError(ValueError):
    """Invalid dump input (length, framing, offsets, values, encoding)."""


# ---------------------------------------------------------------------------
# Framing primitives
# ---------------------------------------------------------------------------

def compute_checksum(dump: Sequence[int]) -> int:
    """xtState.h updateChecksum: low 7 bits of the byte sum from index 7."""
    return sum(dump[IDX_PARAM_FIRST:IDX_CHECKSUM]) & 0x7F


def _encode_fixed(text: str, width: int, what: str) -> bytes:
    """latin-1, space-padded / truncated to exactly ``width`` bytes."""
    try:
        raw = str(text).encode("latin-1")
    except UnicodeEncodeError as exc:
        raise DumpError("%s %r is not latin-1 encodable" % (what, text)) \
            from None
    return raw[:width].ljust(width, b" ")


def _param_byte(value, where: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        if isinstance(value, float):
            value = int(round(value))
        else:
            raise DumpError("%s is not a number: %r" % (where, value))
    if not 0 <= value <= 127:
        raise DumpError("%s outside 0..127: %r" % (where, value))
    return int(value)


def _framing(dump: bytearray, device: int, bank: int, program: int) -> None:
    for value, where in ((device, "device"), (bank, "bank"),
                         (program, "program")):
        if isinstance(value, bool) or not isinstance(value, int) \
                or not 0 <= value <= 127:
            raise DumpError("%s must be a 7-bit int, got %r" % (where, value))
    dump[IDX_F0] = 0xF0
    dump[IDX_ID_WALDORF] = ID_WALDORF
    dump[IDX_ID_MACHINE] = ID_MW2
    dump[IDX_DEVICE] = device
    dump[IDX_CMD] = CMD_SINGLE_DUMP
    dump[IDX_BANK] = bank
    dump[IDX_PROGRAM] = program
    dump[IDX_END] = 0xF7


# ---------------------------------------------------------------------------
# Offset map
# ---------------------------------------------------------------------------

def load_offset_map(path: Optional[str] = None) -> Dict[int, str]:
    """Strict loader: dump byte offset -> unique non-empty param name."""
    path = path or DEFAULT_OFFSET_MAP
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    if not isinstance(data, dict):
        raise DumpError("offset map must be a JSON object of offset -> name")
    mapping: Dict[int, str] = {}
    owner: Dict[str, int] = {}
    for key, name in data.items():
        if not isinstance(key, str) or not key.isdigit():
            raise DumpError("offset map keys must be digit-string dump "
                            "offsets, got %r" % (key,))
        off = int(key)
        if not IDX_PARAM_FIRST <= off <= IDX_PARAM_LAST:
            raise DumpError("offset %d outside the param range %d..%d"
                            % (off, IDX_PARAM_FIRST, IDX_PARAM_LAST))
        if not isinstance(name, str) or not name:
            raise DumpError("offset map names must be non-empty strings "
                            "(offset %d)" % off)
        if name in owner:
            raise DumpError("offset map name %r used for offsets %d and %d; "
                            "the rename must stay injective"
                            % (name, owner[name], off))
        owner[name] = off
        mapping[off] = name
    return mapping


def name_to_offset(path: Optional[str] = None) -> Dict[str, int]:
    """Inverse of load_offset_map: param name -> dump byte offset."""
    return {name: off for off, name in load_offset_map(path).items()}


def _resolve_overrides(overrides: Dict[object, object],
                       name_to_off: Dict[str, int]) -> List[Tuple[int, int]]:
    """(param index, value) pairs; keys are names or integer param indices."""
    out: List[Tuple[int, int]] = []
    for key, value in overrides.items():
        if isinstance(key, str) and not key.isdigit():
            if key not in name_to_off:
                raise DumpError("unknown parameter name %r (not in the "
                                "offset map)" % key)
            off = name_to_off[key]
        else:
            off = int(key) + IDX_PARAM_FIRST
        idx = off - IDX_PARAM_FIRST
        if not 0 <= idx <= PARAM_INDEX_LAST:
            raise DumpError("override %r addresses name/reserved bytes "
                            "(param index %d); params are 0..%d"
                            % (key, idx, PARAM_INDEX_LAST))
        out.append((idx, _param_byte(value, "override %r" % (key,))))
    return sorted(out)


# ---------------------------------------------------------------------------
# Dump writer
# ---------------------------------------------------------------------------

def build_single_dump(base_params, overrides, name=None,
                      device: int = 0x00, bank: int = 0x00,
                      program: int = 0x00,
                      name_to_off: Optional[Dict[str, int]] = None) -> bytes:
    """265-byte mw2 single dump: base patch values + named overrides.

    ``base_params`` maps param indices 0..239 (int or digit-str) to 7-bit
    values -- exactly the decode_bank space.  ``overrides`` maps parameter
    names (offset map) or param indices to new values.  ``name`` (16 chars,
    space-padded/truncated latin-1) lands at full[247].  Header:
    F0 3E 0E <device> 10 <bank> <program>; the checksum is computed, never
    copied.  Returns a new bytes object.
    """
    name_to_off = name_to_off if name_to_off is not None \
        else name_to_offset()
    dump = bytearray(DUMP_LEN)
    _framing(dump, device, bank, program)
    for key, value in dict(base_params or {}).items():
        idx = int(key)
        if not 0 <= idx <= PARAM_INDEX_LAST:
            raise DumpError("base param index %d outside 0..%d"
                            % (idx, PARAM_INDEX_LAST))
        dump[IDX_PARAM_FIRST + idx] = _param_byte(value,
                                                  "base param %d" % idx)
    for idx, value in _resolve_overrides(overrides or {}, name_to_off):
        dump[IDX_PARAM_FIRST + idx] = value
    if name is not None:
        dump[IDX_NAME:IDX_NAME + NAME_LEN] = \
            _encode_fixed(name, NAME_LEN, "name")
    dump[IDX_CHECKSUM] = compute_checksum(dump)
    return bytes(dump)


def load_dump(base_syx_path: str) -> bytes:
    """Read + validate a 265-byte single dump (framing incl. cmd byte)."""
    with open(base_syx_path, "rb") as fh:
        dump = fh.read()
    if len(dump) != DUMP_LEN:
        raise DumpError("%s: expected %d bytes, got %d"
                        % (base_syx_path, DUMP_LEN, len(dump)))
    if dump[IDX_F0] != 0xF0 or dump[IDX_END] != 0xF7:
        raise DumpError("%s: missing F0..F7 framing" % base_syx_path)
    if dump[IDX_ID_WALDORF] != ID_WALDORF or dump[IDX_ID_MACHINE] != ID_MW2:
        raise DumpError("%s: not a Waldorf mw2 message" % base_syx_path)
    if dump[IDX_CMD] != CMD_SINGLE_DUMP:
        raise DumpError("%s: cmd byte is 0x%02X, not SingleDump (0x10)"
                        % (base_syx_path, dump[IDX_CMD]))
    return dump


def patch_dump(base_dump: bytes, overrides, name=None,
               name_to_off: Optional[Dict[str, int]] = None) -> bytes:
    """Parent dump bytes + overrides (+ name); checksum recomputed."""
    if len(base_dump) != DUMP_LEN:
        raise DumpError("base dump must be %d bytes, got %d"
                        % (DUMP_LEN, len(base_dump)))
    name_to_off = name_to_off if name_to_off is not None \
        else name_to_offset()
    dump = bytearray(base_dump)
    for idx, value in _resolve_overrides(overrides or {}, name_to_off):
        dump[IDX_PARAM_FIRST + idx] = value
    if name is not None:
        dump[IDX_NAME:IDX_NAME + NAME_LEN] = \
            _encode_fixed(name, NAME_LEN, "name")
    dump[IDX_CHECKSUM] = compute_checksum(dump)
    return bytes(dump)


# ---------------------------------------------------------------------------
# Bank decoding (independent of microwave_patch.py; read-only)
# ---------------------------------------------------------------------------

def _read_varlen(data: bytes, pos: int) -> Tuple[int, int]:
    value = 0
    for _ in range(4):
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos
    raise DumpError("unterminated SMF varlen")


def iter_sysex(data: bytes) -> List[bytes]:
    """Full F0..F7 messages from a raw stream or a (properly parsed) SMF."""
    out: List[bytes] = []
    if data[:4] != b"MThd":
        pos = 0
        while True:
            start = data.find(b"\xf0", pos)
            if start < 0:
                break
            end = data.find(b"\xf7", start)
            if end < 0:
                break
            out.append(data[start:end + 1])
            pos = end + 1
        return out
    header_len = int.from_bytes(data[4:8], "big")
    pos = 8 + header_len
    tracks = int.from_bytes(data[10:12], "big")
    for _ in range(tracks):
        if pos + 8 > len(data) or data[pos:pos + 4] != b"MTrk":
            break
        track_len = int.from_bytes(data[pos + 4:pos + 8], "big")
        end = pos + 8 + track_len
        cur = pos + 8
        running = None
        fragment = None
        while cur < end:
            while cur < end:
                byte = data[cur]
                cur += 1
                if not byte & 0x80:
                    break
            if cur >= end:
                break
            status = data[cur]
            if status in (0xF0, 0xF7):
                cur += 1
                length, cur = _read_varlen(data, cur)
                chunk = data[cur:cur + length]
                cur += length
                if status == 0xF0:
                    body = chunk if chunk[:1] == b"\xf0" else b"\xf0" + chunk
                    if body.endswith(b"\xf7"):
                        out.append(body)
                    else:
                        if fragment is not None:
                            out.append(fragment)
                        fragment = body
                elif fragment is not None:
                    fragment += chunk
                    if fragment.endswith(b"\xf7"):
                        out.append(fragment)
                        fragment = None
            elif status == 0xFF:
                cur += 2
                length, cur = _read_varlen(data, cur)
                cur += length
                running = None
            elif status & 0x80:
                running = status
                cur += 1
                cur += 2 if (status & 0xE0) in (0x80, 0x90, 0xA0, 0xB0, 0xE0) \
                    else 1
            else:
                cur += 2 if (running & 0xE0) in (0x80, 0x90, 0xA0, 0xB0, 0xE0) \
                    else 1
        if fragment is not None:
            out.append(fragment)
        pos = end
    return out


def _printable_record_name(raw: bytes) -> bool:
    if len(raw) < NAME_LEN:
        return False
    text = raw[:NAME_LEN].decode("latin-1")
    if not any(32 <= ord(c) < 127 for c in text):
        return False
    return all(32 <= ord(c) < 127 or c == "\x00" for c in text)


def _dump_name(full: bytes) -> str:
    return full[IDX_NAME:IDX_NAME + NAME_LEN].decode("latin-1").rstrip()


def load_bank(path: str) -> List[dict]:
    """Every patch in a bank file, in file order.

    Rows: {"name", "params" (param-index space 0..239), "dump" (265-byte
    sysex; .usb records get synthesized framing), "container"}.
    """
    with open(path, "rb") as fh:
        data = fh.read()
    ext = os.path.splitext(path)[1].lower()
    rows: List[dict] = []
    if ext in (".usb", ".\u00b5sb"):
        if len(data) % USB_RECORD or not data:
            raise DumpError("%s: size %d is not a multiple of %d"
                            % (path, len(data), USB_RECORD))
        for idx in range(len(data) // USB_RECORD):
            rec = data[idx * USB_RECORD:(idx + 1) * USB_RECORD]
            raw = rec[USB_NAME_OFFSET:USB_NAME_OFFSET + NAME_LEN]
            if not (_printable_record_name(raw) and 32 <= raw[0] < 127):
                continue
            params = {i: rec[i] for i in range(USB_NAME_OFFSET)}
            rows.append({
                "name": raw.decode("latin-1").rstrip(),
                "params": params,
                "dump": build_single_dump(params, {}, name=None),
                "container": "usb",
            })
        return rows
    for msg in iter_sysex(data):
        if len(msg) != DUMP_LEN:
            continue
        if msg[IDX_ID_WALDORF] != ID_WALDORF \
                or msg[IDX_ID_MACHINE] != ID_MW2 \
                or msg[IDX_CMD] != CMD_SINGLE_DUMP:
            continue
        rows.append({
            "name": _dump_name(msg),
            "params": {i: msg[IDX_PARAM_FIRST + i]
                       for i in range(PARAM_INDEX_LAST + 1)},
            "dump": bytes(msg),
            "container": "mid",
        })
    return rows


# ---------------------------------------------------------------------------
# Injectable morphs (xenia_morphs.json -> per-step sysex)
# ---------------------------------------------------------------------------

def _bank_files(root: str) -> List[str]:
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for fn in sorted(filenames):
            if os.path.splitext(fn)[1].lower() in \
                    (".mid", ".syx", ".usb", ".\u00b5sb"):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def _named_values(preset: dict,
                  name_to_off: Dict[str, int]) -> List[Tuple[str, int, int]]:
    """(name, dump offset, int value) for the preset's mappable params."""
    out = []
    for name, value in sorted((preset.get("params") or {}).items()):
        if value is None:
            continue
        off = name_to_off.get(name)
        if off is None:
            continue
        out.append((name, off, int(round(value)) if isinstance(value, float)
                    else value))
    return out


def _matches(dump: bytes, named: Sequence[Tuple[str, int, int]],
             shift: int) -> int:
    """#named values equal to the dump byte at off+shift (7-bit values)."""
    ok = 0
    for _name, off, value in named:
        if not 0 <= value <= 127:
            continue
        pos = off + shift
        if IDX_PARAM_FIRST <= pos <= IDX_PARAM_LAST and dump[pos] == value:
            ok += 1
    return ok


def resolve_parent_dump(preset: dict, bank_root: str,
                        name_to_off: Dict[str, int]) -> Tuple[bytes, dict]:
    """Byte-verified parent-A dump for a sheet preset.

    Tries every example patch name over the sorted bank corpus, first under
    the verified labeling (byte at 7+index), then under the harvest's
    shifted labeling (byte at 5+index -- the .mid sidecar off-by-2 quirk).
    Raises DumpError when nothing verifies.
    """
    named = _named_values(preset, name_to_off)
    if len(named) < MIN_PARENT_CHECKS:
        raise DumpError("preset %r carries only %d mappable named params "
                        "(need >= %d)" % (preset.get("name"), len(named),
                                          MIN_PARENT_CHECKS))
    examples = set(preset.get("examples", []))
    shifted_fallback = None
    for path in _bank_files(bank_root):
        for row in load_bank(path):
            if row["name"] not in examples:
                continue
            if _matches(row["dump"], named, 0) == len(named):
                return row["dump"], {"bank": os.path.basename(path),
                                     "patch": row["name"],
                                     "container": row["container"],
                                     "how": "verified",
                                     "byteMatches": len(named)}
            # the harvest's +2 labeling quirk only applies to .mid sidecars;
            # a shifted match in a .usb bank would be coincidence
            if shifted_fallback is None and row["container"] == "mid" \
                    and _matches(row["dump"], named, -2) == len(named):
                shifted_fallback = (row, path)
    if shifted_fallback is not None:
        row, path = shifted_fallback
        return row["dump"], {"bank": os.path.basename(path),
                             "patch": row["name"],
                             "container": row["container"],
                             "how": "vocab-shifted",
                             "byteMatches": len(named)}
    raise DumpError("no byte-verified parent dump for preset %r"
                    % preset.get("name"))


def build_injectable_morphs(morphs_path: str, bank_root: str,
                            offset_map_path: Optional[str] = None) -> dict:
    """Companion document of xenia_morphs.json with per-step ``sysex``.

    Pairs/steps/interpolated values are reused verbatim; each pair gains
    ``baseResolution`` and each step a ``sysex`` array (265 ints) built from
    the parent-A dump with the step's mappable named values at their mapped
    offsets (shift -2 for ``vocab-shifted`` pairs, see resolve_parent_dump).
    """
    name_to_off = name_to_offset(offset_map_path)
    with open(morphs_path, encoding="utf-8") as fh:
        morphs = json.load(fh)
    sheet_path = os.path.join(os.path.dirname(os.path.abspath(morphs_path)),
                              os.path.basename(
                                  morphs.get("sourceSheet", "xenia.json")))
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    by_id = {p["id"]: p for p in sheet["presets"]}
    pairs = []
    for pair in morphs["pairs"]:
        parent = by_id[pair["parents"][0]["id"]]
        base, resolution = resolve_parent_dump(parent, bank_root,
                                               name_to_off)
        shift = -2 if resolution["how"] == "vocab-shifted" else 0
        steps = []
        for step in pair["steps"]:
            overrides = {name: value for name, off, value in
                         _named_values(step["preset"], name_to_off)}
            if shift:
                dump = bytearray(base)
                for name, value in overrides.items():
                    pos = name_to_off[name] + shift
                    dump[pos] = _param_byte(value, name)
                dump[IDX_CHECKSUM] = compute_checksum(dump)
                sysex = bytes(dump)
            else:
                sysex = patch_dump(base, overrides, name_to_off=name_to_off)
            steps.append({"preset": step["preset"], "step": step["step"],
                          "sysex": list(sysex)})
        pairs.append({"baseResolution": resolution,
                      "distance": pair["distance"],
                      "pair": pair["pair"],
                      "parents": pair["parents"],
                      "steps": steps})
    return {
        "derivedFrom": "timbre-lib/matrix_presets/xenia_morphs.json",
        "pairs": pairs,
        "schema": morphs.get("schema", "hdaw.matrix.preset.morph.v1"),
        "sourceSheet": morphs.get("sourceSheet",
                                  "timbre-lib/matrix_presets/xenia.json"),
        "sysexRoots": [os.path.basename(os.path.normpath(bank_root))],
        "unverified": True,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_set_spec(specs: Iterable[str]) -> Dict[str, int]:
    """['EffectType=5', 'F1Cutoff=90'] / comma lists -> {key: value}."""
    out: Dict[str, int] = {}
    for spec in specs:
        for item in str(spec).split(","):
            item = item.strip()
            if not item:
                continue
            if "=" not in item:
                raise DumpError("--set expects name=value, got %r" % item)
            key, _, raw = item.partition("=")
            try:
                out[key.strip()] = int(raw.strip(), 0)
            except ValueError:
                raise DumpError("--set value for %r is not an int: %r"
                                % (key, raw)) from None
    return out


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Microwave XT/Xenia single-dump writer.")
    parser.add_argument("--base-bank", default=None,
                        help="bank file (.mid/.syx/.usb) for the base patch")
    parser.add_argument("--patch", default=None,
                        help="base patch name inside the bank")
    parser.add_argument("--set", dest="set_spec", action="append",
                        default=[],
                        help="param override name=value or index=value "
                             "(repeatable, comma lists ok)")
    parser.add_argument("--name", default=None,
                        help="16-char patch name (byte 247)")
    parser.add_argument("--device", type=lambda v: int(v, 0), default=0x00,
                        help="device id byte (default 0)")
    parser.add_argument("--bank", type=lambda v: int(v, 0), default=0x00,
                        help="location byte (0=A, 1=B, 0x20=edit buffer "
                             "single mode)")
    parser.add_argument("--program", type=lambda v: int(v, 0), default=0x00,
                        help="program byte (default 0)")
    parser.add_argument("--offset-map", default=None,
                        help="xenia_offset_map.json (default: shipped map)")
    parser.add_argument("--list-patches", action="store_true",
                        help="list the bank's patches and exit")
    parser.add_argument("--out", default=None, help="output .syx path")
    args = parser.parse_args(argv)

    try:
        if not args.base_bank:
            parser.error("nothing to do: pass --base-bank")
        rows = load_bank(args.base_bank)
        if args.list_patches:
            for row in rows:
                print("%-18r %s" % (row["name"], row["container"]))
            return 0
        if not args.patch or not args.out:
            raise DumpError("nothing to write: pass --patch and --out "
                            "(or --list-patches)")
        matches = [r for r in rows if r["name"] == args.patch]
        if not matches:
            raise DumpError("patch %r not found in %s (%d patches)"
                            % (args.patch, args.base_bank, len(rows)))
        base = matches[0]
        overrides = parse_set_spec(args.set_spec)
        sysex = build_single_dump(
            base["params"], overrides, name=args.name,
            device=args.device, bank=args.bank, program=args.program,
            name_to_off=name_to_offset(args.offset_map))
        parent = os.path.dirname(os.path.abspath(args.out))
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(args.out, "wb") as fh:
            fh.write(sysex)
        print("wrote %s (%d bytes, %d overrides)"
              % (args.out, len(sysex), len(overrides)))
        return 0
    except (ValueError, KeyError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
