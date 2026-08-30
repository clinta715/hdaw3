#!/usr/bin/env python3
"""Build a symbolic-feature analysed MIDI pattern library (midi pipeline stage 1-3).
Usage:
    python midi_analyze.py <folder> [--limit N] [--no-llm] [--sidecars] [--out PATH]
Mirrors lib_analyze.py (audio pipeline): collects *.mid;*.midi recursively,
stage 1 = mido symbolic features (midi_features.extract) + rule dsp_words,
stage 2+3 = one local-LLM completion (llm_stage.run_llm, Qwen2.5-3B GGUF, CPU)
emitting TAGS / CAPTION / PROSE; every missing/unparseable section falls back to
rule-built content.  Incremental cache: <folder>/.timbre_cache/<md5>.json.
With --sidecars writes <file>.mid.json next to each MIDI file (engine ingest
schema, mirrors <file>.timbre.json) and midi_index.json (never touches a
same-folder timbre_index.json).
"""
import argparse, copy, hashlib, json, os, re, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import midi_features as MFEAT
import llm_stage as LS

MIDI_EXTS = {".mid", ".midi"}
GGUF = os.path.join(HERE, "Qwen2.5-3B-Instruct-Q4_K_M.gguf")

SYSTEM = """You describe MIDI patterns for a DAW pattern browser.
Only describe what the evidence supports. Channels/programs are real instrument evidence; never invent other instruments or effects.
Return exactly three lines:
TAGS: <comma-separated short labels, 3-8>
CAPTION: <one 5-15 word summary>
PROSE: <2-4 sentences: dominant musical character (mode/groove), texture (polyphony/range/density), rhythm (grid deviation/syncopation/swing), a suggested use in music production (one clause)>
Do not hedge with "it might be"; do not list numbers."""

USER = """MEASURED SYMBOLIC EVIDENCE:
{evidence}

Write the description now."""


def wsl_to_win(p):
    m = re.match(r"^/mnt/([a-zA-Z])/(.*)$", p)
    if m:
        return f"{m.group(1).upper()}:\\" + m.group(2).replace("/", "\\")
    return p.replace("/", "\\")


def file_key(path):
    st = os.stat(path)
    return hashlib.md5(f"{path}:{st.st_size}:{int(st.st_mtime)}".encode()).hexdigest()


def tagify_dsp_words(dsp_words):
    """Rule fallback TAGS from dsp_words (3-8 short labels)."""
    tags = []
    for part in dsp_words.split(", "):
        part = part.strip()
        if not part:
            continue
        if len(part) <= 28:
            tags.append(part.lower())
        else:
            tags.extend(w.lower() for w in part.split()[:2])
    # de-dup, keep order, cap at 8
    seen, out = set(), []
    for t in tags:
        if t not in seen:
            seen.add(t)
            out.append(t)
    return out[:8]


def rule_caption(dsp_words, key_info):
    """Rule fallback CAPTION: 5-15 word summary built from descriptors."""
    w = [x for x in dsp_words.split(", ") if x]
    mode = key_info.get("key", "") or "unkeyed"
    # pick texture + register + groove words
    pick = []
    for cand in ("monophonic", "polyphonic/chordal", "dense texture",
                 "low/sub register", "mid register", "wide range",
                 "tight 16ths", "tight grid", "loose/swung feel",
                 "syncopated offbeat 16ths", "sparse"):
        if cand in w:
            pick.append(cand)
    head = "a " + " ".join(pick[:2]).replace(" register", "-register")
    if not pick:
        head = "a " + (w[0] if w else "MIDI")
    cap = f"{head} {mode} pattern"
    words = cap.split()
    if len(words) > 15:
        cap = " ".join(words[:15]).rstrip("-")
    if len(words) < 5:
        cap = f"a {mode} pattern in {pick[0] if pick else 'this'} feel".replace("  ", " ")
    return cap


def rule_prose(dsp_words, features, key_info):
    """Rule fallback PROSE: 2-4 sentences without numbers."""
    p = []
    mode = key_info.get("key", "") or "no clear key"
    groove = next((x for x in ("syncopated offbeat 16ths", "loose/swung feel",
                               "tight 16ths", "tight grid", "sparse")
                   if x in dsp_words), "")
    texture = next((x for x in ("monophonic", "polyphonic/chordal", "dense texture")
                    if x in dsp_words), "")
    register = next((x for x in ("low/sub register", "mid register", "wide range")
                     if x in dsp_words), "")
    density = next((x for x in ("sparse density", "moderate density", "dense density")
                    if x in dsp_words), "")
    character = next((x for x in ("repetitive/loopy", "rising contour", "falling contour", "varied")
                      if x in dsp_words), "")
    p.append(f"Dominant character: {mode} with a {groove} groove" if groove
             else f"Dominant character: {mode}.")
    tex = ", ".join(x for x in (texture, register, density) if x)
    p.append(f"Texture: {tex}." if tex else "Texture: simple.")
    sync = "light syncopation" if features["syncopation_fraction"] < 0.25 else "pronounced offbeat syncopation"
    grid = "tightly quantized" if features["grid_deviation"] < 0.05 else "loosely timed"
    p.append(f"Rhythm: {grid} with {sync}.")
    p.append(f"Suggested use: {character} pattern material for a DAW pattern browser.")
    return " ".join(p)


def build_evidence(dsp_words, key_info, inventory):
    ev = dsp_words
    ev += ", key=%s, tempo=%.1f, time_sig=%s" % (
        key_info["key"] or "none", key_info["tempo"], key_info["time_signature"])
    if inventory:
        ev += ", " + ", ".join(inventory)
    return ev


def run_midi_llm(evidence):
    """One LLM completion with the MIDI system/user prompts, reusing
    llm_stage.run_llm (same model handle; system prompt swapped temporarily,
    restored afterwards)."""
    old_s, old_u = LS.SYSTEM, LS.USER
    try:
        LS.SYSTEM = SYSTEM
        LS.USER = USER
        return LS.run_llm(evidence, GGUF, max_tokens=220, temperature=0.4)
    finally:
        LS.SYSTEM, LS.USER = old_s, old_u


def parse_llm(text, dsp_words, features, key_info):
    """Split TAGS/CAPTION/PROSE with per-section rule fallback.  Never raises."""
    tags, caption, prose = None, None, None
    try:
        tags = caption = prose = None
        # locate the three prefixes in line-ish text
        m_tags = re.search(r"TAGS\s*:\s*(.*)", text, re.S)
        m_cap = re.search(r"CAPTION\s*:\s*(.*)", text, re.S)
        m_prose = re.search(r"PROSE\s*:\s*(.*)", text, re.S)
        def grab(m):
            if not m:
                return None
            seg = m.group(1)
            # cut at the next prefix
            for nxt in ("TAGS:", "CAPTION:", "PROSE:"):
                i = seg.find(nxt)
                if i >= 0:
                    seg = seg[:i]
            return seg.strip().strip('"').strip()
        tags = grab(m_tags)
        caption = grab(m_cap)
        prose = grab(m_prose)
        if tags:
            tags = [t.strip() for t in tags.split(",") if t.strip()][:8]
            if not tags:
                tags = None
        if caption and not (5 <= len(caption.split()) <= 15):
            caption = None
    except Exception:
        tags = caption = prose = None
    if not tags:
        tags = tagify_dsp_words(dsp_words) or ["midi pattern"]
    if not caption:
        caption = rule_caption(dsp_words, key_info)
    if not prose:
        prose = rule_prose(dsp_words, features, key_info)
    return tags, caption, prose


SIDECAR_KEYS = ["name", "wsl_path", "win_path", "size", "mtime", "durationSeconds",
                "format", "dsp", "dsp_words", "captions", "tags", "prose", "_cached"]


def run_all(files, use_llm=True, sidecars=False):
    cache_dir = os.path.join(os.path.dirname(os.path.abspath(files[0])) or ".", ".timbre_cache")
    os.makedirs(cache_dir, exist_ok=True)
    out = {}
    t0 = time.perf_counter()
    for i, path in enumerate(files, 1):
        name = os.path.splitext(os.path.basename(path))[0]
        key = file_key(path)
        cpath = os.path.join(cache_dir, key + ".json")
        rec = None
        if os.path.exists(cpath):
            try:
                _c = json.load(open(cpath))
                if "error" not in _c:
                    rec = _c
            except Exception:
                rec = None
        if rec is not None:
            rec["_cached"] = True
            out[name] = rec
            if sidecars:
                side = {k: rec.get(k) for k in SIDECAR_KEYS}
                side["_cached"] = True
                if not os.path.exists(path + ".mid.json"):
                    json.dump(side, open(path + ".mid.json", "w"), indent=1)
            print(f"[{i}/{len(files)}] {name}: cached", flush=True)
            continue
        rec = {"name": name, "wsl_path": path, "win_path": wsl_to_win(path),
               "size": os.path.getsize(path),
               "mtime": int(os.path.getmtime(path)),
               "durationSeconds": None, "format": "midi",
               "dsp": {}, "dsp_words": "", "captions": [], "tags": [],
               "prose": "", "_cached": False, "_llm_error": False}
        try:
            feats = MFEAT.extract(path)
            key_info = MFEAT.key_tempo(path)
            inv = MFEAT.track_inventory(path)
            if feats is None:
                rec["error"] = "no note_on events or unparseable"
            else:
                rec["durationSeconds"] = round(MFEAT._parse(path).length, 3)
                rec["dsp"] = {k: round(v, 6) for k, v in feats.items()}
                rec["dsp_words"] = MFEAT.summarize(feats, key_info)
                if use_llm:
                    try:
                        ev = build_evidence(rec["dsp_words"], key_info, inv)
                        text = run_midi_llm(ev)
                        tags, caption, prose = parse_llm(text, rec["dsp_words"], feats, key_info)
                        rec["tags"] = [[t, 0.0] for t in tags]
                        rec["captions"] = [[caption, 0.0]]
                        rec["prose"] = prose
                        rec["_llm_error"] = not any(p in text for p in ("TAGS:", "CAPTION:", "PROSE:"))
                    except Exception:
                        rec["_llm_error"] = True
                        tags = tagify_dsp_words(rec["dsp_words"]) or ["midi pattern"]
                        rec["tags"] = [[t, 0.0] for t in tags]
                        rec["captions"] = [[rule_caption(rec["dsp_words"], key_info), 0.0]]
                        rec["prose"] = rule_prose(rec["dsp_words"], feats, key_info)
                else:
                    tags = tagify_dsp_words(rec["dsp_words"]) or ["midi pattern"]
                    rec["tags"] = [[t, 0.0] for t in tags]
                    rec["captions"] = [[rule_caption(rec["dsp_words"], key_info), 0.0]]
                    rec["prose"] = rule_prose(rec["dsp_words"], feats, key_info)
            if sidecars:
                side = {k: rec.get(k) for k in SIDECAR_KEYS}
                json.dump(side, open(path + ".mid.json", "w"), indent=1)
            json.dump(rec, open(cpath, "w"))
        except Exception:
            rec["error"] = traceback.format_exc()[-500:]
        out[name] = rec
        dt = time.perf_counter() - t0
        print(f"[{i}/{len(files)}] {name}: {rec.get('prose', rec.get('error','ERR'))[:70]} ({dt:.0f}s)", flush=True)
    return out


def collect(folder):
    found = []
    for r, _, fs in os.walk(folder):
        for f in fs:
            if os.path.splitext(f)[1].lower() in MIDI_EXTS:
                found.append(os.path.join(r, f))
    return sorted(found)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", help="folder to scan (or a single .mid file)")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--no-llm", action="store_true")
    ap.add_argument("--sidecars", action="store_true")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    files = [a.folder] if os.path.isfile(a.folder) else collect(a.folder)
    if a.limit:
        files = files[:a.limit]
    print(f"{len(files)} files", flush=True)
    try:
        recs = run_all(files, use_llm=not a.no_llm, sidecars=a.sidecars)
    finally:
        LS.close()
    base = a.folder if os.path.isdir(a.folder) else os.path.dirname(a.folder)
    out_path = a.out or os.path.join(base, "midi_index.json")
    json.dump(recs, open(out_path, "w"), indent=1)
    print(f"done -> {out_path} ({len(recs)} entries)")
    if a.sidecars:
        for f in files:
            if os.path.exists(f + ".mid.json"):
                print("example sidecar ->", f + ".mid.json")
                print(json.dumps(json.load(open(f + ".mid.json")), indent=1))
                break
