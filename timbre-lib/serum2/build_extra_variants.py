
"""build_extra_variants.py — decisive tests:
  bitwig   : actual Serum 2 component state captured from Bitwig (plugin's own output)
  garbage  : XferJson header + JSON + random body
  half808  : 808 preset truncated to first 1KB of body
  tail808  : 808 preset with JSON replaced by bitwig-style header (body untouched)
"""
import json, os, random, struct, sys, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from buildproject import build
from statecodec import serum2_plugin_id

work = sys.argv[1] if len(sys.argv) > 1 else "serum2/test_projects"
pid = serum2_plugin_id()

def split_xferjson(data):
    n = int.from_bytes(data[9:13], "little")
    return json.loads(data[17:17 + n].decode()), data[17 + n:]

def make_xfer(state_json: dict, body: bytes) -> bytes:
    js = json.dumps(state_json, separators=(",", ":")).encode()
    return b"XferJson\x00" + struct.pack("<I", len(js)) + b"\x00\x00\x00\x00" + js + body

# 1. real Bitwig component state
bitwig = open(os.path.join(work, "serum2_component_state.bin"), "rb").read()

# 2. garbage: bitwig-style header + random body
rng = random.Random(42)
body = bytes(rng.randrange(256) for _ in range(3000))
garbage = make_xfer({"component": "processor", "hash": "0" * 32, "product": "Serum2",
                     "productVersion": "2.0.23", "url": "https://xferrecords.com/",
                     "vendor": "Xfer Records", "version": 9.0}, body)

# 3. 808 raw + truncated
p808 = "/mnt/d/pdf/Xfer/Serum 2 Presets/Presets/(EMZEE) 808 1.SerumPreset"
raw808 = open(p808, "rb").read()
j808, tail808 = split_xferjson(raw808)
half808 = make_xfer(j808, tail808[:1000])
tail808w = make_xfer({"component": "processor", "hash": j808["hash"], "product": j808["product"],
                      "productVersion": j808["productVersion"], "url": j808["url"],
                      "vendor": j808["vendor"], "version": 9.0}, tail808)

for name, state in [("bitwig", bitwig), ("garbage", garbage), ("half808", half808), ("tail808", tail808w)]:
    out = os.path.join(work, f"variant_{name}.hdaw3")
    build([(name, state, {})], out, pid)
print("built 4 extra variants")
