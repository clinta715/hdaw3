#!/usr/bin/env python3
"""Tuning feedback loop — timbre-lib style automated tuning check.
Reuses timbre.py:extract for centroid/rolloff85/mel_low/mid/high/f0.
Compares each WAV (full mix or per-track 2s solo) to per-role spectral targets.

Bands: mel_low (<250Hz), mel_mid (250-4000Hz), mel_high (>=4000Hz)
as defined in timbre.py: band[mel_hz <250].sum() etc.

Usage:
  python tune_roles.py <wav> [--role kick|bass|arp|lead|hat|pad]
  python tune_roles.py <wav> --role-map '{"track0":"kick","track2":"hat"}'  # future
  python tune_roles.py <wav> --loop  # offline: analyze + suggestion only (no auto-re-render)

Exit 0 always; JSON on stdout includes pass/fail + suggestion.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import timbre as TB

# Per-role spectral targets (centroid in Hz, mel bands are fractions 0..1)
ROLE_TARGETS = {
    "kick": {
        "centroid_max": 120,
        "mel_low_min": 0.5,
        "desc": "kick/sub <120Hz centroid, mel_low dominant (>=0.5)",
    },
    "bass": {
        "centroid": [60, 250],
        "mel_low_min": 0.4,
        "desc": "bass 60-250Hz centroid, mel_low >=0.4",
    },
    "arp": {
        "centroid": [400, 3000],
        "desc": "arp 400-3000Hz carved mid presence",
    },
    "lead": {
        "centroid": [400, 3000],
        "desc": "lead 400-3000Hz carved mid presence",
    },
    "hat": {
        "centroid_min": 6000,
        "mel_high_min": 0.12,
        "desc": "hat >6kHz centroid, mel_high >=0.12 airy top",
    },
    "pad": {
        "centroid": [250, 4000],
        "desc": "pad 250-4000Hz broad mid, not too dark/bright",
    },
}

# Allow aliases
ROLE_ALIASES = {
    "kick_drum": "kick",
    "drum_kick": "kick",
    "sub": "bass",
    "bassline": "bass",
    "arpeggio": "arp",
    "hats": "hat",
    "hihat": "hat",
    "closed_hat": "hat",
    "open_hat": "hat",
}

def normalize_role(role: str) -> str:
    r = role.strip().lower()
    return ROLE_ALIASES.get(r, r)


def analyze_wav(path: str) -> dict:
    """Load wav at 48k mono via librosa (fallback: scipy) and run TB.extract."""
    import numpy as np

    x = None
    sr = 48000
    # Try librosa first (matches lib_analyze.py path)
    try:
        import librosa
        x, sr = librosa.load(path, sr=48000, mono=True)
    except Exception:
        # Fallback: scipy.io.wavfile + soundfile
        try:
            from scipy.io import wavfile
            sr_file, data = wavfile.read(path)
            sr = sr_file
            # need to handle junk/data chunk - scipy handles it
            # Convert to float mono 48k if needed
            if data.ndim > 1:
                data = data.mean(axis=1)
            # normalize int types
            if data.dtype == np.int16:
                x = data.astype(np.float64) / 32768.0
            elif data.dtype == np.int32:
                x = data.astype(np.float64) / 2147483648.0
            else:
                x = data.astype(np.float64)
            if sr != 48000:
                # simple resample via scipy.signal
                from scipy import signal as sps
                # resample to 48000
                num = int(len(x) * 48000 / sr)
                x = sps.resample(x, num)
                sr = 48000
        except Exception as e:
            raise RuntimeError(f"failed to load wav {path}: {e}") from e

    if x is None or len(x) == 0:
        raise RuntimeError(f"empty audio: {path}")

    d = TB.extract(x, sr)
    # Keep only serializable floats
    out = {}
    for k, v in d.items():
        if isinstance(v, (float, int, bool)):
            out[k] = float(v) if isinstance(v, float) else v
        else:
            # numpy scalars
            try:
                out[k] = float(v)
            except Exception:
                out[k] = v
    out["duration_s"] = float(len(x) / sr)
    return out


def _target_range(role: str):
    t = ROLE_TARGETS.get(role, {})
    if "centroid" in t:
        lo, hi = t["centroid"]
        return lo, hi
    lo = t.get("centroid_min", None)
    hi = t.get("centroid_max", None)
    return lo, hi


def _suggest(role: str, centroid: float, mel_low: float, mel_high: float, tgt: dict) -> str:
    """Deterministic suggestion based on centroid + mel bands."""
    lo, hi = _target_range(role)
    parts = []

    # centroid suggestions
    if lo is not None and centroid < lo:
        parts.append(f"centroid {centroid:.0f}Hz < target {lo:.0f}Hz: raise rootNote +12 (or +7), raise filter cutoff to {min(2500, int(lo*1.2))}Hz, or increase OctaveRange by 1")
    elif hi is not None and centroid > hi:
        parts.append(f"centroid {centroid:.0f}Hz > target {hi:.0f}Hz: lower rootNote -12 (or -7), lower filter cutoff to {max(120, int(hi*0.8))}Hz, or decrease OctaveRange by 1")
    elif lo is not None and hi is not None:
        # in range but near edge: gentle nudge note
        mid = (lo + hi) / 2
        if centroid < mid * 0.7:
            parts.append(f"centroid {centroid:.0f}Hz low in range [{lo:.0f},{hi:.0f}]: consider +7 semitones or slight cutoff up")
        elif centroid > mid * 1.4:
            parts.append(f"centroid {centroid:.0f}Hz high in range [{lo:.0f},{hi:.0f}]: consider -7 semitones or slight cutoff down")

    # mel_low for kick/bass
    mel_low_min = tgt.get("mel_low_min")
    if mel_low_min is not None and mel_low < mel_low_min:
        parts.append(f"mel_low {mel_low:.2f} < {mel_low_min:.2f}: add low-end (rootNote -12, boost 60-120Hz, lower LP cutoff to 120-180Hz)")

    # mel_high for hat
    mel_high_min = tgt.get("mel_high_min")
    if mel_high_min is not None and mel_high < mel_high_min:
        parts.append(f"mel_high {mel_high:.2f} < {mel_high_min:.2f}: add air (raise HP cutoff, boost 8-12kHz, tighten decay, filter cutoff up to 3000+Hz)")

    # generic: if centroid way outside but mel bands not triggered
    if not parts:
        if lo is not None and hi is not None and lo <= centroid <= hi:
            parts.append("tuning OK: no change needed")
        elif lo is None and hi is None:
            parts.append("no centroid target for this role: check mel bands only")
        elif lo is not None and centroid >= lo and hi is None:
            parts.append("tuning OK (above min)")
        elif hi is not None and centroid <= hi and lo is None:
            parts.append("tuning OK (below max)")
        else:
            parts.append("no suggestion (edge case)")

    return "; ".join(parts)


def check_role(role: str, descriptors: dict) -> dict:
    """Compare descriptors to ROLE_TARGETS[role]; return pass/fail + suggestion."""
    role = normalize_role(role)
    tgt = ROLE_TARGETS.get(role)
    if tgt is None:
        return {
            "role": role,
            "pass": False,
            "error": f"unknown role '{role}'; known: {sorted(ROLE_TARGETS.keys())}",
            "actual_centroid": descriptors.get("centroid"),
            "actual_mel_low": descriptors.get("mel_low"),
            "actual_mel_mid": descriptors.get("mel_mid"),
            "actual_mel_high": descriptors.get("mel_high"),
            "target": None,
            "suggestion": f"unknown role; choose from {sorted(ROLE_TARGETS.keys())}",
        }

    centroid = float(descriptors.get("centroid", 0))
    rolloff85 = float(descriptors.get("rolloff85", 0))
    mel_low = float(descriptors.get("mel_low", 0))
    mel_mid = float(descriptors.get("mel_mid", 0))
    mel_high = float(descriptors.get("mel_high", 0))

    lo, hi = _target_range(role)
    passed = True
    reasons = []

    if lo is not None and centroid < lo:
        passed = False
        reasons.append(f"centroid {centroid:.0f} < {lo:.0f}")
    if hi is not None and centroid > hi:
        passed = False
        reasons.append(f"centroid {centroid:.0f} > {hi:.0f}")
    if tgt.get("mel_low_min") is not None and mel_low < tgt["mel_low_min"]:
        passed = False
        reasons.append(f"mel_low {mel_low:.2f} < {tgt['mel_low_min']:.2f}")
    if tgt.get("mel_high_min") is not None and mel_high < tgt["mel_high_min"]:
        passed = False
        reasons.append(f"mel_high {mel_high:.2f} < {tgt['mel_high_min']:.2f}")

    suggestion = _suggest(role, centroid, mel_low, mel_high, tgt)

    return {
        "role": role,
        "pass": passed,
        "actual_centroid": round(centroid, 1),
        "actual_rolloff85": round(rolloff85, 1),
        "actual_mel_low": round(mel_low, 4),
        "actual_mel_mid": round(mel_mid, 4),
        "actual_mel_high": round(mel_high, 4),
        "actual_bandwidth": round(float(descriptors.get("bandwidth", 0)), 1),
        "actual_f0_hz": round(float(descriptors.get("f0_hz", 0)), 1),
        "target": tgt,
        "reason": "; ".join(reasons) if reasons else ("within target" if passed else "no reason"),
        "suggestion": suggestion,
    }


def tune_roles(mix_path: str, role_map: dict | None = None) -> dict:
    """Analyze mix_path once; if role_map given, treat mix as that role or check each.
    role_map: optional dict mapping label->role e.g. {"bass.wav":"bass"}.
    For single-file usage, caller should call check_role separately.
    Returns overall dict with descriptors + per-role checks.
    """
    desc = analyze_wav(mix_path)
    result = {
        "wav": mix_path,
        "descriptors": {
            "centroid": round(float(desc["centroid"]), 1),
            "bandwidth": round(float(desc["bandwidth"]), 1),
            "rolloff85": round(float(desc["rolloff85"]), 1),
            "rolloff95": round(float(desc["rolloff95"]), 1),
            "mel_low": round(float(desc["mel_low"]), 4),
            "mel_mid": round(float(desc["mel_mid"]), 4),
            "mel_high": round(float(desc["mel_high"]), 4),
            "f0_hz": round(float(desc.get("f0_hz", 0)), 1),
            "duration_s": round(float(desc.get("duration_s", 0)), 3),
            "rms": round(float(desc.get("rms", 0)), 5),
            "peak": round(float(desc.get("peak", 0)), 5),
        },
        "summary": TB.summarize(desc),
    }
    if role_map:
        checks = {}
        for label, role in role_map.items():
            checks[label] = check_role(role, desc)
        result["checks"] = checks
        result["overall_pass"] = all(c.get("pass") for c in checks.values())
    return result


def main():
    ap = argparse.ArgumentParser(description="Timbre tuning check (timbre-lib style)")
    ap.add_argument("wav", help="path to WAV file to analyze")
    ap.add_argument("--role", default=None, help="role to check: kick/bass/arp/lead/hat/pad")
    ap.add_argument("--loop", action="store_true", help="offline loop flag: analyze + suggest only (no auto-re-render; placeholder for MCP iteration)")
    ap.add_argument("--role-map", default=None, help="JSON dict mapping label->role (advanced)")
    ap.add_argument("--json", action="store_true", help="force JSON output (default)")
    args = ap.parse_args()

    wav = args.wav
    if not os.path.exists(wav):
        print(json.dumps({"error": f"wav not found: {wav}", "pass": False}, indent=2))
        sys.exit(1)

    try:
        desc = analyze_wav(wav)
    except Exception as e:
        print(json.dumps({"error": str(e), "pass": False}, indent=2))
        sys.exit(1)

    out = {
        "wav": wav,
        "descriptors": {
            "centroid": round(float(desc["centroid"]), 1),
            "bandwidth": round(float(desc["bandwidth"]), 1),
            "rolloff85": round(float(desc["rolloff85"]), 1),
            "rolloff95": round(float(desc["rolloff95"]), 1),
            "mel_low": round(float(desc["mel_low"]), 4),
            "mel_mid": round(float(desc["mel_mid"]), 4),
            "mel_high": round(float(desc["mel_high"]), 4),
            "f0_hz": round(float(desc.get("f0_hz", 0)), 1),
            "duration_s": round(float(desc.get("duration_s", 0)), 3),
            "rms": round(float(desc.get("rms", 0)), 5),
            "peak": round(float(desc.get("peak", 0)), 5),
            "tonal_fraction": round(float(desc.get("tonal_fraction", 0)), 4),
        },
        "summary": TB.summarize(desc),
    }

    if args.role_map:
        try:
            rm = json.loads(args.role_map)
        except Exception as e:
            print(json.dumps({"error": f"bad --role-map JSON: {e}", "pass": False}, indent=2))
            sys.exit(1)
        checks = {}
        for label, role in rm.items():
            checks[label] = check_role(role, desc)
        out["checks"] = checks
        out["overall_pass"] = all(c.get("pass") for c in checks.values())
    elif args.role:
        chk = check_role(args.role, desc)
        out["check"] = chk
        out["pass"] = chk["pass"]
        out["suggestion"] = chk["suggestion"]
        # loop placeholder: we only analyze+ suggest; no mutation
        if args.loop:
            out["loop"] = {
                "iterations": 1,
                "max_iterations": 3,
                "note": "offline loop: analysis + suggestion only; re-render via MCP export + re-analyze until pass or max 3"
            }

    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
