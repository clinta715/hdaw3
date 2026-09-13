#!/usr/bin/env python3
r"""Nord Lead 2x (NodalRed2x) patch decoder + sidecar sweep.

Mirrors ``virus_patch.py`` for the Clavia SysEx containers found in
``D:\pdf\NL2x Banks`` (6898 files: 5273 .syx, 82 .mid, 26 .fxb), producing
``<patch>.nl2x.json`` sidecars (schema ``hdaw.nl2x.patch.v1``) and a library
survey report.

Wire format (gearmulator 2.2.9 ``source/nord/n2x/n2xLib/n2xmiditypes.h``,
validated against the real bank library on 2026-09-13):

  * single dump  -- F0 33 <dev> 04 <type> <spec> | 132 data | F7   (139 B)
  * multi dump   -- F0 33 <dev> 04 <type> <spec> | 708 data | F7   (715 B)
  * "+name" variants append 10 ASCII bytes before F7 (149/725 B; Aura editor).
  * .syx files concatenate F0..F7 blocks; SMF .mid banks wrap the SAME dumps
    as SMF sysex events whose payload omits the leading F0 (varlen counted);
    1062/1063-byte payloads are the multi dumps.
  * .fxb is a VST FXB chunk (``CcnK`` magic) for the Discovery Pro plugin --
    detected and skipped, never parsed.

Parameter bytes are nibble-encoded: param i lives at data offset 6 + 2i,
value = (data[off] & 0xf) | (data[off+1] << 4).  66 params per single; the
firmware SingleParam enum names 0..24 and 50..65 -- unnamed indices are
reported as ``param<NN>`` with raw values, never dropped.  Sync and
Distortion share byte 52 (bit1-2 sync, bit4 distortion) per
State::changeSingleParameter in n2xstate.cpp.

Usage:
    py -3.14 timbre-lib/nl2x_patch.py --dump <file>            # decode one file
    py -3.14 timbre-lib/nl2x_patch.py --survey "D:\pdf\NL2x Banks" \
        --out timbre-lib/nl2x_survey.json
    py -3.14 timbre-lib/nl2x_patch.py --sidecars "D:\pdf\NL2x Banks"
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import struct
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import role_targets as RT

NL2X_SCHEMA = "hdaw.nl2x.patch.v1"

# ---------------------------------------------------------------------------
# Wire constants (n2xmiditypes.h)
# ---------------------------------------------------------------------------

ID_CLAVIA = 0x33
ID_N2X = 0x04
SYSEX_HEADER_SIZE = 6          # F0, IdClavia, IdDevice, IdN2x, MsgType, MsgSpec
PAYLOAD_HEADER_SIZE = 5        # same header without the leading F0 (payload)
SINGLE_DATA_SIZE = 66 * 2      # 66 params, two nibbles each
MULTI_DATA_SIZE = 4 * SINGLE_DATA_SIZE + 90 * 2
NAME_LENGTH = 10               # Aura editor "+name" variant suffix
SINGLE_DUMP_SIZE = SYSEX_HEADER_SIZE + SINGLE_DATA_SIZE + 1            # 139
SINGLE_DUMP_WITH_NAME = SINGLE_DUMP_SIZE + NAME_LENGTH                 # 149
MULTI_DUMP_SIZE = SYSEX_HEADER_SIZE + MULTI_DATA_SIZE + 1              # 715
MULTI_DUMP_WITH_NAME = MULTI_DUMP_SIZE + NAME_LENGTH                   # 725

SUPPORTED_FORMATS = ("nl2xsyx", "nl2xmid")

NL2X_FORMAT_LABELS = {
    "nl2xsyx": "NL2x SysEx",
    "nl2xmid": "NL2x Std-MIDI",
}

# ---------------------------------------------------------------------------
# Single parameters (SingleParam enum, n2xmiditypes.h)
# ---------------------------------------------------------------------------

PARAM_NAMES = {
    0: "o2_pitch", 1: "o2_pitch_fine", 2: "mix", 3: "cutoff",
    4: "resonance", 5: "filter_env_amount", 6: "pw", 7: "fm_depth",
    8: "filter_env_attack", 9: "filter_env_decay",
    10: "filter_env_sustain", 11: "filter_env_release",
    12: "amp_env_attack", 13: "amp_env_decay",
    14: "amp_env_sustain", 15: "amp_env_release",
    16: "portamento", 17: "gain",
    18: "mod_env_attack", 19: "mod_env_decay",
    20: "mod_env_level", 21: "lfo1_rate", 22: "lfo1_level", 23: "lfo2_rate",
    24: "arp_range",
    50: "o1_waveform", 51: "o2_waveform", 52: "sync_distortion",
    53: "filter_type", 54: "o2_keytrack", 55: "filter_keytrack",
    56: "lfo1_waveform", 57: "lfo1_dest", 58: "voice_mode",
    59: "mod_wheel_dest", 60: "unison", 61: "mod_env_dest",
    62: "auto", 63: "filter_velocity", 64: "octave_shift",
    65: "lfo2_dest",
}

# Human names used by the description builder.
_WAVEFORMS_O1 = {0: "pulse", 1: "pulse", 2: "saw", 3: "tri", 4: "noise",
                 40: "pulse SW", 41: "pulse RW", 42: "saw SW", 43: "saw RW",
                 44: "noise RW"}
_WAVEFORMS_O2 = {0: "off", 1: "pulse", 2: "saw", 3: "tri", 4: "sin",
                 5: "noise", 32: "piano", 33: "organ", 34: "EP-1",
                 35: "EP-2", 36: "EP-3", 37: "EP-4", 38: "clav",
                 39: "mallet", 40: "FM sin", 41: "FM 2op", 42: "FM 3op",
                 43: "FM 4op", 44: "FM bell", 45: "FM e-piano",
                 46: "wave 1", 47: "wave 2", 48: "wave 3", 49: "wave 4",
                 50: "wave 5", 51: "wave 6"}
_FILTER_TYPES = {0: "12 dB LP", 1: "24 dB LP", 2: "12 dB HP", 3: "BP"}
_LFO_WAVES = {0: "triangle", 1: "sine", 2: "sawtooth", 3: "exp decay",
              4: "square", 5: "random", 6: "s+h", 7: "max rise",
              8: "max fall", 9: "sample+hold"}
_VOICE_MODES = {0: "poly", 1: "legato", 2: "mono", 3: "mono legato"}


def _param_name(index):
    return PARAM_NAMES.get(index, f"param{index:02d}")


# ---------------------------------------------------------------------------
# SysEx splitting / classification
# ---------------------------------------------------------------------------

def _is_nl2x_header(data, i, has_f0):
    """True if data[i:] starts a Clavia N2x dump at the given anchor.

    has_f0=True:  data[i]=F0, data[i+1]=33, data[i+3]=04
    has_f0=False: data[i]=33,  data[i+2]=04  (SMF payload w/o leading F0)
    """
    if has_f0:
        return (i + 4 <= len(data) and data[i] == 0xF0
                and data[i + 1] == ID_CLAVIA and data[i + 3] == ID_N2X)
    return (i + 3 <= len(data) and data[i] == ID_CLAVIA
            and data[i + 2] == ID_N2X)


def _find_f7(data, i):
    return data.find(b"\xf7", i)


def split_dumps(data):
    """Split concatenated NL2x SysEx blocks from raw bytes.

    Yields (payload, has_f0) where payload starts at the Clavia ID byte
    (i.e. excludes the leading F0 when present) and excludes F7."""
    out = []
    i = 0
    n = len(data)
    while i < n:
        if data[i] == 0xF0 and _is_nl2x_header(data, i, True):
            end = _find_f7(data, i)
            if end < 0:
                break
            out.append((data[i + 1:end], True))
            i = end + 1
        elif data[i] == ID_CLAVIA and _is_nl2x_header(data, i, False):
            end = _find_f7(data, i)
            if end < 0:
                break
            out.append((data[i:end], False))
            i = end + 1
        else:
            i += 1
    return out


def _smf_varlen(data, pos):
    value = 0
    while True:
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, pos


def split_smf_sysex(data):
    """NL2x payloads from a Standard MIDI File (events carry no leading F0)."""
    if len(data) < 14:
        return []
    if data[:4] == b"RIFF":  # RIFF MIDI (.rmi): unwrap the data chunk
        idx = data.find(b"data")
        if idx < 0:
            return []
        size = struct.unpack("<I", data[idx + 4:idx + 8])[0]
        data = data[idx + 8:idx + 8 + size]
    elif data[:4] != b"MThd":
        return []
    out = []
    pos = 0
    n = len(data)
    while pos < n - 3:
        if data[pos] == 0xF0:
            try:
                ln, q = _smf_varlen(data, pos + 1)
            except IndexError:
                break
            if q + ln > n:
                break
            payload = data[q:q + ln]
            if payload and payload[-1] == 0xF7:
                payload = payload[:-1]  # SMF length includes F7; unify w/o F7
            if (len(payload) > 3 and payload[0] == ID_CLAVIA
                    and payload[2] == ID_N2X):
                out.append(payload)
            pos = q + ln
        else:
            pos += 1
    return out


MULTI_DUMP_BANK_SIZE = 1063          # real bank files: 4x132 data + F7
MULTI_DUMP_BANK_WITH_NAME = MULTI_DUMP_BANK_SIZE + NAME_LENGTH


def classify(payload):
    """(kind, with_name, msg_type, msg_spec) for a payload without F0/F7."""
    if len(payload) < 6 or payload[0] != ID_CLAVIA or payload[2] != ID_N2X:
        return None
    total = len(payload) + 2  # + F0 + F7
    if total in (SINGLE_DUMP_SIZE, SINGLE_DUMP_WITH_NAME):
        kind = "single"
    elif total in (MULTI_DUMP_SIZE, MULTI_DUMP_WITH_NAME,
                   MULTI_DUMP_BANK_SIZE, MULTI_DUMP_BANK_WITH_NAME):
        kind = "multi"
    else:
        return None
    with_name = total in (SINGLE_DUMP_WITH_NAME, MULTI_DUMP_WITH_NAME)
    return kind, with_name, payload[3], payload[4]


def extract_name(payload, kind):
    """Patch name from an Aura +name dump; None when the variant is absent."""
    _kindname, with_name, _t, _s = kind
    if not with_name:
        return None
    raw = payload[-NAME_LENGTH:]
    if not raw or all(b in (0, 0x20) for b in raw):
        return ""
    return raw.decode("ascii", errors="replace").rstrip("\x00").strip()


# ---------------------------------------------------------------------------
# Patch extraction
# ---------------------------------------------------------------------------

def _decode_nibbles(data, offset, count):
    return [(data[offset + i] & 0xF) | (data[offset + i + 1] << 4)
            for i in range(0, count * 2, 2)]


# payload = dump without the leading F0 (and without F7), so parameter i
# lives at PAYLOAD_HEADER_SIZE + 2*i (mirrors getOffsetInSingleDump's
# SYSEX_HEADER_SIZE + 2i on the F0-anchored dump).



def _patch_from_payload(payload, kind):
    kindname, _with_name, msg_type, msg_spec = kind
    name = extract_name(payload, kind)
    if kindname == "single":
        params = _decode_nibbles(payload, PAYLOAD_HEADER_SIZE, 66)
        program = msg_spec
    else:
        # multi = 4 embedded singles then multi params; patch 0 is first.
        params = _decode_nibbles(payload, PAYLOAD_HEADER_SIZE, 66)
        program = msg_spec - 99 if msg_spec >= 99 else msg_spec
    return {
        "kind": kindname,
        "name": name or "",
        "params": params,
        "program": program,
        "msgType": msg_type,
        "msgSpec": msg_spec,
        "error": None,
    }


def decode_params(payload):
    """The 66 single params from a single (or first-single-of-multi) dump."""
    return _decode_nibbles(payload, PAYLOAD_HEADER_SIZE, 66)


def parse_syx(data):
    patches = []
    for payload, _has_f0 in split_dumps(data):
        kind = classify(payload)
        if kind is None:
            continue
        patches.append(_patch_from_payload(payload, kind))
    return patches


def parse_midi(data):
    patches = []
    for payload in split_smf_sysex(data):
        kind = classify(payload)
        if kind is None:
            continue
        patches.append(_patch_from_payload(payload, kind))
    return patches


def parse_file(data):
    """Patches from any supported container (survey + sweep entry point)."""
    if data[:4] in (b"MThd", b"RIFF"):
        return parse_midi(data)
    return parse_syx(data)


# ---------------------------------------------------------------------------
# Detection
# ---------------------------------------------------------------------------

def _has_nl2x_payload(data):
    limit = min(len(data), 8192)
    for i in range(limit):
        if data[i] == 0xF0 and _is_nl2x_header(data, i, True):
            return True
        if data[i] == ID_CLAVIA and _is_nl2x_header(data, i, False):
            return True
    if data[:4] in (b"MThd", b"RIFF"):
        return bool(split_smf_sysex(data))
    return False


def detect_format(data, path=""):
    """'nl2xsyx' / 'nl2xmid' for NL2x containers; None otherwise."""
    if data[:4] in (b"MThd", b"RIFF"):
        return "nl2xmid" if split_smf_sysex(data) else None
    if _has_nl2x_payload(data):
        return "nl2xsyx"
    return None


# ---------------------------------------------------------------------------
# Description + pseudo-measurements
# ---------------------------------------------------------------------------

def _is_percussive(params):
    amp_s = params[14] if 14 < len(params) else 0
    amp_d = params[13] if 13 < len(params) else 127
    return amp_s < 16 and amp_d < 60


def describe(params):
    """Concise human description from the 66 params (documented heuristic)."""
    def g(i):
        return params[i] if i < len(params) else 0

    o1 = _WAVEFORMS_O1.get(g(50), f"o1 wave {g(50)}")
    o2 = _WAVEFORMS_O2.get(g(51), f"o2 wave {g(51)}")
    ftype = _FILTER_TYPES.get(g(53) & 0x3, "filter")
    cutoff = g(3)
    amp_a, amp_d, amp_s, amp_r = g(12), g(13), g(14), g(15)
    fm = g(7)
    sync = bool(g(52) & 0x3)
    dist = bool(g(52) & 0x10)
    unison = bool(g(60))
    res = g(4)

    bits = ["percussive" if _is_percussive(params) else "tonal"]
    if cutoff < 40:
        bits.append("dark")
    elif cutoff > 88:
        bits.append("bright")
    if amp_a > 40:
        bits.append("gradual attack")
    elif amp_a <= 5:
        bits.append("fast attack")
    if amp_d < 20 and amp_s < 40:
        bits.append("short decay (energy dies quickly)")
    elif amp_s > 100:
        bits.append("sustained")
    if amp_r > 100:
        bits.append("long release")
    if unison:
        bits.append("unison")
    if sync:
        bits.append("osc sync")
    if dist:
        bits.append("distortion")
    if fm > 40:
        bits.append("FM flavor")
    if res > 80:
        bits.append("resonant")
    return f"{o1}+{o2} into {ftype}, " + ", ".join(bits)


_PSEUDO_NOTE = "pseudo-measurements from patch bytes, not audio analysis"


def pseudo_measurements(params):
    """NL2x patch bytes to timbre/role measurement keys (documented proxy).

    cutoff to centroid Hz (exponential 20..20k), resonance to mel_mid,
    fm_depth to mel_high, amp env to attack/decay.  Honest about being
    bytes-not-audio (roleCheck note says so)."""
    def g(i, default=0.0):
        return (params[i] / 127.0) if i < len(params) else default

    cutoff = g(3)
    resonance = g(4)
    fm = g(7)
    amp_attack = g(12)
    amp_decay = g(13)
    return {
        "centroid": round(20.0 * (1000.0 ** cutoff), 4),
        "mel_low": round(min(1.0, 0.10 + (1.0 - cutoff) * 0.35), 4),
        "mel_mid": round(min(1.0, 0.20 + resonance * 0.30), 4),
        "mel_high": round(min(1.0, 0.02 + fm * 0.30), 4),
        "attack_s": round(0.01 + amp_attack * 0.50, 4),
        "decay_s": round(0.05 + amp_decay * 1.20, 4),
        "tonal_fraction": 1.0,
        "f0_hz": 0.0,
    }


# ---------------------------------------------------------------------------
# Sidecar + sweep
# ---------------------------------------------------------------------------

def mapped_entries(params):
    """All 66 params as named entries (raw 0..127, value = raw/127)."""
    return {str(i): {"param": _param_name(i), "raw": raw,
                     "value": round(raw / 127.0, 6)}
            for i, raw in enumerate(params)}


def static_description(patch):
    name = str(patch.get("name", "")).strip()
    label = NL2X_FORMAT_LABELS.get(patch.get("format", ""), "NL2x")
    parts = [f"Nord Lead 2x {label} patch"]
    if name:
        parts.append(f"'{name}'")
    if patch.get("program") is not None:
        parts.append(f"(program {patch['program']})")
    return " ".join(parts)


def build_sidecar(patch, role=None, format=None):
    """Build the ``<patch>.nl2x.json`` document for a parsed patch."""
    params = patch.get("params") or []
    side = {
        "schema": NL2X_SCHEMA,
        "name": patch.get("name", ""),
        "engine": "nodalred2x",
        "format": format or patch.get("format", ""),
        "mappedParams": mapped_entries(params),
        "unmapped": [],  # the dump IS the full param set; nothing is dropped
        "description": describe(params),
    }
    if patch.get("program") is not None:
        side["program"] = patch["program"]
    if role:
        role_norm = RT.normalize_role(role)
        if role_norm not in RT.SUPPORTED_ROLES:
            raise ValueError(f"unsupported role '{role}' (supported: "
                             f"{', '.join(RT.SUPPORTED_ROLES)})")
        rc = RT.check_role(role_norm, pseudo_measurements(params))
        rc["note"] = _PSEUDO_NOTE
        side["roleCheck"] = rc
    return side


def _read(path):
    with open(path, "rb") as fh:
        return fh.read()


def _sweep_file(full, role=None):
    try:
        data = _read(full)
    except OSError as e:
        return ("failed", None, f"read failed: {e}")
    fmt = detect_format(data, full)
    if fmt is None:
        return ("skipped", None, None)
    try:
        patches = parse_file(data)
    except Exception as e:  # noqa: BLE001 - a bad file must not kill the sweep
        return ("failed", fmt, f"parse raised: {e}")
    if not patches:
        return ("skipped", fmt, None)
    patch = patches[0]
    if not patch.get("name"):
        patch["name"] = os.path.splitext(os.path.basename(full))[0]
    try:
        side = build_sidecar(patch, role=role, format=fmt)
    except ValueError as e:
        return ("failed", fmt, str(e))
    out_path = full + ".nl2x.json"
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(json.dumps(side, sort_keys=True, indent=2) + "\n")
    return ("parsed", fmt, out_path)


def sweep_sidecars(root, role=None):
    """Walk ``root`` writing ``<patch>.nl2x.json`` per patch file."""
    formats = {fmt: {"files": 0, "parsed": 0, "failed": 0}
               for fmt in SUPPORTED_FORMATS}
    totals = {"files": 0, "parsed": 0, "failed": 0, "skipped": 0,
              "sidecars": 0}
    errors = []
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            full = os.path.join(dirpath, fn)
            totals["files"] += 1
            status, fmt, info = _sweep_file(full, role=role)
            if status == "parsed":
                totals["parsed"] += 1
                totals["sidecars"] += 1
                formats[fmt]["files"] += 1
                formats[fmt]["parsed"] += 1
            elif status == "failed":
                totals["failed"] += 1
                errors.append((full, info))
                if fmt:
                    formats[fmt]["files"] += 1
                    formats[fmt]["failed"] += 1
            else:
                totals["skipped"] += 1
    return {"root": root, "formats": formats, "totals": totals,
            "errors": errors}


def print_summary(summary):
    out = sys.stdout
    out.write(f"sidecar sweep: {summary['root']}\n")
    out.write("format     files  parsed  failed\n")
    for fmt in SUPPORTED_FORMATS:
        f = summary["formats"][fmt]
        out.write(f"{fmt:<10} {f['files']:>5} {f['parsed']:>7} "
                  f"{f['failed']:>7}\n")
    t = summary["totals"]
    out.write(f"{'total':<10} {t['files']:>5} {t['parsed']:>7} "
              f"{t['failed']:>7}  (skipped {t['skipped']})\n")
    out.write(f"sidecars written: {t['sidecars']}\n")
    if summary["errors"]:
        out.write("errors:\n")
        for path, msg in summary["errors"][:20]:
            out.write(f"  {path}: {msg}\n")
        if len(summary["errors"]) > 20:
            out.write(f"  ... and {len(summary['errors']) - 20} more\n")


# ---------------------------------------------------------------------------
# Survey
# ---------------------------------------------------------------------------

def survey(root):
    """Full-library survey -> report dict (mirrors virus_patch.survey)."""
    formats = {}
    kinds = Counter()
    totals = {"files": 0, "patches": 0, "dumps": 0, "parsed": 0,
              "failed": 0, "skipped": 0}
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            full = os.path.join(dirpath, fn)
            ext = os.path.splitext(fn)[1].lower()
            if ext in (".fxb", ".fxp"):
                totals["skipped"] += 1
                continue
            try:
                data = _read(full)
            except OSError:
                totals["failed"] += 1
                continue
            fmt = detect_format(data, full)
            if fmt is None:
                totals["skipped"] += 1
                continue
            totals["files"] += 1
            try:
                patches = parse_file(data)
            except Exception:  # noqa: BLE001 - a bad file must not kill it
                patches = []
            singles = sum(1 for p in patches if p["kind"] == "single")
            multis = len(patches) - singles
            totals["dumps"] += len(patches)
            kinds["single"] += singles
            kinds["multi"] += multis
            f = formats.setdefault(fmt, {"files": 0, "patches": 0,
                                         "singles": 0, "multis": 0,
                                         "failed": 0})
            f["files"] += 1
            if patches:
                totals["parsed"] += 1
                f["patches"] += len(patches)
                f["singles"] += singles
                f["multis"] += multis
            else:
                totals["failed"] += 1
                f["failed"] += 1
    totals["patches"] = kinds["single"] + kinds["multi"]
    return {
        "schema": "hdaw.nl2x.survey.v1",
        "generated_at": datetime.datetime.now().isoformat(
            timespec="seconds"),
        "root": root,
        "totals": totals,
        "kinds": dict(kinds),
        "formats": formats,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Nord Lead 2x (NodalRed2x) patch decoder + sidecar "
                    "sweep.")
    ap.add_argument("--survey", metavar="ROOT",
                    help="survey a preset root (e.g. D:\\pdf\\NL2x Banks)")
    ap.add_argument("--out", metavar="PATH",
                    help="write the survey JSON to PATH (default stdout)")
    ap.add_argument("--dump", metavar="FILE", help="decode one file")
    ap.add_argument("--sidecars", metavar="DIR",
                    help="write <patch>.nl2x.json next to every patch file "
                         "under DIR")
    ap.add_argument("--role", metavar="R",
                    help="role for roleCheck blocks (bass, lead, pad, ...)")
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    if args.sidecars:
        if not os.path.isdir(args.sidecars):
            sys.stderr.write(f"sidecars root not found: {args.sidecars}\n")
            return 1
        try:
            summary = sweep_sidecars(args.sidecars, role=args.role)
        except OSError as e:
            sys.stderr.write(f"sidecar sweep failed: {e}\n")
            return 1
        print_summary(summary)
        return 0

    if args.dump:
        with open(args.dump, "rb") as fh:
            data = fh.read()
        fmt = detect_format(data, args.dump)
        if fmt is None:
            print(f"not an NL2x container: {args.dump}")
            return 1
        patches = parse_file(data)
        for i, p in enumerate(patches):
            if i >= 8:
                print(f"... ({len(patches)} patches total)")
                break
            side = build_sidecar(p, format=fmt)
            print(json.dumps(side, sort_keys=True, indent=2))
        return 0

    if args.survey:
        report = survey(args.survey)
        text = json.dumps(report, sort_keys=True, indent=2)
        if args.out:
            with open(args.out, "w", encoding="utf-8") as fh:
                fh.write(text + "\n")
            print(f"survey written: {args.out}")
        else:
            print(text)
        return 0

    ap.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
