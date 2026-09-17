#!/usr/bin/env python3
"""pytest suite for virus_fx_pages.py (FX / mod-matrix page decoder).

Covers: the handoff anchors (Assign1-N source/destination page-113 indices,
Chorus/Type, Ringmodulator Volume, vocoder block), the checksum rule against
synthetic dumps, the STOP-GATE byte-match protocol (decode -> rebuild must be
byte-identical; independent container walker must agree with virus_patch),
the SMF walker regressions (MThd length at offset 4; 0xFF meta-type byte
before the length VLQ), fx/continuous classification, the sidecarRev-2
sidecar contract (rev-1 keys preserved + fxModel/fxParams/fxCoverage),
sweep determinism, the CLI paths, and the morph blueprint builder.

Run::

    python3 -m pytest timbre-lib/test_virus_fx_pages.py -q
"""
import json
import os
import struct

import pytest

import virus_fx_pages as vfp
import virus_patch as vp

HERE = os.path.dirname(os.path.abspath(__file__))
TESTDATA = os.path.join(HERE, "testdata", "virus")

BC_FIXTURE = os.path.join(TESTDATA, "bcsingle.syx")
TI_FIXTURE = os.path.join(TESTDATA, "tiblock0.syx")
TDM_FIXTURE = os.path.join(TESTDATA, "tdm_1979_gangs.bin")
MIDI_FIXTURE = os.path.join(TESTDATA, "bank_bcsingle.mid")


def _read(path):
    with open(path, "rb") as fh:
        return fh.read()


@pytest.fixture(scope="module")
def bc_data():
    return _read(BC_FIXTURE)


@pytest.fixture(scope="module")
def ti_data():
    return _read(TI_FIXTURE)


@pytest.fixture(scope="module")
def tdm_data():
    return _read(TDM_FIXTURE)


@pytest.fixture(scope="module")
def midi_data():
    return _read(MIDI_FIXTURE)


@pytest.fixture(scope="module")
def vocab():
    v = vfp.Vocabulary()
    if not v._tables:            # emulator sources not present on this box
        pytest.skip("gearmulator parameterDescriptions not available")
    return v


# ---------------------------------------------------------------------------
# Handoff anchors (docs/handoffs/2026-09-16-matrix-presets-handoff.md)
# ---------------------------------------------------------------------------

def test_anchor_assign_sources_and_destinations(vocab):
    # "Assign1 Source = page 113 index 64, Destination 65,
    #  Assign2 Source 67, Assign3 Source 72"
    assert vocab.anchor("TI", 113, 64) == "Assign1 Source"
    assert vocab.anchor("TI", 113, 65) == "Assign1 Destination"
    assert vocab.anchor("TI", 113, 67) == "Assign2 Source"
    assert vocab.anchor("TI", 113, 72) == "Assign3 Source"
    # the same anchor block exists for the B/C vocabulary
    assert vocab.anchor("B/C", 113, 64) == "Assign1 Source"
    assert vocab.anchor("B/C", 113, 65) == "Assign1 Destination"


def test_anchor_assign1_amount_between_source_and_destination(vocab):
    assert vocab.anchor("TI", 113, 66) == "Assign1 Amount"


def test_anchor_chorus_type_ti(vocab):
    assert vocab.anchor("TI", 112, 103) == "Chorus/Type"


def test_anchor_ringmodulator_volume_both_generations(vocab):
    assert vocab.anchor("B/C", 112, 38) == "Ringmodulator Volume"
    assert vocab.anchor("TI", 112, 50) == "Ringmodulator Volume"


def test_anchor_vocoder_block(vocab):
    # B/C vocoder lives on page 113 idx 39+; the TI json aliases the same
    # page-112 bytes as Vocoder/* carriers.
    assert vocab.anchor("B/C", 113, 39) == "Vocoder Mode"
    assert vocab.anchor("TI", 113, 39) == "Vocoder Mode"
    assert vocab.anchor("TI", 112, 40) == "Vocoder/Carrier Center Frequency"


def test_vocab_tables_are_loaded(vocab):
    assert len(vocab.table("TI")) >= 500
    assert len(vocab.table("B/C")) >= 300
    assert "parameterDescriptions_TI" in vocab.sources["TI"]
    assert "parameterDescriptions_C" in vocab.sources["B/C"]


# ---------------------------------------------------------------------------
# FX / morph classification
# ---------------------------------------------------------------------------

def test_is_fx_param_members():
    for name in ("Assign1 Source", "Assign 4 Destination", "Chorus/Type",
                 "Delay Mode", "Ringmodulator Volume", "Vocoder Mode",
                 "Distortion Intensity", "HighEQ Gain", "Filter Bank Type",
                 "Lfo1 Mode", "Lfo3 Destination", "Lfo 2 Env Mode"):
        assert vfp.is_fx_param(name), name


def test_is_fx_param_non_members():
    for name in ("Osc1 Wave", "Patchname", "Category", "Version",
                 "Filter Cutoff", "Env1 Attack", "singlename"):
        assert not vfp.is_fx_param(name), name


def test_morph_classification_continuous_vs_discrete():
    # CONTINUOUS: values/levels/amounts/depths/rates/frequencies
    for name in ("Assign1 Amount", "Chorus Mix", "Delay Feedback",
                 "Lfo Speed", "Ringmodulator Volume", "Distortion Intensity"):
        assert vfp.is_continuous_virus(name), name
    # DISCRETE: types/sources/destinations/modes -- wins over continuous
    for name in ("Chorus/Type", "Assign1 Source", "Assign 4 Destination",
                 "Vocoder Mode", "Delay Clock", "Lfo1 Shape"):
        assert not vfp.is_continuous_virus(name), name


# ---------------------------------------------------------------------------
# Checksum rule (cited: dump json {"type": "checksum", "first": 5, ...})
# ---------------------------------------------------------------------------

def test_checksum_rule_bcsingle(bc_data):
    # (dev + 0x10 + bank + prog + sum(payload)) & 0x7F stored at byte 265
    assert vfp.verify_checksum(bc_data, vfp.PAYLOAD_BC) is True
    forged = bytearray(bc_data)
    forged[265] ^= 0x01
    assert vfp.verify_checksum(bytes(forged), vfp.PAYLOAD_BC) is False


def test_checksum_rule_tibank(ti_data):
    assert vfp.verify_checksum(ti_data, vfp.PAYLOAD_TI) is True
    forged = bytearray(ti_data)
    forged[9] ^= 0x01                      # corrupt a payload byte
    assert vfp.verify_checksum(bytes(forged), vfp.PAYLOAD_TI) is False


def test_checksum_formula_matches_documented_region(bc_data):
    # sum(bytes 5..last) & 0x7F, with the 0x10 command byte at offset 6:
    # dev(5) + cmd(6) + bank(7) + prog(8) + payload(9..9+256)
    expected = (bc_data[5] + bc_data[6] + bc_data[7] + bc_data[8]
                + sum(bc_data[9:9 + 256])) & 0x7F
    assert bc_data[265] == expected
    assert vfp.verify_checksum(bc_data, vfp.PAYLOAD_BC) is True


def test_checksum_na_for_tdm(tdm_data):
    # TDM chunks carry no checksum byte -> None (reported checksum-NA)
    assert vfp.verify_checksum(b"", vfp.PAYLOAD_BC) is None
    dump = vfp.iter_dumps(tdm_data, "tdm")[0]
    assert vfp.verify_checksum(vfp._dump_bytes(tdm_data, "tdm", dump),
                               len(dump["payload"])) is None


# ---------------------------------------------------------------------------
# STOP-GATE: independent container walker + byte-match
# ---------------------------------------------------------------------------

def test_iter_dumps_agrees_with_parser_bcsingle(bc_data):
    dumps = vfp.iter_dumps(bc_data, "bcsingle")
    assert len(dumps) == 1
    patch = vp.parse_bcsingle(bc_data)
    assert dumps[0]["payload"] == bytes(patch["data"])
    assert dumps[0]["name"] == patch["name"] == "~WELCOME"


def test_iter_dumps_agrees_with_parser_tibank(ti_data):
    dumps = vfp.iter_dumps(ti_data, "tibank")
    patch = vp.parse_tibank(ti_data)[0]
    assert len(dumps) == 1
    assert dumps[0]["payload"][:256] == bytes(patch["data"])
    assert dumps[0]["name"] == patch["name"] == "WCOG"


def test_iter_dumps_agrees_with_parser_stdmidi(midi_data):
    dumps = vfp.iter_dumps(midi_data, "stdmidi")
    ok = [p for p in vp.parse_stdmidi(midi_data) if not p.get("error")]
    assert len(dumps) == len(ok) == 128
    for dump, patch in zip(dumps, ok):
        assert dump["payload"][:256] == bytes(patch["data"])


def test_byte_match_decode_rebuilds_payload_identically():
    payload = bytes((i * 7 + 3) & 0x7F for i in range(512))
    table = {(112 + off // 128, off % 128): "p%03d" % off
             for off in range(512)}
    values, holes = vfp.decode_payload(payload, table)
    assert not holes
    assert vfp.byte_mismatches(payload, values) == 0


def test_byte_match_detects_a_corrupted_rebuild():
    payload = bytes(512)
    table = {(112, 0): "x"}
    values, _holes = vfp.decode_payload(payload, table)
    assert vfp.byte_mismatches(payload, values) == 0
    values[(112, 0)] = 1                    # decoded value no longer matches
    assert vfp.byte_mismatches(payload, values) == 1


def test_payload_model_and_pages():
    assert vfp.payload_model(256) == "B/C"
    assert vfp.payload_model(512) == "TI"
    assert vfp.payload_model(100) is None
    assert vfp.model_pages("B/C") == (112, 113)
    assert vfp.model_pages("TI") == (112, 113, 114, 115)


def test_name_bytes_pin_base_page_at_112():
    # The 10-char name sits at payload 240..249 (page 113 idx 112..121) in
    # BOTH generations -- a page-110 layout would put it at 496.
    payload = bytearray(512)
    name = b"WCOG"
    payload[240:240 + 10] = name.ljust(10)
    table = {(113, 112 + i): "name%d" % i for i in range(10)}
    values, _holes = vfp.decode_payload(bytes(payload), table)
    assert [values[(113, 112 + i)] for i in range(4)] == list(name)


# ---------------------------------------------------------------------------
# SMF walker regressions
# ---------------------------------------------------------------------------

def _smf_with_events(events):
    """Build a well-formed format-0 SMF from raw event bytes."""
    track = b"".join(events)
    mtrk = b"MTrk" + struct.pack(">I", len(track)) + track
    return (b"MThd" + struct.pack(">I", 6)      # header data length
            + struct.pack(">HHH", 0, 1, 480)    # format, ntrk, division
            + mtrk)


def test_smf_walker_skips_ff_meta_type_byte(bc_data):
    # FF 20 01 00 (port meta) then one B/C single then end-of-track.
    # Regression: the meta-TYPE byte precedes the length VLQ; treating the
    # type as a length mispositions the walker and loses every message.
    sysex = bc_data

    def _vlq(value):
        out = bytes([value & 0x7F])
        value >>= 7
        while value:
            out = bytes([(value & 0x7F) | 0x80]) + out
            value >>= 7
        return out

    # SMF sysex event: F0 status, then a length counting the bytes AFTER
    # F0 (the trailing F7 included) -- the walker reassembles the F0.
    events = (b"\x00\xff \x01\x00"          # delta + port meta
              + b"\x00\xf0" + _vlq(len(sysex) - 1) + sysex[1:]
              + b"\x00\xff\x2f\x00")          # delta + end of track
    data = _smf_with_events([events])
    msgs = vfp.smf_sysex_messages(data)
    assert len(msgs) == 1
    assert msgs[0] == sysex
    dumps = vfp.iter_dumps(data, "stdmidi")
    assert len(dumps) == 1
    assert dumps[0]["payload"] == bytes(vp.parse_bcsingle(bc_data)["data"])


def test_smf_walker_honours_header_length():
    # MThd length is at offset 4 (not 8); a non-standard 8-byte header must
    # still find the MTrk chunk.  Regression: pos used to be 8+4+header_len.
    track = b"\x00\xff\x2f\x00"
    body = b"MTrk" + struct.pack(">I", len(track)) + track
    data = (b"MThd" + struct.pack(">I", 8) + b"\x00\x00\x00\x01\x01\xe0\x00\x00"
            + body)
    msgs = vfp.smf_sysex_messages(data)
    assert msgs == []


def test_smf_walker_collects_sysex_bank(midi_data):
    msgs = vfp.smf_sysex_messages(midi_data)
    assert len(msgs) == 128
    assert all(m[0] == 0xF0 and m[-1] == 0xF7 for m in msgs)


def test_dump_bytes_recovers_raw_dump_regions(bc_data, ti_data,
                                              midi_data):
    d_bc = vfp.iter_dumps(bc_data, "bcsingle")[0]
    assert vfp._dump_bytes(bc_data, "bcsingle", d_bc) == bc_data
    d_ti = vfp.iter_dumps(ti_data, "tibank")[0]
    assert vfp._dump_bytes(ti_data, "tibank", d_ti) == ti_data
    d_mid = vfp.iter_dumps(midi_data, "stdmidi")[0]
    assert d_mid and len(vfp._dump_bytes(midi_data, "stdmidi", d_mid)) >= 267


# ---------------------------------------------------------------------------
# Sidecar sweep contract (sidecarRev 2)
# ---------------------------------------------------------------------------

REV1_KEYS = ("schema", "name", "engine", "format", "mappedParams", "unmapped",
             "description")


def _sweep_tmp(tmp_path):
    (tmp_path / "bcsingle.syx").write_bytes(_read(BC_FIXTURE))
    (tmp_path / "tiblock0.syx").write_bytes(_read(TI_FIXTURE))
    (tmp_path / "tdm.bin").write_bytes(_read(TDM_FIXTURE))
    return vfp.sweep_virus_fx(str(tmp_path), role="bass")


def test_sweep_writes_rev2_sidecars_with_full_contract(tmp_path):
    stats = _sweep_tmp(tmp_path)
    assert stats["ok"] is True
    assert stats["byteMismatches"] == 0
    assert stats["crossParseDisagreements"] == 0
    assert stats["sidecars"] == 3
    sides = {}
    for fn in os.listdir(tmp_path):
        if fn.endswith(".virus.json"):
            sides[fn] = json.loads((tmp_path / fn).read_text("utf-8"))
    assert len(sides) == 3
    for side in sides.values():
        # every rev-1 contract key survives
        for key in REV1_KEYS:
            assert key in side, key
        assert side["roleCheck"]            # --role was given
        assert side["sidecarRev"] == 2
        assert side["fxModel"] in ("TI", "B/C")
        assert isinstance(side["fxParams"], dict) and side["fxParams"]
        cov = side["fxCoverage"]
        assert cov["byteMatch"] == "pass"
        assert cov["verifiedValues"] == cov["covered"] > 0
        assert cov["checksum"] in ("ok", "na")
    # TDM reports checksum-NA honestly
    tdm_side = sides["tdm.bin.virus.json"]
    assert tdm_side["fxCoverage"]["checksum"] == "na"
    assert tdm_side["fxModel"] == "B/C"


def test_sweep_sidecar_fx_params_carry_named_matrix_values(tmp_path):
    _sweep_tmp(tmp_path)
    side = json.loads((tmp_path / "tiblock0.syx.virus.json").read_text("utf-8"))
    assert side["fxModel"] == "TI"
    for key in ("Assign1 Source", "Assign1 Destination", "Chorus/Type"):
        assert key in side["fxParams"], key
        assert 0 <= side["fxParams"][key] <= 127


def test_sweep_is_deterministic(tmp_path):
    _sweep_tmp(tmp_path)
    first = {fn: (tmp_path / fn).read_bytes()
             for fn in os.listdir(tmp_path) if fn.endswith(".virus.json")}
    for fn in list(first):
        (tmp_path / fn).unlink()
    _sweep_tmp(tmp_path)
    for fn, blob in first.items():
        assert (tmp_path / fn).read_bytes() == blob


def test_sweep_verify_only_writes_nothing(tmp_path):
    (tmp_path / "bcsingle.syx").write_bytes(_read(BC_FIXTURE))
    stats = vfp.sweep_virus_fx(str(tmp_path), verify_only=True)
    assert stats["ok"] is True
    assert stats["sidecars"] == 0
    assert not [f for f in os.listdir(tmp_path) if f.endswith(".virus.json")]


def test_sweep_survives_garbage_file(tmp_path):
    (tmp_path / "bcsingle.syx").write_bytes(_read(BC_FIXTURE))
    (tmp_path / "junk.bin").write_bytes(bytes(4096))
    (tmp_path / "truncated.syx").write_bytes(bytes(100))
    stats = vfp.sweep_virus_fx(str(tmp_path))
    assert stats["ok"] is True
    assert stats["sidecars"] == 1
    assert stats["skipped"] == 2


# ---------------------------------------------------------------------------
# CLI + morph blueprint builder
# ---------------------------------------------------------------------------

def test_cli_verify_only_and_dump(tmp_path, capsys):
    (tmp_path / "bcsingle.syx").write_bytes(_read(BC_FIXTURE))
    assert vfp.main(["--sweep", str(tmp_path), "--verify-only"]) == 0
    assert "PASS" in capsys.readouterr().out
    assert vfp.main(["--dump", BC_FIXTURE]) == 0
    assert "Assign" in capsys.readouterr().out or "model=B/C" in \
        capsys.readouterr().out


def test_cli_sweep_failure_returns_nonzero(tmp_path, capsys):
    # A layout that cannot prove byte-exactness must fail the sweep.
    import virus_fx_pages as m
    (tmp_path / "x.syx").write_bytes(_read(BC_FIXTURE))
    orig = m.byte_mismatches
    m.byte_mismatches = lambda payload, values: 1
    try:
        assert m.main(["--sweep", str(tmp_path), "--verify-only"]) == 1
        assert "FAIL" in capsys.readouterr().out
    finally:
        m.byte_mismatches = orig


def test_build_virus_morphs_sheet(tmp_path):
    sheet = {
        "schema": "hdaw.matrix.preset.v1",
        "engine": "virus",
        "presets": [
            {"id": "a" * 16, "name": "A", "role": "mod-matrix",
             "params": {"Assign1 Amount": 0, "Chorus Mix": 0,
                        "Chorus/Type": 1, "Assign1 Source": 3}},
            {"id": "b" * 16, "name": "B", "role": "mod-matrix",
             "params": {"Assign1 Amount": 127, "Chorus Mix": 127,
                        "Chorus/Type": 1, "Assign1 Source": 21}},
        ],
    }
    sheet_path = tmp_path / "virus.json"
    sheet_path.write_text(json.dumps(sheet), encoding="utf-8")
    out_path = tmp_path / "virus_morphs.json"
    summary = vfp.build_virus_morphs(str(sheet_path), "0:1", 3, str(out_path))
    doc = json.loads(out_path.read_text("utf-8"))
    assert doc["schema"] == "hdaw.matrix.preset.morph.v1"
    assert len(doc["pairs"]) == 1
    steps = doc["pairs"][0]["steps"]
    assert len(steps) == 3
    assert summary["steps"] == 3
    # CONTINUOUS interpolates; DISCRETE anchors to A and is listed in jumps
    mid = steps[1]["preset"]
    assert mid["params"]["Assign1 Amount"] == round(0 + (127 - 0) * 0.5, 1)
    assert mid["params"]["Chorus/Type"] == 1          # discrete: anchored
    assert "Chorus/Type" not in mid["jumps"]          # A == B -> no jump
    assert "Assign1 Source" in mid["jumps"]           # A != B -> jump listed
    for step in steps:
        assert step["preset"]["appliesVia"] == vfp.MORPH_APPLIES_VIA
        assert step["preset"]["unverified"] is True
    assert doc["pairs"][0]["distance"] > 0
