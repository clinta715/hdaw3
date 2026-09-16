#!/usr/bin/env python3
"""pytest suite for timbre-lib/je8086_patch.py (Roland JP-8080 decoder).

Synthetic DT1 banks exercise the format rules the decoder relies on; real
library spot checks skip when D:/pdf/je8086 is not mounted.
"""

import json
import os
import pathlib
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import je8086_patch as j  # noqa: E402

LIB = "/mnt/d/pdf/je8086"
if not os.path.isdir(LIB):
    LIB = "D:/pdf/je8086"           # run from the Windows interpreter too


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def dt1(addr, data):
    a = list(addr)
    cks = (128 - ((sum(a) + sum(data)) % 128)) % 128
    return bytes([0xF0, 0x41, 0x10, 0x00, 0x06, 0x12] + a + list(data) + [cks, 0xF7])


def varint(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def smf_wrap(stream):
    """Wrap a raw SysEx stream in a minimal SMF (F0 events with varint lengths)."""
    body = bytearray()
    i = 0
    while True:
        start = stream.find(j.F0, i)
        if start < 0:
            break
        end = stream.find(j.F7, start)
        if end < 0:
            break
        payload = stream[start + 1:end + 1]     # length includes the F7
        body += b"\x00" + j.F0 + varint(len(payload)) + payload
        i = end + 1
    header = b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big") + (96).to_bytes(2, "big")
    track = b"MTrk" + len(body).to_bytes(4, "big") + bytes(body)
    return header + track


def patch_body(name="MY PATCH", params=None):
    blob = bytearray(0x200)
    blob[0] = 0x00                      # the dump's leading byte
    nm = name.encode("latin-1")[:j.NAME_LEN].ljust(j.NAME_LEN, b" ")
    blob[1:1 + j.NAME_LEN] = nm
    for off, val in (params or {}).items():
        blob[1 + off] = val
    return bytes(blob)


def addr_patch(rel):
    return (2, (rel // 128) % 128, rel % 128)


def bank_stream(patches, two_pages=False):
    """patches: list of (slot0, blob).  Returns a raw .syx stream."""
    out = bytearray()
    for slot0, blob in patches:
        rel = slot0 * j.PATCH_STRIDE_PAGES
        out += dt1(addr_patch(rel), blob[:j.PAGE])
        if two_pages and any(blob[j.PAGE:]):
            out += dt1(addr_patch(rel + 1), blob[j.PAGE:])
    return bytes(out)


# --------------------------------------------------------------------------- #
# format rules
# --------------------------------------------------------------------------- #
def test_page_value_and_area_math():
    assert j.page_value((2, 0, 0)) == 2 << 14
    assert j.page_value((3, 0, 64)) == (3 << 14) + 64
    assert j.area_of(2 << 14) == ("patch", 0, 1, 0)
    assert j.area_of((2 << 14) + 2) == ("patch", 0, 2, 0)
    assert j.area_of((2 << 14) + 128) == ("patch", 1, 1, 0)
    assert j.area_of((2 << 14) + 127) == ("patch", 0, 64, 1)
    assert j.area_of((3 << 14)) == ("performance-common", 1, 0, 0)
    assert j.area_of((3 << 14) + 64) == ("performance-patch", 1, 1, 0)
    assert j.area_of((3 << 14) + 128) == ("performance-common", 2, 0, 0)
    assert j.area_of((3 << 14) + 129) == ("performance-common", 2, 0, 1)
    assert j.area_of((3 << 14) + 128 + 64) == ("performance-patch", 2, 1, 0)
    assert j.area_of(0) == ("other", 0, 0, 0)


def test_checksum_validation_flags_corruption():
    good = dt1(addr_patch(0), patch_body("OK PATCH")[:j.PAGE])
    assert j.parse_dt1(list(j.iter_sysex_blocks(good))[0])["checksumOk"] is True
    corrupt = bytearray(good)
    corrupt[20] ^= 0x01
    assert j.parse_dt1(list(j.iter_sysex_blocks(bytes(corrupt)))[0])["checksumOk"] is False


def test_smf_varint_prefix_is_stripped():
    raw = bank_stream([(0, patch_body("SMF PATCH", {0x1E: 0}))], two_pages=True)
    wrapped = smf_wrap(raw)
    assert len(wrapped) > len(raw)
    raw_msgs = [j.parse_dt1(b) for b in j.iter_sysex_blocks(raw)]
    smf_msgs = [j.parse_dt1(b) for b in j.iter_sysex_blocks(wrapped)]
    raw_msgs = [m for m in raw_msgs if m]
    smf_msgs = [m for m in smf_msgs if m]
    assert [m["addr"] for m in smf_msgs] == [m["addr"] for m in raw_msgs]
    assert [m["data"] for m in smf_msgs] == [m["data"] for m in raw_msgs]
    entries = j.build_entries(raw_msgs)
    assert entries and entries[0]["name"] == "SMF PATCH"


def test_name_and_param_offset_convention():
    blob = patch_body("SUPERSAW", {0x1E: 0, 0x2A: 40})
    entry = j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(bank_stream([(0, blob)]))])[0]
    assert entry["name"] == "SUPERSAW"
    assert entry["params"]["Osc1Waveform"]["raw"] == 0
    assert entry["params"]["Osc1Waveform"]["label"] == "SUPER SAW"
    assert entry["params"]["Resonance"]["raw"] == 40
    assert entry["params"]["Osc1Waveform"]["offset"] == "0x1E"
    # a patch with the name at data[0] instead would NOT be recognised
    shifted = bytearray(blob)
    shifted[0:16] = blob[1:17]
    assert j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(bank_stream([(0, bytes(shifted))]))])[0]["name"] != "SUPERSAW"


def test_param_ranges_labels_and_bipolar():
    blob = patch_body("TYPES", {0x27: 2, 0x28: 1, 0x17: 0, 0x3D: 12, 0x21: 3})
    e = j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(bank_stream([(0, blob)]))])[0]
    assert e["params"]["FilterType"]["label"] == "LPF"
    assert e["params"]["CutoffSlope"]["label"] == "-24 dB/oct"
    assert e["params"]["OscillatorBalance"]["signed"] == -64
    assert e["params"]["MultiEffectsType"]["label"] == "DISTORTION"
    assert e["family"] == "noise"


def test_two_page_merge_and_page2_params():
    blob = bytearray(patch_body("PAGED", {0x29: 100}))
    blob[1 + 0x100] = 0x2A                     # ControlAmpLfo1Depth lives on page 2
    stream = bank_stream([(0, bytes(blob))], two_pages=True)
    e = j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(stream)])[0]
    assert e["truncated"] is False
    assert e["pages"] == [0, 1]
    assert e["params"]["ControlAmpLfo1Depth"]["raw"] == 0x2A
    assert e["bytes"] >= j.PAGE + j.NAME_OFFSET


def test_single_page_patch_is_reported_truncated():
    blob = patch_body("SHORTPATCH", {0x29: 60})
    e = j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(bank_stream([(0, blob)]))])[0]
    assert e["truncated"] is True
    assert e["params"]["CutoffFrequency"]["raw"] == 60


def test_performance_common_is_not_decoded_as_patch():
    stream = dt1((3, 0, 0), patch_body("PERF NAME", {0x1E: 0})[:j.PAGE]) + \
             dt1((3, 0, 64), patch_body("PATCH ONE", {0x1E: 0})[:j.PAGE])
    entries = j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(stream)])
    common = [e for e in entries if e["area"] == "performance-common"][0]
    patch = [e for e in entries if e["area"] == "performance-patch"][0]
    assert common["name"] == "PERF NAME"
    assert common["role"] == "performance"
    assert common["params"] == {}
    assert patch["name"] == "PATCH ONE"
    assert patch["slot"] == 1
    assert patch["params"]["Osc1Waveform"]["label"] == "SUPER SAW"


# --------------------------------------------------------------------------- #
# classification
# --------------------------------------------------------------------------- #
def test_role_classification_keywords_and_params():
    bass = patch_body("DEEP BASS", {0x29: 30, 0x47: 1, 0x36: 2, 0x39: 10})
    pad = patch_body("WARM PAD", {0x36: 90, 0x39: 110, 0x29: 80})
    fx = patch_body("RISER FX", {0x21: 3})
    def role(blob):
        return j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(bank_stream([(0, blob)]))])[0]["role"]
    assert role(bass) == "bass"
    assert role(pad) == "pad"
    assert role(fx) == "fx"


def test_placeholder_flag():
    init = patch_body("INIT PATCH")
    e = j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(bank_stream([(0, init)]))])[0]
    assert e["placeholder"] is True


# --------------------------------------------------------------------------- #
# sidecars / survey
# --------------------------------------------------------------------------- #
def test_bank_sidecar_contract_and_stability(tmp_path):
    stream = bank_stream([(0, patch_body("BASS ONE", {0x47: 1, 0x29: 30})),
                          (1, patch_body("LEAD TWO", {0x1E: 0, 0x38: 120}))])
    path = tmp_path / "test bank.syx"
    path.write_bytes(stream)
    entries = j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(stream)])
    bank = j.build_bank(str(path), entries)
    for key in ("schema", "engine", "name", "description", "roleCheck", "unmapped", "mappedParams", "patches"):
        assert key in bank
    assert bank["engine"] == "je8086"
    assert bank["schema"] == j.SCHEMA_BANK
    assert bank["roleCheck"]["verdict"] in ("bass", "lead")
    assert set(bank["mappedParams"]) >= {"slot001", "slot002"}
    assert bank["mappedParams"]["slot001"]["params"]["MonoSwitch"] == 1
    blob = json.dumps(bank, sort_keys=True)
    assert json.dumps(j.build_bank(str(path), entries), sort_keys=True) == blob
    assert str(tmp_path) not in blob


def test_survey_totals_and_duplicates(tmp_path):
    (tmp_path / "a.syx").write_bytes(bank_stream([(0, patch_body("SHARED NAME", {0x29: 20})),
                                                  (1, patch_body("BASS A", {0x47: 1}))]))
    (tmp_path / "b.mid").write_bytes(smf_wrap(bank_stream([(0, patch_body("SHARED NAME", {0x29: 20}))])))
    data = j.survey(str(tmp_path))
    assert data["totals"]["files"] == 2
    assert data["totals"]["entries"] == 3
    assert data["totals"]["checksumFailures"] == 0
    assert data["totals"]["nonInitPatches"] == 3
    assert any(d["name"] == "SHARED NAME" and len(d["banks"]) == 2 for d in data["duplicateNames"])
    assert data["schema"] == j.SCHEMA_SURVEY
    assert str(tmp_path) not in json.dumps(data)


def test_sidecars_and_explode_written(tmp_path):
    (tmp_path / "bank.syx").write_bytes(bank_stream([(0, patch_body("EXPLODE ME", {0x1E: 0}))]))
    rc = j.run_sidecars(str(tmp_path), None, True, True)
    assert rc == 0
    sidecar = tmp_path / ("bank.syx" + j.SYSEX_SUFFIX)
    assert sidecar.exists()
    data = json.loads(sidecar.read_text())
    assert data["engine"] == "je8086"
    exploded = list((tmp_path / "exploded").rglob("*.syx"))
    assert len(exploded) == 1
    # the exploded file must itself parse back to the same patch
    back = j.build_entries([j.parse_dt1(b) for b in j.iter_sysex_blocks(exploded[0].read_bytes())])
    assert back and back[0]["name"] == "EXPLODE ME"
    patch_sidecar = json.loads(pathlib.Path(str(exploded[0]) + j.SYSEX_SUFFIX).read_text())
    assert patch_sidecar["schema"] == j.SCHEMA_PATCH
    assert "description" in patch_sidecar and patch_sidecar["roleCheck"]["verdict"] in (
        "bass", "lead", "pad", "pluck", "fx", "arp", "chord", "other")


# --------------------------------------------------------------------------- #
# real library spot checks (skipped when the bank root is absent)
# --------------------------------------------------------------------------- #
@pytest.mark.skipif(not os.path.isdir(LIB), reason="JP-8080 bank library not mounted")
def test_real_library_spot_checks():
    psy = os.path.join(LIB, "Psytrance.syx")
    dream = os.path.join(LIB, "Dreamwork Vol 1.mid")
    if not (os.path.isfile(psy) and os.path.isfile(dream)):
        pytest.skip("expected bank files missing")
    msgs = j.parse_file(psy)
    assert len(msgs) == 512
    assert all(m["checksumOk"] for m in msgs)
    assert len(j.build_entries(msgs)) == 192
    dw = j.build_entries(j.parse_file(dream))
    assert len(dw) == 64
    assert dw[0]["name"].startswith("Wired")
    assert dw[0]["params"]["Osc1Waveform"]["label"] == "SUPER SAW"


@pytest.mark.skipif(not os.path.isdir(LIB), reason="JP-8080 bank library not mounted")
def test_real_library_checksum_invariants():
    files = j._bank_files(LIB)
    assert len(files) >= 40
    total = 0
    bad = 0
    for path in files:
        for m in j.parse_file(path):
            total += 1
            bad += 0 if m["checksumOk"] else 1
    # 10368 = the verified census AFTER the SMF one-byte-varint fix (2026-09-16);
    # the pre-fix parser dropped 4224 short messages (payloads < 128 bytes).
    assert total == 10368
    assert bad == 0

def test_smf_one_byte_varint_messages_are_kept(tmp_path):
    # A 48-byte payload gets a ONE-byte SMF varint prefix (0x33 < 0x80): the
    # high-bit test cannot see it, so the parser must fall back to stripping one
    # byte. Short messages used to vanish silently.
    stream = dt1((2, 0, 0), patch_body("SHORT")[:48]) + dt1((2, 0, 2), patch_body("LONG")[:256])
    p = tmp_path / "short_messages.syx"
    p.write_bytes(smf_wrap(stream))
    msgs = j.parse_file(str(p))
    ASSERT_COUNT = len(msgs)
    assert ASSERT_COUNT == 2, "short SMF-wrapped DT1 messages must not be dropped"
    assert [m["value"] for m in msgs] == [j.page_value((2, 0, 0)), j.page_value((2, 0, 2))]
    assert all(m["checksumOk"] for m in msgs)
