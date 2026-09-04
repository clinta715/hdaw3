#!/usr/bin/env python3
"""
generate_psytrance.py — Generative psytrance track builder for HDAW.

Calls HDAW's MCP tools over WebSocket to build a complete psytrance track.
Each run produces a different variation: structure, percussion patterns,
lead melodies, bass sequences, and FX are all randomized within
style-appropriate constraints.

Usage:
    python generate_psytrance.py [--bpm 138] [--bars 64] [--key F] [--mode minor]
                                 [--style progressive] [--seed 42] [--dry-run]

Styles:
    progressive  — gradual build, long breakdown, melodic (default)
    full-on      — fast, aggressive, minimal breakdown
    dark         — minor key, heavy growl bass, eerie leads
    acid         — acid leads dominant, squelchy FM textures
    ambient      — pads + atmosphere, minimal percussion

Requirements:
    - HDAW running with WebSocket on port 8766 (default)
    - `websockets` Python package: pip install websockets
    - Or use the HTTP MCP transport if available
"""

import asyncio
import json
import random
import argparse
import sys
from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional, Tuple

# ── Configuration ──────────────────────────────────────────────────────────

@dataclass
class TrackConfig:
    bpm: float = 138.0
    bars: int = 64
    key: str = "F"
    mode: str = "minor"
    style: str = "progressive"
    seed: int = -1  # -1 = random
    dry_run: bool = False

    # Derived
    beats_per_bar: int = 4
    total_beats: int = 0

    def __post_init__(self):
        if self.seed >= 0:
            random.seed(self.seed)
        self.total_beats = self.bars * self.beats_per_bar

# ── Scale/Music Theory ─────────────────────────────────────────────────────

SCALE_INTERVALS = {
    "minor":        [0, 2, 3, 5, 7, 8, 10],
    "major":        [0, 2, 4, 5, 7, 9, 11],
    "dorian":       [0, 2, 3, 5, 7, 9, 10],
    "phrygian":     [0, 1, 3, 5, 7, 8, 10],
    "lydian":       [0, 2, 4, 6, 7, 9, 11],
    "mixolydian":   [0, 2, 4, 5, 7, 9, 10],
    "minor_pentatonic": [0, 3, 5, 7, 10],
    "harmonic_minor":   [0, 2, 3, 5, 7, 8, 11],
}

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

def note_to_midi(name: str, octave: int = 4) -> int:
    idx = NOTE_NAMES.index(name.upper().replace("B#", "C").replace("EB", "D#").replace("AB", "G#"))
    return 12 * (octave + 1) + idx

def scale_notes(root: str, mode: str, octave_range: Tuple[int, int] = (2, 6)) -> List[int]:
    intervals = SCALE_INTERVALS.get(mode, SCALE_INTERVALS["minor"])
    root_midi = note_to_midi(root, octave_range[0])
    notes = []
    for oct in range(octave_range[0], octave_range[1] + 1):
        base = note_to_midi(root, oct)
        for interval in intervals:
            n = base + interval
            if 0 <= n <= 127:
                notes.append(n)
    return sorted(notes)

# ── Pattern Generators ─────────────────────────────────────────────────────

@dataclass
class Note:
    start: float  # beats
    duration: float
    pitch: int
    velocity: int = 100

def generate_kick_pattern(bars: int, style: str) -> List[Note]:
    """4-on-floor kick, with variations per style."""
    notes = []
    for bar in range(bars):
        for beat in range(4):
            vel = random.randint(115, 127) if beat == 0 else random.randint(105, 120)
            notes.append(Note(bar * 4 + beat, 0.5, 36, vel))
    return notes

def generate_bass_pattern(bars: int, scale: List[int], style: str) -> List[Note]:
    """Offbeat rolling bass with random root movement."""
    notes = []
    root_notes = [n for n in scale if 36 <= n <= 48]  # bass register
    if not root_notes:
        root_notes = [36, 38, 41, 43]

    for bar in range(bars):
        root = random.choice(root_notes)
        for beat in range(4):
            # Offbeat 8th
            notes.append(Note(bar * 4 + beat + 0.5, 0.4, root, random.randint(105, 115)))
            # Rolling 16th on beats 0, 2
            if beat % 2 == 0 and random.random() < 0.7:
                notes.append(Note(bar * 4 + beat + 0.75, 0.2, root + 12, random.randint(80, 95)))
    return notes

def generate_fm_bass_pattern(bars: int, start_bar: int, scale: List[int]) -> List[Note]:
    """FM growl bass — sparse, heavy, on offbeats."""
    notes = []
    root_notes = [n for n in scale if 36 <= n <= 44]
    if not root_notes:
        root_notes = [36, 38, 41]

    for bar in range(start_bar, bars):
        root = random.choice(root_notes)
        # Every other beat, offbeat
        for beat in range(0, 4, 2):
            notes.append(Note((bar - start_bar) * 4 + beat + 0.5, 0.3, root, random.randint(95, 110)))
    return notes

def generate_hat_pattern(bars: int, start_bar: int, style: str) -> List[Note]:
    """Offbeat hats with 16th rolls."""
    notes = []
    hat_pitches = [44, 46]  # closed, open

    for bar in range(start_bar, bars):
        rel_bar = bar - start_bar
        for beat in range(4):
            # Offbeat hat
            pitch = 44 if random.random() < 0.8 else 46
            notes.append(Note(rel_bar * 4 + beat + 0.5, 0.2, pitch, random.randint(85, 95)))

            # 16th rolls at phrase boundaries
            if bar % 8 == 7 and beat >= 2:
                for s in range(1, 4):
                    notes.append(Note(rel_bar * 4 + beat + 0.25 * s, 0.15, 46, random.randint(75, 90)))
    return notes

def generate_lead_pattern(bars: int, start_bar: int, scale: List[int], style: str) -> List[Note]:
    """Arpeggiated lead with random melodic fragments."""
    notes = []
    lead_notes = [n for n in scale if 60 <= n <= 84]
    if not lead_notes:
        lead_notes = [62, 65, 69, 74]

    # Generate a motif (4-8 notes) and repeat with variation
    motif_len = random.choice([4, 6, 8])
    motif = random.sample(lead_notes, min(motif_len, len(lead_notes)))

    for bar in range(start_bar, bars):
        rel_bar = bar - start_bar
        # Vary the motif slightly each bar
        current_motif = motif.copy()
        if random.random() < 0.3:
            # Swap one note
            idx = random.randint(0, len(current_motif) - 1)
            current_motif[idx] = random.choice(lead_notes)

        for beat in range(4):
            for s in range(4):
                note_idx = (beat * 4 + s) % len(current_motif)
                pitch = current_motif[note_idx]
                # Accent on beat, ghost on offbeat
                vel = random.randint(85, 100) if s == 0 else random.randint(70, 85)
                # Occasional octave jump
                if random.random() < 0.1:
                    pitch += 12
                notes.append(Note(rel_bar * 4 + beat + s * 0.25, 0.2, min(pitch, 127), vel))
    return notes

def generate_pad_pattern(bars: int, scale: List[int]) -> List[Note]:
    """Long sustained pad chords."""
    notes = []
    chord_tones = [n for n in scale if 48 <= n <= 72]
    if len(chord_tones) < 3:
        return notes

    for bar in range(0, bars, 2):  # Change chord every 2 bars
        # Pick a 3-note chord
        root_idx = random.randint(0, len(chord_tones) - 3)
        chord = [chord_tones[root_idx], chord_tones[root_idx + 2], chord_tones[root_idx + 4] if root_idx + 4 < len(chord_tones) else chord_tones[root_idx + 2] + 7]
        for pitch in chord:
            notes.append(Note(bar * 4, 7.5, pitch, random.randint(55, 70)))
    return notes

# ── Arrangement Structure ──────────────────────────────────────────────────

@dataclass
class ArrangementSection:
    name: str
    start_bar: int
    end_bar: int
    layers: List[str]  # which tracks are active

def generate_arrangement(bars: int, style: str) -> List[ArrangementSection]:
    """Generate a section-based arrangement."""
    sections = []

    if style == "progressive":
        sections = [
            ArrangementSection("Intro",    0,  bars // 8,    ["kick", "bass"]),
            ArrangementSection("Build",    bars // 8, bars // 4,  ["kick", "bass", "fm_bass"]),
            ArrangementSection("Build+",   bars // 4, bars // 2,  ["kick", "bass", "fm_bass", "hats"]),
            ArrangementSection("Main A",   bars // 2, bars * 3 // 4, ["kick", "bass", "fm_bass", "hats", "lead", "pad"]),
            ArrangementSection("Break",    bars * 3 // 4 - bars // 8, bars * 3 // 4, ["pad", "lead"]),
            ArrangementSection("Main B",   bars * 3 // 4, bars,  ["kick", "bass", "fm_bass", "hats", "lead", "pad"]),
        ]
    elif style == "full-on":
        sections = [
            ArrangementSection("Intro",    0,  bars // 8,    ["kick", "bass"]),
            ArrangementSection("Main",     bars // 8, bars,    ["kick", "bass", "fm_bass", "hats", "lead"]),
        ]
    elif style == "dark":
        sections = [
            ArrangementSection("Intro",    0,  bars // 4,    ["kick", "bass", "fm_bass"]),
            ArrangementSection("Main",     bars // 4, bars,    ["kick", "bass", "fm_bass", "hats", "lead"]),
        ]
    elif style == "acid":
        sections = [
            ArrangementSection("Intro",    0,  bars // 8,    ["kick", "bass"]),
            ArrangementSection("Build",    bars // 8, bars // 4,  ["kick", "bass", "lead"]),
            ArrangementSection("Main",     bars // 4, bars,    ["kick", "bass", "fm_bass", "hats", "lead"]),
        ]
    elif style == "ambient":
        sections = [
            ArrangementSection("Atmos",    0,  bars // 2,    ["pad", "lead"]),
            ArrangementSection("Full",     bars // 2, bars,    ["pad", "lead", "bass"]),
        ]
    else:
        # Default: progressive
        sections = generate_arrangement(bars, "progressive")

    return sections

# ── MCP Client ─────────────────────────────────────────────────────────────

class McpClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 8766, engine_bin: str = None):
        self.host = host
        self.port = port
        self.engine_bin = engine_bin
        self.ws = None
        self.proc = None
        self.request_id = 0

    async def connect(self):
        if self.engine_bin:
            # Stdio transport: spawn the headless engine and speak JSON-RPC
            # over its stdin/stdout (tools/call is not served over the WS 8766
            # endpoint; only --mcp-stdio exposes the MCP tool surface).
            import asyncio
            self.proc = await asyncio.create_subprocess_exec(
                self.engine_bin, "--mcp-stdio",
                stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT)
            await self.send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}})
            await self.recv()
            return
        import websockets
        self.ws = await websockets.connect(f"ws://{self.host}:{self.port}")
        # Initialize
        await self.send("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}})
        await self.recv()

    async def send(self, method: str, params: Dict[str, Any] = None):
        self.request_id += 1
        msg = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": method,
            "params": params or {}
        }
        line = (json.dumps(msg) + "\n").encode("utf-8")
        if self.proc:
            self.proc.stdin.write(line)
            await self.proc.stdin.drain()
        else:
            await self.ws.send(json.dumps(msg))

    async def recv(self) -> Any:
        # Skip interleaved notifications; return the response matching the
        # current request id.
        while True:
            if self.proc:
                raw = await self.proc.stdout.readline()
                if not raw:
                    raise RuntimeError("engine stdout closed")
            else:
                raw = (await self.ws.recv()).encode("utf-8")
            msg = json.loads(raw)
            if isinstance(msg, dict) and "id" in msg and msg["id"] == self.request_id:
                return msg

    async def call(self, tool: str, args: Dict[str, Any]) -> Any:
        await self.send("tools/call", {"name": tool, "arguments": args})
        resp = await self.recv()
        if "result" in resp:
            content = resp["result"].get("content", [])
            if content and content[0].get("type") == "text":
                return content[0]["text"]
        return resp.get("error", "unknown error")

    async def close(self):
        if self.proc:
            try:
                self.proc.kill()
            except Exception:
                pass
        elif self.ws:
            await self.ws.close()

# ── Track Builder ──────────────────────────────────────────────────────────

async def build_track(config: TrackConfig, client: McpClient):
    """Build a complete psytrance track using MCP tools."""
    scale = scale_notes(config.key, config.mode)
    sections = generate_arrangement(config.bars, config.style)

    print(f"\n{'='*60}")
    print(f"  Psytrance Generator — {config.style.title()}")
    print(f"  {config.bpm} BPM | {config.key} {config.mode} | {config.bars} bars")
    print(f"  Seed: {config.seed if config.seed >= 0 else 'random'}")
    print(f"{'='*60}\n")

    # 1. Create project
    print("[1/6] Creating project...")
    await client.call("new_project", {})

    # 2. Create tracks
    print("[2/6] Creating tracks...")
    track_names = ["Kick", "Bass", "FM Bass", "Hats", "Lead", "Pad"]
    track_ids = {}
    for name in track_names:
        r = await client.call("add_track", {"name": name})
        data = json.loads(r)
        track_ids[name] = data["trackId"]
        print(f"  → {name} (track {data['trackId']})")

    # 3. Configure instruments
    print("[3/6] Configuring instruments...")
    # FM Bass — growl preset
    await client.call("add_fx", {"trackId": track_ids["FM Bass"], "fxType": "psy_fm"})
    # Set growl parameters
    for i, ratio in enumerate([1.0, 2.0, 1.0, 1.0, 3.0, 1.0]):
        await client.call("set_internal_fx_param", {"trackId": track_ids["FM Bass"], "slotIndex": 0, "paramIndex": i, "value": ratio})
    await client.call("set_internal_fx_param", {"trackId": track_ids["FM Bass"], "slotIndex": 0, "paramIndex": 6, "value": 0.3})
    await client.call("set_internal_fx_param", {"trackId": track_ids["FM Bass"], "slotIndex": 0, "paramIndex": 31, "value": 0.4})
    await client.call("set_internal_fx_param", {"trackId": track_ids["FM Bass"], "slotIndex": 0, "paramIndex": 32, "value": 0})  # growlBass algo

    # Lead — acid preset
    await client.call("add_fx", {"trackId": track_ids["Lead"], "fxType": "psy_fm"})
    for i in range(6):
        await client.call("set_internal_fx_param", {"trackId": track_ids["Lead"], "slotIndex": 0, "paramIndex": i, "value": 1.0})
    await client.call("set_internal_fx_param", {"trackId": track_ids["Lead"], "slotIndex": 0, "paramIndex": 6, "value": 0.1})
    await client.call("set_internal_fx_param", {"trackId": track_ids["Lead"], "slotIndex": 0, "paramIndex": 31, "value": 0.35})
    await client.call("set_internal_fx_param", {"trackId": track_ids["Lead"], "slotIndex": 0, "paramIndex": 32, "value": 1})  # acidLead algo

    # Kick/Hats — sampler
    await client.call("add_fx", {"trackId": track_ids["Kick"], "fxType": "sampler"})
    await client.call("add_fx", {"trackId": track_ids["Hats"], "fxType": "sampler"})

    # 4. Set mix levels
    print("[4/6] Setting mix levels...")
    levels = {"Kick": 0.85, "Bass": 0.80, "FM Bass": 0.75, "Hats": 1.0, "Lead": 0.90, "Pad": 0.70}
    for name, vol in levels.items():
        await client.call("set_track", {"trackId": track_ids[name], "volume": vol})

    # 5. Generate patterns
    print("[5/6] Generating patterns...")
    all_notes = {
        "Kick": generate_kick_pattern(config.bars, config.style),
        "Bass": generate_bass_pattern(config.bars, scale, config.style),
        "FM Bass": [],
        "Hats": [],
        "Lead": [],
        "Pad": generate_pad_pattern(config.bars, scale),
    }

    # Find section boundaries for layered tracks
    for section in sections:
        if "fm_bass" in section.layers:
            all_notes["FM Bass"].extend(
                generate_fm_bass_pattern(section.end_bar, section.start_bar, scale))
        if "hats" in section.layers:
            all_notes["Hats"].extend(
                generate_hat_pattern(section.end_bar, section.start_bar, config.style))
        if "lead" in section.layers:
            all_notes["Lead"].extend(
                generate_lead_pattern(section.end_bar, section.start_bar, scale, config.style))

    # 6. Create clips and add notes
    print("[6/6] Creating clips...")
    for name, notes in all_notes.items():
        if not notes:
            continue
        # Find the time range for this track
        min_start = min(n.start for n in notes) if notes else 0
        max_end = max(n.start + n.duration for n in notes) if notes else 0
        # Snap to bar boundaries
        start_bar = int(min_start // 4) * 4
        length = (int(max_end // 4) + 1) * 4 - start_bar

        r = await client.call("add_midi_clip", {
            "trackId": track_ids[name],
            "start": start_bar,
            "length": length,
            "name": name
        })
        clip_data = json.loads(r)
        clip_id = clip_data["clipId"]

        # Add notes in batches (MCP tool accepts arrays)
        note_dicts = [{"start": n.start - start_bar, "duration": n.duration, "pitch": n.pitch, "velocity": n.velocity} for n in notes]
        await client.call("add_notes", {"clipId": clip_id, "notes": note_dicts})
        print(f"  → {name}: {len(notes)} notes")

    # Set loop
    await client.call("set_transport", {"loopEnabled": True, "loopStart": 0, "loopEnd": config.total_beats})

    # Summary
    total_notes = sum(len(n) for n in all_notes.values())
    print(f"\n{'='*60}")
    print(f"  Track complete!")
    print(f"  {total_notes} notes across {len([n for n in all_notes.values() if n])} tracks")
    print(f"  Sections: {', '.join(s.name for s in sections)}")
    print(f"{'='*60}\n")

    # Print arrangement map
    print("Arrangement:")
    for section in sections:
        bars_str = f"bars {section.start_bar}-{section.end_bar}"
        layers_str = ", ".join(section.layers)
        print(f"  {section.name:12s} {bars_str:16s} [{layers_str}]")

# ── Main ───────────────────────────────────────────────────────────────────

async def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Generate a psytrance track in HDAW")
    parser.add_argument("--bpm", type=float, default=138.0, help="Tempo in BPM")
    parser.add_argument("--bars", type=int, default=64, help="Length in bars")
    parser.add_argument("--key", default="F", help="Root note (C, D#, F, etc.)")
    parser.add_argument("--mode", default="minor", choices=SCALE_INTERVALS.keys(), help="Scale mode")
    parser.add_argument("--style", default="progressive", choices=["progressive", "full-on", "dark", "acid", "ambient"])
    parser.add_argument("--seed", type=int, default=-1, help="Random seed (-1 = random)")
    parser.add_argument("--host", default="127.0.0.1", help="HDAW WebSocket host")
    parser.add_argument("--port", type=int, default=8766, help="HDAW WebSocket port")
    parser.add_argument("--engine-bin", default=None,
                        help="spawn this headless engine over stdio (preferred; "
                             "tools/call is not served over WS 8766)")
    parser.add_argument("--dry-run", action="store_true", help="Print patterns without sending to HDAW")
    args = parser.parse_args()

    config = TrackConfig(
        bpm=args.bpm, bars=args.bars, key=args.key, mode=args.mode,
        style=args.style, seed=args.seed, dry_run=args.dry_run,
    )

    if config.seed < 0:
        config.seed = random.randint(0, 999999)
        random.seed(config.seed)

    if args.dry_run:
        # Just print the arrangement
        sections = generate_arrangement(config.bars, config.style)
        scale = scale_notes(config.key, config.mode)
        print(f"\nDry run: {config.style} | {config.bpm} BPM | {config.key} {config.mode} | {config.bars} bars | seed={config.seed}")
        print("\nArrangement:")
        for s in sections:
            print(f"  {s.name:12s} bars {s.start_bar}-{s.end_bar:3d}  [{', '.join(s.layers)}]")
        print(f"\nScale notes: {scale[:12]}...")
        return

    client = McpClient(args.host, args.port, args.engine_bin)
    try:
        await client.connect()
        await build_track(config, client)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        await client.close()

if __name__ == "__main__":
    asyncio.run(main())
