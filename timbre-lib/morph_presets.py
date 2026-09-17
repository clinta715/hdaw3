#!/usr/bin/env python3
"""Generate MORPH CHAIN presets by interpolating harvested matrix presets.

Reads a harvested matrix sheet (schema ``hdaw.matrix.preset.v1``, produced by
``harvest_matrix_presets.py``) and, for every requested pair of preset
indices, emits the intermediate presets of a linear morph chain between the
two parents:

- CONTINUOUS keys (every key containing ``Amount`` plus F1Cutoff,
  F1Resonance, Lfo1Delay, Lfo2Delay, EffectParamA/B/C, ModDelayTime, Pan,
  DePan, MixRingMod; classification is per engine -- je8086 sheets classify
  by decoder name, see ``is_continuous``) are interpolated linearly
  A->B, rounded to 1 decimal.
- DISCRETE keys (EffectType, ChorusEnabled, Mod*/Slot* Type/Source/
  Destination/Parameter, ModDelaySource, everything else) are ANCHORED to A
  for the whole chain; when A and B disagree the key is recorded in every
  step's ``jumps`` list (a documented discrete hop).
- Continuous keys that are ``null`` on either side are NOT interpolated;
  A's value (including its null) is copied through unchanged.
- Chain endpoints (position 0 = A, position steps+1 = B) are the ORIGINAL
  presets, embedded by id via ``parents`` -- never regenerated.  Only the
  intermediates are emitted, at t = k/(steps+1), k = 1..steps, named
  "MORPH <A> -> <B> (step k/<steps+1>)".

je8086 sheets (``engine: je8086``) classify carriers by decoder name
(``is_continuous``) and REQUIRE ``--index-map`` (``
hdaw.je8086.param.index.map.v1``): decoder names with no live plugin param
are excluded from the emitted params and recorded per preset in
``unmapped``, while every emitted param carries its live plugin index in
``paramIndex`` (directly applicable via set_fx_param).

- vavra sheets (``engine: vavra``) classify NAMED keys by device-name
  tokens (``_is_continuous_vavra``); the raw ``off_<N>`` keys are
  value-dependent (interpolate only when both parents share the key and
  differ by <= 24) and are classified by ``vavra_dump`` via the
  ``continuous_fn`` hook below.

Output schema: ``hdaw.matrix.preset.morph.v1``.  Deterministic and
byte-stable: sorted JSON, no timestamps, no absolute paths (``sourceSheet``
echoes the --sheet argument exactly as given -- pass a repo-relative path).
Stdlib only.

Run::

    python3 timbre-lib/morph_presets.py \
        --sheet timbre-lib/matrix_presets/xenia.json \
        --pairs '17:21,0:3,17:32,12:28,3:9' --steps 4 \
        --out timbre-lib/matrix_presets/xenia_morphs.json

    python3 timbre-lib/morph_presets.py \
        --sheet timbre-lib/matrix_presets/je8086.json \
        --index-map timbre-lib/matrix_presets/je8086_param_index_map.json \
        --pairs '...' --steps 4 \
        --out timbre-lib/matrix_presets/je8086_morphs.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

SHEET_SCHEMA = "hdaw.matrix.preset.v1"
MORPH_SCHEMA = "hdaw.matrix.preset.morph.v1"
INDEX_MAP_SCHEMA = "hdaw.je8086.param.index.map.v1"
APPLIES_VIA = "patch_or_sysex_unverified"

# Continuous (morphable) carriers that do NOT carry the 'Amount' substring.
# Everything else is discrete: EffectType, ChorusEnabled, Mod*/Slot*
# Type/Source/Destination/Parameter, ModDelaySource, PanKeytrack, ...
CONTINUOUS_EXACT = frozenset({
    "F1Cutoff", "F1Resonance", "Lfo1Delay", "Lfo2Delay",
    "EffectParamA", "EffectParamB", "EffectParamC", "ModDelayTime",
    "Pan", "DePan", "MixRingMod",
})


# je8086 (decoder names): a name is CONTINUOUS when it (case-insensitively)
# contains one of the tokens below; the DISCRETE suffixes win over any
# contained token (Lfo2DepthSelect ends in 'select'); DelayTime and
# PatchName never interpolate; Control* params are CC depths and always
# interpolate.
JE8086_CONTINUOUS_TOKENS = ("depth", "rate", "fade", "time", "level",
                            "feedback", "balance", "cutoff", "resonance",
                            "freq", "width", "sustain", "amount")
JE8086_DISCRETE_SUFFIXES = ("waveform", "type", "switch", "select",
                            "destination")

# vavra (microQ device names): continuous tokens per analysis; the discrete
# tokens WIN over contained continuous tokens (F1ModSource anchors even
# though it contains 'Mod').  Raw off_<N> keys are value-dependent and are
# classified in vavra_dump (interpolate only within a <= 24 hop).
VAVRA_CONTINUOUS_TOKENS = ("mix", "amount", "mod", "level", "balance",
                           "depth", "rate", "fade", "time", "cutoff")
VAVRA_DISCRETE_TOKENS = ("type", "source", "destination", "select", "mode",
                         "switch")


def _is_continuous_vavra(key):
    low = key.lower()
    if any(tok in low for tok in VAVRA_DISCRETE_TOKENS):
        return False
    return any(tok in low for tok in VAVRA_CONTINUOUS_TOKENS)


def _is_continuous_je8086(key):
    low = key.lower()
    if "patchname" in low:
        return False         # string/id param: anchor to A, never interpolate
    if low == "lfo2depthselect":
        return False         # spec: discrete despite the 'Depth' substring
    if low.endswith(JE8086_DISCRETE_SUFFIXES):
        return False         # discrete suffix wins over contained tokens
    if "delaytime" in low:
        return False         # DelayTime is excluded from interpolation
    if low.startswith("control"):
        return True          # Control* CC-depth params are depths
    return any(tok in low for tok in JE8086_CONTINUOUS_TOKENS)


def is_continuous(key, engine="xenia"):
    """True for morphable carriers under the engine's classification.

    xenia (default): *Amount* keys + the exact list above.  je8086:
    decoder-name token heuristics (``_is_continuous_je8086``).  vavra:
    device-name tokens (``_is_continuous_vavra``); the raw off_<N> rule is
    value-dependent and applied by vavra_dump on top.
    """
    if engine == "je8086":
        return _is_continuous_je8086(key)
    if engine == "vavra":
        return _is_continuous_vavra(key)
    return "Amount" in key or key in CONTINUOUS_EXACT


def parse_pairs(spec):
    """Parse '17:21,0:3' -> [(17, 21), (0, 3)]; raise ValueError on junk."""
    pairs = []
    for chunk in str(spec).split(","):
        chunk = chunk.strip()
        head, sep, tail = chunk.partition(":")
        if not sep:
            raise ValueError("bad pair %r: expected START:END" % chunk)
        try:
            a, b = int(head.strip()), int(tail.strip())
        except ValueError:
            raise ValueError(
                "bad pair %r: indices must be integers" % chunk) from None
        if a < 0 or b < 0:
            raise ValueError("bad pair %r: indices must be >= 0" % chunk)
        if a == b:
            raise ValueError("bad pair %r: endpoints must differ" % chunk)
        pairs.append((a, b))
    if not pairs:
        raise ValueError("no pairs given")
    return pairs


def _signed_diff(a, b):
    """Normalized 0..1 distance contribution of one key (0..127 scale)."""
    if a is None and b is None:
        return 0
    if a is None or b is None:
        return 127            # null <-> value is the widest possible hop
    return abs(a - b)


def mean_distance(params_a, params_b):
    """Analysis convention: mean |a-b|/127 over the full normalized signature."""
    keys = sorted(set(params_a) | set(params_b))
    total = sum(_signed_diff(params_a.get(k), params_b.get(k)) for k in keys)
    return total / 127.0 / len(keys)


def intermediate_id(id_a, id_b, step):
    """Deterministic 16-hex id: sha1 of both parent ids + chain position."""
    raw = "%s:%s:%d" % (id_a, id_b, step)
    return hashlib.sha1(raw.encode("utf-8")).hexdigest()[:16]


def build_intermediate(params_a, params_b, *, id_a, id_b, name_a, name_b,
                       role, step, steps, engine="xenia", index_map=None,
                       continuous_fn=None):
    """One intermediate preset at t = step/(steps+1) of the A->B chain.

    Continuous keys interpolate (round 1 decimal, listed in ``interp``);
    discrete keys anchor to A (documented in ``jumps`` when A/B disagree);
    nulls copy A and skip interpolation.  With an ``index_map`` (je8086),
    keys with no live plugin param are excluded entirely and listed per
    preset in ``unmapped``; every emitted key carries its live index in
    ``paramIndex``.  ``continuous_fn`` overrides per-key classification
    (vavra's value-dependent off_<N> rule); default: ``is_continuous``.
    Inputs are never mutated.
    """
    t = step / (steps + 1.0)
    params, interp, jumps = {}, {}, []
    keys = sorted(set(params_a) | set(params_b))
    for key in keys:
        if index_map is not None and key not in index_map:
            continue            # no live param: excluded, listed in unmapped
        va, vb = params_a.get(key), params_b.get(key)
        continuous = (continuous_fn(key) if continuous_fn is not None
                      else is_continuous(key, engine))
        if continuous and va is not None and vb is not None \
                and va != vb:
            params[key] = round(va + (vb - va) * t, 1)
            interp[key] = [va, vb]
        else:
            params[key] = va          # discrete anchor or null copy
            if not continuous and va != vb:
                jumps.append(key)
    preset = {
        "id": intermediate_id(id_a, id_b, step),
        "name": "MORPH %s -> %s (step %d/%d)" % (name_a, name_b, step,
                                                  steps + 1),
        "role": role,
        "params": params,
        "appliesVia": APPLIES_VIA,
        "unverified": True,
        "evidence": ("synthetic: linear interpolation of continuous params "
                     "between %s and %s; discrete keys anchored to %s"
                     % (id_a, id_b, id_a)),
        "parents": [id_a, id_b],
        "interp": interp,
        "jumps": jumps,
    }
    if index_map is not None:
        preset["paramIndex"] = {k: index_map[k]["index"] for k in params}
        preset["unmapped"] = [k for k in keys if k not in index_map]
    return preset


def build_pair(presets, index_a, index_b, steps, engine="xenia",
               index_map=None, continuous_fn=None):
    """Morph chain for one pair: parents (originals, by id) + intermediates."""
    for idx in (index_a, index_b):
        if not 0 <= idx < len(presets):
            raise IndexError("pair index %d out of range (sheet has %d "
                             "presets)" % (idx, len(presets)))
    pa, pb = presets[index_a], presets[index_b]
    chain = []
    for k in range(1, steps + 1):
        chain.append({
            "step": k,
            "preset": build_intermediate(
                pa["params"], pb["params"],
                id_a=pa["id"], id_b=pb["id"],
                name_a=pa["name"], name_b=pb["name"],
                role=pa.get("role", "unknown"), step=k, steps=steps,
                engine=engine, index_map=index_map,
                continuous_fn=continuous_fn),
        })
    return {
        "pair": "%d:%d" % (index_a, index_b),
        "parents": [{"id": pa["id"], "name": pa["name"]},
                    {"id": pb["id"], "name": pb["name"]}],
        "distance": round(mean_distance(pa["params"], pb["params"]), 6),
        "steps": chain,
    }


def build_morph_sheet(sheet, pairs, steps, source, index_map=None,
                      continuous_fn=None):
    """Full hdaw.matrix.preset.morph.v1 document over a v1 sheet."""
    if sheet.get("schema") != SHEET_SCHEMA:
        raise ValueError("bad sheet schema %r (expected %r)"
                         % (sheet.get("schema"), SHEET_SCHEMA))
    if not isinstance(sheet.get("presets"), list):
        raise ValueError("sheet carries no 'presets' list")
    if steps < 1:
        raise ValueError("--steps must be >= 1")
    engine = sheet.get("engine") or "xenia"
    if engine == "je8086" and index_map is None:
        raise ValueError("je8086 sheets require --index-map (decoder name -> "
                         "live param index); generated presets must carry "
                         "paramIndex")
    return {
        "schema": MORPH_SCHEMA,
        "sourceSheet": source,          # echoed verbatim: pass a relative path
        "pairs": [build_pair(sheet["presets"], a, b, steps, engine=engine,
                             index_map=index_map,
                             continuous_fn=continuous_fn)
                  for a, b in pairs],
        "unverified": True,
    }


def render_json(document):
    """Deterministic serialization: sorted keys, stable floats, trailing NL."""
    return json.dumps(document, indent=2, ensure_ascii=False,
                      sort_keys=True) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate morph-chain presets between harvested matrix "
                    "presets (hdaw.matrix.preset.morph.v1).")
    parser.add_argument("--sheet", required=True,
                        help="hdaw.matrix.preset.v1 sheet (pass a relative "
                             "path; it is echoed into sourceSheet)")
    parser.add_argument("--pairs", required=True,
                        help="comma list like '17:21,0:3'")
    parser.add_argument("--steps", type=int, default=4,
                        help="intermediates per pair (default 4)")
    parser.add_argument("--out", required=True, help="output JSON path")
    parser.add_argument("--index-map", default=None,
                        help="hdaw.je8086.param.index.map.v1 JSON (required "
                             "for je8086 sheets: emits paramIndex + unmapped "
                             "per preset, drops keys with no live param)")
    args = parser.parse_args(argv)
    try:
        pairs = parse_pairs(args.pairs)
        with open(args.sheet, encoding="utf-8") as fh:
            sheet = json.load(fh)
        index_map = None
        if args.index_map:
            with open(args.index_map, encoding="utf-8") as fh:
                index_doc = json.load(fh)
            if index_doc.get("schema") != INDEX_MAP_SCHEMA:
                raise ValueError("bad index-map schema %r (expected %r)"
                                 % (index_doc.get("schema"),
                                    INDEX_MAP_SCHEMA))
            index_map = index_doc["map"]
        morph = build_morph_sheet(sheet, pairs, args.steps, args.sheet,
                                  index_map=index_map)
    except (ValueError, IndexError, KeyError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    parent = os.path.dirname(os.path.abspath(args.out))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(render_json(morph))
    total = 0
    for pair in morph["pairs"]:
        total += len(pair["steps"])
        print("pair %-9s d=%.4f steps=%d"
              % (pair["pair"], pair["distance"], len(pair["steps"])))
    print("wrote %s (%d pairs, %d intermediate steps)"
          % (args.out, len(morph["pairs"]), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
