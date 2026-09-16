#!/usr/bin/env python3
"""Roland JP-8080 (HDAW "JE8086" gearmulator CLAP) patch decoder + sidecar/survey pipeline.

Turns the JP-8080 bank library (raw .syx SysEx streams and SMF-wrapped .mid
banks) into per-patch records with NAMED parameters, writes
'<bank>.je8086.json' sidecars (schema hdaw.je8086.bank.v1, engine je8086) next
to each bank so FileLibraryManager / search_library can index them, and emits a
library survey for the Sound Selector role.

Wire format (verified against gearmulator 2.2.9 sources -- jeLib/jemiditypes.h
holds the message constants and 'enum class Patch' parameter map -- plus all 46
real bank files in D:/pdf/je8086 on 2026-09-16):

* Container: a raw SysEx stream (.syx), or a Standard MIDI File (.mid/.MID)
  whose F0 events wrap the same messages.  SMF events carry a variable-length
  length prefix after F0 (high-bit continuation bytes, e.g. 0x81 0x7D) that
  must be stripped before parsing.
* Message -- Roland DT1 "data set"::

      F0 41 10 00 06 12 <a0 a1 a2> <data...> <checksum> F7
          |  |  |  |     |  |         |
          |  |  |  |     |  +-- 3 x 7-bit address bytes
          |  |  |  |     +----- 0x12 = DT1
          |  |  |  +---------- model 0x0006 = JP-8080
          |  |  +------------- device id 0x10
          |  +---------------- Roland
          +------------------- SysEx

  Checksum (all 6144 messages in the real library validate):
  (128 - (sum(addr) + sum(data)) % 128) % 128.
  A zero byte is checksum-invisible, so the leading-byte question below cannot
  be settled by the checksum alone.
* Address unit = 256 bytes (one "page").  A patch occupies 0x200 bytes = 2
  pages, so a 64-patch bank spans 128 pages::

      patch area        (a0 = 2): base 0x020000, bank stride 128 pages
                                  bank = (v - base) // 128
                                  slot = ((v - base) % 128) // 2 + 1
      performance area  (a0 = 3): base 0x030000, performance stride 128 pages
                                  off  = v - base
                                  off < 64 -> performance common (its name)
                                  else patch = (off - 64) // 2 + 1
                                  (PatchUpper = 0x4000 B = page 64,
                                   PatchLower = 0x4200 B = page 66)
* Patch body offset: the dump carries ONE leading byte before the documented
  patch parameter block, i.e. Patch.<Param> = 0xNN lives at data[1 + 0xNN] and
  the 16-char patch name is data[1:17].  Validated statistically over 3893
  name-bearing page-0 messages: the documented ranges of nine tight-range
  parameters hold for 99.9% of patches at data[off+1] versus 0.0% at
  data[off] and data[off-1].

Usage::

    py -3 timbre-lib/je8086_patch.py --dump D:/pdf/je8086/Psytrance.syx
    py -3 timbre-lib/je8086_patch.py --survey D:/pdf/je8086 --out je8086_survey.json
    py -3 timbre-lib/je8086_patch.py --sidecars D:/pdf/je8086
    py -3 timbre-lib/je8086_patch.py --sidecars D:/pdf/je8086 --role bass --explode
    py -3 timbre-lib/je8086_patch.py --sidecars D:/pdf/je8086 --with-bytes

The default sidecar is a compact metadata index (bank/role/params/labels/paramDefs);
--with-bytes additionally embeds each patch's raw DT1 payload, and --explode writes
a per-patch .syx + sidecar under <DIR>/exploded/<bank>/ (byte-identical DT1s).

No third-party dependencies.  Never writes outside the paths it is given.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

SCHEMA_BANK = "hdaw.je8086.bank.v1"
SCHEMA_PATCH = "hdaw.je8086.patch.v1"
SCHEMA_SURVEY = "hdaw.je8086.survey.v1"
ENGINE = "je8086"

ROLAND_ID = 0x41
JP8080_MODEL = 0x0006
DT1 = 0x12
F0 = bytes([0xF0])
F7 = bytes([0xF7])
PAGE = 256
PATCH_STRIDE_PAGES = 2
# page_value() packs the three 7-bit address bytes, so the area bases are the
# PACKED values: address bytes (02 00 00) -> 2 << 14 = 32768, (03 00 00) -> 49152.
PATCH_AREA_BASE = 2 << 14     # address bytes 02 00 00
PERF_AREA_BASE = 3 << 14      # address bytes 03 00 00
AREA_PAGES = 128
PERF_COMMON_PAGES = 64
NAME_OFFSET = 1          # dump carries one leading byte before the patch body
NAME_LEN = 16
SYSEX_SUFFIX = ".je8086.json"
SYSEX_HEADER = bytes([0xF0, ROLAND_ID, 0x10, 0x00, 0x06, 0x12])

# (offset, name, lo, hi, kind, labels, note); kind: enum | lin | bipolar64 | bipolar127
PARAMS: List[Tuple[int, str, int, int, str, Optional[Sequence[Optional[str]]], str]] = [
    (0x10, "Lfo1Waveform", 0, 3, "enum", ("TRI", "SAW", "SQR", "S/H"), ""),
    (0x11, "Lfo1Rate", 0, 127, "lin", None, "0-127"),
    (0x12, "Lfo1Fade", 0, 127, "lin", None, "0-127"),
    (0x13, "Lfo2Rate", 0, 127, "lin", None, "0-127"),
    (0x14, "Lfo2DepthSelect", 0, 2, "enum", ("PITCH", "FILTER", "AMPLIFIER"), ""),
    (0x15, "RingModulatorSwitch", 0, 1, "enum", ("OFF", "ON"), ""),
    (0x16, "CrossModulationDepth", 0, 127, "lin", None, ""),
    (0x17, "OscillatorBalance", 0, 127, "bipolar64", None, "-64 (OSC1) .. +63 (OSC2)"),
    (0x18, "Lfo1AndEnvelopeDestination", 0, 2, "enum", ("OSC1+2", "OSC2", "X-MOD DEPTH"), ""),
    (0x19, "OscLfo1Depth", 0, 127, "bipolar64", None, ""),
    (0x1A, "PitchLfo2Depth", 0, 127, "bipolar64", None, ""),
    (0x1B, "PitchEnvelopeDepth", 0, 127, "bipolar64", None, ""),
    (0x1C, "PitchEnvelopeAttackTime", 0, 127, "lin", None, ""),
    (0x1D, "PitchEnvelopeDecayTime", 0, 127, "lin", None, ""),
    (0x1E, "Osc1Waveform", 0, 6, "enum",
     ("SUPER SAW", "TWM", "FEEDBACK", "SAW2", "PULSE", "SAW", "TRI"),
     "0 = SUPER SAW (hoover/supersaw family)"),
    (0x1F, "Osc1Control1", 0, 127, "lin", None, "SUPER SAW: mix"),
    (0x20, "Osc1Control2", 0, 127, "lin", None, "SUPER SAW: detune"),
    (0x21, "Osc2Waveform", 0, 3, "enum", ("PULSE", "TRI", "SAW", "NOISE"), "3 = NOISE (fx/riser)"),
    (0x22, "Osc2SyncSwitch", 0, 1, "enum", ("OFF", "ON"), ""),
    (0x23, "Osc2Range", 0, 0x32, "lin", None, "0 = -WIDE, 0x19 = 0, 0x32 = +WIDE"),
    (0x24, "Osc2FineWide", 0, 0x64, "lin", None, "0 = -50 .. 100 = +50 cent"),
    (0x25, "Osc2Control1", 0, 127, "lin", None, ""),
    (0x26, "Osc2Control2", 0, 127, "lin", None, ""),
    (0x27, "FilterType", 0, 2, "enum", ("HPF", "BPF", "LPF"), ""),
    (0x28, "CutoffSlope", 0, 1, "enum", ("-12 dB/oct", "-24 dB/oct"), ""),
    (0x29, "CutoffFrequency", 0, 127, "lin", None, "0-127"),
    (0x2A, "Resonance", 0, 127, "lin", None, "0-127"),
    (0x2B, "CutoffFrequencyKeyFollow", 0, 127, "bipolar64", None, ""),
    (0x2C, "FilterLfo1Depth", 0, 127, "bipolar64", None, ""),
    (0x2D, "FilterLfo2Depth", 0, 127, "bipolar64", None, ""),
    (0x2E, "FilterEnvelopeDepth", 0, 127, "bipolar64", None, ""),
    (0x2F, "FilterEnvelopeAttackTime", 0, 127, "lin", None, ""),
    (0x30, "FilterEnvelopeDecayTime", 0, 127, "lin", None, ""),
    (0x31, "FilterEnvelopeSustainLevel", 0, 127, "lin", None, ""),
    (0x32, "FilterEnvelopeReleaseTime", 0, 127, "lin", None, ""),
    (0x33, "AmpLevel", 0, 127, "lin", None, ""),
    (0x34, "AmpLfo1Depth", 0, 127, "bipolar64", None, ""),
    (0x35, "AmpLfo2Depth", 0, 127, "bipolar64", None, ""),
    (0x36, "AmpEnvelopeAttackTime", 0, 127, "lin", None, ""),
    (0x37, "AmpEnvelopeDecayTime", 0, 127, "lin", None, ""),
    (0x38, "AmpEnvelopeSustainLevel", 0, 127, "lin", None, ""),
    (0x39, "AmpEnvelopeReleaseTime", 0, 127, "lin", None, ""),
    (0x3A, "AutoPanManualPanSwitch", 0, 2, "enum", ("OFF", "AUTO PAN", "MANUAL PAN"), ""),
    (0x3B, "ToneControlBass", 0, 127, "bipolar64", None, ""),
    (0x3C, "ToneControlTreble", 0, 127, "bipolar64", None, ""),
    (0x3D, "MultiEffectsType", 0, 0x0C, "enum",
     ("SUPER CHORUS SLW", None, None, None, None, None, None, None, None, None, None, None, "DISTORTION"), ""),
    (0x3E, "MultiEffectsLevel", 0, 127, "lin", None, ""),
    (0x3F, "DelayType", 0, 4, "enum", ("PANNING L-R", None, None, None, "MONO LONG"), ""),
    (0x40, "DelayTime", 0, 127, "lin", None, ""),
    (0x41, "DelayFeedback", 0, 127, "lin", None, ""),
    (0x42, "DelayLevel", 0, 127, "lin", None, ""),
    (0x43, "BendRangeUp", 0, 0x18, "lin", None, "0-24 semitone"),
    (0x44, "BendRangeDown", 0, 0x18, "lin", None, "0-24 semitone"),
    (0x45, "PortamentoSwitch", 0, 1, "enum", ("OFF", "ON"), ""),
    (0x46, "PortamentoTime", 0, 127, "lin", None, ""),
    (0x47, "MonoSwitch", 0, 1, "enum", ("OFF", "ON"), "mono => bass/lead"),
    (0x48, "LegatoSwitch", 0, 1, "enum", ("OFF", "ON"), ""),
    (0x49, "OscillatorShift", 0, 4, "lin", None, "-2 .. +2 octave"),
    (0x4A, "ControlLfo1Rate", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x4E, "ControlLfo2Rate", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x50, "ControlCrossModulationDepth", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x52, "ControlOscillatorBalance", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x62, "ControlOsc2Range", 0x4D, 0xB1, "bipolar127", None, "CC control depth"),
    (0x64, "ControlOsc2FineWide", 0x1B, 0xE3, "bipolar127", None, "CC control depth"),
    (0x6A, "ControlCutoffFrequency", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x6C, "ControlResonance", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x74, "ControlFilterEnvDepth", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x7E, "ControlAmpLevel", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x100, "ControlAmpLfo1Depth", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x104, "ControlAmpEnvAttackTime", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x10A, "ControlAmpEnvReleaseTime", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x10C, "ControlToneControlBass", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x10E, "ControlToneControlTreble", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x110, "ControlMultiEffectsLevel", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x112, "ControlDelayTime", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x114, "ControlDelayFeedback", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x116, "ControlDelayLevel", 0, 0xFE, "bipolar127", None, "CC control depth"),
    (0x118, "MorphBendAssign", 0, 1, "enum", ("OFF", "ON"), ""),
]
PARAM_BY_OFFSET = {p[0]: p for p in PARAMS}

# Emitted ONCE per sidecar instead of repeating offset/range/note per patch.
PARAM_DEFS = {p[1]: {"offset": "0x%X" % p[0], "lo": p[2], "hi": p[3], "kind": p[4], "note": p[6]}
              for p in PARAMS}


def compact_params(params: Dict[str, dict]) -> Dict[str, int]:
    """Rich decoded params -> {name: raw} (the replay/compare form)."""
    return {k: int(v["raw"]) for k, v in params.items()}


def compact_labels(params: Dict[str, dict]) -> Dict[str, str]:
    """Enum labels only (numeric params carry no label)."""
    return {k: v["label"] for k, v in params.items() if isinstance(v.get("label"), str)}

UNMAPPED_BANK_FEATURES = [
    "velocity/morph block 0x119-0x16C not decoded (page 2 of a 2-page patch)",
    "performance common block (split/layer/key-range) not decoded",
]

ROLE_KEYWORDS: List[Tuple[str, Tuple[str, ...]]] = [
    ("bass", ("bass", "sub", "roll", "acid", "punch", "driv", "reese")),
    ("lead", ("lead", "solo", "hoover", "saw", "unison", "main", "anthem", "melody", "scream")),
    ("pad", ("pad", "string", "atmo", "choir", "warm", "wash", "ambient", "texture")),
    ("pluck", ("pluck", "stab", "short", "blip", "ping", "spike", "click", "stac")),
    ("fx", ("fx", "noise", "sweep", "riser", "siren", "zap", "alien", "uplift", "impact", "downer")),
    ("arp", ("arp", "seq", "gate", "trance")),
    ("chord", ("chord", "major", "minor", "stack")),
]


def _norm(kind: str, raw: int, lo: int, hi: int) -> float:
    span = max(hi - lo, 1)
    return round((raw - lo) / span, 4)


def _signed(kind: str, raw: int) -> Optional[int]:
    if kind == "bipolar64":
        return raw - 64
    if kind == "bipolar127":
        return raw - 127
    return None


# --------------------------------------------------------------------------- #
# Container parsing
# --------------------------------------------------------------------------- #
def iter_sysex_blocks(data: bytes) -> Iterable[bytes]:
    """Yield SysEx payloads (without F0/F7) from a raw SysEx stream or an SMF."""
    i = 0
    while True:
        start = data.find(F0, i)
        if start < 0:
            return
        end = data.find(F7, start)
        if end < 0:
            return
        body = data[start + 1:end]
        i = end + 1
        # SMF F0 events carry a variable-length length prefix (high-bit
        # continuation bytes, e.g. 0x81 0x7D); raw SysEx streams do not.
        if body and (body[0] & 0x80):
            k = 0
            while k < len(body) and (body[k] & 0x80):
                k += 1
            k += 1
            body = body[k:]
        if body:
            yield body


def page_value(addr: Sequence[int]) -> int:
    """3 x 7-bit address bytes -> integer page value."""
    return (addr[0] << 14) | (addr[1] << 7) | addr[2]


def parse_dt1(body: bytes) -> Optional[dict]:
    """Parse a Roland DT1 message; returns None for non-JP-8080 messages."""
    if len(body) < 9 or body[0] != ROLAND_ID or body[4] != DT1:
        return None
    model = (body[2] << 8) | body[3]
    if model != JP8080_MODEL:
        return None
    addr = tuple(body[5:8])
    payload = body[8:-1]
    checksum = body[-1]
    calc = (128 - ((sum(addr) + sum(payload)) % 128)) % 128
    return {
        "dev": body[1],
        "addr": addr,
        "value": page_value(addr),
        "data": payload,
        "checksum": checksum,
        "checksumOk": calc == checksum,
    }


def parse_file(path: str) -> List[dict]:
    with open(path, "rb") as fh:
        data = fh.read()
    out = []
    for body in iter_sysex_blocks(data):
        msg = parse_dt1(body)
        if msg is None and body and not (body[0] & 0x80):
            # SMF F0 events carry a varint length prefix. Payloads under 128
            # bytes use a ONE-byte prefix (< 0x80), which the high-bit test in
            # iter_sysex_blocks cannot detect, so the body still starts with the
            # length byte. Retrying with one byte stripped recovers those
            # messages -- short performance-common pages and the 7-byte patch
            # tails were silently dropped before 2026-09-16 (found by comparing
            # the C++ loader's unit count against the survey: 320 SMF events in
            # Kulshan Mystical Psytrance.mid vs 128 parsed).
            msg = parse_dt1(body[1:])
        if msg is not None:
            out.append(msg)
    return out


def addr_bytes(value: int) -> Tuple[int, int, int]:
    """Packed page value -> three 7-bit address bytes."""
    return ((value >> 14) & 0x7F, (value >> 7) & 0x7F, value & 0x7F)


def emit_dt1(addr: Sequence[int], data: bytes) -> bytes:
    """Build a Roland DT1 message (F0 .. checksum F7) for the given address."""
    a = list(addr)
    checksum = (128 - ((sum(a) + sum(data)) % 128)) % 128
    return bytes([0xF0, ROLAND_ID, 0x10, 0x00, 0x06, 0x12] + a + list(data) + [checksum, 0xF7])


def unit_page_value(entry: dict) -> int:
    """Page value of an entry's page 0 (inverse of area_of)."""
    if entry["area"] == "patch":
        rel = entry["bank"] * AREA_PAGES + (entry["slot"] - 1) * PATCH_STRIDE_PAGES
        return PATCH_AREA_BASE + rel
    if entry["area"] == "performance-common":
        return PERF_AREA_BASE + (entry["bank"] - 1) * AREA_PAGES
    rel = ((entry["bank"] - 1) * AREA_PAGES + PERF_COMMON_PAGES
           + (entry["slot"] - 1) * PATCH_STRIDE_PAGES)
    return PERF_AREA_BASE + rel


def area_of(value: int) -> Tuple[str, int, int, int]:
    """(area, bank_or_perf, unit, page_in_unit) for a page value."""
    if PATCH_AREA_BASE <= value < PERF_AREA_BASE:
        rel = value - PATCH_AREA_BASE
        return ("patch", rel // AREA_PAGES, (rel % AREA_PAGES) // PATCH_STRIDE_PAGES + 1,
                rel % PATCH_STRIDE_PAGES)
    if value >= PERF_AREA_BASE:
        rel = value - PERF_AREA_BASE
        perf = rel // AREA_PAGES + 1
        off = rel % AREA_PAGES
        if off < PERF_COMMON_PAGES:
            return ("performance-common", perf, 0, off)
        inner = off - PERF_COMMON_PAGES
        return ("performance-patch", perf, inner // PATCH_STRIDE_PAGES + 1,
                inner % PATCH_STRIDE_PAGES)
    return ("other", 0, 0, 0)


def _is_printable_name(raw: bytes) -> bool:
    if len(raw) < NAME_LEN:
        return False
    text = raw[:NAME_LEN].decode("latin-1")
    if not text.strip():
        return False
    # Space and NUL are both used as name padding (real banks pad with spaces,
    # some tools leave NULs); anything else non-printable means "not a name".
    return all(32 <= ord(c) < 127 or c == "\x00" for c in text)


# --------------------------------------------------------------------------- #
# Patch decode
# --------------------------------------------------------------------------- #
def decode_params(blob: bytes) -> Tuple[Dict[str, dict], List[int]]:
    """Decode the documented patch parameters from a reconstructed blob."""
    values: Dict[str, dict] = {}
    for off, name, lo, hi, kind, labels, note in PARAMS:
        idx = NAME_OFFSET + off
        if idx >= len(blob):
            continue
        raw = blob[idx]
        entry: Dict[str, object] = {"raw": raw, "norm": _norm(kind, raw, lo, hi), "offset": "0x%X" % off}
        signed = _signed(kind, raw)
        if signed is not None:
            entry["signed"] = signed
        if kind == "enum":
            label = None
            if labels is not None and raw < len(labels):
                label = labels[raw]
            entry["label"] = label
        if note:
            entry["note"] = note
        values[name] = entry
    known = {NAME_OFFSET + p[0] for p in PARAMS}
    unknown = [i for i in range(NAME_OFFSET, len(blob)) if i not in known]
    return values, unknown


def classify_role(name: str, params: Dict[str, dict]) -> Tuple[str, float, List[str], str]:
    """Keyword + parameter evidence -> (role, confidence, evidence, family)."""
    scores: Dict[str, float] = {r: 0.0 for r, _ in ROLE_KEYWORDS}
    evidence: List[str] = []
    low = name.lower()
    for role, keys in ROLE_KEYWORDS:
        for key in keys:
            if key in low:
                scores[role] += 1.0
                evidence.append("name:" + key)
                break

    def raw(param: str, default: Optional[int] = None) -> Optional[int]:
        v = params.get(param)
        return default if v is None else int(v["raw"])

    def label(param: str) -> Optional[str]:
        v = params.get(param)
        if v is None:
            return None
        lab = v.get("label")
        return lab if isinstance(lab, str) else None

    osc1_wave = raw("Osc1Waveform")
    osc2_wave = raw("Osc2Waveform")
    family = label("Osc1Waveform") or ("wave%s" % osc1_wave if osc1_wave is not None else "unknown")
    if osc2_wave == 3:
        scores["fx"] += 2.0
        evidence.append("osc2:NOISE")
        family = "noise"
    if label("MultiEffectsType") == "DISTORTION":
        scores["bass"] += 1.0
        scores["lead"] += 1.0
        evidence.append("fx:DISTORTION")
    if label("Osc2SyncSwitch") == "ON":
        scores["lead"] += 0.8
        evidence.append("osc2:SYNC")
    if label("MonoSwitch") == "ON":
        scores["bass"] += 0.8
        scores["lead"] += 0.3
        evidence.append("mono:ON")
    if label("PortamentoSwitch") == "ON":
        scores["lead"] += 0.6
        scores["bass"] += 0.3
        evidence.append("portamento:ON")
    if osc1_wave == 0:
        detune = raw("Osc1Control2") or 0
        scores["lead"] += 1.0 if detune >= 40 else 0.6
        scores["pad"] += 0.5
        evidence.append("osc1:SUPER SAW detune=%d" % detune)
    attack = raw("AmpEnvelopeAttackTime")
    release = raw("AmpEnvelopeReleaseTime")
    if attack is not None:
        if attack <= 8 and (release or 0) <= 30:
            scores["pluck"] += 1.4
            evidence.append("amp env: fast attack/release")
        elif attack >= 55 or (release or 0) >= 80:
            scores["pad"] += 1.4
            evidence.append("amp env: slow attack/long release")
        elif (release or 0) <= 25:
            scores["bass"] += 0.6
            evidence.append("amp env: short release")
    sus = raw("AmpEnvelopeSustainLevel")
    if sus is not None and sus >= 100 and (attack or 0) <= 8:
        scores["lead"] += 0.6
        evidence.append("amp env: sustained")
    cutoff = raw("CutoffFrequency")
    if cutoff is not None and cutoff <= 45:
        scores["bass"] += 0.8
        evidence.append("filter: dark cutoff=%d" % cutoff)
    if cutoff is not None and cutoff >= 100:
        scores["lead"] += 0.3
        evidence.append("filter: open cutoff=%d" % cutoff)
    del_level = raw("DelayLevel")
    if del_level and del_level > 0:
        scores["pluck"] += 0.3
        scores["lead"] += 0.2
        evidence.append("delay: level=%d" % del_level)

    best = max(scores.items(), key=lambda kv: kv[1])
    total = sum(scores.values())
    if best[1] <= 0.35 or total <= 0.0:
        return ("other", 0.0, evidence[:4], family)
    return (best[0], round(best[1] / max(total, 0.001), 3), evidence[:5], family)


def describe(role: str, params: Dict[str, dict], family: str) -> str:
    bits = []
    if family and family != "unknown":
        bits.append("%s osc" % family.lower())
    for key, fmt in (("CutoffFrequency", "cutoff %d"), ("Resonance", "res %d"),
                     ("AmpEnvelopeAttackTime", "amp A %d"), ("AmpEnvelopeReleaseTime", "amp R %d"),
                     ("FilterEnvelopeDepth", "filt env %d"), ("DelayLevel", "delay %d")):
        v = params.get(key)
        if v is not None:
            bits.append(fmt % int(v["raw"]))
    fx = params.get("MultiEffectsType", {}).get("label")
    if isinstance(fx, str):
        bits.append("fx %s" % fx)
    return ("JP-8080 %s patch: " % role) + ", ".join(bits)


# --------------------------------------------------------------------------- #
# Entries / banks
# --------------------------------------------------------------------------- #
def build_entries(messages: Iterable[dict]) -> List[dict]:
    """Group DT1 pages into patch/performance units (page 0 + optional page 1)."""
    groups: Dict[Tuple[str, int, int], dict] = {}
    order: List[Tuple[str, int, int]] = []
    for msg in messages:
        area, unit_bank, unit, page = area_of(int(msg["value"]))
        if area == "other":
            continue
        key = (area, unit_bank, unit)
        grp = groups.get(key)
        if grp is None:
            grp = {"area": area, "bank": unit_bank, "unit": unit, "pages": {}, "checksums": []}
            groups[key] = grp
            order.append(key)
        grp["pages"][page] = msg["data"]
        grp["checksums"].append(msg["checksumOk"])

    entries: List[dict] = []
    for key in order:
        grp = groups[key]
        blob = bytearray()
        for page in sorted(grp["pages"]):
            base = page * PAGE
            data = grp["pages"][page]
            if len(blob) < base:
                blob.extend(bytes(base - len(blob)))
            blob[base:base + len(data)] = data
        blob = bytes(blob)
        name_raw = blob[NAME_OFFSET:NAME_OFFSET + NAME_LEN] if len(blob) > NAME_OFFSET else b""
        if not _is_printable_name(name_raw):
            continue
        name = name_raw.decode("latin-1").rstrip()
        if grp["area"] == "performance-common":
            # A performance common block carries the performance NAME, not a
            # patch body -- decoding patch parameters from it would invent data.
            params, unknown = {}, []
            role, conf, evidence, family = ("performance", 0.0,
                                            ["performance common block"], "n/a")
        else:
            params, unknown = decode_params(blob)
            role, conf, evidence, family = classify_role(name, params)
        entries.append({
            "name": name,
            "role": role,
            "roleConfidence": conf,
            "roleEvidence": evidence,
            "family": family,
            "area": grp["area"],
            "bank": grp["bank"],
            "slot": grp["unit"],
            "pages": sorted(grp["pages"]),
            "bytes": len(blob),
            "truncated": 1 not in grp["pages"],
            "checksumOk": all(grp["checksums"]),
            "placeholder": name.upper().startswith("INIT"),
            "paramCount": len(params),
            "unknownByteCount": len(unknown),
            "params": params,
            "sha1": hashlib.sha1(blob).hexdigest()[:16],
            "sysexHex": blob.hex(),
            # exact per-page payloads so --explode can rewrite byte-identical DT1s
            "pagesRaw": {str(p): grp["pages"][p].hex() for p in sorted(grp["pages"])},
        })
    return entries


def bank_description(entries: Sequence[dict], stem: str) -> str:
    counts: Dict[str, int] = {}
    for e in entries:
        if e["placeholder"]:
            continue
        counts[e["role"]] = counts.get(e["role"], 0) + 1
    top = ", ".join("%d %s" % (n, r) for r, n in sorted(counts.items(), key=lambda kv: -kv[1])[:5])
    return "JP-8080 bank %s: %d entries (%d non-init)%s" % (
        stem, len(entries), sum(counts.values()), (" -- " + top) if top else "")


def build_bank(path: str, entries: Sequence[dict], with_bytes: bool = True) -> dict:
    stem = os.path.basename(path)
    roles: Dict[str, int] = {}
    for e in entries:
        roles[e["role"]] = roles.get(e["role"], 0) + 1
    dominant = max(roles.items(), key=lambda kv: kv[1])[0] if roles else "empty"
    mapped = {}
    for e in entries:
        key = ("slot%03d" % e["slot"]) if e["area"] == "patch" else ("perf%03d_part%02d" % (e["bank"], e["slot"]))
        mapped[key] = {
            "param": e["name"] or key,
            "name": e["name"],
            "role": e["role"],
            "params": compact_params(e["params"]),
        }
    patches = []
    for e in entries:
        item = {
            "name": e["name"], "role": e["role"], "roleConfidence": e["roleConfidence"],
            "roleEvidence": e["roleEvidence"], "family": e["family"], "area": e["area"],
            "bank": e["bank"], "slot": e["slot"], "pages": e["pages"], "bytes": e["bytes"],
            "truncated": e["truncated"], "checksumOk": e["checksumOk"],
            "placeholder": e["placeholder"], "paramCount": e["paramCount"], "sha1": e["sha1"],
            "params": compact_params(e["params"]), "labels": compact_labels(e["params"]),
        }
        if with_bytes:
            item["sysexHex"] = e["sysexHex"]
        patches.append(item)
    return {
        "schema": SCHEMA_BANK,
        "engine": ENGINE,
        "name": stem,
        "format": "jp8080-dt1",
        "description": bank_description(entries, stem),
        "roleCheck": {
            "verdict": dominant,
            "counts": roles,
            "initPlaceholders": sum(1 for e in entries if e["placeholder"]),
            "checksumFailures": sum(1 for e in entries if not e["checksumOk"]),
        },
        "unmapped": list(UNMAPPED_BANK_FEATURES),
        "paramDefs": PARAM_DEFS,
        "mappedParams": mapped,
        "patches": patches,
    }


def build_patch_sidecar(entry: dict, bank_stem: str) -> dict:
    unmapped = list(UNMAPPED_BANK_FEATURES)
    if entry["truncated"]:
        unmapped.append("patch truncated to page 0")
    return {
        "schema": SCHEMA_PATCH,
        "engine": ENGINE,
        "name": entry["name"],
        "format": "jp8080-dt1",
        "description": describe(entry["role"], entry["params"], entry["family"]),
        "roleCheck": {
            "verdict": entry["role"],
            "confidence": entry["roleConfidence"],
            "evidence": entry["roleEvidence"],
        },
        "unmapped": unmapped,
        "paramDefs": PARAM_DEFS,
        "mappedParams": compact_params(entry["params"]),
        "labels": compact_labels(entry["params"]),
        "bank": {"source": bank_stem, "bank": entry["bank"], "slot": entry["slot"], "area": entry["area"]},
        "sha1": entry["sha1"],
        "sysexHex": entry["sysexHex"],
    }


# --------------------------------------------------------------------------- #
# Commands
# --------------------------------------------------------------------------- #
def _bank_files(root: str) -> List[str]:
    exts = (".syx", ".mid")
    out = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in sorted(filenames):
            if fn.lower().endswith(exts):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def _write_json(path: str, payload: object) -> None:
    text = json.dumps(payload, indent=1, sort_keys=True, ensure_ascii=False)
    with open(path, "w", encoding="utf-8", newline=chr(10)) as fh:
        fh.write(text + chr(10))


def run_dump(path: str, limit: int, as_json: bool) -> int:
    messages = parse_file(path)
    entries = build_entries(messages)
    if as_json:
        print(json.dumps(build_bank(path, entries), indent=1, sort_keys=True))
        return 0
    bad = sum(0 if m["checksumOk"] else 1 for m in messages)
    print("%s: %d DT1 messages (%d checksum failures), %d entries"
          % (os.path.basename(path), len(messages), bad, len(entries)))
    for e in entries[:limit]:
        where = ("bank%d" % e["bank"]) if e["area"] == "patch" else ("perf%d" % e["bank"])
        print("  %-18s %-12s slot=%-3d %-16s %-7s %s" % (
            e["area"], where, e["slot"], e["name"], e["role"],
            "truncated" if e["truncated"] else "%d bytes" % e["bytes"]))
        for key in ("Osc1Waveform", "FilterType", "CutoffFrequency", "Resonance",
                    "FilterEnvelopeDepth", "AmpEnvelopeAttackTime", "AmpEnvelopeReleaseTime",
                    "MultiEffectsType", "DelayLevel"):
            v = e["params"].get(key)
            if v is not None:
                extra = "[%s]" % v["label"] if v.get("label") else "signed=%s" % v.get("signed", "-")
                print("        %-26s raw=%-4d %s" % (key, v["raw"], extra))
    return 0


def survey(root: str) -> dict:
    files = _bank_files(root)
    totals = {"files": 0, "messages": 0, "checksumFailures": 0, "entries": 0,
              "nonInit": 0, "nonInitPatches": 0, "performanceNames": 0, "truncated": 0}
    per_bank = []
    role_hist: Dict[str, int] = {}
    family_hist: Dict[str, int] = {}
    name_index: Dict[str, List[str]] = {}
    shortlist: Dict[str, List[dict]] = {}
    param_hist: Dict[str, Dict[int, int]] = {"Osc1Waveform": {}, "FilterType": {}, "MultiEffectsType": {}}
    for path in files:
        try:
            messages = parse_file(path)
        except OSError:
            continue
        entries = build_entries(messages)
        if not messages:
            per_bank.append({"file": os.path.basename(path), "messages": 0, "entries": 0,
                             "note": "no JP-8080 DT1 messages"})
            continue
        totals["files"] += 1
        totals["messages"] += len(messages)
        totals["checksumFailures"] += sum(0 if m["checksumOk"] else 1 for m in messages)
        totals["entries"] += len(entries)
        totals["truncated"] += sum(1 for e in entries if e["truncated"])
        roles: Dict[str, int] = {}
        for e in entries:
            role_hist[e["role"]] = role_hist.get(e["role"], 0) + 1
            family_hist[e["family"]] = family_hist.get(e["family"], 0) + 1
            roles[e["role"]] = roles.get(e["role"], 0) + 1
            if e["role"] == "performance":
                # a performance NAME is not a patch: keep it out of the usable
                # patch count so bank ranking is not fooled by empty
                # performance banks (the psy .syx banks carry 64 real
                # performance names but mostly INIT PATCH bodies).
                totals["performanceNames"] += 1
            elif not e["placeholder"]:
                totals["nonInitPatches"] += 1
            if not e["placeholder"] and e["role"] not in ("performance", "other"):
                # concrete picks for the Sound Selector role (ranked per bank below)
                ref = (("bank%d/slot%02d" % (e["bank"], e["slot"])) if e["area"] == "patch"
                       else ("perf%03d/part%d" % (e["bank"], e["slot"])))
                shortlist.setdefault(e["role"], []).append({
                    "bank": os.path.basename(path), "area": e["area"], "ref": ref,
                    "slot": e["slot"], "name": e["name"], "family": e["family"],
                    "confidence": e["roleConfidence"],
                })
            if not e["placeholder"]:
                totals["nonInit"] += 1
                slot = name_index.setdefault(e["name"].lower(), {"name": e["name"], "banks": []})
                slot["banks"].append(os.path.basename(path))
                for key in param_hist:
                    v = e["params"].get(key)
                    if v is not None:
                        param_hist[key][int(v["raw"])] = param_hist[key].get(int(v["raw"]), 0) + 1
        per_bank.append({
            "file": os.path.basename(path),
            "area": entries[0]["area"] if entries else "none",
            "messages": len(messages),
            "entries": len(entries),
            "nonInit": sum(1 for e in entries if not e["placeholder"]),
            "nonInitPatches": sum(1 for e in entries
                                  if not e["placeholder"] and e["role"] != "performance"),
            "roles": roles,
        })
    duplicates = sorted(({"name": info["name"], "banks": sorted(set(info["banks"]))}
                         for info in name_index.values() if len(set(info["banks"])) > 1),
                        key=lambda d: -len(d["banks"]))
    psy_ranked = []
    for bank in per_bank:
        name = bank["file"].lower()
        score = 0.0
        for key, weight in (("psy", 3.0), ("goa", 3.0), ("trance", 2.0), ("acid", 1.5),
                            ("bass", 1.0), ("hoover", 1.5), ("unison", 0.8), ("distortion", 0.8),
                            ("techno", 1.0), ("edm", 0.5)):
            if key in name:
                score += weight
        # rank by usable PATCHES, not by performance names
        score += 0.01 * bank.get("nonInitPatches", 0)
        psy_ranked.append({"file": bank["file"], "score": round(score, 2),
                           "nonInitPatches": bank.get("nonInitPatches", 0),
                           "roles": bank.get("roles", {})})
    psy_ranked.sort(key=lambda b: -b["score"])
    bank_rank = {b["file"]: i for i, b in enumerate(psy_ranked)}
    role_shortlist: Dict[str, List[dict]] = {}
    for role, items in shortlist.items():
        items.sort(key=lambda it: (bank_rank.get(it["bank"], 999), -float(it["confidence"]), it["slot"]))
        role_shortlist[role] = items[:12]
    return {
        "schema": SCHEMA_SURVEY,
        "engine": ENGINE,
        "root": os.path.basename(os.path.normpath(root)),
        "totals": totals,
        "roleHistogram": dict(sorted(role_hist.items(), key=lambda kv: -kv[1])),
        "familyHistogram": dict(sorted(family_hist.items(), key=lambda kv: -kv[1])),
        "paramHistograms": {k: dict(sorted(v.items())) for k, v in param_hist.items()},
        "perBank": per_bank,
        "duplicateNames": duplicates[:40],
        "psyRelevant": psy_ranked[:12],
        # per-role concrete picks (bank/slot/name), ranked by bank psy-score then
        # classification confidence -- the curation surface for a Sound Selector
        "roleShortlist": {r: role_shortlist[r] for r in sorted(role_shortlist)},
        "unmapped": list(UNMAPPED_BANK_FEATURES),
    }


def run_sidecars(root: str, role: Optional[str], explode: bool, with_bytes: bool = False) -> int:
    files = _bank_files(root)
    written = 0
    entries_written = 0
    exploded = 0
    for path in files:
        try:
            entries = build_entries(parse_file(path))
        except OSError:
            continue
        if not entries:
            continue
        selected = [e for e in entries if role is None or e["role"] == role]
        if not selected:
            continue
        _write_json(path + SYSEX_SUFFIX, build_bank(path, selected, with_bytes=with_bytes))
        written += 1
        entries_written += len(selected)
        if explode:
            folder = os.path.join(os.path.dirname(path), "exploded",
                                  os.path.splitext(os.path.basename(path))[0])
            os.makedirs(folder, exist_ok=True)
            for e in selected:
                if e["placeholder"]:
                    continue
                safe = "".join(c if c.isalnum() or c in " -_()" else "_" for c in e["name"]).strip() or "patch"
                stem = "%02d %s" % (e["slot"], safe)
                syx_path = os.path.join(folder, stem + ".syx")
                base = unit_page_value(e)
                with open(syx_path, "wb") as fh:
                    for page_key in sorted(e["pagesRaw"], key=int):
                        page = int(page_key)
                        fh.write(emit_dt1(addr_bytes(base + page),
                                          bytes.fromhex(e["pagesRaw"][page_key])))
                _write_json(syx_path + SYSEX_SUFFIX, build_patch_sidecar(e, os.path.basename(path)))
                exploded += 1
    print("sidecars: %d banks, %d entries%s"
          % (written, entries_written, (", %d exploded .syx" % exploded) if explode else ""))
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Roland JP-8080 (JE8086) patch decoder / sidecar sweep")
    ap.add_argument("--dump", metavar="FILE", help="decode one bank and print its patches")
    ap.add_argument("--limit", type=int, default=8, help="entries to print in --dump (default 8)")
    ap.add_argument("--json", action="store_true", help="--dump as JSON")
    ap.add_argument("--survey", metavar="DIR", help="scan a bank directory and report")
    ap.add_argument("--out", metavar="FILE", help="write the survey JSON here")
    ap.add_argument("--sidecars", metavar="DIR", help="write <bank>.je8086.json sidecars next to banks")
    ap.add_argument("--role", help="only sidecar patches whose role matches (bass/lead/pad/pluck/fx/arp/chord/other)")
    ap.add_argument("--explode", action="store_true", help="also write per-patch .syx + sidecars into <DIR>/exploded/")
    ap.add_argument("--with-bytes", action="store_true",
                    help="embed each patch's raw DT1 payload (sysexHex) in the bank sidecar; "
                         "default is a compact metadata index (a loader re-parses the bank file)")
    args = ap.parse_args(argv)

    if args.dump:
        return run_dump(args.dump, args.limit, args.json)
    if args.survey:
        if not os.path.isdir(args.survey):
            print("not a directory: %s" % args.survey, file=sys.stderr)
            return 2
        data = survey(args.survey)
        if args.out:
            _write_json(args.out, data)
        print(json.dumps(data["totals"], indent=1, sort_keys=True))
        print("roles: %s" % json.dumps(data["roleHistogram"]))
        print("top psy-relevant banks:")
        for bank in data["psyRelevant"]:
            print("  %-42s score=%-6s usablePatches=%d" % (
                bank["file"], bank["score"], bank["nonInitPatches"]))
        if args.out:
            print("survey -> %s" % args.out)
        return 0
    if args.sidecars:
        if not os.path.isdir(args.sidecars):
            print("not a directory: %s" % args.sidecars, file=sys.stderr)
            return 2
        return run_sidecars(args.sidecars, args.role, args.explode, args.with_bytes)
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
