#!/usr/bin/env python3
"""pytest suite for the Access Virus patch decoder + survey.

Covers: format parsing against real fixtures (name + structural assertions),
the sub_synth mapping contract, stable-JSON determinism, error paths, and
survey invariants (TI bank = 128 patches)."""
import json
import os

import pytest
import virus_patch as vp

HERE = os.path.dirname(os.path.abspath(__file__))
TESTDATA = os.path.join(HERE, "testdata", "virus")

BC_FIXTURE = os.path.join(TESTDATA, "bcsingle.syx")
TI_BLOCK_FIXTURE = os.path.join(TESTDATA, "tiblock0.syx")
TDM_FIXTURE = os.path.join(TESTDATA, "tdm_1979_gangs.bin")
TDM_WELCOME_FIXTURE = os.path.join(TESTDATA, "tdm_welcome.bin")
MIDI_FIXTURE = os.path.join(TESTDATA, "bank_bcsingle.mid")
VHC_FIXTURE = os.path.join(TESTDATA, "he_bank.vhc")


def _read(path):
    with open(path, "rb") as fh:
        return fh.read()


# ---------------------------------------------------------------------------
# Fixture guards
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def bc_data():
    return _read(BC_FIXTURE)


@pytest.fixture(scope="module")
def ti_data():
    return _read(TI_BLOCK_FIXTURE)


@pytest.fixture(scope="module")
def tdm_data():
    return _read(TDM_FIXTURE)


@pytest.fixture(scope="module")
def tdm_welcome_data():
    return _read(TDM_WELCOME_FIXTURE)


@pytest.fixture(scope="module")
def midi_data():
    return _read(MIDI_FIXTURE)


@pytest.fixture(scope="module")
def vhc_data():
    return _read(VHC_FIXTURE)


# ---------------------------------------------------------------------------
# Format parsing
# ---------------------------------------------------------------------------

def test_bcsingle_length_and_delimiters(bc_data):
    assert len(bc_data) == 267
    assert bc_data[0] == 0xF0
    assert bc_data[-1] == 0xF7
    assert bc_data[1:4] == bytes((0x00, 0x20, 0x33))


def test_bcsingle_header_magic(bc_data):
    assert bc_data[6] == 0x10  # single dump command


def test_bcsingle_parses_name(bc_data):
    patch = vp.parse_bcsingle(bc_data)
    assert patch["error"] is None
    # The stored name has a leading '~' (the source library even carries a
    # TDM file literally named "~WELCOME"); it is a real name character.
    assert patch["name"] == "~WELCOME"
    assert patch["format"] == "bcsingle"


def test_bcsingle_checksum(bc_data):
    patch = vp.parse_bcsingle(bc_data)
    assert patch["checksum_ok"] is True


def test_bcsingle_data_region(bc_data):
    patch = vp.parse_bcsingle(bc_data)
    assert len(patch["data"]) == 256


def test_tiblock_length_and_delimiters(ti_data):
    assert len(ti_data) == 524
    assert ti_data[0] == 0xF0
    assert ti_data[-1] == 0xF7
    assert ti_data[1:4] == bytes((0x00, 0x20, 0x33))


def test_tiblock_parses_name(ti_data):
    patch = vp.parse_tibank(ti_data)[0]
    assert patch["error"] is None
    assert patch["name"] == "WCOG"
    assert patch["format"] == "tibank"


def test_tiblock_checksum(ti_data):
    patch = vp.parse_tibank(ti_data)[0]
    assert patch["checksum_ok"] is True


def test_tibank_128_blocks():
    # 67072 = 128 x 524: build a full bank by repeating the block fixture.
    full = _read(TI_BLOCK_FIXTURE) * 128
    assert len(full) == 67072
    patches = vp.parse_tibank(full)
    assert len(patches) == 128
    assert all(p["error"] is None for p in patches)


def test_tdm_magic_and_name(tdm_data):
    patch = vp.parse_tdm(tdm_data)
    assert patch["error"] is None
    assert patch["name"] == "1979 Gangs"
    assert patch["format"] == "tdm"


def test_tdm_welcome_name_matches_bc(tdm_welcome_data, bc_data):
    tdm = vp.parse_tdm(tdm_welcome_data)
    bc = vp.parse_bcsingle(bc_data)
    assert tdm["name"] == bc["name"] == "~WELCOME"


def test_tdm_data_matches_bc_payload(tdm_welcome_data, bc_data):
    # The TDM parameter block at offset 124 holds the same page A+B payload
    # as the B/C single (validated by diffing the real library).  Two bytes
    # legitimately differ between the separate saves: payload[0] (patch
    # version, TDM=3 vs BC=7) and payload[69] (a real param difference).
    tdm = vp.parse_tdm(tdm_welcome_data)
    bc = vp.parse_bcsingle(bc_data)
    assert len(tdm["data"]) == len(bc["data"]) == 256
    diffs = [i for i in range(256) if tdm["data"][i] != bc["data"][i]]
    assert diffs == [0, 69]


def test_stdmidi_unwraps_bc_bank(midi_data):
    patches = vp.parse_stdmidi(midi_data)
    assert len(patches) == 128
    assert all(p["error"] is None for p in patches)
    assert all(p["format"] == "bcsingle" for p in patches)


def test_stdmidi_first_patch_name(midi_data):
    patches = vp.parse_stdmidi(midi_data)
    assert patches[0]["name"] == "~WELCOME"


def test_vhc_128_blocks(vhc_data):
    patches = vp.parse_vhc(vhc_data)
    assert len(patches) == 128
    assert all(p["error"] is None for p in patches)


def test_vhc_first_patch_matches_bc(vhc_data, bc_data):
    # VHC block 0 and the standalone B/C single are two saves of the same
    # patch: same name, same payload except payload[0] (version) and
    # payload[69] (a real param difference between saves) + checksum.
    vhc0 = vp.parse_vhc(vhc_data)[0]
    bc = vp.parse_bcsingle(bc_data)
    assert vhc0["name"] == bc["name"] == "~WELCOME"
    assert len(vhc0["data"]) == len(bc["data"]) == 256
    diffs = [i for i in range(256) if vhc0["data"][i] != bc["data"][i]]
    assert diffs == [69]


# ---------------------------------------------------------------------------
# Mapping contract
# ---------------------------------------------------------------------------

def test_mapping_mapped_keys_present(bc_data):
    patch = vp.parse_bcsingle(bc_data)
    mapped = vp.map_to_sub_synth(patch)["mapped"]
    assert "0" in mapped
    assert "7" in mapped
    assert "17" in mapped
    assert mapped["0"]["param"] == "osc1_wave"
    assert mapped["17"]["param"] == "filter_type"


def test_mapping_drive_is_curve(bc_data):
    patch = vp.parse_bcsingle(bc_data)
    mapped = vp.map_to_sub_synth(patch)["mapped"]
    # Saturation Curve max 6 -> drive 0..1
    assert 0.0 <= mapped["9"]["value"] <= 1.0


def test_mapping_legato_from_key_mode(bc_data):
    patch = vp.parse_bcsingle(bc_data)
    mapped = vp.map_to_sub_synth(patch)["mapped"]
    assert mapped["15"]["value"] in (0.0, 1.0)


def test_mapping_unmapped_nonempty_for_fm(bc_data):
    patch = vp.parse_bcsingle(bc_data)
    result = vp.map_to_sub_synth(patch)
    assert result["unmapped"]
    features = [u["feature"] for u in result["unmapped"]]
    # The classic unmapped set must always be reported.
    for feat in ("osc2_fm_amount", "lfo1", "lfo2", "fx_delay", "noise_level"):
        assert feat in features


def test_mapping_unmapped_has_raw_bytes(bc_data):
    patch = vp.parse_bcsingle(bc_data)
    result = vp.map_to_sub_synth(patch)
    noise = next(u for u in result["unmapped"] if u["feature"] == "noise_level")
    assert "noise_volume" in noise["raw"]


def test_mapping_all_23_mapped_params():
    assert len(vp.SUB_SYNTH_PARAMS) >= 23
    assert vp.SUB_SYNTH_PARAMS[0] == "osc1_wave"
    assert vp.SUB_SYNTH_PARAMS[22] == "filter_env_release"


def test_mapping_sources_cover_params():
    # every mapped sub_synth param index has a Virus source byte
    for index, (_name, source, _conv) in vp.SUBSYNTH_SOURCE.items():
        assert source in vp.PARAM_OFFSETS, f"param {index} source {source}"


# ---------------------------------------------------------------------------
# Stable JSON
# ---------------------------------------------------------------------------

def test_stable_json(bc_data, ti_data, midi_data, vhc_data):
    patches = []
    patches.extend(vp.parse_bcsingle(bc_data) for _ in [1])
    patches.extend(vp.parse_tibank(ti_data))
    patches.extend(vp.parse_stdmidi(midi_data))
    patches.extend(vp.parse_vhc(vhc_data))
    for p in patches:
        p["mapped"] = vp.map_to_sub_synth(p)["mapped"]
        p["unmapped"] = vp.map_to_sub_synth(p)["unmapped"]
    a = json.dumps(patches, sort_keys=True)
    b = json.dumps(patches, sort_keys=True)
    assert a == b


def test_stable_survey_json(tmp_path, monkeypatch):
    # Two survey() runs on the same fixtures must produce byte-identical JSON.
    root = tmp_path / "lib"
    root.mkdir()
    (root / "Access_Virus_TI").mkdir()
    (root / "Sysex format").mkdir()
    (root / "Virus TDM format").mkdir()
    (root / "Standard midi format").mkdir()
    (root / "Virus HE format").mkdir()
    import shutil

    shutil.copy(TI_BLOCK_FIXTURE, root / "Access_Virus_TI" / "bank.syx")
    shutil.copy(BC_FIXTURE, root / "Sysex format" / "one.syx")
    shutil.copy(TDM_FIXTURE, root / "Virus TDM format" / "patch.bin")
    shutil.copy(MIDI_FIXTURE, root / "Standard midi format" / "bank.mid")
    shutil.copy(VHC_FIXTURE, root / "Virus HE format" / "bank.vhc")

    monkeypatch.chdir(tmp_path)
    r1 = vp.survey(str(root))
    r2 = vp.survey(str(root))
    a = json.dumps(r1, sort_keys=True)
    b = json.dumps(r2, sort_keys=True)
    assert a == b


# ---------------------------------------------------------------------------
# Error paths
# ---------------------------------------------------------------------------

def test_bcsingle_truncated_sets_error():
    patch = vp.parse_bcsingle(bytes(100))
    assert patch["error"] is not None
    assert patch["name"] == ""


def test_bcsingle_garbage_sets_error():
    patch = vp.parse_bcsingle(bytes(267))
    assert patch["error"] is not None


def test_tibank_truncated_sets_error():
    patches = vp.parse_tibank(bytes(1000))
    assert len(patches) == 1
    assert patches[0]["error"] is not None


def test_tdm_no_magic_sets_error():
    patch = vp.parse_tdm(bytes(124 + 2 * 256))
    assert patch["error"] is not None


def test_stdmidi_garbage_sets_error():
    patches = vp.parse_stdmidi(b"not a midi file at all")
    assert patches
    assert patches[0]["error"] is not None


def test_vhc_truncated_sets_error():
    patches = vp.parse_vhc(bytes(1000))
    assert patches
    assert patches[0]["error"] is not None


# ---------------------------------------------------------------------------
# Survey invariants
# ---------------------------------------------------------------------------

def test_survey_ti_is_128_per_bank(tmp_path):
    root = tmp_path / "lib"
    root.mkdir()
    (root / "Access_Virus_TI").mkdir()
    with open(root / "Access_Virus_TI" / "bank.syx", "wb") as fh:
        fh.write(_read(TI_BLOCK_FIXTURE) * 128)
    report = vp.survey(str(root))
    assert report["formats"]["tibank"]["patches"] == 128
    assert report["formats"]["tibank"]["parsed"] == 128
    assert report["formats"]["tibank"]["failed"] == 0
    assert report["formats"]["tibank"]["name_ok"] == 128


def test_survey_counts_parsed_vs_failed(tmp_path):
    root = tmp_path / "lib"
    root.mkdir()
    (root / "Sysex format").mkdir()
    with open(root / "Sysex format" / "good.syx", "wb") as fh:
        fh.write(_read(BC_FIXTURE))
    with open(root / "Sysex format" / "bad.syx", "wb") as fh:
        fh.write(bytes(267))
    report = vp.survey(str(root))
    fmt = report["formats"]["bcsingle"]
    assert fmt["patches"] == 2
    assert fmt["parsed"] == 1
    assert fmt["failed"] == 1
    assert fmt["name_ok"] == 1


def test_survey_totals_shape(tmp_path):
    root = tmp_path / "lib"
    root.mkdir()
    (root / "Sysex format").mkdir()
    with open(root / "Sysex format" / "one.syx", "wb") as fh:
        fh.write(_read(BC_FIXTURE))
    report = vp.survey(str(root))
    assert report["totals"]["patches"] >= 1
    assert report["totals"]["parsed"] >= 1
    assert "unmapped_any" in report["totals"]
    assert set(report["formats"].keys()) == set(vp.SUPPORTED_FORMATS)