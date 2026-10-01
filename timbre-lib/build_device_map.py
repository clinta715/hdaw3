#!/usr/bin/env python3
"""Build the core-synth Device Parameter Map.

Deterministic, offline, stdlib-only. Joins the usefulness evidence that already
exists in five formats into one queryable artifact per engine:

  in:  timbre-lib/matrix_presets/<engine>.json      (corpus clusters: role + params)
       timbre-lib/matrix_presets/<engine>_param_index_map.json (je8086: name -> live index)
       timbre-lib/matrix_presets/{xenia,vavra}_offset_map.json, nord_offset_map.json
       timbre-lib/device_map/intents.json            (vocabulary + grammar + overrides)
  out: timbre-lib/device_map/<engine>.params.json    (schema hdaw.device.param.map.v1)
       timbre-lib/device_map/index.json              (schema hdaw.device.index.v1)

The map encodes reachability (name/index/offset), route (appliesVia/durability),
intent (category/tier/intents/stages), and honesty (trap reasons). Audibility is
carried as "unknown" until slice 2 probes it - never fabricated.

Plan: docs/plans/2026-09-21-device-param-map.md
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MP = REPO / "timbre-lib" / "matrix_presets"
OUT = REPO / "timbre-lib" / "device_map"

ENGINES = ["je8086", "nodalred2x", "xenia", "virus", "vavra"]

# The 15 internal fxTypes. Their parameter surface is STATIC C++ TABLES, a
# stronger single source of truth than the VA corpus route: the DSP that
# consumes the values owns the names/ranges, so the advertised def and the
# clamp cannot drift. 13 of the tables live as `{ idx, "Name", def, min, max }`
# literal rows inside TrackFXSlot.h's getParamDefsForType(); delay and filter
# delegate to their own headers (paramDefs() rows have no explicit index).
INTERNAL_ENGINES = {
    "eq":         {"file": "src/engine/TrackFXSlot.h",    "table": "eq"},
    "compressor": {"file": "src/engine/TrackFXSlot.h",    "table": "compressor"},
    "reverb":     {"file": "src/engine/TrackFXSlot.h",    "table": "reverb"},
    "delay":      {"file": "src/engine/InternalDelay.h",  "table": None},
    "chorus":     {"file": "src/engine/TrackFXSlot.h",    "table": "chorus"},
    "flanger":    {"file": "src/engine/TrackFXSlot.h",    "table": "flanger"},
    "phaser":     {"file": "src/engine/TrackFXSlot.h",    "table": "phaser"},
    "filter":     {"file": "src/engine/InternalFilter.h", "table": None},
    "saturator":  {"file": "src/engine/TrackFXSlot.h",    "table": "saturator"},
    "sampler":    {"file": "src/engine/TrackFXSlot.h",    "table": "sampler"},
    "fm_synth":   {"file": "src/engine/TrackFXSlot.h",    "table": "fm_synth"},
    "growl_bass": {"file": "src/engine/TrackFXSlot.h",    "table": "growl_bass"},
    "psyarp":     {"file": "src/engine/TrackFXSlot.h",    "table": "psyarp"},
    "psy_fm":     {"file": "src/engine/TrackFXSlot.h",    "table": "psy_fm"},
    "sub_synth":  {"file": "src/engine/TrackFXSlot.h",    "table": "sub_synth"},
}

_NUM = r"-?\d+(?:\.\d+)?f?"
# A literal def row, e.g. { 12, "OP1 Coarse", 0.0f, 0.0f, 31.0f },
_ROW_IDX = re.compile(
    r'^\s*\{\s*(\d+),\s*"([^"]+)",\s*(' + _NUM + r'),\s*(' + _NUM + r'),\s*(' + _NUM + r')\s*\}')
_TOKEN = r"(?:[A-Za-z_]\w*|-?\d+(?:\.\d+)?f?)"
# Header-table row without an index: { "Cutoff", 1000.0f, 20.0f, 20000.0f },
# or with constexpr tokens: { "Delay Time", 0.5f, kMinDelaySeconds, kMaxDelaySeconds },
_ROW_NONAME_IDX = re.compile(
    r'^\s*\{\s*"([^"]+)",\s*(' + _TOKEN + r'),\s*(' + _TOKEN + r'),\s*(' + _TOKEN + r')\s*\}')
_SECTION_START = re.compile(r'if \(type == "([a-z0-9_]+)"\)')
_ARRAY_TABLE = re.compile(r'defs = \{ \{')
_F = lambda s: float(s.rstrip("f"))
MAP_SCHEMA = "hdaw.device.param.map.v1"
INDEX_SCHEMA = "hdaw.device.index.v1"

STAGE_BY_TIER = {
    "movement": ["fx-automation-engineer"],
    "identity": ["sound-selector"],
    "trap": [],
}


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def match_any(name_lc: str, patterns):
    for p in patterns:
        if p.lower() in name_lc:
            return True
    return False


def load_sheet(engine: str):
    """Return (param_names:set, roles_by_param:dict[name]->set, patchCount:int)."""
    path = MP / f"{engine}.json"
    if not path.exists():
        raise SystemExit(f"missing sheet: {path}")
    root = load_json(path)
    names: set[str] = set()
    roles: dict[str, set] = {}
    for preset in root.get("presets", []):
        role = preset.get("role") or "unroled"
        for name in (preset.get("params") or {}):
            names.add(name)
            roles.setdefault(name, set()).add(role)
    return names, roles, int(root.get("patchCount") or 0)


def load_index_map(engine: str):
    """je8086 decoder-name -> live plugin param index (and the unmatched list)."""
    path = MP / f"{engine}_param_index_map.json"
    if not path.exists():
        return {}, []
    root = load_json(path)
    out = {}
    for name, meta in (root.get("map") or {}).items():
        if isinstance(meta, dict) and "index" in meta:
            out[name] = int(meta["index"])
    return out, list(root.get("unmatched") or [])


def _offset_from_value(value):
    if isinstance(value, dict):
        for k in ("offset", "dumpOffset", "sysexOffset", "payloadOffset"):
            if k in value and value[k] is not None:
                return int(value[k])
    return None


def load_offset_map(engine: str):
    """name -> SysEx dump offset, from either {offset:name} or {name:{offset}}."""
    for fname in (f"{engine}_offset_map.json", f"{engine}-offset-map.json"):
        path = MP / fname
        if not path.exists():
            continue
        root = load_json(path)
        if isinstance(root, dict) and "params" in root and isinstance(root["params"], dict):
            root = root["params"]
        out = {}
        for k, v in root.items():
            if isinstance(k, str) and k.isdigit() and isinstance(v, str):
                out[v] = int(k)
            elif isinstance(k, str) and isinstance(v, dict):
                off = _offset_from_value(v)
                if off is not None:
                    out[k] = off
        return out
    return {}


def parse_track_fx_slot_tables():
    """Parse getParamDefsForType's literal `{ idx, "Name", def, min, max }`
    tables out of src/engine/TrackFXSlot.h. Returns {typeName: [row,...]} where
    row = (index, name, default, min, max, line). The caller FAILS LOUDLY on a
    missing or zero-row table (it names the engines it expects)."""
    path = REPO / "src" / "engine" / "TrackFXSlot.h"
    lines = path.read_text(encoding="utf-8").splitlines()
    tables: dict = {}
    current = None
    for i, line in enumerate(lines, start=1):
        m = _SECTION_START.search(line)
        if m:
            current = m.group(1)
            tables.setdefault(current, [])
            continue
        if current is None:
            continue
        row = _ROW_IDX.match(line)
        if row:
            tables[current].append((int(row.group(1)), row.group(2),
                                    _F(row.group(3)), _F(row.group(4)),
                                    _F(row.group(5)), i))
        elif line.strip() == "return {};":
            current = None
    return tables


def parse_header_param_defs(relpath: str):
    """Parse a `defs = { { ... } };` ParamDef table from an internal engine
    header (rows carry no index; indices are assigned in order). Rows may use
    the header's own `static constexpr float` tokens (e.g. kMaxFeedback) —
    resolved from the same file, still in-source evidence.
    Returns [(index, name, default, min, max, line), ...]."""
    path = REPO / relpath
    text = path.read_text(encoding="utf-8")
    consts = {m.group(1): float(m.group(2).rstrip("f")) for m in
              re.finditer(r'static constexpr float (\w+) = (-?\d+(?:\.\d+)?)f?;', text)}
    lines = text.splitlines()
    rows = []
    inside = False
    for i, line in enumerate(lines, start=1):
        if not inside:
            if _ARRAY_TABLE.search(line):
                inside = True
            continue
        if line.strip().startswith("} };"):
            break
        row = _ROW_NONAME_IDX.match(line)
        if row:
            def val(tok):
                return consts[tok] if tok in consts else float(tok.rstrip("f"))
            rows.append((len(rows), row.group(1), val(row.group(2)),
                         val(row.group(3)), val(row.group(4)), i))
    return rows


def parse_internal_table(engine: str, spec: dict, slot_tables: dict):
    if spec["table"] is None:
        rows = parse_header_param_defs(spec["file"])
    else:
        if spec["table"] not in slot_tables:
            raise SystemExit(f"source table vanished: {spec['file']} has no"
                             f" `if (type == \"{spec['table']}\")` section")
        rows = slot_tables[spec["table"]]
    if not rows:
        raise SystemExit(f"source table parsed to ZERO rows: {engine}"
                         f" ({spec['file']}) — refusing to emit an empty map")
    return rows


def classify(name: str, cat_rules, intent_rules, category_defaults):
    lc = name.lower()
    category = None
    for rule in cat_rules:
        if match_any(lc, rule.get("match", [])):
            category = rule["category"]
            break

    intents: list[str] = []
    movement = False
    identity = False
    matched_rules = []
    for rule in intent_rules:
        if match_any(lc, rule.get("match", [])):
            matched_rules.append(rule["id"])
            for it in rule.get("intents", []):
                if it not in intents:
                    intents.append(it)
            if rule.get("tier") == "movement":
                movement = True
            elif rule.get("tier") == "identity":
                identity = True

    # Explicit intent rules win; otherwise the category default supplies the
    # intent+tier (e.g. an unnamed fx-delay sub-param defaults to delay-throw).
    if not matched_rules and category in category_defaults:
        default = category_defaults[category]
        matched_rules.append(f"categoryDefault:{category}")
        for it in default.get("intents", []):
            if it not in intents:
                intents.append(it)
        if default.get("tier") == "movement":
            movement = True
        elif default.get("tier") == "identity":
            identity = True

    tier = "movement" if movement else ("identity" if identity else None)
    return category, intents, tier, matched_rules


def build_engine(engine: str, intent_cfg: dict):
    sheet_names, roles, patch_count = load_sheet(engine)
    index_map, unmatched = load_index_map(engine)
    offset_map = load_offset_map(engine)

    names = set(sheet_names) | set(index_map) | set(offset_map)
    override = (intent_cfg.get("engineOverrides") or {}).get(engine, {})
    traps = override.get("traps") or {}
    notes = override.get("notes") or {}

    cat_rules = intent_cfg["categoryRules"]
    intent_rules = intent_cfg["intentRules"]
    category_defaults = intent_cfg.get("categoryDefaults") or {}
    name_traps = [(re.compile(r["regex"]), r) for r in (intent_cfg.get("nameTrapRules") or [])]

    entries = []
    for name in sorted(names):
        category, intents, tier, matched = classify(
            name, cat_rules, intent_rules, category_defaults)
        trap_reason = None
        note = notes.get(name)
        # Precedence: explicit engine trap > generic name trap > unclassified.
        if name in traps:
            tier = "trap"
            trap_reason = traps[name].get("reason", "trap")
            note = traps[name].get("note", note)
        else:
            for rx, rule in name_traps:
                if rx.match(name):
                    tier = "trap"
                    trap_reason = rule.get("reason", rule.get("id", "trap"))
                    note = rule.get("note", note)
                    break
        if tier is None:
            tier = "trap"
            trap_reason = trap_reason or "unclassified"
        if category is None:
            category = "unclassified"

        sources = []
        if name in sheet_names:
            sources.append(f"matrix:{engine}.json")
        if name in index_map:
            sources.append("indexmap")
        if name in offset_map:
            sources.append("offsetmap")
        sources += [f"grammar:{m}" for m in matched]
        if not sources:
            sources = ["derived:name"]

        entries.append({
            "name": name,
            "index": index_map.get(name),
            "offset": offset_map.get(name),
            "category": category,
            "tier": tier,
            "intents": intents,
            "stages": STAGE_BY_TIER[tier],
            "roles": sorted(roles.get(name, [])),
            "sources": sources,
            "trapReason": trap_reason,
            "note": note,
        })

    counts = {
        "total": len(entries),
        "movement": sum(1 for e in entries if e["tier"] == "movement"),
        "identity": sum(1 for e in entries if e["tier"] == "identity"),
        "trap": sum(1 for e in entries if e["tier"] == "trap"),
        "unclassified": sum(1 for e in entries if e["trapReason"] == "unclassified"),
        "indexMapped": sum(1 for e in entries if e["index"] is not None),
        "offsetMapped": sum(1 for e in entries if e["offset"] is not None),
    }

    doc = {
        "schema": MAP_SCHEMA,
        "engine": engine,
        "verifiedOn": intent_cfg.get("verifiedOn"),
        "appliesVia": override.get("appliesVia"),
        "durability": override.get("durability"),
        "durabilityNote": override.get("durabilityNote"),
        "source": {
            "sheet": f"matrix_presets/{engine}.json",
            "patchCount": patch_count,
            "unmatchedIndexNames": unmatched,
        },
        "counts": counts,
        "params": entries,
    }
    return doc


def build_internal_engine(engine: str, spec: dict, slot_tables: dict,
                          intent_cfg: dict, tables_cfg: dict):
    cfg = tables_cfg["engines"][engine]
    cat_rules = cfg.get("categoryRules") or []
    intent_rules = cfg.get("intentRules") or []
    category_defaults = cfg.get("categoryDefaults") or {}
    param_notes = cfg.get("paramNotes") or {}

    rows = parse_internal_table(engine, spec, slot_tables)
    entries = []
    for index, name, default, vmin, vmax, line_no in rows:
        category, intents, tier, matched = classify(
            name, cat_rules, intent_rules, category_defaults)
        if tier is None:
            # Every entry must classify; an unclassified param is reported,
            # never hidden (same honesty rule as the VA route).
            tier = "trap"
            category = category or "unclassified"
            matched = matched + ["unclassified"]
        entry = {
            "name": name,
            "index": index,
            "offset": None,
            "category": category,
            "tier": tier,
            "intents": intents,
            "stages": STAGE_BY_TIER[tier],
            "roles": [],
            "sources": [f"{spec['file']}:{line_no}"],
            "source": f"{spec['file']}:{line_no}",
            "trapReason": None,
            "note": (param_notes.get(name) or {}).get("note"),
            "default": default,
            "min": vmin,
            "max": vmax,
        }
        enum = (param_notes.get(name) or {}).get("enum")
        if enum:
            entry["enum"] = enum
        if entry["trapReason"] is None and not entry["note"]:
            del entry["note"]
        entries.append(entry)

    counts = {
        "total": len(entries),
        "movement": sum(1 for e in entries if e["tier"] == "movement"),
        "identity": sum(1 for e in entries if e["tier"] == "identity"),
        "trap": sum(1 for e in entries if e["tier"] == "trap"),
        "unclassified": sum(1 for e in entries if e["category"] == "unclassified"),
        "indexMapped": len(entries),
        "offsetMapped": 0,
    }
    return {
        "schema": MAP_SCHEMA,
        "engine": engine,
        "verifiedOn": tables_cfg.get("verifiedOn"),
        "appliesVia": cfg.get("appliesVia"),
        "durability": cfg.get("durability"),
        "durabilityNote": cfg.get("durabilityNote"),
        "source": {
            "tables": spec["file"],
            "evidence": "in-source static param table (see per-param sources)",
        },
        "counts": counts,
        "params": entries,
    }


def build_index(docs, intent_cfg):
    stages = []
    for it in intent_cfg["intents"]:
        if it["stage"] not in stages:
            stages.append(it["stage"])
    return {
        "schema": INDEX_SCHEMA,
        "verifiedOn": intent_cfg.get("verifiedOn"),
        "engines": [
            {
                "engine": d["engine"],
                "paramCount": d["counts"]["total"],
                "appliesVia": d["appliesVia"],
                "durability": d["durability"],
            }
            for d in docs
        ],
        "intents": intent_cfg["intents"],
        "stages": stages,
        "stageNotes": intent_cfg.get("stageNotes", {}),
        "excludedStages": intent_cfg.get("excludedStages", []),
    }


def validate(doc):
    errs = []
    if doc["schema"] != MAP_SCHEMA:
        errs.append("bad schema")
    for e in doc["params"]:
        if not e["name"]:
            errs.append("empty name")
        if not e["category"]:
            errs.append(f"{e['name']}: empty category")
        if e["tier"] not in STAGE_BY_TIER:
            errs.append(f"{e['name']}: bad tier {e['tier']}")
        if not isinstance(e["stages"], list):
            errs.append(f"{e['name']}: stages not a list")
        if not e["sources"]:
            errs.append(f"{e['name']}: no sources")
        if "default" in e and not (e["min"] <= e["default"] <= e["max"]):
            errs.append(f"{e['name']}: default {e['default']} outside"
                        f" [{e['min']}, {e['max']}]")
    return errs


def dumps(obj):
    return json.dumps(obj, indent=1, sort_keys=True, ensure_ascii=False) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if any output would change (no writes)")
    args = ap.parse_args()

    intent_cfg = load_json(OUT / "intents.json")
    OUT.mkdir(parents=True, exist_ok=True)

    docs = [build_engine(e, intent_cfg) for e in ENGINES]
    tables_cfg = intent_cfg.get("internalEngines") or {}
    missing = sorted(set(INTERNAL_ENGINES) - set(tables_cfg.get("engines") or {}))
    if missing:
        raise SystemExit(f"intents.json internalEngines missing: {', '.join(missing)}")
    slot_tables = parse_track_fx_slot_tables()
    for engine, spec in INTERNAL_ENGINES.items():
        docs.append(build_internal_engine(engine, spec, slot_tables,
                                          intent_cfg, tables_cfg))
    for d in docs:
        errs = validate(d)
        if errs:
            for e in errs:
                print(f"VALIDATION FAIL {d['engine']}: {e}", file=sys.stderr)
            return 1

    index = build_index(docs, intent_cfg)

    planned = {f"{d['engine']}.params.json": dumps(d) for d in docs}
    planned["index.json"] = dumps(index)

    if args.check:
        changed = []
        for fname, text in planned.items():
            path = OUT / fname
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                changed.append(fname)
        if changed:
            print("OUT OF DATE: " + ", ".join(changed), file=sys.stderr)
            return 1
        print("device map is up to date")
        return 0

    for fname, text in planned.items():
        (OUT / fname).write_text(text, encoding="utf-8", newline="\n")

    total = 0
    for d in docs:
        c = d["counts"]
        total += c["total"]
        print(f"{d['engine']:12s} total={c['total']:4d} movement={c['movement']:4d} "
              f"identity={c['identity']:4d} trap={c['trap']:4d} "
              f"(unclassified={c['unclassified']:3d}) "
              f"indexMapped={c['indexMapped']:3d} offsetMapped={c['offsetMapped']:3d}")
    print(f"{'TOTAL':12s} {total} params across {len(docs)} engines")
    for fname, text in sorted(planned.items()):
        h = hashlib.sha256(text.encode("utf-8")).hexdigest()[:12]
        print(f"  wrote {fname}  sha256:{h}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
