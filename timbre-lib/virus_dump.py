#!/usr/bin/env python3
"""Access Virus single-dump patch WRITER + morph emitter.

Inverse of ``virus_fx_pages.py`` (the byte-match-verified fx/matrix page
decoder -- UNCHANGED, used as reference) and ``virus_patch.py`` (framing):
writes Virus B/C and TI single-program SysEx dumps by name-addressed
overrides over a decoded base patch, and emits deterministic morph-chain
steps with EMBEDDED sysex over the harvested matrix sheet
``matrix_presets/virus.json``.

Framing (virus_patch.py, verified 1.7M values byte-matched):

  B/C:  F0 00 20 33 01 <dev> 10 <bank> <prog> | 256B payload | <cs> F7   (267B)
  TI :  F0 00 20 33 01 <dev> 10 <bank> <prog> | 512B payload | <cs> F7   (524B)
  cs = (dev + 0x10 + bank + prog + sum(payload)) & 0x7F
     stored at byte 265 (B/C) / 522 (TI) -- the dump json's
     {"type": "checksum", "first": 5, "last": 521} rule.

Param addressing: the model's parameterDescriptions vocabulary maps
(page, index) -> name; payload offset = (page - 112) * 128 + index.  The
inverse table is built LAST-slot-wins, mirroring ``fx_params``' decode
convention (later (page, index) overwrite earlier ones), so a value read
back through the decoder always agrees with the slot the writer addressed.
The current TI (600 slots) and B/C (305 slots) tables carry no duplicate
primary names.

Byte fidelity: overrides touch ONLY the named slots' payload bytes plus the
recomputed checksum byte; every other byte of the base dump (header, name,
unaddressed slots, F7) is preserved verbatim.  Overriding 0 params yields
bytes identical to the base bank's original dump bytes.

Bases come from FRAMED containers only -- bcsingle/tibank/stdmidi/vhc
(each dump carries its own F0..F7 framing + checksum).  TDM chunks carry no
sysex framing and are not resolvable bases (documented limitation).

Morph emission reuses morph_presets.py's distance/interp/jumps/id
conventions verbatim (imported, never modified) with virus' continuous
classification (``virus_fx_pages.is_continuous_virus``).  Each step carries
the FULL 267/524-byte dump inline as 'sysex': [ints] plus 'model'
('TI'|'BC') and 'basePatch' (basename of the corpus file the base dump was
resolved from), so chains are injectable via send_fx_midi
{kind:'sysEx', bytes:[...]} on an Osirus (Virus C) or OsTIrus (TI) slot.
Base resolution byte-verifies a preset's full param signature against the
corpus decode; when a preset matches dumps of BOTH generations the B/C dump
is preferred (Osirus is installed locally, OsTIrus is not), so B/C steps
are live-verifiable while TI steps are format-verified only.

appliesVia = "sysex_writer_verified_format": the dump FORMAT is proven
(framing + checksum + decode round-trip); live audibility on the hosted
plugin is still pending -- hence document-level ``unverified: true``.

Deterministic and byte-stable: sorted JSON, no timestamps, bank-relative
paths only (basenames; the corpus root is echoed as a basename).

Usage:
    python3 virus_dump.py --dump <bank.syx|.mid|.vhc> [--index N]
    python3 virus_dump.py --write BASE --out OUT --set "Chorus Mix"=90 \
        --set "Assign1 Amount"=64 [--index N]
    python3 virus_dump.py --emit-morphs \
        --sheet timbre-lib/matrix_presets/virus.json \
        --pairs '16:26,9:20,22:39,12:34,0:30' --steps 4 \
        --out-json timbre-lib/matrix_presets/virus_morphs.json \
        --corpus-root "/mnt/d/pdf/Virus Presets"
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Dict, List, Optional, Sequence, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import morph_presets as MP           # canonical morph conventions
import virus_fx_pages as vfp         # decoder: the reference implementation
import virus_patch as VP             # framing/format detection

MORPH_SCHEMA = MP.MORPH_SCHEMA       # hdaw.matrix.preset.morph.v1
APPLIES_VIA = "sysex_writer_verified_format"
DEFAULT_CORPUS_ROOT = "/mnt/d/pdf/Virus Presets"
DEFAULT_SHEET = os.path.join("timbre-lib", "matrix_presets", "virus.json")
DEFAULT_OUT_JSON = os.path.join("timbre-lib", "matrix_presets",
                                "virus_morphs.json")
DELIVERABLE_PAIRS = "16:26,9:20,22:39,12:34,0:30"
DELIVERABLE_STEPS = 4

# model -> (payload bytes, full dump bytes, checksum offset, pad bytes).
# TI dumps carry ONE pad byte between payload end and checksum
# (F0 + 8B header + 512B payload + pad(521) + cs(522) + F7(523)); real
# corpus dumps carry 0x00 there and it is preserved verbatim by the writer.
FRAMING = {
    "TI": (vfp.PAYLOAD_TI, VP.TI_BLOCK_LEN, vfp.PAYLOAD_TI + 10, 1),
    "B/C": (vfp.PAYLOAD_BC, VP.BC_SINGLE_LEN, vfp.PAYLOAD_BC + 9, 0),
}
# morph document model tag (task vocabulary) per internal generation tag
MODEL_TAG = {"TI": "TI", "B/C": "BC"}


def model_framing(model: str) -> Tuple[int, int, int, int]:
    """(payload_len, dump_len, checksum_offset, pad_bytes) per generation."""
    try:
        return FRAMING[model]
    except KeyError:
        raise ValueError("unknown model %r (valid: %s)"
                         % (model, ", ".join(sorted(FRAMING)))) from None


def dump_model(dump: bytes) -> str:
    """'TI' | 'B/C' for a strictly-framed single dump; raise otherwise."""
    if len(dump) == VP.TI_BLOCK_LEN:
        model, plen = "TI", vfp.PAYLOAD_TI
    elif len(dump) == VP.BC_SINGLE_LEN:
        model, plen = "B/C", vfp.PAYLOAD_BC
    else:
        raise ValueError("bad dump length %d (expected %d B/C or %d TI)"
                         % (len(dump), VP.BC_SINGLE_LEN, VP.TI_BLOCK_LEN))
    if dump[0] != 0xF0 or dump[1:4] != VP.MANUFACTURER or dump[4] != 0x01:
        raise ValueError("missing F0 00 20 33 01 header")
    if dump[6] != 0x10:
        raise ValueError("not a single dump (cmd byte %02X)" % dump[6])
    if dump[-1] != 0xF7:
        raise ValueError("missing F7 terminator")
    _plen, dump_len, cs_off, _pad = model_framing(model)
    if len(dump) != dump_len or len(dump) != cs_off + 2:
        raise ValueError("framing length inconsistent for %s" % model)
    return model


def checksum(dump: bytes) -> int:
    """(dev + 0x10 + bank + prog + sum(payload)) & 0x7F."""
    plen = model_framing(dump_model(dump))[0]
    return (dump[5] + 0x10 + dump[7] + dump[8]
            + sum(dump[9:9 + plen])) & 0x7F


def _vocab(vocab: Optional[vfp.Vocabulary]) -> vfp.Vocabulary:
    if vocab is None:
        vocab = vfp.Vocabulary()
    if not vocab._tables:
        raise ValueError("gearmulator parameterDescriptions not available "
                         "(cannot resolve parameter names)")
    return vocab


def name_slots(model: str, vocab: vfp.Vocabulary) -> Dict[str, Tuple[int, int]]:
    """Inverse vocabulary: name -> (page, index), LAST slot wins (the slot
    ``fx_params``' decode reports)."""
    inverse: Dict[str, Tuple[int, int]] = {}
    for (page, index), name in sorted(vocab.table(model).items()):
        inverse[name] = (page, index)
    return inverse


def payload_offset(slot: Tuple[int, int]) -> int:
    """Byte offset of a (page, index) slot inside the payload."""
    page, index = slot
    return (page - vfp.BASE_PAGE) * vfp.PAGE_LEN + index


def decode_dump(dump: bytes, vocab: vfp.Vocabulary) -> Dict[str, int]:
    """Named value of EVERY vocabulary slot in a dump (last-wins names).

    The full table (not just the FX subset) so any named parameter is
    addressable; ``load_patch`` reports the same view."""
    model = dump_model(dump)
    plen = model_framing(model)[0]
    payload = dump[9:9 + plen]
    values, _holes = vfp.decode_payload(payload, vocab.table(model))
    named: Dict[str, int] = {}
    for (_page, _index), value in sorted(values.items()):
        named[vocab.table(model)[_page, _index]] = value
    return named


# ---------------------------------------------------------------------------
# Container loading
# ---------------------------------------------------------------------------

def load_dumps(path: str, vocab: Optional[vfp.Vocabulary] = None) -> List[dict]:
    """Every framed Virus single dump in a bank file.

    Supports bcsingle / tibank / stdmidi (.mid) / vhc via the decoder's own
    container walker + raw-dump recovery.  TDM chunks carry no framing and
    are excluded (they cannot be re-emitted as standalone sysex)."""
    vocab = _vocab(vocab)
    with open(path, "rb") as fh:
        data = fh.read()
    fmt = VP.detect_format(data, path)
    if fmt is None:
        raise ValueError("unrecognized Virus container: %s" % path)
    if fmt == "tdm":
        raise ValueError("tdm chunks carry no sysex framing and cannot be "
                         "written (use a bcsingle/tibank/stdmidi/vhc base)")
    out = []
    for d in vfp.iter_dumps(data, fmt):
        raw = vfp._dump_bytes(data, fmt, d)
        try:
            model = dump_model(raw)
        except ValueError:
            continue                     # not a strictly-framed single dump
        patch = VP.PARSERS[fmt](data)
        info = patch[d["index"]] if d["index"] < len(patch) else {}
        out.append({
            "dump": raw,
            "model": model,
            "index": d["index"],
            "name": d["name"],
            "bank": info.get("bank"),
            "program": info.get("program"),
            "checksumOk": vfp.verify_checksum(raw, model_framing(model)[0]),
            "params": decode_dump(raw, vocab),
        })
    if not out:
        raise ValueError("no framed Virus single dump found in %s" % path)
    return out


def load_patch(path: str, index: int = 0,
               vocab: Optional[vfp.Vocabulary] = None) -> Dict[str, int]:
    """Named param values of dump ``index`` of a bank file (default 0)."""
    dumps = load_dumps(path, vocab)
    if not 0 <= index < len(dumps):
        raise IndexError("dump index %d out of range (%s has %d dumps)"
                         % (index, path, len(dumps)))
    return dumps[index]["params"]


# ---------------------------------------------------------------------------
# Writer
# ---------------------------------------------------------------------------

def build_dump(base_dump: bytes, overrides: Dict[str, int],
               vocab: Optional[vfp.Vocabulary] = None) -> bytes:
    """Base dump with name-addressed overrides; recomputed checksum.

    Byte-identical to ``base_dump`` outside the overridden payload offsets
    and the checksum byte.  Overriding 0 params returns bytes equal to the
    input."""
    vocab = _vocab(vocab)
    model = dump_model(base_dump)            # strict framing validation
    plen = model_framing(model)[0]
    inverse = name_slots(model, vocab)
    payload = bytearray(base_dump[9:9 + plen])
    for name, value in sorted(overrides.items()):
        if not isinstance(value, int) or isinstance(value, bool):
            raise ValueError("param %r value must be int, got %r"
                             % (name, value))
        if not 0 <= value <= 127:
            raise ValueError("param %r value %d out of writable range 0..127"
                             % (name, value))
        slot = inverse.get(name)
        if slot is None:
            raise ValueError("unknown %s param name %r (valid: %d names)"
                             % (model, name, len(inverse)))
        payload[payload_offset(slot)] = value
    cs = (base_dump[5] + 0x10 + base_dump[7] + base_dump[8]
          + sum(payload)) & 0x7F
    _plen, _dump_len, cs_off, _pad = model_framing(model)
    tail = base_dump[9 + plen:cs_off]      # TI pad byte(s), verbatim
    return base_dump[:9] + bytes(payload) + tail + bytes((cs, 0xF7))


def write_patch(base_path: str, overrides: Dict[str, int], out_path: str,
                index: int = 0,
                vocab: Optional[vfp.Vocabulary] = None) -> dict:
    """Write dump ``index`` of ``base_path`` with overrides to ``out_path``
    as a standalone single-dump .syx.  Returns an info dict."""
    vocab = _vocab(vocab)
    dumps = load_dumps(base_path, vocab)
    if not 0 <= index < len(dumps):
        raise IndexError("dump index %d out of range (%s has %d dumps)"
                         % (index, base_path, len(dumps)))
    entry = dumps[index]
    out = build_dump(entry["dump"], overrides, vocab)
    parent = os.path.dirname(os.path.abspath(out_path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(out_path, "wb") as fh:
        fh.write(out)
    inverse = name_slots(entry["model"], vocab)
    return {
        "out": out_path,
        "base": base_path,
        "model": MODEL_TAG[entry["model"]],
        "dumpIndex": index,
        "dumpSize": len(out),
        "patchName": entry["name"],
        "checksum": out[model_framing(entry["model"])[2]],
        "applied": dict(sorted(overrides.items())),
        "offsets": {name: payload_offset(inverse[name]) + 9
                    for name in sorted(overrides)},
    }


# ---------------------------------------------------------------------------
# Corpus indexing + base resolution
# ---------------------------------------------------------------------------

def index_corpus(corpus_root: str,
                 vocab: Optional[vfp.Vocabulary] = None) -> dict:
    """Deterministic corpus index: fx-param signature -> candidate bases.

    Walks ``corpus_root`` (sorted), parses every FRAMED container with the
    decoder's walker, keeps only dumps whose checksum verifies AND whose
    full-table decode byte-match rebuilds the payload exactly.  Each hit:
    {model, file (corpus-relative posix), index, dump, name}."""
    vocab = _vocab(vocab)
    sig_index: Dict[tuple, List[dict]] = {}
    stats = {"files": 0, "dumps": 0, "kept": 0, "badChecksum": 0,
             "byteMismatch": 0}
    for dirpath, dirs, files in os.walk(corpus_root):
        dirs.sort()
        for fn in sorted(files):
            if fn.endswith(".virus.json"):
                continue
            full = os.path.join(dirpath, fn)
            try:
                with open(full, "rb") as fh:
                    data = fh.read()
            except OSError:
                continue
            fmt = VP.detect_format(data, full)
            if fmt not in ("bcsingle", "tibank", "stdmidi", "vhc"):
                continue
            stats["files"] += 1
            rel = os.path.relpath(full, corpus_root).replace(os.sep, "/")
            try:
                dumps = vfp.iter_dumps(data, fmt)
            except Exception:            # noqa: BLE001 - a bad file must not
                continue                 # kill the index
            for d in dumps:
                model = vfp.payload_model(len(d["payload"]))
                if model is None:
                    continue
                stats["dumps"] += 1
                raw = vfp._dump_bytes(data, fmt, d)
                try:
                    dump_model(raw)
                except ValueError:
                    continue
                if vfp.verify_checksum(raw, len(d["payload"])) is not True:
                    stats["badChecksum"] += 1
                    continue
                table = vocab.table(model)
                values, _holes = vfp.decode_payload(d["payload"], table)
                if vfp.byte_mismatches(d["payload"], values) != 0:
                    stats["byteMismatch"] += 1
                    continue
                sig = tuple(sorted(vfp.fx_params(values, table).items()))
                sig_index.setdefault(sig, []).append({
                    "model": model,
                    "file": rel,
                    "index": d["index"],
                    "dump": raw,
                    "name": d["name"],
                })
                stats["kept"] += 1
    for hits in sig_index.values():
        hits.sort(key=lambda h: (h["file"], h["index"]))
    return {"root": corpus_root, "vocab": vocab,
            "index": sig_index, "stats": stats}


def resolve_base(preset: dict, corpus: dict,
                 model: Optional[str] = None) -> Optional[dict]:
    """First corpus dump (sorted file, index) whose fx-param decode equals
    ``preset['params']`` exactly; ``model`` filters the generation."""
    sig = tuple(sorted((k, v) for k, v in preset["params"].items()))
    for hit in corpus["index"].get(sig, []):
        if model is None or hit["model"] == model:
            return hit
    return None


def resolve_base_preferred(preset: dict, corpus: dict) -> Tuple[dict, str]:
    """(hit, note): B/C-preferred resolution for morph emission.

    A preset matching dumps of BOTH generations resolves to its first B/C
    hit (Osirus-installed local verification); otherwise the first hit of
    the only matching generation.  Raises when no dump byte-verifies."""
    hit = resolve_base(preset, corpus, "B/C")
    if hit is not None:
        return hit, "BC-preferred"
    hit = resolve_base(preset, corpus, "TI")
    if hit is not None:
        return hit, "TI-only"
    raise ValueError("preset %s (%s): no corpus dump byte-verifies its "
                     "params" % (preset.get("id"), preset.get("name")))


# ---------------------------------------------------------------------------
# Morph emission
# ---------------------------------------------------------------------------

def emit_morphs(sheet_path: str, pairs, steps: int, out_json: str,
                corpus_root: str = DEFAULT_CORPUS_ROOT,
                corpus: Optional[dict] = None) -> dict:
    """Deterministic morph document with per-step embedded sysex dumps.

    Each step carries 'sysex' (the full 267/524-byte dump as an int list),
    'model' ('TI'|'BC') and 'basePatch' (corpus file basename); the preset
    keys keep morph_presets' conventions (interp/jumps/parents/id) with
    appliesVia upgraded to ``sysex_writer_verified_format``.  Values of
    ``None`` (key present only on the other parent's generation) write
    nothing -- the base dump's byte stays."""
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    if sheet.get("schema") != MP.SHEET_SCHEMA:
        raise ValueError("bad sheet schema %r" % sheet.get("schema"))
    presets = sheet["presets"]
    if corpus is None:
        corpus = index_corpus(corpus_root, None)
    vocab = corpus["vocab"]

    bases: Dict[int, tuple] = {}
    for idx in sorted({i for pair in pairs for i in pair}):
        if not 0 <= idx < len(presets):
            raise IndexError("preset index %d out of range" % idx)
        bases[idx] = resolve_base_preferred(presets[idx], corpus)

    chains = []
    for a, b in pairs:
        chain = MP.build_pair(presets, a, b, steps, engine="virus",
                              continuous_fn=vfp.is_continuous_virus)
        base_hit, note = bases[a]
        model = base_hit["model"]
        inverse = name_slots(model, vocab)
        base_params = decode_dump(base_hit["dump"], vocab)
        new_steps = []
        for entry in chain["steps"]:
            preset = entry["preset"]
            syx_ints = {k: int(round(v)) for k, v in preset["params"].items()
                        if v is not None}
            unknown = set(syx_ints) - set(inverse)
            if unknown:
                raise ValueError("params not in %s vocabulary: %s"
                                 % (model, sorted(unknown)))
            overrides = {k: v for k, v in sorted(syx_ints.items())
                         if base_params[k] != v}
            dump = build_dump(base_hit["dump"], overrides, vocab)
            preset["appliesVia"] = APPLIES_VIA
            preset["evidence"] = (
                "synthetic: linear interpolation of continuous params "
                "between %s and %s; discrete keys anchored to %s; full "
                "single dump written from byte-verified base %s (%s, "
                "dump %d, %s)"
                % (preset["parents"][0], preset["parents"][1],
                   preset["parents"][0], os.path.basename(base_hit["file"]),
                   model, base_hit["index"], note))
            new_steps.append({
                "step": entry["step"],
                "preset": preset,
                "sysex": list(dump),
                "model": MODEL_TAG[model],
                "basePatch": os.path.basename(base_hit["file"]),
            })
        chains.append({
            "pair": chain["pair"],
            "parents": chain["parents"],
            "distance": chain["distance"],
            "steps": new_steps,
        })
    doc = {
        "schema": MORPH_SCHEMA,
        "engine": "virus",
        "sourceSheet": sheet_path,       # echoed verbatim: relative arg
        "corpusRootBasename": os.path.basename(corpus_root),
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
# CLI
# ---------------------------------------------------------------------------

def _parse_set(items):
    overrides = {}
    for item in items or []:
        name, sep, value = item.partition("=")
        if not sep:
            raise ValueError("--set expects NAME=VALUE, got %r" % item)
        overrides[name.strip()] = int(value.strip(), 0)
    return overrides


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description="Access Virus patch writer + morph emitter (inverse of "
                    "virus_fx_pages.py).")
    ap.add_argument("--dump", metavar="FILE", help="decode one bank file "
                    "to JSON (first dump, or --index)")
    ap.add_argument("--index", type=int, default=0,
                    help="dump index within the container (default 0)")
    ap.add_argument("--write", metavar="BASE",
                    help="write dump --index of BASE with --set overrides")
    ap.add_argument("--set", dest="sets", action="append", metavar="N=V",
                    help="override NAME=VALUE (repeatable)")
    ap.add_argument("--out", metavar="PATH", help="output path for --write")
    ap.add_argument("--emit-morphs", action="store_true",
                    help="emit morph chains with embedded sysex")
    ap.add_argument("--sheet", metavar="PATH", default=DEFAULT_SHEET)
    ap.add_argument("--pairs", metavar="A:B,...", default=DELIVERABLE_PAIRS)
    ap.add_argument("--steps", type=int, default=DELIVERABLE_STEPS)
    ap.add_argument("--out-json", metavar="PATH", default=DEFAULT_OUT_JSON)
    ap.add_argument("--corpus-root", metavar="DIR",
                    default=DEFAULT_CORPUS_ROOT)
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    try:
        if args.emit_morphs:
            pairs = MP.parse_pairs(args.pairs)
            doc = emit_morphs(args.sheet, pairs, args.steps, args.out_json,
                              corpus_root=args.corpus_root)
            for chain in doc["pairs"]:
                print("pair %-8s d=%.4f steps=%d model=%s basePatch=%s"
                      % (chain["pair"], chain["distance"],
                         len(chain["steps"]), chain["steps"][0]["model"],
                         chain["steps"][0]["basePatch"]))
            print("wrote %s (%d pairs, %d steps)"
                  % (args.out_json, len(doc["pairs"]),
                     sum(len(c["steps"]) for c in doc["pairs"])))
            return 0
        if args.write:
            if not args.out:
                raise ValueError("--write requires --out")
            info = write_patch(args.write, _parse_set(args.sets), args.out,
                               index=args.index)
            print(json.dumps(info, sort_keys=True, indent=2))
            return 0
        if args.dump:
            dumps = load_dumps(args.dump)
            if not 0 <= args.index < len(dumps):
                raise IndexError("dump index %d out of range (%s has %d "
                                 "dumps)" % (args.index, args.dump,
                                             len(dumps)))
            first = dumps[args.index]
            print(json.dumps({
                "file": os.path.basename(args.dump),
                "model": MODEL_TAG[first["model"]],
                "dumpSize": len(first["dump"]),
                "dumpsInFile": len(dumps),
                "patchName": first["name"],
                "bank": first["bank"],
                "program": first["program"],
                "checksumOk": first["checksumOk"],
                "params": first["params"],
            }, sort_keys=True, indent=2))
            return 0
    except (ValueError, IndexError, KeyError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    ap.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
