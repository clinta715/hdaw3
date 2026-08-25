"""Timbre descriptor extraction — numpy/scipy only, portable to C++.
Deterministic stage of the CLAP/PANNs + descriptors + LLM pipeline."""
import numpy as np
from scipy import signal as sps

def hz_to_mel(hz): return 2595 * np.log10(1 + hz/700)
def mel_to_hz(mel): return 700 * (10**(mel/2595) - 1)

def mel_filterbank(n_fft, sr, n_mels=64, fmin=30, fmax=None):
    fmax = fmax or sr/2
    mels = np.linspace(hz_to_mel(fmin), hz_to_mel(fmax), n_mels+2)
    freq = np.fft.rfftfreq(n_fft, 1/sr)
    m = hz_to_mel(freq)
    banks = np.zeros((n_mels, len(freq)))
    for i in range(n_mels):
        lo, c, hi = mels[i], mels[i+1], mels[i+2]
        banks[i] = np.clip(np.minimum((m-lo)/(c-lo), (hi-m)/(hi-c)), 0, None)
    return banks

def frame(x, sr, win=0.05, hop=0.025):
    n = int(win*sr); h = int(hop*sr)
    pad = n - len(x) % h
    if pad < n: x = np.pad(x, (0, pad))
    idx = np.arange(n)[None, :] + np.arange(0, len(x)-n+1, h)[:, None]
    return x[idx]

def autocorr_f0(frame_sig, sr, lo=40, hi=1200):
    x = frame_sig - frame_sig.mean()
    n = len(x)
    r = np.correlate(x, x, mode='full')[n-1:]
    if r[0] <= 1e-12: return 0.0, 0.0, 0.0
    r /= r[0]
    lo_lag, hi_lag = max(1, int(sr/hi)), int(sr/lo)
    seg = r[lo_lag:hi_lag+1]
    if len(seg) == 0: return 0.0, 0.0, 0.0
    lag = lo_lag + int(np.argmax(seg))
    ratio = r[2*lag]/r[lag] if 2*lag < len(r) else 0.0   # comb test: 1.0 = truly periodic
    return sr/lag, float(seg.max()), float(max(ratio, 0.0))

def extract(x, sr, win=0.05, hop=0.025, n_fft=2048, n_mels=64):
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1: x = x.mean(axis=1)
    x0 = x - x.mean()  # DC-corrected (for envelope/onsets)
    bhp, ahp = sps.butter(2, 25/(sr/2), btype='high'); xf = sps.lfilter(bhp, ahp, x0)  # sub-25Hz drift
    frames = frame(xf, sr, win, hop)
    winf = np.hanning(frames.shape[1])
    X = np.fft.rfft(frames * winf, n=n_fft, axis=1)
    mag = np.abs(X); freqs = np.fft.rfftfreq(n_fft, 1/sr)
    power = mag**2; tot = power.sum(axis=1, keepdims=True) + 1e-12

    centroid = (freqs*power).sum(axis=1)/tot[:,0]
    bandwidth = np.sqrt(((freqs[None,:]-centroid[:,None])**2 * power).sum(axis=1)/tot[:,0])
    cum = np.cumsum(power, axis=1)/tot
    rolloff85 = freqs[np.argmax(cum >= 0.85, axis=1)]
    rolloff95 = freqs[np.argmax(cum >= 0.95, axis=1)]

    banks = mel_filterbank(n_fft, sr, n_mels)
    mel_e = banks @ power.T                      # (n_mels, n_frames)
    mel_flat = np.exp(np.mean(np.log(mel_e+1e-12), axis=0))/(mel_e.mean(axis=0)+1e-12)
    mel_hz = mel_to_hz((np.arange(n_mels)+0.5)*(hz_to_mel(sr/2)-hz_to_mel(30))/n_mels + hz_to_mel(30))
    band = mel_e.mean(axis=1); band = band/(band.sum()+1e-12)   # mean-of-energies: sparse hits still count
    low  = band[mel_hz < 250].sum()
    mid  = band[(mel_hz>=250)&(mel_hz<4000)].sum()
    high = band[mel_hz>=4000].sum()
    crest = power.max(axis=1)/(power.mean(axis=1)+1e-12)

    env = np.abs(x0); env = np.convolve(env, np.ones(int(sr*0.01))/int(sr*0.01), mode='same')
    idx_above = np.nonzero(env >= env.max()*0.1)[0]
    attack = (idx_above[0]/sr) if len(idx_above) else 0.0
    pki = int(np.argmax(env)); pkt = pki/sr
    tail = env[pki:]; rel = float((tail <= env.max()*0.4).mean())   # fraction below 40% peak

    f0s, cls, prs, pks = [], [], [], []
    for ri, fr in enumerate(frames[::4]):
        f, c, p = autocorr_f0(fr, sr)
        f0s.append(f); cls.append(c); prs.append(p)
        pk_best = 0.0
        if f > 0:
            for kk in range(1, 6):                                    # harmonic peakiness
                b = int(round(kk*f/sr*n_fft))
                if 0 < b < mag.shape[1]-7:
                    pk = mag[ri*4, b]; med = float(np.median(mag[ri*4, max(0,b-6):b+7]))
                    pk_best = max(pk_best, pk/(med+1e-12))
        pks.append(pk_best)
    f0s, cls, prs, pks = np.array(f0s), np.array(cls), np.array(prs), np.array(pks)
    strong = (cls > 0.7) & (prs > 0.6) & (pks > 4.0)
    f0m = np.log2(np.maximum(f0s,1e-9))*12
    votes, steps = np.unique(np.round(f0m[strong]), return_counts=True)
    best = steps.max() if len(steps) else 0
    tonal = float(best >= 0.4*len(f0s))
    f0_med = 2**(votes[np.argmax(steps)]/12) if tonal else 0.0
    if best >= 0.15*len(f0s):
        g = np.abs(np.diff(f0m[strong][:200]))
        sweep = float(np.median(g)) if len(g) else 0.0
    else:
        sweep = 9.0

    zcr = (np.signbit(x0[1:]) != np.signbit(x0[:-1])).sum()/len(x0)
    rms = np.sqrt(np.mean(x0**2)); peak = np.abs(x0).max()
    kern = np.ones(int(100/(freqs[1]-freqs[0]))); kern /= kern.sum()
    sm = np.apply_along_axis(lambda r: np.convolve(r, kern, mode='same'), 1, mag)
    d = np.abs(np.diff(sm, axis=1)); spec_irr = (d.sum(axis=1)/(sm.sum(axis=1)+1e-9))
    med = lambda a: float(np.median(a))
    return dict(duration=len(x0)/sr, rms=float(rms), peak=float(peak),
        crest_dB=float(20*np.log10(peak/(rms+1e-12)+1e-12)), zcr=float(zcr),
        centroid=med(centroid), bandwidth=med(bandwidth),
        rolloff85=med(rolloff85), rolloff95=med(rolloff95),
        flatness=med(mel_flat), spectral_crest=med(crest),
        spec_irregularity=med(spec_irr),
        mel_low=float(low), mel_mid=float(mid), mel_high=float(high),
        attack_s=float(attack), decay_s=float(rel),
        f0_hz=float(f0_med), tonal_fraction=float(tonal), f0_sweep=float(sweep))

def summarize(d):
    w = []
    if d['f0_sweep'] < 8.5 and d['f0_sweep'] > 0.25: w.append('pitch-swept (slide/glide)')
    elif d['tonal_fraction'] > 0.0:
        w.append('tonal')
        if d['f0_hz'] > 0: w.append(f'f0≈{d["f0_hz"]:.0f}Hz')
    else: w.append('noisy/polyphonic')
    c = d['centroid']
    w.append('dark' if c < 500 else ('warm/mid' if c < 2000 else ('bright' if c < 5000 else 'very bright/edgy')))
    if d['mel_high'] > 0.12: w.append('airy top')
    if d['spec_irregularity'] > 0.12 and d['flatness'] > 0.02: w.append('rough/gritty')
    if d['flatness'] < 0.06: w.append('spectrally thin')
    elif d['flatness'] > 0.2: w.append('full-bodied')
    if d['attack_s'] > 0.25: w.append('slow/soft attack')
    elif d['attack_s'] > 0.05: w.append('gradual attack')
    elif d['decay_s'] < 0.45: w.append('percussive/plucky onset')
    else: w.append('instant onset')
    if d['decay_s'] > 0.5: w.append('short decay (energy dies quickly)')
    elif d['decay_s'] > 0.2: w.append('medium decay')
    else: w.append('sustained')
    return ', '.join(w)
