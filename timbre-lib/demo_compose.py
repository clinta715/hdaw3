#!/usr/bin/env python3
"""End-to-end composition demo driver for HDAW.

Drives a spawned `build\\HDAW_headless.exe --mcp-stdio` engine over JSON-RPC
tools/call. Phases:
  setup    new project, add+scan patch/audio/midi libraries, list them
  cluster  cluster_library (dsp) + search_library queries
  compose  build palette tracks (internal synths + a found patch), run
           generate_psytrance_markov, export audio
Usage: py -3.14 timbre-lib/demo_compose.py [--phase setup|cluster|compose]
"""
import argparse, json, os, subprocess, sys, threading, queue, time

ENGINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "HDAW_headless.exe")
BASE = os.path.dirname(os.path.abspath(__file__))


class Engine:
    def __init__(self, engine_bin=None):
        self.proc = subprocess.Popen([engine_bin or ENGINE, "--mcp-stdio"],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, bufsize=0)
        self.q = queue.Queue()
        self.nid = 0
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while True:
            line = self.proc.stdout.readline()
            if not line:
                self.q.put(None)
                return
            self.q.put(line)

    def _recv(self, timeout=60):
        while True:
            line = self.q.get(timeout=timeout)
            if line is None:
                raise RuntimeError("engine stdout closed")
            msg = json.loads(line)
            if "id" in msg and msg["id"] != self.nid:
                continue
            return msg

    def call(self, method, params, timeout=120):
        """Call an RPC method directly (notifications discarded)."""
        self.nid += 1
        req = {"jsonrpc": "2.0", "id": self.nid, "method": method, "params": params}
        self.proc.stdin.write((json.dumps(req) + "\n").encode("utf-8"))
        self.proc.stdin.flush()
        return self._recv(timeout=timeout)

    def tool(self, name, args, timeout=300):
        """Call an MCP tool via tools/call; returns the result JSON or text."""
        resp = self.call("tools/call", {"name": name, "arguments": args}, timeout=timeout)
        result = resp.get("result") or {}
        content = result.get("content") or []
        if content and content[0].get("type") == "text":
            text = content[0]["text"]
            try:
                return json.loads(text)
            except Exception:
                return {"_text": text}
        return {"_text": json.dumps(result)}

    def export_and_wait(self, args, timeout=240):
        """Start export_audio and wait for notifications/exportComplete."""
        self.nid += 1
        req = {"jsonrpc": "2.0", "id": self.nid, "method": "tools/call",
               "params": {"name": "export_audio", "arguments": args}}
        self.proc.stdin.write((json.dumps(req) + "\n").encode("utf-8"))
        self.proc.stdin.flush()
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.q.get(timeout=timeout)
            if line is None:
                raise RuntimeError("engine stdout closed during export")
            msg = json.loads(line)
            if "id" in msg and msg["id"] == self.nid:
                continue  # the "export started" response
            if isinstance(msg, dict) and msg.get("method") == "notifications/exportComplete":
                return msg.get("params", {})
        raise RuntimeError("export timeout")


def setup(e):
    libs = [
        ("DX7 Demo", os.path.join(BASE, "demo_libs", "dx7"), "patch"),
        ("Virus Demo", os.path.join(BASE, "demo_libs", "virus"), "patch"),
        ("Psytrance Samples", r"E:\samples\Prism - Psytrance - Zenhiser", "audio"),
        ("Psytrance Samples 2", r"E:\samples\Zenhiser Studio Essentials - Psytrance", "audio"),
        ("Psytrance Midi", r"E:\samples\Antinomy Psytrance Sounds Vol.2 WAV MiDi-ARCADiA", "midi"),
    ]
    ids = {}
    for name, path, typ in libs:
        if not os.path.isdir(path):
            print(f"skip missing: {path}")
            continue
        r = e.tool("add_library", {"name": name, "path": path, "type": typ})
        lid = r.get("id")
        ids[name] = lid
        print(f"add_library {name} ({typ}) -> {lid}")
        s = e.tool("scan_library", {"id": lid})
        print(f"  scan: {s}")
    time.sleep(4)
    with open(os.path.join(BASE, "demo_libs", ".lib_ids.json"), "w", encoding="utf-8") as f:
        json.dump(ids, f)
    print("saved lib ids:", ids)
    return ids


def _load_ids():
    p = os.path.join(BASE, "demo_libs", ".lib_ids.json")
    if not os.path.exists(p):
        return {}
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def wait_scanned(e, ids, timeout=120):
    for name, lid in ids.items():
        e.tool("scan_library", {"id": lid})
    deadline = time.time() + timeout
    counts = {}
    while time.time() < deadline:
        libs = e.tool("list_libraries", {})
        arr = libs if isinstance(libs, list) else []
        by_id = {l.get("id"): l.get("fileCount", 0) for l in arr}
        counts = {k: by_id.get(v, 0) for k, v in ids.items()}
        if all(c > 0 for c in counts.values()):
            break
        time.sleep(2)
    print("scanned fileCounts:", counts)


def cluster(e):
    ids = _load_ids()
    # cluster_library scopes to audio/patch libraries; exclude midi.
    wait_scanned(e, ids)
    lib_ids = [v for k, v in ids.items() if "Midi" not in k]
    cl = e.tool("cluster_library", {"libraryIds": lib_ids, "method": "dsp", "k": 6})
    print("=== CLUSTER (dsp, auto-k) over demo libs ===")
    if not cl.get("clusters"):
        print("RAW:", json.dumps(cl)[:1200])
        return
    for c in cl.get("clusters", []):
        print(f"\n[{c.get('id')}] {c.get('label')} ({len(c.get('members', []))} members)")
        for m in c.get("members", [])[:8]:
            print(f"   {m.get('similarity', 0):.2f} {os.path.basename(m.get('path', ''))} :: {(m.get('description') or '')[:60]}")
    un = cl.get("unassigned", [])
    if un:
        print(f"\nunassigned ({len(un)}):", ", ".join(os.path.basename(m.get('path','')) for m in un[:10]))


def search(e):
    wait_scanned(e, _load_ids())
    for q in ["dark", "gritty", "tonal", "bright", "kick", "acid"]:
        r = e.tool("search_library", {"query": q})
        hits = r if isinstance(r, list) else []
        print(f"\n=== search '{q}' -> {len(hits)} ===")
        for h in hits[:6]:
            print(f"   {os.path.basename(h.get('path',''))} [{h.get('patchEngine') or h.get('format')}] :: {(h.get('description') or '')[:70]}")


def compose(e, ids, seed=1337, total_bars=64, out_name="markov_demo.wav"):
    # Fresh project on the spawned engine; build palette tracks with internal
    # synths, load two found DX7 patches, run the markov generator, export.
    e.tool("new_project", {})
    roles = ["kick", "bass", "hat", "arp", "stab", "pad"]
    fx = {"kick": "fm_synth", "bass": "psy_fm", "hat": "fm_synth",
          "arp": "fm_synth", "stab": "fm_synth", "pad": "psy_fm"}
    tracks = {}
    for i, role in enumerate(roles):
        r = e.tool("add_track", {"name": role.title()})
        tid = (r or {}).get("trackId")
        if tid is None:
            print("add_track failed:", r)
            return
        e.tool("add_fx", {"trackId": tid, "fxType": fx[role], "position": 0})
        tracks[role] = tid
    print("palette tracks:", tracks)

    # Load two swept DX7 patches (bright brass -> arp/stab) from demo_libs.
    dx7dir = os.path.join(BASE, "demo_libs", "dx7")
    patches = sorted(f for f in os.listdir(dx7dir) if not f.endswith(".json"))[:2]
    for role, pf in zip(("arp", "stab"), patches):
        imp = e.tool("fm_synth_import_sysex",
                     {"trackId": tracks[role], "slotIndex": 0,
                      "filePath": os.path.join(dx7dir, pf)})
        print(f"loaded {pf} -> {role}: {imp}")

    gen = e.tool("generate_psytrance_markov", {
        "paletteTrackIds": {role: tracks[role] for role in roles},
        "totalBars": total_bars, "keyRoot": 5, "scaleMode": 1, "density": 0.75,
        "seed": seed, "minTracks": 4, "maxTracks": 7,
        "minPercTracks": 2, "maxPercTracks": 4, "sectionCycleBars": 16,
    }, timeout=300)
    print("markov:", json.dumps(gen)[:800])

    out = os.path.join(BASE, "demo_libs", out_name)
    exp = e.export_and_wait({"outputPath": out, "format": "wav", "sampleRate": 48000,
                             "bitDepth": 24, "start": 0.0, "end": 0.0, "queue": True})
    print("export:", exp)
    print("wav:", out, os.path.exists(out))


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", default="setup")
    ap.add_argument("--engine-bin", default=ENGINE)
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--bars", type=int, default=64)
    ap.add_argument("--out", default="markov_demo.wav")
    args = ap.parse_args()
    e = Engine(args.engine_bin)
    try:
        r = e.call("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}})
        print("engine:", (r.get("result") or {}).get("serverInfo", {}).get("version"))
        if args.phase == "setup":
            setup(e)
        elif args.phase == "cluster":
            cluster(e)
        elif args.phase == "search":
            search(e)
        elif args.phase == "compose":
            compose(e, {}, seed=args.seed, total_bars=args.bars, out_name=args.out)
        elif args.phase == "all":
            setup(e)
            cluster(e)
            search(e)
            compose(e, {}, seed=args.seed, total_bars=args.bars, out_name=args.out)
    finally:
        e.proc.kill()


if __name__ == "__main__":
    main()