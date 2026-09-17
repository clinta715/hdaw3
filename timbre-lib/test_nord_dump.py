#!/usr/bin/env python3
"""pytest for nord_dump.py (NL2x patch WRITER + offset map + morph emitter).

Covers the nibble packer (round-trip, neighbor-byte preservation), the
writer (byte-fidelity outside overridden offsets, no-op identity, bit RMW
on packed byte 52, multi-group-0 override, name/value validation), the
shipped offset map document (66 entries, sheet keys, offset relations),
the full-corpus MAPPING FIXTURE (independent strict re-parse; every
sidecar param value byte-matched -- >=200 files across >=10 banks, 0
mismatches; whole-corpus with HDAW_NL2X_GATE_FULL=1, skipped when the
bank root is absent), the committed morph deliverables (schema, 5 pairs x
4 steps, .syx files decode back through the decoder and are byte-identical
to their base outside the overridden offsets), CLI byte-stability, and
the md5 pins of the neighbor artifacts this change must not touch.

Run::

    PYTHONPATH=/tmp/hdawpylib python3 -m pytest timbre-lib/test_nord_dump.py -q
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys

import pytest

import nl2x_patch as NP
import nord_dump as ND
import morph_presets as MP

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(os.path.dirname(os.path.abspath(ND.__file__)),
                    "nord_dump.py")
MATRIX = os.path.join("timbre-lib", "matrix_presets")
SHEET_REL = os.path.join(MATRIX, "nodalred2x.json")
MAP_REL = os.path.join(MATRIX, "nord_offset_map.json")
MORPHS_REL = os.path.join(MATRIX, "nord_morphs.json")
MORPH_DIR_REL = os.path.join(MATRIX, "nord_morphs")
PAIRS = "29:32,16:23,34:35,1:22,34:39"


def _banks_root():
    for cand in (os.environ.get("HDAW_NL2X_BANKS"),
                 "/mnt/d/pdf/NL2x Banks", r"D:\pdf\NL2x Banks"):
        if cand and os.path.isdir(cand):
            return cand
    return None


BANKS_ROOT = _banks_root()

# neighbor artifacts pinned at their current digests -- this change must
# not touch any of them (xor the decoder/harvest/morph machinery).
PINNED_MD5 = {
    "timbre-lib/matrix_presets/xenia.json":
        "3129622d5767f1232d24c980c2d41d2c",
    "timbre-lib/matrix_presets/xenia_morphs.json":
        "92279534e3816bc123e45530151b486f",
    "timbre-lib/matrix_presets/xenia_morphs_injectable.json":
        "a955de1e63d64ebf1bdafe709e4daaf7",
    "timbre-lib/matrix_presets/xenia_offset_map.json":
        "f9baf391aa738f3bdaabcb1de10760ae",
    "timbre-lib/matrix_presets/vavra_morphs.json":
        "f69fbb66c2e07cd7771aca5c81ffc72b",
    "timbre-lib/matrix_presets/je8086_morphs.json":
        "9839eac0ac7cccdce5a18f9f4109fbc9",
    "timbre-lib/matrix_presets/je8086_param_index_map.json":
        "02cf7171e2fd8eb7183451be9c654298",
}


# ---------------------------------------------------------------------------
# Synthetic dumps
# ---------------------------------------------------------------------------

def mk_single(params, msg_type=0, msg_spec=0, name=None):
    data = bytearray([0xF0, NP.ID_CLAVIA, 0x0F, NP.ID_N2X, msg_type,
                      msg_spec])
    for v in params:
        data.append(v & 0xF)
        data.append((v >> 4) & 0xF)
    if name is not None:
        data += name.encode("ascii")[:NP.NAME_LENGTH].ljust(NP.NAME_LENGTH)
    data.append(0xF7)
    return bytes(data)


def mk_multi715(first_params, msg_type=30, msg_spec=0):
    data = bytearray([0xF0, NP.ID_CLAVIA, 0x0F, NP.ID_N2X, msg_type,
                      msg_spec])
    for _group in range(4):
        for v in first_params:
            data.append(v & 0xF)
            data.append((v >> 4) & 0xF)
    for _i in range(90):
        data += b"\x00\x00"
    data.append(0xF7)
    assert len(data) == NP.MULTI_DUMP_SIZE
    return bytes(data)


PARAMS_A = [(i * 7 + 3) % 128 for i in range(66)]
PARAMS_B = [(i * 5 + 11) % 128 for i in range(66)]


@pytest.fixture()
def base_syx(tmp_path):
    p = tmp_path / "base.syx"
    p.write_bytes(mk_single(PARAMS_A))
    return str(p)


def _run_cli(args, cwd=REPO):
    return subprocess.run([sys.executable, TOOL] + list(args),
                          capture_output=True, text=True, cwd=cwd)


# ---------------------------------------------------------------------------
# Packer
# ---------------------------------------------------------------------------

def test_offsets_match_n2x_rule():
    for i in (0, 5, 7, 24, 25, 49, 52, 56, 61, 65):
        assert ND.dump_offset(i) == 6 + 2 * i
        assert ND.payload_offset(i) == 5 + 2 * i
        assert ND.dump_offset(i) == ND.payload_offset(i) + 1
    with pytest.raises(ValueError):
        ND.dump_offset(66)


def test_load_patch_synthetic(tmp_path):
    p = tmp_path / "a.syx"
    p.write_bytes(mk_single(PARAMS_A, msg_type=1, msg_spec=7))
    dumps = ND.load_dumps(str(p))
    assert len(dumps) == 1
    assert dumps[0]["params"] == PARAMS_A
    assert dumps[0]["kind"] == "single"
    assert dumps[0]["msgType"] == 1 and dumps[0]["msgSpec"] == 7
    assert ND.load_patch(str(p)) == PARAMS_A


def test_load_patch_with_name_and_multi(tmp_path):
    p1 = tmp_path / "n.syx"
    p1.write_bytes(mk_single(PARAMS_A, name="AURA NAME "))
    assert len(p1.read_bytes()) == NP.SINGLE_DUMP_WITH_NAME
    assert ND.load_patch(str(p1)) == PARAMS_A
    p2 = tmp_path / "m.syx"
    p2.write_bytes(mk_multi715(PARAMS_B))
    assert ND.load_patch(str(p2)) == PARAMS_B   # group 0


def test_set_param_preserves_neighbor_nibbles():
    dump = bytearray(mk_single(PARAMS_A))
    for i in range(66):
        ND.set_param(dump, i, 0x55)
        assert ND.get_param(dump, i) == 0x55
    assert dump[6:138] == mk_single([0x55] * 66)[6:138]


def test_sheet_key_indices():
    assert ND.SHEET_KEY_INDEX == {
        "filter_env_amount": 5, "filter_env_attack": 8,
        "filter_env_decay": 9, "filter_env_release": 11,
        "filter_env_sustain": 10, "fm_depth": 7, "lfo1_dest": 57,
        "lfo1_level": 22, "lfo1_rate": 21, "lfo1_waveform": 56,
        "lfo2_dest": 65, "lfo2_rate": 23, "mod_env_attack": 18,
        "mod_env_decay": 19, "mod_env_dest": 61, "mod_env_level": 20,
        "sync_distortion": 52,
    }


def test_morph_classification():
    cont = [k for k in ND.SHEET_KEYS if ND.is_continuous_nord(k)]
    assert sorted(cont) == sorted([
        "filter_env_amount", "filter_env_attack", "filter_env_decay",
        "filter_env_release", "filter_env_sustain", "fm_depth",
        "lfo1_level", "lfo1_rate", "lfo2_rate", "mod_env_attack",
        "mod_env_decay", "mod_env_level"])
    assert not ND.is_continuous_nord("sync_distortion")
    assert not ND.is_continuous_nord("lfo1_waveform")
    assert not ND.is_continuous_nord("mod_env_dest")


# ---------------------------------------------------------------------------
# Writer
# ---------------------------------------------------------------------------

def test_write_noop_is_byte_identical(base_syx, tmp_path):
    out = str(tmp_path / "noop.syx")
    ND.write_patch(base_syx, {}, out)
    assert open(out, "rb").read() == open(base_syx, "rb").read()


def test_write_byte_fidelity_outside_overrides(base_syx, tmp_path):
    overrides = {"cutoff": 3, "fm_depth": 90, "lfo1_rate": 1,
                 "sync_distortion": 0x11}
    out = str(tmp_path / "w.syx")
    ND.write_patch(base_syx, overrides, out)
    base = open(base_syx, "rb").read()
    new = open(out, "rb").read()
    assert len(base) == len(new)
    changed = {i for i, (a, b) in enumerate(zip(base, new)) if a != b}
    expected = set()
    for name in overrides:
        off = ND.dump_offset(ND.NAME_TO_INDEX[name])
        expected |= {off, off + 1}
    assert changed == expected
    params = ND.load_patch(out)
    assert params[3] == 3 and params[7] == 90 and params[21] == 1
    assert params[52] == 0x11
    # untouched params stay byte-exact
    for i in range(66):
        if i not in (3, 7, 21, 52):
            assert params[i] == PARAMS_A[i]


def test_write_roundtrip_through_decoder(base_syx, tmp_path):
    out = str(tmp_path / "rt.syx")
    ND.write_patch(base_syx, {"filter_env_amount": 127}, out)
    assert NP.parse_file(open(out, "rb").read())[0]["params"][5] == 127
    again = str(tmp_path / "rt2.syx")
    ND.write_patch(out, {"filter_env_amount": PARAMS_A[5]}, again)
    assert ND.load_patch(again)[5] == PARAMS_A[5]
    base = open(base_syx, "rb").read()
    assert open(again, "rb").read() == base  # writing the original == undo


def test_write_bit_rmw_on_byte52(base_syx, tmp_path):
    out = str(tmp_path / "bits.syx")
    ND.write_patch(base_syx, {"sync_distortion": 0x10}, out)  # distortion on
    assert ND.load_patch(out)[52] == 0x10
    ND.write_patch(out, {"sync": 1}, out)                     # + sync bit0
    assert ND.load_patch(out)[52] == 0x11
    ND.write_patch(out, {"ringmod": 1}, out)                  # + bit1
    assert ND.load_patch(out)[52] == 0x13
    ND.write_patch(out, {"distortion": 0}, out)               # - bit4
    assert ND.load_patch(out)[52] == 0x03


def test_write_multi_group0(tmp_path):
    base = tmp_path / "m.syx"
    base.write_bytes(mk_multi715(PARAMS_A))
    out = str(tmp_path / "m2.syx")
    ND.write_patch(str(base), {"mix": 99}, out)
    raw_base = base.read_bytes()
    raw_new = open(out, "rb").read()
    off = ND.dump_offset(2)                 # 10: only mix's 2 bytes move
    assert raw_new[:off] + raw_new[off + 2:] == (raw_base[:off]
                                                 + raw_base[off + 2:])
    assert ND.load_patch(out)[2] == 99


def test_write_name_suffix_preserved(tmp_path):
    base = tmp_path / "n.syx"
    base.write_bytes(mk_single(PARAMS_A, name="PRESERVE "))
    out = str(tmp_path / "n2.syx")
    ND.write_patch(str(base), {"gain": 64}, out)
    assert open(out, "rb").read() == (
        mk_single(PARAMS_A, name="PRESERVE ")[:ND.dump_offset(17)]
        + mk_single([64] * 66)[ND.dump_offset(17):ND.dump_offset(17) + 2]
        + mk_single(PARAMS_A, name="PRESERVE ")[ND.dump_offset(17) + 2:])


def test_write_validation_errors(base_syx, tmp_path):
    out = str(tmp_path / "err.syx")
    with pytest.raises(ValueError):
        ND.write_patch(base_syx, {"no_such_param": 1}, out)
    for bad in (128, -1, 1.5, True, "x"):
        with pytest.raises(ValueError):
            ND.write_patch(base_syx, {"cutoff": bad}, out)
    with pytest.raises(ValueError):
        ND.write_patch(base_syx, {"sync": 2}, out)
    smf = tmp_path / "bank.mid"
    smf.write_bytes(b"MThd" + b"\x00" * 10)
    with pytest.raises(ValueError):
        ND.write_patch(str(smf), {}, out)


def test_sens_names_accepted(base_syx, tmp_path):
    out = str(tmp_path / "sens.syx")
    ND.write_patch(base_syx, {"cutoff_sens": 42, "arp_range_sens": 7}, out)
    params = ND.load_patch(out)
    assert params[28] == 42 and params[49] == 7


# ---------------------------------------------------------------------------
# Offset map document
# ---------------------------------------------------------------------------

def test_offset_map_document():
    doc = json.load(open(os.path.join(REPO, MAP_REL), encoding="utf-8"))
    assert doc["_meta"]["schema"] == "hdaw.nord.offset.map.v1"
    assert doc["_meta"]["engine"] == "nodalred2x"
    params = doc["params"]
    assert len(params) == 66
    indices = sorted(e["index"] for e in params.values())
    assert indices == list(range(66))
    for name, e in params.items():
        assert e["offset"] == 5 + 2 * e["index"]
        assert e["dumpOffset"] == 6 + 2 * e["index"]
        assert isinstance(e["sheet"], bool)
    for key, idx in ND.SHEET_KEY_INDEX.items():
        e = params[key]
        assert e["index"] == idx and e["sheet"] is True
    packed = params["sync_distortion"]
    assert packed["access"] == "bits"
    assert packed["bits"] == {"sync": 0, "ringmod": 1, "distortion": 4}
    assert params["o2_pitch_sens"]["index"] == 25
    assert params["arp_range_sens"]["index"] == 49


# ---------------------------------------------------------------------------
# Corpus mapping fixture (independent strict re-parse)
# ---------------------------------------------------------------------------

def _strict_syx_dumps(data):
    dumps, i, n = [], 0, len(data)
    while i < n:
        if (data[i] == 0xF0 and i + 4 <= n and data[i + 1] == 0x33
                and data[i + 3] == 0x04):
            end = data.find(b"\xf7", i)
            if end < 0:
                break
            dumps.append(data[i:end + 1])
            i = end + 1
        else:
            i += 1
    return dumps


def _strict_smf_dumps(data):
    if data[:4] == b"RIFF":
        idx = data.find(b"data")
        if idx < 0:
            return []
        size = int.from_bytes(data[idx + 4:idx + 8], "little")
        data = data[idx + 8:idx + 8 + size]
    if data[:4] != b"MThd":
        return []

    def varlen(pos):
        v = 0
        while True:
            b = data[pos]
            pos += 1
            v = (v << 7) | (b & 0x7F)
            if not b & 0x80:
                return v, pos

    dumps, pos, n = [], 0, len(data)
    while pos < n - 3:
        if data[pos] == 0xF0:
            ln, q = varlen(pos + 1)
            if q + ln > n:
                break
            payload = bytes(data[q:q + ln])
            if payload[-1:] == b"\xf7":
                payload = payload[:-1]
            if len(payload) > 3 and payload[0] == 0x33 and payload[2] == 0x04:
                dumps.append(b"\xf0" + payload + b"\xf7")
            pos = q + ln
        else:
            pos += 1
    return dumps


def _corpus_mismatch_sample(limit_per_bank=None, limit_total=None):
    """(files, banks, values, mismatches) over sidecar-carrying patches."""
    files, banks, values, mismatches = 0, set(), 0, []
    for dirpath, dirs, fns in os.walk(BANKS_ROOT):
        dirs.sort()
        rel_top = os.path.relpath(dirpath, BANKS_ROOT).split(os.sep)[0]
        taken = 0
        for fn in sorted(fns):
            if not fn.lower().endswith((".syx", ".mid")):
                continue
            if fn.lower().endswith(".nl2x.json"):
                continue
            side = os.path.join(dirpath, fn + ".nl2x.json")
            if not os.path.isfile(side):
                continue
            if limit_per_bank is not None and taken >= limit_per_bank:
                break
            if limit_total is not None and files >= limit_total:
                return files, banks, values, mismatches
            taken += 1
            full = os.path.join(dirpath, fn)
            data = open(full, "rb").read()
            dumps = (_strict_smf_dumps(data) if data[:4] in (b"MThd", b"RIFF")
                     else _strict_syx_dumps(data))
            if not dumps:
                mismatches.append((full, "no strict dump"))
                continue
            dump = dumps[0]
            if not (dump[0] == 0xF0 and dump[1] == 0x33 and dump[3] == 0x04
                    and dump[-1] == 0xF7):
                mismatches.append((full, "framing"))
                continue
            files += 1
            banks.add(rel_top)
            sidecar = json.load(open(side, encoding="utf-8"))
            for k, e in sidecar["mappedParams"].items():
                i = int(k)
                values += 1
                v = (dump[6 + 2 * i] & 0xF) | (dump[7 + 2 * i] << 4)
                if v != e["raw"]:
                    mismatches.append((full, "idx %d sidecar %d raw %d"
                                       % (i, e["raw"], v)))
    return files, banks, values, mismatches


@pytest.mark.skipif(BANKS_ROOT is None, reason="NL2x bank root not mounted")
def test_corpus_mapping_gate():
    full = os.environ.get("HDAW_NL2X_GATE_FULL") == "1"
    files, banks, values, mismatches = _corpus_mismatch_sample(
        None if full else 25, None if full else 600)
    assert files >= 200, "gate floor: >=200 patches (got %d)" % files
    assert len(banks) >= 10, "gate floor: >=10 banks (got %d)" % len(banks)
    assert values == files * 66              # every sidecar carries all 66
    assert mismatches == [], mismatches[:5]


# ---------------------------------------------------------------------------
# Morph deliverables
# ---------------------------------------------------------------------------

def _sheet():
    return json.load(open(os.path.join(REPO, SHEET_REL), encoding="utf-8"))


def test_morphs_document():
    mp = json.load(open(os.path.join(REPO, MORPHS_REL), encoding="utf-8"))
    sheet = _sheet()
    presets = sheet["presets"]
    assert mp["schema"] == MP.MORPH_SCHEMA
    assert mp["engine"] == "nodalred2x"
    assert mp["unverified"] is True
    assert mp["sourceSheet"] == SHEET_REL
    assert len(mp["pairs"]) == 5
    for pair in mp["pairs"]:
        a, b = (int(x) for x in pair["pair"].split(":"))
        assert len(pair["steps"]) == 4
        assert [s["step"] for s in pair["steps"]] == [1, 2, 3, 4]
        d = round(MP.mean_distance(presets[a]["params"],
                                   presets[b]["params"]), 6)
        assert pair["distance"] == d
        assert pair["parents"][0]["id"] == presets[a]["id"]
        assert pair["parents"][1]["id"] == presets[b]["id"]
        base_abs = os.path.join(BANKS_ROOT, pair["base"]["file"])
        assert os.path.isfile(base_abs)
        base_params = ND.load_patch(base_abs)
        for k, v in presets[a]["params"].items():
            assert base_params[ND.NAME_TO_INDEX[k]] == v


@pytest.mark.skipif(BANKS_ROOT is None, reason="NL2x bank root not mounted")
def test_morph_step_files():
    mp = json.load(open(os.path.join(REPO, MORPHS_REL), encoding="utf-8"))
    sheet = _sheet()
    presets = sheet["presets"]
    n_files = 0
    for pair in mp["pairs"]:
        a, b = (int(x) for x in pair["pair"].split(":"))
        base_abs = os.path.join(BANKS_ROOT, pair["base"]["file"])
        base_bytes = open(base_abs, "rb").read()
        base_params = ND.load_patch(base_abs)
        pa, pb = presets[a]["params"], presets[b]["params"]
        for entry in pair["steps"]:
            pr = entry["preset"]
            path = os.path.join(REPO, MORPH_DIR_REL,
                                "%d-%d" % (a, b), pr["file"])
            raw = open(path, "rb").read()
            n_files += 1
            assert len(raw) == NP.SINGLE_DUMP_SIZE
            assert raw[0] == 0xF0 and raw[1] == 0x33 and raw[3] == 0x04
            assert raw[-1] == 0xF7
            # loads back through the reference decoder
            patches = NP.parse_file(raw)
            assert len(patches) == 1 and patches[0]["kind"] == "single"
            params = ND.load_patch(path)
            # syxParams byte-match the file; round() of the json params
            for k, v in pr["syxParams"].items():
                assert params[ND.NAME_TO_INDEX[k]] == v
                assert v == int(round(pr["params"][k]))
            # byte-identity vs base outside the overridden offsets
            expected = bytearray(base_bytes)
            for k, v in pr["syxParams"].items():
                if base_params[ND.NAME_TO_INDEX[k]] != v:
                    off = ND.dump_offset(ND.NAME_TO_INDEX[k])
                    expected[off] = (expected[off] & 0xF0) | (v & 0xF)
                    expected[off + 1] = ((expected[off + 1] & 0xF0)
                                         | ((v >> 4) & 0xF))
            assert bytes(expected) == raw
            # interp lists [a, b] endpoints; emitted mid stays between
            for k, ends in pr["interp"].items():
                assert ND.is_continuous_nord(k)
                assert ends == [pa[k], pb[k]]
                mid = pr["params"][k]
                assert min(pa[k], pb[k]) - 0.01 <= mid \
                    <= max(pa[k], pb[k]) + 0.01
            for k in pr["jumps"]:
                assert not ND.is_continuous_nord(k)
                assert pa[k] != pb[k]
            assert pr["appliesVia"] == "load_nord_bank"
            assert pr["unverified"] is True
            assert pr["parents"] == [presets[a]["id"], presets[b]["id"]]
    assert n_files == 20


def test_morph_ladder_endpoints(tmp_path):
    """step k's syx value moves monotonically toward B for a linear key."""
    mp = json.load(open(os.path.join(REPO, MORPHS_REL), encoding="utf-8"))
    pair = mp["pairs"][1]              # 16:23 -- fm_depth diff -67, no jumps
    assert pair["pair"] == "16:23"
    vals = [pair["steps"][i]["preset"]["syxParams"]["fm_depth"]
            for i in range(4)]
    sheet = _sheet()
    pa = sheet["presets"][16]["params"]["fm_depth"]
    pb = sheet["presets"][23]["params"]["fm_depth"]
    assert pb < pa                           # fm_depth diff is -67
    assert vals[0] > vals[1] > vals[2] > vals[3]
    assert abs(vals[0] - round(pa + (pb - pa) * 0.2)) <= 1
    assert abs(vals[3] - round(pa + (pb - pa) * 0.8)) <= 1


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def test_cli_dump_stable(tmp_path):
    src = tmp_path / "d.syx"
    src.write_bytes(mk_single(PARAMS_A))
    r1 = _run_cli(["--dump", str(src)])
    r2 = _run_cli(["--dump", str(src)])
    assert r1.returncode == 0 and r1.stdout == r2.stdout
    doc = json.loads(r1.stdout)
    assert doc["params"]["cutoff"] == PARAMS_A[3]
    assert doc["dumpSize"] == 139 and doc["dumpsInFile"] == 1


def test_cli_write_matches_library(base_syx, tmp_path):
    out = str(tmp_path / "cli.syx")
    r = _run_cli(["--write", base_syx, "--out", out,
                  "--set", "fm_depth=90", "--set", "sync=1"])
    assert r.returncode == 0, r.stderr
    ref = str(tmp_path / "lib.syx")
    ND.write_patch(base_syx, {"fm_depth": 90, "sync": 1}, ref)
    assert open(out, "rb").read() == open(ref, "rb").read()


def test_cli_error_paths(base_syx, tmp_path):
    out = str(tmp_path / "x.syx")
    r = _run_cli(["--write", base_syx, "--out", out,
                  "--set", "bogus=1"])
    assert r.returncode == 2 and "bogus" in r.stderr
    r = _run_cli(["--write", base_syx])
    assert r.returncode == 2
    r = _run_cli(["--dump", str(tmp_path / "missing.syx")])
    assert r.returncode == 2


@pytest.mark.skipif(BANKS_ROOT is None, reason="NL2x bank root not mounted")
def test_cli_emit_morphs_deterministic(tmp_path):
    outs = []
    for i in (1, 2):
        j = str(tmp_path / ("m%d.json" % i))
        d = str(tmp_path / ("d%d" % i))
        r = _run_cli(["--emit-morphs", "--sheet", SHEET_REL,
                      "--pairs", PAIRS, "--steps", "4",
                      "--out-json", j, "--out-dir", d,
                      "--banks-root", BANKS_ROOT])
        assert r.returncode == 0, r.stderr
        outs.append((j, d))
    assert (open(outs[0][0], "rb").read()
            == open(outs[1][0], "rb").read())
    for dirpath, _dirs, fns in os.walk(outs[0][1]):
        for fn in fns:
            rel = os.path.relpath(os.path.join(dirpath, fn), outs[0][1])
            assert open(os.path.join(outs[0][1], rel), "rb").read() == \
                open(os.path.join(outs[1][1], rel), "rb").read()


def test_pinned_neighbor_md5():
    for rel, digest in PINNED_MD5.items():
        data = open(os.path.join(REPO, rel), "rb").read()
        assert hashlib.md5(data).hexdigest() == digest, rel
