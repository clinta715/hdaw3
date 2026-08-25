
"""wrap_and_build.py — build single-track Serum 2 test projects.

Variants per preset:
  A: preset JSON header with fileType -> component (keep author fields/version as saved)
  B: Bitwig-style component header (version 9.0, productVersion 2.0.23)
  R: raw .SerumPreset bytes (control; known-failing)
Outputs: <work>/variant_<name>_<preset>.hdaw3  (one track each)
"""
import json, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from buildproject import build
from statecodec import serum2_plugin_id

def split_xferjson(data):
    n = int.from_bytes(data[9:13], "little")
    j = json.loads(data[17:17 + n].decode())
    return j, data[17 + n:]

def wrap(preset_bytes, variant):
    j, tail = split_xferjson(preset_bytes)
    if variant == "R":
        return preset_bytes
    if variant == "A":
        h = {"component": "processor",
             "hash": j.get("hash"),
             "product": j.get("product", "Serum2"),
             "productVersion": j.get("productVersion", "2.0.16"),
             "url": j.get("url", "https://xferrecords.com/"),
             "vendor": j.get("vendor", "Xfer Records"),
             "version": j.get("version", 6.0)}
    elif variant == "B":
        h = {"component": "processor",
             "hash": j.get("hash"),
             "product": "Serum2",
             "productVersion": "2.0.23",
             "url": "https://xferrecords.com/",
             "vendor": "Xfer Records",
             "version": 9.0}
    else:
        raise SystemExit(f"unknown variant {variant}")
    js = json.dumps(h, separators=(",", ":")).encode()
    return b"XferJson\x00" + struct.pack("<I", len(js)) + b"\x00\x00\x00\x00" + js + tail

def main():
    work = sys.argv[1] if len(sys.argv) > 1 else "serum2/test_projects"
    presets = {
        "808": "/mnt/d/pdf/Xfer/Serum 2 Presets/Presets/(EMZEE) 808 1.SerumPreset",
        "bass": "/mnt/d/pdf/Xfer/Serum 2 Presets/Presets/! ! jackwya serum 2 stash.SerumPack::Presets/Packs/@jackwya/! ! jackwya serum 2 stash/bass - acoustic (mother 3).SerumPreset",
    }
    pid = serum2_plugin_id()
    for pname, path in presets.items():
        if "::" in path:
            pack, member = path.split("::", 1)
            with __import__("zipfile").ZipFile(pack) as z:
                raw = z.read(member)
        else:
            raw = open(path, "rb").read()
        for variant in ("R", "A", "B"):
            state = wrap(raw, variant)
            out = os.path.join(work, f"variant_{variant}_{pname}.hdaw3")
            build([(f"{variant}-{pname}", state, {})], out, pid)
    print("done")

if __name__ == "__main__":
    main()
