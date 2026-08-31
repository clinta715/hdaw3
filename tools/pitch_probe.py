#!/usr/bin/env python
"""Estimate natural pitch of one-shot samples -> MIDI rootNote hints.
Uses autocorrelation on the strongest low-band region + spectral peak."""

import math
import sys
import wave

import numpy as np


def load_wav(path):
    w = wave.open(path, "rb")
    sr = w.getframerate()
    ch = w.getnchannels()
    sw = w.getsampwidth()
    n = w.getnframes()
    raw = w.readframes(n)
    w.close()
    if sw == 2:
        a = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        a = (
            b[:, 0].astype(np.int32)
            | (b[:, 1].astype(np.int32) << 8)
            | (b[:, 2].astype(np.int32) << 16)
        )
        a = np.where(a & 0x800000, a - 0x1000000, a) / float(1 << 23)
    elif sw == 4:
        a = np.frombuffer(raw, dtype="<i4").astype(np.float64) / float(1 << 31)
    else:
        raise ValueError(f"sampwidth {sw}")
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return sr, a


def midi_of_freq(f):
    return 69 + 12 * math.log2(f / 440.0) if f > 0 else None


def estimate(path):
    sr, a = load_wav(path)
    if len(a) < sr // 10:
        return None
    # use the loudest 0.2 s window
    win = int(0.2 * sr)
    hops = max(1, (len(a) - win) // win)
    best, bi = -1, 0
    for i in range(hops):
        seg = a[i * win : (i + 1) * win]
        e = float(np.sqrt((seg**2).mean()))
        if e > best:
            best, bi = e, i
    seg = a[bi * win : (bi + 1) * win] * np.hanning(win)
    spec = np.abs(np.fft.rfft(seg))
    freqs = np.fft.rfftfreq(win, 1 / sr)
    # spectral peak (dominant partial)
    lo = np.searchsorted(freqs, 25)
    hi = np.searchsorted(freqs, min(8000, sr / 2 - 1))
    k = lo + int(np.argmax(spec[lo:hi]))
    f_peak = freqs[k]
    # autocorrelation for fundamental (better for basses)
    seg2 = a[bi * win : (bi + 1) * win]
    seg2 = seg2 - seg2.mean()
    ac = np.correlate(seg2, seg2, "full")[win - 1 :]
    ac /= ac[0] + 1e-12
    lo2, hi2 = int(sr / 600), int(sr / 45)  # 45..600 Hz search
    if hi2 < len(ac):
        lag = lo2 + int(np.argmax(ac[lo2:hi2]))
        f_fund = sr / lag if lag > 0 else 0
    else:
        f_fund = 0
    f_fund = 0.0
    # pick: if fund in 30..400 use it, else peak if in 30..2000
    f = f_fund if 30 <= f_fund <= 400 else (f_peak if 30 <= f_peak <= 2000 else f_fund)
    m = midi_of_freq(f)
    mp = midi_of_freq(f_peak)
    try:
        rms = round(float(np.sqrt((a**2).mean())), 4)
    except Exception:  # noqa: BLE001 - empty/invalid buffer
        rms = 0.0
    return {
        "dur": len(a) / sr,
        "f_fund": round(f_fund, 1),
        "f_peak": round(f_peak, 1),
        "midi_fund": round(m, 2) if m is not None else None,
        "midi_peak": round(mp, 2) if mp is not None else None,
        "rms": rms,
    }


if __name__ == "__main__":
    for p in sys.argv[1:]:
        try:
            r = estimate(p)
        except Exception as e:  # noqa: BLE001 - diagnostic utility, report and continue
            print(f"{p}: ERROR {e}")
            continue
        if r is None:
            print(f"{p}: too short")
            continue
        name = p.split("\\")[-1].split("/")[-1]
        print(
            f"{name:55s} dur={r['dur']:6.2f} fund={r['f_fund']:7.1f}Hz (MIDI {r['midi_fund']}) peak={r['f_peak']:7.1f}Hz (MIDI {r['midi_peak']}) rms={r['rms']}"
        )
