#!/usr/bin/env python3
"""Tests for vavra_dump.py (microQ single-dump writer + vavra morph chains).

Covers the dump writer (392-byte invariants, round-trip -- only intended
offsets differ, name/category encoding, 0..127 + offset validation), the
vavra engine profile (named-key token classification, value-dependent
off_<N> rule with the <= 24 boundary, interpolation/jumps), parent-syx
resolution (exact/prefix/ambiguity/byte-verification), auto pair selection,
CLI dump-writer + morph modes (byte-stable re-run, no absolute paths, error
paths), and the committed deliverable (vavra_morphs.json: schema, 5 pairs x
4 steps, per-step 392-int sysex re-derived independently from the real
parent dumps, byte-for-byte CLI reproduction).  Also pins the md5 of the
neighbor artifacts the change must not touch.

Run::

    PYTHONPATH=/tmp/hdawpylib python3 -m pytest timbre-lib/test_vavra_dump.py -q
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys

import pytest

import morph_presets as mp
import vavra_dump as vd

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(os.path.dirname(os.path.abspath(vd.__file__)),
                    "vavra_dump.py")
SHEET_REL = os.path.join("timbre-lib", "matrix_presets", "vavra.json")
OUT_REL = os.path.join("timbre-lib", "matrix_presets", "vavra_morphs.json")
MAP_REL = os.path.join("timbre-lib", "matrix_presets",
                       "vavra_offset_map.json")
SYX_ROOT = os.environ.get("HDAW_VAVRA_SYX_ROOT",
                          "/mnt/d/pdf/rhythm-lab.com_waldorf_micro_q")
DELIVERABLE_PAIRS = ["25:32", "8:36", "23:38", "23:39", "38:39"]
DELIVERABLE_STEPS = 4

# neighbor artifacts pinned at their current digests (xenia_morphs.json
# re-pinned after the 2026-09-18 .mid off-by-2 regeneration; vavra stays
# untouched).
PINNED_MD5 = {
    "timbre-lib/matrix_presets/vavra.json":
        "bd1ec8b430712ef5fa49189fcb807449",
    "timbre-lib/matrix_presets/xenia_morphs.json":
        "92279534e3816bc123e45530151b486f",
    "timbre-lib/matrix_presets/je8086_morphs.json":
        "9839eac0ac7cccdce5a18f9f4109fbc9",
    "timbre-lib/matrix_presets/je8086.json":
        "a7beff9c5c02ab8ab2ee0e50b84cc28e",
}


# ---------------------------------------------------------------------------
# Synthetic dump / sheet helpers
# ---------------------------------------------------------------------------

def mk_base_dump(name="Original Name", category="Bas "):
    dump = bytearray(392)
    dump[0:5] = b"\xf0\x3e\x10\x00\x10"
    for off in range(vd.IDX_PARAM_FIRST, vd.IDX_PARAM_LAST + 1):
        dump[off] = (off * 7) % 128
    dump[vd.IDX_PARAM_FIRST] = 1                       # Version
    dump[vd.IDX_CHECKSUM] = 42                         # kept verbatim
    dump[391] = 0xF7
    dump[vd.IDX_NAME:vd.IDX_NAME + vd.NAME_LEN] = \
        name.encode("latin-1").ljust(vd.NAME_LEN, b" ")
    dump[vd.IDX_CATEGORY:vd.IDX_CATEGORY + vd.CATEGORY_LEN] = \
        category.encode("latin-1").ljust(vd.CATEGORY_LEN, b" ")
    return bytes(dump)


def write_syx(root, subdir, filename, dump):
    d = os.path.join(root, subdir)
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, filename)
    with open(path, "wb") as fh:
        fh.write(dump)
    return path


def mk_preset(pid, name16, params, role="fx"):
    return {"id": pid, "name": name16, "role": role, "params": params,
            "appliesVia": "state_blob_or_patch_unverified",
            "examples": [name16], "evidence": "1 patches carry this config"}


def _write_json(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(obj, fh)


def _run_cli(args, cwd=None):
    return subprocess.run([sys.executable, TOOL] + list(args),
                          capture_output=True, text=True, cwd=cwd)


def _expected_params(params_a, params_b, t):
    """Independent re-derivation of one vavra intermediate param set."""
    out = {}
    for k in sorted(set(params_a) | set(params_b)):
        va, vb = params_a.get(k), params_b.get(k)
        if vd.vavra_key_is_continuous(k, va, vb) and va is not None \
                and vb is not None and va != vb:
            out[k] = round(va + (vb - va) * t, 1)
        else:
            out[k] = va
    return out


def _expected_sysex(base, params_a, params_b, t, name_to_off):
    dump = bytearray(base)
    for k, v in _expected_params(params_a, params_b, t).items():
        off = vd.byte_of_key(k, name_to_off)
        val = v if isinstance(v, int) and not isinstance(v, bool) \
            else int(round(v))
        dump[off] = val
    return list(dump)


# ---------------------------------------------------------------------------
# Dump writer
# ---------------------------------------------------------------------------

def test_load_dump_rejects_bad_framing(tmp_path):
    good = mk_base_dump()
    p = tmp_path / "ok.syx"
    p.write_bytes(good)
    assert vd.load_dump(str(p)) == good
    short = tmp_path / "short.syx"
    short.write_bytes(good[:-1])
    with pytest.raises(vd.DumpError):
        vd.load_dump(str(short))
    nof0 = tmp_path / "nof0.syx"
    nof0.write_bytes(b"\xf1" + good[1:])
    with pytest.raises(vd.DumpError):
        vd.load_dump(str(nof0))
    nof7 = tmp_path / "nof7.syx"
    nof7.write_bytes(good[:-1] + b"\x00")
    with pytest.raises(vd.DumpError):
        vd.load_dump(str(nof7))


def test_build_dump_round_trip_only_intended_offsets_differ(tmp_path):
    base = mk_base_dump()
    p = tmp_path / "base.syx"
    p.write_bytes(base)
    out = vd.build_dump(str(p), {20: 64, 100: 127, 7: 1})   # 7 already 1
    diffs = [i for i in range(392) if out[i] != base[i]]
    assert diffs == [20, 100]
    assert out[20] == 64 and out[100] == 127
    # name/category/checksum untouched when not requested
    assert out[370:390] == base[370:390]
    assert out[vd.IDX_CHECKSUM] == base[vd.IDX_CHECKSUM]
    assert len(out) == 392 and out[0] == 0xF0 and out[391] == 0xF7


def test_build_dump_name_and_category_encoding(tmp_path):
    base = mk_base_dump()
    p = tmp_path / "base.syx"
    p.write_bytes(base)
    out = vd.build_dump(str(p), {}, name="Pad", category="Ld")
    assert out[370:386] == b"Pad" + b" " * 13
    assert out[386:390] == b"Ld  "
    # truncation to the fixed field widths
    out = vd.build_dump(str(p), {}, name="1234567890123456789", category="Bassy")
    assert out[370:386] == b"1234567890123456"
    assert out[386:390] == b"Bass"


def test_build_dump_validation_errors(tmp_path):
    base = mk_base_dump()
    p = tmp_path / "base.syx"
    p.write_bytes(base)
    for off in (0, 6, vd.IDX_PARAM_LAST + 1, vd.IDX_NAME, 391):
        with pytest.raises(vd.DumpError):
            vd.build_dump(str(p), {off: 1})       # outside param range
    for val in (128, -1, 1.5, "x", None, True):
        with pytest.raises(vd.DumpError):
            vd.build_dump(str(p), {20: val})      # not a 7-bit int
    with pytest.raises(vd.DumpError):
        vd.build_dump(str(p), {}, name="\u20acuro")  # euro sign: not latin-1
    # the parent file itself is never mutated
    assert p.read_bytes() == base


def test_parse_set_spec():
    assert vd.parse_set_spec("7=1,20=64") == {7: 1, 20: 64}
    assert vd.parse_set_spec(" 20 = 64 ") == {20: 64}
    for bad in ("", "7", "7=", "=1", "x=1", "7=y", "7=1,7=2"):
        with pytest.raises(vd.DumpError):
            vd.parse_set_spec(bad)


# ---------------------------------------------------------------------------
# vavra engine profile
# ---------------------------------------------------------------------------

def test_vavra_named_key_classification():
    for key in ("FX1Mix", "FX2Mix", "F1CutoffMod", "F1EnvMod", "F1PanMod",
                "O1Level", "O2Balance", "RingModLevel", "RingModBalance",
                "F1VelMod", "FiveFX1RingModLevel", "F1Cutoff", "F2Cutoff"):
        assert mp.is_continuous(key, "vavra"), key
    for key in ("FX1Type", "FX2Type", "F1ModSource", "F1PanModSource",
                "F1FmSource", "O1PwmSource", "NoiseModeF1", "NoiseModeF2",
                "F1Resonance", "F1KeyTrack", "F1Drive", "Fx1ChorusSpeed",
                "Fx2FlangerPolarity", "Fx2DelayAutopan"):
        assert not mp.is_continuous(key, "vavra"), key
    # discrete tokens win over contained continuous tokens
    assert mp.is_continuous("F1ModSource", "vavra") is False   # 'Mod' in it
    # other engines keep their own classification
    assert mp.is_continuous("FX1Mix") is False                  # xenia
    assert mp.is_continuous("OscLfo1Depth", "je8086") is True


def test_vavra_off_key_value_rule_boundary():
    assert vd.vavra_key_is_continuous("off_10", 0, 24) is True
    assert vd.vavra_key_is_continuous("off_10", 0, vd.OFF_INTERP_MAX_DELTA)
    assert not vd.vavra_key_is_continuous("off_10", 0, 25)
    assert not vd.vavra_key_is_continuous("off_10", 64, 7)      # hop 57
    # both parents must share the key
    assert not vd.vavra_key_is_continuous("off_10", None, 5)
    assert not vd.vavra_key_is_continuous("off_10", 5, None)


def test_vavra_intermediate_mixed_classification():
    pa = {"F1Cutoff": 10, "FX1Mix": 0, "FX1Type": 1, "F1ModSource": 3,
          "off_10": 0, "off_20": 0}
    pb = {"F1Cutoff": 60, "FX1Mix": 100, "FX1Type": 4, "F1ModSource": 9,
          "off_10": 20, "off_20": 90}
    fn = vd.make_continuous_fn(pa, pb)
    mid = mp.build_intermediate(pa, pb, id_a="a" * 16, id_b="b" * 16,
                                name_a="A", name_b="B", role="fx",
                                step=1, steps=1, engine="vavra",
                                continuous_fn=fn)
    assert mid["params"]["F1Cutoff"] == 35.0        # named continuous
    assert mid["params"]["FX1Mix"] == 50.0          # named continuous
    assert mid["params"]["off_10"] == 10.0          # <= 24 hop interpolates
    assert mid["params"]["FX1Type"] == 1            # discrete anchors to A
    assert mid["params"]["F1ModSource"] == 3        # source wins over Mod
    assert mid["params"]["off_20"] == 0             # > 24 hop anchors
    assert mid["interp"] == {"F1Cutoff": [10, 60], "FX1Mix": [0, 100],
                             "off_10": [0, 20]}
    assert mid["jumps"] == ["F1ModSource", "FX1Type", "off_20"]


# ---------------------------------------------------------------------------
# Parent-syx resolution + selection
# ---------------------------------------------------------------------------

def test_resolve_parent_syx_exact_prefix_and_ambiguity(tmp_path):
    params = {"F1Cutoff": 64, "off_10": 7}
    om = vd.load_offset_map(os.path.join(REPO, MAP_REL))
    by_byte = vd.params_to_bytes(params, om)
    dump = bytearray(mk_base_dump(name="Morph Src"))
    dump[om["F1Cutoff"]] = 64
    dump[10] = 7
    exact = write_syx(str(tmp_path), "Lead", "Morph Src.syx", bytes(dump))
    path, info = vd.resolve_parent_syx(str(tmp_path), "Morph Src", by_byte)
    assert path == exact and info["how"] == "exact"
    assert info["nameMatches"] == 1 and info["byteMatches"] == 1
    # prefix (category-suffixed) resolution, duplicates byte-verified
    os.remove(exact)
    d1 = write_syx(str(tmp_path), "Lead", "Morph Src Bass.syx", bytes(dump))
    d2 = write_syx(str(tmp_path), os.path.join("Lead", "work"),
                   "Morph Src Bass.syx", bytes(dump))
    path, info = vd.resolve_parent_syx(str(tmp_path), "Morph Src", by_byte)
    assert os.path.basename(path) == "Morph Src Bass.syx"
    assert info["how"] == "prefix" and info["nameMatches"] == 2
    assert info["byteMatches"] == 2                 # ambiguous, deterministic
    assert path == sorted([d1, d2])[0]
    # a name-matching file with wrong bytes never wins
    wrong = bytearray(bytes(dump))
    wrong[om["F1Cutoff"]] = 65
    os.remove(d1)
    os.remove(d2)
    write_syx(str(tmp_path), "Bass", "Morph Src Bass.syx", bytes(wrong))
    with pytest.raises(vd.DumpError):
        vd.resolve_parent_syx(str(tmp_path), "Morph Src", by_byte)
    with pytest.raises(FileNotFoundError):
        vd.resolve_parent_syx(str(tmp_path), "No Such Patch", by_byte)


def test_select_pairs_ranking_and_named_filter():
    named = {"F1Cutoff": 0, "FX1Mix": 0}
    presets = [
        mk_preset("p0", "A", dict(named, **{"off_1": 0, "off_2": 0})),
        mk_preset("p1", "B", dict(named, F1Cutoff=10, FX1Mix=10,
                                  **{"off_1": 4, "off_2": 4})),
        mk_preset("p2", "C", dict(named, **{"off_1": 0, "off_2": 0,
                                             "off_3": 0})),
        mk_preset("p3", "D", dict(named, F1Cutoff=30, FX1Mix=30,
                                  **{"off_1": 90, "off_2": 90,
                                     "off_3": 90})),
    ]
    resolvable = {0, 1, 2, 3}
    # 0:1 -> 2 named + 2 off diffs = 0.5 named, d small; 2:3 -> 2 named +
    # 3 off = 0.4 named -> filtered; 0:3, 1:3 same ratio, larger d
    pairs = vd.select_pairs(presets, resolvable, count=3)
    assert pairs[0] == (0, 1)
    assert all(p in [(0, 1), (0, 3), (1, 3), (2, 3)] for p in pairs)
    assert (2, 3) not in pairs                       # named ratio < 0.5
    assert vd.select_pairs(presets, resolvable, count=3) == pairs
    # unresolvable parents are excluded
    assert vd.select_pairs(presets, {0, 1}, count=5) == [(0, 1)]
    # identical signatures never rank
    assert vd.select_pairs([presets[0], presets[0]], {0, 1}, count=5) == []


def test_auto_pairs_skips_unresolvable_presets(tmp_path):
    params = {"F1Cutoff": 0}
    presets = [mk_preset("p0", "Has File", dict(params, F1Cutoff=10)),
               mk_preset("p1", "No File", dict(params, F1Cutoff=20)),
               mk_preset("p2", "Also File", dict(params, F1Cutoff=30))]
    sheet = {"schema": mp.SHEET_SCHEMA, "engine": "vavra",
             "presets": presets}
    om = vd.load_offset_map(os.path.join(REPO, MAP_REL))
    dump = bytearray(mk_base_dump(name="Has File"))
    dump[om["F1Cutoff"]] = 10
    write_syx(str(tmp_path), "Bass", "Has File Bass.syx", bytes(dump))
    dump2 = bytearray(mk_base_dump(name="Also File"))
    dump2[om["F1Cutoff"]] = 30
    write_syx(str(tmp_path), "Bass", "Also File Bass.syx", bytes(dump2))
    pairs, resolved = vd.build_auto_pairs(sheet, str(tmp_path), om, count=5)
    assert set(resolved) == {0, 2}                   # preset 1 has no syx
    assert pairs == [(0, 2)]


# ---------------------------------------------------------------------------
# Morph document (synthetic, end to end)
# ---------------------------------------------------------------------------

def _synthetic_morph(tmp_path):
    om = vd.load_offset_map(os.path.join(REPO, MAP_REL))
    pa = {"F1Cutoff": 10, "FX1Mix": 0, "FX1Type": 1, "off_10": 0,
          "off_20": 0}
    pb = {"F1Cutoff": 110, "FX1Mix": 120, "FX1Type": 4, "off_10": 16,
          "off_20": 99}
    dump_a = bytearray(mk_base_dump(name="Synth A"))
    dump_a[om["F1Cutoff"]] = 10
    dump_a[om["FX1Mix"]] = 0
    dump_a[om["FX1Type"]] = 1
    dump_a[10] = 0
    dump_a[20] = 0
    write_syx(str(tmp_path), "Bass", "Synth A Bass.syx", bytes(dump_a))
    sheet = {"schema": mp.SHEET_SCHEMA, "engine": "vavra",
             "sourceRoots": ["lib"], "unverified": True, "patchCount": 2,
             "scannedSidecars": 2,
             "presets": [mk_preset("a" * 16, "Synth A", pa),
                         mk_preset("b" * 16, "Synth B", pb)]}
    _write_json(tmp_path / "sheet.json", sheet)
    return sheet, bytes(dump_a), om


def test_build_vavra_morphs_document_and_sysex(tmp_path):
    sheet, dump_a, om = _synthetic_morph(tmp_path)
    doc = vd.build_vavra_morphs(sheet, "sheet.json", str(tmp_path),
                                [(0, 1)], 2, om, syx_root_label="lib")
    assert set(doc) == {"schema", "sourceSheet", "sysexRoots", "pairs",
                        "unverified"}
    assert doc["schema"] == "hdaw.matrix.preset.morph.v1"
    assert doc["sysexRoots"] == ["lib"]
    assert doc["unverified"] is True
    pair = doc["pairs"][0]
    assert set(pair) == {"pair", "parents", "distance", "namedDiffRatio",
                         "baseResolution", "steps"}
    assert pair["baseResolution"] == {"how": "prefix",
                                      "file": "Synth A Bass.syx",
                                      "nameMatches": 1, "byteMatches": 1}
    assert len(pair["steps"]) == 2
    for entry, k in zip(pair["steps"], (1, 2)):
        assert set(entry) == {"step", "preset", "sysex", "baseSyx"}
        assert entry["step"] == k
        assert entry["baseSyx"] == "Synth A Bass.syx"
        sysex = entry["sysex"]
        assert len(sysex) == 392
        assert all(isinstance(v, int) and 0 <= v <= 255 for v in sysex)
        # 7-bit param values; the F0/F7 framing bytes are > 127 by design
        assert all(0 <= v <= 127
                   for v in sysex[vd.IDX_PARAM_FIRST:vd.IDX_PARAM_LAST + 1])
        assert sysex[0] == 0xF0 and sysex[391] == 0xF7
        assert sysex[vd.IDX_CHECKSUM] == dump_a[vd.IDX_CHECKSUM]
        # independently re-derived from the parent dump at t = k/(steps+1)
        expected = _expected_sysex(dump_a, sheet["presets"][0]["params"],
                                   sheet["presets"][1]["params"],
                                   k / 3.0, om)
        assert sysex == expected
        # non-param bytes never move
        for off in range(392):
            if not vd.IDX_PARAM_FIRST <= off <= vd.IDX_PARAM_LAST:
                assert sysex[off] == dump_a[off]


def test_build_vavra_morphs_fails_loudly_on_unresolvable_parent(tmp_path):
    sheet, _dump, om = _synthetic_morph(tmp_path)
    import shutil
    shutil.rmtree(os.path.join(tmp_path, "Bass"))
    with pytest.raises((FileNotFoundError, vd.DumpError)):
        vd.build_vavra_morphs(sheet, "sheet.json", str(tmp_path),
                              [(0, 1)], 4, om)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def test_cli_dump_writer_matches_library(tmp_path):
    base = mk_base_dump()
    p = tmp_path / "base.syx"
    p.write_bytes(base)
    out = tmp_path / "out.syx"
    proc = _run_cli(["--base", str(p), "--set", "7=1,20=64",
                     "--name", "Cli Name", "--category", "Cat",
                     "--dump-out", str(out)])
    assert proc.returncode == 0, proc.stderr
    expected = vd.build_dump(str(p), {7: 1, 20: 64}, name="Cli Name",
                             category="Cat")
    assert out.read_bytes() == expected
    # error paths
    proc = _run_cli(["--base", str(p), "--set", "20=128",
                     "--dump-out", str(out)])
    assert proc.returncode == 2 and "outside" in proc.stderr
    proc = _run_cli(["--base", str(p), "--dump-out", str(out)])
    assert proc.returncode == 2 and "nothing to write" in proc.stderr


def test_cli_morph_byte_stable_and_no_absolute_paths(tmp_path):
    _synthetic_morph(tmp_path)
    out1, out2 = tmp_path / "out1.json", tmp_path / "out2.json"
    for out in (out1, out2):
        proc = _run_cli(["--sheet", "sheet.json", "--syx-root", ".",
                         "--offset-map",
                         os.path.join(REPO, MAP_REL),
                         "--pairs", "0:1", "--steps", "2",
                         "--out", out.name], cwd=tmp_path)
        assert proc.returncode == 0, proc.stderr
    b1, b2 = out1.read_bytes(), out2.read_bytes()
    assert b1 == b2                                  # byte-stable re-run
    text = b1.decode("utf-8")
    for marker in ("/mnt/", "D:/pdf", ":/pdf", str(tmp_path), "tmp"):
        assert marker not in text                    # no absolute paths
    # neither/both of --pairs and --auto-pairs is an error
    for extra in ([], ["--pairs", "0:1", "--auto-pairs", "5"]):
        proc = _run_cli(["--sheet", "sheet.json", "--syx-root", ".",
                         "--out", "x.json"] + extra, cwd=tmp_path)
        assert proc.returncode == 2
    proc = _run_cli(["--out", "x.json"])
    assert proc.returncode == 2


# ---------------------------------------------------------------------------
# Committed deliverable + neighbor-artifact pins
# ---------------------------------------------------------------------------

def _real_paths():
    sheet = os.path.join(REPO, SHEET_REL)
    out = os.path.join(REPO, OUT_REL)
    if not all(os.path.isfile(p) for p in (sheet, out)):
        pytest.skip("vavra sheet / morphs not present")
    if not os.path.isdir(SYX_ROOT):
        pytest.skip("microQ syx root not mounted")
    return sheet, out


def test_committed_neighbor_artifacts_untouched():
    for rel, digest in PINNED_MD5.items():
        path = os.path.join(REPO, rel)
        if not os.path.isfile(path):
            pytest.skip("artifact not present: %s" % rel)
        with open(path, "rb") as fh:
            assert hashlib.md5(fh.read()).hexdigest() == digest, rel


def test_vavra_morphs_deliverable_contract():
    sheet_path, out_path = _real_paths()
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    with open(out_path, encoding="utf-8") as fh:
        text = fh.read()
    doc = json.loads(text)
    assert doc["schema"] == "hdaw.matrix.preset.morph.v1"
    assert doc["sourceSheet"] == SHEET_REL.replace(os.sep, "/")
    assert doc["unverified"] is True
    assert doc["sysexRoots"] == ["rhythm-lab.com_waldorf_micro_q"]
    for marker in ("/mnt/", "D:/pdf", ":/pdf"):
        assert marker not in text
    assert [p["pair"] for p in doc["pairs"]] == DELIVERABLE_PAIRS
    presets = sheet["presets"]
    om = vd.load_offset_map(os.path.join(REPO, MAP_REL))
    base_cache = {}
    for pair in doc["pairs"]:
        ia, ib = (int(x) for x in pair["pair"].split(":"))
        pa, pb = presets[ia], presets[ib]
        assert pair["parents"] == [{"id": pa["id"], "name": pa["name"]},
                                   {"id": pb["id"], "name": pb["name"]}]
        keys = sorted(pa["params"])
        dist = sum(abs(pa["params"][k] - pb["params"][k])
                   for k in keys) / 127.0 / len(keys)
        assert pair["distance"] == round(dist, 6)
        diff = [k for k in keys if pa["params"][k] != pb["params"][k]]
        assert pair["namedDiffRatio"] == round(
            sum(1 for k in diff if not k.startswith("off_")) / len(diff), 6)
        assert pair["namedDiffRatio"] >= vd.MIN_NAMED_RATIO
        base_name = pair["baseResolution"]["file"]
        if base_name not in base_cache:              # locate + cache by name
            for dirpath, _d, files in os.walk(SYX_ROOT):
                if base_name in files:
                    with open(os.path.join(dirpath, base_name), "rb") as fh:
                        base_cache[base_name] = fh.read()
                    break
        base = base_cache[base_name]
        assert len(base) == 392
        assert len(pair["steps"]) == DELIVERABLE_STEPS
        assert [s["step"] for s in pair["steps"]] == [1, 2, 3, 4]
        for entry in pair["steps"]:
            assert entry["baseSyx"] == base_name
            t = entry["step"] / (DELIVERABLE_STEPS + 1.0)
            assert entry["preset"]["params"] == \
                _expected_params(pa["params"], pb["params"], t)
            sysex = entry["sysex"]
            assert len(sysex) == 392
            assert all(isinstance(v, int) and 0 <= v <= 255 for v in sysex)
            assert all(0 <= v <= 127 for v in
                       sysex[vd.IDX_PARAM_FIRST:vd.IDX_PARAM_LAST + 1])
            assert sysex == _expected_sysex(base, pa["params"], pb["params"],
                                            t, om)


def test_cli_reproduces_committed_vavra_morphs_byte_for_byte(tmp_path):
    sheet_path, out_path = _real_paths()
    out = tmp_path / "repro.json"
    proc = _run_cli(["--sheet", SHEET_REL, "--syx-root", SYX_ROOT,
                     "--auto-pairs", "5", "--steps", str(DELIVERABLE_STEPS),
                     "--out", str(out)], cwd=REPO)
    assert proc.returncode == 0, proc.stderr
    assert out.read_bytes() == open(out_path, "rb").read()
