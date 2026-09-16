#!/usr/bin/env python3
"""Harvest FX / modulation-matrix recipes out of the patch-library sidecars.

The device FX and matrix settings live inside the patches, so the sidecar libraries
are a large corpus of *effect and modulation recipes*. This tool mines them and emits
a compact preset sheet that HDAW can re-create with its own internal FX and movement
planes (the modulation-first policy: the no-param devices cannot be driven directly,
but their recipes can be reproduced).

Per engine it reports, for every FX/modulation parameter found:
  * the value distribution (top values + counts),
  * how many patches carry it,
  * example patches that use the most common value (so a human can audition them).

Usage::

    py -3 timbre-lib/harvest_fx_presets.py --out timbre-lib/harvested_fx_presets.json \
        "D:/pdf/je8086" "D:/pdf/rhythm-lab.com_waldorf_micro_q" "D:/pdf/microwave"

No third-party dependencies.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys
from typing import Dict, List, Optional, Sequence, Tuple

SUFFIX_ENGINE = {
    ".je8086.json": "je8086",
    ".vavra.json": "vavra",
    ".xenia.json": "xenia",
    ".nl2x.json": "nodalred2x",
    ".virus.json": "virus",
}

# FX + modulation vocabulary. Deliberately broad but explicit: a parameter is harvested
# when its NAME matches one of these groups.
GROUPS: List[Tuple[str, str]] = [
    ("fx-type", r"(multieffects?type|fx\d?type|effecttype|chorustype|delaytype)"),
    ("fx-mix", r"(fx\d?mix|effects?level|multieffectlevel|mix$|deptheffect)"),
    ("delay", r"(delay(time|feedback|level)|delaysend)"),
    ("chorus", r"(chorus|flanger)"),
    ("reverb", r"(reverb)"),
    ("phaser", r"(phaser)"),
    ("distortion", r"(distortion|overdrive|saturat|drive)"),
    ("vocoder", r"(vocoder)"),
    ("filter", r"(cutoff|resonance|filtertype|cutoffslope|filtermode)"),
    ("mod-source", r"(modsource|modsrc|lfo\d?destination|lfodest|moddestination|assign\d+ (source|destination)|lfo\d?mode)"),
    ("mod-amount", r"(modamount|moddepth|lfo\d?depth|lfo\d?level|lfodepth|envelopedepth|envmod|envamount|fmdepth|velmod|envvelamount)"),
    ("pan", r"(pan\b|autopan|panmod)"),
]


def _group_for(param: str) -> Optional[str]:
    low = param.lower()
    for group, pattern in GROUPS:
        if re.search(pattern, low):
            return group
    return None


def _sidecar_files(roots: Sequence[str]) -> List[Tuple[str, str]]:
    """(engine, path) for every recognisable sidecar under the roots."""
    out = []
    for root in roots:
        for dirpath, _dirnames, filenames in os.walk(root):
            for fn in sorted(filenames):
                for suffix, engine in SUFFIX_ENGINE.items():
                    if fn.endswith(suffix):
                        out.append((engine, os.path.join(dirpath, fn)))
                        break
    return out


def _params_of(sidecar: dict) -> Dict[str, object]:
    """Banks (je8086/xenia) keep per-patch params under 'params'; patch sidecars use
    'mappedParams' with {param|name: value} or a flat {index: value} map."""
    if isinstance(sidecar.get("params"), dict):
        flat = {}
        for patch_name, params in sidecar["params"].items():
            if isinstance(params, dict):
                for k, v in params.items():
                    flat.setdefault(str(k), v)
        if flat:
            return flat
    mp = sidecar.get("mappedParams")
    if isinstance(mp, dict):
        flat = {}
        for key, value in mp.items():
            if isinstance(value, dict):
                name = value.get("param") or value.get("name") or key
                raw = value.get("value", value.get("raw", 0))
                flat[str(name)] = raw
            else:
                flat[str(key)] = value
        return flat
    return {}


def load_vocabulary(path: str) -> Dict[str, str]:
    """index -> parameter name from an emulator parameterDescriptions_*.json.

    Those files are JSON5-ish (comments, trailing commas) and some resist a strict
    parse, so the name/index pairs are read with a regex instead. The mq/xt sidecar
    libraries store parameters by numeric index, which is why this mapping exists.
    """
    text = open(path, encoding="utf-8", errors="replace").read()
    pairs = re.findall(r'"index"\s*:\s*(\d+)[^{}]{0,200}?"name"\s*:\s*"([^"]+)"', text, re.S)
    if not pairs:
        pairs = re.findall(r'"name"\s*:\s*"([^"]+)"[^{}]{0,200}?"index"\s*:\s*(\d+)', text, re.S)
        pairs = [(i, n) for n, i in pairs]
    return {str(i): n for i, n in pairs}


def harvest(roots: Sequence[str], example_limit: int = 5,
            vocab: Optional[Dict[str, Dict[str, str]]] = None) -> dict:
    files = _sidecar_files(roots)
    values: Dict[str, Dict[str, collections.Counter]] = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    examples: Dict[str, Dict[str, Dict[str, List[str]]]] = collections.defaultdict(lambda: collections.defaultdict(lambda: collections.defaultdict(list)))
    patch_counts: Dict[str, int] = collections.Counter()
    scanned = 0
    for engine, path in files:
        try:
            with open(path, encoding="utf-8") as fh:
                sidecar = json.load(fh)
        except Exception:
            continue
        scanned += 1
        patch_counts[engine] += 1
        params = _params_of(sidecar)
        label = sidecar.get("name") or os.path.basename(path)
        table = (vocab or {}).get(engine) or {}
        for name, raw in params.items():
            if name in table:                      # numeric index -> device param name
                name = table[name]
            group = _group_for(name)
            if group is None:
                continue
            try:
                val = float(raw)
            except (TypeError, ValueError):
                continue
            values[engine][name][val] += 1
            if len(examples[engine][name][val]) < example_limit:
                examples[engine][name][val].append(str(label))
    if vocab:
        for engine, table in vocab.items():
            if engine not in values:
                continue
            values[engine].setdefault("__vocabulary_params_mapped__", collections.Counter())[float(len(table))] += 1
    sheet = {"schema": "hdaw.harvested.fx.presets.v1",
             "roots": [os.path.basename(os.path.normpath(r)) for r in roots],
             "scannedSidecars": scanned,
             "patchCounts": dict(patch_counts),
             "engines": {}}
    for engine, params in sorted(values.items()):
        entry = {}
        for name, counter in sorted(params.items(), key=lambda kv: -sum(kv[1].values())):
            top = counter.most_common(3)
            entry[name] = {
                "group": _group_for(name),
                "patches": sum(counter.values()),
                "topValues": [{"value": v, "count": c,
                               "examples": examples[engine][name][v][:3]} for v, c in top],
            }
        sheet["engines"][engine] = entry
    return sheet


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Harvest FX/matrix recipes from patch sidecars")
    ap.add_argument("roots", nargs="+", help="library roots that contain <patch>.<engine>.json sidecars")
    ap.add_argument("--out", metavar="FILE", help="write the preset sheet here")
    ap.add_argument("--vocab", action="append", default=[], metavar="ENGINE=PATH",
                    help="map numeric parameter indices to names for an engine, e.g. "
                         "vavra=<mqJucePlugin/parameterDescriptions_mq.json>")
    args = ap.parse_args(argv)

    for r in args.roots:
        if not os.path.isdir(r):
            print("not a directory: %s" % r, file=sys.stderr)
            return 2
    vocab: Dict[str, Dict[str, str]] = {}
    for spec in args.vocab:
        if "=" not in spec:
            print("--vocab expects ENGINE=PATH", file=sys.stderr)
            return 2
        engine, path = spec.split("=", 1)
        if not os.path.isfile(path):
            print("vocabulary file not found: %s" % path, file=sys.stderr)
            return 2
        vocab[engine] = load_vocabulary(path)
    sheet = harvest(args.roots, vocab=vocab)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline=chr(10)) as fh:
            fh.write(json.dumps(sheet, indent=1, sort_keys=True) + chr(10))
    print("scanned %d sidecars: %s" % (sheet["scannedSidecars"], json.dumps(sheet["patchCounts"])))
    for engine, params in sheet["engines"].items():
        names = list(params)[:4]
        print("  %-12s %d FX/mod params  e.g. %s" % (engine, len(params), ", ".join(names)))
    if args.out:
        print("sheet -> %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
