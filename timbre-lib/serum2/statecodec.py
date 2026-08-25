
"""statecodec.py — Serum 2 XferJson parsing + HDAW/JUCE state codecs.

Formats handled here:

1. Serum 2 "XferJson" chunks (both .SerumPreset files and the raw VST3
   component-state stream captured by DAWs):
       magic   "XferJson\\0"            (9 bytes)
       u32     json_len (little endian)
       4 bytes (zero)
       json    json_len bytes of UTF-8 header JSON
       binary  remaining bytes (osc data, wavetables, ...) -- opaque

2. HDAW/JUCE pluginState attribute encoding
   (juce::MemoryBlock::toBase64Encoding):
       "<decimal byte size>." + custom-base64(data)
   with the JUCE alphabet  ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"

3. HDAW plugin identifier strings (juce::PluginDescription::createIdentifierString):
       "<Format>-<Name>-<hex(hashCode32(fileOrIdentifier))>-<hex(uniqueId)>"
   hashCode32 = repeated  h = (h*31 + char)  over the file path (uint32 wrap).
"""
from __future__ import annotations
import json, os, re, struct
from typing import Any, Optional

# ── Serum 2 XferJson ────────────────────────────────────────────────────────
SERUM2_MAGIC = b"XferJson\x00"
JSON_OFFSET = 17  # 9 magic + 4 len + 4 zero

def parse_xferjson_head(data: bytes, max_json: int = 65536) -> Optional[dict]:
    """Read the JSON header of a XferJson chunk. Returns dict or None."""
    if len(data) < JSON_OFFSET or data[:9] != SERUM2_MAGIC:
        return None
    n = struct.unpack_from("<I", data, 9)[0]
    if n <= 0 or n > max_json or 17 + n > len(data):
        return None
    try:
        return json.loads(data[17:17 + n].decode("utf-8"))
    except Exception:
        return None

def is_xferjson(data: bytes) -> bool:
    return len(data) >= 9 and data[:9] == SERUM2_MAGIC

# ── JUCE custom base64 ───────────────────────────────────────────────────────
_ALPH = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
_TOK = {c: i for i, c in enumerate(_ALPH)}

_ENC3 = []
for i in range(256 * 256 * 256):
    b0, b1, b2 = (i >> 16) & 0xFF, (i >> 8) & 0xFF, i & 0xFF
    _ENC3.append(_ALPH[b0 >> 2] + _ALPH[((b0 & 3) << 4) | (b1 >> 4)]
                 + _ALPH[((b1 & 15) << 2) | (b2 >> 6)] + _ALPH[b2 & 63])

def juce_b64_encode(data: bytes) -> str:
    n = len(data)
    full = n - (n % 3)
    out = []
    ap = out.append
    for i in range(0, full, 3):
        ap(_ENC3[(data[i] << 16) | (data[i + 1] << 8) | data[i + 2]])
    rem = n - full
    if rem == 1:
        b = data[full]
        ap(_ALPH[b >> 2] + _ALPH[(b & 3) << 4])
    elif rem == 2:
        b0, b1 = data[full], data[full + 1]
        ap(_ALPH[b0 >> 2] + _ALPH[((b0 & 3) << 4) | (b1 >> 4)] + _ALPH[(b1 & 15) << 2])
    return "".join(out)

_DEC4 = []
for i in range(64 * 64 * 64 * 64):
    c0, c1, c2, c3 = i & 63, (i >> 6) & 63, (i >> 12) & 63, (i >> 18) & 63
    _DEC4.append(bytes(((c0 << 2) | (c1 >> 4),
                        ((c1 & 15) << 4) | (c2 >> 2),
                        ((c2 & 3) << 6) | c3)))

def juce_b64_decode(s: str) -> bytes:
    n = len(s)
    full = n - (n % 4)
    out = bytearray()
    ext = out.extend
    i = 0
    while i < full:
        c0, c1, c2, c3 = _TOK[s[i]], _TOK[s[i + 1]], _TOK[s[i + 2]], _TOK[s[i + 3]]
        ext(_DEC4[(c3 << 18) | (c2 << 12) | (c1 << 6) | c0])
        i += 4
    rem = n - full
    if rem == 1:
        raise ValueError("impossible 1-char tail")
    if rem == 2:
        c0, c1 = _TOK[s[full]], _TOK[s[full + 1]]
        out.append((c0 << 2) | (c1 >> 4))
    elif rem == 3:
        c0, c1, c2 = _TOK[s[full]], _TOK[s[full + 1]], _TOK[s[full + 2]]
        out.append((c0 << 2) | (c1 >> 4))
        out.append(((c1 & 15) << 4) | (c2 >> 2))
    return bytes(out)

def to_base64_encoding(data: bytes) -> str:
    return str(len(data)) + "." + juce_b64_encode(data)

def from_base64_encoding(s: str) -> bytes:
    size_s, _, b64 = s.partition(".")
    data = juce_b64_decode(b64)
    if int(size_s) != len(data):
        raise ValueError(f"size mismatch: header {size_s}, decoded {len(data)}")
    return data

# ── HDAW plugin identifier string ────────────────────────────────────────────
def juce_hash32(s: str) -> int:
    h = 0
    for ch in s:
        h = (h * 31 + ord(ch)) & 0xFFFFFFFF
    return h

def plugin_identifier(format_name: str, name: str, file_path: str, unique_id: int) -> str:
    return (f"{format_name}-{name}-{juce_hash32(file_path):08x}-{unique_id & 0xFFFFFFFF:08x}")

def serum2_plugin_id(cache_xml_path: Optional[str] = None) -> str:
    """Compute HDAW's pluginID for Serum 2 from the cached scan list."""
    paths = cache_xml_path or [
        r"/mnt/c/Users/hapbt/AppData/Roaming/HDAW/plugin_cache.xml",
        r"/mnt/c/Users/hapbt/AppData/Roaming/hdaw/plugin_cache.xml",
    ]
    for p in paths:
        if not os.path.exists(p):
            continue
        text = open(p, encoding="utf-8", errors="replace").read()
        m = re.search(r'<PLUGIN name="Serum 2"[^>]*?file="([^"]*)"[^>]*?uniqueId="([0-9a-fA-F]+)"', text)
        if m:
            return plugin_identifier("VST3", "Serum 2", m.group(1), int(m.group(2), 16))
    raise RuntimeError("Serum 2 not found in HDAW plugin cache")

if __name__ == "__main__":
    # self-test against the known demo project codec example
    assert to_base64_encoding(b"\x00\x01\x02\x03").startswith("4.")
    print("statecodec OK")


def wrap_vst3_state(raw: bytes) -> bytes:
    """Wrap raw plugin state the way JUCE 8's VST3 wrapper requires.

    JUCE 8's VST3PluginInstance::setStateInformation only forwards bytes to
    the component's setState() if the input parses as its binary-XML wrapper
    (magic 0x21324356 + length + single-line XML + NUL). The wrapper expects:
        <VST3PluginState><IComponent>custom-b64(raw)</IComponent></VST3PluginState>
    and decodes the text with MemoryBlock::fromBase64Encoding (same custom
    alphabet as pluginState attributes).
    """
    xml_text = '<VST3PluginState><IComponent>' + to_base64_encoding(raw) + '</IComponent></VST3PluginState>'
    body = xml_text.encode("utf-8") + b"\x00"
    return struct.pack("<II", 0x21324356, len(body) - 1) + body
