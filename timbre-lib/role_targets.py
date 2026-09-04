#!/usr/bin/env python3
"""Deterministic per-role target checks for the synth probe analyzer.

Standalone module (numpy only) so it can be imported without
torch/transformers/llama. Role thresholds live HERE and only here -- they are
never embedded in the LLM prompt. The LLM explains measured evidence and can
never override these checks.

Measurement keys come from timbre.extract (centroid, mel_low, mel_mid,
mel_high, decay_s, attack_s, tonal_fraction, f0_hz, ...) plus the derived
``body`` (mel_mid + mel_high) and the spectral_evolution keys
(energy_growth, centroid_growth) used by the riser/fx roles."""
import numpy as np

SUPPORTED_ROLES = ("kick", "bass", "hat", "snare", "rim", "clap",
                   "lead", "arp", "stab", "pad", "riser", "fx")

ROLE_ALIASES = {
    "kick_drum": "kick",
    "drum_kick": "kick",
    "kicks": "kick",
    "sub": "bass",
    "sub_bass": "bass",
    "bassline": "bass",
    "basses": "bass",
    "arpeggio": "arp",
    "arps": "arp",
    "hats": "hat",
    "hihat": "hat",
    "hi_hat": "hat",
    "hi-hat": "hat",
    "closed_hat": "hat",
    "open_hat": "hat",
    "snares": "snare",
    "rims": "rim",
    "claps": "clap",
    "clap_snare": "clap",
    "snare_clap": "clap",
    "leads": "lead",
    "pads": "pad",
    "risers": "riser",
    "effect": "fx",
    "sfx": "fx",
    "fx_effect": "fx",
}


def normalize_role(role):
    """Lowercase + alias-map a role string; unknown roles pass through."""
    r = str(role).strip().lower()
    return ROLE_ALIASES.get(r, r)


def _ck(name, op, target, desc):
    return {"name": name, "op": op, "target": target, "desc": desc}


ROLE_TARGETS = {
    "kick": {
        "checks": [
            _ck("centroid", "le", 120, "centroid below 120 Hz (deep thump)"),
            _ck("mel_low", "ge", 0.45, "sub-bass dominant (mel_low >= 0.45)"),
            _ck("decay_s", "ge", 0.5, "short decay (energy dies quickly)"),
        ],
        "summary": "kick/sub: centroid <= 120 Hz, mel_low >= 0.45, decay_s >= 0.5",
    },
    "bass": {
        "checks": [
            _ck("centroid", "range", [60, 250], "energy concentrated around 60-250 Hz"),
            _ck("mel_low", "ge", 0.35, "sub/low energy (mel_low >= 0.35)"),
            _ck("decay_s", "le", 0.85, "controlled decay (not a one-shot thump)"),
        ],
        "summary": "bass: centroid 60-250 Hz, mel_low >= 0.35, decay_s <= 0.85",
    },
    "hat": {
        "checks": [
            _ck("centroid", "ge", 6000, "energy above 6 kHz"),
            _ck("mel_high", "ge", 0.12, "airy top (mel_high >= 0.12)"),
            _ck("decay_s", "ge", 0.5, "short decay (fast, percussive)"),
        ],
        "summary": "hat: centroid >= 6000 Hz, mel_high >= 0.12, decay_s >= 0.5",
    },
    "snare": {
        "checks": [
            _ck("centroid", "range", [1200, 8000], "mid/high transient energy"),
            _ck("body", "ge", 0.4, "body = mel_mid + mel_high >= 0.4"),
            _ck("attack_s", "le", 0.05, "fast attack transient"),
            _ck("decay_s", "ge", 0.5, "short decay"),
        ],
        "summary": "snare: centroid 1200-8000 Hz, body >= 0.4, attack_s <= 0.05, decay_s >= 0.5",
    },
    "rim": {
        "checks": [
            _ck("centroid", "range", [800, 8000], "mid/high transient energy"),
            _ck("body", "ge", 0.35, "body = mel_mid + mel_high >= 0.35"),
            _ck("attack_s", "le", 0.05, "fast attack transient"),
            _ck("decay_s", "ge", 0.5, "short decay"),
        ],
        "summary": "rim: centroid 800-8000 Hz, body >= 0.35, attack_s <= 0.05, decay_s >= 0.5",
    },
    "clap": {
        "checks": [
            _ck("centroid", "range", [1500, 8000], "mid/high burst energy"),
            _ck("body", "ge", 0.4, "body = mel_mid + mel_high >= 0.4"),
            _ck("attack_s", "le", 0.05, "fast attack transient"),
            _ck("decay_s", "ge", 0.5, "short decay"),
        ],
        "summary": "clap: centroid 1500-8000 Hz, body >= 0.4, attack_s <= 0.05, decay_s >= 0.5",
    },
    "lead": {
        "checks": [
            _ck("tonal_fraction", "gt", 0.0, "tonal (pitched), not noisy"),
            _ck("f0_hz", "range", [150, 2500], "pitch in the lead range"),
            _ck("centroid", "range", [400, 3000], "midrange presence"),
        ],
        "summary": "lead: tonal, f0 150-2500 Hz, centroid 400-3000 Hz",
    },
    "arp": {
        "checks": [
            _ck("tonal_fraction", "gt", 0.0, "tonal (pitched), not noisy"),
            _ck("f0_hz", "range", [150, 4000], "pitch in the arp range"),
            _ck("centroid", "range", [400, 3500], "midrange presence"),
        ],
        "summary": "arp: tonal, f0 150-4000 Hz, centroid 400-3500 Hz",
    },
    "stab": {
        "checks": [
            _ck("tonal_fraction", "gt", 0.0, "tonal (pitched), not noisy"),
            _ck("centroid", "range", [400, 3000], "midrange presence"),
            _ck("decay_s", "ge", 0.3, "short/medium decay (stab)"),
        ],
        "summary": "stab: tonal, centroid 400-3000 Hz, decay_s >= 0.3",
    },
    "pad": {
        "checks": [
            _ck("decay_s", "le", 0.25, "sustained envelope"),
            _ck("mel_mid", "ge", 0.3, "body/mid energy (mel_mid >= 0.3)"),
            _ck("mel_high", "gt", 0.03, "nonzero upper content (mel_high > 0.03)"),
        ],
        "summary": "pad: sustained (decay_s <= 0.25), mel_mid >= 0.3, mel_high > 0.03",
    },
    "riser": {
        "checks": [
            _ck("energy_growth", "ge", 1.3, "energy grows over the sweep"),
            _ck("centroid_growth", "ge", 0.7, "spectral centroid rises over the sweep"),
        ],
        "summary": "riser: energy_growth >= 1.3, centroid_growth >= 0.7",
    },
    "fx": {
        "checks": [
            _ck("energy_growth", "ge", 1.2, "energy grows over the sweep"),
            _ck("centroid_growth", "ge", 0.7, "spectral centroid rises over the sweep"),
        ],
        "summary": "fx: energy_growth >= 1.2, centroid_growth >= 0.7",
    },
}

REGISTER_WINDOWS = {
    "kick": [20, 400],
    "bass": [30, 500],
    "hat": [3000, 20000],
    "snare": [500, 12000],
    "rim": [500, 12000],
    "clap": [500, 12000],
    "lead": [150, 6000],
    "arp": [150, 6000],
    "stab": [150, 6000],
    "pad": [150, 8000],
    "riser": None,
    "fx": None,
}


def spectral_evolution(x, sr, win=0.05, hop=0.025, n_fft=2048):
    """Energy + spectral-centroid trend across the signal.

    Frames x with a Hanning window (0.05s win / 0.025s hop, like timbre.frame)
    and returns plain floats: energy_growth and centroid_growth, each a
    last-quarter / first-quarter ratio with an epsilon guard."""
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    n = int(win * sr)
    h = int(hop * sr)
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    pad = n - len(x) % h
    if pad < n:
        x = np.pad(x, (0, pad))
    idx = np.arange(n)[None, :] + np.arange(0, len(x) - n + 1, h)[:, None]
    frames = x[idx]
    winf = np.hanning(n)
    mag = np.abs(np.fft.rfft(frames * winf, n=n_fft, axis=1))
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    rms = np.sqrt((frames ** 2).mean(axis=1))
    power = mag ** 2
    tot = power.sum(axis=1) + 1e-12
    centroid = (freqs * power).sum(axis=1) / tot
    q = max(1, len(frames) // 4)
    first_rms = float(rms[:q].mean())
    last_rms = float(rms[-q:].mean())
    first_c = float(centroid[:q].mean())
    last_c = float(centroid[-q:].mean())
    return {
        "energy_growth": float(last_rms / (first_rms + 1e-9)),
        "centroid_growth": float(last_c / (first_c + 1e-9)),
    }


def _passes(op, measured, target):
    if op == "le":
        return measured <= target
    if op == "lt":
        return measured < target
    if op == "ge":
        return measured >= target
    if op == "gt":
        return measured > target
    if op == "range":
        return target[0] <= measured <= target[1]
    return False


def _derived(measurements, name):
    if name == "body":
        return float(measurements.get("mel_mid", 0.0)) + float(measurements.get("mel_high", 0.0))
    return float(measurements.get(name, 0.0))


def _failure_text(role, check):
    name = check["name"]
    m = check["measured"]
    op = check["op"]
    t = check["target"]
    if op == "le":
        return f"{name} {m} exceeds {role} max {t}"
    if op == "lt":
        return f"{name} {m} is not below {t}"
    if op == "ge":
        return f"{name} {m} is below {role} min {t}"
    if op == "gt":
        return f"{name} {m} is not above {t}"
    if op == "range":
        return f"{name} {m} is outside {role} range [{t[0]}, {t[1]}]"
    return f"{name} {m} fails {op} {t}"


def check_role(role, measurements):
    """Compare measurements against the role's checks; deterministic verdict."""
    role = normalize_role(role)
    measurements = measurements or {}
    targets = ROLE_TARGETS.get(role)
    if targets is None:
        return {"role": role, "verdict": "fail", "checks": [],
                "passed_count": 0, "total_count": 0,
                "failures": [f"unknown role '{role}'"],
                "summary": "0/0 checks passed"}
    checks = []
    for c in targets["checks"]:
        measured = _derived(measurements, c["name"])
        passed = _passes(c["op"], measured, c["target"])
        checks.append({"name": c["name"], "measured": round(measured, 4),
                       "target": c["target"], "op": c["op"],
                       "passed": bool(passed), "desc": c["desc"]})
    total = len(checks)
    passed = sum(1 for c in checks if c["passed"])
    failures = [_failure_text(role, c) for c in checks if not c["passed"]]
    verdict = "pass" if total and passed == total else "fail"
    return {"role": role, "verdict": verdict, "checks": checks,
            "passed_count": passed, "total_count": total,
            "failures": failures,
            "summary": f"{passed}/{total} checks passed"}


_UNIT = {"centroid": "Hz", "f0_hz": "Hz", "rolloff85": "Hz",
         "rolloff95": "Hz", "decay_s": "s", "attack_s": "s"}


def _fmt_target(target):
    if isinstance(target, (list, tuple)):
        return f"[{target[0]}, {target[1]}]"
    return f"{target}"


def _high_message(role, check):
    name = check["name"]
    m = check["measured"]
    unit = _UNIT.get(name, "")
    op = check["op"]
    t = check["target"]
    desc = check["desc"]
    if op == "le":
        return f"{name} {m}{unit} exceeds {role} max {t}{unit} ({desc})"
    if op == "ge":
        return f"{name} {m}{unit} is below {role} min {t}{unit} ({desc})"
    if op == "range":
        return f"{name} {m}{unit} is outside {role} range [{t[0]}, {t[1]}] ({desc})"
    if op == "lt":
        return f"{name} {m}{unit} is not below {t}{unit} ({desc})"
    if op == "gt":
        return f"{name} {m}{unit} is not above {t}{unit} ({desc})"
    return f"{name} {m}{unit} fails {op} {t}{unit} ({desc})"


def _near_edge(check):
    if check["op"] == "ge":
        return check["measured"] < check["target"] * 1.10
    if check["op"] == "le":
        return check["measured"] > check["target"] * 0.90
    return False


_LOW_RECS = {
    "kick": "try a parallel octave layer for extra body",
    "bass": "try a sub-sine layer an octave down for more weight",
    "hat": "try a tighter hi-hat layer with a shorter envelope for groove",
    "snare": "try layering a rim click under the snare body",
    "rim": "try a slightly longer decay for a fuller rim",
    "clap": "try a second clap layer slightly delayed for a bigger room",
    "lead": "try a parallel detuned saw layer for width",
    "arp": "try adding a delay with short feedback for movement",
    "stab": "try stacking a fifth above for a stab variant",
    "pad": "try adding a slow filter sweep for movement",
    "riser": "try adding a sub swell underneath the riser",
    "fx": "try a reverse cymbal into the effect for a bigger impact",
}
_LOW_DEFAULT = "try layering the same part with a slightly detuned copy for width"


def build_recommendations(role, measurements, role_check):
    """Priority-ranked recommendations: critical -> high -> medium -> low."""
    role = normalize_role(role)
    recs = []
    if measurements is None:
        recs.append({"priority": "critical",
                     "message": "input contains non-finite samples; cannot measure",
                     "check": None})
    else:
        peak = float(measurements.get("peak", 0.0))
        rms = float(measurements.get("rms", 0.0))
        centroid = float(measurements.get("centroid", 0.0))
        silent = peak < 1e-5 or rms < 1e-7
        if silent:
            recs.append({"priority": "critical",
                         "message": "input is silent (peak below -100 dBFS)",
                         "check": None})
        if peak >= 0.99:
            recs.append({"priority": "critical",
                         "message": "input is clipped (peak >= 0.99 of full scale); lower probe gain and re-render",
                         "check": None})
        if not silent:
            window = REGISTER_WINDOWS.get(role)
            if window is not None:
                lo, hi = window
                if centroid < lo * 0.33 or centroid > hi * 3.0:
                    recs.append({"priority": "critical",
                                 "message": f"centroid {centroid:.0f}Hz is severely outside the {role} register window {window}; wrong instrument/fundamental",
                                 "check": None})
    if role_check is not None and role_check.get("checks"):
        for c in role_check["checks"]:
            if not c["passed"]:
                recs.append({"priority": "high",
                             "message": _high_message(role, c),
                             "check": c["name"]})
    if measurements is not None and role_check is not None and role_check.get("checks"):
        for c in role_check["checks"]:
            if c["passed"] and _near_edge(c):
                recs.append({"priority": "medium",
                             "message": f"passes but {c['name']} is near the {_fmt_target(c['target'])} edge",
                             "check": c["name"]})
    if role_check is not None and role_check.get("verdict") == "pass":
        recs.append({"priority": "low",
                     "message": _LOW_RECS.get(role, _LOW_DEFAULT),
                     "check": None})
    return recs