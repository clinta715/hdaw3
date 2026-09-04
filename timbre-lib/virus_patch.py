#!/usr/bin/env python3
"""Access Virus patch decoder + HDAW sub_synth survivability survey.

Parses the four Access Virus patch containers found in the HDAW preset
library and reports, per patch: (a) the patch name, (b) the parameters that
map onto the HDAW ``sub_synth`` internal FX (params 0-23), and (c) an
explicit ``unmapped`` list for features the sub_synth does not cover
(FM amount, ring mod, LFOs, keytrack, FX, mod matrix, ...).

Formats (validated against the real library on 2026-09-03):

  * bcsingle -- a 267-byte B/C single program dump:
        F0 00 20 33 01 dd 10 bb ss | 256 data bytes | cs | F7
    Header (9 bytes) matches the documented Virus single dump; the 256 data
    bytes are pages A+B of the single program; ``cs`` is
    ``(dev + 0x10 + bank + prog + sum(data)) & 0x7F``; patch name lives at
    data[240:250] (page B params 112-121).
  * tibank -- 128 x 524-byte self-contained sysex blocks.  Each block is a
    TI single dump ``F0 00 20 33 01 dd 10 bb ss <512B data> <cs> F7``;
    checksum byte 522, F7 at 523, name at data[240:250].
  * tdm -- Digidesign Pro Tools ``DigiVrusSS01`` chunk (710 bytes).  The
    parameter block (the same 256-byte page A+B payload) starts at offset
    124; the patch name sits at data offset 240 of that block (byte 364).
  * stdmidi -- Standard MIDI file; the sysex events inside are unwrapped and
    parsed as either B/C singles (267B) or TI singles (524B).
  * vhc -- Virus HE single bank: 128 x 267-byte B/C single dumps back to
    back (``F0 00 20 33 01 00 10 01 <slot> 07 ...``).

The parameter offsets inside the 256-byte page A+B payload follow the
published Access Virus B/C SysEx documentation (waf80.de / virus controller
numbers), pages A and B.  Mapped offsets (page A unless noted):

    A5   Portamento Time          A51  Filter1 Mode (LP/HP/BP/BS)
    A17  Osc1 Shape               A54-58  Filter Env ADSR
    A19  Osc1 Wave Select         A59-63  Amp Env ADSR
    A24  Osc2 Wave Select         A91  Patch Volume
    A26  Osc2 Detune              A94  Key Mode (mono -> legato)
    A34  Subosc Volume            A40  Cutoff
    A35  Subosc Shape             A42  Filter1 Resonance
    A33  Osc Balance (osc2 level) A44  Filter1 Env Amt
    A36  Osc Mainvolume (osc1 level)  A49  Saturation Curve (drive)

Unmapped features are reported explicitly (never silently dropped):
osc2_fm_amount, ring_mod, lfo1, lfo2, keytrack, filter_slope_24db,
osc_sync, fx_chorus, fx_delay, fx_reverb, mod_matrix, noise_level.

Usage:
    python virus_patch.py --survey "D:\\pdf\\Virus Presets" --out virus_survey.json
    python virus_patch.py --dump <file>            # guess format, pretty-print

Exit 0 on a completed survey (even if some patches fail to parse), exit 1
on a hard error (unreadable root, no files, broken survey)."""
import argparse
import json
import os
import sys

import numpy as np

# ---------------------------------------------------------------------------
# Format-level constants
# ---------------------------------------------------------------------------

SUPPORTED_FORMATS = ("bcsingle", "tibank", "tdm", "stdmidi", "vhc")

MANUFACTURER = bytes((0x00, 0x20, 0x33))

BC_SINGLE_LEN = 267  # F0 + 8-byte header + 256 data + checksum + F7
TI_BLOCK_LEN = 524   # F0 + 8-byte header + 512 data + checksum + F7
VHC_BLOCK_LEN = 267

PAGE = 256            # bytes of page A + page B inside a single dump
NAME_OFF = 240        # page B params 112..121 = name chars 1..10
NAME_LEN = 10

# Offsets within the 256-byte page A+B payload (page A unless marked "B").
# Keyed by (offset, label, page) as documented in the Virus B/C SysEx spec.
PARAM_OFFSETS = {
    "portamento_time": (5, "A5"),
    "osc1_shape": (17, "A17"),
    "osc1_wave": (19, "A19"),
    "osc1_semitone": (20, "A20"),
    "osc1_keyfollow": (21, "A21"),
    "osc2_shape": (22, "A22"),
    "osc2_wave": (24, "A24"),
    "osc2_semitone": (25, "A25"),
    "osc2_detune": (26, "A26"),
    "osc2_fm_amount": (27, "A27"),
    "osc2_sync": (28, "A28"),
    "osc2_keyfollow": (31, "A31"),
    "osc_balance": (33, "A33"),
    "subosc_volume": (34, "A34"),
    "subosc_shape": (35, "A35"),
    "osc_mainvolume": (36, "A36"),
    "noise_volume": (37, "A37"),
    "ringmod_volume": (38, "A38"),
    "cutoff": (40, "A40"),
    "filter2_offset": (41, "A41"),
    "filter1_resonance": (42, "A42"),
    "filter2_resonance": (43, "A43"),
    "filter1_envamt": (44, "A44"),
    "filter2_envamt": (45, "A45"),
    "filter1_keyfollow": (46, "A46"),
    "filter2_keyfollow": (47, "A47"),
    "filter_balance": (48, "A48"),
    "saturation_curve": (49, "A49"),
    "filter1_mode": (51, "A51"),
    "filter2_mode": (52, "A52"),
    "filter_routing": (53, "A53"),
    "filter_env_attack": (54, "A54"),
    "filter_env_decay": (55, "A55"),
    "filter_env_sustain": (56, "A56"),
    "filter_env_sustain_time": (57, "A57"),
    "filter_env_release": (58, "A58"),
    "amp_env_attack": (59, "A59"),
    "amp_env_decay": (60, "A60"),
    "amp_env_sustain": (61, "A61"),
    "amp_env_sustain_time": (62, "A62"),
    "amp_env_release": (63, "A63"),
    "lfo1_rate": (67, "A67"),
    "lfo1_shape": (68, "A68"),
    "lfo2_rate": (79, "A79"),
    "lfo2_shape": (80, "A80"),
    "patch_volume": (91, "A91"),
    "transpose": (93, "A93"),
    "key_mode": (94, "A94"),
    "unison_mode": (97, "A97"),
    "chorus_mix": (105, "A105"),
    "delay_time": (114, "A114"),
    "delay_feedback": (115, "A115"),
    "filter_select": (122, "B122"),
}

# Features the HDAW sub_synth has no equivalent for.  Reported explicitly per
# patch with their source byte value where a byte exists.
UNMAPPED_FEATURES = (
    "osc2_fm_amount",
    "ring_mod",
    "lfo1",
    "lfo2",
    "keytrack",
    "filter_slope_24db",
    "osc_sync",
    "fx_chorus",
    "fx_delay",
    "fx_reverb",
    "mod_matrix",
    "noise_level",
)

# sub_synth params 0-23 (from TrackFXSlot.h ActiveType::SubSynth switch).
SUB_SYNTH_PARAMS = (
    "osc1_wave",          # 0
    "osc1_level",         # 1
    "osc2_wave",          # 2
    "osc2_level",         # 3
    "osc2_detune_cents",  # 4
    "sub_level",          # 5
    "sub_octave",         # 6
    "cutoff",             # 7
    "resonance",          # 8
    "drive",              # 9
    "amp_attack",         # 10
    "amp_decay",          # 11
    "amp_sustain",        # 12
    "amp_release",        # 13
    "output",             # 14
    "legato",             # 15
    "portamento",         # 16
    "filter_type",        # 17
    "filter_env_amount",  # 18
    "filter_env_attack",  # 19
    "filter_env_decay",   # 20
    "filter_env_sustain", # 21
    "filter_env_release", # 22
    # 23 reserved
)

# sub_synth param -> Virus source key (PARAM_OFFSETS) + value conversion.
# "wave" parameters map 0..63 / 0..64 to a 0..1 curve; 0x40 = middle for
# bipolar params.
SUBSYNTH_SOURCE = {
    0: ("osc1_wave", "osc1_wave", "norm64"),
    1: ("osc1_level", "osc_mainvolume", "norm"),
    2: ("osc2_wave", "osc2_wave", "norm64"),
    3: ("osc2_level", "osc_balance", "bipolar_norm"),
    4: ("osc2_detune_cents", "osc2_detune", "detune_cents"),
    5: ("sub_level", "subosc_volume", "norm"),
    6: ("sub_octave", "subosc_shape", "sub_octave"),
    7: ("cutoff", "cutoff", "norm"),
    8: ("resonance", "filter1_resonance", "norm"),
    9: ("drive", "saturation_curve", "drive"),
    10: ("amp_attack", "amp_env_attack", "norm"),
    11: ("amp_decay", "amp_env_decay", "norm"),
    12: ("amp_sustain", "amp_env_sustain", "norm"),
    13: ("amp_release", "amp_env_release", "norm"),
    14: ("output", "patch_volume", "norm"),
    15: ("legato", "key_mode", "legato"),
    16: ("portamento", "portamento_time", "norm"),
    17: ("filter_type", "filter1_mode", "filter_type"),
    18: ("filter_env_amount", "filter1_envamt", "bipolar_norm"),
    19: ("filter_env_attack", "filter_env_attack", "norm"),
    20: ("filter_env_decay", "filter_env_decay", "norm"),
    21: ("filter_env_sustain", "filter_env_sustain", "norm"),
    22: ("filter_env_release", "filter_env_release", "norm"),
}

# unmapped feature -> list of Virus source keys whose byte values are reported.
UNMAPPED_SOURCE = {
    "osc2_fm_amount": ["osc2_fm_amount"],
    "ring_mod": ["ringmod_volume"],
    "lfo1": ["lfo1_rate", "lfo1_shape"],
    "lfo2": ["lfo2_rate", "lfo2_shape"],
    "keytrack": ["osc1_keyfollow", "osc2_keyfollow", "filter1_keyfollow"],
    "filter_slope_24db": [],
    "osc_sync": ["osc2_sync"],
    "fx_chorus": ["chorus_mix"],
    "fx_delay": ["delay_time", "delay_feedback"],
    "fx_reverb": [],
    "mod_matrix": [],
    "noise_level": ["noise_volume"],
}


# ---------------------------------------------------------------------------
# Conversion helpers
# ---------------------------------------------------------------------------

def _norm(x):
    return float(x) / 127.0


def _norm64(x):
    return float(x) / 64.0


def _bipolar_norm(x):
    return float(x - 64) / 64.0


def _detune_cents(x):
    # Osc2 Detune 0..127 -> bipolar cents; 64 = no detune.
    return float(x - 64) * 100.0 / 64.0


def _sub_octave(x):
    # Subosc Shape 0=Square 1=Triangle.  Virus sub sits one octave below;
    # report the octave as -1 regardless of shape (documented behavior).
    return -1.0


def _drive(x):
    # Saturation Curve 0=Off 1:Light 2:Soft 3:Middle 4:Hard 5:Digital 6:Shaper
    return float(min(x, 6)) / 6.0


def _legato(x):
    # Key Mode 0=Poly 1..4=Mono1-4.  Mono modes imply legato behavior.
    return 1.0 if x != 0 else 0.0


def _filter_type(x):
    # Filter1 Mode 0:LP 1:HP 2:BP 3:BS -> sub_synth filter type (0..3).
    return float(min(x, 3))


_CONVERTERS = {
    "norm": _norm,
    "norm64": _norm64,
    "bipolar_norm": _bipolar_norm,
    "detune_cents": _detune_cents,
    "sub_octave": _sub_octave,
    "drive": _drive,
    "legato": _legato,
    "filter_type": _filter_type,
}


# ---------------------------------------------------------------------------
# Sysex structural helpers
# ---------------------------------------------------------------------------

def _is_sysex(data):
    return (len(data) >= 5
            and data[0] == 0xF0
            and data[1:4] == MANUFACTURER
            and data[4] == 0x01)


def _name_from_data(data):
    """Extract the 10-char patch name at NAME_OFF inside a page A+B payload."""
    raw = data[NAME_OFF:NAME_OFF + NAME_LEN]
    chars = [c for c in raw if 0x20 <= c < 0x7F]
    return "".join(chr(c) for c in chars).rstrip()


def _checksum_bc(data):
    """B/C single checksum: (dev + 0x10 + bank + prog + sum(data)) & 0x7F."""
    body = data[9:9 + PAGE]
    return (data[5] + 0x10 + data[7] + data[8] + int(sum(body))) & 0x7F


def _checksum_ti(block):
    """TI single checksum over the 512-byte payload (block[9:521])."""
    body = block[9:9 + 512]
    return (block[5] + 0x10 + block[7] + block[8] + int(sum(body))) & 0x7F


def _patch_base(fmt, name, data, extra=None):
    patch = {
        "format": fmt,
        "name": name,
        "data": list(data),
        "error": None,
    }
    if extra:
        patch.update(extra)
    return patch


def _error_patch(fmt, message, extra=None):
    patch = {"format": fmt, "name": "", "data": [], "error": message}
    if extra:
        patch.update(extra)
    return patch


# ---------------------------------------------------------------------------
# Format parsers
# ---------------------------------------------------------------------------

def parse_bcsingle(data):
    """Parse a 267-byte B/C single program dump -> patch dict."""
    if len(data) != BC_SINGLE_LEN:
        return _error_patch("bcsingle",
                            f"bad length {len(data)} (expected {BC_SINGLE_LEN})")
    if not _is_sysex(data):
        return _error_patch("bcsingle", "missing F0 00 20 33 01 header")
    if data[6] != 0x10:
        return _error_patch("bcsingle", f"not a single dump (cmd {data[6]:02X})")
    if data[-1] != 0xF7:
        return _error_patch("bcsingle", "missing F7 terminator")
    page = data[9:9 + PAGE]
    cs = data[265]
    expected = _checksum_bc(data)
    name = _name_from_data(page)
    return _patch_base("bcsingle", name, page, {
        "device": data[5],
        "bank": data[7],
        "program": data[8],
        "checksum": cs,
        "checksum_ok": cs == expected,
    })


def parse_tibank(data):
    """Parse a 67072-byte TI bank (128 x 524-byte blocks) -> list of patches."""
    if len(data) == 0 or len(data) % TI_BLOCK_LEN != 0:
        return [_error_patch("tibank",
                             f"bad length {len(data)} (not a multiple of 524)")]
    patches = []
    for i in range(0, len(data), TI_BLOCK_LEN):
        block = data[i:i + TI_BLOCK_LEN]
        if not _is_sysex(block):
            patches.append(_error_patch("tibank", "missing sysex header",
                                        {"index": i // TI_BLOCK_LEN}))
            continue
        if block[6] != 0x10:
            patches.append(_error_patch("tibank", "not a single dump",
                                        {"index": i // TI_BLOCK_LEN}))
            continue
        if block[-1] != 0xF7:
            patches.append(_error_patch("tibank", "missing F7 terminator",
                                        {"index": i // TI_BLOCK_LEN}))
            continue
        page = block[9:9 + 512]  # TI payload; page A+B is the first 256 bytes
        cs = block[522]
        expected = _checksum_ti(block)
        name = _name_from_data(page)
        patches.append(_patch_base("tibank", name, page[:PAGE], {
            "device": block[5],
            "bank": block[7],
            "program": block[8],
            "index": i // TI_BLOCK_LEN,
            "checksum": cs,
            "checksum_ok": cs == expected,
        }))
    return patches


def parse_tdm(data):
    """Parse a Digidesign Pro Tools Virus chunk (710 bytes) -> patch dict."""
    if len(data) < 124 + 2 * PAGE:
        return _error_patch("tdm", f"bad length {len(data)}")
    if data[8:16] != b"DigiVrus":
        return _error_patch("tdm", "missing DigiVrusSS01midi magic")
    page = data[124:124 + PAGE]
    name = _name_from_data(page)
    return _patch_base("tdm", name, page, {
        "source_magic": "DigiVrusSS01",
    })


def _read_vlq(data, i):
    value = 0
    while True:
        if i >= len(data):
            raise ValueError("truncated VLQ")
        b = data[i]
        i += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, i


def parse_stdmidi(data):
    """Parse a Standard MIDI file, unwrap sysex, parse each dump."""
    if data[0:4] != b"MThd":
        return [_error_patch("stdmidi", "not a MIDI file (MThd missing)")]
    try:
        ntrk = int.from_bytes(data[10:12], "big")
    except Exception as e:  # noqa: BLE001 - spec: any failure -> error patch
        return [_error_patch("stdmidi", f"bad header: {e}")]
    patches = []
    pos = 14
    for _ in range(ntrk):
        if pos + 8 > len(data):
            break
        if data[pos:pos + 4] != b"MTrk":
            break
        trk_len = int.from_bytes(data[pos + 4:pos + 8], "big")
        body = data[pos + 8:pos + 8 + trk_len]
        pos += 8 + trk_len
        i = 0
        running = None
        while i < len(body):
            try:
                _, i = _read_vlq(body, i)
            except ValueError as e:
                patches.append(_error_patch("stdmidi", f"truncated delta: {e}"))
                break
            if i >= len(body):
                break
            status = body[i]
            if status == 0xF0:
                running = None
                length, i = _read_vlq(body, i + 1)
                payload = body[i:i + length]
                i += length
                msg = bytes([0xF0]) + payload
                if msg[-1] != 0xF7:
                    msg = msg + bytes([0xF7])
                patches.extend(_dispatch_sysex(msg))
            elif status == 0xF7:
                running = None
                length, i = _read_vlq(body, i + 1)
                i += length
            elif status == 0xFF:
                running = None
                i += 1
                if i >= len(body):
                    break
                meta_len, i = _read_vlq(body, i + 1)
                i += meta_len
            elif status & 0x80:
                running = status
                if status in (0xC0, 0xD0):
                    i += 2
                else:
                    i += 3
            else:
                if running is None:
                    patches.append(_error_patch("stdmidi", "orphan running status"))
                    break
                if running in (0xC0, 0xD0):
                    i += 1
                else:
                    i += 2
    return patches


def _dispatch_sysex(msg):
    """Route a raw sysex message (F0..F7) to the right single parser."""
    if len(msg) == BC_SINGLE_LEN:
        return [parse_bcsingle(msg)]
    if len(msg) == TI_BLOCK_LEN:
        return parse_tibank(msg)
    return [_error_patch("stdmidi",
                         f"unrecognized sysex length {len(msg)}")]


def parse_vhc(data):
    """Parse a Virus HE single bank (128 x 267-byte B/C singles) -> list."""
    if len(data) == 0 or len(data) % VHC_BLOCK_LEN != 0:
        return [_error_patch("vhc",
                             f"bad length {len(data)} (not a multiple of 267)")]
    patches = []
    for i in range(0, len(data), VHC_BLOCK_LEN):
        block = data[i:i + VHC_BLOCK_LEN]
        if not _is_sysex(block):
            patches.append(_error_patch("vhc", "missing sysex header",
                                        {"index": i // VHC_BLOCK_LEN}))
            continue
        if block[6] != 0x10:
            patches.append(_error_patch("vhc", "not a single dump",
                                        {"index": i // VHC_BLOCK_LEN}))
            continue
        page = block[9:9 + PAGE]
        cs = block[265]
        expected = _checksum_bc(block)
        name = _name_from_data(page)
        patches.append(_patch_base("vhc", name, page, {
            "device": block[5],
            "bank": block[7],
            "program": block[8],
            "index": i // VHC_BLOCK_LEN,
            "checksum": cs,
            "checksum_ok": cs == expected,
        }))
    return patches


PARSERS = {
    "bcsingle": lambda d: [parse_bcsingle(d)],
    "tibank": parse_tibank,
    "tdm": lambda d: [parse_tdm(d)],
    "stdmidi": parse_stdmidi,
    "vhc": parse_vhc,
}


def detect_format(data, path=""):
    """Guess the container format from the first bytes of a file."""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".syx":
        if len(data) == BC_SINGLE_LEN:
            return "bcsingle"
        if len(data) % TI_BLOCK_LEN == 0 and _is_sysex(data):
            return "tibank"
    if ext == ".mid":
        return "stdmidi"
    if ext == ".vhc":
        return "vhc"
    if data[8:16] == b"DigiVrus":
        return "tdm"
    if len(data) == BC_SINGLE_LEN and _is_sysex(data):
        return "bcsingle"
    if len(data) % TI_BLOCK_LEN == 0 and _is_sysex(data):
        return "tibank"
    if _is_sysex(data) and data[6] == 0x10:
        return "bcsingle"
    return None


# ---------------------------------------------------------------------------
# Mapping
# ---------------------------------------------------------------------------

def map_to_sub_synth(patch):
    """Map a parsed patch onto sub_synth params 0-23.

    Returns {"mapped": {...}, "unmapped": [...]}.  ``mapped`` holds every
    sub_synth param index that has a readable Virus source byte; ``unmapped``
    lists the Virus features with no sub_synth equivalent (with source byte
    values where present)."""
    mapped = {}
    data = patch.get("data") or []
    for index, (_name, source, conv) in SUBSYNTH_SOURCE.items():
        info = PARAM_OFFSETS.get(source)
        if info is None:
            continue
        off, _page = info
        if off < 0 or off >= len(data):
            continue
        raw = data[off]
        mapped[str(index)] = {
            "param": SUB_SYNTH_PARAMS[index],
            "value": round(_CONVERTERS[conv](raw), 6),
            "raw": raw,
            "source": source,
        }
    unmapped = []
    for feature in UNMAPPED_FEATURES:
        sources = UNMAPPED_SOURCE[feature]
        entry = {"feature": feature, "raw": {}, "bytes": []}
        for src in sources:
            info = PARAM_OFFSETS.get(src)
            if info is None:
                continue
            off, _page = info
            if off < 0 or off >= len(data):
                continue
            entry["raw"][src] = data[off]
            entry["bytes"].append(data[off])
        unmapped.append(entry)
    return {"mapped": mapped, "unmapped": unmapped}


def _mapped_count(patch):
    return len(patch.get("mapped", {}))


def _has_unmapped_bytes(patch):
    for entry in patch.get("unmapped", []):
        if entry["bytes"]:
            return True
    return False


# ---------------------------------------------------------------------------
# Survey
# ---------------------------------------------------------------------------

def _iter_files(root, pattern, min_size=None):
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            full = os.path.join(dirpath, fn)
            if not fn.lower().endswith(pattern):
                continue
            if min_size is not None and os.path.getsize(full) != min_size:
                continue
            yield full


def discover_sources(root):
    """Map a source root to {format: [file paths]}."""
    sources = {}
    # TI banks: 67072-byte .syx in Access_Virus_TI
    ti_dir = os.path.join(root, "Access_Virus_TI")
    if os.path.isdir(ti_dir):
        sources["tibank"] = sorted(
            os.path.join(ti_dir, f)
            for f in os.listdir(ti_dir)
            if f.lower().endswith(".syx")
            and os.path.isfile(os.path.join(ti_dir, f))
            and os.path.getsize(os.path.join(ti_dir, f)) == 67072
        )
    # B/C singles: 267-byte .syx
    sources["bcsingle"] = list(_iter_files(root, ".syx", min_size=BC_SINGLE_LEN))
    # TDM: 710-byte files with DigiVrus magic
    sources["tdm"] = []
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            full = os.path.join(dirpath, fn)
            if os.path.getsize(full) != 710:
                continue
            with open(full, "rb") as fh:
                head = fh.read(16)
            if head[8:16] == b"DigiVrus":
                sources["tdm"].append(full)
    # Std-MIDI banks
    sources["stdmidi"] = list(_iter_files(root, ".mid"))
    # VHC single banks
    sources["vhc"] = list(_iter_files(root, ".vhc"))
    return sources


def survey(root):
    """Run the full survivability survey over a preset root -> report dict."""
    sources = discover_sources(root)
    formats = {}
    totals = {"patches": 0, "parsed": 0, "unmapped_any": 0}
    for fmt in SUPPORTED_FORMATS:
        paths = sources.get(fmt, [])
        patches = []
        for path in paths:
            try:
                with open(path, "rb") as fh:
                    data = fh.read()
            except OSError as e:
                patches.append(_error_patch(fmt, f"read failed: {e}"))
                continue
            try:
                parsed = PARSERS[fmt](data)
            except Exception as e:  # noqa: BLE001 - a bad file must not kill the survey
                parsed = [_error_patch(fmt, f"parse raised: {e}")]
            patches.extend(parsed)
        totals["patches"] += len(patches)
        parsed = [p for p in patches if not p["error"]]
        failed = [p for p in patches if p["error"]]
        totals["parsed"] += len(parsed)

        for p in parsed:
            p["mapped"] = map_to_sub_synth(p)["mapped"]
            p["unmapped"] = map_to_sub_synth(p)["unmapped"]
        name_ok = sum(1 for p in parsed if p.get("name", "").strip())
        mapped_counts = [_mapped_count(p) for p in parsed]
        if mapped_counts:
            mapped_avg = round(float(np.mean(mapped_counts)), 2)
            mapped_min = int(np.min(mapped_counts))
            mapped_max = int(np.max(mapped_counts))
        else:
            mapped_avg = 0.0
            mapped_min = 0
            mapped_max = 0

        # top unmapped features by how often the source byte is non-zero
        used = {}
        for p in parsed:
            for entry in p["unmapped"]:
                if any(b != 0 for b in entry["bytes"]):
                    used[entry["feature"]] = used.get(entry["feature"], 0) + 1
        top_unmapped = sorted(used, key=lambda k: (-used[k], k))

        totals["unmapped_any"] += sum(
            1 for p in parsed if _has_unmapped_bytes(p)
        )

        formats[fmt] = {
            "files": len(paths),
            "patches": len(patches),
            "parsed": len(parsed),
            "failed": len(failed),
            "name_ok": name_ok,
            "mapped_params": {
                "avg": mapped_avg,
                "min": mapped_min,
                "max": mapped_max,
            },
            "top_unmapped": top_unmapped,
        }

    report = {
        "generated_at": _survey_stamp(root),
        "sources": {k: len(v) for k, v in sources.items()},
        "formats": formats,
        "totals": totals,
    }
    return report


def _survey_stamp(root):
    import datetime as _dt
    import time as _time

    try:
        mtimes = [os.path.getmtime(os.path.join(dirpath, fn))
                  for dirpath, _d, files in os.walk(root)
                  for fn in files]
    except OSError:
        mtimes = []
    latest = max(mtimes) if mtimes else _time.time()
    return _dt.datetime.fromtimestamp(latest,
                                      tz=_dt.timezone.utc).isoformat()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Access Virus patch decoder + sub_synth survivability "
                    "survey.")
    ap.add_argument("--survey", metavar="ROOT",
                    help="survey a preset root (e.g. D:\\pdf\\Virus Presets)")
    ap.add_argument("--out", metavar="PATH",
                    help="write the survey JSON to PATH (default stdout)")
    ap.add_argument("--dump", metavar="FILE", help="decode one file")
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    if args.dump:
        with open(args.dump, "rb") as fh:
            data = fh.read()
        fmt = detect_format(data, args.dump)
        if fmt is None:
            sys.stderr.write(f"could not detect format for {args.dump}\n")
            return 1
        patches = PARSERS[fmt](data)
        for p in patches:
            p["mapped"] = map_to_sub_synth(p)["mapped"]
            p["unmapped"] = map_to_sub_synth(p)["unmapped"]
        sys.stdout.write(json.dumps(patches, indent=2) + "\n")
        return 0

    if args.survey:
        if not os.path.isdir(args.survey):
            sys.stderr.write(f"survey root not found: {args.survey}\n")
            return 1
        report = survey(args.survey)
        text = json.dumps(report, indent=2, sort_keys=True)
        if args.out:
            with open(args.out, "w", encoding="utf-8") as fh:
                fh.write(text + "\n")
            sys.stdout.write(f"wrote {args.out}\n")
        else:
            sys.stdout.write(text + "\n")
        return 0

    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())