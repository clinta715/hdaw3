#!/usr/bin/env python3
"""G5 validation gates for the MIDI analysis pipeline (midi_features/midi_analyze).

    python midi_validate.py --determinism --families
    python midi_validate.py --teknoir
    python midi_validate.py --sidecars   # print one example sidecar JSON if present

--teknoir     : 46 teknoir/*.mid files, filename-encoded ground truth
                (teknoir_<genre>_<key>_<bpm>bpm_s<seed>_<datetime>.mid; F#
                appears as F_sharp in the spec, here as literal F#).
                extract() key must match filename tonic (phrygian files: tonic
                only) and mode for major/minor files; parsed tempo must match
                filename bpm (+-1).  Exit non-zero if gates fail.
--determinism : extract() run twice on the same file -> bit-identical dicts.
--families    : synthesize 3 MIDI families (4-on-floor drums on GM ch10 /
                A-minor 8th bassline / slow wide pads), extract features,
                kmeans2 (K=3) on the z-scored 20-dim matrix; report mean
                silhouette + purity.  Must separate (purity >= 0.8,
                silhouette > 0.2).
"""
import argparse, glob, json, os, re, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import midi_features as MFEAT

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
PCNAME = {n: i for i, n in enumerate(NOTE_NAMES)}
TEKNOIR_PAT = re.compile(r"teknoir_(\w+)_([A-G]#?)_(\w+)_(\d+)bpm_s(\w+)_(\d+)_(\d+)\.mid$")
TONIC_AIM = 40  # of 46


def teknoir_gate(folder="teknoir", verbose=True):
    files = sorted(glob.glob(os.path.join(folder, "*.mid")))
    rows = []
    tonic_ok = tempo_ok = mode_ok = 0
    tonic_total = tempo_total = mode_total = 0
    for f in files:
        m = TEKNOIR_PAT.search(os.path.basename(f))
        if not m:
            continue
        gt_tonic, gt_mode, gt_bpm = PCNAME[m.group(2)], m.group(3), int(m.group(4))
        ki = MFEAT.key_tempo(f)
        det_name, det_mode = (ki["key"].split() + [""])[:2] if ki["key"] else ("", "")
        det_tonic = PCNAME.get(det_name, -1)
        ok_t = (det_tonic == gt_tonic)
        ok_tempo = abs(ki["tempo"] - gt_bpm) <= 1
        ok_mode = (det_mode == gt_mode) if gt_mode in ("major", "minor") else True
        tonic_ok += ok_t; tonic_total += 1
        tempo_ok += ok_tempo; tempo_total += 1
        if gt_mode in ("major", "minor"):
            mode_ok += ok_mode; mode_total += 1
        rows.append((os.path.basename(f)[:46], "%s %s" % (m.group(2), gt_mode),
                     ki["key"] or "—", gt_bpm, round(ki["tempo"], 1),
                     "OK" if ok_t else "X", "OK" if ok_mode else "X",
                     "OK" if ok_tempo else "X"))
    if verbose:
        print("teknoir key/tempo acceptance (G5):")
        print(f"{'file':<48} {'gt':<10} {'detected':<10} {'gtBPM':>5} {'detBPM':>7}  T  M  TMP")
        for r in rows:
            print(f"{r[0]:<48} {r[1]:<10} {r[2]:<10} {r[3]:>5} {r[4]:>7}  {r[5]}  {r[6]}  {r[7]}")
        print()
        print(f"tonic hit rate : {tonic_ok}/{tonic_total}  (aim >= {TONIC_AIM}/{tonic_total})")
        print(f"mode  hit rate : {mode_ok}/{mode_total}  (major/minor files only)")
        print(f"tempo hit rate : {tempo_ok}/{tempo_total}  (aim 100%)")
    fail = tonic_ok < TONIC_AIM or tempo_ok != tempo_total or mode_ok != mode_total
    return fail, (tonic_ok, tonic_total, mode_ok, mode_total, tempo_ok, tempo_total)


def determinism_gate(file_path):
    a = json.dumps(MFEAT.extract(file_path), sort_keys=True, indent=1)
    b = json.dumps(MFEAT.extract(file_path), sort_keys=True, indent=1)
    ka = json.dumps(MFEAT.key_tempo(file_path), sort_keys=True)
    kb = json.dumps(MFEAT.key_tempo(file_path), sort_keys=True)
    ok = (a == b) and (ka == kb) and (a is not None)
    print("determinism:", "bit-identical" if ok else "MISMATCH")
    return not ok


def synthesize_families(tmpdir, per_family=8):
    """Write ~8 files per family with mido.  Returns list of (path, family)."""
    import mido
    tpq = 480
    bars = 16
    bar_ticks = tpq * 4
    total_ticks = bar_ticks * bars
    out = []

    def new_mid():
        m = mido.MidiFile(type=1, ticks_per_beat=tpq)
        t0 = mido.MidiTrack()
        t0.append(mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(130), time=0))
        t0.append(mido.MetaMessage("time_signature", numerator=4, denominator=4, time=0))
        t0.append(mido.MetaMessage("end_of_track", time=1))
        return m, t0

    # --- family 1: 4-on-floor drums (GM ch10 == mido ch9) ---
    for s in range(per_family):
        import random
        rnd = random.Random(1000 + s)
        m, t0 = new_mid()
        tr = mido.MidiTrack()
        evs = []
        for bar in range(bars):
            for b in range(4):
                kick = bar * bar_ticks + b * tpq
                evs.append((kick, "note_on", 9, 36, rnd.randint(105, 125)))
                evs.append((kick + 30, "note_off", 9, 36, 0))
                for e8 in range(2):
                    hat = kick + e8 * tpq // 2
                    evs.append((hat, "note_on", 9, 44, rnd.randint(60, 100)))
                    evs.append((hat + 20, "note_off", 9, 44, 0))
            for b in (1, 3):
                sn = bar * bar_ticks + b * tpq
                evs.append((sn, "note_on", 9, 40, rnd.randint(100, 118)))
                evs.append((sn + 60, "note_off", 9, 40, 0))
        evs.sort(key=lambda e: (e[0], e[1]))
        prev = 0
        for tick, kind, ch, note, vel in evs:
            tr.append(mido.Message(kind, channel=ch, note=note, velocity=vel, time=tick - prev))
            prev = tick
        tr.append(mido.MetaMessage("end_of_track", time=0))
        p = os.path.join(tmpdir, "fam_drums_%02d.mid" % s)
        m.tracks.append(tr)
        m.save(p)
        out.append((p, "drums"))

    # --- family 2: A-minor 8th-note bassline (channel 0, melodic) ---
    amin = [33, 36, 38, 40, 43, 45, 48]  # A1 C2 D2 E2 G2 A2 C3 (A aeolian)
    for s in range(per_family):
        import random
        rnd = random.Random(2000 + s)
        m, t0 = new_mid()
        tr = mido.MidiTrack()
        tr.append(mido.Message("program_change", program=33, time=0))
        evs = []
        cur = 1
        for bar in range(bars):
            for e8 in range(8):
                tick = bar * bar_ticks + e8 * tpq // 2
                if rnd.random() < 0.78:
                    opts = [cur - 1, cur, cur + 1, 4, 6]
                    cur = max(0, min(len(amin) - 1, rnd.choice(opts)))
                    note = amin[cur]
                    vel = rnd.randint(80, 104)
                    evs.append((tick, "note_on", 0, note, vel))
                    evs.append((tick + tpq // 2 - 10, "note_off", 0, note, 0))
        evs.sort(key=lambda e: (e[0], e[1]))
        prev = 0
        for tick, kind, ch, note, vel in evs:
            tr.append(mido.Message(kind, channel=ch, note=note, velocity=vel, time=tick - prev))
            prev = tick
        tr.append(mido.MetaMessage("end_of_track", time=0))
        p = os.path.join(tmpdir, "fam_bass_%02d.mid" % s)
        m.tracks.append(tr)
        m.save(p)
        out.append((p, "bass"))

    # --- family 3: slow wide chords pads (channel 4, program 89 pad warm) ---
    chords = [  # Am, F, C, G — 2 bars each, wide voicings
        [57, 60, 64, 69], [53, 57, 60, 65], [48, 55, 60, 64], [55, 59, 62, 67],
    ]
    for s in range(per_family):
        import random
        rnd = random.Random(3000 + s)
        m, t0 = new_mid()
        tr = mido.MidiTrack()
        tr.append(mido.Message("program_change", program=89, time=0))
        evs = []
        for ci in range(4):
            base = chords[ci]
            inv = [(n + 12 * k) for n in base for k in (0, 1)]
            voicing = rnd.sample(inv, 4)
            start = ci * 2 * bar_ticks
            dur = 2 * bar_ticks - 60
            for n in voicing:
                vel = rnd.randint(58, 78)
                evs.append((start, "note_on", 4, n, vel))
                evs.append((start + dur, "note_off", 4, n, 0))
        evs.sort(key=lambda e: (e[0], e[1]))
        prev = 0
        for tick, kind, ch, note, vel in evs:
            tr.append(mido.Message(kind, channel=ch, note=note, velocity=vel, time=tick - prev))
            prev = tick
        tr.append(mido.MetaMessage("end_of_track", time=0))
        p = os.path.join(tmpdir, "fam_pads_%02d.mid" % s)
        m.tracks.append(tr)
        m.save(p)
        out.append((p, "pads"))
    return out


def silhouette_mean(X, labels):
    import numpy as np
    n = len(X)
    if n < 3:
        return 0.0
    d = np.sqrt(((X[:, None, :] - X[None, :, :]) ** 2).sum(axis=2))
    s_vals = []
    for i in range(n):
        own = labels == labels[i]
        other = ~own
        if own.sum() > 1:
            a = d[i][own].sum() / (own.sum() - 1)
        else:
            a = 0.0
        b = np.inf
        for c in np.unique(labels[other]):
            m_ = other & (labels == c)
            if m_.sum() > 0:
                b = min(b, d[i][m_].mean())
        s_vals.append((b - a) / max(a, b) if np.isfinite(b) and max(a, b) > 0 else 0.0)
    return float(np.mean(s_vals))


def families_gate():
    import numpy as np
    from scipy.cluster.vq import kmeans2
    tmpdir = tempfile.mkdtemp(prefix="midi_families_")
    fams = synthesize_families(tmpdir)
    X, labels = [], []
    for path, fam in fams:
        feats = MFEAT.extract(path)
        if feats is None:
            print("FAMILIES ERROR: extract failed on", path)
            return True
        X.append([feats[k] for k in MFEAT.FEATURE_KEYS])
        labels.append(fam)
    X = np.array(X, dtype=np.float64)
    mu = X.mean(axis=0)
    sd = X.std(axis=0)
    sd[sd == 0] = 1.0
    Xz = (X - mu) / sd  # z-score, zero-std columns left at 0
    centroids, lab = kmeans2(Xz, k=3, minit='points', seed=42)
    sil = silhouette_mean(Xz, lab)
    purity = 0.0
    fam_list = ["drums", "bass", "pads"]
    for c in range(3):
        members = [labels[i] for i in range(len(labels)) if lab[i] == c]
        if members:
            dom = max(set(members), key=members.count)
            purity += members.count(dom)
    purity /= len(labels)
    print("--- families gate ---")
    print("synthesized %d files (drums/bass/pads = %d/%d/%d) in %s" % (
        len(fams), sum(1 for _, f in fams if f == "drums"),
        sum(1 for _, f in fams if f == "bass"), sum(1 for _, f in fams if f == "pads"), tmpdir))
    from collections import Counter
    per = {c: Counter(labels[i] for i in range(len(labels)) if lab[i] == c) for c in range(3)}
    for c in range(3):
        print("  cluster %d: %s" % (c, dict(per[c])))
    print("  mean silhouette: %.3f (need > 0.2)" % sil)
    print("  purity: %.3f (need >= 0.8)" % purity)
    ok = purity >= 0.8 and sil > 0.2
    print("families separation:", "PASS" if ok else "FAIL")
    return not ok


def example_sidecar(folder="teknoir"):
    for f in sorted(glob.glob(os.path.join(folder, "*.mid.json")))[:1]:
        print("example sidecar ->", f)
        print(json.dumps(json.load(open(f)), indent=1))
        return False
    print("no sidecars found (run midi_analyze.py --sidecars first)")
    return False


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--teknoir", action="store_true")
    ap.add_argument("--determinism", action="store_true")
    ap.add_argument("--families", action="store_true")
    ap.add_argument("--sidecars", action="store_true")
    ap.add_argument("--folder", default="teknoir")
    ap.add_argument("--file", default="teknoir/teknoir_techno_C_minor_130bpm_s1337_20260707_145830.mid")
    a = ap.parse_args()
    failed = False
    if a.teknoir:
        fail, stats = teknoir_gate(a.folder)
        failed |= fail
    if a.determinism:
        failed |= determinism_gate(a.file)
    if a.families:
        failed |= families_gate()
    if a.sidecars:
        failed |= example_sidecar(a.folder)
    if not (a.teknoir or a.determinism or a.families or a.sidecars):
        print("no gate selected; run with --teknoir/--determinism/--families/--sidecars")
    sys.exit(1 if failed else 0)
