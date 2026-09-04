#!/usr/bin/env python3
"""pytest suite for the synth probe analyzer (timbre-lib/analyze_probe.py).

Deterministic: fixed-seed numpy noise, no CLAP/LLM model loads, no network.
Only numpy/scipy/librosa/stdlib are used."""
import json
import os
import struct
import subprocess
import sys
import wave

import numpy as np
import pytest
from scipy import signal as sps

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import analyze_probe as AP
import role_targets as RT
import timbre as TB

SR = 48000
SEED = 12345
REPO_ROOT = os.path.dirname(HERE)


def _rng():
    return np.random.default_rng(SEED)


def _norm(x, peak=0.95):
    x = np.asarray(x, dtype=np.float64)
    m = float(np.max(np.abs(x)))
    if m > 0:
        x = x * (peak / m)
    return x


# ── synthetic signal factory (48 kHz, numpy only) ────────────────────────────

def sine(f, dur, decay=None):
    t = np.arange(int(SR * dur)) / SR
    x = np.sin(2 * np.pi * f * t)
    if decay is not None:
        x = x * np.exp(-t / decay)
    return x


def noise(dur):
    return _rng().standard_normal(int(SR * dur))


def bp_noise(lo, hi, dur, decay=None):
    x = noise(dur)
    b, a = sps.butter(4, [lo / (SR / 2), hi / (SR / 2)], btype="band")
    x = sps.lfilter(b, a, x)
    if decay is not None:
        t = np.arange(len(x)) / SR
        x = x * np.exp(-t / decay)
    return x


def hp_noise(cut, dur, decay=None):
    x = noise(dur)
    b, a = sps.butter(4, cut / (SR / 2), btype="high")
    x = sps.lfilter(b, a, x)
    if decay is not None:
        t = np.arange(len(x)) / SR
        x = x * np.exp(-t / decay)
    return x


def ramp_noise(dur):
    x = noise(dur)
    t = np.arange(len(x)) / SR
    return x * (t / dur) ** 2


def _clap_signal():
    out = np.zeros(int(SR * 0.4))
    for tk in (0.02, 0.13, 0.24):
        seg = bp_noise(1000, 8000, 0.09, decay=0.03)
        i0 = int(tk * SR)
        i1 = min(len(out), i0 + len(seg))
        out[i0:i1] += seg[: i1 - i0]
    return out


def _pad_signal():
    return (0.5 * sine(220, 2.0) + 1.0 * sine(440, 2.0)
            + 1.0 * sine(660, 2.0) + 0.35 * sine(5000, 2.0))


def _signals():
    return {
        "kick": {"pass": sine(55, 0.4, decay=0.09), "fail": sine(2000, 0.6)},
        "bass": {"pass": sine(110, 1.0) + 0.3 * sine(220, 1.0), "fail": sine(3000, 1.0)},
        "hat": {"pass": hp_noise(6000, 0.12, decay=0.03), "fail": sine(110, 0.3, decay=0.05)},
        "snare": {"pass": bp_noise(1000, 8000, 0.15, decay=0.04), "fail": sine(110, 0.3, decay=0.09)},
        "rim": {"pass": bp_noise(800, 6000, 0.12, decay=0.04), "fail": sine(110, 0.3, decay=0.09)},
        "clap": {"pass": _clap_signal(), "fail": sine(110, 0.3, decay=0.09)},
        "lead": {"pass": sine(440, 1.0), "fail": noise(1.0)},
        "arp": {"pass": sine(880, 0.4, decay=0.08), "fail": bp_noise(500, 3000, 0.5)},
        "stab": {"pass": sine(440, 0.25, decay=0.06), "fail": noise(0.4)},
        "pad": {"pass": _pad_signal(), "fail": sine(55, 0.4, decay=0.09)},
        "riser": {"pass": ramp_noise(2.0), "fail": noise(1.5)},
        "fx": {"pass": ramp_noise(1.0), "fail": noise(1.0)},
    }


# ── WAV fixtures ─────────────────────────────────────────────────────────────

def write_wav(path, sr, samples):
    """Write 16-bit PCM mono/stereo WAV via stdlib wave (clips to [-1, 1])."""
    samples = np.asarray(samples, dtype=np.float64)
    if samples.ndim == 1:
        samples = samples.reshape(-1, 1)
    channels = samples.shape[1]
    data = np.clip(samples, -1.0, 1.0) * 32767.0
    data = data.astype(np.int16)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(data.tobytes())


def write_wav_f32(path, sr, samples):
    """Write an IEEE float32 WAV (format tag 3) manually so NaN/Inf survive
    the round trip. The stdlib wave module only writes WAVE_FORMAT_PCM, so the
    audio-format tag at offset 20 is patched to IEEE_FLOAT=3 afterwards."""
    samples = np.asarray(samples, dtype=np.float32)
    if samples.ndim == 1:
        samples = samples.reshape(-1, 1)
    channels = samples.shape[1]
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(4)
        wf.setframerate(sr)
        wf.writeframes(samples.astype("<f4").tobytes())
    with open(str(path), "r+b") as f:
        f.seek(20)
        f.write(struct.pack("<H", 3))


def fixture(tmp_path, name, x):
    p = tmp_path / name
    write_wav(p, SR, x)
    return str(p)


# ── tests ────────────────────────────────────────────────────────────────────

@pytest.mark.parametrize("role", list(RT.SUPPORTED_ROLES))
def test_roles_pass_and_fail(tmp_path, role):
    sigs = _signals()
    pass_wav = fixture(tmp_path, f"{role}_pass.wav", sigs[role]["pass"])
    fail_wav = fixture(tmp_path, f"{role}_fail.wav", sigs[role]["fail"])
    r1 = AP.analyze_probe(pass_wav, role, use_clap=False, use_llm=False)
    r2 = AP.analyze_probe(fail_wav, role, use_clap=False, use_llm=False)
    assert r1["error"] is None, r1
    assert r2["error"] is None, r2
    assert r1["roleCheck"] is not None and r2["roleCheck"] is not None
    assert r1["roleCheck"]["verdict"] == "pass", (role, r1["roleCheck"], r1["measurements"])
    assert r2["roleCheck"]["verdict"] == "fail", (role, r2["roleCheck"], r2["measurements"])


def test_silent_input(tmp_path):
    wav = fixture(tmp_path, "silent.wav", np.zeros(int(SR * 0.5)))
    r = AP.analyze_probe(wav, "kick", use_clap=False, use_llm=False)
    assert r["error"] is None
    crit = [rec for rec in r["recommendations"] if rec["priority"] == "critical"]
    assert any("silent" in rec["message"] for rec in crit)
    if r["roleCheck"] is not None:
        assert r["roleCheck"]["verdict"] == "fail"


def test_clipped_input(tmp_path):
    x = np.sign(sine(440, 0.5))
    wav = fixture(tmp_path, "clipped.wav", x)
    r = AP.analyze_probe(wav, "kick", use_clap=False, use_llm=False)
    assert r["error"] is None
    crit = [rec for rec in r["recommendations"] if rec["priority"] == "critical"]
    assert any("clipped" in rec["message"] for rec in crit)


def test_nonfinite_input(tmp_path):
    x = np.zeros(int(SR * 0.1))
    x[100] = np.nan
    x[500] = np.inf
    x[2000] = -np.inf
    wav = tmp_path / "nonfinite.wav"
    write_wav_f32(wav, SR, x)
    r = AP.analyze_probe(str(wav), "kick", use_clap=False, use_llm=False)
    assert r["error"] is None
    assert r["measurements"] is None
    assert r["roleCheck"] is None
    crit = [rec for rec in r["recommendations"] if rec["priority"] == "critical"]
    assert any("non-finite" in rec["message"] for rec in crit)


def test_malformed_input(tmp_path):
    p = tmp_path / "bad.wav"
    p.write_text("this is not an audio file", encoding="utf-8")
    r = AP.analyze_probe(str(p), "kick", use_clap=False, use_llm=False)
    assert r["error"], r
    assert r["measurements"] is None
    assert r["roleCheck"] is None
    for key in ("input", "role", "measurements", "roleCheck", "description",
                "recommendations", "warnings", "error"):
        assert key in r


def test_missing_input(tmp_path):
    p = tmp_path / "missing.wav"
    r = AP.analyze_probe(str(p), "kick", use_clap=False, use_llm=False)
    assert r["error"]
    assert r["measurements"] is None
    assert r["input"]["path"] == str(p)


def test_mono_and_stereo(tmp_path):
    x = _signals()["kick"]["pass"]
    mono = fixture(tmp_path, "mono.wav", x)
    stereo = fixture(tmp_path, "stereo.wav", np.stack([x, x], axis=1))
    r1 = AP.analyze_probe(mono, "kick", use_clap=False, use_llm=False)
    r2 = AP.analyze_probe(stereo, "kick", use_clap=False, use_llm=False)
    assert r1["error"] is None and r2["error"] is None
    assert r1["roleCheck"]["verdict"] == r2["roleCheck"]["verdict"]


def test_dsp_only_fallback(tmp_path):
    wav = fixture(tmp_path, "kick.wav", _signals()["kick"]["pass"])
    r = AP.analyze_probe(wav, "kick", use_clap=False, use_llm=False)
    assert r["error"] is None
    assert r["warnings"], "expected explicit stage-unavailable warnings"
    joined = " ".join(r["warnings"]).lower()
    assert ("clap" in joined and "unavailable" in joined) \
        or ("llm" in joined and "unavailable" in joined)
    assert r["description"] == TB.summarize(r["measurements"])
    assert set(r.keys()) == {"input", "role", "measurements", "roleCheck",
                             "description", "recommendations", "warnings", "error"}
    assert r["roleCheck"]["verdict"] == "pass"


def test_stable_json(tmp_path):
    wav = fixture(tmp_path, "kick.wav", _signals()["kick"]["pass"])
    r1 = AP.analyze_probe(wav, "kick", use_clap=False, use_llm=False)
    r2 = AP.analyze_probe(wav, "kick", use_clap=False, use_llm=False)
    assert json.dumps(r1, sort_keys=True) == json.dumps(r2, sort_keys=True)
    assert r1 == r2


def test_unknown_role(tmp_path):
    wav = fixture(tmp_path, "kick.wav", _signals()["kick"]["pass"])
    r = AP.analyze_probe(wav, "bogus", use_clap=False, use_llm=False)
    assert r["error"]
    assert "bogus" in r["error"] and "role" in r["error"]
    assert r["role"] == "bogus"
    assert r["measurements"] is None
    assert r["recommendations"] == []


def test_report_schema(tmp_path):
    wav = fixture(tmp_path, "kick.wav", _signals()["kick"]["pass"])
    r = AP.analyze_probe(wav, "kick", use_clap=False, use_llm=False)
    assert r["error"] is None
    assert set(r.keys()) == {"input", "role", "measurements", "roleCheck",
                             "description", "recommendations", "warnings", "error"}
    assert set(r["input"].keys()) == {"path", "name", "plugin"}
    assert r["input"]["path"] == wav
    assert r["input"]["name"] == "kick"
    assert r["input"]["plugin"] is None
    assert r["measurements"] is not None
    assert r["roleCheck"]["verdict"] == "pass"
    assert isinstance(r["roleCheck"]["checks"], list)
    assert "summary" in r["roleCheck"]
    for rec in r["recommendations"]:
        assert "priority" in rec and "message" in rec
        assert rec["priority"] in ("critical", "high", "medium", "low")


def test_cli_smoke(tmp_path):
    wav = fixture(tmp_path, "kick.wav", _signals()["kick"]["pass"])
    proc = subprocess.run(
        [sys.executable, "timbre-lib/analyze_probe.py", wav, "--role", "kick",
         "--no-clap", "--no-llm"],
        cwd=REPO_ROOT, capture_output=True, text=True, timeout=120, check=False)
    assert proc.returncode == 0, proc.stderr
    report = json.loads(proc.stdout)
    for key in ("input", "role", "measurements", "roleCheck", "description",
                "recommendations", "warnings", "error"):
        assert key in report

    missing = str(tmp_path / "nope.wav")
    proc2 = subprocess.run(
        [sys.executable, "timbre-lib/analyze_probe.py", missing, "--role", "kick",
         "--no-clap", "--no-llm"],
        cwd=REPO_ROOT, capture_output=True, text=True, timeout=120, check=False)
    assert proc2.returncode == 1, proc2.stdout
    err_report = json.loads(proc2.stdout)
    assert err_report["error"]