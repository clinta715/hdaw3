
"""buildproject.py — author an HDAW .hdaw3 project that instantiates Serum 2
with injected .SerumPreset states.

Each selected preset becomes one TRACK with one FX_SLOT:
    fxType="plugin" pluginID="<Serum2 id>" pluginFormat="VST3"
    pluginState="<JUCE base64 of the whole .SerumPreset file bytes>"

The track also carries a short MIDI arp clip so every slot sounds when the
project is rendered.

Usage:
    python serum2/buildproject.py --presets "a.SerumPreset" "b.SerumPreset" ... -o out.hdaw3
    python serum2/buildproject.py --catalog <harvest_out>/catalog.json --pick small --count 4 -o out.hdaw3
    (--pick small|medium|big|mixed; --max-size MB; --allow-huge)
"""
from __future__ import annotations
import argparse, json, os, sys, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from statecodec import to_base64_encoding, from_base64_encoding, serum2_plugin_id  # noqa: E402

ARP_NOTES = [60, 64, 67, 72, 67, 64, 67, 64, 62, 65, 69, 74, 69, 65, 69, 65]
NOTE_LEN = 0.5

def read_preset_bytes(path_or_pack: str, member: str | None) -> bytes:
    if member is None:
        with open(path_or_pack, "rb") as f:
            return f.read()
    with zipfile.ZipFile(path_or_pack) as z:
        return z.read(member)

def clip_xml(clip_id: int, note_id0: int, duration_beats: float) -> str:
    notes = []
    beat = 0.0
    for n in ARP_NOTES:
        notes.append(f'<MIDI_NOTE noteID="{note_id0}" noteNumber="{n}" velocity="0.8" '
                     f'startBeat="{beat:.3f}" durationBeats="{NOTE_LEN}"/>')
        note_id0 += 1
        beat += NOTE_LEN
    return (f'<CLIP clipID="{clip_id}" name="arp" startTime="0.0" duration="{duration_beats:.3f}" '
            f'offset="0.0" clipType="midi" gain="1.0" fadeIn="0.0" fadeOut="0.0" looping="0" color="-1213135">\n'
            f'  <MIDI_NOTE_LIST>\n    ' + "\n    ".join(notes) + "\n  </MIDI_NOTE_LIST>\n</CLIP>")

def build(presets, out_path: str, plugin_id: str, project_name: str = "Serum2 Inject Test") -> None:
    tracks = []
    used = []
    for i, (label, data, info) in enumerate(presets):
        state_enc = to_base64_encoding(data)
        safe = "".join(c if c.isprintable() and c not in '&<>"' else "_" for c in label)[:60] or f"preset{i}"
        track = (
            f'<TRACK name="{safe}" volume="0.8" pan="0.0" isMuted="0" isSoloed="0" '
            f'parentBus="0" color="-1213135" midiChannel="1">\n'
            f'  <CLIP_LIST>\n    {clip_xml(1000 + i, 5000 + i * 100, len(ARP_NOTES) * NOTE_LEN)}\n  </CLIP_LIST>\n'
            f'  <FX_CHAIN>\n'
            f'    <FX_SLOT fxType="plugin" pluginID="{plugin_id}" pluginFormat="VST3" '
            f'name="{safe}" bypassed="0" pluginState="{state_enc}"/>\n'
            f'  </FX_CHAIN>\n'
            f'  <AUTOMATION_LIST/>\n'
            f'</TRACK>'
        )
        tracks.append(track)
        used.append({"track": i, "label": label, "info": info,
                     "state_bytes": len(data), "state_base64_len": len(state_enc)})

    doc = (
        '<?xml version="1.0" encoding="UTF-8"?>\n\n'
        f'<PROJECT name="{project_name}" tempo="120.0">\n'
        '  <TRANSPORT position="0.0" isPlaying="0" loopStart="0.0" loopEnd="8.0" isLooping="0" '
        'timeSigNumerator="4" timeSigDenominator="4"/>\n'
        '  <TRACK_LIST>\n    ' + "\n    ".join(tracks) + "\n  </TRACK_LIST>\n"
        '  <SCALE_INFO scaleRoot="0" scaleMode="0"/>\n'
        '  <TEMPO_POINT_LIST>\n    <TEMPO_POINT startTime="0.0" tempo="120.0"/>\n  </TEMPO_POINT_LIST>\n'
        '  <ROUTING_GRAPH>\n    <BUS_LIST>\n'
        '      <BUS name="Master" busID="0" busType="master" busTarget="-1" fxType="none"/>\n'
        '    </BUS_LIST>\n  </ROUTING_GRAPH>\n'
        '</PROJECT>\n'
    )
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(doc)
    meta = {"project": out_path, "pluginID": plugin_id, "tracks": used}
    meta_path = out_path.rsplit(".", 1)[0] + "_used.json"
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=1)
    print(f"wrote {out_path} ({os.path.getsize(out_path)} bytes, {len(presets)} slots)")
    print(f"metadata: {meta_path}")

def load_selected(sel: list[str]) -> list[tuple[str, bytes, dict]]:
    out = []
    for s in sel:
        member = None
        if os.path.isdir(s) or not os.path.exists(s):
            if "::" in s:
                pack, member = s.split("::", 1)
                data = read_preset_bytes(pack, member)
            else:
                raise SystemExit(f"preset not found: {s}")
        else:
            pack = None
            data = read_preset_bytes(s, None)
        j = None
        if data[:9] == b"XferJson\x00":
            try:
                import struct
                n = int.from_bytes(data[9:13], "little")
                j = json.loads(data[17:17+n])
            except Exception:
                j = None
        label_src = member or s
        out.append((j.get("presetName", os.path.basename(label_src)) if j else os.path.basename(s),
                    data, {"hash": j.get("hash") if j else None}))
    return out

def pick_from_catalog(cat_path: str, strategy: str, count: int, max_size_mb: float) -> list[dict]:
    cat = json.load(open(cat_path, encoding="utf-8"))
    def size(pr):
        # pack members have size field; standalone too
        return pr.get("size") or 0
    if strategy == "small":
        pool = [pr for pr in cat["presets"] if 0 < size(pr) <= 1_000_000]
        pool.sort(key=size)
    elif strategy == "medium":
        pool = [pr for pr in cat["presets"] if 1_000_000 < size(pr) <= 10_000_000]
        pool.sort(key=size)
    elif strategy == "big":
        pool = [pr for pr in cat["presets"] if size(pr) > 10_000_000]
        pool.sort(key=size)
    else:  # mixed = evenly spread small/medium/big
        small = sorted([pr for pr in cat["presets"] if 0 < size(pr) <= 300_000], key=size)
        medium = sorted([pr for pr in cat["presets"] if 300_000 < size(pr) <= 10_000_000], key=size)
        big = sorted([pr for pr in cat["presets"] if size(pr) > 10_000_000], key=size)
        pool = (small[:max(1, count // 3)] + medium[:max(1, count // 3)] + big[:1])[:count]
    if max_size_mb:
        pool = [pr for pr in pool if size(pr) <= max_size_mb * 1_000_000]
    if not pool:
        raise SystemExit(f"no presets matched strategy={strategy}")
    return pool[:count]

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--presets", nargs="*", default=None)
    ap.add_argument("--catalog", default=None)
    ap.add_argument("--pick", choices=["small", "medium", "big", "mixed"], default="small")
    ap.add_argument("--count", type=int, default=3)
    ap.add_argument("--max-size", type=float, default=0, help="MB cap on preset size (0 = none)")
    ap.add_argument("--plugin-id", default=None, help="override Serum 2 HDAW pluginID")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    plugin_id = args.plugin_id or serum2_plugin_id()
    print("Serum 2 pluginID:", plugin_id)

    if args.presets:
        loaded = load_selected(args.presets)
    elif args.catalog:
        cat = json.load(open(args.catalog, encoding="utf-8"))
        picked = pick_from_catalog(args.catalog, args.pick, args.count, args.max_size)
        loaded = []
        for pr in picked:
            if pr["member"]:
                data = read_preset_bytes(pr["pack"], pr["member"])
            else:
                data = read_preset_bytes(pr["path"], None)
            loaded.append((pr["name"], data, {"hash": pr["hash"], "author": pr.get("author")}))
        print(f"picked {len(loaded)} presets [{args.pick}]")
        for l_ in loaded:
            print("  ", l_[0], len(l_[1]), "bytes")
    else:
        raise SystemExit("need --presets or --catalog")
    build(loaded, args.out, plugin_id)

if __name__ == "__main__":
    main()
