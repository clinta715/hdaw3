"""Symbolic MIDI feature extraction — pure numpy + mido, deterministic, no RNG.
Mirrors timbre.py (audio descriptor stage) for the MIDI pipeline.

extract()  -> the 20 fixed MIDI feature keys (numeric axis for clustering)
key_tempo() -> parsed key/tempo/time-signature info (evidence layer)
track_inventory() -> per-track role-guess lines (evidence layer)
summarize() -> rule-based dsp_words (audio timbre.summarize analogue)
"""
import numpy as np

DRUM_CHANNEL = 9  # mido channel (0-based) == GM channel 10 (1-based percussion map)

# Krummhansl-Kessler major/minor tonal profiles (PC 0 = tonic).
_KK_MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
_KK_MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
_NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
_MAJ_SCALE = {0, 2, 4, 5, 7, 9, 11}
_MIN_SCALE = {0, 2, 3, 5, 7, 8, 10}
_DOMINANT_ANCHOR = 0.9  # score weight for candidate key's dominant (5th) emphasis

FEATURE_KEYS = [
    "duration_beats", "note_count", "note_density", "polyphony_mean", "polyphony_max",
    "pitch_min", "pitch_span", "pitch_centroid", "pitch_class_entropy", "scale_fit",
    "key_confidence", "interval_mean", "interval_entropy", "contour_up_ratio",
    "note_repetition_rate", "ioi_mean", "grid_deviation", "syncopation_fraction",
    "velocity_mean", "velocity_std",
]

# Tiny inline GM program map (mido 0-based programs 0..40), fallback "program N".
_GM_NAMES = [
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
    "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavi", "Celesta", "Glockenspiel",
    "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ",
    "Accordion", "Harmonica", "Tango Accordion", "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)",
    "Electric Guitar (jazz)", "Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar",
    "Distortion Guitar", "Guitar Harmonics", "Acoustic Bass", "Electric Bass (finger)",
    "Electric Bass (pick)", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1",
    "Synth Bass 2", "Violin",
]


def _program_name(program):
    if program is None:
        return "-"
    if 0 <= program < len(_GM_NAMES):
        return _GM_NAMES[program]
    return "program %d" % program


def _parse(mid_path):
    """Return mido.MidiFile or None (unparseable/tolerated)."""
    try:
        import mido
        return mido.MidiFile(mid_path)
    except Exception:
        return None


def _all_note_events(mid):
    """List of (abs_tick, kind, channel, pitch, velocity) where kind in {'on','off'}.
    note_on with velocity 0 counts as off.  Tracks are independent; end_of_track
    timestamps are ignored for note events (only maxTick uses them)."""
    events = []
    for tr in mid.tracks:
        t = 0
        for msg in tr:
            t += msg.time
            if msg.type == "note_on":
                if msg.velocity > 0:
                    events.append((t, "on", msg.channel, msg.note, msg.velocity))
                else:
                    events.append((t, "off", msg.channel, msg.note, 0))
            elif msg.type == "note_off":
                events.append((t, "off", msg.channel, msg.note, 0))
    events.sort(key=lambda e: e[0])
    return events


def _max_tick(mid):
    mx = 0
    for tr in mid.tracks:
        t = 0
        for msg in tr:
            t += msg.time
            mx = max(mx, t)
    return mx


def _polyphony_run(events):
    """Simultaneous sounding notes at note-on times.
    Off events at a tick are applied before ons (release-before-restrike).
    Returns (list_of_onset_poly_counts, max_simultaneous)."""
    active = set()
    onset_counts = []
    mx = 0
    i = 0
    n = len(events)
    while i < n:
        tick = events[i][0]
        j = i
        ons_at_tick = []
        while j < n and events[j][0] == tick:
            if events[j][1] == "off":
                active.discard((events[j][2], events[j][3]))
            else:
                ons_at_tick.append(events[j])
            j += 1
        if ons_at_tick:
            for e in ons_at_tick:
                active.add((e[2], e[3]))
            cur = len(active)
            for _ in range(len(ons_at_tick)):
                onset_counts.append(cur)
            mx = max(mx, cur)
        i = j
    return onset_counts, mx


def _detect_key(melodic_pcs, hist):
    """Scale-fit + dominant-anchor key detection over the 24 (tonic, mode) candidates.

    Score(t, mode) = fraction of melodic note-ons on the candidate scale
                     + _DOMINANT_ANCHOR * fraction on the candidate's dominant.
    The dominant-anchor term separates relative/dominant-saturating keys (the
    teknoir bassline corpus avoids tonics and anchors on 5ths), which plain
    Krumhansl-Kessler correlation misassigns.
    Returns ((tonic, mode), kk_confidence) where kk_confidence is the KK
    best-minus-second correlation margin over the same 24 candidates."""
    if len(melodic_pcs) == 0 or hist.sum() == 0:
        return (0, "major"), 0.0
    hn = hist / hist.sum()
    kk_corr = {}
    fit = {}
    for t in range(12):
        for mode, prof in (("major", _KK_MAJ), ("minor", _KK_MIN)):
            p = np.roll(prof, t)
            p = p / p.sum()
            kk_corr[(t, mode)] = float(np.corrcoef(hn, p)[0, 1])
            scale = _MAJ_SCALE if mode == "major" else _MIN_SCALE
            fit[(t, mode)] = float(sum(hist[(s + t) % 12] for s in scale) / hist.sum())
    # KK correlation margin (spec: best minus 2nd best, normalized histogram)
    kk_sorted = sorted(kk_corr.values(), reverse=True)
    kk_conf = (kk_sorted[0] - kk_sorted[1]) if len(kk_sorted) > 1 else 0.0
    best = max(fit, key=lambda k: fit[k] + _DOMINANT_ANCHOR * (hist[(k[0] + 7) % 12] / hist.sum()))
    return best, kk_conf


def extract(mid_path, tempo_hint=None):
    """Extract the 20 symbolic feature keys.  Returns None for unparseable files
    or files with zero note_on events.  All values are python floats, always
    finite; melodic-stream features fall back to 0.0 when no melodic notes exist;
    duration_beats>0 is required for density/ioi, else 0.0.
    tempo_hint is accepted for API symmetry with the audio pipeline but has no
    effect on the 20 keys (they are tick-relative, not tempo-relative)."""
    mid = _parse(mid_path)
    if mid is None or mid.ticks_per_beat <= 0:
        return None
    tpq = float(mid.ticks_per_beat)
    events = _all_note_events(mid)
    ons = [e for e in events if e[1] == "on"]
    if not ons:
        return None

    max_tick = float(_max_tick(mid))
    duration_beats = max_tick / tpq if max_tick > 0 else 0.0

    # ---- global note statistics (all tracks incl. drums) ----
    pitches = np.array([e[3] for e in ons], dtype=np.float64)
    vels = np.array([e[4] for e in ons], dtype=np.float64)
    note_count = float(len(ons))
    note_density = note_count / duration_beats if duration_beats > 0 else 0.0
    pitch_min = float(pitches.min())
    pitch_span = float(pitches.max() - pitches.min())
    pitch_centroid = float(pitches.mean())

    # ---- polyphony ----
    onset_counts, poly_max = _polyphony_run(events)
    poly_mean = float(np.mean(onset_counts)) if onset_counts else 0.0

    # ---- melodic stream (exclude GM channel 10 == mido channel 9) ----
    mel = sorted([e for e in ons if e[2] != DRUM_CHANNEL], key=lambda e: (e[0], e[3]))
    mel_pcs = np.array([e[3] % 12 for e in mel], dtype=np.int64)
    mel_pitches = np.array([e[3] for e in mel], dtype=np.float64)

    # key detection + scale_fit (melodic only)
    hist = np.zeros(12)
    for pc in mel_pcs:
        hist[int(pc)] += 1
    (key_tonic, key_mode), kk_conf = _detect_key(mel_pcs, hist)
    key_scale = _MAJ_SCALE if key_mode == "major" else _MIN_SCALE
    key_scale_pcs = {(s + key_tonic) % 12 for s in key_scale}
    scale_fit = float(np.mean([1.0 if int(pc) in key_scale_pcs else 0.0 for pc in mel_pcs])) if len(mel_pcs) else 0.0

    # pitch-class entropy (melodic only), normalized by log2(12)
    if len(mel_pcs):
        pc_h = np.bincount(mel_pcs, minlength=12).astype(np.float64)
        pc_h = pc_h[pc_h > 0]
        pc_p = pc_h / pc_h.sum()
        pc_entropy = float(-(pc_p * np.log2(pc_p)).sum() / np.log2(12.0))
    else:
        pc_entropy = 0.0

    # ---- melodic interval features (12-15) ----
    if len(mel_pitches) >= 2:
        diff = np.abs(np.diff(mel_pitches))  # abs semitone diffs, ties->lowest pitch first
        interval_mean = float(diff.mean())
        iv = np.minimum(diff.astype(np.int64), 24)
        iv_h = np.bincount(iv, minlength=25).astype(np.float64)
        iv_p = iv_h[iv_h > 0] / iv_h.sum()
        interval_entropy = float(-(iv_p * np.log2(iv_p)).sum() / np.log2(25.0))
        contour_up = float((diff > 0).mean())
        repetition = float((diff == 0).mean())
    else:
        interval_mean = interval_entropy = contour_up = repetition = 0.0

    # ---- onset rhythm features (all channels, unique onset ticks) ----
    onset_ticks = sorted({e[0] for e in ons})
    if len(onset_ticks) >= 2:
        ioi_mean = float(np.mean(np.diff(np.array(onset_ticks, dtype=np.float64))) / tpq)
    else:
        ioi_mean = 0.0
    gridstep = tpq / 4.0  # 16th note
    devs = []
    sync = 0
    for ot in onset_ticks:
        rem = ot % gridstep
        devs.append(min(rem, gridstep - rem))
        if round(ot / gridstep) % 4 in (1, 3):
            sync += 1
    grid_deviation = float(np.mean(devs) / tpq) if devs else 0.0
    syncopation_fraction = sync / len(onset_ticks) if onset_ticks else 0.0

    # ---- velocity ----
    velocity_mean = float(vels.mean())
    velocity_std = float(vels.std()) if len(vels) > 1 else 0.0

    values = [
        duration_beats, note_count, note_density, poly_mean, float(poly_max),
        pitch_min, pitch_span, pitch_centroid, pc_entropy, scale_fit,
        kk_conf, interval_mean, interval_entropy, contour_up, repetition,
        ioi_mean, grid_deviation, syncopation_fraction, velocity_mean, velocity_std,
    ]
    return dict(zip(FEATURE_KEYS, [float(v) for v in values]))


def _first_meta(mid, kind):
    for tr in mid.tracks:
        for msg in tr:
            if msg.type == kind:
                return msg
    return None


def key_tempo(mid_path):
    """Parsed key/tempo/time-signature info (evidence layer).
    key: "<PC> <major|minor>" from extract()'s detector ("" when no melodic
    notes); tempo: first set_tempo converted to BPM (default 120.0);
    time_signature "n/d" (default "4/4"); tpq; tracks; has_drums (additive)."""
    mid = _parse(mid_path)
    if mid is None:
        return {"key": "", "key_confidence": 0.0, "tempo": 120.0,
                "time_signature": "4/4", "tpq": 0, "tracks": 0, "has_drums": False}
    tempo_meta = _first_meta(mid, "set_tempo")
    tempo = 60_000_000.0 / tempo_meta.tempo if tempo_meta is not None else 120.0
    ts = _first_meta(mid, "time_signature")
    ts_str = "%d/%d" % (ts.numerator, ts.denominator) if ts is not None else "4/4"
    ons = [e for e in _all_note_events(mid) if e[1] == "on"]
    mel_pcs = [e[3] % 12 for e in ons if e[2] != DRUM_CHANNEL]
    hist = np.zeros(12)
    for pc in mel_pcs:
        hist[int(pc)] += 1
    (tonic, mode), kk_conf = _detect_key(mel_pcs, hist)
    key = ("%s %s" % (_NOTE_NAMES[tonic], mode)) if mel_pcs else ""
    return {"key": key, "key_confidence": kk_conf, "tempo": float(tempo),
            "time_signature": ts_str, "tpq": int(mid.ticks_per_beat),
            "tracks": len(mid.tracks), "has_drums": any(e[2] == DRUM_CHANNEL for e in ons)}


def track_inventory(mid_path):
    """Per-track lines: track index, channel, GM program, note count, median
    pitch, heuristic role (channel 10 -> drums; median pitch <60 bass, <84 lead,
    else chords).  Only tracks with note_on events are listed."""
    mid = _parse(mid_path)
    if mid is None:
        return []
    lines = []
    for ti, tr in enumerate(mid.tracks):
        t = 0
        notes = []
        prog = None
        ch_counts = {}
        for msg in tr:
            t += msg.time
            if msg.type == "note_on" and msg.velocity > 0:
                notes.append((msg.channel, msg.note))
                ch_counts[msg.channel] = ch_counts.get(msg.channel, 0) + 1
            elif msg.type == "program_change" and prog is None:
                prog = msg.program
        if not notes:
            continue
        ch = max(ch_counts, key=ch_counts.get)
        med = float(np.median([n[1] for n in notes]))
        if ch == DRUM_CHANNEL:
            role = "drums"
        elif med < 60:
            role = "bass"
        elif med < 84:
            role = "lead"
        else:
            role = "chords"
        lines.append(
            "track%d: ch%d, program %s (%s), %d notes, median pitch %d, role guess %s"
            % (ti + 1, ch, prog if prog is not None else "-", _program_name(prog),
               len(notes), int(round(med)), role))
    return lines


def summarize(features, key_info):
    """Rule-based dsp_words (timbre.summarize analogue)."""
    w = []
    key = key_info.get("key", "")
    if key.endswith("major"):
        w.append("major-key")
    elif key.endswith("minor"):
        w.append("minor-key")
    else:
        w.append("no clear key")

    density = features["note_density"]
    sync = features["syncopation_fraction"]
    grid = features["grid_deviation"]
    ioi = features["ioi_mean"]
    if density < 1.0:
        w.append("sparse")
    elif sync >= 0.3:
        w.append("syncopated offbeat 16ths")
    elif grid >= 0.06:
        w.append("loose/swung feel")
    elif ioi <= 0.45:
        w.append("tight 16ths")
    else:
        w.append("tight grid")

    pm = features["polyphony_mean"]
    if pm < 1.3:
        w.append("monophonic")
    elif pm < 3.0:
        w.append("polyphonic/chordal")
    else:
        w.append("dense texture")

    if features["pitch_span"] >= 48:
        w.append("wide range")
    elif features["pitch_centroid"] < 48:
        w.append("low/sub register")
    else:
        w.append("mid register")

    if density < 2.0:
        w.append("sparse density")
    elif density < 6.0:
        w.append("moderate density")
    else:
        w.append("dense density")

    rep = features["note_repetition_rate"]
    up = features["contour_up_ratio"]
    if rep >= 0.25:
        w.append("repetitive/loopy")
    elif up > 0.6:
        w.append("rising contour")
    elif up < 0.4:
        w.append("falling contour")
    else:
        w.append("varied")

    if key_info.get("has_drums"):
        w.append("with drum track")
    return ", ".join(w)
