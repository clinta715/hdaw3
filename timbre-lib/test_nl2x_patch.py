"""pytest for nl2x_patch: Clavia Nord Lead 2x decoder + sidecars.

Synthetic dumps are built from nibbles (the format is fully specified by
n2xmiditypes.h); real-library spot checks skip silently when the bank
library is not mounted.
"""
import json
import os

import pytest

import nl2x_patch as NP

BANKS_ROOT = r"D:\pdf\NL2x Banks"


def _build_single(params, msg_type=0, msg_spec=0, name=None):
    """Pack 66 params into a 139/149-byte dump (with F0, with F7)."""
    data = bytearray([0xF0, NP.ID_CLAVIA, 0x0F, NP.ID_N2X, msg_type, msg_spec])
    for v in params:
        data.append(v & 0xF)
        data.append((v >> 4) & 0xF)
    assert len(data) == NP.SINGLE_DUMP_SIZE - 1
    if name is not None:
        raw = name.encode("ascii")[:NP.NAME_LENGTH].ljust(NP.NAME_LENGTH)
        data += raw
    data.append(0xF7)
    return bytes(data)


def _build_multi(first_params, msg_spec=99):
    """Real-bank-shape multi: 8x66 nibble params (1056 data bytes).

    Real bank multis (verified against Alan_M_Nord_Lead_2_v1.0.syx and
    ProgBank0.mid) carry 1056 data bytes = 8 groups of 66 params; part 0's
    single is the first group."""
    data = bytearray([0xF0, NP.ID_CLAVIA, 0x0F, NP.ID_N2X, 1, msg_spec])
    for _group in range(8):
        for v in first_params:
            data.append(v & 0xF)
            data.append((v >> 4) & 0xF)
    data.append(0xF7)
    return bytes(data)


INIT_PARAMS = [0] * 66


def test_constants_match_n2x_header():
    assert NP.SINGLE_DUMP_SIZE == 139
    assert NP.SINGLE_DUMP_WITH_NAME == 149
    assert NP.MULTI_DUMP_SIZE == 715
    assert NP.MULTI_DUMP_BANK_SIZE == 1063


def test_single_dump_roundtrip():
    params = list(range(66))
    dump = _build_single(params)
    assert len(dump) == 139
    patches = NP.parse_syx(dump)
    assert len(patches) == 1
    assert patches[0]["kind"] == "single"
    assert patches[0]["params"] == params
    assert patches[0]["program"] == 0


def test_single_with_name_roundtrip():
    params = [64] * 66
    dump = _build_single(params, name="MY PATCH ")
    assert len(dump) == 149
    patches = NP.parse_syx(dump)
    assert patches[0]["name"] == "MY PATCH"


def test_multi_bank_roundtrip():
    first = list(range(10)) + [0] * 56
    dump = _build_multi(first_params=first)
    assert len(dump) == 1063
    patches = NP.parse_syx(dump)
    assert len(patches) == 1
    assert patches[0]["kind"] == "multi"
    assert patches[0]["program"] == 0
    assert patches[0]["params"] == first


def test_concatenated_syx():
    a = _build_single([1] * 66, msg_type=1, msg_spec=0)
    b = _build_single([2] * 66, msg_type=1, msg_spec=1)
    patches = NP.parse_syx(a + b)
    assert [p["program"] for p in patches] == [0, 1]
    assert [p["params"][0] for p in patches] == [1, 2]


def test_classify_rejects_garbage():
    assert NP.classify(bytes([0x33, 0x0F, 0x04, 0x00, 0x00])) is None
    short = bytes([0xF0, 0x33, 0x0F, 0x04, 0x00, 0x00, 0x01, 0x02, 0xF7])
    assert NP.parse_syx(short) == []


def test_detect_format_guards():
    assert NP.detect_format(b"CcnK....") is None
    assert NP.detect_format(b"GARBAGE") is None
    assert NP.detect_format(_build_single(INIT_PARAMS)) == "nl2xsyx"


def test_sidecar_contract():
    patch = NP.parse_syx(_build_single([100] * 66))[0]
    patch["name"] = "TestPatch"
    side = NP.build_sidecar(patch, format="nl2xsyx")
    assert side["schema"] == "hdaw.nl2x.patch.v1"
    assert side["engine"] == "nodalred2x"
    assert side["name"] == "TestPatch"
    assert side["format"] == "nl2xsyx"
    assert len(side["mappedParams"]) == 66
    assert side["mappedParams"]["3"]["param"] == "cutoff"
    assert side["mappedParams"]["3"]["value"] == pytest.approx(
        100 / 127.0, abs=1e-5)
    assert side["unmapped"] == []
    assert side["description"]
    assert "program" in side


def test_sidecar_role_check():
    patch = NP.parse_syx(_build_single([60] * 66))[0]
    side = NP.build_sidecar(patch, role="bass", format="nl2xsyx")
    assert side["roleCheck"]["role"] == "bass"
    assert "pseudo-measurements" in side["roleCheck"]["note"]
    with pytest.raises(ValueError):
        NP.build_sidecar(patch, role="guitar")


def _write(tmp_path, name, data):
    p = tmp_path / name
    p.write_bytes(data)
    return str(p)


def test_sweep_writes_sidecars(tmp_path):
    _write(tmp_path, "one.syx", _build_single([50] * 66, msg_type=1, msg_spec=5))
    _write(tmp_path, "two.mid", _build_single([70] * 66, msg_type=1, msg_spec=6))
    summary = NP.sweep_sidecars(str(tmp_path))
    assert summary["totals"]["parsed"] == 2
    assert summary["totals"]["sidecars"] == 2
    assert summary["totals"]["failed"] == 0
    for name, prog in (("one.syx", 5), ("two.mid", 6)):
        side = json.loads(
            (tmp_path / (name + ".nl2x.json")).read_text(encoding="utf-8"))
        assert side["schema"] == "hdaw.nl2x.patch.v1"
        assert side["name"] == name.split(".")[0]  # filename fallback
        assert side["program"] == prog


def test_sweep_stable_and_no_path_leak(tmp_path):
    a = tmp_path / "a"
    b = tmp_path / "b"
    a.mkdir()
    b.mkdir()
    for d in (a, b):
        _write(d, "p.syx", _build_single([33] * 66))
    NP.sweep_sidecars(str(a))
    NP.sweep_sidecars(str(b))
    ba = (a / "p.syx.nl2x.json").read_bytes()
    bb = (b / "p.syx.nl2x.json").read_bytes()
    assert ba == bb
    assert str(a) not in ba.decode("utf-8")


def test_survey_shape(tmp_path):
    _write(tmp_path, "x.syx", _build_single([1] * 66))
    _write(tmp_path, "bank.syx", _build_multi([2] * 66))
    _write(tmp_path, "skip.fxb", b"CcnK0000")
    report = NP.survey(str(tmp_path))
    assert report["schema"] == "hdaw.nl2x.survey.v1"
    assert report["totals"]["dumps"] == 2
    assert report["kinds"]["single"] == 1
    assert report["kinds"]["multi"] == 1
    assert report["formats"]["nl2xsyx"]["files"] == 2


def test_pseudo_measurements_keys():
    keys = set(NP.pseudo_measurements([64] * 66))
    assert {"centroid", "mel_low", "mel_mid", "mel_high",
            "attack_s", "decay_s"} <= keys


# ---------------------------------------------------------------------------
# Real-library spot checks (skipped when the bank root is absent)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not os.path.isdir(BANKS_ROOT), reason="banks not mounted")
def test_real_syx_single():
    p = os.path.join(BANKS_ROOT, "BoBSwanS Random Rework NL2 Patches",
                     "Alarm(Use Mod wheel+Velocity).syx")
    if not os.path.isfile(p):
        pytest.skip("fixture file missing")
    patches = NP.parse_file(open(p, "rb").read())
    assert patches and all(len(x["params"]) == 66 for x in patches)


@pytest.mark.skipif(not os.path.isdir(BANKS_ROOT), reason="banks not mounted")
def test_real_mid_factory_bank():
    p = os.path.join(BANKS_ROOT, "NL2x Factory", "ProgBank0.mid")
    if not os.path.isfile(p):
        pytest.skip("fixture file missing")
    patches = NP.parse_file(open(p, "rb").read())
    singles = [x for x in patches if x["kind"] == "single"]
    multis = [x for x in patches if x["kind"] == "multi"]
    assert len(singles) == 99
    assert len(multis) == 10
