#!/usr/bin/env python3
"""Access Virus FX / modulation-matrix page decoder (sidecar enrichment).

Decodes the FX / mod-matrix bytes of Access Virus single dumps into NAMED
values using the gearmulator parameterDescriptions vocabularies:

  osirusJucePlugin/parameterDescriptions_C.json   -> B/C generation singles
      267-byte dump, 256-byte payload = page 112 ("A") + page 113 ("B")
  osTIrusJucePlugin/parameterDescriptions_TI.json -> TI generation singles
      524-byte block, 512-byte payload = pages 112, 113, 114, 115

Page -> payload offset (verified against the real corpus, see tests):

    offset = (page - 112) * 128 + index

The 10-char patch name lives at payload bytes 240..249 in BOTH generations
(page 113, indices 112..121), which pins the base page at 112: a page-110
based layout would put the name at 496, and all 128 blocks of the reference
TI bank have ASCII at 240 and zeros at 496 (measured 2026-09-16).

Anchors (docs/handoffs/2026-09-16-matrix-presets-handoff.md, "What a matrix
preset is per engine" -> Virus; verified in both vocabularies):

    page 113 index 64  "Assign1 Source"        65 "Assign1 Destination"
    page 113 index 66  "Assign1 Amount"        67 "Assign2 Source"
    page 113 index 72  "Assign3 Source"
    TI  page 112 idx 103  "Chorus/Type";  C page 112 idx 38 "Ringmodulator
    Volume" / TI page 112 idx 50; vocoder block page 113 idx 39+ (C) and
    page 112 idx 40..58 aliases (TI).

Stop-gate protocol (byte-match): every dump is parsed twice -- once by
``virus_patch`` and once by this module's own container walker -- and the
payloads must be byte-identical.  The vocab decode is then REBUILT into a
payload and compared byte-for-byte against the raw payload.  Any mismatch
fails the sweep (exit 1).  Checksums are validated per dump:

    B/C:  (dev + 0x10 + bank + prog + sum(payload)) & 0x7F  at byte 265
    TI:   same formula over the 512-byte payload             at byte 522

(equivalently sum(bytes 5..last) & 0x7F -- the TI json's dump definition
says {"type": "checksum", "first": 5, "last": 521}).  TDM chunks carry no
checksum byte; they are reported as checksum-NA.

Coverage is reported honestly: vocabulary holes (page/index slots with no
parameter definition, e.g. C page 112 indices 112..119 where the waf80 B/C
docs place the delay block) are counted and any NONZERO byte in a hole is
reported per slot -- hole bytes are never named (names are never invented).

fxParams selection: a slot is an FX/mod-matrix member when its resolved
primary name matches the FX family tokens (chorus/delay/vocoder/ringmod/
phaser/distortion/EQ/filter bank/assign/...) or is an LFO routing member
(Lfo mode/destination/assign/env/clock/fade).  Where the TI vocabulary
gives one byte several context aliases (e.g. index 40 is Cutoff AND
Vocoder/Carrier Center Frequency), the LAST non-deprecated alias wins --
that is the newer (``version: 1``) definition; the compromise is documented
in README and does not affect byte-match verification.

Sidecar output (sidecarRev 2) keeps every rev-1 key (schema, name, engine,
format, mappedParams, unmapped, roleCheck, description) and adds:

    sidecarRev   2
    fxModel      "TI" | "B/C"
    fxParams     {paramName: rawByte, ...}   (FX/matrix slots only)
    fxCoverage   {payloadBytes, covered, verifiedValues, holes,
                  holesNonZero, checksum, byteMatch}

Usage:
    python3 virus_fx_pages.py --sweep "DIR" [--role R] [--verify-only]
    python3 virus_fx_pages.py --dump FILE
    python3 virus_fx_pages.py --morphs --sheet S --pairs a:b,... --steps N --out P
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from typing import Dict, List, Optional, Sequence, Tuple

import virus_patch as vp

MORPH_APPLIES_VIA = "program_writer_pending"

SIDECAR_REV = 2
BASE_PAGE = 112
PAGE_LEN = 128
PAYLOAD_BC = 2 * PAGE_LEN
PAYLOAD_TI = 4 * PAGE_LEN

VOCAB_CANDIDATES = {
    "TI": (
        "/mnt/d/pdf/gearmulator-2.2.9/source/osTIrusJucePlugin/parameterDescriptions_TI.json",
        "/mnt/d/pdf/retromulator-main/source/osTIrusJucePlugin/parameterDescriptions_TI.json",
        "D:/pdf/gearmulator-2.2.9/source/osTIrusJucePlugin/parameterDescriptions_TI.json",
    ),
    "B/C": (
        "/mnt/d/pdf/gearmulator-2.2.9/source/osirusJucePlugin/parameterDescriptions_C.json",
        "/mnt/d/pdf/retromulator-main/source/osirusJucePlugin/parameterDescriptions_C.json",
        "D:/pdf/gearmulator-2.2.9/source/osirusJucePlugin/parameterDescriptions_C.json",
    ),
}

_UNWANTED = re.compile(r"deprecated|_undefined|singlename|^category|patchname|^version$",
                       re.IGNORECASE)

# FX-family tokens (checked against the lowercased primary vocab name).
_FX_TOKENS = ("assign", "vocoder", "chorus", "delay", "dly", "ringmod",
              "phaser", "distortion", "loweq", "higheq", "mideq",
              "bass intensity", "bass tune", "filter bank",
              "input follower", "spectral")
# LFO routing/movement members (handoff: Lfo1/2/3 Mode, Lfo3 Destination,
# LfoN Env Mode; plus clock/assign/fade routing).
_LFO_TOKENS = ("mode", "destination", "assign", "env", "clock", "fade")


def is_fx_param(name: str) -> bool:
    """True when a vocab name is an FX / mod-matrix member."""
    low = name.lower()
    if _UNWANTED.search(low):
        return False
    if any(tok in low for tok in _FX_TOKENS):
        return True
    return "lfo" in low and any(tok in low for tok in _LFO_TOKENS)


# ---------------------------------------------------------------------------
# Vocabulary
# ---------------------------------------------------------------------------

_VOCAB_RE = re.compile(
    r'"page":\s*(\d+),\s*"index":\s*(\d+),[^{}]{0,200}?"name":\s*"([^"]+)"')


def load_vocab(path: str) -> Dict[Tuple[int, int], List[str]]:
    """(page, index) -> [names in file order] from a parameterDescriptions json.

    The files are JSON5-ish (comments, trailing commas, extra keys like
    "class" between "index" and "name") and resist a strict parse, so the
    triplets are read with a bounded regex -- the same approach
    harvest_fx_presets.load_vocabulary established for this repo.
    """
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    slots: Dict[Tuple[int, int], List[str]] = {}
    for m in _VOCAB_RE.finditer(text):
        slot = (int(m.group(1)), int(m.group(2)))
        slots.setdefault(slot, []).append(m.group(3))
    return slots


def primary_name(names: Sequence[str]) -> str:
    """Deterministic alias resolution: LAST non-deprecated/undefined name.

    The vocabularies list newer definitions later (the ``version: 1``
    rewrite of deprecated rows), so last-wins picks the current name.
    """
    for name in reversed(names):
        if not _UNWANTED.search(name):
            return name
    return names[-1]


class Vocabulary:
    """Model -> {(page, index): primary name} tables, loaded lazily."""

    def __init__(self) -> None:
        self._tables: Dict[str, Dict[Tuple[int, int], str]] = {}
        self.sources: Dict[str, str] = {}
        for model, candidates in VOCAB_CANDIDATES.items():
            for path in candidates:
                if os.path.isfile(path):
                    raw = load_vocab(path)
                    self._tables[model] = {
                        slot: primary_name(names)
                        for slot, names in sorted(raw.items())
                    }
                    self.sources[model] = path
                    break

    def table(self, model: str) -> Dict[Tuple[int, int], str]:
        return self._tables.get(model, {})

    def anchor(self, model: str, page: int, index: int) -> Optional[str]:
        return self._tables.get(model, {}).get((page, index))


# ---------------------------------------------------------------------------
# Independent container walker (stop-gate re-parse)
# ---------------------------------------------------------------------------

def payload_model(payload_len: int) -> Optional[str]:
    if payload_len == PAYLOAD_BC:
        return "B/C"
    if payload_len == PAYLOAD_TI:
        return "TI"
    return None


def model_pages(model: str) -> Tuple[int, ...]:
    return (BASE_PAGE, BASE_PAGE + 1) if model == "B/C" else \
        (BASE_PAGE, BASE_PAGE + 1, BASE_PAGE + 2, BASE_PAGE + 3)


def _read_vlq(data: bytes, i: int) -> Tuple[int, int]:
    value = 0
    while i < len(data):
        byte = data[i]
        i += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, i
    raise ValueError("truncated VLQ")


def smf_sysex_messages(data: bytes) -> List[bytes]:
    """Sysex messages (F0..F7) of a Standard MIDI file (independent walker)."""
    msgs: List[bytes] = []
    if len(data) < 14 or data[:4] != b"MThd":
        return msgs
    header_len = int.from_bytes(data[4:8], "big")   # length field at offset 4
    pos = 8 + header_len
    while pos + 8 <= len(data):
        if data[pos:pos + 4] != b"MTrk":
            break
        track_len = int.from_bytes(data[pos + 4:pos + 8], "big")
        pos += 8
        end = min(pos + track_len, len(data))
        i = pos
        running: Optional[int] = None
        while i < end:
            while i < end and data[i] & 0x80:      # delta time VLQ
                i += 1
            i += 1
            if i >= end:
                break
            status = data[i]
            if status == 0xF0:
                length, i = _read_vlq(data, i + 1)
                msgs.append(bytes([0xF0]) + data[i:i + length])
                i += length
            elif status == 0xFF:
                length, i = _read_vlq(data, i + 2)   # +1 meta type, +1 length
                i += length
                running = None
            elif status == 0xF7:        # escaped sysex: no meta-type byte
                length, i = _read_vlq(data, i + 1)
                i += length
                running = None
            elif status & 0x80:
                running = status
                i += 2 if status in (0xC0, 0xD0) else 3
            else:
                if running is None:
                    break
                i += 1 if running in (0xC0, 0xD0) else 2
        pos += track_len
    return msgs


def _single_fields(dump: bytes, index: int) -> Optional[dict]:
    """Dump dict for a 267-byte (B/C) or 524-byte (TI) single message."""
    if len(dump) == 267:
        payload_len, cs_off = PAYLOAD_BC, 265
    elif len(dump) == 524:
        payload_len, cs_off = PAYLOAD_TI, 522
    else:
        return None
    if dump[0] != 0xF0 or dump[1:4] != vp.MANUFACTURER or dump[4] != 0x01:
        return None
    if dump[6] != 0x10:
        return None
    payload = dump[9:9 + payload_len]
    return {
        "payload": payload,
        "payloadLen": payload_len,
        "storedCs": dump[cs_off],
        "name": vp._name_from_data(payload),
        "index": index,
    }


def iter_dumps(data: bytes, fmt: str) -> List[dict]:
    """Re-parse a container file into single dumps (independent of PARSERS)."""
    dumps: List[dict] = []
    if fmt == "bcsingle":
        fields = _single_fields(data, 0)
        if fields:
            dumps.append(fields)
    elif fmt == "vhc":
        for i in range(0, len(data) - 266, 267):
            fields = _single_fields(data[i:i + 267], i // 267)
            if fields:
                dumps.append(fields)
    elif fmt == "tibank":
        for i in range(0, len(data) - 523, 524):
            fields = _single_fields(data[i:i + 524], i // 524)
            if fields:
                dumps.append(fields)
    elif fmt == "tdm":
        if len(data) >= 124 + PAYLOAD_BC and data[8:16] == b"DigiVrus":
            payload = data[124:124 + PAYLOAD_BC]
            dumps.append({"payload": payload, "payloadLen": PAYLOAD_BC,
                          "storedCs": None, "name": vp._name_from_data(payload),
                          "index": 0})
    elif fmt == "stdmidi":
        index = 0
        for msg in smf_sysex_messages(data):
            fields = _single_fields(msg, index)
            if fields:
                dumps.append(fields)
                index += 1
    return dumps


def verify_checksum(dump: bytes, payload_len: int) -> Optional[bool]:
    """Checksum rule per generation; None when the container carries none."""
    if payload_len == PAYLOAD_BC and len(dump) >= 266:
        stored = dump[265]
    elif payload_len == PAYLOAD_TI and len(dump) >= 523:
        stored = dump[522]
    else:
        return None
    expected = (dump[5] + 0x10 + dump[7] + dump[8]
                + sum(dump[9:9 + payload_len])) & 0x7F
    return stored == expected


# ---------------------------------------------------------------------------
# Decode + byte-match
# ---------------------------------------------------------------------------

def decode_payload(payload: bytes,
                   table: Dict[Tuple[int, int], str]
                   ) -> Tuple[Dict[Tuple[int, int], int], List[Tuple[int, int]]]:
    """(page, index) -> raw byte for every vocab slot inside the payload.

    Slots with no vocabulary entry are returned as holes (never named).
    """
    model = payload_model(len(payload))
    values: Dict[Tuple[int, int], int] = {}
    holes: List[Tuple[int, int]] = []
    for page in model_pages(model or ""):
        for index in range(PAGE_LEN):
            offset = (page - BASE_PAGE) * PAGE_LEN + index
            if offset >= len(payload):
                break
            if (page, index) in table:
                values[(page, index)] = payload[offset]
            else:
                holes.append((page, index))
    return values, holes


def byte_mismatches(payload: bytes, values: Dict[Tuple[int, int], int]) -> int:
    """Rebuild the payload from the decoded map; count differing bytes."""
    rebuilt = bytearray(payload)
    for (page, index), value in values.items():
        rebuilt[(page - BASE_PAGE) * PAGE_LEN + index] = value
    return sum(1 for a, b in zip(rebuilt, payload) if a != b)


def fx_params(values: Dict[Tuple[int, int], int],
              table: Dict[Tuple[int, int], str]) -> Dict[str, int]:
    """Named FX/mod-matrix values, ordered by (page, index)."""
    named: Dict[str, int] = {}
    for (page, index), value in sorted(values.items()):
        name = table.get((page, index))
        if name and is_fx_param(name):
            named[name] = value
    return named


# ---------------------------------------------------------------------------
# Sweep
# ---------------------------------------------------------------------------

def _align_dump(dumps: List[dict], patch: dict) -> Optional[dict]:
    """Pick the dump matching virus_patch's first OK patch (byte identity)."""
    data = bytes(patch.get("data") or [])
    if data:
        for dump in dumps:
            if dump["payload"][:len(data)] == data:
                return dump
    name = patch.get("name")
    if name:
        for dump in dumps:
            if dump["name"] == name:
                return dump
    index = patch.get("index")
    if isinstance(index, int) and 0 <= index < len(dumps):
        return dumps[index]
    return dumps[0] if dumps else None


def _dump_bytes(data: bytes, fmt: str, dump: dict) -> bytes:
    """The raw dump bytes (header..checksum) a dump dict was parsed from."""
    payload_len = dump["payloadLen"]
    if fmt == "bcsingle":
        return data
    if fmt == "vhc":
        off = dump["index"] * 267
        return data[off:off + 267]
    if fmt == "tibank":
        off = dump["index"] * 524
        return data[off:off + 524]
    if fmt == "stdmidi":
        for msg in smf_sysex_messages(data):
            if msg[9:9 + payload_len] == dump["payload"]:
                return msg
        return data
    return b""  # tdm: no checksum byte (reported as checksum-NA)


def sweep_virus_fx(root: str, role: Optional[str] = None,
                   verify_only: bool = False) -> dict:
    """Re-sweep ``<patch>.virus.json`` sidecars under root with FX decode."""
    vocab = Vocabulary()
    stats: dict = {
        "root": root, "files": 0, "sidecars": 0, "skipped": 0, "failed": 0,
        "dumps": 0, "valuesVerified": 0, "byteMismatches": 0,
        "checksumOk": 0, "checksumBad": 0, "checksumNA": 0,
        "holes": 0, "holesNonZero": 0, "nonZeroHoleSlots": {},
        "model": {"TI": 0, "B/C": 0}, "crossParseDisagreements": 0,
        "vocab": vocab.sources, "verifyOnly": verify_only, "ok": True,
        "errors": [],
    }
    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if fn.endswith(".virus.json"):
                continue
            full = os.path.join(dirpath, fn)
            stats["files"] += 1
            try:
                with open(full, "rb") as fh:
                    data = fh.read()
            except OSError as exc:
                stats["failed"] += 1
                stats["errors"].append(f"{full}: read failed: {exc}")
                continue
            fmt = vp.detect_format(data, full)
            if fmt is None:
                stats["skipped"] += 1
                continue
            try:
                patches = vp.PARSERS[fmt](data)
            except Exception as exc:  # noqa: BLE001 - bad file must not kill sweep
                stats["failed"] += 1
                stats["errors"].append(f"{full}: parse raised: {exc}")
                continue
            ok = [p for p in patches if not p.get("error")]
            if not ok:
                stats["skipped"] += 1
                continue
            dumps = iter_dumps(data, fmt)
            side_dump = _align_dump(dumps, ok[0])
            if not dumps or side_dump is None:
                stats["skipped"] += 1
                continue

            # stop-gate 1: independent parse paths must agree byte-for-byte
            # for EVERY patch of the container (a 128-patch bank is 128
            # checks, not one).  vp patch payloads are prefix-matched by
            # their first bytes so duplicate patches still pair up.
            vp_payloads = [bytes(p["data"]) for p in patches if p.get("data")]
            by_prefix: Dict[bytes, List[bytes]] = {}
            for vp_pl in vp_payloads:
                by_prefix.setdefault(vp_pl[:24], []).append(vp_pl)

            def _vp_agrees(payload: bytes) -> bool:
                for vp_pl in by_prefix.get(payload[:24], ()):
                    if payload[:len(vp_pl)] == vp_pl:
                        return True
                return False

            side_model: Optional[str] = None
            side_values: Dict[Tuple[int, int], int] = {}
            side_holes: List[Tuple[int, int]] = []
            side_cs: Optional[bool] = None
            side_mismatches = 0
            for dump in dumps:
                payload = dump["payload"]
                model = payload_model(len(payload))
                if model is None:
                    continue
                stats["dumps"] += 1
                stats["model"][model] += 1
                if not _vp_agrees(payload):
                    stats["crossParseDisagreements"] += 1
                    stats["errors"].append(
                        f"{full}: patch {dump['index']} "
                        f"({dump['name']!r}) payloads disagree")

                cs = verify_checksum(_dump_bytes(data, fmt, dump),
                                     len(payload))
                if cs is None:
                    stats["checksumNA"] += 1
                elif cs:
                    stats["checksumOk"] += 1
                else:
                    stats["checksumBad"] += 1
                    if dump is side_dump:
                        stats["errors"].append(
                            f"{full}: checksum mismatch")

                table = vocab.table(model)
                values, holes = decode_payload(payload, table)
                mismatches = byte_mismatches(payload, values)
                stats["valuesVerified"] += len(values)
                stats["byteMismatches"] += mismatches
                stats["holes"] += len(holes)
                nonzero = 0
                for slot in holes:
                    offset = (slot[0] - BASE_PAGE) * PAGE_LEN + slot[1]
                    if payload[offset] != 0:
                        nonzero += 1
                        key = "%d/%d" % slot
                        stats["nonZeroHoleSlots"][key] = \
                            stats["nonZeroHoleSlots"].get(key, 0) + 1
                stats["holesNonZero"] += nonzero
                if mismatches:
                    stats["errors"].append(
                        f"{full}: patch {dump['index']}: "
                        f"{mismatches} byte-match mismatches")
                if dump is side_dump:
                    side_model = model
                    side_values, side_holes = values, holes
                    side_cs, side_mismatches = cs, mismatches

            if side_model is None:
                stats["skipped"] += 1
                continue
            model, values, holes = side_model, side_values, side_holes
            cs, mismatches = side_cs, side_mismatches

            if verify_only:
                continue

            side = vp.build_sidecar(ok[0], role=role, format=fmt)
            side["sidecarRev"] = SIDECAR_REV
            side["fxModel"] = model
            side["fxParams"] = fx_params(values, table)
            side["fxCoverage"] = {
                "payloadBytes": len(payload),
                "covered": len(values),
                "verifiedValues": len(values),
                "holes": len(holes),
                "holesNonZero": nonzero,
                "checksum": ("na" if cs is None else ("ok" if cs else "bad")),
                "byteMatch": "pass" if mismatches == 0 else "fail",
            }
            out_path = full + ".virus.json"
            with open(out_path, "w", encoding="utf-8") as fh:
                fh.write(json.dumps(side, sort_keys=True, indent=2) + "\n")
            stats["sidecars"] += 1

    # The stop-gate is the byte-match + cross-parse agreement: a layout the
    # decode cannot prove byte-exactly must fail the sweep.  Checksum
    # failures are reported per file (vendor banks exist whose stored
    # checksum bytes match NO consistent formula while their content still
    # decodes byte-exactly -- measured: 'AZS Dream State Vol.2.syx') and do
    # not mask a proven layout.
    stats["ok"] = (stats["byteMismatches"] == 0
                   and stats["crossParseDisagreements"] == 0)
    return stats


def print_fx_summary(stats: dict) -> None:
    out = sys.stdout
    out.write(f"fx-pages sweep: {stats['root']}"
              f"{' (verify-only)' if stats['verifyOnly'] else ''}\n")
    out.write("files=%d dumps=%d sidecars=%d skipped=%d failed=%d\n"
              % (stats["files"], stats["dumps"], stats["sidecars"],
                 stats["skipped"], stats["failed"]))
    out.write("models TI=%d B/C=%d  checksum ok=%d bad=%d na=%d\n"
              % (stats["model"]["TI"], stats["model"]["B/C"],
                 stats["checksumOk"], stats["checksumBad"],
                 stats["checksumNA"]))
    out.write("byte-match: %d values verified, %d mismatches, "
              "%d cross-parse disagreements\n"
              % (stats["valuesVerified"], stats["byteMismatches"],
                 stats["crossParseDisagreements"]))
    out.write("coverage: %d hole slots (%d with nonzero bytes)\n"
              % (stats["holes"], stats["holesNonZero"]))
    if stats["nonZeroHoleSlots"]:
        slots = sorted(stats["nonZeroHoleSlots"].items(),
                       key=lambda kv: (-kv[1], kv[0]))[:8]
        out.write("  nonzero holes: "
                  + ", ".join("page/idx %s x%d" % (k, v) for k, v in slots)
                  + "\n")
    for model, path in sorted(stats["vocab"].items()):
        out.write("vocab[%s]: %s\n" % (model, path))
    for err in stats["errors"][:20]:
        out.write("error: %s\n" % err)
    if len(stats["errors"]) > 20:
        out.write("  ... and %d more\n" % (len(stats["errors"]) - 20))
    out.write("result: %s\n" % ("PASS" if stats["ok"] else "FAIL"))


# ---------------------------------------------------------------------------
# Morph blueprints
# ---------------------------------------------------------------------------

# CONTINUOUS: values/levels/amounts/depths/rates/frequencies.
_CONTINUOUS_TOKENS = ("amount", "level", "volume", "depth", "rate", "freq",
                      "time", "value", "spread", "balance", "mix", "feedback",
                      "attack", "release", "offset", "speed", "distance",
                      "q factor", "intensity", "gain", "color")
# DISCRETE: types/sources/destinations/modes/on-off -- wins over continuous.
_DISCRETE_TOKENS = ("type", "source", "destination", "dest", "mode",
                    "select", "switch", "shape", "bands", "enable",
                    "name", "version", "category", "clock")


def is_continuous_virus(key: str) -> bool:
    """Morph classification per the established distance convention."""
    low = key.lower()
    if any(tok in low for tok in _DISCRETE_TOKENS):
        return False
    return any(tok in low for tok in _CONTINUOUS_TOKENS)


def build_virus_morphs(sheet_path: str, pairs_spec: str, steps: int,
                       out_path: str) -> dict:
    """Morph-chain blueprint sheet via morph_presets (no engine edits).

    Uses the public continuous_fn hook for the virus classification and
    marks every generated preset appliesVia=program_writer_pending (virus
    apply is CC0/PC program loads; parameter-level injection needs a future
    Virus SysEx writer) and unverified: true.
    """
    import morph_presets as MP

    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    doc = MP.build_morph_sheet(sheet, MP.parse_pairs(pairs_spec), steps,
                               sheet_path, continuous_fn=is_continuous_virus)
    for pair in doc["pairs"]:
        for step in pair["steps"]:
            step["preset"]["appliesVia"] = MORPH_APPLIES_VIA
    payload = MP.render_json(doc)
    parent = os.path.dirname(os.path.abspath(out_path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(payload)
    return {
        "out": out_path,
        "pairs": ["%s d=%.4f steps=%d" % (p["pair"], p["distance"],
                                          len(p["steps"]))
                  for p in doc["pairs"]],
        "steps": sum(len(p["steps"]) for p in doc["pairs"]),
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description="Virus FX/mod-matrix page decoder (sidecar enrichment).")
    ap.add_argument("--sweep", metavar="DIR", action="append", default=[],
                    help="re-sweep <patch>.virus.json sidecars under DIR")
    ap.add_argument("--role", metavar="R", default=None,
                    help="role for the regenerated sidecars' roleCheck")
    ap.add_argument("--verify-only", action="store_true",
                    help="verify byte-match/checksums, write nothing")
    ap.add_argument("--dump", metavar="FILE", help="decode one file (debug)")
    ap.add_argument("--morphs", action="store_true",
                    help="generate a morph blueprint sheet")
    ap.add_argument("--sheet", metavar="PATH", help="matrix preset sheet")
    ap.add_argument("--pairs", metavar="SPEC", help="pair list like '0:3,2:7'")
    ap.add_argument("--steps", type=int, default=4)
    ap.add_argument("--out", metavar="PATH")
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    if args.dump:
        with open(args.dump, "rb") as fh:
            data = fh.read()
        fmt = vp.detect_format(data, args.dump)
        if fmt is None:
            print("unrecognized format", file=sys.stderr)
            return 2
        vocab = Vocabulary()
        for dump in iter_dumps(data, fmt)[:3]:
            model = payload_model(len(dump["payload"]))
            table = vocab.table(model or "")
            values, holes = decode_payload(dump["payload"], table)
            print("format=%s model=%s name=%r payload=%d values=%d holes=%d"
                  % (fmt, model, dump["name"], len(dump["payload"]),
                     len(values), len(holes)))
            for name, value in sorted(fx_params(values, table).items()):
                print("  %-34s %3d" % (name, value))
        return 0

    if args.morphs:
        if not (args.sheet and args.pairs and args.out):
            print("--morphs needs --sheet/--pairs/--out", file=sys.stderr)
            return 2
        summary = build_virus_morphs(args.sheet, args.pairs, args.steps,
                                     args.out)
        for line in summary["pairs"]:
            print("pair %s" % line)
        print("wrote %s (%d pairs, %d intermediate steps)"
              % (summary["out"], len(summary["pairs"]), summary["steps"]))
        return 0

    if not args.sweep:
        ap.print_help()
        return 2
    rc = 0
    for root in args.sweep:
        if not os.path.isdir(root):
            print("not a directory: %s" % root, file=sys.stderr)
            return 2
        stats = sweep_virus_fx(root, role=args.role,
                               verify_only=args.verify_only)
        print_fx_summary(stats)
        if not stats["ok"]:
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
