#!/usr/bin/env python3
"""pytest suite for timbre-lib/microq_patch.py (Waldorf microQ / Vavra dumps)."""

import json
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import microq_patch as mq  # noqa: E402

LIB = "/mnt/d/pdf/rhythm-lab.com_waldorf_micro_q"
if not os.path.isdir(LIB):
    LIB = "D:/pdf/rhythm-lab.com_waldorf_micro_q"


def make_dump(name="Acid bass", category="Bass", params=None, command=0x10,
              manufacturer=0x3E, trailing_f7=True, size=None):
    data = bytearray(mq.DUMP_SIZE)
    data[0] = 0xF0
    data[1] = manufacturer
    data[2] = mq.ID_MICROQ
    data[3] = mq.ID_DEVICE_OMNI
    data[4] = command
    data[5] = 0x30
    for i, v in enumerate((params or [1, 52, 64]), start=mq.PARAM_FIRST):
        data[i] = v & 0x7F
    data[mq.NAME_OFFSET:mq.NAME_OFFSET + mq.NAME_LEN] = (
        name.encode("latin-1")[:mq.NAME_LEN].ljust(mq.NAME_LEN, b" "))
    data[mq.CAT_OFFSET:mq.CAT_OFFSET + mq.CAT_LEN] = (
        category.encode("latin-1")[:mq.CAT_LEN].ljust(mq.CAT_LEN, b" "))
    data[mq.DUMP_SIZE - 2] = 0x00
    data[mq.DUMP_SIZE - 1] = 0xF7 if trailing_f7 else 0x00
    out = bytes(data)
    return out[:size] if size else out


def test_parses_a_single_dump():
    patch, err = mq.parse_dump(make_dump())
    assert err == ""
    assert patch["name"].strip() == "Acid bass"
    assert patch["category"].strip() == "Bass"
    assert patch["role"] == "bass"
    assert patch["paramCount"] == mq.NAME_OFFSET - mq.PARAM_FIRST
    assert patch["params"][str(mq.PARAM_FIRST)] == 1
    assert patch["size"] == mq.DUMP_SIZE


@pytest.mark.parametrize("kwargs,fragment", [
    ({"size": 300}, "unexpected size"),
    ({"manufacturer": 0x41}, "not a Waldorf microQ"),
    ({"command": 0x11}, "not a SingleDump"),
    ({"trailing_f7": False}, "not F7-terminated"),
])
def test_rejects_malformed_dumps(kwargs, fragment):
    patch, err = mq.parse_dump(make_dump(**kwargs))
    assert patch is None
    assert fragment in err


def test_category_maps_to_the_shared_role_vocabulary():
    assert mq.parse_dump(make_dump(category="Arp"))[0]["role"] == "arp"
    assert mq.parse_dump(make_dump(category="Pad"))[0]["role"] == "pad"
    assert mq.parse_dump(make_dump(category="Atmo"))[0]["role"] == "pad"
    assert mq.parse_dump(make_dump(category="Poly"))[0]["role"] == "chord"
    assert mq.parse_dump(make_dump(category="Perc"))[0]["role"] == "other"
    assert mq.parse_dump(make_dump(category="Fxyz"))[0]["role"] == "other"


def test_sidecar_contract_and_search_tags(tmp_path):
    data = make_dump(name="Deep down", category="Bass")
    p = tmp_path / "Deep down    CJ Bass.syx"
    p.write_bytes(data)
    patch, _ = mq.parse_dump(data)
    sidecar = mq.build_sidecar(patch, str(p))
    for key in ("schema", "engine", "name", "description", "roleCheck", "unmapped", "mappedParams"):
        assert key in sidecar
    assert sidecar["engine"] == "vavra"
    assert sidecar["schema"] == mq.SCHEMA_PATCH
    assert sidecar["roleCheck"]["verdict"] == "bass"
    # FileLibraryManager turns the first two mappedParams entries into tags, so they
    # must be descriptive (category/pack), not numeric parameter indices
    assert sidecar["mappedParams"]["category"]["param"] == "category:Bass"
    assert sidecar["mappedParams"]["pack"]["param"].startswith("pack:")
    assert sidecar["params"][str(mq.PARAM_FIRST)] == 1
    blob = json.dumps(sidecar, sort_keys=True)
    assert str(tmp_path) not in blob


def test_sidecars_and_verify_round_trip(tmp_path):
    for i, cat in enumerate(("Bass", "Lead", "Arp")):
        (tmp_path / ("patch%d.syx" % i)).write_bytes(make_dump(name="Patch %d" % i, category=cat))
    assert mq.run_sidecars(str(tmp_path), None) == 0
    assert mq.verify(str(tmp_path)) == 0
    # a corrupted sidecar is caught
    sidecar = tmp_path / "patch0.syx" + mq.SYSEX_SUFFIX
    data = json.loads(sidecar.read_text())
    data["name"] = "tampered"
    sidecar.write_text(json.dumps(data))
    assert mq.verify(str(tmp_path)) == 1


def test_survey_counts_categories_and_roles(tmp_path):
    (tmp_path / "a.syx").write_bytes(make_dump(name="One", category="Bass"))
    (tmp_path / "b.syx").write_bytes(make_dump(name="Two", category="Arp"))
    (tmp_path / "c.syx").write_bytes(make_dump(name="One", category="Bass"))
    data = mq.survey(str(tmp_path))
    assert data["totals"] == {"files": 3, "parsed": 3, "failed": 0, "checksumMatches": 0}
    assert data["categories"] == {"Bass": 2, "Arp": 1}
    assert data["roles"] == {"bass": 2, "arp": 1}
    assert any(d["name"] == "one" and len(d["files"]) == 2 for d in data["duplicateNames"])


@pytest.mark.skipif(not os.path.isdir(LIB), reason="microQ library not mounted")
def test_real_library_spot_check():
    files = mq._syx_files(LIB)
    assert len(files) == 528
    parsed = 0
    categories = {}
    for path in files:
        with open(path, "rb") as fh:
            patch, err = mq.parse_dump(fh.read())
        assert patch is not None, "%s: %s" % (path, err)
        parsed += 1
        categories[patch["category"].strip()] = categories.get(patch["category"].strip(), 0) + 1
    assert parsed == 528
    assert categories.get("Arp") == 147
    assert categories.get("Bass") == 79
    assert sum(categories.values()) == 528
