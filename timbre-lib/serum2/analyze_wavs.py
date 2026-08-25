
"""analyze_wavs.py — per-file audio stats: silence check, RMS, peak, centroid."""
import json, os, sys
import wave, struct, math

def analyze(path):
    w = wave.open(path, "rb")
    nch = w.getnchannels()
    rate = w.getframerate()
    n = w.getnframes()
    raw = w.readframes(n)
    w.close()
    samples = struct.unpack("<%dh" % (len(raw)//2), raw)
    ch = [samples[i::nch] for i in range(nch)]
    def stats(x):
        peak = max(abs(v) for v in x)
        rms = math.sqrt(sum(v*v for v in x)/len(x)) if x else 0
        zcr = sum(1 for a, b in zip(x, x[1:]) if (a >= 0) != (b >= 0)) / len(x)
        # spectral centroid via FFT on first 8192 samples
        import cmath
        N = 4096
        seg = x[:N]
        if len(seg) < N:
            seg = seg + [0] * (N - len(seg))
        # crude DFT estimates — use real Cooley-ish via numpy if available
        try:
            import numpy as np
            sp = np.abs(np.fft.rfft(np.array(seg, dtype=np.float64) / 32768.0))
            freqs = np.fft.rfftfreq(N, 1.0 / rate)
            if sp.sum() > 0:
                centroid = float((freqs * sp).sum() / sp.sum())
            else:
                centroid = 0.0
        except Exception:
            centroid = None
        return {"peak": round(peak, 1), "rms": round(rms, 1), "zcr": round(zcr, 4),
                "centroid_hz": round(centroid, 1) if centroid is not None else None}
    return {"channels": nch, "rate": rate, "samples": len(samples), "stats_per_ch": [stats(c) for c in ch]}

base = sys.argv[1]
out = {}
for t in range(4):
    p = os.path.join(base, f"track{t}.wav")
    out[f"track{t}"] = analyze(p)
print(json.dumps(out, indent=1))
with open(os.path.join(base, "audio_report.json"), "w") as f:
    json.dump(out, f, indent=1)
