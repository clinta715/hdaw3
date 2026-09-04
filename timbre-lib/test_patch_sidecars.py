#!/usr/bin/env python3
"""pytest suite for the patch sidecar writers (virus + dx7).

Covers: the virus ``--sidecars`` writer across all 5 supported formats,
stable-JSON determinism, ``--role`` validation against
``role_targets.SUPPORTED_ROLES``, DX7 single + cartridge parsing and
sidecar schemas, and the error paths (empty dir, unreadable file, no
patches found) -- all of which must be recorded, never a crash."""
import json
import os
import shutil

import dx7_patch as dp
import role_targets as RT
import virus_patch as vp

HERE = os.path.dirname(os.path.abspath(__file__))
VIRUS_TESTDATA = os.path.join(HERE, "testdata", "virus")
DX7_TESTDATA = os.path.join(HERE, "testdata", "dx7")

BC_FIXTURE = os.path.join(VIRUS_TESTDATA, "bcsingle.syx")
TI_FIXTURE = os.path.join(VIRUS_TESTDATA, "tiblock0.syx")
TDM_FIXTURE = os.path.join(VIRUS_TESTDATA, "tdm_1979_gangs.bin")
MIDI_FIXTURE = os.path.join(VIRUS_TESTDATA, "bank_bcsingle.mid")
VHC_FIXTURE = os.path.join(VIRUS_TESTDATA, "he_bank.vhc")

DX7_SINGLE_FIXTURE = os.path.join(DX7_TESTDATA, "single.syx")
DX7_CARTRIDGE_FIXTURE = os.path.join(DX7_TESTDATA, "cartridge.syx")

# (src fixture, expected detected format)
VIRUS_FIXTURES = [
    (BC_FIXTURE, "bcsingle"),
    (TI_FIXTURE, "tibank"),
    (TDM_FIXTURE, "tdm"),
    (MIDI_FIXTURE, "stdmidi"),
    (VHC_FIXTURE, "vhc"),
]


def _read(path):
    with open(path, "rb") as fh:
        return fh.read()


def _populate(dest, fixtures, copies=2):
    """Copy the given fixtures into ``dest`` plus ``copies`` extra copies."""
    dest = str(dest)
    for src, _fmt in fixtures:
        base = os.path.basename(src)
        shutil.copy(src, os.path.join(dest, base))
        for i in range(copies):
            stem, ext = os.path.splitext(base)
            shutil.copy(src, os.path.join(dest, f"{stem}_copy{i}{ext}"))


def _copy_names(base, copies):
    """All filenames (incl. copies) produced by _populate for one fixture."""
    stem, ext = os.path.splitext(base)
    names = [base]
    names += [f"{stem}_copy{i}{ext}" for i in range(copies)]
    return names


def _sidecar_for(dest, fixture_name):
    return os.path.join(str(dest), fixture_name + ".virus.json")


# ---------------------------------------------------------------------------
# Virus writer
# ---------------------------------------------------------------------------

def test_virus_sidecar_writer_all_formats(tmp_path):
    _populate(tmp_path, VIRUS_FIXTURES, copies=2)
    summary = vp.sweep_sidecars(str(tmp_path))
    assert summary["totals"]["parsed"] == len(VIRUS_FIXTURES) * 3
    assert summary["totals"]["failed"] == 0
    assert summary["totals"]["sidecars"] == len(VIRUS_FIXTURES) * 3
    for src, fmt in VIRUS_FIXTURES:
        base = os.path.basename(src)
        for name in _copy_names(base, 2):
            side_path = _sidecar_for(tmp_path, name)
            assert os.path.exists(side_path), side_path
            with open(side_path, "r", encoding="utf-8") as fh:
                side = json.load(fh)
            assert side["schema"] == "hdaw.virus.patch.v1"
            assert side["engine"] == "sub_synth"
            assert side["format"] == fmt
            assert side["name"]
            assert isinstance(side["mappedParams"], dict)
            assert len(side["mappedParams"]) >= 20
            assert isinstance(side["unmapped"], list)
            assert "osc2_fm_amount" in side["unmapped"]
            assert isinstance(side["description"], str) and side["description"]


def test_virus_sidecar_names_derive_from_patch_data(tmp_path):
    _populate(tmp_path, [(BC_FIXTURE, "bcsingle")], copies=0)
    vp.sweep_sidecars(str(tmp_path))
    with open(_sidecar_for(tmp_path, os.path.basename(BC_FIXTURE)),
              "r", encoding="utf-8") as fh:
        side = json.load(fh)
    assert side["name"] == "~WELCOME"      # patch-internal name, not the filename
    # bank/program pass through from the parsed fixture (bank 1, program 0)
    parsed = vp.parse_bcsingle(_read(BC_FIXTURE))
    assert side["bank"] == parsed["bank"]
    assert side["program"] == parsed["program"]


def test_virus_sidecar_stable_json(tmp_path):
    a = tmp_path / "a"
    b = tmp_path / "b"
    a.mkdir()
    b.mkdir()
    _populate(a, VIRUS_FIXTURES, copies=1)
    _populate(b, VIRUS_FIXTURES, copies=1)
    vp.sweep_sidecars(str(a))
    vp.sweep_sidecars(str(b))
    for src, _fmt in VIRUS_FIXTURES:
        for name in _copy_names(os.path.basename(src), 1):
            with open(_sidecar_for(a, name), "rb") as fh:
                ba = fh.read()
            with open(_sidecar_for(b, name), "rb") as fh:
                bb = fh.read()
            assert ba == bb, f"{name}.virus.json differs across runs"
    # no absolute path must leak into the sidecar content
    with open(_sidecar_for(a, os.path.basename(BC_FIXTURE)),
              "r", encoding="utf-8") as fh:
        content = fh.read()
    assert str(a) not in content and str(b) not in content


# ---------------------------------------------------------------------------
# --role validation + roleCheck shape
# ---------------------------------------------------------------------------

def test_role_rejected_when_not_supported(tmp_path, capsys):
    _populate(tmp_path, [(BC_FIXTURE, "bcsingle")], copies=0)
    rc = vp.main(["--sidecars", str(tmp_path), "--role", "guitar"])
    assert rc == 1
    err = capsys.readouterr().err.lower()
    assert "role" in err and "guitar" in err


def test_role_alias_accepted(tmp_path):
    _populate(tmp_path, [(BC_FIXTURE, "bcsingle")], copies=0)
    # "sub_bass" is an alias for "bass" -- must validate via normalize_role
    summary = vp.sweep_sidecars(str(tmp_path), role="sub_bass")
    assert summary["totals"]["parsed"] == 1


def test_valid_role_writes_role_check(tmp_path):
    _populate(tmp_path, [(BC_FIXTURE, "bcsingle")], copies=0)
    vp.sweep_sidecars(str(tmp_path), role="bass")
    with open(_sidecar_for(tmp_path, os.path.basename(BC_FIXTURE)),
              "r", encoding="utf-8") as fh:
        side = json.load(fh)
    rc = side["roleCheck"]
    for key in ("role", "verdict", "passed_count", "total_count", "checks",
                "summary"):
        assert key in rc, f"roleCheck missing {key}"
    assert rc["role"] == "bass"
    assert isinstance(rc["passed_count"], int)
    assert isinstance(rc["total_count"], int)
    assert isinstance(rc["checks"], list)
    assert len(rc["checks"]) == rc["total_count"] > 0
    assert rc["verdict"] in ("pass", "fail", "unknown")
    # The check must be the honest param-proxy, documented in a note.
    assert "note" in rc


def test_role_check_unknown_when_no_mapped_params():
    # A patch with no readable bytes cannot produce pseudo-measurements; the
    # roleCheck must stay present but honestly report "unknown".
    patch = {"format": "bcsingle", "name": "X", "data": [], "error": None}
    side = vp.build_sidecar(patch, role="bass")
    assert side["roleCheck"]["verdict"] == "unknown"
    assert side["roleCheck"]["total_count"] == 0


# ---------------------------------------------------------------------------
# DX7 single
# ---------------------------------------------------------------------------

def test_dx7_single_parse():
    voice = dp.parse_single(_read(DX7_SINGLE_FIXTURE))[0]
    assert voice["error"] is None
    assert voice["name"] == "TESTPATCH"
    assert voice["algorithm"] == 5
    assert voice["feedback"] == 3
    assert voice["format"] == "single"
    assert voice["checksum_ok"] is True


def test_dx7_single_sidecar(tmp_path):
    shutil.copy(DX7_SINGLE_FIXTURE, str(tmp_path / "single.syx"))
    summary = dp.write_sidecars(str(tmp_path))
    assert summary["totals"]["parsed"] == 1
    side_path = str(tmp_path / "single.syx.dx7.json")
    assert os.path.exists(side_path)
    with open(side_path, "r", encoding="utf-8") as fh:
        side = json.load(fh)
    assert side["schema"] == "hdaw.dx7.patch.v1"
    assert side["engine"] == "fm_synth"
    assert side["format"] == "single"
    assert side["name"] == "TESTPATCH"
    assert side["algorithm"] == 5
    assert side["feedback"] == 3
    assert isinstance(side["params"], dict) and len(side["params"]) > 0
    assert side["description"] == "DX7 algorithm 5, feedback 3"


# ---------------------------------------------------------------------------
# DX7 cartridge
# ---------------------------------------------------------------------------

def test_dx7_cartridge_parses_32_voices():
    voices = dp.parse_cartridge(_read(DX7_CARTRIDGE_FIXTURE))
    assert len(voices) == 32
    assert all(v["error"] is None for v in voices)
    # first-voice extraction: name/algo/feedback per fixture
    assert voices[0]["name"] == "VOICE00"
    assert voices[0]["algorithm"] == 0
    assert voices[0]["feedback"] == 0
    assert voices[31]["name"] == "VOICE31"
    assert voices[31]["algorithm"] == 31
    assert all(v["checksum_ok"] for v in voices)


def test_dx7_cartridge_sidecar(tmp_path):
    shutil.copy(DX7_CARTRIDGE_FIXTURE, str(tmp_path / "cartridge.syx"))
    summary = dp.write_sidecars(str(tmp_path))
    assert summary["totals"]["parsed"] == 1
    side_path = str(tmp_path / "cartridge.syx.dx7.json")
    assert os.path.exists(side_path)
    with open(side_path, "r", encoding="utf-8") as fh:
        side = json.load(fh)
    assert side["schema"] == "hdaw.dx7.patch.v1"
    assert side["engine"] == "fm_synth"
    assert side["format"] == "cartridge"
    assert side["voiceCount"] == 32
    # sidecar represents the first voice
    assert side["name"] == "VOICE00"
    assert side["algorithm"] == 0
    assert side["params"]["op1_output_level"] == 80


def test_dx7_sidecar_stable_json(tmp_path):
    a = tmp_path / "a"
    b = tmp_path / "b"
    a.mkdir()
    b.mkdir()
    for dest in (a, b):
        shutil.copy(DX7_SINGLE_FIXTURE, str(dest / "single.syx"))
        shutil.copy(DX7_CARTRIDGE_FIXTURE, str(dest / "cartridge.syx"))
        dp.write_sidecars(str(dest))
    for name in ("single.syx.dx7.json", "cartridge.syx.dx7.json"):
        with open(str(a / name), "rb") as fh:
            ba = fh.read()
        with open(str(b / name), "rb") as fh:
            bb = fh.read()
        assert ba == bb, name


# ---------------------------------------------------------------------------
# Error paths
# ---------------------------------------------------------------------------

def test_empty_dir_recorded_no_crash(tmp_path, capsys):
    empty = tmp_path / "empty"
    empty.mkdir()
    summary = vp.sweep_sidecars(str(empty))
    assert summary["totals"]["files"] == 0
    assert summary["totals"]["parsed"] == 0
    rc = vp.main(["--sidecars", str(empty)])
    assert rc == 0
    out = capsys.readouterr().out
    assert "0" in out  # the summary is printed, nothing is silent


def test_no_patches_found_recorded_no_crash(tmp_path, capsys):
    d = tmp_path / "noise"
    d.mkdir()
    (d / "readme.txt").write_text("not a patch\n", encoding="utf-8")
    summary = vp.sweep_sidecars(str(d))
    assert summary["totals"]["files"] == 1
    assert summary["totals"]["parsed"] == 0
    assert summary["totals"]["skipped"] == 1
    rc = vp.main(["--sidecars", str(d)])
    assert rc == 0


def test_unreadable_file_recorded_no_crash(tmp_path, monkeypatch):
    d = tmp_path / "lib"
    d.mkdir()
    shutil.copy(BC_FIXTURE, str(d / "good.syx"))
    bad = str(d / "locked.syx")
    shutil.copy(BC_FIXTURE, bad)

    real_read = vp._read_bytes

    def flaky(path):
        if path == bad:
            raise OSError("simulated read failure")
        return real_read(path)

    monkeypatch.setattr(vp, "_read_bytes", flaky)
    summary = vp.sweep_sidecars(str(d))
    assert summary["totals"]["failed"] == 1
    assert summary["totals"]["parsed"] == 1
    assert os.path.exists(_sidecar_for(d, "good.syx"))


def test_missing_dir_is_hard_error(tmp_path, capsys):
    rc = vp.main(["--sidecars", str(tmp_path / "does_not_exist")])
    assert rc == 1
    assert capsys.readouterr().err


def test_garbage_dx7_syx_recorded(tmp_path):
    d = tmp_path / "dx7"
    d.mkdir()
    # Correct header + byte count, but a checksum byte that fails the sum.
    (d / "garbage.syx").write_bytes(
        b"\xF0\x43\x00\x00\x01\x1B" + bytes(155) + b"\x01\xF7")
    summary = dp.write_sidecars(str(d))
    assert summary["totals"]["files"] == 1
    assert summary["totals"]["parsed"] == 0
    assert summary["totals"]["failed"] == 1
    assert not os.path.exists(str(d / "garbage.syx.dx7.json"))


def test_dx7_empty_dir_recorded_no_crash(tmp_path, capsys):
    empty = tmp_path / "empty"
    empty.mkdir()
    rc = dp.main(["--sidecars", str(empty)])
    assert rc == 0


# ---------------------------------------------------------------------------
# role_targets contract used by the writers
# ---------------------------------------------------------------------------

def test_role_is_in_supported_roles():
    for role in ("kick", "bass", "hat", "snare", "rim", "clap", "lead",
                 "arp", "stab", "pad", "riser", "fx"):
        assert role in RT.SUPPORTED_ROLES