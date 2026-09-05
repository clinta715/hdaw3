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


TRACK5_SECTIONS = [
    {"type": "intro", "bars": 16, "minTracks": 4, "maxTracks": 5, "minPercTracks": 1, "maxPercTracks": 2},
    {"type": "build", "bars": 32, "minTracks": 5, "maxTracks": 8, "minPercTracks": 2, "maxPercTracks": 4},
    {"type": "breakdown", "bars": 8, "minTracks": 3, "maxTracks": 5, "minPercTracks": 1, "maxPercTracks": 2},
    {"type": "peak", "bars": 48, "minTracks": 7, "maxTracks": 9, "minPercTracks": 3, "maxPercTracks": 5},
    {"type": "build", "bars": 24, "minTracks": 5, "maxTracks": 8, "minPercTracks": 2, "maxPercTracks": 4},
    {"type": "breakdown", "bars": 8, "minTracks": 3, "maxTracks": 5, "minPercTracks": 1, "maxPercTracks": 2},
    {"type": "peak", "bars": 40, "minTracks": 7, "maxTracks": 9, "minPercTracks": 3, "maxPercTracks": 5},
    {"type": "outro", "bars": 8, "minTracks": 3, "maxTracks": 4, "minPercTracks": 1, "maxPercTracks": 2},
]  # 184 bars == 736 beats == ~5.26 min @ 140 BPM

# Frequency-band assignment per element (DarkForest lesson): constrain each
# sound to its band (HP filter) and boost the band it lives in (EQ peak), so
# parts are made louder IN their band, not the whole sound.
BANDS = {
    "kick": (30, 120, 55),
    "bass": (40, 300, 90),
    "hat": (4000, 20000, 9000),
    "snare": (300, 8000, 1400),
    "rim": (300, 8000, 1100),
    "clap": (300, 8000, 1500),
    "arp": (300, 8000, 2200),
    "stab": (300, 6000, 1300),
    "pad": (150, 6000, 500),
    "riser": (400, 20000, 2000),
    "down": (400, 20000, 2000),
}


def find_sample(e, query, prefer):
    r = e.tool("search_library", {"query": query})
    hits = r if isinstance(r, list) else []
    for h in hits:
        p = h.get("path", "")
        if p.lower().endswith(".wav") and any(s in p.lower() for s in prefer):
            return p
    for h in hits:
        p = h.get("path", "")
        if p.lower().endswith(".wav"):
            return p
    return None


# Factory psytrance per-role FX chains (filters/saturation/reverb/compression),
# loaded onto the track BEFORE the instrument is prepended at slot 0.
CHAINS = {"kick": "Kick Punch", "bass": "Bass Glue", "hat": "Hat Air",
          "arp": "Arp Width", "stab": "Stab Snip", "pad": "Pad Shimmer",
          "riser": "Riser Sweep", "down": "Riser Sweep"}


# Extra heavy psytrance FX on top of the factory chains (the chains are 3-4
# slots; these add more distortion/saturation, tempo-synced delay, reverb,
# compression, EQ, and goa phaser/chorus swirl). Genre-informed (2026-09-04):
# dotted/triplet tempo-synced delays (Division 4/3), short reverb on leads,
# big reverb on pads, hard waveshaping (saturator type 2) on bass/acid.
# Each entry: (fxType, {paramIndex: value}) appended after instrument+filter+chain.
EXTRA_FX = {
    "bass": [("saturator", {0: 18.0, 1: 2.0, 4: 6.0}),
             ("eq", {0: 400.0, 1: 2.0, 2: -3.0}),   # notch dip ~400Hz (mud)
             ("compressor", {0: -24.0, 1: 8.0, 3: 80.0})],
    "arp":  [("eq", {0: 4500.0, 1: 0.8, 2: 4.0}), ("saturator", {0: 16.0, 1: 2.0, 4: 4.0})],
    "stab": [("eq", {0: 3200.0, 1: 0.8, 2: 3.0}), ("saturator", {0: 14.0, 1: 2.0, 4: 4.0}),
             ("delay", {3: 1.0, 4: 5.0, 1: 0.4, 2: 0.4}),   # dotted-1/16, fb 0.4
             ("reverb", {2: 0.12})],                        # short plate, 12% wet
    "pad":  [("saturator", {0: 10.0, 4: 3.0}), ("phaser", {0: 0.2, 1: 0.5, 4: 0.5}),
             ("reverb", {0: 0.75, 2: 0.45})],
    "riser": [("saturator", {0: 4.0}), ("reverb", {2: 0.4}), ("delay", {3: 1.0, 4: 4.0, 1: 0.5, 2: 0.4})],
    "down":  [("saturator", {0: 4.0}), ("reverb", {2: 0.35}), ("delay", {3: 1.0, 4: 4.0, 1: 0.5, 2: 0.4})],
}


def fx_count(e, tid):
    r = e.tool("read.getFxSlots", {"trackIndex": tid})
    return len(r) if isinstance(r, list) else 0


def track5(e, ids, seed=4242, bpm=140, out_name="psy5min_demo.wav"):
    e.tool("new_project", {})
    e.tool("set_tempo", {"bpm": bpm})
    roles = ["kick", "bass", "hat", "snare", "rim", "arp", "stab", "pad",
             "clap", "riser", "down"]
    perc_note = {"kick": 41, "hat": 44, "snare": 38, "rim": 37, "clap": 42}
    fx = {"kick": "sampler", "hat": "sampler", "snare": "sampler", "rim": "sampler",
          "clap": "sampler", "bass": "growl_bass", "arp": "psyarp", "stab": "fm_synth",
          "pad": "sub_synth", "riser": "psy_fm", "down": "psy_fm"}
    tracks = {}
    extra_idx = {}
    for role in roles:
        r = e.tool("add_track", {"name": role.title()})
        tid = (r or {}).get("trackId")
        if tid is None:
            print("add_track failed:", r)
            return
        # Factory psytrance FX chain first (eq/comp/sat/reverb/filter per role),
        # then prepend the instrument at slot 0 so FX run after it.
        chain = CHAINS.get(role)
        if chain:
            lc = e.tool("load_fx_chain", {"trackId": tid, "name": chain})
            print(f"{role}: fx chain {chain} -> {lc.get('ok')}")
        e.tool("add_fx", {"trackId": tid, "fxType": fx[role], "position": 0})
        # Acid/303 + growl instrument tuning (aggressive psy character).
        if role == "arp":  # psy_arp: saw, resonant filter sweep, ping-pong delay, low octave
            for pi, v in {0: 0.0, 2: 12.0, 3: 1.0, 4: 2.0, 5: 2.0, 6: 900.0,
                          7: 12.0, 8: 4.0, 9: 0.375, 10: 0.6, 11: 1.0, 12: 0.45,
                          13: 2.0, 14: 0.02, 15: 0.1, 16: 1.0, 17: 0.25, 18: 0.4,
                          19: 0.45}.items():
                e.tool("set_internal_fx_param",
                       {"trackId": tid, "slotIndex": 0, "paramIndex": pi, "value": v})
        if role == "bass":  # growl_bass: hard clip, heavy drive, resonant filter
            for pi, v in {0: 55.0, 2: 0.7, 4: 2.0, 5: 22.0, 6: 0.2, 8: 700.0,
                          9: 6.0, 10: 0.7, 16: 0.7, 17: 1.0, 18: 2.0}.items():
                e.tool("set_internal_fx_param",
                       {"trackId": tid, "slotIndex": 0, "paramIndex": pi, "value": v})
        # Filter + LFO wobble on the melodic synths (psytrance movement):
        # filter at slot 1 (after the instrument), LFO on its cutoff (200).
        if role in ("bass", "arp", "stab", "pad"):
            e.tool("add_fx", {"trackId": tid, "fxType": "filter", "position": 1})
            li = e.tool("add_lfo", {"trackId": tid})
            lfo = (li or {}).get("lfoIndex", 0)
            e.tool("set_lfo_param", {"trackId": tid, "lfoIndex": lfo,
                                     "param": "targetParamID", "value": 200})
            e.tool("set_lfo_param", {"trackId": tid, "lfoIndex": lfo,
                                     "param": "rate", "value": 0.5})
            e.tool("set_lfo_param", {"trackId": tid, "lfoIndex": lfo,
                                     "param": "depth", "value": 0.5})
        # Heavy extra FX (distortion/sat/delay/reverb/comp/eq) per role.
        for fxt, params in EXTRA_FX.get(role, []):
            si = fx_count(e, tid)
            e.tool("add_fx", {"trackId": tid, "fxType": fxt})
            extra_idx.setdefault(role, {})[fxt] = si
            for pi, val in params.items():
                e.tool("set_internal_fx_param",
                       {"trackId": tid, "slotIndex": si, "paramIndex": pi, "value": val})
        tracks[role] = tid
    print("palette tracks:", tracks)

    # Frequency-band grouping: HP-filter out-of-band mud, EQ-boost the band
    # each element owns. Parts are made louder IN their band (not whole-sound
    # volume) per the DarkForest mix lesson.
    for role in roles:
        if role not in BANDS:
            continue
        hp, _lp, boost = BANDS[role]
        e.tool("add_fx", {"trackId": tracks[role], "fxType": "filter"})
        si = fx_count(e, tracks[role]) - 1
        e.tool("set_internal_fx_param", {"trackId": tracks[role], "slotIndex": si,
                                         "paramIndex": 1, "value": 1})     # mode HP
        e.tool("set_internal_fx_param", {"trackId": tracks[role], "slotIndex": si,
                                         "paramIndex": 0, "value": hp})
        e.tool("add_fx", {"trackId": tracks[role], "fxType": "eq"})
        si = fx_count(e, tracks[role]) - 1
        e.tool("set_internal_fx_param", {"trackId": tracks[role], "slotIndex": si,
                                         "paramIndex": 0, "value": boost})  # band centre
        e.tool("set_internal_fx_param", {"trackId": tracks[role], "slotIndex": si,
                                         "paramIndex": 2,
                                         "value": 0.0 if role in ("riser", "down")
                                         else (3.0 if role in ("hat", "snare", "rim", "clap") else 2.0)})    # +4 dB in-band
    print("band EQ applied per role")

    # Genre LFOs feeding instrument/effect params (psytrance hallmark): acid
    # filter wobble + delay feedback swell on psy_arp (slot 0 params 6/10), growl
    # filter wobble on growl_bass (slot 0 param 8), pad reverb/phaser wash + pan.
    def effect_lfo(role, slot, param, rate, depth):
        li = e.tool("add_lfo", {"trackId": tracks[role]})
        li = (li or {}).get("lfoIndex", 0)
        e.tool("set_lfo_param", {"trackId": tracks[role], "lfoIndex": li,
                                 "param": "targetParamID",
                                 "value": 100 + slot * 100 + param})
        e.tool("set_lfo_param", {"trackId": tracks[role], "lfoIndex": li,
                                 "param": "rate", "value": rate})
        e.tool("set_lfo_param", {"trackId": tracks[role], "lfoIndex": li,
                                 "param": "depth", "value": depth})

    effect_lfo("arp", 0, 6, 0.25, 0.4)      # psy_arp filter cutoff wobble (acid)
    effect_lfo("arp", 0, 10, 0.25, 0.35)    # psy_arp delay feedback swell
    effect_lfo("bass", 0, 8, 0.5, 0.3)      # growl_bass filter cutoff wobble
    if "reverb" in extra_idx.get("pad", {}):
        effect_lfo("pad", extra_idx["pad"]["reverb"], 0, 0.1, 0.35)    # size wash
    if "phaser" in extra_idx.get("pad", {}):
        effect_lfo("pad", extra_idx["pad"]["phaser"], 0, 0.15, 0.4)    # phaser rate
    effect_lfo("pad", 0, 2, 0.1, 0.5)       # pan sweep on the pad (target 2)

    # Gain staging: fm_synth arps run ~2x hotter than psy_fm/growl (verified by
    # the engines phase); balance so bass/pad/stab sit above the arp instead of
    # being masked by it.
    vols = {"kick": 0.9, "bass": 0.95, "hat": 0.75, "snare": 0.85, "rim": 0.7,
            "arp": 0.28, "stab": 0.75, "pad": 0.42, "clap": 0.85, "riser": 0.35,
            "down": 0.35}
    for role, v in vols.items():
        e.tool("set_track", {"trackId": tracks[role], "volume": v})

    for role, note in perc_note.items():
        src = find_sample(e, role, ["kick" if role == "kick" else role])
        if src:
            e.tool("sampler_set_sample",
                   {"trackId": tracks[role], "slotIndex": 0,
                    "filePath": src, "rootNote": note})
            print(f"loaded {role} sample: {os.path.basename(src)}")

    # DX7 patches on arp/stab; a LOUD Virus voice on pad (sub_synth).
    dx7dir = os.path.join(BASE, "demo_libs", "dx7")
    virdir = os.path.join(BASE, "demo_libs", "virus")
    dx7patches = sorted(f for f in os.listdir(dx7dir) if not f.endswith(".json"))
    for role, pf in zip(("arp", "stab"), dx7patches[:2]):
        imp = e.tool("fm_synth_import_sysex",
                     {"trackId": tracks[role], "slotIndex": 0,
                      "filePath": os.path.join(dx7dir, pf)})
        print(f"loaded {pf} -> {role}: {imp.get('voiceName')}")
    virus_patch = os.path.join(virdir, "wc_olo_garb_virus_ti-c01.syx")
    if os.path.exists(virus_patch):
        imp = e.tool("sub_synth_import_sysex",
                     {"trackId": tracks["pad"], "slotIndex": 0,
                      "filePath": virus_patch})
        print(f"loaded Virus -> pad: {imp.get('name')}")

    total_bars = sum(s["bars"] for s in TRACK5_SECTIONS)
    total_beats = total_bars * 4

    # ── Background / atmospheric layers (outside the markov palette) ──
    # Ambience bed: a 145bpm ambient loop from e:\samples, looped over the track,
    # with filter + reverb and low volume so it sits under the arrangement.
    amb = e.tool("add_track", {"name": "Ambience"})
    atid = (amb or {}).get("trackId")
    if atid is not None:
        e.tool("add_fx", {"trackId": atid, "fxType": "filter", "position": 0})
        e.tool("add_fx", {"trackId": atid, "fxType": "reverb", "position": 1})
        loop = find_sample(e, "ambience", ["Ambience"])
        if loop:
            cl = e.tool("add_audio_clip",
                        {"trackId": atid, "start": 0.0, "length": total_beats,
                         "sourceFile": loop, "name": "Atmo"})
            cid = (cl or {}).get("clipId")
            if cid is not None:
                e.tool("set_clip", {"clipId": cid, "looping": True})
            e.tool("set_track", {"trackId": atid, "volume": 0.18})
            print(f"ambience bed: {os.path.basename(loop)}")

    # FX hits: riser sweeps into the drops and impacts at section boundaries.
    fxh = e.tool("add_track", {"name": "FX Hits"})
    ftid = (fxh or {}).get("trackId")
    if ftid is not None:
        sweep = find_sample(e, "sweep", ["Sweep"])
        impact = find_sample(e, "impact", ["Impact"])
        for start, ln, tag in [(220, 4, "riser->drop"), (224, 2, "impact@drop"),
                               (508, 4, "down->breakdown"), (704, 4, "outro")]:
            src = impact if tag.startswith("impact") else sweep
            if src:
                e.tool("add_audio_clip",
                       {"trackId": ftid, "start": start, "length": ln,
                        "sourceFile": src, "name": tag})
        e.tool("set_track", {"trackId": ftid, "volume": 0.22})
        print("fx hits placed (riser->drop, impact@drop, down->breakdown, outro)")
        # More FX hits at the second drop + into the second peak.
        for start, ln, tag in [(252, 4, "riser->2nd-drop"), (256, 2, "impact@2nd-drop"),
                               (540, 4, "riser->2nd-peak")]:
            src = impact if tag.startswith("impact") else sweep
            if src:
                e.tool("add_audio_clip",
                       {"trackId": ftid, "start": start, "length": ln,
                        "sourceFile": src, "name": tag})

    # Percussion loop track: a top-loop/percussion loop from e:\samples for density.
    ptrack = e.tool("add_track", {"name": "Perc Loop"})
    ptid = (ptrack or {}).get("trackId")
    if ptid is not None:
        loop = find_sample(e, "percussion", ["Perc", "Top Loop"])
        if loop:
            e.tool("add_fx", {"trackId": ptid, "fxType": "filter", "position": 0})  # HP
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 0,
                                             "paramIndex": 1, "value": 1})
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 0,
                                             "paramIndex": 0, "value": 3000})
            e.tool("add_fx", {"trackId": ptid, "fxType": "saturator"})  # bitcrush lo-fi
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 1,
                                             "paramIndex": 1, "value": 3})   # type bitcrush
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 1,
                                             "paramIndex": 5, "value": 8})   # bits 8
            e.tool("add_fx", {"trackId": ptid, "fxType": "filter"})          # BP pocket
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 2,
                                             "paramIndex": 1, "value": 2})   # mode BP
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 2,
                                             "paramIndex": 0, "value": 2000})
            e.tool("add_fx", {"trackId": ptid, "fxType": "delay"})           # fast shimmer
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 3,
                                             "paramIndex": 3, "value": 1})   # sync
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 3,
                                             "paramIndex": 4, "value": 1})   # 1/16
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 3,
                                             "paramIndex": 1, "value": 0.5})
            e.tool("set_internal_fx_param", {"trackId": ptid, "slotIndex": 3,
                                             "paramIndex": 2, "value": 0.35})
            li = e.tool("add_lfo", {"trackId": ptid})
            li = (li or {}).get("lfoIndex", 0)
            e.tool("set_lfo_param", {"trackId": ptid, "lfoIndex": li,
                                     "param": "targetParamID", "value": 2})  # pan
            e.tool("set_lfo_param", {"trackId": ptid, "lfoIndex": li,
                                     "param": "rate", "value": 0.25})
            e.tool("set_lfo_param", {"trackId": ptid, "lfoIndex": li,
                                     "param": "depth", "value": 0.5})
            cl = e.tool("add_audio_clip",
                        {"trackId": ptid, "start": 0.0, "length": total_beats,
                         "sourceFile": loop, "name": "Perc"})
            cid = (cl or {}).get("clipId")
            if cid is not None:
                e.tool("set_clip", {"clipId": cid, "looping": True})
            e.tool("set_track", {"trackId": ptid, "volume": 0.3})
            print(f"perc loop: {os.path.basename(loop)}")

    # Drone track: sustained low sub chord across the whole track so a tonal
    # bed is always present (core content never fully drops out).
    dr = e.tool("add_track", {"name": "Drone"})
    drid = (dr or {}).get("trackId")
    if drid is not None:
        e.tool("add_fx", {"trackId": drid, "fxType": "sub_synth", "position": 0})
        e.tool("add_fx", {"trackId": drid, "fxType": "reverb", "position": 1})
        e.tool("set_internal_fx_param", {"trackId": drid, "slotIndex": 1,
                                         "paramIndex": 2, "value": 0.5})  # big wet
        cl = e.tool("add_midi_clip",
                    {"trackId": drid, "start": 0, "length": total_beats, "name": "Drone"})
        cid = (cl or {}).get("clipId")
        if cid is not None:
            e.tool("add_notes", {"clipId": cid, "notes": [
                {"pitch": 29, "start": 0, "duration": total_beats, "velocity": 60},
                {"pitch": 33, "start": 0, "duration": total_beats, "velocity": 55},
                {"pitch": 36, "start": 0, "duration": total_beats, "velocity": 50}]})
        e.tool("set_track", {"trackId": drid, "volume": 0.12})
        print("drone bed added")

    gen = e.tool("generate_psytrance_markov", {
        "paletteTrackIds": {role: tracks[role] for role in roles},
        "totalBars": total_bars, "sections": TRACK5_SECTIONS,
        "keyRoot": 5, "scaleMode": 1, "density": 0.8,
        "seed": seed, "minTracks": 6, "maxTracks": 9,
        "minPercTracks": 2, "maxPercTracks": 5, "sectionCycleBars": 0,
    }, timeout=600)
    print("markov:", json.dumps(gen)[:500])

    # Trim the master bus so 11 summing tracks don't clip at full scale.
    e.tool("set_master_gain", {"gain": 0.24})

    out = os.path.join(BASE, "demo_libs", out_name)
    exp = e.export_and_wait({"outputPath": out, "format": "wav", "sampleRate": 48000,
                             "bitDepth": 24, "start": 0.0, "end": 0.0, "queue": True},
                            timeout=600)
    print("export:", exp)
    print("wav:", out, os.path.exists(out))


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


def engines(e):
    """Render each internal synth in isolation and report peak/rms so we know
    which engines actually output audio (and at what level)."""
    import analyze_probe as AP
    tests = [
        ("fm_synth", "fm_synth", None),
        ("fm_synth+patch", "fm_synth", "ACCORD01.SYX"),
        ("psy_fm", "psy_fm", None),
        ("sub_synth", "sub_synth", None),
        ("growl_bass", "growl_bass", None),
        ("psy_arp", "psyarp", None),
        ("sampler", "sampler", None),
    ]
    for label, fxtype, patch in tests:
        e.tool("new_project", {})
        r = e.tool("add_track", {"name": label})
        tid = (r or {}).get("trackId")
        if tid is None:
            print(f"{label}: add_track failed {r}")
            continue
        e.tool("add_fx", {"trackId": tid, "fxType": fxtype, "position": 0})
        if patch:
            p = os.path.join(BASE, "demo_libs", "dx7", patch)
            if os.path.exists(p):
                e.tool("fm_synth_import_sysex",
                       {"trackId": tid, "slotIndex": 0, "filePath": p})
        cl = e.tool("add_midi_clip", {"trackId": tid, "start": 0, "length": 8, "name": "Probe"})
        cid = (cl or {}).get("clipId")
        if cid is not None:
            e.tool("add_notes", {"clipId": cid, "notes": [
                {"pitch": 60, "start": 0, "duration": 8, "velocity": 100}]})
        out = os.path.join(BASE, "demo_libs", "_engine_check.wav")
        e.export_and_wait({"outputPath": out, "format": "wav", "sampleRate": 48000,
                           "bitDepth": 24, "start": 0.0, "end": 4.0, "queue": True}, timeout=120)
        if os.path.exists(out):
            rep = AP.analyze_probe(out, "lead", use_clap=False, use_llm=False)
            m = rep["measurements"]
            print(f"{label:16s} rms={m['rms']:.4f} peak={m['peak']:.4f} centroid={m['centroid']:.0f}")
            os.remove(out)
        else:
            print(f"{label:16s} NO WAV")


def synthcheck(e):
    """Solo-render each synth role WITH its factory FX chain + a sustained note,
    so we can see the real per-role levels the mix is built from."""
    import analyze_probe as AP
    tests = [
        ("bass(psy_fm)", "psy_fm", None, "Bass Glue", None),
        ("arp(fm+ACCORDION)", "fm_synth", "ACCORD01.SYX", "Arp Width", None),
        ("stab(fm+Trombone)", "fm_synth", "BANK3.SYX", "Stab Snip", None),
        ("pad(fm+Bay.Brass)", "fm_synth", "BRASS-01.SYX", "Pad Shimmer", None),
        ("sub(virus)", "sub_synth", "wc_olo_garb_virus_ti-a02.syx", "Acid Lead", "sub"),
        ("sub(default)", "sub_synth", None, "Acid Lead", None),
    ]
    for label, fxtype, patch, chain, imp in tests:
        e.tool("new_project", {})
        r = e.tool("add_track", {"name": label})
        tid = (r or {}).get("trackId")
        if tid is None:
            print(f"{label}: add_track failed")
            continue
        if chain:
            e.tool("load_fx_chain", {"trackId": tid, "name": chain})
        e.tool("add_fx", {"trackId": tid, "fxType": fxtype, "position": 0})
        if patch:
            src = None
            if imp == "sub":
                p = os.path.join(BASE, "demo_libs", "virus", patch)
            else:
                p = os.path.join(BASE, "demo_libs", "dx7", patch)
            if os.path.exists(p):
                tool = "sub_synth_import_sysex" if imp == "sub" else "fm_synth_import_sysex"
                e.tool(tool, {"trackId": tid, "slotIndex": 0, "filePath": p})
        cl = e.tool("add_midi_clip", {"trackId": tid, "start": 0, "length": 8, "name": "Probe"})
        cid = (cl or {}).get("clipId")
        if cid is not None:
            e.tool("add_notes", {"clipId": cid, "notes": [
                {"pitch": 60, "start": 0, "duration": 8, "velocity": 100}]})
        out = os.path.join(BASE, "demo_libs", "_synth_check.wav")
        e.export_and_wait({"outputPath": out, "format": "wav", "sampleRate": 48000,
                           "bitDepth": 24, "start": 0.0, "end": 4.0, "queue": True}, timeout=120)
        if os.path.exists(out):
            rep = AP.analyze_probe(out, "lead", use_clap=False, use_llm=False)
            m = rep["measurements"]
            print(f"{label:26s} rms={m['rms']:.4f} peak={m['peak']:.4f} centroid={m['centroid']:.0f}")
            os.remove(out)
        else:
            print(f"{label:26s} NO WAV")


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", default="setup")
    ap.add_argument("--engine-bin", default=ENGINE)
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--bars", type=int, default=64)
    ap.add_argument("--bpm", type=int, default=140)
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
        elif args.phase == "track5":
            track5(e, {}, seed=args.seed, bpm=args.bpm, out_name=args.out)
        elif args.phase == "engines":
            engines(e)
        elif args.phase == "synthcheck":
            synthcheck(e)
        elif args.phase == "all":
            setup(e)
            cluster(e)
            search(e)
            compose(e, {}, seed=args.seed, total_bars=args.bars, out_name=args.out)
    finally:
        e.proc.kill()


if __name__ == "__main__":
    main()