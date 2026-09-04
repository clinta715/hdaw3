#!/usr/bin/env python3
"""sweep_dx7_patches.py — batch DX7 / Virus patch sweep + ranked report.

Loads patches from a folder into an internal synth FX slot on a transient
probe track (DX7 .syx into ``fm_synth`` by default, or Access Virus patches
into ``sub_synth`` with ``--engine sub_synth``), renders a role-appropriate
probe phrase to WAV via export_audio, runs the probe analyzer
(timbre-lib/analyze_probe.py) per patch, and writes an aggregated JSON report
ranking patches for a production role.

Usage:
    py -3.14 timbre-lib/sweep_dx7_patches.py [mode] [options]
    py -3.14 timbre-lib/sweep_dx7_patches.py --engine sub_synth --role lead
        --dir "D:\\pdf\\Virus Presets" --out virus_out --report virus.json
    py -3.14 timbre-lib/sweep_dx7_patches.py analyze-only --out <wavdir> --role bass --report out.json

With --sidecars, each successfully analyzed patch also gets a
``<patch>.dx7.json`` (fm_synth) or ``<patch>.virus.json`` (sub_synth) sidecar
written next to it (Qwen prose when --gguf, else the DSP summary) so
FileLibraryManager / search_library can index it.

Modes:
    sweep         (default) drive the engine end-to-end
    analyze-only  skip the engine; analyze existing WAVs in --out against --role

Engine transport: MCP JSON-RPC over the spawned engine's stdio
(HDAW_headless.exe --mcp-stdio). The MCP server in this repo ships over stdio
only (the 8766 WebSocket speaks the frontend RPC namespace, not tools/call),
and the MCP fm_synth_import_sysex / fm_synth_load_preset tools route through
ProjectCommands::setFmPatch, which writes fmPatchData into the slot ValueTree
(restored on the LIVE processor after a rebuild — proven by FmPatchPersistence
gtests). For sub_synth, sub_synth_import_sysex routes through
AudioEngineCommands::loadVirusPatch, which persists the mapped sub_synth
params (param_N 0..22) to the slot ValueTree so offline exports hear them.
RESOLVED (verified 2026-09-04): the offline tree-copy render path
(export_audio / audition) previously rendered a fixed FM tone that was
invariant to the imported patch bytes, so per-patch WAVs came out
byte-identical regardless of fmPatchData. Root cause: FmSynthEngine::prepare()
re-seeded patchData_ to the DX7 init patch on the reset issued before an
offline export. Fixed by seeding init exactly once (initPatchSeeded_) and
restoring fmPatchData via TrackFXSlot::loadFmPatchFromTree in every
rebuildFXChain (RoutingManager::buildTrackProcessor prepares the track BEFORE
rebuildFXChain, so fxSpec.sampleRate > 0 on the offline path). Verified by
gtest FmPatchOfflineExport.*: two real cartridge voices export byte-different
WAVs, and an imported patch differs from the default init tone. --host/--port
are accepted for CLI compatibility and reported in meta.engine_ws.

Exit codes: 0 = completed (per-patch failures are recorded in the report),
1 = fatal (bad args / engine unreachable / no patches found / probe setup
failed).
"""
import argparse
import asyncio
import datetime
import json
import os
import random
import re
import subprocess
import sys
import time
import fnmatch

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import analyze_probe as AP
import role_targets as RT

try:
    import dx7_patch as DX7P
    SIDECAR_SCHEMA = DX7P.SCHEMA
except Exception:  # noqa: BLE001 - a missing dx7_patch must not break the sweep
    SIDECAR_SCHEMA = "hda.dx7.sidecar.v1"

try:
    import virus_patch as VP
    VIRUS_SIDECAR_SCHEMA = VP.VIRUS_SCHEMA
except Exception:  # noqa: BLE001 - a missing virus_patch must not break the sweep
    VIRUS_SIDECAR_SCHEMA = "hdaw.virus.patch.v1"

# Per-engine profile. ``fm_synth`` keeps the historical DX7 behavior; the two
# sidecar schemas above give the sweep its schema identifiers (a missing
# parser module must not break the sweep, hence the fallbacks).
ENGINES = {
    "fm_synth": {
        "fx_type": "fm_synth",
        "import_tool": "fm_synth_import_sysex",
        "voice_key": "voiceIndex",
        "voice_name_field": "voiceName",
        "cart_voices_desc": "cartridge .syx (32 voices, voiceIndex 0..31)",
        "default_dir": r"d:\pdf\dexed presets",
        "default_pattern": "*.syx",
        "extensions": (".syx",),
        "probe_extensionless": False,
        "probe_track_name": "FM Probe",
        "probe_setup": "add_track_with_fx",
        "sidecar_suffix": ".dx7.json",
        "sidecar_engine": "fm_synth",
        "sidecar_format": "dx7",
        "sidecar_schema": SIDECAR_SCHEMA,
        "import_meta_fields": ("algorithm", "feedback"),
    },
    "sub_synth": {
        "fx_type": "sub_synth",
        "import_tool": "sub_synth_import_sysex",
        "voice_key": "voiceIndex",
        "voice_name_field": "name",
        "cart_voices_desc": "TI bank (128 voices, voiceIndex 0..N-1)",
        "default_dir": r"D:\pdf\Virus Presets",
        "default_pattern": "*",
        "extensions": (".syx", ".mid", ".vhc"),
        "probe_extensionless": True,
        "probe_track_name": "Virus Probe",
        "probe_setup": "add_track_add_fx",
        "sidecar_suffix": ".virus.json",
        "sidecar_engine": "sub_synth",
        "sidecar_format": "virus",
        "sidecar_schema": VIRUS_SIDECAR_SCHEMA,
        "import_meta_fields": ("name", "bank", "program", "mappedCount",
                               "unmapped"),
    },
}

ACCESS_VIRUS_HEADER = b"\xf0\x00\x20\x33\x01"  # first 5 bytes of a Virus SysEx

# The 20-key DSP feature vector returned by timbre.extract() (what the probe
# analyzer's `measurements` carry). MUST match kDspFeatureKeys in
# src/engine/LibraryClusterer.h exactly — the C++ ingest (applyPatchSidecar /
# applyTimbreSidecar) requires ALL 20 keys present and finite, else the entry
# has no dsp signal and clusters by text only.
DSP_KEYS = (
    "duration", "rms", "peak", "crest_dB", "zcr", "centroid", "bandwidth",
    "rolloff85", "rolloff95", "flatness", "spectral_crest", "spec_irregularity",
    "mel_low", "mel_mid", "mel_high", "attack_s", "decay_s", "f0_hz",
    "tonal_fraction", "f0_sweep",
)


def sanitize(s):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", s)


ROLE_DEFAULT_ROOT = {
    "bass": 36,
    "lead": 72,
    "pad": 48,
    "stab": 60,
    "arp": 60,
    "fx": 36,
    "riser": 36,
}

PERCUSSIVE_ROLES = ("kick", "snare", "hat", "rim", "clap")


def build_probe_notes(role, root, window_beats, seed):
    """Deterministic clip-local-beat probe notes for a role.

    Returns (notes, warning). Notes are dicts {pitch, start, duration,
    velocity} in clip-local beats. Pitches are clamped to 0..127 by skipping
    out-of-range notes."""
    rng = random.Random(seed)
    notes = []
    warning = None

    def add(pitch, start, dur, vel=100):
        if 0 <= pitch <= 127:
            notes.append({"pitch": pitch, "start": float(start),
                          "duration": float(dur), "velocity": vel})

    w = window_beats
    if role == "bass":
        add(root, 0.0, w, 100)
    elif role == "lead":
        add(root, 0.0, w, 100)
        add(root + 4, 0.0, w, 90)
    elif role == "pad":
        add(root, 0.0, w, 100)
        add(root + 7, 0.0, w, 90)
        add(root + 12, 0.0, w, 80)
    elif role == "stab":
        beat = 0.0
        while beat < w:
            add(root, beat, 0.5)
            add(root + 7, beat, 0.5)
            add(root + 12, beat, 0.5)
            beat += 2.0
    elif role == "arp":
        seq = [0, 3, 7, 12, 7, 3]
        t = 0.0
        i = 0
        while t < w:
            add(root + seq[i % len(seq)], t, 0.4)
            t += 0.5
            i += 1
    else:
        if role in PERCUSSIVE_ROLES:
            warning = (f"single-note probe may not be representative of "
                       f"percussive role '{role}'; using gliss/rise builder")
            print(f"warning: {warning}")
        for k in range(16):
            t = float(k)
            if t >= w:
                break
            add(root + k, t, 0.8)
    return notes, warning


class McpToolError(Exception):
    def __init__(self, name, message):
        super().__init__(f"{name}: {message}")
        self.name = name
        self.message = message


class EngineUnreachable(Exception):
    pass


class McpClient:
    """MCP JSON-RPC client over a spawned engine's stdin/stdout.

    Responses are correlated by id (the engine interleaves
    notifications/progress and notifications/exportComplete between
    responses), so a reader task routes them to pending futures by id and
    buffers notifications in a queue. stdout is newline-delimited JSON."""

    def __init__(self, proc, verbose=False):
        self.proc = proc
        self.verbose = verbose
        self._n = 0
        self._pending = {}
        self._notifications = asyncio.Queue()
        self._reader_task = None

    async def start(self):
        self._reader_task = asyncio.create_task(self._reader())
        await self.call("initialize",
                        {"protocolVersion": "2024-11-05", "capabilities": {}},
                        timeout=20)

    async def _reader(self):
        try:
            while True:
                line = await self.proc.stdout.readline()
                if not line:
                    break
                text = line.decode("utf-8", "replace").strip()
                if not text:
                    continue
                try:
                    msg = json.loads(text)
                except json.JSONDecodeError:
                    continue
                if "id" in msg:
                    fut = self._pending.pop(msg.get("id"), None)
                    if fut is not None and not fut.done():
                        fut.set_result(msg)
                else:
                    await self._notifications.put(msg)
        except Exception:
            pass
        finally:
            for fut in list(self._pending.values()):
                if not fut.done():
                    fut.set_exception(
                        RuntimeError("engine process closed"))

    async def call(self, method, params=None, timeout=120):
        self._n += 1
        rid = self._n
        loop = asyncio.get_running_loop()
        fut = loop.create_future()
        self._pending[rid] = fut
        msg = {"jsonrpc": "2.0", "id": rid, "method": method,
               "params": params or {}}
        if self.verbose:
            print(f"[rpc] -> {method} {json.dumps(params or {})[:200]}")
        self.proc.stdin.write((json.dumps(msg) + "\n").encode("utf-8"))
        await self.proc.stdin.drain()
        resp = await asyncio.wait_for(fut, timeout)
        if self.verbose:
            print(f"[rpc] <- {method} (id {rid})")
        return resp

    async def tool(self, name, args=None, timeout=120):
        resp = await self.call("tools/call",
                               {"name": name, "arguments": args or {}},
                               timeout=timeout)
        if "error" in resp:
            raise McpToolError(name, json.dumps(resp["error"]))
        result = resp.get("result") or {}
        content = result.get("content") or []
        text = ""
        is_error = bool(result.get("isError"))
        if content and content[0].get("type") == "text":
            text = content[0].get("text", "")
        if is_error:
            raise McpToolError(name, text or "tool error")
        sc = result.get("structuredContent")
        if sc is not None:
            return sc
        return text

    async def wait_notification(self, method, timeout=120):
        end = time.monotonic() + timeout
        while True:
            remaining = end - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(
                    f"timed out waiting for notification {method}")
            try:
                msg = await asyncio.wait_for(self._notifications.get(),
                                             remaining)
            except asyncio.TimeoutError:
                raise TimeoutError(
                    f"timed out waiting for notification {method}")
            if msg.get("method") == method:
                return msg

    async def close(self):
        if self._reader_task is not None:
            self._reader_task.cancel()
            try:
                await self._reader_task
            except (asyncio.CancelledError, Exception):
                pass
        try:
            if self.proc.stdin is not None:
                self.proc.stdin.close()
        except Exception:
            pass
        try:
            await asyncio.wait_for(self.proc.wait(), 6)
        except asyncio.TimeoutError:
            self.proc.kill()
            try:
                await asyncio.wait_for(self.proc.wait(), 5)
            except Exception:
                pass


def resolve_engine_bin(engine_arg):
    if engine_arg:
        if os.path.isfile(engine_arg):
            return engine_arg
        return None
    env = os.environ.get("HDAW_ENGINE_BIN")
    if env and os.path.isfile(env):
        return env
    repo = os.path.abspath(os.path.join(HERE, ".."))
    for cand in (os.path.join(repo, "build", "HDAW_headless.exe"),
                 os.path.join(repo, "build", "Release",
                              "HDAW_headless.exe")):
        if os.path.isfile(cand):
            return cand
    return None


class EngineSession:
    """Spawns HDAW_headless.exe --mcp-stdio and waits until it responds."""

    def __init__(self, args):
        self.args = args
        self.proc = None
        self.client = None

    async def start(self):
        exe = resolve_engine_bin(self.args.engine_bin)
        if not exe:
            raise EngineUnreachable(
                "engine binary not found (pass --engine-bin or set "
                "HDAW_ENGINE_BIN)")
        if self.args.verbose:
            stderr = open(os.path.join(self.args.out, "_engine_stderr.log"),
                          "wb")
        else:
            stderr = subprocess.DEVNULL
        self.proc = await asyncio.create_subprocess_exec(
            exe, "--mcp-stdio",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=stderr)
        self.client = McpClient(self.proc, verbose=self.args.verbose)
        try:
            await self.client.start()
            tracks = await self._wait_ready()
        except EngineUnreachable:
            raise
        except Exception as e:
            raise EngineUnreachable(f"engine not reachable: {e}")
        return tracks

    async def _wait_ready(self):
        deadline = time.monotonic() + 120
        last = None
        prev_ids = None
        while time.monotonic() < deadline:
            try:
                r = await self.client.tool("list_tracks", {}, timeout=20)
                data = json.loads(r)
                if isinstance(data, list) and data:
                    ids = tuple(t["id"] for t in data)
                    # Require the track list to be stable across two polls so
                    # the engine has finished creating its default project
                    # (a list that is still growing would snapshot a partial
                    # state, breaking the teardown equality check).
                    if ids == prev_ids:
                        return data
                    prev_ids = ids
            except Exception as e:
                last = e
            await asyncio.sleep(1.0)
        raise EngineUnreachable(
            f"engine did not become ready: {last}")

    async def close(self):
        if self.client is not None:
            await self.client.close()
        if self.proc is not None and self.proc.returncode is None:
            try:
                self.proc.kill()
            except Exception:
                pass


def detect_voice_count(path, cfg):
    """Engine-aware voice count for a patch container.

    fm_synth: a 4104-byte DX7 cartridge (F0 43 ... 09) holds 32 voices.
    sub_synth: a 267-byte B/C single holds 1 voice; a TI bank
    (size % 524 == 0, Access header) holds size // 524 voices.  Anything else
    is treated as a single voice."""
    try:
        with open(path, "rb") as f:
            head = f.read(5)
        size = os.path.getsize(path)
    except OSError:
        return 1
    if cfg["fx_type"] == "sub_synth":
        if size == 267 and head[0:5] == ACCESS_VIRUS_HEADER:
            return 1
        if size >= 524 and size % 524 == 0 and head[0:5] == ACCESS_VIRUS_HEADER:
            return size // 524
        return 1
    if size >= 4104 and head[0:2] == b"\xf0\x43" and len(head) > 3 and \
            head[3] == 0x09:
        return 32
    return 1


def _engine_file_ok(path, cfg):
    """Per-engine file acceptance for enumeration.

    fm_synth: only ``.syx`` (the default ``*.syx`` pattern already filters).
    sub_synth: ``.syx`` / ``.mid`` / ``.vhc``, OR an extensionless file whose
    first 5 bytes are the Access Virus SysEx header (F0 00 20 33 01)."""
    ext = os.path.splitext(path)[1].lower()
    if ext in cfg["extensions"]:
        return True
    if ext == "" and cfg["probe_extensionless"]:
        try:
            with open(path, "rb") as fh:
                return fh.read(5) == ACCESS_VIRUS_HEADER
        except OSError:
            return False
    return False


def enumerate_patches(args, cfg):
    root = args.dir
    if not os.path.isdir(root):
        return None, f"patch root not found: {root}"
    pattern = args.pattern.lower()
    found = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            if not fnmatch.fnmatchcase(fn.lower(), pattern):
                continue
            if not _engine_file_ok(full, cfg):
                continue
            found.append(full)
    banks = args.bank or []
    names = args.names or []

    def keep(path):
        lowp = path.lower()
        if banks and not any(b.lower() in lowp for b in banks):
            return False
        if names and not any(n.lower() in os.path.basename(path).lower()
                             for n in names):
            return False
        return True

    found = [p for p in found if keep(p)]
    found.sort()
    if not found:
        return None, "no patches found"

    entries = []
    for p in found:
        nvoices = detect_voice_count(p, cfg)
        if args.cart_voices and nvoices > 1:
            for vi in range(nvoices):
                entries.append((p, vi))
        else:
            entries.append((p, 0))

    entries = entries[args.offset:args.offset + args.limit]
    if args.shuffle:
        rng = random.Random(args.shuffle)
        rng.shuffle(entries)

    out = []
    used = set()
    for p, vi in entries:
        rel = os.path.relpath(p, root)
        bank = os.path.dirname(rel).replace(os.sep, "_")
        base = os.path.splitext(os.path.basename(p))[0]
        raw = f"{sanitize(bank)}__{sanitize(base)}__v{vi}"
        name = raw
        k = 2
        while name in used:
            name = f"{raw}_{k}"
            k += 1
        used.add(name)
        out.append({"file": p, "name": name, "voice_index": vi,
                    "base": base,
                    "wav": os.path.join(args.out, name + ".wav")})
    return out, None


def parse_int_fields(text):
    """Extract `name=value` int fields from a tool result that is plain text
    (add_track_with_fx returns 'trackId=N routed=1 fxType=...') or JSON."""
    if text is None:
        return {}
    text = str(text)
    try:
        data = json.loads(text)
        if isinstance(data, dict):
            return {k: v for k, v in data.items() if isinstance(v, (int, float))}
    except (json.JSONDecodeError, TypeError):
        pass
    out = {}
    for k, v in re.findall(r"(\w+)=([^\s]+)", text):
        try:
            out[k] = int(float(v))
        except ValueError:
            out[k] = v
    return out


def entry_rank(entry):
    if entry.get("error"):
        return (2, 0.0)
    rc = (entry.get("analysis") or {}).get("roleCheck") or {}
    total = rc.get("total_count") or 0
    passed = rc.get("passed_count") or 0
    ratio = (passed / total) if total else 0.0
    if rc.get("verdict") == "pass":
        return (0, ratio)
    return (1, ratio)


def verdict_label(entry):
    if entry.get("error"):
        return "error"
    rc = (entry.get("analysis") or {}).get("roleCheck") or {}
    return rc.get("verdict", "error")


def _build_sidecar(entry, args, cfg):
    """Build the per-engine sidecar document for an analyzed entry."""
    analysis = entry.get("analysis") or {}
    rc = analysis.get("roleCheck") or {}
    verdict = rc.get("verdict")
    meta = entry.get("_patch_meta") or {}
    src = entry.get("file") or ""
    name = (entry.get("voice_name")
            or os.path.splitext(os.path.basename(src))[0])
    side = {
        "schema": cfg["sidecar_schema"],
        "name": name,
        "engine": cfg["sidecar_engine"],
        "format": cfg["sidecar_format"],
    }
    if cfg["sidecar_engine"] == "fm_synth":
        side["algorithm"] = meta.get("algorithm", 0)
        side["feedback"] = meta.get("feedback", 0)
    side["role"] = args.role
    side["roleCheck"] = {"verdict": verdict}
    side["description"] = analysis.get("description")
    if cfg["sidecar_engine"] != "fm_synth":
        side["unmapped"] = meta.get("unmapped") or []
        side["mappedCount"] = meta.get("mappedCount", 0)
    return side


def write_patch_sidecar(entry, args, cfg):
    """Write a `<source><sidecar_suffix>` sidecar next to the source patch
    file after a successful analysis, populated for FileLibraryManager /
    search_library.

    Returns a list of non-fatal warning strings (guard skips, write failures);
    never raises."""
    warnings = []
    analysis = entry.get("analysis") or {}
    desc = analysis.get("description")
    rc = analysis.get("roleCheck") or {}
    verdict = rc.get("verdict")
    if not desc or not verdict:
        return warnings
    src = entry.get("file")
    if not src or not os.path.isfile(src):
        return warnings
    sidecar_path = src + cfg["sidecar_suffix"]
    sidecar = _build_sidecar(entry, args, cfg)
    # dsp: the rendered-probe timbre vector (timbre.extract's 20 keys, already
    # in analysis["measurements"]) so cluster_library / related_samples can
    # cluster patches by timbre. Probe-context-specific — cluster only patches
    # swept with the same role/seed/window/bpm. Missing/partial keys simply
    # yield a smaller dict; the C++ ingest then leaves dspFeatures empty.
    measurements = analysis.get("measurements") or {}
    dsp = {k: measurements[k] for k in DSP_KEYS if k in measurements}
    if dsp:
        sidecar["dsp"] = dsp
    # Do-not-downgrade guard: a DSP-only re-run must never clobber an existing
    # curated (Qwen) description; --gguf overwrites. When an existing sidecar
    # carries a description (and this run is NOT --gguf), MERGE instead of
    # skipping: preserve the curated description while refreshing `dsp` and
    # roleCheck.verdict from the fresh analysis — so a DSP-only re-sweep adds
    # `dsp` to sidecars originally written by dx7_patch.py / virus_patch.py.
    if os.path.exists(sidecar_path):
        existing_sidecar = None
        existing_desc = ""
        try:
            with open(sidecar_path, "r", encoding="utf-8") as fh:
                existing_sidecar = json.load(fh)
            existing_desc = (existing_sidecar or {}).get("description") or ""
        except Exception:  # noqa: BLE001 - malformed sidecar is overwritten
            existing_sidecar = None
            existing_desc = ""
        if existing_desc and args.gguf is None:
            merged = dict(existing_sidecar or {})
            merged["description"] = existing_desc
            if dsp:
                merged["dsp"] = dsp
            if isinstance(merged.get("roleCheck"), dict):
                merged["roleCheck"]["verdict"] = verdict
            else:
                merged["roleCheck"] = {"verdict": verdict}
            try:
                with open(sidecar_path, "w", encoding="utf-8") as fh:
                    fh.write(json.dumps(merged, indent=2))
                entry["sidecar_written"] = True
            except OSError as e:
                warnings.append(f"sidecar write failed for {sidecar_path}: {e}")
            return warnings
    try:
        with open(sidecar_path, "w", encoding="utf-8") as fh:
            fh.write(json.dumps(sidecar, indent=2))
        entry["sidecar_written"] = True
    except OSError as e:
        warnings.append(f"sidecar write failed for {sidecar_path}: {e}")
    return warnings


async def process_patch(client, args, patch, i, n, cfg):
    entry = {"index": patch["index"], "file": patch["file"],
             "name": patch["name"], "voice_index": patch["voice_index"],
             "wav": patch["wav"], "engine": args.engine, "error": None,
             "analysis": None}
    voice_name = None
    need_export = not (args.reuse_wavs and os.path.isfile(patch["wav"]))
    if need_export:
        try:
            imp_args = {"trackId": args.probe_track_id, "slotIndex": 0,
                        "filePath": patch["file"]}
            if patch["voice_index"]:
                imp_args[cfg["voice_key"]] = patch["voice_index"]
            r = await client.tool(cfg["import_tool"], imp_args)
            imp = json.loads(r)
            voice_name = imp.get(cfg["voice_name_field"]) or None
            entry["voice_name"] = voice_name
            entry["_patch_meta"] = {
                f: imp.get(f) for f in cfg["import_meta_fields"]
            }
        except Exception as e:
            entry["error"] = f"import failed: {e}"
            print(f"[{i}/{n}] {patch['name']} -> error ({entry['error']})")
            return entry
        try:
            await client.tool("export_audio", {
                "outputPath": patch["wav"], "format": "wav",
                "sampleRate": 48000, "bitDepth": 24, "start": 0.0,
                "end": args.window, "queue": True})
            notif = await client.wait_notification(
                "notifications/exportComplete", timeout=120)
            params = notif.get("params", notif)
            if not params.get("success"):
                entry["error"] = ("export failed: " +
                                  str(params.get("message", "unknown")))
                print(f"[{i}/{n}] {patch['name']} -> error ({entry['error']})")
                return entry
            if not os.path.isfile(patch["wav"]):
                entry["error"] = "export reported success but file missing"
                print(f"[{i}/{n}] {patch['name']} -> error ({entry['error']})")
                return entry
        except Exception as e:
            entry["error"] = f"export failed: {e}"
            print(f"[{i}/{n}] {patch['name']} -> error ({entry['error']})")
            return entry
    try:
        report = AP.analyze_probe(
            patch["wav"], args.role,
            name=voice_name or patch["base"], plugin=cfg["sidecar_engine"],
            gguf=args.gguf, use_clap=False,
            use_llm=(args.gguf is not None))
        if report.get("error"):
            entry["error"] = report["error"]
        entry["analysis"] = report
    except Exception as e:
        entry["error"] = f"analyze failed: {e}"
    if args.sidecars and entry.get("analysis") and not entry.get("error"):
        sw = write_patch_sidecar(entry, args, cfg)
        if sw:
            entry["sidecar_warnings"] = sw
    label = verdict_label(entry)
    rc = (entry.get("analysis") or {}).get("roleCheck") or {}
    if entry.get("error"):
        tail = entry["error"]
    else:
        pt = (f"{rc.get('passed_count', 0)}/{rc.get('total_count', 0)}"
              if rc else "-")
        recs = (entry.get("analysis") or {}).get("recommendations") or []
        tail = f"{pt}"
        if recs:
            tail += f" [{recs[0].get('priority', '')}] {recs[0].get('message', '')}"
    print(f"[{i}/{n}] {patch['name']} -> {label} ({tail})")
    return entry


async def sweep(args, patches, notes, cfg):
    session = EngineSession(args)
    try:
        before = await session.start()
    except EngineUnreachable as e:
        print(f"engine not reachable: {e}", file=sys.stderr)
        return None
    client = session.client
    snapshot = {t["id"]: (bool(t.get("mute")), bool(t.get("solo")))
                for t in before}
    args.probe_track_id = None
    muted = False
    probe_created = False
    entries = []
    try:
        if cfg["probe_setup"] == "add_track_with_fx":
            r = await client.tool("add_track_with_fx",
                                  {"name": cfg["probe_track_name"],
                                   "fxType": cfg["fx_type"]})
            data = parse_int_fields(r)
            args.probe_track_id = data.get("trackId")
            if args.probe_track_id is None:
                raise McpToolError("add_track_with_fx",
                                   f"unexpected result: {r}")
        else:
            r = await client.tool("add_track",
                                  {"name": cfg["probe_track_name"]})
            data = parse_int_fields(r)
            args.probe_track_id = data.get("trackId")
            if args.probe_track_id is None:
                raise McpToolError("add_track",
                                   f"unexpected result: {r}")
            await client.tool("add_fx", {"trackId": args.probe_track_id,
                                         "fxType": cfg["fx_type"],
                                         "position": 0})
        probe_created = True
        beats = max(1.0, round(args.window * args.bpm / 60.0))
        r = await client.tool("add_midi_clip",
                              {"trackId": args.probe_track_id, "start": 0,
                               "length": beats, "name": "Probe"})
        clip_id = json.loads(r).get("clipId")
        r = await client.tool("add_notes",
                              {"clipId": clip_id, "notes": notes,
                               "relative": True})
        for tid in snapshot:
            await client.tool("set_track",
                              {"trackId": tid, "mute": True, "solo": False})
        muted = True
        for i, patch in enumerate(patches, 1):
            patch["index"] = i - 1
            entries.append(await process_patch(client, args, patch, i,
                                               len(patches), cfg))
    except Exception as e:
        print(f"fatal: probe setup failed: {e}", file=sys.stderr)
        return None
    finally:
        restore_ok = True
        if not args.keep_track:
            try:
                if muted:
                    for tid, (m, s) in snapshot.items():
                        try:
                            await client.tool("set_track",
                                              {"trackId": tid, "mute": m,
                                               "solo": s})
                        except BaseException:
                            restore_ok = False
                if probe_created:
                    try:
                        # The probe track carries a clip, so the engine
                        # requires force:true to confirm deletion.
                        await client.tool("remove_track",
                                          {"trackId": args.probe_track_id,
                                           "force": True})
                    except BaseException:
                        restore_ok = False
            except BaseException:
                restore_ok = False
        else:
            restore_ok = True
        if args.probe_track_id is not None and not args.keep_track:
            try:
                after = json.loads(await client.tool("list_tracks", {},
                                                     timeout=20))
                after_snapshot = {t["id"]: (bool(t.get("mute")),
                                            bool(t.get("solo")))
                                  for t in after}
                if after_snapshot != snapshot:
                    restore_ok = False
                    print("warning: track state after teardown differs from "
                          "pre-run snapshot", file=sys.stderr)
            except BaseException:
                pass
        if not restore_ok:
            print("warning: teardown did not fully restore track state",
                  file=sys.stderr)
        print(f"state restored: {restore_ok}")
        await session.close()
    return entries


def write_report(args, entries, patches, started, warnings, cfg):
    for e in entries:
        for w in e.get("sidecar_warnings") or []:
            warnings.append(w)
    if args.sidecars:
        warnings.append("--sidecars: wrote <patch>%s sidecars next to "
                        "analyzed patch files for FileLibraryManager indexing"
                        % cfg["sidecar_suffix"])
    meta = {
        "mode": args.mode,
        "role": args.role,
        "engine": args.engine,
        "bpm": args.bpm,
        "window_seconds": args.window,
        "seed": args.seed,
        "root_pitch": args.root_pitch,
        "out_dir": args.out,
        "plugin": cfg["sidecar_engine"],
        "engine_ws": f"ws://{args.host}:{args.port}",
        "juce": "8.0.0 (build/_deps/juce-src)",
        "started_iso": started.isoformat(timespec="seconds"),
        "finished_iso": datetime.datetime.now().isoformat(timespec="seconds"),
        "patches_requested": len(patches),
        "patches_done": sum(1 for e in entries if e.get("analysis")),
        "warnings": warnings,
    }
    if args.sidecars:
        meta["sidecars_written"] = sum(
            1 for e in entries if e.get("sidecar_written"))
    sorted_entries = sorted(entries, key=entry_rank)
    total = len(entries)
    pass_n = sum(1 for e in entries if verdict_label(e) == "pass")
    fail_n = sum(1 for e in entries if verdict_label(e) == "fail")
    err_n = sum(1 for e in entries if verdict_label(e) == "error")
    report = {
        "meta": meta,
        "patches": sorted_entries,
        "summary": {
            "total": total,
            "pass": pass_n,
            "fail": fail_n,
            "error": err_n,
            "pass_ratio": (pass_n / total) if total else 0.0,
            "reuse_wavs": args.reuse_wavs,
        },
    }
    with open(args.report, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    print()
    print(f"report: {args.report}")
    print(f"{'idx':>3} | {'name':40} | {'verdict':6} | {'passed/total':>12} "
          f"| top recommendation (priority)")
    print("-" * 110)
    for e in sorted_entries:
        name = e["name"][:40]
        if e.get("error"):
            verdict = "error"
            pt = "-"
            rec = e["error"][:60]
        else:
            rc = (e.get("analysis") or {}).get("roleCheck") or {}
            verdict = rc.get("verdict", "error")
            pt = (f"{rc.get('passed_count', 0)}/{rc.get('total_count', 0)}"
                  if rc else "-")
            recs = (e.get("analysis") or {}).get("recommendations") or []
            if recs:
                rec = (f"[{recs[0].get('priority', '')}] "
                       f"{recs[0].get('message', '')}")
            else:
                rec = ""
        print(f"{e['index']:>3} | {name:40} | {verdict:6} | {pt:>12} "
              f"| {rec[:60]}")
    print("-" * 110)
    print(f"total={total} pass={pass_n} fail={fail_n} error={err_n} "
          f"pass_ratio={report['summary']['pass_ratio']:.2f}")
    if args.reuse_wavs:
        print("note: --reuse-wavs enabled; existing WAVs were re-analyzed "
              "without re-export")
    return report


def run_analyze_only(args, cfg):
    wavdir = args.out
    if not os.path.isdir(wavdir):
        print(f"error: wav dir not found: {wavdir}", file=sys.stderr)
        return 1
    files = sorted(f for f in os.listdir(wavdir)
                   if f.lower().endswith(".wav"))
    if not files:
        print(f"error: no .wav files found in {wavdir}", file=sys.stderr)
        return 1
    if args.offset or args.limit is not None:
        files = files[args.offset:args.offset + (args.limit or len(files))]
    if args.shuffle:
        rng = random.Random(args.shuffle)
        rng.shuffle(files)
    entries = []
    for i, f in enumerate(files, 1):
        wav = os.path.join(wavdir, f)
        base = os.path.splitext(f)[0]
        entry = {"index": i - 1, "file": wav, "name": base,
                 "voice_index": None, "wav": wav, "engine": args.engine,
                 "error": None, "analysis": None}
        try:
            report = AP.analyze_probe(wav, args.role, name=base,
                                      plugin=cfg["sidecar_engine"],
                                      gguf=args.gguf, use_clap=False,
                                      use_llm=(args.gguf is not None))
            if report.get("error"):
                entry["error"] = report["error"]
            entry["analysis"] = report
        except Exception as e:
            entry["error"] = f"analyze failed: {e}"
        label = verdict_label(entry)
        tail = entry["error"] if entry.get("error") else "analyzed"
        print(f"[{i}/{len(files)}] {base} -> {label} ({tail})")
        entries.append(entry)
    patches = entries
    warnings = ["analyze-only mode: no engine used; --host/--port are "
                "metadata only"]
    write_report(args, entries, patches,
                 datetime.datetime.now(), warnings, cfg)
    return 0


def print_plan(args, patches, notes):
    print(f"dry-run plan: {len(patches)} patches | role={args.role} "
          f"out={args.out} engine=ws://{args.host}:{args.port}")
    print(f"root pitch={args.root_pitch} bpm={args.bpm} "
          f"window={args.window}s "
          f"({max(1.0, round(args.window * args.bpm / 60.0))} beats) "
          f"seed={args.seed}")
    print("patches:")
    for p in patches[:20]:
        vi = f" (voice {p['voice_index']})" if p["voice_index"] else ""
        print(f"  {p['file']}{vi}")
    if len(patches) > 20:
        print(f"  ... and {len(patches) - 20} more")
    print("probe notes (clip-local beats):")
    for n in notes:
        print(f"  pitch={n['pitch']} start={n['start']} "
              f"duration={n['duration']} velocity={n['velocity']}")


async def run_dry_run(args, patches):
    engine_ok = False
    session = EngineSession(args)
    try:
        await session.start()
        engine_ok = True
    except EngineUnreachable as e:
        print(f"warning: engine not reachable: {e} (dry-run continues)",
              file=sys.stderr)
    finally:
        if engine_ok:
            await session.close()
    beats = max(1.0, round(args.window * args.bpm / 60.0))
    notes, _warn = build_probe_notes(args.role, args.root_pitch, beats,
                                     args.seed)
    print_plan(args, patches, notes)
    return 0


async def amain(args):
    role = RT.normalize_role(args.role)
    if role not in RT.SUPPORTED_ROLES:
        print(f"error: unknown role '{args.role}'; supported roles: "
              f"{', '.join(RT.SUPPORTED_ROLES)}", file=sys.stderr)
        return 1
    args.role = role
    cfg = ENGINES[args.engine]
    if not args.dir:
        args.dir = cfg["default_dir"]
    if not args.pattern:
        args.pattern = cfg["default_pattern"]
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    if args.report is None:
        args.report = os.path.join(args.out, "report.json")
    else:
        args.report = os.path.abspath(args.report)
    if args.root_pitch is None:
        args.root_pitch = ROLE_DEFAULT_ROOT.get(role, 36)
    args.mode = args.mode or "sweep"

    if args.mode == "analyze-only":
        return run_analyze_only(args, cfg)

    if args.limit is not None and args.limit > 300 and not args.yes:
        print("error: sweeping more than 300 patches requires --yes",
              file=sys.stderr)
        return 1

    patches, err = enumerate_patches(args, cfg)
    if err:
        print(f"error: {err}", file=sys.stderr)
        return 1

    if args.dry_run:
        return await run_dry_run(args, patches)

    started = datetime.datetime.now()
    warnings = []
    beats = max(1.0, round(args.window * args.bpm / 60.0))
    notes, note_warn = build_probe_notes(args.role, args.root_pitch, beats,
                                         args.seed)
    if note_warn:
        warnings.append(note_warn)
    if args.reuse_wavs:
        warnings.append("--reuse-wavs: existing WAVs were re-analyzed "
                        "without re-export")
    warnings.append("engine transport: spawned MCP stdio "
                    "(HDAW_headless.exe --mcp-stdio); ws://host:port is "
                    "report metadata only")

    print(f"sweep: {len(patches)} patches | role={args.role} "
          f"engine={args.engine} out={args.out}")
    print(f"probe: root={args.root_pitch} bpm={args.bpm} "
          f"window={args.window}s ({beats} beats) seed={args.seed}")

    entries = await sweep(args, patches, notes, cfg)
    if entries is None:
        return 1
    write_report(args, entries, patches, started, warnings, cfg)
    return 0


def build_parser():
    ap = argparse.ArgumentParser(
        prog="sweep_dx7_patches.py",
        description="Sweep DX7 .syx (fm_synth) or Access Virus (sub_synth) "
                    "patches through the HDAW engine, render role probes, "
                    "analyze them, and write a ranked report.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode_pos", nargs="?", default=None,
                    choices=["sweep", "analyze-only"],
                    help="mode: sweep (default) or analyze-only")
    ap.add_argument("--mode", choices=["sweep", "analyze-only"], default=None,
                    help="mode (overrides positional)")
    ap.add_argument("--analyze-only", action="store_true",
                    help="alias for mode=analyze-only")
    ap.add_argument("--engine", choices=["fm_synth", "sub_synth"],
                    default="fm_synth",
                    help="synthesis engine to sweep: fm_synth (DX7 .syx, "
                         "default) or sub_synth (Access Virus patches)")
    ap.add_argument("--role", default="bass",
                    help="production role (default bass)")
    ap.add_argument("--dir", default=None,
                    help="search root for patches (default: engine-specific; "
                         "fm_synth d:\\pdf\\dexed presets, sub_synth "
                         "D:\\pdf\\Virus Presets)")
    ap.add_argument("--bank", action="append", default=None, metavar="SUBSTR",
                    help="only files whose path contains SUBSTR "
                         "(case-insensitive; repeatable)")
    ap.add_argument("--names", action="append", default=None, metavar="SUBSTR",
                    help="only files whose basename contains SUBSTR "
                         "(case-insensitive; repeatable)")
    ap.add_argument("--pattern", default=None,
                    help="file glob under --dir (default: engine-specific; "
                         "fm_synth *.syx, sub_synth *)")
    ap.add_argument("--limit", type=int, default=None,
                    help="max patches (sweep default 50; require --yes when "
                         "> 300; analyze-only defaults to all)")
    ap.add_argument("--offset", type=int, default=0,
                    help="skip first N (after sort/filter)")
    ap.add_argument("--shuffle", type=int, default=0, metavar="SEED",
                    help="randomize enumeration order with seed (0 = no "
                         "shuffle)")
    ap.add_argument("--cart-voices", action="store_true",
                    help="sweep every voice of a multi-voice container "
                         "(fm_synth cartridge 0..31; sub_synth TI bank "
                         "0..N-1) instead of voice 0 only")
    ap.add_argument("--out", default=r"timbre-lib\sweep_out",
                    help="probe WAV + report dir (default: "
                         "timbre-lib\\sweep_out)")
    ap.add_argument("--report", default=None,
                    help="aggregate JSON (default <out>\\report.json)")
    ap.add_argument("--reuse-wavs", action="store_true",
                    help="skip re-export when the target WAV already exists "
                         "(analyze from disk)")
    ap.add_argument("--sidecars", action="store_true",
                    help="write a <patch>.dx7.json (fm_synth) or "
                         "<patch>.virus.json (sub_synth) sidecar next to each "
                         "analyzed patch with its (Qwen) description + role "
                         "verdict, so FileLibraryManager / search_library "
                         "can index it; also writes a dsp feature vector "
                         "(rendered-probe timbre) so cluster_library can "
                         "cluster patches; dsp is probe-context-specific — "
                         "cluster only patches swept with the same "
                         "role/seed/window/bpm")
    ap.add_argument("--bpm", type=float, default=138.0,
                    help="tempo for note timing (default 138)")
    ap.add_argument("--window", type=float, default=4.0,
                    help="probe/export window in seconds (default 4.0)")
    ap.add_argument("--root", type=int, default=None, dest="root_pitch",
                    help="probe root MIDI note (default: role-specific)")
    ap.add_argument("--seed", type=int, default=12345,
                    help="deterministic probe notes + shuffle RNG (default "
                         "12345)")
    ap.add_argument("--host", default="127.0.0.1",
                    help="engine WS host (report metadata; default "
                         "127.0.0.1)")
    ap.add_argument("--port", type=int, default=8766,
                    help="engine WS port (report metadata; default 8766)")
    ap.add_argument("--engine-bin", default=None, metavar="PATH",
                    help="HDAW_headless.exe path (default: repo build dir or "
                         "HDAW_ENGINE_BIN)")
    ap.add_argument("--gguf", default=None,
                    help="enable LLM prose via the local model (default: "
                         "none)")
    ap.add_argument("--no-clap", action="store_true",
                    help="skip CLAP captions/tags - keep DSP-only for speed "
                         "(default)")
    ap.add_argument("--keep-track", action="store_true",
                    help="leave the probe track + mutes as-is after the "
                         "sweep (debugging)")
    ap.add_argument("--dry-run", action="store_true",
                    help="enumerate + print the plan, connect to engine but "
                         "mutate nothing")
    ap.add_argument("--yes", action="store_true",
                    help="confirm sweeps > 300 patches")
    ap.add_argument("--verbose", action="store_true",
                    help="per-call RPC logging")
    return ap


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    ap = build_parser()
    args = ap.parse_args()
    mode = args.mode or args.mode_pos or "sweep"
    if args.analyze_only:
        mode = "analyze-only"
    args.mode = mode
    if mode == "sweep" and args.limit is None:
        args.limit = 50
    try:
        rc = asyncio.run(amain(args))
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        rc = 1
    sys.exit(rc)


if __name__ == "__main__":
    main()