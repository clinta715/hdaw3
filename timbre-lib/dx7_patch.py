#!/usr/bin/env python3
"""DX7 .syx patch decoder + fm_synth sidecar writer.

Parses the two DX7 sysex containers, mirroring the byte semantics of
``src/engine/Dx7SysexImport.cpp``:

  * single voice dump   F0 43 00 00 01 1B <155 VCED> <cs> F7   (163 bytes)
  * cartridge dump      F0 43 00 09 20 00 <4096 VMEM> <cs> F7  (4104 bytes)

The 156-byte engine VCED layout (``Dx7Voice::patchData``) is canonical:
name at [145:155] (10 chars), algorithm at [134] & 0x1F, feedback at
[135] & 0x07.  The checksum byte is the value that makes
``(sum(data) + cs) & 0x7F == 0``.  Cartridge voices are 128-byte VMEM
voices unpacked to the 156-byte layout with the same bit arithmetic as
``unpackVmemVoice()``.

``--sidecars <dir>`` walks a directory and writes ``<file>.dx7.json`` next
to every patch-containing file (first voice for cartridges, with
``voiceCount`` recorded), matching the ``.timbre.json`` sidecar convention.
Sidecars use ``json.dumps(sort_keys=True)`` so re-running over the same
directory is byte-identical.  Patch files are never modified.

Exit 0 on a completed sweep (per-file failures are recorded in the printed
summary), exit 1 on a hard error (unreadable root, invalid arguments).
"""
import argparse
import json
import os
import sys

# ---------------------------------------------------------------------------
# Format constants (mirroring Dx7SysexImport.cpp)
# ---------------------------------------------------------------------------

SINGLE_LEN = 163        # F0 43 00 00 01 1B + 155 VCED + checksum + F7
CARTRIDGE_LEN = 4104    # F0 43 00 09 20 00 + 4096 VMEM + checksum + F7
VCED_DATA_LEN = 155
VMEM_TOTAL = 4096

NAME_OFF = 145
NAME_LEN = 10
ALGO_OFF = 134
FEEDBACK_OFF = 135
ENGINE_TAIL = 0x3F      # the fixed 156th engine VCED byte (Dx7Voice)

SCHEMA = "hdaw.dx7.patch.v1"

# The 21 bytes per operator inside the 156-byte engine VCED.
_OP_FIELDS = (
    "eg_rate_1", "eg_rate_2", "eg_rate_3", "eg_rate_4",
    "eg_level_1", "eg_level_2", "eg_level_3", "eg_level_4",
    "breakpoint", "left_depth", "right_depth", "left_curve", "right_curve",
    "rate_scale", "amp_mod_sens", "key_vel_sens", "output_level",
    "osc_mode", "freq_coarse", "freq_fine", "detune",
)

# Non-operator global VCED offsets (engine layout).
_GLOBAL_OFFSETS = {
    "pitch_eg_rate_1": 126, "pitch_eg_rate_2": 127,
    "pitch_eg_rate_3": 128, "pitch_eg_rate_4": 129,
    "pitch_eg_level_1": 130, "pitch_eg_level_2": 131,
    "pitch_eg_level_3": 132, "pitch_eg_level_4": 133,
    "algorithm": 134,
    "feedback": 135,
    "osc_sync": 136,
    "lfo_speed": 137,
    "lfo_delay": 138,
    "lfo_pitch_mod_depth": 139,
    "lfo_amp_mod_depth": 140,
    # 141..143 hold the engine-packed LFO waveform bits (unpackVmemVoice
    # reads them as packed[116] & 0x01, >>1 & 0x07, >>4 & 0x07); exposed raw.
    "lfo_wave_bits_0": 141,
    "lfo_wave_bits_1": 142,
    "lfo_wave_bits_2": 143,
    "transpose": 144,
}


def _checksum_ok(data):
    return (sum(data) & 0x7F) == 0


def _name_from_vced(vced):
    raw = vced[NAME_OFF:NAME_OFF + NAME_LEN]
    chars = [c for c in raw if 0x20 <= c < 0x7F]
    return "".join(chr(c) for c in chars).rstrip()


def _map_vced_params(vced):
    """Flatten the 156-byte engine VCED into an fm_synth-named param dict.

    Values are the raw VCED bytes; the naming mirrors the operator
    parameters FmSynthEngine exposes (op level/coarse/fine/detune, LFO,
    algorithm, feedback).  Offsets follow the engine layout defined by
    ``unpackVmemVoice()`` in Dx7SysexImport.cpp."""
    params = {}
    for op in range(6):
        base = op * 21
        for i, field in enumerate(_OP_FIELDS):
            params[f"op{op + 1}_{field}"] = vced[base + i]
    for name, off in _GLOBAL_OFFSETS.items():
        params[name] = vced[off]
    return params


def _voice_base(fmt, vced, error=None, extra=None):
    voice = {
        "format": fmt,
        "name": "" if error else _name_from_vced(vced),
        "data": list(vced),
        "params": {} if error else _map_vced_params(vced),
        "error": error,
    }
    if extra:
        voice.update(extra)
    return voice


def _error_voice(fmt, message, extra=None):
    voice = {"format": fmt, "name": "", "data": [],
             "params": {}, "error": message}
    if extra:
        voice.update(extra)
    return voice


# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------

def parse_single(data):
    """Parse a 163-byte DX7 single voice dump -> [voice dict]."""
    if len(data) < 6 or data[0] != 0xF0 or data[1] != 0x43:
        return [_error_voice("single", "missing F0 43 header")]
    if data[3] != 0x00:
        return [_error_voice("single", f"not a single dump (byte 3 {data[3]:02X})")]
    data_len = (data[4] << 7) | data[5]
    if data_len != VCED_DATA_LEN:
        return [_error_voice("single",
                             f"byte count {data_len} (expected {VCED_DATA_LEN})")]
    if len(data) < 6 + VCED_DATA_LEN + 1:
        return [_error_voice("single", f"truncated ({len(data)} bytes)")]
    body = data[6:6 + VCED_DATA_LEN + 1]  # 155 data + checksum
    cs_ok = _checksum_ok(body)
    vced = data[6:6 + VCED_DATA_LEN] + bytes([ENGINE_TAIL])
    voice = _voice_base("single", vced, extra={
        "algorithm": vced[ALGO_OFF] & 0x1F,
        "feedback": vced[FEEDBACK_OFF] & 0x07,
        "checksum": data[6 + VCED_DATA_LEN],
        "checksum_ok": cs_ok,
        "voiceCount": 1,
    })
    if not cs_ok:
        voice["error"] = "checksum mismatch"
    return [voice]


def _unpack_vmem_voice(packed):
    """128-byte VMEM voice -> 156-byte engine VCED (mirror unpackVmemVoice)."""
    v = bytearray(156)
    for op in range(6):
        p = packed[op * 17:op * 17 + 17]
        u = op * 21
        v[u:u + 8] = p[0:8]
        v[u + 8] = p[8]
        v[u + 9] = p[9]
        v[u + 10] = p[10]
        v[u + 11] = p[11] & 0x03                # left curve
        v[u + 12] = (p[11] >> 2) & 0x03         # right curve
        v[u + 13] = p[12] & 0x07                # rate scale
        v[u + 14] = p[13] & 0x03                # amp mod sens
        v[u + 15] = (p[13] >> 2) & 0x07         # key vel sens
        v[u + 16] = p[14]                       # output level
        v[u + 17] = p[15] & 0x01                # osc mode
        v[u + 18] = (p[15] >> 1) & 0x1F         # freq coarse
        v[u + 19] = p[16]                       # freq fine
        v[u + 20] = (p[12] >> 3) & 0x0F         # detune
    v[126:134] = packed[102:110]                # pitch EG
    v[134] = packed[110] & 0x1F                 # algorithm
    v[135] = (packed[111] >> 1) & 0x07          # feedback
    v[136] = packed[111] & 0x01                 # osc key sync
    v[137:141] = packed[112:116]                # LFO speed/delay/depths
    v[141] = packed[116] & 0x01
    v[142] = (packed[116] >> 1) & 0x07
    v[143] = (packed[116] >> 4) & 0x07
    v[144] = packed[117]                        # transpose
    v[145:155] = packed[118:128]                # name
    v[155] = ENGINE_TAIL
    return bytes(v)


def parse_cartridge(data):
    """Parse a 4104-byte DX7 cartridge dump -> [32 voice dicts]."""
    if len(data) < 4104 or data[0] != 0xF0 or data[1] != 0x43:
        return [_error_voice("cartridge", "missing F0 43 header")]
    if data[3] != 0x09:
        return [_error_voice("cartridge",
                             f"not a cartridge dump (byte 3 {data[3]:02X})")]
    if not _checksum_ok(data[6:6 + VMEM_TOTAL + 1]):
        return [_error_voice("cartridge", "checksum mismatch")]
    voices = []
    for v in range(32):
        packed = data[6 + v * 128:6 + (v + 1) * 128]
        vced = _unpack_vmem_voice(packed)
        voices.append(_voice_base("cartridge", vced, extra={
            "algorithm": vced[ALGO_OFF] & 0x1F,
            "feedback": vced[FEEDBACK_OFF] & 0x07,
            "checksum": data[6 + VMEM_TOTAL],
            "checksum_ok": True,
            "voiceCount": 32,
            "index": v,
        }))
    return voices


PARSERS = {
    "single": parse_single,
    "cartridge": parse_cartridge,
}


def detect_format(data, path=""):
    """Guess the DX7 container from the header bytes (mirrors the C++)."""
    if len(data) < 6 or data[0] != 0xF0 or data[1] != 0x43:
        return None
    data_len = (data[4] << 7) | data[5]
    if data[3] == 0x00 and data_len == VCED_DATA_LEN and len(data) >= 162:
        return "single"
    # Cartridge: file size is ground truth (spec 0x20 / Dexed 0x10 at byte 4).
    if data[3] == 0x09 and len(data) >= 4104:
        return "cartridge"
    return None


# ---------------------------------------------------------------------------
# Sidecar writing
# ---------------------------------------------------------------------------

def build_sidecar(voice, voice_count=None):
    """Build the ``<patch>.dx7.json`` document for a parsed voice."""
    side = {
        "schema": SCHEMA,
        "name": voice.get("name", ""),
        "engine": "fm_synth",
        "format": voice.get("format", ""),
        "algorithm": voice.get("algorithm", 0),
        "feedback": voice.get("feedback", 0),
        "params": voice.get("params", {}),
    }
    if voice_count and voice_count > 1:
        side["voiceCount"] = voice_count
        side["description"] = (
            f"DX7 cartridge ({voice_count} voices): first voice algorithm "
            f"{side['algorithm']}, feedback {side['feedback']}")
    else:
        side["description"] = (f"DX7 algorithm {side['algorithm']}, "
                               f"feedback {side['feedback']}")
    return side


def _read_bytes(path):
    with open(path, "rb") as fh:
        return fh.read()


def _sweep_file(full):
    """Write one sidecar for a file -> (status, fmt, info)."""
    try:
        data = _read_bytes(full)
    except OSError as e:
        return ("failed", None, f"read failed: {e}")
    fmt = detect_format(data, full)
    if fmt is None:
        return ("skipped", None, None)
    try:
        voices = PARSERS[fmt](data)
    except Exception as e:  # noqa: BLE001 - a bad file must not kill the sweep
        return ("failed", fmt, f"parse raised: {e}")
    ok = [v for v in voices if not v["error"]]
    if not ok:
        msg = voices[0]["error"] if voices else "no voices parsed"
        return ("failed", fmt, msg)
    side = build_sidecar(ok[0], voice_count=len(voices))
    with open(full + ".dx7.json", "w", encoding="utf-8") as fh:
        fh.write(json.dumps(side, sort_keys=True, indent=2) + "\n")
    return ("parsed", fmt, full + ".dx7.json")


def write_sidecars(root):
    """Walk ``root`` writing ``<file>.dx7.json`` per patch file.

    Returns a summary dict; per-file failures are recorded, never raised.
    Raises OSError if the root is unreadable/missing."""
    formats = {fmt: {"files": 0, "parsed": 0, "failed": 0}
               for fmt in PARSERS}
    totals = {"files": 0, "parsed": 0, "failed": 0, "skipped": 0,
              "sidecars": 0}
    errors = []
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            full = os.path.join(dirpath, fn)
            totals["files"] += 1
            status, fmt, info = _sweep_file(full)
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
    out.write("format    files  parsed  failed\n")
    for fmt in sorted(summary["formats"]):
        f = summary["formats"][fmt]
        out.write(f"{fmt:<9} {f['files']:>5} {f['parsed']:>7} {f['failed']:>7}\n")
    t = summary["totals"]
    out.write(f"{'total':<9} {t['files']:>5} {t['parsed']:>7} {t['failed']:>7}"
              f"  (skipped {t['skipped']})\n")
    out.write(f"sidecars written: {t['sidecars']}\n")
    if summary["errors"]:
        out.write("errors:\n")
        for path, msg in summary["errors"][:20]:
            out.write(f"  {path}: {msg}\n")
        if len(summary["errors"]) > 20:
            out.write(f"  ... and {len(summary['errors']) - 20} more\n")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="DX7 .syx patch decoder + fm_synth sidecar writer.")
    ap.add_argument("--sidecars", metavar="DIR",
                    help="write <patch>.dx7.json next to every .syx patch "
                         "file under DIR")
    ap.add_argument("--dump", metavar="FILE", help="decode one file")
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    if args.dump:
        with open(args.dump, "rb") as fh:
            data = fh.read()
        fmt = detect_format(data, args.dump)
        if fmt is None:
            sys.stderr.write(f"could not detect DX7 format for {args.dump}\n")
            return 1
        voices = PARSERS[fmt](data)
        sys.stdout.write(json.dumps(voices, indent=2) + "\n")
        return 0

    if args.sidecars:
        if not os.path.isdir(args.sidecars):
            sys.stderr.write(f"sidecars root not found: {args.sidecars}\n")
            return 1
        try:
            summary = write_sidecars(args.sidecars)
        except OSError as e:
            sys.stderr.write(f"sidecar sweep failed: {e}\n")
            return 1
        print_summary(summary)
        return 0

    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())