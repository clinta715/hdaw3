#!/usr/bin/env python3
"""Harvest per-engine MATRIX PRESETS out of the patch-library sidecars.

Unlike ``harvest_fx_presets.py`` (which aggregates value *distributions* for
re-creation with HDAW's internal FX), this tool extracts the concrete FX /
modulation-matrix parameter TUPLE of each canonical patch config and emits
named, device-applicable presets: "je8086: trance delay throw", "xenia:
6-slot matrix + fx swell", and so on.  One preset == the full matrix/FX
tuple of one canonical patch (clusters of identical configs), NOT a value
distribution.

Output: ``matrix_presets/<engine>.json``, schema ``hdaw.matrix.preset.v1``::

    {"schema": "hdaw.matrix.preset.v1", "engine": "...", "unverified": true,
     "sourceRoots": [basenames only], "scannedSidecars": N, "patchCount": N,
     "presets": [{"id", "name", "role", "params", "appliesVia",
                  "examples", "evidence"}]}

Per-engine sidecar shapes (verified 2026-09-16 against the real libraries):

  je8086      bank sidecars keep one params dict PER PATCH under
              mappedParams.<slotNNN>.params (name-keyed); the exploded
              per-patch sidecars carry a flat name-keyed mappedParams.
              Names are device names already -- never map this engine by
              dump offset (the plugin's 461-param list is not the SysEx
              layout).
  xenia       bank sidecars keep params.<patchName> = {numericIndex: value};
              indices are mapped through the Microwave XT vocabulary
              (parameterDescriptions_xt.json) read with the same regex
              approach harvest_fx_presets.py uses for the JSON5-ish
              vocabularies.  Patch-section indices are the FIRST occurrence
              in the file (later global/multi/instrument sections reuse the
              same index space); intra-section EffectParamX aliases prefer
              the more specific sibling name (xt index 81: EffectParamA ->
              DelayTime).  The sidecar KEY SPACE differs by container
              (matrix_presets/xenia-offset-map.md): .usb banks key params
              by vocab index directly, .mid/.syx banks key by body offset
              (key 0 = bank byte, key 1 = program byte, param N at key
              N+2) -- mid keys are shifted back by 2 before labeling, and
              keys 0/1 / the 242+ name bytes never label a param.  (The
              unshifted labeling mis-read ~50% of the named values in
              .mid-derived clusters -- off-by-2, fixed 2026-09-18.)
  nodalred2x  patch sidecars carry named mappedParams.{i: {param, raw, value}}.
              The opaque ``paramNN`` slots are deliberately excluded from
              the tuple (unnamed in the normalized space -> would make
              presets unreadable; the handoff's FilterEnv *Sens* members
              live there and are deferred with the microQ offset map).
  vavra       params = raw Waldorf dump OFFSETS (7..369), identical shape
              across the corpus.  When the VERIFIED offset->name map is
              available (matrix_presets/vavra_offset_map.json; rule and FX
              alias table in matrix_presets/vavra-offset-map.md: dump byte
              = 7 + linear index, one 7-bit byte per param), mapped offsets
              are renamed to their canonical primary names and unmapped
              offsets stay raw as "off_<offset>" -- names are never
              invented.  FX sub-param bytes (137-150/153-166) keep the
              PRIMARY alias name: the FXType-value -> alias enum is NOT
              verified, so no context suffixing.  Mapped presets use
              appliesVia=state_blob_or_patch_unverified (mapping solved;
              the APPLY path stays unverified until the live-engine pass);
              without a map the raw-offset behavior
              (appliesVia=unmapped-pending-offset-map) is unchanged.
  virus       sidecars are sub_synth-normalized: 23 params, no FX/matrix
              values; FX/mod-matrix features appear only as a CONSTANT
              presence list (unmapped).  The harvester therefore emits the
              universal feature-presence config plus a shortfall note
              instead of pretending per-patch tuples exist.

Presets are marked ``"unverified": true``: cluster frequency is not musical
quality -- the listening/apply pass (handoff Phase D) is the quality gate.

Usage::

    py -3 timbre-lib/harvest_matrix_presets.py            # real roots
    py -3 timbre-lib/harvest_matrix_presets.py --out-dir DIR ROOT...

No third-party dependencies (stdlib only).
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import re
import sys
from typing import Dict, Iterator, List, Optional, Sequence, Tuple

SCHEMA = "hdaw.matrix.preset.v1"

SUFFIX_ENGINE = {
    ".je8086.json": "je8086",
    ".vavra.json": "vavra",
    ".xenia.json": "xenia",
    ".nl2x.json": "nodalred2x",
    ".virus.json": "virus",
}

APPLIES_VIA = {
    "je8086": "set_fx_param",
    "virus": "midi_cc_pc",
    "nodalred2x": "load_nord_bank",
    "xenia": "patch_or_sysex_unverified",
    "vavra": "unmapped-pending-offset-map",
}

# appliesVia once a verified offset->name map is applied: the MAPPING is
# solved, the APPLY path itself (state blob / patch load) stays unverified
# until the live-engine pass.
APPLIES_VIA_OFFSET_MAPPED = {
    "vavra": "state_blob_or_patch_unverified",
}

# Default library roots (WSL spellings; the same directories are D:\pdf\...
# on Windows).  Only the first existing candidate is used per engine.
DEFAULT_ROOTS = {
    "je8086": ("/mnt/d/pdf/je8086", "D:/pdf/je8086"),
    "nodalred2x": ("/mnt/d/pdf/NL2x Banks", "D:/pdf/NL2x Banks"),
    "xenia": ("/mnt/d/pdf/microwave", "D:/pdf/microwave"),
    "vavra": ("/mnt/d/pdf/rhythm-lab.com_waldorf_micro_q",
              "D:/pdf/rhythm-lab.com_waldorf_micro_q"),
    "virus": ("/mnt/d/pdf/Virus Presets", "D:/pdf/Virus Presets"),
}

# The xt vocabulary ships inside the emulator sources; first hit wins.
VOCAB_CANDIDATES = {
    "xenia": (
        "/mnt/d/pdf/gearmulator-2.2.9/source/xtJucePlugin/parameterDescriptions_xt.json",
        "/mnt/d/pdf/retromulator-main/source/xtJucePlugin/parameterDescriptions_xt.json",
        "D:/pdf/gearmulator-2.2.9/source/xtJucePlugin/parameterDescriptions_xt.json",
    ),
}

# Verified dump-offset -> parameter-name maps (rule + FX alias caveat in
# matrix_presets/vavra-offset-map.md).  First existing candidate wins,
# like VOCAB_CANDIDATES.
OFFSET_MAP_CANDIDATES = {
    "vavra": (os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "matrix_presets", "vavra_offset_map.json"),),
}

# The matrix/FX tuple per engine, in the handoff's vocabulary order.
# (docs/handoffs/2026-09-16-matrix-presets-handoff.md, "What a matrix preset
# is per engine" -- names verified against the sidecar param inventory.)
TUPLE_PARAMS = {
    "je8086": [
        # FX block
        "MultiEffectsType", "MultiEffectsLevel",
        "DelayType", "DelayTime", "DelayFeedback", "DelayLevel",
        "ChorusType", "ChorusLevel",
        # LFOs + per-target depths
        "Lfo1Waveform", "Lfo1Rate", "Lfo1Fade",
        "Lfo2Rate", "Lfo2DepthSelect",
        "AmpLfo1Depth", "AmpLfo2Depth", "FilterLfo1Depth", "FilterLfo2Depth",
        "PitchLfo2Depth", "OscLfo1Depth",
        # envelopes (depths + ADSR)
        "PitchEnvelopeAttackTime", "PitchEnvelopeDecayTime",
        "PitchEnvelopeDepth", "PitchEnvelopeSustainLevel",
        "PitchEnvelopeReleaseTime",
        "FilterEnvelopeAttackTime", "FilterEnvelopeDecayTime",
        "FilterEnvelopeDepth", "FilterEnvelopeSustainLevel",
        "FilterEnvelopeReleaseTime",
        "AmpEnvelopeAttackTime", "AmpEnvelopeDecayTime",
        "AmpEnvelopeSustainLevel", "AmpEnvelopeReleaseTime",
        # routing switches + tone-shaping matrix members
        "Lfo1AndEnvelopeDestination", "CrossModulationDepth",
        "RingModulatorSwitch", "AutoPanManualPanSwitch",
        "CutoffFrequency", "Resonance", "CutoffSlope",
        # performance-control routing depths (D-beam / assigned controls)
        "ControlAmpLfo1Depth", "ControlAmpLevel", "ControlAmpEnvAttackTime",
        "ControlCrossModulationDepth", "ControlCutoffFrequency",
        "ControlFilterEnvDepth", "ControlLfo1Rate", "ControlLfo2Rate",
        "ControlOsc2FineWide", "ControlOsc2Range", "ControlOscillatorBalance",
        "ControlResonance",
    ],
    "nodalred2x": [
        # normalized-space equivalents of the handoff's Nord matrix vocab
        "filter_env_amount",
        "filter_env_attack", "filter_env_decay",
        "filter_env_sustain", "filter_env_release",
        "mod_env_attack", "mod_env_decay", "mod_env_level", "mod_env_dest",
        "lfo1_rate", "lfo1_level", "lfo1_dest", "lfo1_waveform",
        "lfo2_rate", "lfo2_dest",
        "fm_depth", "sync_distortion",
    ],
    "xenia": [
        # FX block
        "EffectType", "EffectParamA", "EffectParamB", "EffectParamC",
        "ChorusEnabled", "Pan", "PanKeytrack", "DePan",
        "MixRingMod", "ModDelaySource", "ModDelayTime",
        "Lfo1Delay", "Lfo2Delay",
        # wave-envelope amounts
        "W1EnvAmount", "W1EnvVelAmount", "W2EnvAmount", "W2EnvVelAmount",
        # filter modulation
        "F1Cutoff", "F1Resonance", "F1EnvAmount", "F1EnvVelAmount",
        # the Microwave XT modulation matrix proper: Mod1..4
        "Mod1Source1", "Mod1Source2", "Mod1Type", "Mod1Parameter",
        "Mod2Source1", "Mod2Source2", "Mod2Type", "Mod2Parameter",
        "Mod3Source1", "Mod3Source2", "Mod3Type", "Mod3Parameter",
        "Mod4Source1", "Mod4Source2", "Mod4Type", "Mod4Parameter",
    ] + ["Slot%d%s" % (n, field) for n in range(1, 17)
         for field in ("Source", "Amount", "Destination")],
    # vavra: raw dump offsets 7..369, keyed as "offset" strings (never named)
    "vavra": None,
    # virus: feature-presence config (see module docstring)
    "virus": None,
}

VIRUS_MATRIX_FEATURES = ("fx_delay", "fx_chorus", "fx_reverb", "mod_matrix",
                         "lfo1", "lfo2", "ring_mod", "osc2_fm_amount")

VOCAB_SHORTFALL_MIN_PRESETS = 10


def load_vocabulary(path: str) -> Dict[str, str]:
    """index -> parameter name from an emulator parameterDescriptions_*.json.

    The vocabularies are JSON5-ish (comments, trailing commas) and some
    resist a strict parse, so index/name pairs are read with a regex (same
    approach as harvest_fx_presets.py).  The files define several sections
    (patch, multi, multi-instrument, play-param, control) that REUSE the
    same index space; the patch section comes first in the file, so the
    FIRST occurrence per index wins.  Intra-section aliases where a generic
    EffectParamX shares an index with a specific name prefer the specific
    name (xt index 81: EffectParamA -> DelayTime).
    """
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    pairs = re.findall(r'"index"\s*:\s*(\d+)[^{}]{0,200}?"name"\s*:\s*"([^"]+)"',
                       text, re.S)
    if not pairs:
        pairs = re.findall(r'"name"\s*:\s*"([^"]+)"[^{}]{0,200}?"index"\s*:\s*(\d+)',
                           text, re.S)
        pairs = [(i, n) for n, i in pairs]
    by_index: Dict[str, List[str]] = collections.OrderedDict()
    for idx, name in pairs:
        by_index.setdefault(idx, []).append(name)
    vocab: Dict[str, str] = {}
    for idx, names in by_index.items():
        chosen = names[0]
        if re.match(r"^EffectParam[A-Z]$", chosen) and len(names) > 1:
            for alt in names[1:]:
                if not re.match(r"^EffectParam[A-Z]$", alt):
                    chosen = alt
                    break
        vocab[idx] = chosen
    return vocab


def find_vocabulary(engine: str) -> Optional[str]:
    for cand in VOCAB_CANDIDATES.get(engine, ()):  # first existing wins
        if os.path.isfile(cand):
            return cand
    return None


def find_offset_map(engine: str) -> Optional[str]:
    """Path of the shipped dump-offset -> name map, if any."""
    for cand in OFFSET_MAP_CANDIDATES.get(engine, ()):  # first existing wins
        if os.path.isfile(cand):
            return cand
    return None


def load_offset_map(path: str) -> Dict[str, str]:
    """Strict loader: digit-string dump offsets -> unique non-empty names.

    Names must be unique so the offset -> name rename stays injective and
    clustering semantics are preserved.  Aliased bytes (the FX sub-params)
    must already arrive as ONE canonical primary name -- see
    matrix_presets/vavra-offset-map.md.
    """
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    if not isinstance(data, dict):
        raise ValueError("offset map must be a JSON object of offset -> name")
    mapping: Dict[str, str] = {}
    owner: Dict[str, str] = {}
    for key, name in data.items():
        if not isinstance(key, str) or not key.isdigit():
            raise ValueError("offset map keys must be digit-string dump "
                             "offsets, got %r" % (key,))
        if not isinstance(name, str) or not name:
            raise ValueError("offset map names must be non-empty strings, "
                             "got %r for offset %s" % (name, key))
        if name in owner:
            raise ValueError("offset map name %r used for offsets %s and %s; "
                             "the rename must stay injective"
                             % (name, owner[name], key))
        owner[name] = key
        mapping[key] = name
    return mapping


def _sidecar_files(roots: Sequence[str]) -> List[Tuple[str, str]]:
    """(engine, path) for every recognisable sidecar under the roots.

    Deterministic: directories and files are walked in sorted order.
    """
    out: List[Tuple[str, str]] = []
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames.sort()
            for fn in sorted(filenames):
                for suffix, engine in SUFFIX_ENGINE.items():
                    if fn.endswith(suffix):
                        out.append((engine, os.path.join(dirpath, fn)))
                        break
    return out


def _load_json(path: str) -> Optional[dict]:
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) else None


def _quant(value: object) -> object:
    """Keep raw values; quantize only meaningless float noise."""
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    try:
        fval = float(value)
    except (TypeError, ValueError):
        return str(value)
    if fval.is_integer():
        return int(fval)
    return round(fval, 3)


def _flatten_nl2x(sidecar: dict) -> Dict[str, object]:
    mp = sidecar.get("mappedParams")
    flat: Dict[str, object] = {}
    if not isinstance(mp, dict):
        return flat
    for _key, entry in mp.items():
        if isinstance(entry, dict):
            name = entry.get("param") or entry.get("name") or _key
            raw = entry.get("raw", entry.get("value", 0))
            flat[str(name)] = raw
        else:
            flat[str(_key)] = entry
    return flat


# Microwave XT patch params are vocab indices 0..239; 240..255 are the 16
# name characters (xenia-offset-map.md).
IDX_XENIA_PARAM_LAST = 239


def _xenia_sidecar_is_mid(sidecar: dict) -> bool:
    """True when a xenia sidecar's params are keyed by .mid BODY OFFSET.

    microwave_patch.py writes ``format`` = "microwave-sysex" for .mid/.syx
    banks (key 0 = bank byte, key 1 = program byte, param N at key N+2) vs
    "microwave-usb" for .usb bank records (key = param index directly).
    Sidecars without ``format`` fall back to the bank file's extension in
    the sidecar ``name``; anything unrecognized keeps the direct (usb)
    indexing -- values are never shifted on a guess.
    """
    fmt = sidecar.get("format")
    if fmt == "microwave-sysex":
        return True
    if fmt == "microwave-usb":
        return False
    ext = os.path.splitext(str(sidecar.get("name") or ""))[1].lower()
    return ext in (".mid", ".syx")


def iter_patch_configs(engine: str, sidecar: dict,
                       vocab: Optional[Dict[str, str]]
                       ) -> Iterator[Tuple[str, Dict[str, object]]]:
    """Yield (patch_name, raw param dict) per patch found in a sidecar.

    Bank sidecars yield one entry PER PATCH (the je8086/xenia trap: a
    256-patch bank is 256 data points, not one).
    """
    if engine == "virus":
        # Feature-presence config (constant across the current corpus).
        features = [f for f in VIRUS_MATRIX_FEATURES
                    if f in (sidecar.get("unmapped") or [])]
        yield str(sidecar.get("name") or "unnamed"), {f: 1 for f in features}
        return

    if engine == "je8086":
        mp = sidecar.get("mappedParams")
        if isinstance(mp, dict):
            slots = []
            for key, entry in mp.items():
                if isinstance(entry, dict) and isinstance(entry.get("params"), dict):
                    slots.append((str(entry.get("name") or key), entry["params"]))
            if slots:
                for name, params in slots:
                    yield name, params
                return
            # per-patch exploded sidecar: flat name-keyed mappedParams
            yield str(sidecar.get("name") or "unnamed"), mp
            return
        params = sidecar.get("params")
        if isinstance(params, dict):
            yield str(sidecar.get("name") or "unnamed"), params
        return

    if engine == "xenia":
        params = sidecar.get("params")
        if not isinstance(params, dict):
            return
        table = vocab or {}
        shifted = _xenia_sidecar_is_mid(sidecar)
        for patch_name, values in params.items():
            if not isinstance(values, dict):
                continue
            named: Dict[str, object] = {}
            for idx, raw in values.items():
                try:
                    k = int(idx)
                except (TypeError, ValueError):
                    continue
                if shifted:
                    # .mid key space: 0/1 = bank/program bytes, param N at
                    # key N+2, name chars at 242+ -> vocab index k - 2.
                    if not 2 <= k <= IDX_XENIA_PARAM_LAST + 2:
                        continue
                    k -= 2
                elif k > IDX_XENIA_PARAM_LAST:
                    continue            # params are 0..239; 240+ = name bytes
                name = table.get(str(k))
                if name is not None:
                    named[name] = raw
            yield str(patch_name), named
        return

    if engine == "nodalred2x":
        yield str(sidecar.get("name") or "unnamed"), _flatten_nl2x(sidecar)
        return

    if engine == "vavra":
        params = sidecar.get("params")
        if isinstance(params, dict):
            yield str(sidecar.get("name") or "unnamed"), params
        return

    # Generic fallback: flat mappedParams / params.
    for key in ("mappedParams", "params"):
        raw = sidecar.get(key)
        if isinstance(raw, dict):
            yield str(sidecar.get("name") or "unnamed"), raw
            return


def _project_tuple(engine: str, raw_params: Dict[str, object]
                   ) -> Optional[Dict[str, object]]:
    """Reduce a patch's params to the engine tuple, quantized.

    Tuple params the patch does not carry are recorded as None so cluster
    keys compare full tuples.  Returns None when the patch carries no
    tuple member at all.
    """
    tuple_params = TUPLE_PARAMS.get(engine)
    config: Dict[str, object] = {}
    if tuple_params is None:
        # offset-keyed (vavra) or presence (virus): keep verbatim
        for name, value in raw_params.items():
            config[str(name)] = _quant(value)
        return config or None
    lowered = {str(k).lower(): v for k, v in raw_params.items()}
    for name in tuple_params:
        value = lowered.get(name.lower())
        config[name] = _quant(value) if value is not None else None
    return config


def _apply_offset_map(engine: str, config: Optional[Dict[str, object]],
                      offset_map: Optional[Dict[str, str]]
                      ) -> Optional[Dict[str, object]]:
    """Rename dump-offset keys via the verified map (vavra only).

    Mapped offsets take their canonical primary name; unmapped offsets stay
    raw as 'off_<offset>' -- names are never invented.
    """
    if engine != "vavra" or not offset_map or not config:
        return config
    named: Dict[str, object] = {}
    for key, value in config.items():
        name = offset_map.get(key)
        named[name if name is not None else "off_%s" % key] = value
    return named


# ---------------------------------------------------------------------------
# Naming: deterministic role + character fragments per engine.
# ---------------------------------------------------------------------------

def _bucket(value: object, low: float, high: float) -> Optional[str]:
    if value is None:
        return None
    try:
        v = float(value)
    except (TypeError, ValueError):
        return None
    if v < low:
        return "low"
    if v > high:
        return "high"
    return "mid"


def describe_je8086(cfg: Dict[str, object]) -> Tuple[str, str]:
    frags: List[str] = []
    fx = cfg.get("MultiEffectsType")
    if fx not in (None, 0):
        frags.append("fx mode %s" % fx)
    delay = cfg.get("DelayType")
    if delay not in (None, 0):
        frags.append("delay type %s" % delay)
    if cfg.get("RingModulatorSwitch") == 1:
        frags.append("ring mod")
    cross = _bucket(cfg.get("CrossModulationDepth"), 8, 80)
    if cross is not None and cross != "low":
        frags.append("%s cross-mod" % ("heavy" if cross == "high" else "soft"))
    lfo_depths = [abs(int(cfg.get(k) or 64) - 64) for k in
                  ("AmpLfo1Depth", "AmpLfo2Depth", "FilterLfo1Depth",
                   "FilterLfo2Depth", "PitchLfo2Depth", "OscLfo1Depth")
                  if cfg.get(k) is not None]
    max_lfo = max(lfo_depths) if lfo_depths else 0
    if max_lfo > 40:
        frags.append("deep LFO depths")
    elif max_lfo > 10:
        frags.append("gentle LFO depths")
    fenv = _bucket(cfg.get("FilterEnvelopeDepth"), 40, 100)
    if fenv == "low":
        frags.append("flat filter env")
    elif fenv == "high":
        frags.append("wide-open filter env")
    cutoff = _bucket(cfg.get("CutoffFrequency"), 40, 100)
    if cutoff == "low":
        frags.append("dark")
    elif cutoff == "high":
        frags.append("bright")
    if cfg.get("Lfo1AndEnvelopeDestination") not in (None, 0):
        frags.append("routed LFO/env destination")
    if cfg.get("AutoPanManualPanSwitch") == 1:
        frags.append("auto-pan")
    role = ("fx" if any(f.startswith(("fx mode", "delay type")) for f in frags)
            else "mod" if any(("LFO" in f or "cross-mod" in f or "ring" in f)
                              for f in frags)
            else "env" if any("env" in f for f in frags)
            else "baseline")
    return role, (" + ".join(frags) if frags else "baseline patch config")


def describe_nodalred2x(cfg: Dict[str, object]) -> Tuple[str, str]:
    frags: List[str] = []
    if (cfg.get("lfo1_level") or 0) not in (None, 0):
        frags.append("LFO1 moving")
    if (cfg.get("lfo2_rate") or 0) not in (None, 0):
        frags.append("LFO2")
    fenv = _bucket(cfg.get("filter_env_amount"), 30, 90)
    if fenv == "high":
        frags.append("big filter env")
    elif fenv == "low":
        frags.append("closed filter env")
    if (cfg.get("mod_env_level") or 0) not in (None, 0):
        frags.append("mod env routed")
    if (cfg.get("fm_depth") or 0) not in (None, 0):
        frags.append("FM")
    if (cfg.get("sync_distortion") or 0) not in (None, 0):
        frags.append("sync/distortion")
    if (cfg.get("lfo1_dest") or 0) not in (None, 0):
        frags.append("LFO1 routed")
    role = ("lfo" if any("LFO" in f for f in frags)
            else "mod" if any(("mod env" in f or f == "FM") for f in frags)
            else "env" if any("filter env" in f for f in frags)
            else "baseline")
    return role, (" + ".join(frags) if frags else "baseline patch config")


def describe_xenia(cfg: Dict[str, object]) -> Tuple[str, str]:
    frags: List[str] = []
    slots = sum(1 for n in range(1, 17)
                if (cfg.get("Slot%dAmount" % n) or 0) not in (None, 0))
    mods = sum(1 for n in range(1, 5)
               if (cfg.get("Mod%dSource1" % n) or 0) not in (None, 0))
    if slots:
        frags.append("%d-slot matrix" % slots)
    if mods:
        frags.append("%d mod-router(s)" % mods)
    fx = cfg.get("EffectType")
    if fx not in (None, 0):
        frags.append("fx mode %s" % fx)
    if cfg.get("ChorusEnabled") == 1:
        frags.append("chorus on")
    ring = _bucket(cfg.get("MixRingMod"), 4, 60)
    if ring == "high":
        frags.append("loud ring mod")
    elif ring == "mid":
        frags.append("ring mod mix")
    fenv = _bucket(cfg.get("F1EnvAmount"), 20, 80)
    if fenv == "high":
        frags.append("big filter env")
    elif fenv == "low":
        frags.append("closed filter env")
    if (cfg.get("Lfo1Delay") or 0) not in (None, 0):
        frags.append("delayed LFO1")
    if (cfg.get("W1EnvAmount") or 0) not in (None, 0):
        frags.append("wave env")
    role = ("mod-matrix" if slots or mods
            else "fx" if any(f.startswith(("fx mode", "chorus", "ring"))
                             for f in frags)
            else "env" if any("env" in f for f in frags)
            else "baseline")
    return role, (" + ".join(frags) if frags else "baseline patch config")


# FX-family words recognized inside canonical FX sub-param NAMES (from the
# verified offset map -- fragments of real parameter names, NOT the
# unverified FXType value enum).
_VAVRA_FX_FAMILY_TOKENS = ("Chorus", "Flanger", "Phaser", "Vocoder",
                           "Overdrive", "Reverb", "RingMod", "Delay",
                           "Autopan")
_VAVRA_FX_FAMILY_DISPLAY = {"RingMod": "ring mod", "Autopan": "auto-pan"}


def _vavra_fx_family(key: str) -> Optional[str]:
    for tok in _VAVRA_FX_FAMILY_TOKENS:
        if tok in key:
            return _VAVRA_FX_FAMILY_DISPLAY.get(tok, tok.lower())
    return None


def _hot(value: object, threshold: float) -> bool:
    if value is None:
        return False
    try:
        return float(value) >= threshold
    except (TypeError, ValueError):
        return False


def describe_vavra(cfg: Dict[str, object]) -> Tuple[str, str]:
    """Name a vavra config from its (optionally mapped) params.

    Without an offset map (bare digit keys) the honest legacy wording is
    kept.  With a map, fragments cite the real named params; FX family
    words come from the canonical sub-param names, never from the
    unverified FXType value enum.
    """
    named = {k: v for k, v in cfg.items() if not k.isdigit()}
    if not named:
        nonzero = sum(1 for v in cfg.values() if v not in (0, None))
        return "raw-config", "raw dump config (%d nonzero offsets)" % nonzero
    mapped = {k: v for k, v in named.items() if not k.startswith("off_")}
    if not mapped:
        return ("raw-config",
                "raw dump config (%d unnamed offsets)" % len(named))
    frags: List[str] = []
    for num, prefixes in (("1", ("Fx1", "FiveFX1")),
                          ("2", ("Fx2", "FiveFX2"))):
        fams: List[str] = []
        for key in sorted(mapped):
            if key.startswith(prefixes) and not key.endswith("Polarity"):
                fam = _vavra_fx_family(key)
                if fam and _hot(mapped[key], 16) and fam not in fams:
                    fams.append(fam)
        if fams:
            # cap to 3 for human-usable labels; params carry the full tuple
            frags.append("FX%s %s movement" % (num, "+".join(fams[:3])))
    for osc in ("O1", "O2", "O3"):
        if _hot(mapped.get(osc + "FmAmount"), 4):
            frags.append("%s FM" % osc)
        if _hot(mapped.get(osc + "Pwm"), 4):
            frags.append("%s PWM" % osc)
    if _hot(mapped.get("PitchModAmount"), 4):
        frags.append("pitch mod")
    if _hot(mapped.get("RingModLevel"), 4):
        frags.append("ring mod")
    for f in ("F1", "F2"):
        cut = _bucket(mapped.get(f + "Cutoff"), 40, 100)
        if cut == "low":
            frags.append("%s filter closed" % f)
        elif cut == "high":
            frags.append("%s filter open" % f)
        if _hot(mapped.get(f + "Resonance"), 100):
            frags.append("%s resonance hot" % f)
        if _hot(mapped.get(f + "Drive"), 100):
            frags.append("%s drive" % f)
        if _hot(mapped.get(f + "FmAmount"), 4):
            frags.append("%s FM" % f)
        if _hot(mapped.get(f + "CutoffMod"), 4):
            frags.append("%s cutoff mod" % f)
    role = ("fx" if any(fr.startswith("FX") for fr in frags)
            else "mod" if any("mod" in fr or fr.endswith(("FM", "PWM"))
                              for fr in frags)
            else "filter" if any("filter" in fr for fr in frags)
            else "baseline")
    return role, (" + ".join(frags[:5]) if frags else "named matrix config")


def describe_virus(cfg: Dict[str, object]) -> Tuple[str, str]:
    feats = sorted(k for k, v in cfg.items() if v)
    label = {"fx_delay": "delay FX", "fx_chorus": "chorus FX",
             "fx_reverb": "reverb FX", "mod_matrix": "mod matrix",
             "lfo1": "LFO1", "lfo2": "LFO2", "ring_mod": "ring mod",
             "osc2_fm_amount": "osc2 FM"}
    named = " + ".join(label.get(f, f) for f in feats)
    return "feature-presence", named or "no matrix features flagged"


_DESCRIBERS = {
    "je8086": describe_je8086,
    "nodalred2x": describe_nodalred2x,
    "xenia": describe_xenia,
    "vavra": describe_vavra,
    "virus": describe_virus,
}


def preset_id(engine: str, role: str, params: Dict[str, object]) -> str:
    payload = json.dumps({"engine": engine, "role": role, "params": params},
                         sort_keys=True, separators=(",", ":"))
    return hashlib.sha1(payload.encode("utf-8")).hexdigest()[:16]


def harvest_engine(engine: str, roots: Sequence[str],
                   vocab: Optional[Dict[str, str]],
                   max_presets: int = 40,
                   offset_map: Optional[Dict[str, str]] = None,
                   offset_map_source: Optional[str] = None) -> dict:
    configs: Dict[Tuple[Tuple[str, object], ...], List[str]] = {}
    scanned = 0
    seen: set = set()
    for sidecar_engine, path in _sidecar_files(roots):
        if sidecar_engine != engine:
            continue
        sidecar = _load_json(path)
        if sidecar is None:
            continue
        scanned += 1
        for patch_name, raw_params in iter_patch_configs(engine, sidecar, vocab):
            config = _apply_offset_map(engine,
                                       _project_tuple(engine, raw_params),
                                       offset_map)
            if config is None:
                continue
            key = tuple(sorted(config.items()))
            dedup = (patch_name, key)
            if dedup in seen:
                continue
            seen.add(dedup)
            configs.setdefault(key, []).append(patch_name)

    ordered = sorted(configs.items(),
                     key=lambda kv: (-len(kv[1]),
                                     preset_id(engine, "", dict(kv[0]))))
    presets: List[dict] = []
    used_names: Dict[str, int] = collections.OrderedDict()
    for key, examples in ordered[:max_presets]:
        params = dict(key)
        role, name = _DESCRIBERS[engine](params)
        base = name
        n = used_names.get(base, 0)
        used_names[base] = n + 1
        if n:
            name = "%s (%d)" % (base, n + 1)
        presets.append({
            "id": preset_id(engine, role, params),
            "name": name,
            "role": role,
            "params": params,
            "appliesVia": (APPLIES_VIA_OFFSET_MAPPED.get(engine,
                                                          APPLIES_VIA[engine])
                           if offset_map else APPLIES_VIA[engine]),
            "examples": sorted(examples)[:5],
            "evidence": "%d patches carry this config" % len(examples),
        })

    sheet = {
        "schema": SCHEMA,
        "engine": engine,
        "unverified": True,
        "sourceRoots": sorted({os.path.basename(os.path.normpath(r))
                               for r in roots}),
        "scannedSidecars": scanned,
        "patchCount": len(seen),
        "presets": presets,
    }
    if engine in APPLIES_VIA_OFFSET_MAPPED and offset_map is not None:
        sheet["offsetMap"] = offset_map_source or "unknown"
    if len(presets) < VOCAB_SHORTFALL_MIN_PRESETS:
        sheet["presetsShortfall"] = (
            "%d preset(s) < %d: the %s sidecar corpus carries no per-patch "
            "FX/matrix parameter values to cluster (see the module docstring); "
            "deferred until a richer sidecar pass or dump decoder exists."
            % (len(presets), VOCAB_SHORTFALL_MIN_PRESETS, engine))
    return sheet


def harvest(roots_by_engine: Dict[str, Sequence[str]],
            vocabs: Dict[str, Dict[str, str]],
            max_presets: int = 40,
            offset_maps: Optional[Dict[str, Dict[str, str]]] = None,
            offset_map_sources: Optional[Dict[str, str]] = None
            ) -> Dict[str, dict]:
    offset_maps = offset_maps or {}
    offset_map_sources = offset_map_sources or {}
    return {engine: harvest_engine(engine, roots, vocabs.get(engine),
                                   max_presets,
                                   offset_map=offset_maps.get(engine),
                                   offset_map_source=offset_map_sources.get(engine))
            for engine, roots in sorted(roots_by_engine.items())}


def _resolve_default_roots() -> Dict[str, List[str]]:
    resolved: Dict[str, List[str]] = {}
    for engine, candidates in DEFAULT_ROOTS.items():
        present = [c for c in candidates if os.path.isdir(c)]
        if present:
            resolved[engine] = present[:1]
    return resolved


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description="Harvest per-engine matrix presets from patch sidecars")
    ap.add_argument("roots", nargs="*",
                    help="library roots (default: the known engine roots)")
    ap.add_argument("--out-dir", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "matrix_presets"),
        metavar="DIR", help="output directory for <engine>.json")
    ap.add_argument("--max-presets", type=int, default=40,
                    help="clusters to emit per engine (default 40)")
    ap.add_argument("--vocab", action="append", default=[], metavar="ENGINE=PATH",
                    help="index->name vocabulary override, e.g. "
                         "xenia=<parameterDescriptions_xt.json>")
    ap.add_argument("--offset-map", action="append", default=[],
                    metavar="ENGINE=PATH",
                    help="dump-offset -> name map override, e.g. "
                         "vavra=vavra_offset_map.json (auto-detected "
                         "for vavra)")
    ap.add_argument("--engine", action="append", dest="engines", default=[],
                    help="restrict to this engine (repeatable)")
    args = ap.parse_args(argv)

    roots_by_engine: Dict[str, List[str]] = {}
    if args.roots:
        # route each root by the sidecar suffixes found under it
        for root in args.roots:
            if not os.path.isdir(root):
                print("not a directory: %s" % root, file=sys.stderr)
                return 2
            engines = {engine for engine, _ in _sidecar_files([root])}
            if not engines:
                print("no recognisable sidecars under: %s" % root,
                      file=sys.stderr)
                return 2
            for engine in engines:
                roots_by_engine.setdefault(engine, []).append(root)
    else:
        roots_by_engine = _resolve_default_roots()
        if not roots_by_engine:
            print("no default library roots present on this machine",
                  file=sys.stderr)
            return 2
    if args.engines:
        wanted = set(args.engines)
        roots_by_engine = {e: r for e, r in roots_by_engine.items()
                           if e in wanted}

    vocabs: Dict[str, Dict[str, str]] = {}
    for engine in roots_by_engine:
        path = find_vocabulary(engine)
        if path:
            vocabs[engine] = load_vocabulary(path)
    for spec in args.vocab:
        if "=" not in spec:
            print("--vocab expects ENGINE=PATH", file=sys.stderr)
            return 2
        engine, path = spec.split("=", 1)
        if not os.path.isfile(path):
            print("vocabulary file not found: %s" % path, file=sys.stderr)
            return 2
        vocabs[engine] = load_vocabulary(path)

    offset_maps: Dict[str, Dict[str, str]] = {}
    offset_map_sources: Dict[str, str] = {}
    for spec in args.offset_map:
        if "=" not in spec:
            print("--offset-map expects ENGINE=PATH", file=sys.stderr)
            return 2
        engine, path = spec.split("=", 1)
        if engine not in OFFSET_MAP_CANDIDATES:
            print("--offset-map not defined for engine %r (supported: %s)"
                  % (engine, ", ".join(sorted(OFFSET_MAP_CANDIDATES))),
                  file=sys.stderr)
            return 2
        if not os.path.isfile(path):
            print("offset-map file not found: %s" % path, file=sys.stderr)
            return 2
        try:
            offset_maps[engine] = load_offset_map(path)
        except (OSError, ValueError) as exc:
            print("bad offset map %s: %s" % (path, exc), file=sys.stderr)
            return 2
        offset_map_sources[engine] = os.path.basename(path)
    for engine in roots_by_engine:
        if engine in offset_maps:
            continue
        auto = find_offset_map(engine)
        if auto is None:
            continue
        try:
            offset_maps[engine] = load_offset_map(auto)
        except (OSError, ValueError) as exc:
            print("bad offset map %s: %s" % (auto, exc), file=sys.stderr)
            return 2
        offset_map_sources[engine] = os.path.basename(auto)

    sheets = harvest(roots_by_engine, vocabs, args.max_presets,
                     offset_maps, offset_map_sources)
    os.makedirs(args.out_dir, exist_ok=True)
    for engine, sheet in sorted(sheets.items()):
        out_path = os.path.join(args.out_dir, "%s.json" % engine)
        with open(out_path, "w", encoding="utf-8", newline=chr(10)) as fh:
            fh.write(json.dumps(sheet, indent=1, sort_keys=True) + chr(10))
        print("%-11s sidecars=%-5d patches=%-5d presets=%-3d -> %s"
              % (engine, sheet["scannedSidecars"], sheet["patchCount"],
                 len(sheet["presets"]), out_path))
        if sheet.get("presetsShortfall"):
            print("             %s" % sheet["presetsShortfall"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
