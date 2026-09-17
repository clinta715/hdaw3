#!/usr/bin/env python3
"""Tests for morph_presets.py (schema hdaw.matrix.preset.morph.v1).

Covers interpolation math (exact midpoints, t = k/(steps+1) ladder),
continuous/discrete carrier classification (per engine: xenia Amount/exact
list + je8086 decoder-name token heuristics), discrete anchoring + documented
jumps, null handling, endpoint preservation (parents embedded by id, never
regenerated, inputs never mutated), the output schema contract, CLI
byte-stability + no-absolute-path, CLI error paths, the regenerated
real-sheet contract (xenia_morphs.json over xenia.json: the 5 analysis
pairs x 4 steps, distances re-derived independently from the source sheet),
and the je8086 contract (decoder-name classification, index-map integration
with paramIndex + unmapped, je8086_morphs.json byte-for-byte reproduction).

Run::

    PYTHONPATH=/tmp/hdawpylib python3 -m pytest timbre-lib/test_morph_presets.py -q
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

import pytest

import morph_presets as mp

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(os.path.dirname(os.path.abspath(mp.__file__)),
                    "morph_presets.py")
REAL_SHEET_REL = os.path.join("timbre-lib", "matrix_presets", "xenia.json")
REAL_OUT_REL = os.path.join("timbre-lib", "matrix_presets",
                            "xenia_morphs.json")
DELIVERABLE_PAIRS = "4:25,13:36,20:33,10:34,0:4"
DELIVERABLE_STEPS = 4


# ---------------------------------------------------------------------------
# Synthetic sheet helpers
# ---------------------------------------------------------------------------

def _write(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(obj, fh)


def mk_preset(pid, name, params, role="env"):
    return {"id": pid, "name": name, "role": role, "params": params,
            "appliesVia": mp.APPLIES_VIA, "examples": [name],
            "evidence": "1 patches carry this config"}


def make_sheet(path, presets):
    _write(path, {"schema": mp.SHEET_SCHEMA, "engine": "test",
                  "sourceRoots": ["lib"], "unverified": True,
                  "patchCount": 99, "scannedSidecars": 1,
                  "presets": presets})


def _run_cli(args, cwd=None):
    return subprocess.run([sys.executable, TOOL] + list(args),
                          capture_output=True, text=True, cwd=cwd)


def _expected_step_params(params_a, params_b, t):
    """Independent re-derivation of one intermediate param set (test-side)."""
    out = {}
    for k in sorted(set(params_a) | set(params_b)):
        va, vb = params_a.get(k), params_b.get(k)
        if mp.is_continuous(k) and va is not None and vb is not None \
                and va != vb:
            out[k] = round(va + (vb - va) * t, 1)
        else:
            out[k] = va
    return out


# ---------------------------------------------------------------------------
# Unit: carrier classification, pair parsing, distance convention
# ---------------------------------------------------------------------------

def test_is_continuous_classification():
    for key in ("Slot1Amount", "F1EnvVelAmount", "W2EnvAmount",
                "F1Cutoff", "F1Resonance", "Lfo1Delay", "Lfo2Delay",
                "EffectParamA", "EffectParamB", "EffectParamC",
                "ModDelayTime", "Pan", "DePan", "MixRingMod"):
        assert mp.is_continuous(key), key
    for key in ("EffectType", "ChorusEnabled", "Slot1Source",
                "Slot4Destination", "Mod1Type", "Mod2Source1",
                "Mod3Parameter", "ModDelaySource", "PanKeytrack"):
        assert not mp.is_continuous(key), key


def test_parse_pairs_valid_and_errors():
    assert mp.parse_pairs("17:21,0:3,17:32,12:28,3:9") == \
        [(17, 21), (0, 3), (17, 32), (12, 28), (3, 9)]
    assert mp.parse_pairs(" 2 : 5 ") == [(2, 5)]
    for bad in ("", "17", "17:", ":21", "x:y", "17:21,", "3:3", "-1:2"):
        with pytest.raises(ValueError):
            mp.parse_pairs(bad)


def test_mean_distance_matches_analysis_convention():
    # 2 of 3 keys swing full-scale, 1 is equal -> (127+127)/127/3
    pa = {"F1Cutoff": 0, "MixRingMod": 127, "Pan": 50}
    pb = {"F1Cutoff": 127, "MixRingMod": 0, "Pan": 50}
    assert round(mp.mean_distance(pa, pb), 6) == 0.666667
    # identical signatures -> zero distance
    assert mp.mean_distance(pa, dict(pa)) == 0.0


def test_intermediate_id_deterministic_16_hex():
    a = mp.intermediate_id("aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb", 1)
    assert a == mp.intermediate_id("aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb", 1)
    assert len(a) == 16 and int(a, 16) >= 0
    assert a != mp.intermediate_id("aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb", 2)
    assert a != mp.intermediate_id("aaaaaaaaaaaaaaaa", "cccccccccccccccc", 1)
    assert a != mp.intermediate_id("bbbbbbbbbbbbbbbb", "aaaaaaaaaaaaaaaa", 1)


# ---------------------------------------------------------------------------
# Unit: interpolation, anchoring, nulls, endpoints
# ---------------------------------------------------------------------------

def _parents():
    pa = {"F1Cutoff": 10, "MixRingMod": 80, "Slot1Amount": 0,
          "EffectType": 0, "ChorusEnabled": 64, "Slot1Source": 64,
          "EffectParamA": None, "PanKeytrack": 0}
    pb = {"F1Cutoff": 60, "MixRingMod": 20, "Slot1Amount": 90,
          "EffectType": 5, "ChorusEnabled": 64, "Slot1Source": 10,
          "EffectParamA": None, "PanKeytrack": 0}
    return pa, pb


def test_interpolation_math_midpoint_exact():
    pa, pb = _parents()
    # steps=1 -> the single intermediate IS the exact midpoint
    mid = mp.build_intermediate(pa, pb, id_a="a" * 16, id_b="b" * 16,
                                name_a="A", name_b="B", role="env",
                                step=1, steps=1)
    assert mid["params"]["F1Cutoff"] == 35.0            # (10+60)/2
    assert mid["params"]["MixRingMod"] == 50.0           # (80+20)/2
    assert mid["params"]["Slot1Amount"] == 45.0           # (0+90)/2
    assert mid["interp"] == {"F1Cutoff": [10, 60],
                             "MixRingMod": [80, 20],
                             "Slot1Amount": [0, 90]}
    # steps=3 -> k=2 sits exactly on the midpoint, k=1/k=3 at 25%/75%
    for k, frac in ((1, 0.25), (2, 0.5), (3, 0.75)):
        step = mp.build_intermediate(pa, pb, id_a="a" * 16, id_b="b" * 16,
                                     name_a="A", name_b="B", role="env",
                                     step=k, steps=3)
        assert step["params"]["F1Cutoff"] == round(10 + 50 * frac, 1)
    # 1-decimal rounding on non-representable fractions
    pa2 = dict(pa, F1Cutoff=10)
    pb2 = dict(pb, F1Cutoff=63)
    step = mp.build_intermediate(pa2, pb2, id_a="a" * 16, id_b="b" * 16,
                                 name_a="A", name_b="B", role="env",
                                 step=1, steps=1)
    assert step["params"]["F1Cutoff"] == round((10 + 63) / 2, 1)  # 36.5


def test_interpolation_ladder_t_equals_k_over_steps_plus_one():
    pa = dict(_parents()[0], F1Cutoff=0)
    pb = dict(_parents()[1], F1Cutoff=100)
    got = [mp.build_intermediate(pa, pb, id_a="a" * 16, id_b="b" * 16,
                                 name_a="A", name_b="B", role="env",
                                 step=k, steps=4)["params"]["F1Cutoff"]
           for k in range(1, 5)]
    assert got == [20.0, 40.0, 60.0, 80.0]      # t = 1/5 .. 4/5; 0 and 100
    # are the untouched endpoints (positions 0 and 5)


def test_discrete_keys_anchored_to_a_and_jumps_recorded():
    pa, pb = _parents()
    for k in range(1, 5):
        step = mp.build_intermediate(pa, pb, id_a="a" * 16, id_b="b" * 16,
                                     name_a="A", name_b="B", role="env",
                                     step=k, steps=4)
        # anchored to A for the WHOLE chain
        assert step["params"]["EffectType"] == 0
        assert step["params"]["Slot1Source"] == 64
        # documented discrete hops (sorted), equal keys never jump
        assert step["jumps"] == ["EffectType", "Slot1Source"]
        # discrete keys never leak into interp
        assert not (set(step["interp"]) & {"EffectType", "Slot1Source",
                                           "ChorusEnabled"})


def test_null_continuous_copies_a_and_skips_interpolation():
    pa, pb = _parents()
    assert pa["EffectParamA"] is None and pb["EffectParamA"] is None
    step = mp.build_intermediate(pa, dict(pb, MixRingMod=None),
                                 id_a="a" * 16, id_b="b" * 16,
                                 name_a="A", name_b="B", role="env",
                                 step=1, steps=2)
    assert step["params"]["EffectParamA"] is None        # null copied
    assert "EffectParamA" not in step["interp"]
    # null on B only: no interpolation, A's value carries through
    assert step["params"]["MixRingMod"] == pa["MixRingMod"]
    assert "MixRingMod" not in step["interp"]
    assert "MixRingMod" not in step["jumps"]   # continuous key, not a hop


def test_inputs_are_never_mutated():
    pa, pb = _parents()
    pa_snap, pb_snap = json.dumps(pa), json.dumps(pb)
    mp.build_intermediate(pa, pb, id_a="a" * 16, id_b="b" * 16,
                          name_a="A", name_b="B", role="env",
                          step=2, steps=4)
    assert json.dumps(pa) == pa_snap and json.dumps(pb) == pb_snap


def test_build_pair_preserves_endpoints_and_numbers_steps():
    presets = [mk_preset("id_a_aaaaaaaaaaaa", "Parent A", _parents()[0]),
               mk_preset("pad", "filler", dict(_parents()[0])),
               mk_preset("id_b_bbbbbbbbbbbb", "Parent B", _parents()[1])]
    pair = mp.build_pair(presets, 0, 2, 4)
    assert pair["pair"] == "0:2"
    assert pair["parents"] == [{"id": "id_a_aaaaaaaaaaaa", "name": "Parent A"},
                               {"id": "id_b_bbbbbbbbbbbb", "name": "Parent B"}]
    # endpoints embedded by id, NEVER regenerated as steps
    assert [s["step"] for s in pair["steps"]] == [1, 2, 3, 4]
    ids = {s["preset"]["id"] for s in pair["steps"]}
    assert "id_a_aaaaaaaaaaaa" not in ids and "id_b_bbbbbbbbbbbb" not in ids
    names = [s["preset"]["name"] for s in pair["steps"]]
    assert names[0] == "MORPH Parent A -> Parent B (step 1/5)"
    assert names[-1] == "MORPH Parent A -> Parent B (step 4/5)"
    # every intermediate inherits A's role
    assert all(s["preset"]["role"] == "env" for s in pair["steps"])


# ---------------------------------------------------------------------------
# Schema contract (synthetic)
# ---------------------------------------------------------------------------

def test_schema_contract_synthetic(tmp_path):
    pa, pb = _parents()
    make_sheet(tmp_path / "sheet.json",
               [mk_preset("aaaaaaaaaaaaaaaa", "A", pa),
                mk_preset("bbbbbbbbbbbbbbbb", "B", pb)])
    proc = _run_cli(["--sheet", "sheet.json", "--pairs", "0:1",
                     "--steps", "4", "--out", str(tmp_path / "out.json")],
                    cwd=tmp_path)
    assert proc.returncode == 0, proc.stderr
    doc = json.loads((tmp_path / "out.json").read_text(encoding="utf-8"))
    assert set(doc) == {"schema", "sourceSheet", "pairs", "unverified"}
    assert doc["schema"] == "hdaw.matrix.preset.morph.v1"
    assert doc["sourceSheet"] == "sheet.json"       # echoed, relative
    assert doc["unverified"] is True
    assert len(doc["pairs"]) == 1
    pair = doc["pairs"][0]
    assert set(pair) == {"pair", "parents", "distance", "steps"}
    assert pair["pair"] == "0:1"
    assert len(pair["steps"]) == 4
    for step in pair["steps"]:
        assert set(step) == {"step", "preset"}
        preset = step["preset"]
        assert set(preset) == {"id", "name", "role", "params", "appliesVia",
                               "unverified", "evidence", "parents", "interp",
                               "jumps"}
        assert len(preset["id"]) == 16 and int(preset["id"], 16) >= 0
        assert preset["appliesVia"] == "patch_or_sysex_unverified"
        assert preset["unverified"] is True
        assert preset["parents"] == ["aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb"]
        assert "aaaaaaaaaaaaaaaa" in preset["evidence"]
        assert "bbbbbbbbbbbbbbbb" in preset["evidence"]
        assert set(preset["params"]) == set(pa)     # full signature kept
        assert isinstance(preset["interp"], dict)
        assert all(len(v) == 2 for v in preset["interp"].values())


# ---------------------------------------------------------------------------
# CLI: byte-stability, no absolute paths, error paths, emitted gates
# ---------------------------------------------------------------------------

def test_cli_byte_stable_and_no_absolute_paths(tmp_path):
    pa, pb = _parents()
    make_sheet(tmp_path / "sheet.json",
               [mk_preset("aaaaaaaaaaaaaaaa", "A", pa),
                mk_preset("bbbbbbbbbbbbbbbb", "B", pb)])
    out1, out2 = tmp_path / "out1.json", tmp_path / "out2.json"
    for out in (out1, out2):
        proc = _run_cli(["--sheet", "sheet.json", "--pairs", "0:1",
                         "--steps", "4", "--out", str(out)], cwd=tmp_path)
        assert proc.returncode == 0, proc.stderr
    b1, b2 = out1.read_bytes(), out2.read_bytes()
    assert b1 == b2                                  # byte-stable re-run
    text = b1.decode("utf-8")
    for marker in ("/mnt/", "D:/pdf", ":/pdf", str(tmp_path), "tmp"):
        assert marker not in text                    # no absolute paths


def test_cli_emits_five_pairs_x_four_steps(tmp_path):
    presets = [mk_preset("p%02d" % i, "P%02d" % i, dict(_parents()[0],
                                                         F1Cutoff=i * 7))
               for i in range(6)]
    make_sheet(tmp_path / "sheet.json", presets)
    out = tmp_path / "out.json"
    proc = _run_cli(["--sheet", "sheet.json", "--pairs",
                     "0:1,0:2,1:3,2:4,3:5",      # 5 valid pairs, 6 presets
                     "--steps", "4", "--out", str(out)], cwd=tmp_path)
    assert proc.returncode == 0, proc.stderr
    doc = json.loads(out.read_text(encoding="utf-8"))
    assert len(doc["pairs"]) == 5
    for pair in doc["pairs"]:
        assert len(pair["steps"]) >= 4
        assert [s["step"] for s in pair["steps"]] == [1, 2, 3, 4]
    # every generated id unique across the whole document
    ids = [s["preset"]["id"] for p in doc["pairs"] for s in p["steps"]]
    assert len(ids) == len(set(ids))


def test_cli_error_paths(tmp_path):
    pa, pb = _parents()
    make_sheet(tmp_path / "sheet.json",
               [mk_preset("aaaaaaaaaaaaaaaa", "A", pa),
                mk_preset("bbbbbbbbbbbbbbbb", "B", pb)])
    out = str(tmp_path / "out.json")

    def _err(*extra):
        proc = _run_cli(["--sheet", "sheet.json", "--pairs", "0:1",
                         "--out", out] + list(extra), cwd=tmp_path)
        assert proc.returncode == 2, (extra, proc.stdout, proc.stderr)
        assert "error:" in proc.stderr
        return proc

    _err("--pairs", "17")                     # bad format
    _err("--pairs", "0:9")                    # out of range
    _err("--steps", "0")                      # no intermediates
    bad_sheet = tmp_path / "bad.json"
    bad_sheet.write_text("{}", encoding="utf-8")
    proc = _run_cli(["--sheet", "bad.json", "--pairs", "0:1",
                     "--out", out], cwd=tmp_path)
    assert proc.returncode == 2 and "schema" in proc.stderr
    missing = _run_cli(["--sheet", "nope.json", "--pairs", "0:1",
                        "--out", out], cwd=tmp_path)
    assert missing.returncode == 2


# ---------------------------------------------------------------------------
# Regenerated real-sheet contract (xenia_morphs.json over xenia.json)
# ---------------------------------------------------------------------------

def _real_paths():
    sheet = os.path.join(REPO, REAL_SHEET_REL)
    out = os.path.join(REPO, REAL_OUT_REL)
    if not (os.path.isfile(sheet) and os.path.isfile(out)):
        pytest.skip("xenia.json / xenia_morphs.json not present")
    return sheet, out


def test_regenerated_xenia_morphs_contract():
    sheet_path, out_path = _real_paths()
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    with open(out_path, encoding="utf-8") as fh:
        text = fh.read()
    doc = json.loads(text)
    assert doc["schema"] == "hdaw.matrix.preset.morph.v1"
    assert doc["sourceSheet"] == REAL_SHEET_REL.replace(os.sep, "/")
    assert doc["unverified"] is True
    assert [p["pair"] for p in doc["pairs"]] == \
        ["4:25", "13:36", "20:33", "10:34", "0:4"]
    for marker in ("/mnt/", "D:/pdf", ":/pdf"):
        assert marker not in text
    presets = sheet["presets"]
    for pair in doc["pairs"]:
        ia, ib = (int(x) for x in pair["pair"].split(":"))
        pa, pb = presets[ia], presets[ib]
        # parents embed the ORIGINAL presets' ids + names, in pair order
        assert pair["parents"] == [{"id": pa["id"], "name": pa["name"]},
                                   {"id": pb["id"], "name": pb["name"]}]
        # distance re-derived independently: mean |a-b|/127 over the signature
        # (null-aware: both-null contributes 0, null<->value counts full-scale)
        keys = sorted(pa["params"])

        def _diff(va, vb):
            if va is None and vb is None:
                return 0
            if va is None or vb is None:
                return 127
            return abs(va - vb)

        dist = sum(_diff(pa["params"][k], pb["params"][k])
                   for k in keys) / 127.0 / len(keys)
        assert pair["distance"] == round(dist, 6)
        # >=4 emitted intermediates, endpoints never regenerated
        assert len(pair["steps"]) == DELIVERABLE_STEPS
        assert [s["step"] for s in pair["steps"]] == [1, 2, 3, 4]
        parent_ids = {pa["id"], pb["id"]}
        for step in pair["steps"]:
            preset = step["preset"]
            assert preset["id"] not in parent_ids
            assert preset["parents"] == [pa["id"], pb["id"]]
            assert preset["role"] == pa["role"]
            assert set(preset["params"]) == set(keys)
            t = step["step"] / (DELIVERABLE_STEPS + 1.0)
            assert preset["params"] == _expected_step_params(pa["params"],
                                                             pb["params"], t)
            assert preset["interp"] == {
                k: v for k, v in preset["interp"].items()
                if mp.is_continuous(k) and pa["params"][k] != pb["params"][k]
                and pa["params"][k] is not None}


def test_xenia_jumps_match_analysis_discrete_hops():
    sheet_path, out_path = _real_paths()
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    with open(out_path, encoding="utf-8") as fh:
        doc = json.load(fh)
    by_pair = {p["pair"]: p for p in doc["pairs"]}
    # [4]->[25]: resonance/brightness swell -- NO discrete hops at all
    for step in by_pair["4:25"]["steps"]:
        assert step["preset"]["jumps"] == []
        assert set(step["preset"]["interp"]) == {"F1Cutoff", "F1Resonance",
                                                 "Slot1Amount"}
    # [13]->[36]: the minimal two-carrier morph (cutoff + slot-1 depth)
    for step in by_pair["13:36"]["steps"]:
        assert step["preset"]["jumps"] == []
        assert set(step["preset"]["interp"]) == {"F1Cutoff", "Slot1Amount"}
    # [20]->[33]: chorus switch + filter-env depth -- the analysis' one
    # documented discrete hop
    jumps = by_pair["20:33"]["steps"][0]["preset"]["jumps"]
    assert jumps == ["ChorusEnabled"]
    assert set(by_pair["20:33"]["steps"][0]["preset"]["interp"]) == \
        {"F1EnvAmount"}
    ia, ib = 20, 33
    pa, pb = sheet["presets"][ia]["params"], sheet["presets"][ib]["params"]
    expected = sorted(k for k in pa
                      if not mp.is_continuous(k) and pa[k] != pb[k])
    assert jumps == expected
    # [0]->[4]: the big zero-jump swing carries the full MixRingMod rise
    interp = by_pair["0:4"]["steps"][0]["preset"]["interp"]
    assert interp["MixRingMod"] == [sheet["presets"][0]["params"]["MixRingMod"],
                                    sheet["presets"][4]["params"]["MixRingMod"]]
    for step in by_pair["0:4"]["steps"]:
        assert step["preset"]["jumps"] == []
        assert set(step["preset"]["interp"]) == {
            "F1Cutoff", "F1EnvAmount", "F1Resonance", "MixRingMod",
            "W1EnvAmount", "W2EnvAmount"}


def test_cli_reproduces_committed_xenia_morphs_byte_for_byte(tmp_path):
    sheet_path, out_path = _real_paths()
    out = tmp_path / "repro.json"
    # exact deliverable invocation, run from the repo root with relative args
    proc = _run_cli(["--sheet", REAL_SHEET_REL, "--pairs", DELIVERABLE_PAIRS,
                     "--steps", str(DELIVERABLE_STEPS), "--out", str(out)],
                    cwd=REPO)
    assert proc.returncode == 0, proc.stderr
    assert out.read_bytes() == open(out_path, "rb").read()


# ---------------------------------------------------------------------------
# je8086 engine: decoder-name classification + index-map integration
# ---------------------------------------------------------------------------

JE8086_SHEET_REL = os.path.join("timbre-lib", "matrix_presets", "je8086.json")
JE8086_MAP_REL = os.path.join("timbre-lib", "matrix_presets",
                              "je8086_param_index_map.json")
JE8086_OUT_REL = os.path.join("timbre-lib", "matrix_presets",
                              "je8086_morphs.json")
JE8086_PAIRS = "4:20,5:19,17:25,1:36,2:17"
JE8086_STEPS = 4


def test_je8086_classification():
    # continuous: depth/rate/fade/time/level/feedback/balance/cutoff/
    # resonance/freq/width/sustain tokens + Control* CC-depth params
    for key in ("OscLfo1Depth", "FilterEnvelopeDepth", "AmpLfo2Depth",
                "Lfo1Rate", "Lfo2Rate", "Lfo1Fade",
                "AmpEnvelopeAttackTime", "ControlAmpEnvAttackTime",
                "AmpEnvelopeSustainLevel", "DelayFeedback",
                "ControlOscillatorBalance", "CutoffFrequency", "Resonance",
                "MultiEffectsLevel", "ControlLfo1Rate", "ControlAmpLevel",
                "ControlOsc2FineWide", "ControlOsc2Range",
                "ControlCutoffFrequency", "CrossModulationDepth",
                "CutoffSlope"):          # contains 'cutoff' (spec letter)
        assert mp.is_continuous(key, "je8086"), key
    # discrete suffixes win over contained tokens; DelayTime and PatchName
    # never interpolate
    for key in ("RingModulatorSwitch", "ChorusType", "DelayType",
                "MultiEffectsType", "Lfo1Waveform",
                "Lfo1AndEnvelopeDestination", "Lfo2DepthSelect", "DelayTime",
                "ArpPatchName", "AutoPanManualPanSwitch"):
        assert not mp.is_continuous(key, "je8086"), key
    # classification is per engine: the xenia default is untouched and does
    # not see the je8086 token heuristics
    assert mp.is_continuous("OscLfo1Depth") is False
    assert mp.is_continuous("EffectType", "je8086") is False
    assert mp.is_continuous("F1Cutoff") is True


def _je8086_synthetic(tmp_path):
    params = {"OscLfo1Depth": 10, "ControlLfo1Rate": 0,
              "Lfo2DepthSelect": 0, "DelayTime": 5, "NoLiveParam": 1}
    presets = [mk_preset("aaaaaaaaaaaaaaaa", "JA", dict(params)),
               mk_preset("bbbbbbbbbbbbbbbb", "JB",
                         dict(params, OscLfo1Depth=60, ControlLfo1Rate=90,
                              Lfo2DepthSelect=3, DelayTime=9,
                              NoLiveParam=100))]
    _write(tmp_path / "je.json",
           {"schema": mp.SHEET_SCHEMA, "engine": "je8086",
            "presets": presets})
    _write(tmp_path / "map.json",
           {"schema": mp.INDEX_MAP_SCHEMA, "matched": 4,
            "unmatched": ["NoLiveParam"],
            "map": {"OscLfo1Depth": {"index": 25},
                    "ControlLfo1Rate": {"index": 95},
                    "Lfo2DepthSelect": {"index": 26},
                    "DelayTime": {"index": 84}}})
    return tmp_path / "je.json", tmp_path / "map.json"


def test_je8086_index_map_integration(tmp_path):
    _je8086_synthetic(tmp_path)
    out = tmp_path / "out.json"
    proc = _run_cli(["--sheet", "je.json", "--index-map", "map.json",
                     "--pairs", "0:1", "--steps", "1",
                     "--out", "out.json"], cwd=tmp_path)
    assert proc.returncode == 0, proc.stderr
    doc = json.loads(out.read_text(encoding="utf-8"))
    step = doc["pairs"][0]["steps"][0]["preset"]
    # NoLiveParam (no live index) excluded from params + listed in unmapped
    assert "NoLiveParam" not in step["params"]
    assert step["unmapped"] == ["NoLiveParam"]
    # every emitted param carries its live index, and only those
    assert step["paramIndex"] == {"OscLfo1Depth": 25, "ControlLfo1Rate": 95,
                                  "Lfo2DepthSelect": 26, "DelayTime": 84}
    assert set(step["paramIndex"]) == set(step["params"])
    # continuous interpolated, discrete anchored + documented jumps
    assert set(step["interp"]) == {"OscLfo1Depth", "ControlLfo1Rate"}
    assert step["params"]["OscLfo1Depth"] == 35.0
    assert step["params"]["ControlLfo1Rate"] == 45.0
    assert step["params"]["Lfo2DepthSelect"] == 0      # suffix-wins discrete
    assert step["params"]["DelayTime"] == 5            # excluded time key
    assert step["jumps"] == ["DelayTime", "Lfo2DepthSelect"]


def test_je8086_requires_index_map(tmp_path):
    _je8086_synthetic(tmp_path)
    proc = _run_cli(["--sheet", "je.json", "--pairs", "0:1",
                     "--out", "out.json"], cwd=tmp_path)
    assert proc.returncode == 2 and "--index-map" in proc.stderr
    (tmp_path / "badmap.json").write_text('{"schema": "nope"}',
                                          encoding="utf-8")
    proc = _run_cli(["--sheet", "je.json", "--index-map", "badmap.json",
                     "--pairs", "0:1", "--out", "out.json"], cwd=tmp_path)
    assert proc.returncode == 2 and "schema" in proc.stderr


def _je8086_real_paths():
    sheet = os.path.join(REPO, JE8086_SHEET_REL)
    imap = os.path.join(REPO, JE8086_MAP_REL)
    out = os.path.join(REPO, JE8086_OUT_REL)
    if not all(os.path.isfile(p) for p in (sheet, imap, out)):
        pytest.skip("je8086 sheet / index map / morphs not present")
    return sheet, imap, out


def test_regenerated_je8086_morphs_contract():
    sheet_path, imap_path, out_path = _je8086_real_paths()
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    with open(imap_path, encoding="utf-8") as fh:
        imap = json.load(fh)
    with open(out_path, encoding="utf-8") as fh:
        text = fh.read()
    doc = json.loads(text)
    assert doc["schema"] == "hdaw.matrix.preset.morph.v1"
    assert doc["sourceSheet"] == JE8086_SHEET_REL.replace(os.sep, "/")
    assert doc["unverified"] is True
    assert [p["pair"] for p in doc["pairs"]] == \
        ["4:20", "5:19", "17:25", "1:36", "2:17"]
    for marker in ("/mnt/", "D:/pdf", ":/pdf"):
        assert marker not in text
    mapping = imap["map"]
    presets = sheet["presets"]
    for pair in doc["pairs"]:
        ia, ib = (int(x) for x in pair["pair"].split(":"))
        pa, pb = presets[ia], presets[ib]
        # parents embed the ORIGINAL presets' ids + names, in pair order
        assert pair["parents"] == [{"id": pa["id"], "name": pa["name"]},
                                   {"id": pb["id"], "name": pb["name"]}]
        # distance re-derived independently: mean |a-b|/127 over the signature
        keys = sorted(pa["params"])

        def _diff(va, vb):
            if va is None and vb is None:
                return 0
            if va is None or vb is None:
                return 127
            return abs(va - vb)

        dist = sum(_diff(pa["params"][k], pb["params"][k])
                   for k in keys) / 127.0 / len(keys)
        assert pair["distance"] == round(dist, 6)
        assert len(pair["steps"]) == JE8086_STEPS
        assert [s["step"] for s in pair["steps"]] == [1, 2, 3, 4]
        unmapped = sorted(k for k in keys if k not in mapping)
        parent_ids = {pa["id"], pb["id"]}
        for step in pair["steps"]:
            preset = step["preset"]
            assert set(preset) == {"id", "name", "role", "params",
                                   "appliesVia", "unverified", "evidence",
                                   "parents", "interp", "jumps",
                                   "paramIndex", "unmapped"}
            assert preset["id"] not in parent_ids
            assert preset["unmapped"] == unmapped
            # no key without a live index leaks in; every key carries one
            assert not (set(preset["params"]) & set(unmapped))
            assert set(preset["paramIndex"]) == set(preset["params"])
            assert all(mapping[k]["index"] == v
                       for k, v in preset["paramIndex"].items())
            # params re-derived independently at t = step/(steps+1)
            t = step["step"] / (JE8086_STEPS + 1.0)
            expected = {}
            for k in keys:
                if k in unmapped:
                    continue
                va, vb = pa["params"][k], pb["params"][k]
                if mp.is_continuous(k, "je8086") and va is not None \
                        and vb is not None and va != vb:
                    expected[k] = round(va + (vb - va) * t, 1)
                else:
                    expected[k] = va
            assert preset["params"] == expected
            assert preset["jumps"] == sorted(
                k for k in keys if k not in unmapped
                and not mp.is_continuous(k, "je8086")
                and pa["params"][k] != pb["params"][k])


def test_je8086_smooth_and_all_continuous_pairs():
    sheet_path, _, out_path = _je8086_real_paths()
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    with open(out_path, encoding="utf-8") as fh:
        doc = json.load(fh)
    by_pair = {p["pair"]: p for p in doc["pairs"]}
    # 4:20 -- the single-knob MultiEffectsLevel morph: no discrete hops
    for step in by_pair["4:20"]["steps"]:
        assert step["preset"]["jumps"] == []
        assert set(step["preset"]["interp"]) == {"MultiEffectsLevel"}
        assert step["preset"]["interp"]["MultiEffectsLevel"] == \
            [sheet["presets"][4]["params"]["MultiEffectsLevel"],
             sheet["presets"][20]["params"]["MultiEffectsLevel"]]
    # 2:17 -- all 13 differing params are continuous: zero jumps, all interp
    pa, pb = sheet["presets"][2]["params"], sheet["presets"][17]["params"]
    diff = [k for k in pa if pa[k] != pb[k]]
    assert len(diff) == 13
    assert all(mp.is_continuous(k, "je8086") for k in diff)
    for step in by_pair["2:17"]["steps"]:
        assert step["preset"]["jumps"] == []
        assert set(step["preset"]["interp"]) == set(diff)


def test_cli_reproduces_committed_je8086_morphs_byte_for_byte(tmp_path):
    _, imap_path, out_path = _je8086_real_paths()
    out = tmp_path / "repro.json"
    # exact deliverable invocation, run from the repo root with relative args
    proc = _run_cli(["--sheet", JE8086_SHEET_REL,
                     "--index-map", JE8086_MAP_REL,
                     "--pairs", JE8086_PAIRS,
                     "--steps", str(JE8086_STEPS), "--out", str(out)],
                    cwd=REPO)
    assert proc.returncode == 0, proc.stderr
    assert out.read_bytes() == open(out_path, "rb").read()
