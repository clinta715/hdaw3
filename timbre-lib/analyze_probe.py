#!/usr/bin/env python3
"""Standalone WAV-in synth probe analyzer.

Validates a rendered instrument probe against a psytrance production role and
emits one stable JSON report:

    validate -> load (48k mono) -> DSP measurements (timbre.py +
    spectral_evolution) -> optional CLAP captions/tags -> optional LLM prose ->
    deterministic role check -> recommendations.

Deterministic checks produce the verdict; the LLM only explains measured
evidence and can never override a check.

Usage:
    python analyze_probe.py <wav> --role ROLE [--name N] [--plugin P]
        [--gguf PATH] [--no-clap] [--no-llm]

Exit 0 for any completed analysis (including critical findings / verdict fail),
exit 1 for error reports (missing/malformed/unreadable/invalid role)."""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import role_targets as RT
import timbre as TB

LABELS = os.path.join(HERE, "audioset_labels.txt")


def _basename(path):
    return os.path.splitext(os.path.basename(str(path)))[0]


def _load_scipy(path, target_sr):
    from scipy import signal as sps
    from scipy.io import wavfile

    sr, data = wavfile.read(path)
    if data.ndim > 1:
        data = data.mean(axis=1)
    if data.dtype == np.int16:
        x = data.astype(np.float64) / 32768.0
    elif data.dtype == np.int32:
        x = data.astype(np.float64) / 2147483648.0
    elif data.dtype == np.uint8:
        x = (data.astype(np.float64) - 128.0) / 128.0
    else:
        x = data.astype(np.float64)
    if sr != target_sr:
        num = int(len(x) * target_sr / sr)
        x = sps.resample(x, num)
        sr = target_sr
    return x, sr


def load_audio(path):
    """Load a WAV to float64 mono at 48 kHz. librosa first, scipy fallback.

    Raises FileNotFoundError for a missing path, ValueError for empty or
    unreadable audio. Signals shorter than 0.05 s are zero-padded to 2400
    samples so timbre.extract framing is stable."""
    if not os.path.exists(path):
        raise FileNotFoundError(f"input not found: {path}")
    x = None
    sr = 48000
    try:
        import librosa
        x, sr = librosa.load(path, sr=48000, mono=True)
    except Exception:  # noqa: BLE001 - any load failure falls back to scipy
        try:
            x, sr = _load_scipy(path, 48000)
        except Exception as e:
            raise ValueError(f"unreadable audio: {path}: {e}") from e
    if x is None or len(x) == 0:
        raise ValueError(f"empty audio: {path}")
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    min_samples = int(0.05 * sr)
    if len(x) < min_samples:
        x = np.pad(x, (0, min_samples - len(x)))
    return x, sr


def _report(input_info, role, measurements, role_check, description,
            recommendations, warnings, error):
    return {"input": input_info, "role": role, "measurements": measurements,
            "roleCheck": role_check, "description": description,
            "recommendations": recommendations, "warnings": warnings,
            "error": error}


def analyze_probe(path, role, name=None, plugin=None, gguf=None,
                  use_clap=True, use_llm=True):
    """Analyze one probe WAV against a role; returns the JSON-report dict."""
    warnings = []
    role = RT.normalize_role(role)
    input_info = {"path": str(path),
                  "name": (name if name else _basename(path)),
                  "plugin": (plugin if plugin else None)}
    if role not in RT.SUPPORTED_ROLES:
        return _report(input_info, role, None, None, "", [], [],
                       f"unknown role '{role}'; supported roles: {', '.join(RT.SUPPORTED_ROLES)}")
    try:
        x, sr = load_audio(path)
    except Exception as e:  # noqa: BLE001 - spec: any load failure -> error report
        return _report(input_info, role, None, None, "", [], [], str(e))
    if not bool(np.isfinite(x).all()):
        warnings.append("non-finite samples detected")
        return _report(input_info, role, None, None, "",
                       RT.build_recommendations(role, None, None), warnings, None)
    meas = TB.extract(x, sr)
    meas.update(RT.spectral_evolution(x, sr))
    meas["body"] = float(meas["mel_mid"]) + float(meas["mel_high"])
    measurements = {k: round(float(v), 6) for k, v in meas.items()}
    if measurements["peak"] < 1e-5 or measurements["rms"] < 1e-7:
        warnings.append("input is silent (peak below -100 dBFS)")
    if measurements["peak"] >= 0.99:
        warnings.append("input is clipped (peak >= 0.99 of full scale)")

    captions, tags = [], []
    if use_clap:
        try:
            import clap_stage as CS
            aemb = CS.audio_embed(path)
            captions = CS.rank_captions(aemb, CS._CAPTIONS, top_k=4)
            tags = CS.tag_audioset(aemb, LABELS, top_k=6, threshold=0.04)
        except ImportError as e:
            warnings.append(f"clap stage unavailable: {e}")
        except Exception as e:  # noqa: BLE001 - model run failure is non-fatal
            warnings.append(f"clap stage failed: {e}")
    else:
        warnings.append("clap stage unavailable: disabled (use_clap=False)")

    description = ""
    if use_llm and gguf:
        try:
            import llama_cpp  # noqa: F401  # must be importable to run the LLM
            import llm_stage as LS
            summary = TB.summarize(measurements)
            evidence = LS.build_probe_evidence(summary, tags, captions, role, name, plugin)
            description = LS.run_llm_role(evidence, role, gguf)
        except ImportError as e:
            warnings.append(f"llm stage unavailable: {e}")
        except Exception as e:  # noqa: BLE001 - model run failure is non-fatal
            warnings.append(f"llm stage failed: {e}")
    elif not use_llm:
        warnings.append("llm stage unavailable: disabled (use_llm=False)")
    else:
        warnings.append("llm stage unavailable: no gguf model provided")
    if not description:
        description = TB.summarize(measurements)

    role_check = RT.check_role(role, measurements)
    recommendations = RT.build_recommendations(role, measurements, role_check)
    return _report(input_info, role, measurements, role_check, description,
                   recommendations, warnings, None)


def main():
    ap = argparse.ArgumentParser(
        description="Synth probe analyzer: validate a rendered probe WAV "
                    "against a psytrance production role.")
    ap.add_argument("wav", help="path to the probe WAV file")
    ap.add_argument("--role", required=True,
                    help="production role: " + ", ".join(RT.SUPPORTED_ROLES))
    ap.add_argument("--name", default=None, help="optional patch name")
    ap.add_argument("--plugin", default=None, help="optional plugin name")
    ap.add_argument("--gguf", default=None, help="optional GGUF model path for LLM prose")
    ap.add_argument("--no-clap", action="store_true", help="skip the CLAP caption/tag stage")
    ap.add_argument("--no-llm", action="store_true", help="skip the LLM prose stage")
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    report = analyze_probe(args.wav, args.role, name=args.name, plugin=args.plugin,
                           gguf=args.gguf, use_clap=not args.no_clap,
                           use_llm=not args.no_llm)
    sys.stdout.write(json.dumps(report, indent=2) + "\n")
    sys.stdout.flush()
    sys.exit(1 if report.get("error") else 0)


if __name__ == "__main__":
    main()