#!/usr/bin/env python3
"""Nord Lead 2x (NodalRed2x) patch WRITER + param-offset map + morph emitter.

Inverse of ``nl2x_patch.py`` (the decoder -- unchanged, used as reference):
writes Clavia NL2x single-dump .syx patch files by name-addressed overrides
and emits deterministic morph-chain .syx steps over the harvested matrix
sheet ``matrix_presets/nodalred2x.json``.

Layout (verified; full citation chain in matrix_presets/nord-offset-map.md):

  F0 33 <dev> 04 <msgType> <msgSpec> | 66 params x 2 nibbles | [10B name] F7
  param i (FULL-dump offset) = 6 + 2*i   (n2xstate.cpp getOffsetInSingleDump)
  param i (unpacked payload) = 5 + 2*i   (nl2x_patch PAYLOAD_HEADER_SIZE)
  value = (data[off] & 0xF) | (data[off+1] << 4)     -> 8 bits (0..255)

Byte 52 is PACKED (n2xController.cpp combineSyncRingModDistortion):
  bit 0 = Sync, bit 1 = RingMod, bit 4 = Distortion (write via read-modify-
  write names 'sync' / 'ringmod' / 'distortion'; raw name 'sync_distortion'
  writes the whole byte).  No checksum exists anywhere in the container.

The morph emitter reuses morph_presets.py's distance/interp/jumps/id
conventions verbatim (imported, never modified); per step it also writes a
real 139-byte .syx under the output dir, so chains are performable via
load_nord_bank.  Deterministic and byte-stable: sorted JSON, no timestamps,
bank-relative paths only.

Usage:
    python3 timbre-lib/nord_dump.py --dump <patch.syx>
    python3 timbre-lib/nord_dump.py --write BASE.syx --out OUT.syx \
        --set fm_depth=90 --set lfo1_rate=64
    python3 timbre-lib/nord_dump.py --emit-morphs \
        --sheet timbre-lib/matrix_presets/nodalred2x.json \
        --pairs '29:32,16:23,34:35,1:22,34:39' --steps 4 \
        --out-json timbre-lib/matrix_presets/nord_morphs.json \
        --out-dir  timbre-lib/matrix_presets/nord_morphs \
        --banks-root "/mnt/d/pdf/NL2x Banks"
"""
from __future__ import annotations

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import nl2x_patch as NP            # decoder: the reference implementation
import morph_presets as MP         # canonical morph conventions

MORPH_SCHEMA = MP.MORPH_SCHEMA     # hdaw.matrix.preset.morph.v1
APPLIES_VIA = "load_nord_bank"     # verified loader (docs/plans/2026-09-12)
DEFAULT_BANKS_ROOT = "/mnt/d/pdf/NL2x Banks"

# Sheet (matrix/mod) keys in harvest order -> SingleParam storage index.
SHEET_KEYS = [
    "filter_env_amount", "filter_env_attack", "filter_env_decay",
    "filter_env_release", "filter_env_sustain", "fm_depth", "lfo1_dest",
    "lfo1_level", "lfo1_rate", "lfo1_waveform", "lfo2_dest", "lfo2_rate",
    "mod_env_attack", "mod_env_decay", "mod_env_dest", "mod_env_level",
    "sync_distortion",
]
INDEX_BY_NAME = {name: idx for idx, name in NP.PARAM_NAMES.items()}
SHEET_KEY_INDEX = {k: INDEX_BY_NAME[k] for k in SHEET_KEYS}

# Storage indices 25..49 are unnamed in the SingleParam enum (n2xmiditypes.h)
# but named by parameterDescriptions_n2x.json AND the g_singleDefault comment
# table (n2xstate.cpp): the per-destination mod-matrix Sens amounts
# (min -128, toText signed256).  Snake-cased from the descriptions JSON.
SENS_NAMES = {
    25: "o2_pitch_sens", 26: "o2_pitch_fine_sens", 27: "mix_sens",
    28: "cutoff_sens", 29: "resonance_sens", 30: "filter_env_amount_sens",
    31: "pw_sens", 32: "fm_depth_sens", 33: "filter_env_a_sens",
    34: "filter_env_d_sens", 35: "filter_env_s_sens", 36: "filter_env_r_sens",
    37: "amp_env_a_sens", 38: "amp_env_d_sens", 39: "amp_env_s_sens",
    40: "amp_env_r_sens", 41: "portamento_sens", 42: "gain_sens",
    43: "mod_env_a_sens", 44: "mod_env_d_sens", 45: "mod_env_level_sens",
    46: "lfo1_rate_sens", 47: "lfo1_level_sens", 48: "lfo2_rate_sens",
    49: "arp_range_sens",
}

# Byte 52 bit layout (n2xController.cpp combineSyncRingModDistortion):
SYNC_INDEX = 52
SYNC_BITS = {"sync": 0, "ringmod": 1, "distortion": 4}

NAME_TO_INDEX = dict(INDEX_BY_NAME)           # 0..24, 50..65
NAME_TO_INDEX.update({name: i for i, name in SENS_NAMES.items()})  # 25..49

# Morph classification for the nodalred2x vocabulary (continuous tokens
# mirror morph_presets' je8086/vavra token style; discrete suffixes WIN).
NORD_CONTINUOUS_TOKENS = ("amount", "level", "rate", "depth", "attack",
                          "decay", "sustain", "release")
NORD_DISCRETE_SUFFIXES = ("waveform", "dest", "type", "select", "switch",
                          "mode")


def is_continuous_nord(key):
    """Continuous = Amount/Level/Rate/Depth (+ env A/D/S/R times).

    Discrete: *waveform/*dest/*type/... suffixes and the PACKED byte 52
    ('sync_distortion' carries three bitfields -- never interpolate).
    """
    low = key.lower()
    if low == "sync_distortion":
        return False
    if low.endswith(NORD_DISCRETE_SUFFIXES):
        return False
    return any(tok in low for tok in NORD_CONTINUOUS_TOKENS)


# ---------------------------------------------------------------------------
# Strict container scanning (independent of the decoder's scan: full framing
# validation; used by the writer AND the corpus-mapping test fixture).
# ---------------------------------------------------------------------------

def scan_syx_spans(data):
    """(start, end) inclusive byte spans of strictly-framed F0..F7 dumps."""
    spans = []
    i, n = 0, len(data)
    while i < n:
        if (data[i] == 0xF0 and i + 4 <= n and data[i + 1] == NP.ID_CLAVIA
                and data[i + 3] == NP.ID_N2X):
            end = data.find(b"\xf7", i)
            if end < 0:
                break
            spans.append((i, end))
            i = end + 1
        else:
            i += 1
    return spans


def load_dumps(path):
    """Every NL2x dump in a file: dict with full dump bytes + decoded params.

    .syx (raw, possibly concatenated F0..F7 runs) and .mid/.rmi (SMF sysex
    events, payload without leading F0) both supported; payloads are
    re-framed to full dumps (F0 included) so offsets are uniform.
    """
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] in (b"MThd", b"RIFF"):
        items = [(p, False) for p in NP.split_smf_sysex(data)]
    else:
        items = NP.split_dumps(data)
    out = []
    for payload, has_f0 in items:
        kind = NP.classify(payload)
        if kind is None:
            continue
        kindname, with_name, msg_type, msg_spec = kind
        full = b"\xf0" + payload + b"\xf7"
        out.append({
            "dump": full,
            "kind": kindname,
            "withName": with_name,
            "msgType": msg_type,
            "msgSpec": msg_spec,
            "params": NP.decode_params(payload),
        })
    if not out:
        raise ValueError("no NL2x dump found in %s" % path)
    return out


def load_patch(path):
    """The 66 unpacked params of the FIRST dump in ``path``."""
    return load_dumps(path)[0]["params"]


# ---------------------------------------------------------------------------
# Param addressing
# ---------------------------------------------------------------------------

def dump_offset(index):
    """Byte offset of param ``index`` within a full dump (F0 included)."""
    if not 0 <= index < 66:
        raise ValueError("param index %d out of range 0..65" % index)
    return NP.SYSEX_HEADER_SIZE + 2 * index        # 6 + 2i


def payload_offset(index):
    """Byte offset of param ``index`` in the unpacked payload (no F0)."""
    return NP.PAYLOAD_HEADER_SIZE + 2 * index      # 5 + 2i


def get_param(dump, index):
    off = dump_offset(index)
    return (dump[off] & 0xF) | (dump[off + 1] << 4)


def set_param(dump, index, value):
    """Nibble write: preserves the high nibble of both container bytes."""
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError("param value must be int, got %r" % (value,))
    if not 0 <= value <= 127:
        raise ValueError("param value %d out of writable range 0..127 "
                         "(declared/CC range; container would carry 0..255)"
                         % value)
    off = dump_offset(index)
    dump[off] = (dump[off] & 0xF0) | (value & 0xF)
    dump[off + 1] = (dump[off + 1] & 0xF0) | ((value >> 4) & 0xF)


# ---------------------------------------------------------------------------
# Writer
# ---------------------------------------------------------------------------

def _resolve_overrides(overrides):
    """{name: value} -> ({index: value}, [(bit, value), ...]).

    Bit names (sync/ringmod/distortion) are NOT resolved here: they need
    the dump's CURRENT byte 52 -- write_patch applies them read-modify-write.
    """
    plan = {}
    rmw = []
    for name, value in overrides.items():
        if name in SYNC_BITS:
            rmw.append((SYNC_BITS[name], value))
            continue
        index = NAME_TO_INDEX.get(name)
        if index is None:
            raise ValueError("unknown param name %r (valid: %s)"
                             % (name, ", ".join(sorted(NAME_TO_INDEX))))
        plan[index] = value
    for bit, value in rmw:
        if not isinstance(value, int) or isinstance(value, bool) \
                or not 0 <= value <= 1:
            raise ValueError("bit param value must be 0/1, got %r" % (value,))
    return plan, rmw


def write_patch(base_path, overrides, out_path):
    """Write ``base`` with name-addressed overrides applied to dump 0.

    Byte-identical to the base outside the overridden param offsets (all
    other dumps, gaps, name suffixes and F7 preserved verbatim).  Multi
    dumps take the overrides in their FIRST single group (offsets 6..137),
    matching the decoder's patch-0 view.  Returns an info dict.
    """
    with open(base_path, "rb") as fh:
        data = fh.read()
    if data[:4] in (b"MThd", b"RIFF"):
        raise ValueError("writer bases must be raw .syx (got SMF/RIFF); "
                         "re-encoding .mid is out of scope")
    spans = scan_syx_spans(data)
    if not spans:
        raise ValueError("no NL2x dump found in %s" % base_path)
    start, end = spans[0]
    dump = bytearray(data[start:end + 1])
    first = NP.classify(bytes(dump[1:-1]))
    if first is None:
        raise ValueError("first dump of %s has unsupported shape %d bytes"
                         % (base_path, len(dump)))
    plan, bit_ops = _resolve_overrides(overrides)
    for index in sorted(plan):
        set_param(dump, index, plan[index])
    if bit_ops:
        current = get_param(dump, SYNC_INDEX)   # read-modify-write
        for bit, value in bit_ops:
            current = (current & ~(1 << bit)) | ((value & 1) << bit)
        set_param(dump, SYNC_INDEX, current)
    out = data[:start] + bytes(dump) + data[end + 1:]
    with open(out_path, "wb") as fh:
        fh.write(out)
    return {
        "out": out_path,
        "base": base_path,
        "kind": first[0],
        "msgType": first[2],
        "msgSpec": first[3],
        "applied": {name: overrides[name] for name in overrides},
        "offsets": {name: dump_offset(NAME_TO_INDEX.get(name, SYNC_INDEX))
                    for name in overrides},
    }


# ---------------------------------------------------------------------------
# Morph emission
# ---------------------------------------------------------------------------

def index_banks(banks_root):
    """stem -> sorted absolute .syx paths (deterministic)."""
    by_stem = {}
    for dirpath, dirs, files in os.walk(banks_root):
        dirs.sort()
        for fn in sorted(files):
            if fn.lower().endswith(".syx"):
                stem = os.path.splitext(fn)[0]
                by_stem.setdefault(stem.casefold(), []).append(
                    os.path.join(dirpath, fn))
    for stem in by_stem:
        by_stem[stem].sort()
    return by_stem


def resolve_base(preset, by_stem):
    """First example that exists, parses AND byte-matches the preset params.

    Returns (absolute path, params list) or None.
    """
    for ex in preset.get("examples", []):
        for path in by_stem.get(ex.casefold(), []):
            try:
                params = load_patch(path)
            except (OSError, ValueError):
                continue
            named = {NP._param_name(i): v for i, v in enumerate(params)}
            if all(named.get(k) == preset["params"][k]
                   for k in preset["params"]):
                return path, params
    return None


def emit_morphs(sheet_path, pairs, steps, out_json, morph_dir,
                banks_root=DEFAULT_BANKS_ROOT):
    """Deterministic hdaw.matrix.preset.morph.v1 + performable .syx steps.

    Distance/interp/jumps/ids come from morph_presets (imported verbatim);
    each step additionally carries 'file' (basename) + 'syxParams' (the
    integer values actually written) and appliesVia load_nord_bank.
    """
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    if sheet.get("schema") != MP.SHEET_SCHEMA:
        raise ValueError("bad sheet schema %r" % sheet.get("schema"))
    presets = sheet["presets"]
    by_stem = index_banks(banks_root)

    bases = {}
    for idx in sorted({i for pair in pairs for i in pair}):
        if not 0 <= idx < len(presets):
            raise IndexError("preset index %d out of range" % idx)
        got = resolve_base(presets[idx], by_stem)
        if got is None:
            raise ValueError("preset %d (%s): no example .syx byte-verifies "
                             "its params" % (idx, presets[idx]["name"]))
        bases[idx] = got

    chains = []
    for a, b in pairs:
        chain = MP.build_pair(presets, a, b, steps, engine="nodalred2x",
                              continuous_fn=is_continuous_nord)
        base_abs, base_params = bases[a]
        base_rel = os.path.relpath(base_abs, banks_root).replace(os.sep, "/")
        pair_dir = os.path.join(morph_dir, "%d-%d" % (a, b))
        os.makedirs(pair_dir, exist_ok=True)
        new_steps = []
        for entry in chain["steps"]:
            preset = entry["preset"]
            syx = {k: int(round(v)) for k, v in preset["params"].items()}
            overrides = {k: v for k, v in syx.items()
                         if base_params[NAME_TO_INDEX[k]] != v}
            fname = "step%d.syx" % entry["step"]
            write_patch(base_abs, overrides, os.path.join(pair_dir, fname))
            preset["appliesVia"] = APPLIES_VIA
            preset["file"] = fname
            preset["syxParams"] = {k: syx[k] for k in sorted(syx)}
            preset["evidence"] = (
                "synthetic: linear interpolation of continuous params "
                "between %s and %s; discrete keys anchored to %s; .syx "
                "written from byte-verified base %s"
                % (preset["parents"][0], preset["parents"][1],
                   preset["parents"][0], os.path.basename(base_abs)))
            new_steps.append({
                "step": entry["step"],
                "preset": preset,
                "baseFile": base_rel,
            })
        chains.append({
            "pair": chain["pair"],
            "parents": chain["parents"],
            "distance": chain["distance"],
            "base": {"preset": a,
                     "name": presets[a]["name"],
                     "id": presets[a]["id"],
                     "file": base_rel},
            "steps": new_steps,
        })
    doc = {
        "schema": MORPH_SCHEMA,
        "engine": "nodalred2x",
        "sourceSheet": sheet_path,
        "banksRootBasename": os.path.basename(banks_root),
        "pairs": chains,
        "unverified": True,
    }
    parent = os.path.dirname(os.path.abspath(out_json))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(out_json, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(MP.render_json(doc))
    return doc


# ---------------------------------------------------------------------------
# Offset map document
# ---------------------------------------------------------------------------

def build_offset_map():
    """nord_offset_map.json content (name -> entry map + _meta).

    'offset' is the UNPACKED-payload byte offset (5 + 2i, nl2x_patch's
    decode space); 'dumpOffset' the full-syx offset (6 + 2i,
    getOffsetInSingleDump).  Extras: all 66 storage params.
    """
    params = {}
    for index in range(66):
        name = NP.PARAM_NAMES.get(index) or SENS_NAMES[index]
        entry = {
            "index": index,
            "offset": payload_offset(index),
            "dumpOffset": dump_offset(index),
            "access": "bits" if index == SYNC_INDEX else "byte",
            "sheet": name in SHEET_KEY_INDEX,
            "continuous": is_continuous_nord(name)
                          if index != SYNC_INDEX else False,
        }
        if index == SYNC_INDEX:
            entry["bits"] = {"sync": 0, "ringmod": 1, "distortion": 4}
            entry["bitNote"] = "packed; bits 2-3,5-7 unallocated " \
                               "(junk observed in real files)"
        if index in SENS_NAMES:
            entry["name"] = name
            entry["source"] = "parameterDescriptions_n2x.json (min -128, " \
                              "signed256; unnamed in SingleParam enum)"
        params[name] = entry
    return {
        "_meta": {
            "schema": "hdaw.nord.offset.map.v1",
            "engine": "nodalred2x",
            "rule": "value = (dump[6+2i] & 0xF) | (dump[7+2i] << 4); "
                    "offset = 5+2i in the unpacked payload (no F0), "
                    "dumpOffset = 6+2i in the full sysex",
            "doc": "timbre-lib/matrix_presets/nord-offset-map.md",
            "sheetKeys": SHEET_KEYS,
        },
        "params": params,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _parse_set(items):
    overrides = {}
    for item in items or []:
        name, sep, value = item.partition("=")
        if not sep:
            raise ValueError("--set expects NAME=VALUE, got %r" % item)
        name = name.strip()
        if name not in NAME_TO_INDEX and name not in SYNC_BITS:
            raise ValueError("unknown param name %r" % name)
        overrides[name] = int(value.strip(), 0)
    return overrides


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Nord Lead 2x patch writer + morph emitter (inverse of "
                    "nl2x_patch.py).")
    ap.add_argument("--dump", metavar="FILE", help="decode one file to JSON")
    ap.add_argument("--write", metavar="BASE", help="write BASE with --set "
                    "overrides to --out")
    ap.add_argument("--set", dest="sets", action="append", metavar="N=V",
                    help="override NAME=VALUE (repeatable; sync/ringmod/"
                         "distortion are bit RMW on byte 52)")
    ap.add_argument("--out", metavar="PATH", help="output path for --write")
    ap.add_argument("--emit-morphs", action="store_true",
                    help="emit morph chains (see module docstring)")
    ap.add_argument("--sheet", metavar="PATH", default=os.path.join(
        "timbre-lib", "matrix_presets", "nodalred2x.json"))
    ap.add_argument("--pairs", metavar="A:B,...", default="")
    ap.add_argument("--steps", type=int, default=4)
    ap.add_argument("--out-json", metavar="PATH",
                    default=os.path.join("timbre-lib", "matrix_presets",
                                         "nord_morphs.json"))
    ap.add_argument("--out-dir", metavar="DIR",
                    default=os.path.join("timbre-lib", "matrix_presets",
                                         "nord_morphs"))
    ap.add_argument("--banks-root", metavar="DIR", default=DEFAULT_BANKS_ROOT)
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    try:
        if args.emit_morphs:
            pairs = MP.parse_pairs(args.pairs)
            doc = emit_morphs(args.sheet, pairs, args.steps, args.out_json,
                              args.out_dir, banks_root=args.banks_root)
            for chain in doc["pairs"]:
                print("pair %-9s d=%.4f steps=%d base=%s"
                      % (chain["pair"], chain["distance"],
                         len(chain["steps"]), chain["base"]["file"]))
            print("wrote %s (%d pairs)" % (args.out_json, len(doc["pairs"])))
            return 0
        if args.write:
            if not args.out:
                raise ValueError("--write requires --out")
            info = write_patch(args.write, _parse_set(args.sets), args.out)
            print(json.dumps(info, sort_keys=True, indent=2))
            return 0
        if args.dump:
            dumps = load_dumps(args.dump)
            first = dumps[0]
            named = {NP._param_name(i): v
                     for i, v in enumerate(first["params"])}
            print(json.dumps({
                "file": os.path.basename(args.dump),
                "kind": first["kind"],
                "msgType": first["msgType"],
                "msgSpec": first["msgSpec"],
                "dumpSize": len(first["dump"]),
                "dumpsInFile": len(dumps),
                "params": named,
            }, sort_keys=True, indent=2))
            return 0
    except (ValueError, IndexError, KeyError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    ap.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
