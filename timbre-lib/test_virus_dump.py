#!/usr/bin/env python3
"""pytest for virus_dump.py (Access Virus patch WRITER + morph emitter).

Covers the strict framing validator (B/C 267B cs@265 vs TI 524B cs@522 with
its payload pad byte 521), the checksum rule against synthetic dumps, the
writer (no-op identity, byte-fidelity outside overridden payload offsets +
recomputed checksum, decode round-trip, undo identity, container bases incl.
.mid index addressing, value/name validation, tdm refusal), the committed
morph deliverable (schema, 5 pairs x 4 steps, distances re-derived
independently from the source sheet, per-step embedded sysex: framing +
checksum + re-parse + decode equality with the interpolated params, model
split 4 B/C pairs + 1 TI pair, no-absolute-path, byte-stable CLI
regeneration -- corpus-gated, skipped when the corpus root is absent), and
the md5 pins of the neighbor artifacts this change must not touch.

Run::

    PYTHONPATH=/tmp/hdawpylib python3 -m pytest timbre-lib/test_virus_dump.py \
        timbre-lib/test_virus_fx_pages.py timbre-lib/test_morph_presets.py -q
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys

import pytest

import morph_presets as MP
import virus_dump as VD
import virus_fx_pages as VFP
import virus_patch as VP

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(os.path.dirname(os.path.abspath(VD.__file__)),
                    "virus_dump.py")
MATRIX = os.path.join("timbre-lib", "matrix_presets")
SHEET_REL = os.path.join(MATRIX, "virus.json")
MORPHS_REL = os.path.join(MATRIX, "virus_morphs.json")
PAIRS = "16:26,9:20,22:39,12:34,0:30"
STEPS = 4

HERE = os.path.dirname(os.path.abspath(__file__))
TESTDATA = os.path.join(HERE, "testdata", "virus")
BC_FIXTURE = os.path.join(TESTDATA, "bcsingle.syx")
TI_FIXTURE = os.path.join(TESTDATA, "tiblock0.syx")
TDM_FIXTURE = os.path.join(TESTDATA, "tdm_1979_gangs.bin")
MID_FIXTURE = os.path.join(TESTDATA, "bank_bcsingle.mid")
VHC_FIXTURE = os.path.join(TESTDATA, "he_bank.vhc")


def _corpus_root():
    for cand in (os.environ.get("HDAW_VIRUS_CORPUS"),
                 "/mnt/d/pdf/Virus Presets", r"D:\pdf\Virus Presets"):
        if cand and os.path.isdir(cand):
            return cand
    return None


CORPUS_ROOT = _corpus_root()


@pytest.fixture(scope="module")
def vocab():
    v = VFP.Vocabulary()
    if not v._tables:            # emulator sources not present on this box
        pytest.skip("gearmulator parameterDescriptions not available")
    return v


# neighbor artifacts pinned at their current digests -- this change must
# not touch any of them.
PINNED_MD5 = {
    "timbre-lib/matrix_presets/xenia.json":
        "3129622d5767f1232d24c980c2d41d2c",
    "timbre-lib/matrix_presets/xenia_morphs_injectable.json":
        "a955de1e63d64ebf1bdafe709e4daaf7",
    "timbre-lib/matrix_presets/vavra_morphs.json":
        "f69fbb66c2e07cd7771aca5c81ffc72b",
    "timbre-lib/matrix_presets/je8086_morphs.json":
        "9839eac0ac7cccdce5a18f9f4109fbc9",
    "timbre-lib/matrix_presets/nord_morphs.json":
        "87dd529a0a377d17aa2eb1430dea0ddc",
    "timbre-lib/matrix_presets/xenia_offset_map.json":
        "f9baf391aa738f3bdaabcb1de10760ae",
    "timbre-lib/matrix_presets/nord_offset_map.json":
        "af4ecccba8b54bb67bb87b40f53fefb3",
}


# ---------------------------------------------------------------------------
# Synthetic dumps (strict framing incl. the TI pad byte)
# ---------------------------------------------------------------------------

def mk_dump(model, dev=0x00, bank=0x01, prog=0x2A, pattern=None):
    """A strictly-framed synthetic single dump for ``model``."""
    plen, dump_len, cs_off, pad = VD.model_framing(model)
    if pattern is None:
        pattern = (lambda i: (i * 7 + 3) & 0x7F)
    payload = bytes(pattern(i) for i in range(plen))
    cs = (dev + 0x10 + bank + prog + sum(payload)) & 0x7F
    return (bytes((0xF0,)) + VP.MANUFACTURER + bytes((0x01, dev, 0x10,
                                                      bank, prog))
            + payload + bytes(pad) + bytes((cs, 0xF7)))


def _bc(**kw):
    return mk_dump("B/C", **kw)


def _ti(**kw):
    return mk_dump("TI", **kw)


def _run_cli(args, cwd=REPO):
    return subprocess.run([sys.executable, TOOL] + list(args),
                          capture_output=True, text=True, cwd=cwd)


# ---------------------------------------------------------------------------
# Framing + checksum
# ---------------------------------------------------------------------------

def test_model_framing_table():
    assert VD.model_framing("B/C") == (256, 267, 265, 0)
    assert VD.model_framing("TI") == (512, 524, 522, 1)
    with pytest.raises(ValueError):
        VD.model_framing("B")


def test_dump_model_strict_framing():
    assert VD.dump_model(_bc()) == "B/C"
    assert VD.dump_model(_ti()) == "TI"
    bad = bytearray(_ti())
    del bad[521]                       # drop the TI pad byte -> 523 bytes
    with pytest.raises(ValueError):
        VD.dump_model(bytes(bad))
    for mutate in (
        lambda d: b"\xf1" + d[1:],                    # bad status
        lambda d: d[:4] + b"\x02" + d[5:],            # bad model id
        lambda d: d[:6] + b"\x11" + d[7:],            # bad command
        lambda d: d[:-1] + b"\xf0",                   # bad terminator
        lambda d: d[:-1],                             # truncated
    ):
        with pytest.raises(ValueError):
            VD.dump_model(mutate(_bc()))


def test_checksum_rule_synthetic():
    for dump in (_bc(dev=3, bank=2, prog=7), _ti(dev=1, bank=0x7F,
                                                 prog=0x11)):
        model = VD.dump_model(dump)
        plen, _dl, cs_off, _pad = VD.model_framing(model)
        expected = (dump[5] + 0x10 + dump[7] + dump[8]
                    + sum(dump[9:9 + plen])) & 0x7F
        assert dump[cs_off] == expected
        assert VD.checksum(dump) == expected
        assert VFP.verify_checksum(dump, plen) is True
        forged = bytearray(dump)
        forged[9] ^= 0x01                 # corrupt one payload byte
        assert VFP.verify_checksum(bytes(forged), plen) is False


def test_ti_fixture_carries_pad_byte():
    data = open(TI_FIXTURE, "rb").read()
    assert len(data) == 524
    assert data[521] == 0x00              # pad byte, preserved by the writer
    assert data[522] == VD.checksum(data) and data[523] == 0xF7


# ---------------------------------------------------------------------------
# Container loading (load_dumps / load_patch)
# ---------------------------------------------------------------------------

def _payload_slot_count(model, vocab):
    """Table slots inside the payload pages (the decodable subset)."""
    pages = VFP.model_pages(model)
    return sum(1 for (page, _index) in vocab.table(model) if page in pages)


def test_load_dumps_fixture_models(vocab):
    bc = VD.load_dumps(BC_FIXTURE, vocab)
    assert len(bc) == 1 and bc[0]["model"] == "B/C"
    assert bc[0]["name"] == "~WELCOME" and bc[0]["checksumOk"] is True
    # every payload-page slot decodes (the vocabs also carry slots on pages
    # outside the payload -- B/C page 114, TI pages 110/111 -- which don't)
    assert len(bc[0]["params"]) == _payload_slot_count("B/C", vocab)
    ti = VD.load_dumps(TI_FIXTURE, vocab)
    assert len(ti) == 1 and ti[0]["model"] == "TI"
    assert ti[0]["name"] == "WCOG" and ti[0]["checksumOk"] is True
    assert len(ti[0]["params"]) == _payload_slot_count("TI", vocab)
    assert len(VD.load_dumps(VHC_FIXTURE, vocab)) == 128
    assert len(VD.load_dumps(MID_FIXTURE, vocab)) == 128


def test_load_patch_agrees_with_decoder_fx_view(vocab):
    # independence check: the FX-subset view must equal virus_fx_pages'
    # fx_params over the same dump (the sheet-harvest convention).
    for path in (BC_FIXTURE, TI_FIXTURE):
        data = open(path, "rb").read()
        fmt = VP.detect_format(data, path)
        dump = VFP.iter_dumps(data, fmt)[0]
        model = VFP.payload_model(len(dump["payload"]))
        values, _holes = VFP.decode_payload(dump["payload"],
                                            vocab.table(model))
        fx = VFP.fx_params(values, vocab.table(model))
        named = VD.load_patch(path, vocab=vocab)
        for key, value in fx.items():
            assert named[key] == value, (path, key)


def test_load_patch_index_range(vocab):
    with pytest.raises(IndexError):
        VD.load_patch(BC_FIXTURE, index=1, vocab=vocab)
    with pytest.raises(IndexError):
        VD.load_patch(MID_FIXTURE, index=128, vocab=vocab)
    with pytest.raises(ValueError):
        VD.load_dumps(TDM_FIXTURE, vocab)     # tdm: no sysex framing
    with pytest.raises(OSError):
        VD.load_dumps(os.path.join(TESTDATA, "tiblock0.syx") + ".nope",
                      vocab)                    # missing file


def test_name_slot_addressing(vocab):
    inverse = VD.name_slots("B/C", vocab)
    table = vocab.table("B/C")
    assert len(inverse) == len(table)      # no duplicate primary names
    assert set(inverse) == set(table.values())
    for name, (page, index) in inverse.items():
        assert table[(page, index)] == name
        assert VD.payload_offset((page, index)) == \
            (page - 112) * 128 + index


# ---------------------------------------------------------------------------
# Writer: identity, fidelity, round-trip, validation
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("fixture", [BC_FIXTURE, TI_FIXTURE])
def test_build_noop_is_byte_identical(fixture, vocab):
    base = open(fixture, "rb").read()
    assert VD.build_dump(base, {}, vocab) == base


def test_write_byte_fidelity_outside_overrides(vocab, tmp_path):
    for fixture in (BC_FIXTURE, TI_FIXTURE):
        base = open(fixture, "rb").read()
        model = VD.dump_model(base)
        named = VD.decode_dump(base, vocab)
        # 'Delay Time' is TI-only (the B/C C-vocab delay block is the
        # documented vocabulary hole); pick a third name that exists.
        names = ["Assign1 Amount", "Chorus Mix",
                 "Ringmodulator Volume" if model == "TI" else "Chorus Delay"]
        overrides = {n: (named[n] + 37) % 128 for n in names}
        out = VD.build_dump(base, overrides, vocab)
        assert len(out) == len(base)
        _plen, _dl, cs_off, pad = VD.model_framing(model)
        changed = {i for i, (a, b) in enumerate(zip(base, out)) if a != b}
        expected = {9 + VD.payload_offset(VD.name_slots(model, vocab)[n])
                    for n in names}
        expected.add(cs_off)
        assert changed == expected, fixture
        if pad:
            assert out[9 + _plen:cs_off] == base[9 + _plen:cs_off]
        assert VFP.verify_checksum(out, _plen) is True
        redone = VD.decode_dump(out, vocab)
        for n, v in overrides.items():
            assert redone[n] == v
        for n, v in named.items():
            if n not in overrides:
                assert redone[n] == v        # untouched names stay exact


def test_write_roundtrip_and_undo(vocab, tmp_path):
    base_params = VD.load_patch(BC_FIXTURE, vocab=vocab)
    out = str(tmp_path / "rt.syx")
    info = VD.write_patch(BC_FIXTURE, {"Chorus Mix": 90}, out, vocab=vocab)
    assert info["model"] == "BC" and info["dumpSize"] == 267
    assert VD.load_patch(out, vocab=vocab)["Chorus Mix"] == 90
    again = str(tmp_path / "undo.syx")
    VD.write_patch(out, {"Chorus Mix": base_params["Chorus Mix"]}, again,
                   vocab=vocab)
    assert open(again, "rb").read() == open(BC_FIXTURE, "rb").read()


def test_write_from_mid_container_index(vocab, tmp_path):
    # a .mid bank base: no-op write of dump 57 must equal the exact sysex
    # event bytes at that position of the SMF.
    data = open(MID_FIXTURE, "rb").read()
    msg = VFP.smf_sysex_messages(data)[57]
    out = str(tmp_path / "dump57.syx")
    info = VD.write_patch(MID_FIXTURE, {}, out, index=57, vocab=vocab)
    assert open(out, "rb").read() == msg
    assert info["checksum"] == msg[265] and info["dumpIndex"] == 57


def test_write_validation_errors(vocab, tmp_path):
    out = str(tmp_path / "err.syx")
    with pytest.raises(ValueError):
        VD.write_patch(BC_FIXTURE, {"No Such Param": 1}, out, vocab=vocab)
    for bad in (128, -1, 1.5, True, "x", None):
        with pytest.raises(ValueError):
            VD.build_dump(_bc(), {"Chorus Mix": bad}, vocab)
    with pytest.raises(ValueError):
        VD.write_patch(TDM_FIXTURE, {}, out, vocab=vocab)   # tdm refused
    with pytest.raises(ValueError):
        VD.build_dump(b"garbage", {}, vocab)                # bad framing
    with pytest.raises(IndexError):
        VD.write_patch(MID_FIXTURE, {}, out, index=999, vocab=vocab)


def test_checksum_recomputed_not_copied(vocab):
    base = _bc(pattern=lambda i: 64)      # cs consistent with a flat payload
    out = VD.build_dump(base, {"Chorus Mix": 20}, vocab)
    plen, _dl, cs_off, _pad = VD.model_framing("B/C")
    assert out[cs_off] != base[cs_off]    # recomputed, not copied
    assert VFP.verify_checksum(out, plen) is True


# ---------------------------------------------------------------------------
# Committed morph deliverable (corpus-free)
# ---------------------------------------------------------------------------

def _morphs():
    return json.load(open(os.path.join(REPO, MORPHS_REL), encoding="utf-8"))


def _sheet():
    return json.load(open(os.path.join(REPO, SHEET_REL), encoding="utf-8"))


def test_morphs_document():
    doc, sheet = _morphs(), _sheet()
    presets = sheet["presets"]
    text = open(os.path.join(REPO, MORPHS_REL), encoding="utf-8").read()
    assert doc["schema"] == MP.MORPH_SCHEMA
    assert doc["engine"] == "virus"
    assert doc["sourceSheet"] == SHEET_REL.replace(os.sep, "/")
    assert doc["corpusRootBasename"] == "Virus Presets"
    assert doc["unverified"] is True
    assert [p["pair"] for p in doc["pairs"]] == PAIRS.split(",")
    for marker in ("/mnt/", "D:/pdf", ":/pdf", "/home/"):
        assert marker not in text
    for pair in doc["pairs"]:
        a, b = (int(x) for x in pair["pair"].split(":"))
        pa, pb = presets[a], presets[b]
        assert pair["parents"] == [{"id": pa["id"], "name": pa["name"]},
                                   {"id": pb["id"], "name": pb["name"]}]
        # the two parents may carry different key SETS (66-key B/C-side
        # configs vs 90-key TI-side configs): distance runs over the UNION,
        # null-aware (morph_presets._signed_diff convention)
        keys = sorted(set(pa["params"]) | set(pb["params"]))

        def _diff(va, vb):
            if va is None and vb is None:
                return 0
            if va is None or vb is None:
                return 127
            return abs(va - vb)

        dist = sum(_diff(pa["params"].get(k), pb["params"].get(k))
                   for k in keys) / 127.0 / len(keys)
        assert pair["distance"] == round(dist, 6)
        assert [s["step"] for s in pair["steps"]] == [1, 2, 3, 4]


def test_morph_step_sysex_contract(vocab):
    doc = _morphs()
    seen_models = set()
    for pair in doc["pairs"]:
        pair_models = set()
        for entry in pair["steps"]:
            preset = entry["preset"]
            assert entry["model"] in ("TI", "BC")
            assert "/" not in entry["basePatch"]
            assert entry["basePatch"].lower().endswith(
                (".mid", ".syx", ".vhc"))
            assert preset["appliesVia"] == "sysex_writer_verified_format"
            assert preset["unverified"] is True
            assert entry["basePatch"] in preset["evidence"]
            syx = bytes(entry["sysex"])
            assert all(0 <= b <= 255 for b in entry["sysex"])
            model = VD.dump_model(syx)          # strict framing
            tag = VD.MODEL_TAG[model]
            assert tag == entry["model"]
            pair_models.add(tag)
            plen, dl, cs_off, _pad = VD.model_framing(model)
            assert len(syx) == dl
            assert VFP.verify_checksum(syx, plen) is True
            # re-parses as a standalone container of its own generation
            fmt = "bcsingle" if len(syx) == 267 else "tibank"
            redumps = VFP.iter_dumps(syx, fmt)
            assert len(redumps) == 1
            assert redumps[0]["payload"] == syx[9:9 + plen]
            # EVERY non-null interpolated param is carried by the bytes
            named = VD.decode_dump(syx, vocab)
            for key, value in preset["params"].items():
                if value is None:
                    continue
                assert key in named, key
                assert named[key] == int(round(value)), key
            seen_models.add(tag)
        assert len(pair_models) == 1             # one generation per pair
        for entry in pair["steps"]:
            assert entry["model"] == pair["steps"][0]["model"]
            assert entry["basePatch"] == pair["steps"][0]["basePatch"]
    assert seen_models == {"TI", "BC"}


def test_morph_model_split_bc_majority():
    doc = _morphs()
    pair_models = [p["steps"][0]["model"] for p in doc["pairs"]]
    assert pair_models == ["BC", "BC", "TI", "BC", "BC"]
    assert pair_models.count("BC") >= 2          # locally verifiable pairs


def test_morph_ladder_monotonic():
    doc, sheet = _morphs(), _sheet()
    pair = doc["pairs"][0]                        # 16:26
    a, b = (int(x) for x in pair["pair"].split(":"))
    pa, pb = sheet["presets"][a]["params"], sheet["presets"][b]["params"]
    diffs = [(k, pa[k], pb[k]) for k in pa
             if k in pb and VFP.is_continuous_virus(k)
             and pa[k] is not None and pb[k] is not None
             and abs(pb[k] - pa[k]) >= 40]
    assert diffs                                  # a real continuous swing
    key = sorted(diffs, key=lambda t: -abs(t[2] - t[1]))[0][0]
    vals = [s["preset"]["params"][key] for s in pair["steps"]]
    if pb[key] < pa[key]:
        assert vals[0] >= vals[1] >= vals[2] >= vals[3]
    else:
        assert vals[0] <= vals[1] <= vals[2] <= vals[3]
    lo, hi = sorted((pa[key], pb[key]))
    assert all(lo - 0.01 <= v <= hi + 0.01 for v in vals)


# ---------------------------------------------------------------------------
# Corpus gate + CLI (skipped without the corpus root)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(CORPUS_ROOT is None, reason="Virus corpus not mounted")
def test_corpus_index_gate(vocab):
    corpus = VD.index_corpus(CORPUS_ROOT, vocab)
    stats = corpus["stats"]
    assert stats["files"] >= 40
    assert stats["dumps"] >= 5000
    assert stats["kept"] >= stats["dumps"] * 0.8   # checksum+byte-match kept
    assert len(corpus["index"]) >= 2000


@pytest.mark.skipif(CORPUS_ROOT is None, reason="Virus corpus not mounted")
def test_corpus_resolves_deliverable_parents(vocab):
    doc = _morphs()
    sheet = _sheet()
    corpus = VD.index_corpus(CORPUS_ROOT, vocab)
    for pair in doc["pairs"]:
        a = int(pair["pair"].split(":")[0])
        hit, note = VD.resolve_base_preferred(sheet["presets"][a], corpus)
        assert hit["file"].endswith(pair["steps"][0]["basePatch"])
        assert VD.MODEL_TAG[hit["model"]] == pair["steps"][0]["model"]
        assert note in ("BC-preferred", "TI-only")


@pytest.mark.skipif(CORPUS_ROOT is None, reason="Virus corpus not mounted")
def test_cli_regenerates_committed_morphs_byte_for_byte(tmp_path):
    out1, out2 = tmp_path / "m1.json", tmp_path / "m2.json"
    for out in (out1, out2):
        r = _run_cli(["--emit-morphs", "--sheet", SHEET_REL,
                      "--pairs", PAIRS, "--steps", str(STEPS),
                      "--out-json", str(out),
                      "--corpus-root", CORPUS_ROOT])
        assert r.returncode == 0, r.stderr
    assert out1.read_bytes() == out2.read_bytes()      # byte-stable re-run
    committed = os.path.join(REPO, MORPHS_REL)
    assert out1.read_bytes() == open(committed, "rb").read()


# ---------------------------------------------------------------------------
# CLI (fixture-based, no corpus needed)
# ---------------------------------------------------------------------------

def test_cli_dump_stable():
    r1 = _run_cli(["--dump", BC_FIXTURE])
    r2 = _run_cli(["--dump", BC_FIXTURE])
    assert r1.returncode == 0 and r1.stdout == r2.stdout
    doc = json.loads(r1.stdout)
    assert doc["model"] == "BC" and doc["dumpSize"] == 267
    assert doc["dumpsInFile"] == 1 and doc["checksumOk"] is True
    assert "Chorus Mix" in doc["params"]
    r3 = _run_cli(["--dump", TI_FIXTURE])
    assert r3.returncode == 0
    doc3 = json.loads(r3.stdout)
    assert doc3["model"] == "TI" and doc3["dumpSize"] == 524


def test_cli_write_matches_library(tmp_path):
    out = str(tmp_path / "cli.syx")
    r = _run_cli(["--write", BC_FIXTURE, "--out", out,
                  "--set", "Chorus Mix=90", "--set", "Assign1 Amount=64"])
    assert r.returncode == 0, r.stderr
    ref = str(tmp_path / "lib.syx")
    VD.write_patch(BC_FIXTURE, {"Chorus Mix": 90, "Assign1 Amount": 64},
                   ref)
    assert open(out, "rb").read() == open(ref, "rb").read()
    assert VD.load_patch(out)["Chorus Mix"] == 90


def test_cli_error_paths(tmp_path):
    out = str(tmp_path / "x.syx")
    r = _run_cli(["--write", BC_FIXTURE, "--out", out,
                  "--set", "bogus_param=1"])
    assert r.returncode == 2 and "bogus" in r.stderr
    r = _run_cli(["--write", BC_FIXTURE])
    assert r.returncode == 2
    r = _run_cli(["--dump", str(tmp_path / "missing.syx")])
    assert r.returncode == 2
    r = _run_cli(["--dump", TDM_FIXTURE, "--write", TDM_FIXTURE,
                  "--out", out])
    assert r.returncode == 2 and "tdm" in r.stderr
    r = _run_cli(["--emit-morphs", "--sheet", SHEET_REL, "--pairs", "3:3",
                  "--out-json", out, "--corpus-root", CORPUS_ROOT or
                  "/nonexistent"])
    assert r.returncode == 2


# ---------------------------------------------------------------------------
# Neighbor pins
# ---------------------------------------------------------------------------

def test_pinned_neighbor_md5():
    for rel, digest in PINNED_MD5.items():
        data = open(os.path.join(REPO, rel), "rb").read()
        assert hashlib.md5(data).hexdigest() == digest, rel
