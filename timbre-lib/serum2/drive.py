
"""drive.py — drive the Serum 2 injection experiment through HDAW's MCP server.

End-to-end loop:
  1. load_project(<authored .hdaw3>)
  2. summary / list_fx / list_fx_params        (did Serum 2 instantiate?)
  3. param probe across slots                  (did the state actually land?)
  4. save_project(<copy>)                      (HDAW re-captures live state)
  5. reload saved copy, diff pluginState       (is the round-trip stable?)
  6. export_audio(<wav>)                       (does it render?)

Run from IPython:
    from serum2 import drive   (or execfile)
    await drive.check_and_report(project_path, mcp=mcp)
"""
from __future__ import annotations
import json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from statecodec import from_base64_encoding, to_base64_encoding
except Exception:
    from statecodec import from_base64_encoding, to_base64_encoding


async def call(mcp, tool: str, args: dict):
    try:
        r = await mcp.call_tool("hdaw", tool, args)
        return r
    except Exception as e:
        return {"__error__": str(e)}


async def load_project(mcp, path): return await call(mcp, "load_project", {"filePath": path})
async def save_project(mcp, path): return await call(mcp, "save_project", {"filePath": path})
async def summary(mcp): return await call(mcp, "get_project_summary", {})
async def list_fx(mcp, track_id): return await call(mcp, "list_fx", {"trackId": track_id})
async def list_params(mcp, track_id, slot): return await call(mcp, "list_fx_params", {"trackId": track_id, "slotIndex": slot})
async def export_audio(mcp, path, start=0.0, end=8.0, track_ids=None):
    a = {"outputPath": path, "start": start, "end": end, "format": "wav", "sampleRate": 44100, "bitDepth": 16}
    if track_ids: a["trackIds"] = track_ids
    return await call(mcp, "export_audio", a)


def extract_plugin_states(project_path: str) -> list[dict]:
    """Read an .hdaw3 project file and return all plugin FX slots."""
    text = open(project_path, encoding="utf-8", errors="replace").read()
    slots = []
    # note: pluginState can be huge — use a non-greedy regex on the attribute
    for m in re.finditer(r'<FX_SLOT\b[^>]*>', text):
        attrs = m.group(0)
        d = {}
        for key in ("fxType", "pluginID", "pluginFormat", "name", "bypassed", "pluginState"):
            km = re.search(key + r'="([^"]*)"', attrs)
            d[key] = km.group(1) if km else None
        if d["fxType"] == "plugin":
            slots.append(d)
    return slots


def state_bytes(project_path: str) -> list[bytes]:
    out = []
    for s in extract_plugin_states(project_path):
        if s["pluginState"]:
            out.append(from_base64_encoding(s["pluginState"]))
        else:
            out.append(b"")
    return out


async def check_and_report(mcp, project_path: str, export_dir: str = None, export_seconds: float = 8.0):
    report = {}
    r1 = await load_project(mcp, project_path)
    report["load_project"] = str(r1)[:300]
    r2 = await summary(mcp)
    report["summary"] = str(r2)[:500]
    tracks_json = json.loads(str(r2)) if isinstance(str(r2), str) else {}
    # find track count
    m = re.search(r'"trackCount"\s*:\s*(\d+)', str(r2))
    n_tracks = int(m.group(1)) if m else None
    report["track_count"] = n_tracks
    fx = []
    for t in range(n_tracks or 0):
        r3 = await list_fx(mcp, t)
        fx.append(str(r3)[:500])
    report["list_fx"] = fx
    # param probe: first two plugin slots
    probe = {}
    for t in range(min(3, n_tracks or 0)):
        rp = await list_params(mcp, t, 0)
        probe[t] = str(rp)[:600]
    report["params_probe"] = probe
    saved = project_path.rsplit(".", 1)[0] + "_resaved.hdaw3"
    r4 = await save_project(mcp, saved)
    report["save_project"] = str(r4)[:200]
    if os.path.exists(saved):
        injected = state_bytes(project_path)
        resaved = state_bytes(saved)
        stable = len(injected) == len(resaved) and all(
            a == b for a, b in zip(injected, resaved))
        report["roundtrip_stable"] = stable
        report["state_sizes_injected"] = [len(b) for b in injected]
        report["state_sizes_resaved"] = [len(b) for b in resaved]
        if not stable and injected and resaved:
            a, b = injected[0], resaved[0]
            common = 0
            for x, y in zip(a, b):
                if x == y: common += 1
                else: break
            report["first_diff_offset"] = common
    if export_dir:
        os.makedirs(export_dir, exist_ok=True)
        wav = os.path.join(export_dir, os.path.splitext(os.path.basename(project_path))[0] + ".wav")
        r5 = await export_audio(mcp, wav, start=0.0, end=export_seconds)
        report["export_audio"] = str(r5)[:300]
        report["wav"] = wav if os.path.exists(wav) else None
    return report
