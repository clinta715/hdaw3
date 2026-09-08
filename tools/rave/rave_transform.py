#!/usr/bin/env python3
"""RAVE sidecar transform script (HDAW RAVE #1).

CLI contract (must match HDAW::RaveService exactly):
    transform (default): script.py --input X --model Y --output Z --temperature T --seed S
    probe:             script.py --mode probe --model Y

Modes:
  1. REAL model inference -- torch is importable AND --model loads as a
     TorchScript module AND supports a waveform in/out call. Output is the
     model's render.
  2. FALLBACK-DSP -- deterministic stdlib-only waveshape transform driven by
     temperature (drive amount) with seed-driven subtle variation. Used when
     torch is unavailable (clean machine) or when --allow-fallback is given
     and real inference is unavailable. NEVER claims RAVE fidelity: stdout
     always carries an explicit ``FALLBACK-DSP`` marker in this mode.

Exit contract (mirrors the C++ side):
  * exit 0  => --output WAV was written and verified (finite, non-silent).
  * exit != 0 => failure with a clear message on stderr; no output claimed.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import struct
import sys
import wave

SUPPORTED_MODEL_EXTS = (".ts", ".pt", ".pth", ".rave", ".onnx")
FALLBACK_MARKER = "FALLBACK-DSP"

# Temperature range accepted by this sidecar. RAVE prior-sampling
# temperatures are typically in [0, ~2]; we allow headroom but reject
# garbage (inf/NaN/negative/huge) before it can reach any DSP.
TEMPERATURE_MIN = 0.0
TEMPERATURE_MAX = 16.0


_last_error: str = ""


def fail(msg: str, code: int = 2) -> "NoReturn":
    global _last_error
    from typing import NoReturn  # noqa: F401  (local import keeps module import light)
    _last_error = msg
    print(f"rave_transform: error: {msg}", file=sys.stderr)
    raise SystemExit(code)


# ---------------------------------------------------------------------------
# WAV I/O (stdlib only)
# ---------------------------------------------------------------------------

def read_wav_mono(path: str) -> tuple[list[float], int, int]:
    """Read a PCM WAV file, return (mono float samples in [-1, 1], rate, channels)."""
    try:
        wf = wave.open(path, "rb")
    except (OSError, wave.Error) as exc:
        fail(f"cannot open input WAV '{path}': {exc}")
    with wf:
        nch = wf.getnchannels()
        width = wf.getsampwidth()
        rate = wf.getframerate()
        nframes = wf.getnframes()
        if nframes <= 0:
            fail(f"input WAV '{path}' has no audio frames")
        raw = wf.readframes(nframes)
    if len(raw) == 0:
        fail(f"input WAV '{path}' has no audio data")

    if width == 1:
        # 8-bit unsigned
        vals = struct.unpack(f"<{len(raw)}B", raw)
        floats = [(v - 128) / 128.0 for v in vals]
    elif width == 2:
        count = len(raw) // 2
        vals = struct.unpack(f"<{count}h", raw[: count * 2])
        floats = [v / 32768.0 for v in vals]
    elif width == 3:
        floats = []
        for i in range(0, len(raw) - len(raw) % 3, 3):
            chunk = raw[i : i + 3]
            iv = int.from_bytes(chunk, "little", signed=False)
            if iv & 0x800000:
                iv -= 0x1000000
            floats.append(iv / 8388608.0)
    elif width == 4:
        count = len(raw) // 4
        vals = struct.unpack(f"<{count}i", raw[: count * 4])
        floats = [v / 2147483648.0 for v in vals]
    else:
        fail(f"unsupported sample width {width * 8}-bit in '{path}'")

    # Downmix to mono for the transform path.
    if nch > 1:
        frames = len(floats) // nch
        mono = [0.0] * frames
        for f in range(frames):
            acc = 0.0
            for c in range(nch):
                acc += floats[f * nch + c]
            mono[f] = acc / nch
    else:
        mono = list(floats)

    # Sanitize: replace non-finite input with silence (never propagate NaN/inf).
    cleaned = [s if math.isfinite(s) else 0.0 for s in mono]
    return cleaned, rate, nch


def write_wav_mono(path: str, samples: list[float], rate: int) -> None:
    """Write mono 16-bit PCM WAV, clipping to [-1, 1]. Rejects NaN/inf."""
    for s in samples:
        if not math.isfinite(s):
            fail("internal error: non-finite sample reached WAV writer (clamping bug)")
    ints = []
    for s in samples:
        c = 1.0 if s > 1.0 else (-1.0 if s < -1.0 else s)
        ints.append(int(round(c * 32767.0)))
    data = struct.pack(f"<{len(ints)}h", *ints) if ints else b""
    try:
        with wave.open(path, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(rate)
            wf.writeframes(data)
    except (OSError, wave.Error) as exc:
        fail(f"cannot write output WAV '{path}': {exc}")


def wav_peak(path: str) -> tuple[int, float, int]:
    """Return (frames, peak abs amplitude, rate) for a written WAV."""
    with wave.open(path, "rb") as wf:
        nframes = wf.getnframes()
        width = wf.getsampwidth()
        rate = wf.getframerate()
        raw = wf.readframes(nframes)
    if width == 2 and raw:
        count = len(raw) // 2
        vals = struct.unpack(f"<{count}h", raw[: count * 2])
        peak = max(abs(v) for v in vals) / 32768.0
    elif raw:
        peak = 1.0  # non-16-bit output we wrote ourselves; trust writer checks
    else:
        peak = 0.0
    return nframes, peak, rate


# ---------------------------------------------------------------------------
# Fallback DSP: deterministic, temperature-driven, seed-varied (stdlib only)
# ---------------------------------------------------------------------------

def fallback_transform(samples: list[float], temperature: float, seed: int) -> list[float]:
    """Deterministic waveshape transform. NOT a RAVE model render.

    * temperature controls saturation drive: drive = 1 + 2*t.
    * seed drives a small deterministic variation (2nd-harmonic blend and
      wet/dry mix jitter) so different seeds render audibly different files.
    * Output is tanh-normalized, wet/dry mixed, clipped to [-1, 1].
    """
    rng = random.Random(seed)
    drive = 1.0 + 2.0 * temperature
    norm = math.tanh(drive)
    k2 = (rng.random() - 0.5) * 0.20      # seed-driven 2nd-harmonic depth (+/-10%)
    wet = min(1.0, max(0.0, 0.55 + 0.20 * temperature + (rng.random() - 0.5) * 0.06))
    makeup = 1.0 / max(1e-6, (0.55 + 0.45 * wet))

    out: list[float] = []
    for x in samples:
        shaped = math.tanh(drive * x) / norm
        shaped = shaped + k2 * shaped * shaped
        y = (1.0 - wet) * x + wet * shaped
        y *= makeup
        if y > 1.0:
            y = 1.0
        elif y < -1.0:
            y = -1.0
        out.append(y)
    return out


# ---------------------------------------------------------------------------
# Real model path (torch, optional)
# ---------------------------------------------------------------------------

def _shape_of(value) -> list[int] | None:
    if hasattr(value, "shape"):
        return [int(x) for x in list(value.shape)]
    return None


def _public_methods(module) -> list[str]:
    names: set[str] = set()
    for name in dir(module):
        if not name.startswith("_"):
            names.add(name)
    try:
        for method in module._c._method_names():  # type: ignore[attr-defined]
            if not str(method).startswith("_"):
                names.add(str(method))
    except Exception:
        pass
    return sorted(names)


def _sample_rate_from_module(module):
    for name in ("sample_rate", "sampleRate", "sr", "sampling_rate"):
        try:
            value = getattr(module, name)
            if callable(value):
                value = value()
            if hasattr(value, "item"):
                value = value.item()
            if isinstance(value, (int, float)) and value > 0:
                return int(value)
        except Exception:
            continue
    return None


def probe_model(model_path: str) -> dict:
    payload = {
        "ok": False,
        "methods": [],
        "sampleRate": None,
        "latentDim": None,
        "latentFrames": None,
        "encodeShape": None,
        "decodeShape": None,
        "error": "",
    }
    try:
        import torch  # type: ignore
    except Exception as exc:  # noqa: BLE001
        payload["error"] = f"torch not importable: {type(exc).__name__}: {exc}"
        return payload

    try:
        module = torch.jit.load(model_path, map_location="cpu")
        module.eval()
        payload["methods"] = _public_methods(module)
        payload["sampleRate"] = _sample_rate_from_module(module)
        with torch.no_grad():
            x = torch.zeros(1, 1, 44100, dtype=torch.float32)
            if hasattr(module, "encode"):
                z = module.encode(x)
                if isinstance(z, (tuple, list)):
                    z = z[0]
                encode_shape = _shape_of(z)
                payload["encodeShape"] = encode_shape
                if encode_shape and len(encode_shape) >= 3:
                    payload["latentDim"] = int(encode_shape[1])
                    payload["latentFrames"] = int(encode_shape[2])
                if hasattr(module, "decode"):
                    y = module.decode(z)
                    if isinstance(y, (tuple, list)):
                        y = y[0]
                    payload["decodeShape"] = _shape_of(y)
            elif hasattr(module, "forward"):
                y = module(x)
                if isinstance(y, (tuple, list)):
                    y = y[0]
                payload["decodeShape"] = _shape_of(y)
        payload["ok"] = True
        payload["error"] = ""
    except Exception as exc:  # noqa: BLE001
        payload["error"] = f"{type(exc).__name__}: {exc}"
    return payload


def print_probe_json(payload: dict) -> int:
    print(json.dumps(payload, separators=(",", ":")))
    if not payload.get("ok"):
        err = str(payload.get("error") or "probe failed")
        print(f"rave_transform probe: {err}", file=sys.stderr)
        return 1
    return 0


def try_real_inference(model_path: str, samples: list[float], rate: int,
                        temperature: float, seed: int) -> list[float] | None:
    """Attempt genuine TorchScript RAVE inference.

    Returns the rendered samples on success, or None if torch is not
    importable (caller falls back). Raises SystemExit with a clear message
    if torch IS present but the model/inference is unusable -- that must
    never silently degrade to fake output.
    """
    try:
        import torch  # type: ignore
    except ImportError:
        return None

    load_errors: list[str] = []
    module = None
    try:
        module = torch.jit.load(model_path, map_location="cpu")
    except Exception as exc:  # noqa: BLE001 -- reported verbatim to the user
        load_errors.append(f"{type(exc).__name__}: {exc}")
    if module is None:
        fail(
            "torch is installed but the model could not be loaded as TorchScript "
            f"('{model_path}'): {'; '.join(load_errors)}. "
            "Place a valid RAVE export here or re-run with --allow-fallback "
            "for the deterministic DSP stand-in."
        )

    try:
        import torch  # type: ignore  # noqa: F811
        torch.manual_seed(seed)
        # RAVE v2 waveform-in/out TorchScript exports expect [N, C, T] (a
        # batched audio tensor); passing [1, T] breaks inside pqmf/cached_conv.
        x = torch.tensor(samples, dtype=torch.float32).reshape(1, 1, -1)  # [1, 1, T]
        module.eval()
        with torch.no_grad():
            y = module(x)
            if isinstance(y, (tuple, list)):
                y = y[0]
        print(
            "REAL-MODEL note: temperature/seed only affect torch.manual_seed noise "
            "for models that sample internally; RAVE v2 forward is deterministic "
            "reconstruction (no temperature applied)."
        )
        if not hasattr(y, "detach"):
            fail(
                f"model '{model_path}' returned {type(y).__name__}, not a tensor; "
                "this sidecar supports waveform in/out TorchScript RAVE exports. "
                "Re-run with --allow-fallback for the DSP stand-in."
            )
        y = y.detach().cpu().float()
        if y.dim() == 3 and y.shape[1] > 1:
            # Multi-channel export (e.g. stereo vintage.ts returns [1, 2, T']):
            # downmix to mono. A bare reshape(-1) would interleave channels and
            # double the rendered duration. Mono [1, 1, T'] is unaffected.
            y = y.mean(dim=1)
        flat = y.reshape(-1).tolist()
    except SystemExit:
        raise
    except Exception as exc:  # noqa: BLE001
        fail(
            f"real model inference failed on '{model_path}': "
            f"{type(exc).__name__}: {exc}. "
            "Re-run with --allow-fallback for the DSP stand-in."
        )

    if len(flat) == 0:
        fail(f"model '{model_path}' returned empty audio; refusing to write silence.")
    if any(not math.isfinite(v) for v in flat):
        fail(f"model '{model_path}' returned non-finite samples; refusing to write corrupt audio.")
    peak = max(abs(v) for v in flat)
    if peak < 1e-4:
        fail(f"model '{model_path}' returned digital silence (peak {peak:.2e}); refusing to claim success.")
    return [min(1.0, max(-1.0, v)) for v in flat]


# ---------------------------------------------------------------------------
# Test-tone generator (fixture helper)
# ---------------------------------------------------------------------------

def make_test_tone(path: str, seconds: float = 2.0, rate: int = 44100,
                    freq: float = 440.0) -> None:
    n = max(1, int(seconds * rate))
    samples = [
        0.6 * math.sin(2.0 * math.pi * freq * i / rate)
        + 0.2 * math.sin(2.0 * math.pi * 2.0 * freq * i / rate)
        for i in range(n)
    ]
    write_wav_mono(path, samples, rate)
    print(f"test-tone: wrote {n} frames @ {rate} Hz to {path}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="HDAW RAVE sidecar: render input audio through a RAVE model "
        "(torch) or a deterministic DSP stand-in (stdlib only)."
    )
    p.add_argument("--mode", choices=("transform", "probe"), default="transform",
                   help="operation mode: transform writes --output WAV; probe prints JSON metadata only")
    p.add_argument("--input", default=None, help="input WAV file")
    p.add_argument("--model", default=None, help="RAVE model file (.ts/.pt/.pth/.rave/.onnx)")
    p.add_argument("--output", default=None, help="output WAV file to write")
    p.add_argument("--temperature", type=float, default=1.0,
                   help=f"sampling temperature [{TEMPERATURE_MIN}, {TEMPERATURE_MAX}] (default 1.0)")
    p.add_argument("--seed", type=int, default=0, help="integer seed >= 0 (default 0)")
    p.add_argument("--allow-fallback", action="store_true",
                   help="permit the deterministic DSP stand-in when real model "
                   "inference is unavailable (no torch, or model unloadable)")
    p.add_argument("--make-test-tone", action="store_true",
                   help="write a synthetic test tone to --output and exit")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if args.make_test_tone:
        if not args.output:
            fail("--make-test-tone requires --output <tone.wav>")
        make_test_tone(args.output)
        return 0

    if args.mode == "probe":
        if not args.model:
            return print_probe_json({
                "ok": False, "methods": [], "sampleRate": None,
                "latentDim": None, "latentFrames": None,
                "encodeShape": None, "decodeShape": None,
                "error": "--model is required",
            })
        if not os.path.isfile(args.model):
            return print_probe_json({
                "ok": False, "methods": [], "sampleRate": None,
                "latentDim": None, "latentFrames": None,
                "encodeShape": None, "decodeShape": None,
                "error": f"model file not found: {args.model}",
            })
        ext = os.path.splitext(args.model)[1].lower()
        if ext not in SUPPORTED_MODEL_EXTS:
            return print_probe_json({
                "ok": False, "methods": [], "sampleRate": None,
                "latentDim": None, "latentFrames": None,
                "encodeShape": None, "decodeShape": None,
                "error": f"unsupported RAVE model extension '{ext}' (expected one of {', '.join(SUPPORTED_MODEL_EXTS)})",
            })
        return print_probe_json(probe_model(args.model))

    for name in ("input", "model", "output"):
        if not getattr(args, name):
            fail(f"--{name} is required")

    if not math.isfinite(args.temperature) or not (TEMPERATURE_MIN <= args.temperature <= TEMPERATURE_MAX):
        fail(f"--temperature must be within [{TEMPERATURE_MIN}, {TEMPERATURE_MAX}] (got {args.temperature})")
    if args.seed is None or args.seed < 0:
        fail(f"--seed must be an integer >= 0 (got {args.seed})")

    if not os.path.isfile(args.input):
        fail(f"input file not found: {args.input}")
    if not os.path.isfile(args.model):
        fail(f"model file not found: {args.model}")
    ext = os.path.splitext(args.model)[1].lower()
    if ext not in SUPPORTED_MODEL_EXTS:
        fail(f"unsupported RAVE model extension '{ext}' (expected one of {', '.join(SUPPORTED_MODEL_EXTS)})")

    out_dir = os.path.dirname(os.path.abspath(args.output))
    if out_dir and not os.path.isdir(out_dir):
        fail(f"output directory does not exist: {out_dir}")

    samples, rate, _channels = read_wav_mono(args.input)

    rendered: list[float] | None = None
    used_fallback = False
    try:
        import torch  # type: ignore
        have_torch = True
    except ImportError:
        have_torch = False

    if have_torch:
        try:
            rendered = try_real_inference(args.model, samples, rate, args.temperature, args.seed)
        except SystemExit as exc:
            if not args.allow_fallback:
                raise
            print(f"{FALLBACK_MARKER}: real inference unavailable; "
                  f"{_last_error} Using deterministic DSP stand-in (NOT RAVE fidelity).")
            rendered = None
        if rendered is not None:
            print(f"REAL-MODEL: rendered {len(rendered)} samples via torch model '{args.model}'")
        else:
            used_fallback = True
    else:
        used_fallback = True
        print(f"{FALLBACK_MARKER}: torch not importable; using deterministic DSP "
              "stand-in (NOT RAVE fidelity). Install torch + stage a real model "
              "for genuine timbre transfer.")

    if rendered is None:
        rendered = fallback_transform(samples, args.temperature, args.seed)
        if not used_fallback:
            used_fallback = True
            print(f"{FALLBACK_MARKER}: using deterministic DSP stand-in (NOT RAVE fidelity).")

    write_wav_mono(args.output, rendered, rate)

    # Post-write verification: exists, non-zero, non-silent, finite.
    if not os.path.isfile(args.output) or os.path.getsize(args.output) == 0:
        fail(f"output was not created correctly: {args.output}")
    nframes, peak, _ = wav_peak(args.output)
    if nframes == 0 or peak < 0.01:
        try:
            os.remove(args.output)
        except OSError:
            pass
        fail(f"render is silent/empty (frames={nframes}, peak={peak:.4f}); refusing to claim success.")

    mode = FALLBACK_MARKER if used_fallback else "REAL-MODEL"
    print(f"{mode}: wrote {args.output} ({nframes} frames @ {rate} Hz, peak {peak:.3f}, "
          f"temperature {args.temperature}, seed {args.seed})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
