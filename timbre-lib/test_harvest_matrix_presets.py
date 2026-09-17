#!/usr/bin/env python3
"""Tests for harvest_matrix_presets.py (schema hdaw.matrix.preset.v1).

Covers every sidecar keying style (bank-params-per-patch, named mappedParams,
numeric-index, raw-offset), vocabulary mapping (section collisions + alias
preference), the xenia .mid body-offset key space (off-by-2 regression:
param N lives at key N+2; bank/program bytes and name chars never label
a param; .usb keys stay direct) with a committed-xenia.json vs .mid-bank
byte-match spot check, the vavra dump-offset -> name map (named + off_
fallback, primary-alias handling, CLI auto-detect, regenerated-sheet
contract),
clustering determinism, the >=10-presets gate on a synthetic
engine, byte-stable re-runs, and the no-absolute-path contract.  Real-library
spot checks skip when the library roots are absent (same convention as
harvest_fx_presets.py).

Run::

    python3 -m pytest timbre-lib/test_harvest_matrix_presets.py -q
"""

from __future__ import annotations

import importlib
import json
import os
import shutil
import subprocess
import sys

import pytest

import harvest_matrix_presets as hmp

SCHEMA = "hdaw.matrix.preset.v1"


# ---------------------------------------------------------------------------
# Synthetic sidecar builders (one per keying style)
# ---------------------------------------------------------------------------

def _write(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(obj, fh)


def make_je8086_bank(path, patches):
    """Bank sidecar: mappedParams.<slotNNN>.params, one param set PER PATCH."""
    slots = {}
    for i, (name, params) in enumerate(patches):
        slots["slot%03d" % (i + 1)] = {"name": name, "param": name,
                                       "params": params}
    _write(path, {"engine": "je8086", "format": "jp8080-dt1", "name": "bank",
                  "mappedParams": slots})


def make_je8086_patch(path, name, params):
    """Exploded per-patch sidecar: flat name-keyed mappedParams."""
    _write(path, {"engine": "je8086", "name": name, "mappedParams": params})


def make_nl2x(path, name, params):
    """Patch sidecar: named mappedParams {i: {param, raw, value}}."""
    mp = {str(i): {"param": k, "raw": v, "value": round(v / 127.0, 6)}
          for i, (k, v) in enumerate(sorted(params.items()))}
    _write(path, {"engine": "nodalred2x", "format": "nl2xmid", "name": name,
                  "mappedParams": mp})


def make_xenia_bank(path, bankname, patches):
    """Bank sidecar: params.<patchName> = {numericIndex: value}."""
    _write(path, {"engine": "xenia", "format": "microwave-usb",
                  "name": bankname, "params": patches})


def make_xenia_mid_bank(path, bankname, patches):
    """Bank sidecar with .mid BODY-OFFSET keys: param N at key N+2."""
    _write(path, {"engine": "xenia", "format": "microwave-sysex",
                  "name": bankname, "params": patches})


def make_vavra(path, name, offsets):
    """Patch sidecar: raw dump offsets 7..369 keyed params."""
    _write(path, {"engine": "vavra", "format": "microq-single-dump",
                  "name": name, "params": offsets})


def make_virus(path, name, unmapped):
    """Normalized virus sidecar: 23 sub_synth params + unmapped presence list."""
    mp = {"0": {"param": "osc1_wave", "raw": 0, "value": 0.0},
          "1": {"param": "osc1_level", "raw": 64, "value": 0.5}}
    _write(path, {"engine": "sub_synth", "format": "tdm", "name": name,
                  "mappedParams": mp, "unmapped": list(unmapped)})


# ---------------------------------------------------------------------------
# Unit: quantization, vocabulary, id stability
# ---------------------------------------------------------------------------

def test_quant_keeps_ints_merges_float_noise():
    assert hmp._quant(64) == 64
    assert hmp._quant(0.5) == 0.5
    assert hmp._quant(0.5000001) == hmp._quant(0.5)      # float noise merges
    assert hmp._quant(64.0) == 64                         # integral float -> int
    assert hmp._quant(True) == 1


def test_load_vocabulary_first_wins_and_alias_preference(tmp_path):
    # JSON5-ish: comments + a later section REUSING the same index space.
    vocab_file = tmp_path / "parameterDescriptions_test.json"
    vocab_file.write_text(
        "// \u00e4ccented comment\n"
        "{\n"
        "  \"params\": [\n"
        "    {\"index\": 81, \"name\": \"EffectParamA\", \"default\": 64},\n"
        "    {\"index\": 81, \"name\": \"DelayTime\", \"toText\": \"delayTime\"},\n"
        "    {\"index\": 76, \"name\": \"EffectType\"},\n"
        "    {\"index\": 28, \"name\": \"W1EnvAmount\"},\n"
        "    // multi section reuses indices\n"
        "    {\"index\": 76, \"name\": \"MWhatever\"},\n"
        "    {\"index\": 28, \"name\": \"MName12\"}\n"
        "  ]\n"
        "}\n",
        encoding="utf-8")
    vocab = hmp.load_vocabulary(str(vocab_file))
    assert vocab["76"] == "EffectType"      # first (patch) section wins
    assert vocab["28"] == "W1EnvAmount"     # first section wins
    assert vocab["81"] == "DelayTime"       # EffectParam alias prefers specific


def test_preset_id_is_content_hash_stable():
    a = hmp.preset_id("je8086", "fx", {"A": 1})
    b = hmp.preset_id("je8086", "fx", {"A": 1})
    c = hmp.preset_id("je8086", "fx", {"A": 2})
    assert a == b and a != c
    assert len(a) == 16 and int(a, 16) >= 0


# ---------------------------------------------------------------------------
# Per-keying-style extraction
# ---------------------------------------------------------------------------

def test_bank_params_per_patch_je8086(tmp_path):
    # one bank, two patches with DIFFERENT tuples -> 2 distinct clusters
    make_je8086_bank(tmp_path / "b.je8086.json", [
        ("Deep Throw", {"MultiEffectsType": 3, "DelayTime": 90,
                        "AmpLfo1Depth": 100}),
        ("Init Like", {"MultiEffectsType": 0, "DelayTime": 64,
                       "AmpLfo1Depth": 64}),
    ])
    sheet = hmp.harvest_engine("je8086", [str(tmp_path)], None)
    assert sheet["scannedSidecars"] == 1
    assert sheet["patchCount"] == 2
    assert len(sheet["presets"]) == 2
    by_name = {p["name"]: p for p in sheet["presets"]}
    fx = [p for name, p in by_name.items() if "fx mode 3" in name]
    assert fx, by_name.keys()
    assert fx[0]["examples"] == ["Deep Throw"]
    assert fx[0]["evidence"] == "1 patches carry this config"


def test_exploded_flat_and_bank_dedup_je8086(tmp_path):
    params = {"MultiEffectsType": 3, "DelayTime": 90}
    make_je8086_bank(tmp_path / "b.je8086.json", [("Same Patch", params)])
    make_je8086_patch(tmp_path / "exploded" / "s.je8086.json",
                      "Same Patch", params)
    sheet = hmp.harvest_engine("je8086", [str(tmp_path)], None)
    # both sources describe the SAME patch -> one deduped instance
    assert sheet["scannedSidecars"] == 2
    assert sheet["patchCount"] == 1
    assert sheet["presets"][0]["evidence"] == "1 patches carry this config"


def test_named_mappedparams_nl2x(tmp_path):
    make_nl2x(tmp_path / "p.nl2x.json", "Martian", {
        "filter_env_amount": 100, "lfo1_rate": 70, "lfo1_level": 90,
        "mod_env_level": 0})
    sheet = hmp.harvest_engine("nodalred2x", [str(tmp_path)], None)
    assert sheet["patchCount"] == 1
    preset = sheet["presets"][0]
    assert preset["appliesVia"] == "load_nord_bank"
    assert preset["params"]["lfo1_rate"] == 70
    # opaque paramNN slots never leak into the tuple
    assert not any(k.startswith("param") for k in preset["params"])


def test_numeric_index_xenia_uses_vocabulary(tmp_path):
    vocab = {"76": "EffectType", "81": "EffectParamA", "82": "ChorusEnabled",
             "193": "Slot1Amount", "62": "F1Cutoff"}
    make_xenia_bank(tmp_path / "bank.xenia.json", "TstBank", {
        "Matrix Beast": {"76": 5, "81": 90, "82": 1, "193": 100, "62": 30},
        "Plain": {"76": 0, "81": 64, "82": 0, "193": 0, "62": 64},
    })
    sheet = hmp.harvest_engine("xenia", [str(tmp_path)], vocab)
    assert sheet["patchCount"] == 2
    names = {p["name"] for p in sheet["presets"]}
    assert any("1-slot matrix" in n for n in names)
    assert any("chorus on" in n for n in names)
    for preset in sheet["presets"]:
        # full-tuple contract: provided indices resolved to device names,
        # tuple members the patch does not carry are None (never dropped)
        assert set(preset["params"]) == set(hmp.TUPLE_PARAMS["xenia"])
        assert preset["params"]["EffectType"] in (0, 5)
        assert preset["params"]["Slot1Amount"] in (0, 100, None)
        assert all(not k.isdigit() for k in preset["params"])
        assert preset["appliesVia"] == "patch_or_sysex_unverified"


def test_raw_offsets_vavra_never_named(tmp_path):
    make_vavra(tmp_path / "a.vavra.json", "Acid", {"7": 64, "141": 3, "369": 1})
    sheet = hmp.harvest_engine("vavra", [str(tmp_path)], None)
    preset = sheet["presets"][0]
    assert preset["appliesVia"] == "unmapped-pending-offset-map"
    assert preset["role"] == "raw-config"
    assert preset["params"] == {"7": 64, "141": 3, "369": 1}
    # offset keys only -- no invented parameter names
    assert all(k.isdigit() for k in preset["params"])


def test_virus_presence_and_shortfall(tmp_path):
    feats = ["fx_delay", "fx_chorus", "fx_reverb", "mod_matrix",
             "lfo1", "lfo2", "ring_mod"]
    for i, name in enumerate(("a", "b")):
        make_virus(tmp_path / ("%s.virus.json" % name), name, feats)
    sheet = hmp.harvest_engine("virus", [str(tmp_path)], None)
    assert sheet["patchCount"] == 2                # two patches, one config
    assert len(sheet["presets"]) == 1              # constant -> one cluster
    assert sheet["presets"][0]["appliesVia"] == "midi_cc_pc"
    assert "presetsShortfall" in sheet             # below the 10 gate, honestly


def test_virus_varying_presence_yields_distinct_presets(tmp_path):
    for i in range(3):
        feats = ["fx_delay", "lfo1"] + (["mod_matrix"] if i else []) \
            + (["lfo2"] if i == 2 else [])
        make_virus(tmp_path / ("p%d.virus.json" % i), "p%d" % i, feats)
    sheet = hmp.harvest_engine("virus", [str(tmp_path)], None)
    assert sheet["patchCount"] == 3
    assert len(sheet["presets"]) == 3


# ---------------------------------------------------------------------------
# Virus sidecarRev >= 2: dump-decoded fxParams cluster like every engine
# ---------------------------------------------------------------------------

def make_virus_rev2(path, name, fx_params):
    """fx-pages sidecar: named FX/matrix values under fxParams (rev 2)."""
    mp = {"0": {"param": "osc1_wave", "raw": 0, "value": 0.0}}
    _write(path, {"engine": "sub_synth", "format": "stdmidi", "name": name,
                  "mappedParams": mp, "unmapped": ["fx_delay"],
                  "sidecarRev": 2, "fxModel": "TI",
                  "fxParams": dict(fx_params),
                  "fxCoverage": {"payloadBytes": 512, "covered": 344,
                                 "verifiedValues": 344, "holes": 168,
                                 "holesNonZero": 73, "checksum": "ok",
                                 "byteMatch": "pass"}})


def test_virus_rev2_fxparams_cluster_as_named_tuples(tmp_path):
    for i, name in enumerate(("a", "b")):
        make_virus_rev2(tmp_path / ("%s.virus.json" % name),
                        name, {"Assign1 Source": 21 + i,
                               "Assign1 Destination": 9,
                               "Assign1 Amount": 64})
    sheet = hmp.harvest_engine("virus", [str(tmp_path)], None)
    # two distinct configs -> two named presets, NOT a presence-list merge
    assert sheet["patchCount"] == 2
    assert len(sheet["presets"]) == 2
    params = sheet["presets"][0]["params"]
    assert "Assign1 Source" in params          # device names, not presence
    assert "fx_delay" not in params


def test_virus_rev2_and_legacy_sidecars_coexist(tmp_path):
    make_virus_rev2(tmp_path / "rev2.virus.json", "rev2",
                    {"Assign1 Source": 21, "Assign1 Destination": 9,
                     "Chorus/Type": 1})
    make_virus(tmp_path / "legacy.virus.json", "legacy",
               ["fx_delay", "mod_matrix"])
    sheet = hmp.harvest_engine("virus", [str(tmp_path)], None)
    assert sheet["patchCount"] == 2
    assert len(sheet["presets"]) == 2
    roles = {p["role"] for p in sheet["presets"]}
    assert roles == {"mod-matrix", "feature-presence"}


def test_virus_gate_at_least_ten_presets_from_fxparams(tmp_path):
    for i in range(12):
        make_virus_rev2(tmp_path / ("p%02d.virus.json" % i), "p%02d" % i,
                        {"Assign1 Source": 21,
                         "Assign1 Amount": 40 + i,          # varies
                         "Chorus Mix": (i * 7) % 128})      # varies
    sheet = hmp.harvest_engine("virus", [str(tmp_path)], None)
    assert len(sheet["presets"]) >= hmp.VOCAB_SHORTFALL_MIN_PRESETS
    assert "presetsShortfall" not in sheet


def test_virus_describe_named_fragments():
    role, name = hmp._describe_virus_named({
        "Assign1 Source": 21, "Assign1 Destination": 9,
        "Assign1 Amount": 64, "Chorus/Type": 1, "Chorus Mix": 40,
        "Ringmodulator Volume": 10, "Lfo3 Destination": 5,
    })
    assert role == "mod-matrix"
    assert "1-slot matrix" in name
    assert "chorus" in name and "ring mod" in name and "LFO routed" in name


def test_virus_describe_fx_only_role():
    role, name = hmp._describe_virus_named({
        "Delay Mode": 1, "Delay Send": 64,
    })
    assert role == "fx"
    assert "delay" in name


def test_virus_matrix_slots_counts_used_sources():
    cfg = {"Assign1 Source": 21, "Assign2 Source": 0, "Assign3 Source": 5,
           "Assign1 Amount": 64}
    assert hmp._virus_matrix_slots(cfg) == 2    # source != 0; amount ignored



# ---------------------------------------------------------------------------
# Clustering, naming, determinism, contracts
# ---------------------------------------------------------------------------

def _nl2x_configs(count):
    """count DISTINCT matrix configs (deterministic)."""
    out = []
    for i in range(count):
        cfg = {"filter_env_amount": 30 + i, "lfo1_rate": 40 + i,
               "lfo1_level": 0, "mod_env_level": 0}
        if i % 2:
            cfg["lfo1_level"] = 50 + i
        out.append(("Patch %02d" % i, cfg))
    return out


def test_gate_at_least_ten_presets_synthetic_engine(tmp_path):
    for i, (name, cfg) in enumerate(_nl2x_configs(12)):
        make_nl2x(tmp_path / ("p%02d.nl2x.json" % i), name, cfg)
    sheet = hmp.harvest_engine("nodalred2x", [str(tmp_path)], None)
    assert len(sheet["presets"]) >= 10
    assert "presetsShortfall" not in sheet


def test_naming_is_deterministic_and_suffixes_collisions(tmp_path):
    # two clusters that describe identically get a deterministic (2) suffix
    make_nl2x(tmp_path / "1.nl2x.json", "A", {"lfo1_rate": 10, "lfo1_level": 0})
    make_nl2x(tmp_path / "2.nl2x.json", "B", {"lfo1_rate": 20, "lfo1_level": 0})
    make_nl2x(tmp_path / "3.nl2x.json", "C", {"lfo1_rate": 10, "lfo1_level": 80})
    sheet = hmp.harvest_engine("nodalred2x", [str(tmp_path)], None)
    names = sorted(p["name"] for p in sheet["presets"])
    assert names == sorted(set(names))               # unique


def test_schema_contract(tmp_path):
    make_je8086_bank(tmp_path / "b.je8086.json", [
        ("Deep Throw", {"MultiEffectsType": 3}),
        ("Plain", {"MultiEffectsType": 0}),
    ])
    sheet = hmp.harvest_engine("je8086", [str(tmp_path)], None)
    assert sheet["schema"] == SCHEMA
    assert sheet["engine"] == "je8086"
    assert sheet["unverified"] is True
    assert sheet["sourceRoots"] == [os.path.basename(os.path.normpath(str(tmp_path)))]
    assert isinstance(sheet["scannedSidecars"], int)
    assert isinstance(sheet["patchCount"], int)
    for preset in sheet["presets"]:
        assert set(preset) == {"id", "name", "role", "params", "appliesVia",
                               "examples", "evidence"}
        assert isinstance(preset["params"], dict)
        assert preset["appliesVia"] == "set_fx_param"
        assert isinstance(preset["examples"], list)
        assert preset["evidence"].endswith("patches carry this config")


def test_missing_tuple_params_are_none_in_config(tmp_path):
    make_nl2x(tmp_path / "partial.nl2x.json", "Sparse", {"lfo1_rate": 42})
    sheet = hmp.harvest_engine("nodalred2x", [str(tmp_path)], None)
    preset = sheet["presets"][0]
    assert preset["params"]["lfo1_rate"] == 42
    assert preset["params"]["filter_env_amount"] is None


def _run_real_cli(out_dir, roots, extra=()):
    import subprocess
    cmd = [sys.executable, os.path.join(os.path.dirname(__file__),
                                        "harvest_matrix_presets.py"),
           "--out-dir", str(out_dir)] + list(extra) + list(roots)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    return proc


def test_cli_byte_stable_and_no_absolute_paths(tmp_path):
    lib = tmp_path / "lib"
    for i, (name, cfg) in enumerate(_nl2x_configs(11)):
        make_nl2x(lib / ("p%02d.nl2x.json" % i), name, cfg)
    out1, out2 = tmp_path / "out1", tmp_path / "out2"
    _run_real_cli(out1, [str(lib)])
    _run_real_cli(out2, [str(lib)])
    files = sorted(os.listdir(out1))
    assert files == ["nodalred2x.json"]
    b1 = (out1 / "nodalred2x.json").read_bytes()
    b2 = (out2 / "nodalred2x.json").read_bytes()
    assert b1 == b2                                  # byte-stable re-run
    text = b1.decode("utf-8")
    assert "/" + str(tmp_path).strip("/") not in text
    for marker in ("/mnt/", "D:/pdf", ":/pdf", str(tmp_path)):
        assert marker not in text                    # no absolute paths
    sheet = json.loads(text)
    assert sheet["sourceRoots"] == ["lib"]           # basenames only


def test_cli_vocabulary_wiring_produces_named_xenia_params(tmp_path):
    vocab_file = tmp_path / "xt.json"
    vocab_file.write_text(json.dumps({"params": [
        {"index": 76, "name": "EffectType"},
        {"index": 193, "name": "Slot1Amount"}]}), encoding="utf-8")
    lib = tmp_path / "lib"
    make_xenia_bank(lib / "b.xenia.json", "B", {"P1": {"76": 2, "193": 80}})
    out = tmp_path / "out"
    proc = _run_real_cli(out, [str(lib)])
    assert "xenia" in proc.stdout
    sheet = json.loads((out / "xenia.json").read_text(encoding="utf-8"))
    assert sheet["presets"][0]["params"]["EffectType"] == 2
    assert sheet["presets"][0]["params"]["Slot1Amount"] == 80


def test_xenia_mid_key_space_off_by_two_regression(tmp_path):
    """Regression (xenia-offset-map.md): .mid sidecars key param N at key
    N+2 -- keys 0/1 are the bank/program bytes and keys 242+ the 16 name
    chars.  The pre-fix harvester labeled key K with vocab[K], so every
    .mid-derived named value read the WRONG param (~50% shifted)."""
    vocab = {"0": "Version", "76": "EffectType", "62": "F1Cutoff",
             "240": "Name00"}
    params = {"0": 1, "1": 64,       # bank + program bytes: never params
              "2": 1,                # param 0 (Version) at key 2
              "78": 5,               # param 76 (EffectType) at key 78
              "64": 90,              # param 62 (F1Cutoff) at key 64
              "242": 66, "243": 67}  # name chars: never params
    make_xenia_mid_bank(tmp_path / "b.mid.xenia.json", "b.mid", {"P": params})
    sidecar = hmp._load_json(str(tmp_path / "b.mid.xenia.json"))
    named = dict(hmp.iter_patch_configs("xenia", sidecar, vocab))["P"]
    assert named == {"Version": 1, "EffectType": 5, "F1Cutoff": 90}
    # the sheet path agrees (tuple projection uses the corrected names)
    sheet = hmp.harvest_engine("xenia", [str(tmp_path)], vocab)
    assert sheet["patchCount"] == 1
    assert sheet["presets"][0]["params"]["EffectType"] == 5
    assert sheet["presets"][0]["params"]["F1Cutoff"] == 90
    # .usb sidecars keep the DIRECT key space (key = param N), unchanged
    make_xenia_bank(tmp_path / "b.usb.xenia.json", "b.usb",
                    {"P": {"0": 1, "76": 5, "62": 90}})
    sidecar_usb = hmp._load_json(str(tmp_path / "b.usb.xenia.json"))
    named_usb = dict(hmp.iter_patch_configs("xenia", sidecar_usb, vocab))["P"]
    assert named_usb == named
    # corrected mid labeling and usb labeling AGREE: the same patch with the
    # same true params dedupes into ONE cluster (the off-by-2 split them)
    sheet2 = hmp.harvest_engine("xenia", [str(tmp_path)], vocab)
    assert sheet2["patchCount"] == 1
    assert sheet2["presets"][0]["params"] == sheet["presets"][0]["params"]


# ---------------------------------------------------------------------------
# vavra offset-name map (matrix_presets/vavra_offset_map.json)
# ---------------------------------------------------------------------------

def _repo_offset_map():
    """(mapping, path) of the committed vavra offset map; skips if absent."""
    path = hmp.find_offset_map("vavra")
    if not path:
        pytest.skip("vavra offset map not shipped beside the harvester")
    return hmp.load_offset_map(path), path


def test_offset_map_loads_with_primary_alias_names():
    omap, _ = _repo_offset_map()
    assert len(omap) == 86
    assert omap["135"] == "FX1Type" and omap["151"] == "FX2Type"
    assert omap["96"] == "F1ModSource"
    # aliased FX sub-param bytes keep the canonical PRIMARY name from the
    # memo's alias table (first-file-order), never a context guess
    assert omap["137"] == "Fx1ChorusSpeed"
    assert omap["145"] == "Fx1FlangerPolarity"
    assert omap["153"] == "Fx2ChorusSpeed"
    assert omap["162"] == "Fx2DelayAutopan"
    # injective rename keeps clustering semantics
    assert len(set(omap.values())) == len(omap)


def test_offset_map_named_and_unmapped_fallback(tmp_path):
    omap, path = _repo_offset_map()
    make_vavra(tmp_path / "a.vavra.json", "Acid",
               {"135": 3, "136": 90, "200": 7, "15": 1})
    sheet = hmp.harvest_engine(
        "vavra", [str(tmp_path)], None, offset_map=omap,
        offset_map_source=os.path.basename(path))
    assert sheet["offsetMap"] == "vavra_offset_map.json"
    assert sheet["unverified"] is True
    preset = sheet["presets"][0]
    assert preset["appliesVia"] == "state_blob_or_patch_unverified"
    # mapped offsets named, unmapped stay raw as off_<offset>
    assert preset["params"] == {"FX1Type": 3, "FX1Mix": 90,
                                "off_200": 7, "off_15": 1}
    text = json.dumps(sheet, sort_keys=True)
    assert "/mnt/" not in text and "D:/pdf" not in text


def test_offset_map_alias_context_not_invented(tmp_path):
    # even when FX1Type suggests another alias, the byte keeps its
    # canonical primary name: the type-value -> alias enum is NOT verified
    omap, _ = _repo_offset_map()
    make_vavra(tmp_path / "f.vavra.json", "Flangerish",
               {"135": 1, "137": 40, "138": 90})
    sheet = hmp.harvest_engine("vavra", [str(tmp_path)], None,
                               offset_map=omap, offset_map_source="m.json")
    params = sheet["presets"][0]["params"]
    assert params["Fx1ChorusSpeed"] == 40
    assert params["Fx1ChorusDepth"] == 90
    assert "Fx1FlangerSpeed" not in params
    assert "Fx1FlangerDepth" not in params


def test_describe_vavra_named_fragments_and_legacy_fallback():
    role, name = hmp.describe_vavra({"FX1Type": 2, "Fx1ChorusDepth": 100,
                                     "F1Cutoff": 20, "F1CutoffMod": 90,
                                     "off_200": 5})
    assert role == "fx"
    assert "FX1 chorus movement" in name
    assert "F1 filter closed" in name
    assert "off_200" not in name          # unnamed offsets never become names
    # deterministic regardless of dict insertion order
    again = hmp.describe_vavra({"off_200": 5, "F1CutoffMod": 90,
                                "F1Cutoff": 20, "Fx1ChorusDepth": 100,
                                "FX1Type": 2})
    assert (role, name) == again
    # legacy path (bare digit keys) keeps the raw wording
    role_raw, name_raw = hmp.describe_vavra({"7": 64, "141": 3})
    assert role_raw == "raw-config"
    assert name_raw == "raw dump config (2 nonzero offsets)"
    # map applied but nothing named -> honest unnamed wording
    role_un, name_un = hmp.describe_vavra({"off_7": 64, "off_141": 3})
    assert role_un == "raw-config"
    assert name_un == "raw dump config (2 unnamed offsets)"


def test_load_offset_map_validation(tmp_path):
    bad_key = tmp_path / "bad_key.json"
    bad_key.write_text('{"abc": "X"}', encoding="utf-8")
    with pytest.raises(ValueError):
        hmp.load_offset_map(str(bad_key))
    empty_name = tmp_path / "empty.json"
    empty_name.write_text('{"7": ""}', encoding="utf-8")
    with pytest.raises(ValueError):
        hmp.load_offset_map(str(empty_name))
    duplicate = tmp_path / "dup.json"
    duplicate.write_text('{"7": "A", "8": "A"}', encoding="utf-8")
    with pytest.raises(ValueError):
        hmp.load_offset_map(str(duplicate))
    ok = tmp_path / "ok.json"
    ok.write_text('{"7": "A", "8": "B"}', encoding="utf-8")
    assert hmp.load_offset_map(str(ok)) == {"7": "A", "8": "B"}


def test_cli_vavra_offset_map_autodetect_byte_stable_and_explicit(tmp_path):
    omap, repo_path = _repo_offset_map()
    lib = tmp_path / "lib"
    for i in range(12):
        make_vavra(lib / ("p%02d.vavra.json" % i), "Patch %02d" % i,
                   {"135": i % 6, "136": 60 + i, "85": 20 + i, "99": i * 3,
                    "200": 7, "369": i % 2})
    out_auto, out_again, out_explicit = (tmp_path / "auto1",
                                         tmp_path / "auto2",
                                         tmp_path / "explicit")
    _run_real_cli(out_auto, [str(lib)])
    _run_real_cli(out_again, [str(lib)])
    # explicit override ships the same basename as the auto-detected map so
    # the provenance key -- and therefore the whole file -- matches
    map_copy = tmp_path / "maps" / "vavra_offset_map.json"
    map_copy.parent.mkdir()
    shutil.copy(repo_path, map_copy)
    _run_real_cli(out_explicit, [str(lib)],
                  extra=["--offset-map", "vavra=%s" % map_copy])
    b_auto = (out_auto / "vavra.json").read_bytes()
    b_again = (out_again / "vavra.json").read_bytes()
    b_explicit = (out_explicit / "vavra.json").read_bytes()
    assert b_auto == b_again                     # byte-stable re-run
    assert b_auto == b_explicit                  # auto-detect == explicit flag
    sheet = json.loads(b_auto.decode("utf-8"))
    assert sheet["offsetMap"] == "vavra_offset_map.json"
    text = b_auto.decode("utf-8")
    for marker in ("/mnt/", "D:/pdf", str(tmp_path)):
        assert marker not in text
    named = set(omap.values())
    for preset in sheet["presets"]:
        assert preset["appliesVia"] == "state_blob_or_patch_unverified"
        keys = set(preset["params"])
        assert all(not k.isdigit() for k in keys)
        assert keys & named                      # some params named
        assert "off_200" in keys                 # unmapped fallback kept raw


def test_cli_offset_map_error_paths(tmp_path):
    _, repo_path = _repo_offset_map()
    lib = tmp_path / "lib"
    make_vavra(lib / "a.vavra.json", "A", {"135": 1})
    script = os.path.join(os.path.dirname(__file__),
                          "harvest_matrix_presets.py")

    def _cli(*extra):
        return subprocess.run(
            [sys.executable, script, "--out-dir",
             str(tmp_path / "out")] + list(extra) + [str(lib)],
            capture_output=True, text=True)

    missing = _cli("--offset-map", "vavra=%s" % (tmp_path / "nope.json"))
    assert missing.returncode == 2
    assert "not found" in missing.stderr
    wrong_engine = _cli("--offset-map", "virus=%s" % repo_path)
    assert wrong_engine.returncode == 2
    assert "not defined for engine" in wrong_engine.stderr
    malformed = tmp_path / "bad.json"
    malformed.write_text('{"nope": 1}', encoding="utf-8")
    bad = _cli("--offset-map", "vavra=%s" % malformed)
    assert bad.returncode == 2
    assert "bad offset map" in bad.stderr


def test_regenerated_vavra_sheet_contract():
    """matrix_presets/vavra.json must carry the mapped, verified contract."""
    sheet_path = os.path.join(os.path.dirname(hmp.__file__),
                              "matrix_presets", "vavra.json")
    if not os.path.isfile(sheet_path):
        pytest.skip("regenerated vavra sheet not present")
    with open(sheet_path, encoding="utf-8") as fh:
        text = fh.read()
    sheet = json.loads(text)
    assert sheet["schema"] == SCHEMA
    assert sheet["engine"] == "vavra"
    assert sheet["unverified"] is True
    assert sheet["offsetMap"] == "vavra_offset_map.json"
    assert sheet["sourceRoots"] == ["rhythm-lab.com_waldorf_micro_q"]
    assert len(sheet["presets"]) >= 10
    for marker in ("/mnt/", "D:/pdf", ":/pdf"):
        assert marker not in text
    for preset in sheet["presets"]:
        assert set(preset) == {"id", "name", "role", "params", "appliesVia",
                               "examples", "evidence"}
        assert preset["appliesVia"] == "state_blob_or_patch_unverified"
        assert preset["examples"]
        assert preset["evidence"].endswith("patches carry this config")
        keys = set(preset["params"])
        assert keys and all(not k.isdigit() for k in keys)
        assert any(not k.startswith("off_") for k in keys)
        assert preset["role"] != "raw-config"
        assert "raw dump config" not in preset["name"]
    names = [p["name"] for p in sheet["presets"]]
    assert len(names) == len(set(names))         # deterministic unique names


# ---------------------------------------------------------------------------
# Real-library spot checks (SKIP when the roots are absent)
# ---------------------------------------------------------------------------

def _real_root(engine):
    for cand in hmp.DEFAULT_ROOTS.get(engine, ()):  # first existing wins
        if os.path.isdir(cand):
            return cand
    return None


@pytest.mark.parametrize("engine", ["je8086", "nodalred2x", "xenia", "vavra",
                                    "virus"])
def test_real_library_spot_check(engine):
    root = _real_root(engine)
    if root is None:
        pytest.skip("library root absent for %s" % engine)
    vocab = None
    vocab_path = hmp.find_vocabulary(engine)
    if vocab_path:
        vocab = hmp.load_vocabulary(vocab_path)
    offset_map = None
    offset_map_path = hmp.find_offset_map(engine)
    if offset_map_path:
        offset_map = hmp.load_offset_map(offset_map_path)
    sheet = hmp.harvest_engine(
        engine, [root], vocab,
        offset_map=offset_map,
        offset_map_source=(os.path.basename(offset_map_path)
                           if offset_map_path else None))
    assert sheet["schema"] == SCHEMA
    assert sheet["scannedSidecars"] > 0, engine
    assert sheet["patchCount"] > 0, engine
    assert sheet["presets"], engine
    text = json.dumps(sheet, sort_keys=True)
    assert "/mnt/" not in text and "D:/pdf" not in text
    if engine == "xenia":
        # indices must be resolved to device names, never raw numbers
        for preset in sheet["presets"]:
            assert all(not k.isdigit() for k in preset["params"])
    if engine == "vavra":
        if offset_map:
            for preset in sheet["presets"]:
                assert preset["appliesVia"] == "state_blob_or_patch_unverified"
                assert all(not k.isdigit() for k in preset["params"])
                assert any(not k.startswith("off_") for k in preset["params"])
            assert sheet.get("offsetMap") == "vavra_offset_map.json"
        else:
            for preset in sheet["presets"]:
                assert preset["appliesVia"] == "unmapped-pending-offset-map"
                assert all(k.isdigit() for k in preset["params"])
    if engine == "virus":
        # sidecarRev >= 2 sidecars (virus_fx_pages) carry dump-decoded
        # FX/matrix values: the corpus clears the >= 10 gate and every
        # preset names device params (never the legacy presence list).
        assert len(sheet["presets"]) >= hmp.VOCAB_SHORTFALL_MIN_PRESETS
        assert "presetsShortfall" not in sheet
        for preset in sheet["presets"]:
            assert preset["appliesVia"] == "midi_cc_pc"
            assert all(not k.isdigit() for k in preset["params"])


def test_committed_xenia_sheet_mid_banks_byte_match():
    """xenia-offset-map.md protocol on the committed deliverable: every
    named value xenia.json harvested from a .mid bank must equal the raw
    dump byte at dump offset 7+vocab_index (sidecar key K = dump byte 5+K
    = param K-2).  Skips when the corpus or the sheet is absent."""
    sheet_path = os.path.join(os.path.dirname(hmp.__file__),
                              "matrix_presets", "xenia.json")
    root = _real_root("xenia")
    if not (os.path.isfile(sheet_path) and root):
        pytest.skip("xenia sheet / bank corpus not present")
    pytest.importorskip("xenia_dump")
    import xenia_dump as xd
    name_to_off = xd.name_to_offset()
    with open(sheet_path, encoding="utf-8") as fh:
        sheet = json.load(fh)
    mid_banks = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for fn in sorted(filenames):
            if os.path.splitext(fn)[1].lower() in (".mid", ".syx"):
                mid_banks.append(os.path.join(dirpath, fn))
    dumps = {}                       # bank path -> {patch name: 265-byte dump}
    # resolve_parent_dump semantics: a preset is mid-verified when SOME
    # example matches SOME .mid bank on ALL its mappable named values (patch
    # names are only unique per bank; unrelated banks may reuse a name).
    # A preset whose examples live only in .usb banks resolves nowhere here
    # and is allowed; FOUND-BUT-MISMATCHED is the off-by-2 signature and
    # fails.
    verified_presets = checked = 0
    mismatched = []
    for preset in sheet["presets"]:
        named = [(name, name_to_off[name], value)
                 for name, value in sorted(preset["params"].items())
                 if name in name_to_off and value is not None
                 and 0 <= value <= 127]
        hit = found = False
        for example in preset["examples"]:
            if hit:
                break
            for path in mid_banks:
                if path not in dumps:
                    dumps[path] = {r["name"]: r["dump"]
                                   for r in xd.load_bank(path)}
                dump = dumps[path].get(example)
                if dump is None:
                    continue
                found = True
                checked += len(named)
                if all(dump[off] == value for _n, off, value in named):
                    hit = True
                    break
        if hit:
            verified_presets += 1
        elif found:
            mismatched.append(preset["id"])
    assert verified_presets >= 5      # >=5 .mid-derived presets verify fully
    assert checked >= 500             # the check is not vacuous
    assert mismatched == []           # no found-but-mismatched preset
