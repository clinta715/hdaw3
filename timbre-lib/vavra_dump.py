#!/usr/bin/env python3
"""Waldorf microQ (Vavra) single-dump WRITER + injectable morph-chain emitter.

Two tools in one file (stdlib only):

1. Dump writer (``build_dump`` / CLI ``--base`` mode).  Byte layout is the
   verified microQ single-program dump (see
   ``timbre-lib/matrix_presets/vavra-offset-map.md``)::

     392 bytes total; header F0 3E 10 00 10 (bank@5, prog@6);
     one 7-bit param value per byte at 7..369 (dump[7] == 1 is Version);
     16-char latin-1 name, space-padded, at 370..385;
     4-char category at 386..389; checksum@390; F7@391.

   The parent's checksum byte (390) is PRESERVED: the emulation
   (wLib/wState.h) validates size only, and no checksum formula reproduced
   all 528 source files (249/528 match sum(d[4:390]) & 0x7F), so
   synthesizing one would be guesswork.  Caveat: a real device may still
   re-check it (unverified).

2. Morph mode (``--sheet``): extends the morph_presets conventions
   (schema ``hdaw.matrix.preset.morph.v1``) with the vavra engine profile.
   Carrier classification = morph_presets.is_continuous(key, 'vavra') for
   named keys (continuous: mix/amount/mod/level/balance/depth/rate/fade/
   time/cutoff; discrete type/source/destination/select/mode/switch win)
   plus the value-dependent raw rule: off_<N> keys interpolate only when
   both parents share the key and |a-b| <= 24 (unknown params are still
   just values within a small hop); larger hops anchor to A and are
   recorded in ``jumps``.

   For every emitted step the document ALSO carries ``sysex``: the 392
   ints of parent A's ORIGINAL .syx file with the step's param values
   written at their byte offsets (i.e. an immediately injectable
   single-program dump), and ``baseSyx``: the source file's basename.
   The parent syx is located via the preset's ``examples`` patch name:
   exact <name>.syx match first, then <name> <category>.syx prefix
   matches (files append the 4-char category); every candidate is
   byte-verified against the preset's params and ambiguous duplicate
   files are resolved deterministically (sorted first) and recorded in
   the pair's ``baseResolution``.

   Pair selection (--auto-pairs): all undirected pairs with d > 0 and a
   byte-resolvable parent A whose differing params are mostly NAMED
   (device-name ratio >= 0.5 -- FX1/FX2/mod-matrix names are the
   musically meaningful diffs), ranked by the shared distance convention
   (mean |a-b|/127), top N.

Deterministic and byte-stable: sorted JSON, basenames only, no
timestamps, no absolute paths.  Run::

    python3 timbre-lib/vavra_dump.py \
        --base IN.syx --set 7=1,20=64 --name 'New Name' --category 'Bas' \
        --dump-out OUT.syx

    python3 timbre-lib/vavra_dump.py \
        --sheet timbre-lib/matrix_presets/vavra.json \
        --syx-root /path/to/rhythm-lab.com_waldorf_micro_q \
        --auto-pairs 5 --steps 4 \
        --out timbre-lib/matrix_presets/vavra_morphs.json
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import morph_presets as mp

DUMP_LEN = 392
IDX_F0 = 0
IDX_END = 391
IDX_PARAM_FIRST = 7
IDX_PARAM_LAST = 369
IDX_NAME = 370
NAME_LEN = 16
IDX_CATEGORY = 386
CATEGORY_LEN = 4
IDX_CHECKSUM = 390          # preserved from the parent, never recomputed

OFF_KEY_PREFIX = "off_"
OFF_INTERP_MAX_DELTA = 24   # raw off_<N> keys interpolate only within this
AUTO_PAIRS_DEFAULT = 5
MIN_NAMED_RATIO = 0.5       # "mostly named" differing params


class DumpError(ValueError):
    """Invalid dump input (length, framing, offsets, values, encoding)."""


# ---------------------------------------------------------------------------
# Dump writer
# ---------------------------------------------------------------------------

def load_dump(base_syx_path):
    """Read + validate a 392-byte single dump (length, F0 first, F7 last)."""
    with open(base_syx_path, "rb") as fh:
        dump = fh.read()
    if len(dump) != DUMP_LEN:
        raise DumpError("%s: expected %d bytes, got %d"
                        % (base_syx_path, DUMP_LEN, len(dump)))
    if dump[IDX_F0] != 0xF0:
        raise DumpError("%s: missing leading F0" % base_syx_path)
    if dump[IDX_END] != 0xF7:
        raise DumpError("%s: missing trailing F7" % base_syx_path)
    return dump


def _encode_fixed(text, width, what):
    """latin-1, space-padded / truncated to exactly ``width" bytes."""
    try:
        raw = str(text).encode("latin-1")
    except UnicodeEncodeError as exc:
        raise DumpError("%s %r is not latin-1 encodable" % (what, text)) \
            from None
    return raw[:width].ljust(width, b" ")


def build_dump(base_syx_path, overrides, name=None, category=None):
    """392-byte microQ single dump: parent bytes + overrides (+ name/category).

    ``overrides`` maps param byte offsets (7..369) to 7-bit values (0..127,
    ints).  ``name`` (16 bytes, space-padded/truncated latin-1) lands at
    370, ``category`` (4 bytes) at 386.  The parent's checksum byte (390)
    is kept -- see module docstring.  Returns a new bytes object; the
    parent file is never mutated.
    """
    dump = bytearray(load_dump(base_syx_path))
    for off, val in sorted(overrides.items()):
        off = int(off)
        if not IDX_PARAM_FIRST <= off <= IDX_PARAM_LAST:
            raise DumpError("override offset %d outside param range %d..%d"
                            % (off, IDX_PARAM_FIRST, IDX_PARAM_LAST))
        if isinstance(val, bool) or not isinstance(val, int):
            raise DumpError("override at offset %d is not an int: %r"
                            % (off, val))
        if not 0 <= val <= 127:
            raise DumpError("override value %d at offset %d outside 0..127"
                            % (val, off))
        dump[off] = val
    if name is not None:
        dump[IDX_NAME:IDX_NAME + NAME_LEN] = \
            _encode_fixed(name, NAME_LEN, "name")
    if category is not None:
        dump[IDX_CATEGORY:IDX_CATEGORY + CATEGORY_LEN] = \
            _encode_fixed(category, CATEGORY_LEN, "category")
    return bytes(dump)


def parse_set_spec(spec):
    """'7=1,20=64' -> {7: 1, 20: 64}; raises DumpError on junk."""
    overrides = {}
    for chunk in str(spec).split(","):
        chunk = chunk.strip()
        if not chunk:
            raise DumpError("empty --set entry")
        head, sep, tail = chunk.partition("=")
        if not sep:
            raise DumpError("bad --set entry %r: expected OFFSET=VALUE"
                            % chunk)
        try:
            off, val = int(head.strip()), int(tail.strip())
        except ValueError:
            raise DumpError("bad --set entry %r: integers required"
                            % chunk) from None
        if off in overrides:
            raise DumpError("duplicate --set offset %d" % off)
        overrides[off] = val
    if not overrides:
        raise DumpError("no --set entries")
    return overrides


# ---------------------------------------------------------------------------
# vavra engine profile (morph generation)
# ---------------------------------------------------------------------------

def load_offset_map(path):
    """vavra_offset_map.json (byte-offset string -> primary device name)."""
    with open(path, encoding="utf-8") as fh:
        doc = json.load(fh)
    if not isinstance(doc, dict):
        raise DumpError("offset map must be a JSON object")
    return {str(val): int(key) for key, val in doc.items()}


def byte_of_key(key, name_to_off):
    """Sheet param key -> dump byte offset (off_<N> raw, named via map)."""
    if key.startswith(OFF_KEY_PREFIX):
        return int(key[len(OFF_KEY_PREFIX):])
    try:
        return name_to_off[key]
    except KeyError:
        raise DumpError("param key %r is neither %s<N> nor in the offset map"
                        % (key, OFF_KEY_PREFIX)) from None


def params_to_bytes(params, name_to_off):
    """{param key: value} -> {byte offset: value}."""
    return {byte_of_key(key, name_to_off): val
            for key, val in params.items()}


def vavra_key_is_continuous(key, va, vb):
    """vavra carrier classification (value-dependent for raw off_<N> keys).

    Named keys: morph_presets.is_continuous(key, 'vavra').  off_<N> keys:
    interpolate only when both parents share the key and differ by <=
    OFF_INTERP_MAX_DELTA; otherwise anchor to A (and jump).
    """
    if key.startswith(OFF_KEY_PREFIX):
        if va is None or vb is None:
            return False
        return abs(va - vb) <= OFF_INTERP_MAX_DELTA
    return mp.is_continuous(key, "vavra")


def make_continuous_fn(params_a, params_b):
    """classification closure for one A->B pair (morph_presets hook)."""
    return lambda key: vavra_key_is_continuous(key, params_a.get(key),
                                               params_b.get(key))


def _dump_matches_params(syx_path, params_by_byte):
    try:
        dump = load_dump(syx_path)
    except (OSError, DumpError):
        return False
    return all(dump[off] == val for off, val in params_by_byte.items())


def _syx_candidates(syx_root, patch_name):
    """*.syx under syx_root whose stem equals the 16-char patch name, or
    starts with name + ' ' (files append the 4-char category); exact
    matches first, each group sorted for determinism."""
    exact, prefix = [], []
    for dirpath, _dirnames, filenames in os.walk(syx_root):
        for fname in filenames:
            if not fname.endswith(".syx"):
                continue
            stem = fname[:-4]
            if stem == patch_name:
                exact.append(os.path.join(dirpath, fname))
            elif stem.startswith(patch_name + " "):
                prefix.append(os.path.join(dirpath, fname))
    return sorted(exact) + sorted(prefix)


def resolve_parent_syx(syx_root, patch_name, params_by_byte):
    """Locate + byte-verify the ORIGINAL .syx of a harvested patch.

    Candidates (by patch name) are byte-verified against the preset's
    params; a byte-matching candidate wins over a merely name-matching
    one, ties resolve to the sorted-first file.  Returns (path, info)
    with info = {'how', 'file', 'nameMatches', 'byteMatches'} -- basename
    only, safe for the output document.  Raises FileNotFoundError when no
    candidate exists at all, DumpError when none byte-matches.
    """
    candidates = _syx_candidates(syx_root, patch_name)
    if not candidates:
        raise FileNotFoundError("no .syx for patch %r under %s"
                                % (patch_name, syx_root))
    matching = [path for path in candidates
                if _dump_matches_params(path, params_by_byte)]
    if not matching:
        raise DumpError("no .syx for patch %r byte-matches its %d params"
                        % (patch_name, len(params_by_byte)))
    chosen = matching[0]
    stem = os.path.basename(chosen)[:-4]
    info = {
        "how": "exact" if stem == patch_name else "prefix",
        "file": os.path.basename(chosen),
        "nameMatches": len(candidates),
        "byteMatches": len(matching),
    }
    return chosen, info


def select_pairs(presets, resolvable, count=AUTO_PAIRS_DEFAULT,
                 min_named_ratio=MIN_NAMED_RATIO):
    """Deterministic auto-selection of morph pairs.

    Undirected pairs (a < b) with d > 0, resolvable parent A, and a
    named-diff ratio >= min_named_ratio, ranked by mean |a-b|/127 then
    indices; returns the top ``count`` as [(a, b), ...].
    """
    rows = []
    for a in range(len(presets)):
        if a not in resolvable:
            continue
        pa = presets[a]["params"]
        for b in range(a + 1, len(presets)):
            if b not in resolvable:
                continue
            pb = presets[b]["params"]
            keys = sorted(set(pa) | set(pb))
            diff = [k for k in keys if pa.get(k) != pb.get(k)]
            if not diff:
                continue
            named_ratio = sum(1 for k in diff
                              if not k.startswith(OFF_KEY_PREFIX)) / len(diff)
            if named_ratio < min_named_ratio:
                continue
            rows.append((round(mp.mean_distance(pa, pb), 6), a, b))
    rows.sort()
    return [(a, b) for _dist, a, b in rows[:count]]


def _step_overrides(step_params, base, name_to_off):
    """{byte offset: int value} for every param whose rounded step value
    differs from the parent dump; interpolated floats are rounded to the
    nearest int (Python banker's rounding, deterministic)."""
    overrides = {}
    for key in sorted(step_params):
        val = step_params[key]
        if isinstance(val, bool) or not isinstance(val, int):
            val = int(round(val))
        off = byte_of_key(key, name_to_off)
        if val != base[off]:
            overrides[off] = val
    return overrides


def build_vavra_morphs(sheet, source, syx_root, pairs, steps, name_to_off,
                       syx_root_label=None):
    """hdaw.matrix.preset.morph.v1 + per-step injectable SysEx.

    ``pairs`` is a list of (a, b) preset-index tuples; parent A's original
    .syx is the base dump for every step of the chain.  Raises when a
    parent A cannot be located/byte-verified (fail loudly rather than
    emit a dump that does not reproduce the preset).
    """
    if sheet.get("schema") != mp.SHEET_SCHEMA:
        raise ValueError("bad sheet schema %r (expected %r)"
                         % (sheet.get("schema"), mp.SHEET_SCHEMA))
    if not isinstance(sheet.get("presets"), list):
        raise ValueError("sheet carries no 'presets' list")
    if steps < 1:
        raise ValueError("--steps must be >= 1")
    presets = sheet["presets"]
    out_pairs = []
    for a, b in pairs:
        pa, pb = presets[a], presets[b]
        syx_path, info = resolve_parent_syx(
            syx_root, pa["examples"][0],
            params_to_bytes(pa["params"], name_to_off))
        base = load_dump(syx_path)
        pair = mp.build_pair(presets, a, b, steps, engine="vavra",
                             continuous_fn=make_continuous_fn(pa["params"],
                                                              pb["params"]))
        for entry in pair["steps"]:
            overrides = _step_overrides(entry["preset"]["params"], base,
                                        name_to_off)
            entry["sysex"] = list(build_dump(syx_path, overrides))
            entry["baseSyx"] = info["file"]
        pair["namedDiffRatio"] = _named_diff_ratio(pa["params"], pb["params"])
        pair["baseResolution"] = info
        out_pairs.append(pair)
    return {
        "schema": mp.MORPH_SCHEMA,
        "sourceSheet": source,      # echoed verbatim: pass a relative path
        "sysexRoots": [syx_root_label or os.path.basename(
            os.path.normpath(syx_root))],
        "pairs": out_pairs,
        "unverified": True,
    }


def _named_diff_ratio(params_a, params_b):
    keys = sorted(set(params_a) | set(params_b))
    diff = [k for k in keys if params_a.get(k) != params_b.get(k)]
    if not diff:
        return 0.0
    return round(sum(1 for k in diff
                     if not k.startswith(OFF_KEY_PREFIX)) / len(diff), 6)


def build_auto_pairs(sheet, syx_root, name_to_off, count=AUTO_PAIRS_DEFAULT):
    """Resolve parent-A syx for every preset, then select the best pairs.

    Returns (pairs, resolved) where resolved maps preset index -> syx path
    for every byte-verified preset (presets without a syx -- e.g. a
    harvested config whose source file is gone -- are excluded silently
    from selection; explicit --pairs still fail loudly on them).
    """
    presets = sheet["presets"]
    resolved = {}
    for idx, preset in enumerate(presets):
        try:
            path, _info = resolve_parent_syx(
                syx_root, preset["examples"][0],
                params_to_bytes(preset["params"], name_to_off))
        except (FileNotFoundError, DumpError):
            continue
        resolved[idx] = path
    return select_pairs(presets, set(resolved), count=count), resolved


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="microQ/Vavra single-dump writer + morph-chain emitter.")
    # dump-writer mode
    parser.add_argument("--base", default=None,
                        help="parent 392-byte .syx (dump-writer mode)")
    parser.add_argument("--set", dest="set_spec", default=None,
                        help="comma list like '7=1,20=64' (param byte="
                             "value overrides)")
    parser.add_argument("--name", default=None,
                        help="16-char patch name (space-padded, byte 370)")
    parser.add_argument("--category", default=None,
                        help="4-char category (space-padded, byte 386)")
    parser.add_argument("--dump-out", default=None,
                        help="output .syx path (dump-writer mode)")
    # morph mode
    parser.add_argument("--sheet", default=None,
                        help="hdaw.matrix.preset.v1 sheet (morph mode; pass "
                             "a relative path, echoed into sourceSheet)")
    parser.add_argument("--syx-root", default=None,
                        help="directory holding the original .syx files")
    parser.add_argument("--offset-map", default=None,
                        help="vavra_offset_map.json (default: next to the "
                             "sheet)")
    parser.add_argument("--pairs", default=None,
                        help="comma list like '25:32,8:36' (morph mode)")
    parser.add_argument("--auto-pairs", type=int, default=0,
                        help="select the N best pairs automatically "
                             "(default 5 when neither --pairs nor this)")
    parser.add_argument("--steps", type=int, default=4,
                        help="intermediates per pair (default 4)")
    parser.add_argument("--out", default=None, help="output JSON path")
    args = parser.parse_args(argv)

    try:
        if args.sheet:
            return _run_morph(args)
        if args.base:
            return _run_dump_writer(args)
        parser.error("nothing to do: pass --sheet (morph mode) or --base "
                     "(dump-writer mode)")
    except (ValueError, IndexError, KeyError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    return 0


def _run_dump_writer(args):
    if not args.dump_out:
        raise ValueError("dump-writer mode requires --dump-out")
    overrides = parse_set_spec(args.set_spec) if args.set_spec else {}
    if not overrides and args.name is None and args.category is None:
        raise ValueError("nothing to write: pass --set and/or --name and/or "
                         "--category")
    dump = build_dump(args.base, overrides, name=args.name,
                      category=args.category)
    parent = os.path.dirname(os.path.abspath(args.dump_out))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(args.dump_out, "wb") as fh:
        fh.write(dump)
    print("wrote %s (%d bytes, %d param overrides)"
          % (args.dump_out, len(dump), len(overrides)))
    return 0


def _run_morph(args):
    if not args.out:
        raise ValueError("morph mode requires --out")
    if not args.syx_root:
        raise ValueError("morph mode requires --syx-root (directory holding "
                         "the original .syx files)")
    if bool(args.pairs) == bool(args.auto_pairs):
        raise ValueError("pass exactly one of --pairs / --auto-pairs")
    with open(args.sheet, encoding="utf-8") as fh:
        sheet = json.load(fh)
    offset_map = args.offset_map or os.path.join(
        os.path.dirname(args.sheet) or ".", "vavra_offset_map.json")
    name_to_off = load_offset_map(offset_map)
    steps = args.steps
    if args.pairs:
        pairs = mp.parse_pairs(args.pairs)
        resolved = None
    else:
        pairs, resolved = build_auto_pairs(
            sheet, args.syx_root, name_to_off,
            count=args.auto_pairs or AUTO_PAIRS_DEFAULT)
        if not pairs:
            raise ValueError("auto-selection found no eligible pair (need "
                             "d > 0, byte-resolvable parent A, named-diff "
                             "ratio >= %.2f)" % MIN_NAMED_RATIO)
    morph = build_vavra_morphs(sheet, args.sheet, args.syx_root, pairs,
                               steps, name_to_off)
    parent = os.path.dirname(os.path.abspath(args.out))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(mp.render_json(morph))
    total = 0
    for pair in morph["pairs"]:
        total += len(pair["steps"])
        print("pair %-9s d=%.6f named=%.2f base=%s steps=%d"
              % (pair["pair"], pair["distance"], pair["namedDiffRatio"],
                 pair["baseResolution"]["file"], len(pair["steps"])))
    print("wrote %s (%d pairs, %d intermediate steps, %d sysex dumps)"
          % (args.out, len(morph["pairs"]), total, total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
