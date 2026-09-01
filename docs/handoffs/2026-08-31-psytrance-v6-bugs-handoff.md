# Handoff — Psytrance v6 composition bugs uncovered (2026-08-31)

Session: long psytrance composition run with multiple engine restarts, render hangs, and
export-silence scares. Built v6–v9 at 145 BPM / F harmonic minor, 12 tracks, heavy
filter chains, 2-bar variation. Every bug below was hit live during the session.
Companion updates in `docs/psytrance-composition-guide.md` §0.5 (7 sound-design
principles) + §5c (`growl_bass` / `psyarp` docs).

---

## 0. What shipped before the bugs

- §0.5 Sound-design canon (7 principles: random FX welcome, filter-discipline
  `filter→filter→waveshaper`, rhythmic ear-candy, kick+bass root, arp-leads are
  programmatic, non-arp = monotonic, L/R phasing & cutoff variation)
- §5c `growl_bass` (ClipType 0..3, Drive 0–40, built-in filter, ADSR, sidechain)
  and `psyarp` (OscShape 0..2, PatternShape 0..2, OctaveRange, BarsPerMotif,
  FilterCutoff/Res/SweepBars, Delay, Reverb, Phaser) docs
- `growl_bass` / `psyarp` verified live via `list_fx_params` (clip types, pattern
  shapes, filter ranges) — internal waveshaper is `growl_bass` ClipType=2/Drive
- v6 final render `renders/psytrance_v6_final.wav` (59.6 MB, 3:27, peak ~0.15,
  Break/MainA ≈0.18) with tempo-matched loops before the break fixes below

---

## 1. (FIXED) `clipId` capture returns text, not JSON — hardcoded 0 sent notes to nowhere

**Status:** FIXED in-session. **File(s):** any `add_midi_clip` caller.

**Repro:** `add_midi_clip {trackId,start,length}` returns
`{content:[{text:"clipId=N"}]}` — plain text, not `{"clipId":N}` JSON.
Hardcoding `clipId: 0` or `JSON.parse(text)` throws / silently targets a
nonexistent clip. `add_notes {clipId:0, notes:[...]}` returns OK but adds to
nothing → export silent / RMS 0 after ~1 s.

**Evidence:** `growl_test` at D1(26) first rendered SILENT (RMS 0.0000);
second attempt capturing `clipId=1` from raw text → `added:16` and
RMS 0.1560 HAS AUDIO.

**Fix:** helper `xid(data)` — try `/clipId\s*=\s*(\d+)/` first, then
`/"clipId"\s*:\s*(\d+)/`, then fallback `/\d+/`. Never hardcode `clipId: 0`.

```js
const xid = d => {
  const t = d?.content?.[0]?.text||"";
  let m = t.match(/clipId\s*=\s*(\d+)/); if (m) return Number(m[1]);
  m = t.match(/"clipId"\s*:\s*(\d+)/); if (m) return Number(m[1]);
  m = t.match(/\d+/); return m?Number(m[1]):-1;
};
```

**Rule:** any new clip-creating call must use the `xid` / `extractId` pattern;
do not assume JSON.

---

## 2. (FIXED) `set_automation_enabled` / `set_automation_points` use `lane`, not `laneName`

**Status:** FIXED. **Tools:** `hdaw_set_automation_enabled`, `hdaw_set_automation_points`.

**Repro:** Passing `{trackId, laneName:"Volume"}` is an **unknown property** —
the engine silently ignores it, returns OK, but the lane stays
`enabled:false, pointCount:2` (defaults) and the full render's RMS arc is
flat (~0.07 throughout) — a fake breakdown.

Verified: with `laneName` → `pointCount` stayed 2 and RMS flat;
with `lane:"Volume"` (string) → `enabled:true, pointCount:6` and RMS
Intro 0.088 / Build 0.169 / MainA 0.174 / Break 0.001 / MainB 0.211 —
real breakdown.

**Fix:** use `{trackId, lane:"Volume", enabled:true}` and
`{trackId, lane:"Volume", mode:"replace", points:[{time,value},…]}`.
For custom lanes: `{trackId, laneName:"FilterSweep", paramID:200}` on
`add_automation_lane`, then subsequent calls use `lane:"FilterSweep"`.

**Rule:** `lane` is `number|string`; `laneName` is only valid on
`add_automation_lane`. After creation, address lanes by `lane` string.

---

## 3. (FIXED) `add_track_with_fx` enum excludes `psy_fm`

**Status:** FIXED. **Tool:** `hdaw_add_track_with_fx`.

**Repro:** `add_track_with_fx {fxType:"psy_fm"}` → `tool_error`:
`fxType: value not in enum` (allowed: `eq, compressor, reverb, delay,
chorus, flanger, phaser, filter, sampler, fm_synth, growl_bass, psyarp`).

**Fix:** two-step: `add_track({name})`, then `add_fx({trackId, fxType:"psy_fm"})`,
then `psy_fm_load_preset`. Use `add_track_with_fx` only for the integer-enum
types; `psy_fm` always goes via `add_fx`.

---

## 4. (FIXED) Audio-clip timestretch + `looping:false` produces a drone / wrong sync

**Status:** FIXED. **Tools:** `hdaw_add_audio_clip`, `hdaw_set_clip_source_bpm`,
`hdaw_tempo_match_clip`.

**Repro:** `add_audio_clip {length:500}` with a ~4-bar loop (e.g. Antinomy
bass 138 BPM ≈13.9 s ≈30 beats native) then
`set_clip_source_bpm` + `tempo_match_clip` **stretches the entire source file
to fill 500 beats** — a 4-bar loop becomes a 475-beat drone. `looping:false`
means it plays once and stops. Result: user hears "bassline out of sync" —
the stretched audio is a drone, not a rhythm.

Evidence: with `length:500, looping:false` durations came back 475.86 (bass
138→145) and 517.24 (TerraTech 150→145) — the full file stretched to fill
the clip.

**Fix for loops:** use native length + `looping:true`:

```js
await add_audio_clip({trackId, sourceFile, start:0, length: nativeBeats}); // 32 beats
await set_clip_source_bpm({clipId, bpm: srcBpm});  // 138 or 150
await tempo_match_clip({clipId});                  // stretches to ~30.5 / 33.1
await set_clip({clipId, looping:true});            // repeats across arrangement
```

Native beats verified: Hypnoticum bass ≈32 beats, TerraTech arps/synths/squelchs ≈32 beats
(after match: 30.46–33.10). For rhythmic elements, **prefer MIDI-triggered
instruments** (`growl_bass` offbeat, `psyarp` arp, sampler one-shots) — they
never drift because they re-trigger on the grid. Reserve `add_audio_clip`
loops for sustained textures (pads/drones) where phase drift is inaudible.

---

## 5. (FIXED) WAV header has a `JUNK` chunk — data does not start at byte 44

**Status:** FIXED (analysis helper). **Files:** every `export_audio` WAV.

**Repro:** `with open(path).seek(44)` reads inside `JUNK` (52 bytes) before
`fmt`, so the first "audio" bytes are junk, not samples — RMS reads as
spurious peaks, middle of file appears silent. Naive `f.seek(44)` gave
frame 4 `R=7169536 (0.85)` from junk.

**Fix:** parse the RIFF chunks to find `data`:

```py
pos=12; ds=0
while pos < len(hdr)-8:
    cid=hdr[pos:pos+4]; csz=struct.unpack_from('<I',hdr,pos+4)[0]
    if cid==b'data': ds=pos+8; break
    pos+=8+csz+(csz%2)
```

Actual layout: `JUNK` (52) at 12, `fmt` (16) at 72, `data` at 96 → data at
byte 104, not 44. Every WAV analyzer in the session was updated to this.

---

## 6. (FIXED) Export is async and cancels in-flight work — next export kills the previous

**Status:** FIXED (CAS guard + queue param). **Tool:** `hdaw_export_audio`.

**Repro:** Looping `export_audio` without waiting for the file to stabilize
(lesson 12) aborts the first export with no error — two "export started"
acks, one missing/truncated file. Analogous to `new_project` races.

**Workaround used:** poll file size against `expected = seconds*48000*2*3 + hdr`
and wait for `size >= expected-1000` before the next `export_audio`.

**Follow-up:** needs a serialize guard in `ExportManager` or a queued-export
API; meanwhile keep the poll-before-next-export discipline.

**Follow-up fixed in v0.25.2:** ExportManager compare_exchange_strong + queue:true poll (120s timeout) instead of immediate reject; cancel_export still aborts.

---

## 7. (FIXED) `remove_track` shifts `trackId`s — saved `trackId`s go stale mid-script

**Status:** FIXED in-session. **Tool:** `hdaw_remove_track`.

**Repro:** Removing track 1 (`Synth` default) shifts `Vocals` 2→1, `Kick` 3→2,
etc. Media callers holding old `trackId`s then `add_fx`/`set_track` on the
wrong tracks (e.g. kick params landed on hat). Verified in `list_tracks`
before/after.

**Fix:** either never remove defaults (simpler) or re-`list_tracks` after
every `remove_track`. Final v7 leaves defaults in place (`Kick` on 0,
`BassGrowl` on 1, `ClHat` on 2) and adds new tracks at 3+ without removals.

---

## 8. (FIXED) Mix problems even after fixing sync

**a) Parts too quiet / "only hihat" for stretches:** `looping:false` on all
audio loops meant they played once as drones then silence; after ~30 s only
the looping MIDI hihat remained. Fixed together with #4.

**b) Frequency separation:** everything occupied the mid 400–3000 Hz band.
Fix: per-role filter discipline (guide §0.5): `filter` LP on kick (120 Hz),
`growl_bass` internal filter + external filter pass, `psyarp` HP+LPF carve,
`filter` HP on pads/drones, reverb wet as needed. Restored growl metallic
edge and made layers distinguishable.

**c) Levels collapsed:** first canary with no automation flat at ~0.07; with
`laneName` bug also flat; volume automation with correct `lane` recovered the
arc. Canary at master 0.4–0.5 then `auto_gain_to_target` / `set_master_gain`
for headroom.

---

## 9. (FIXED) `hdaw_save_project` param is `filePath`, not `path`

**Status:** FIXED. **Tool:** `hdaw_save_project`.

**Repro:** `save_project {path:"renders/foo.hdaw"}` → `path: unknown property`
or silent no-file. `renders/*.hdaw` missing (`NOT FOUND`) after a save call.
Same confusion exists for `load_project` (`filePath`).

**Fix:** always `save_project {filePath:"D:\\pdf\\roo projects\\hdaw3\\renders\\..."`
(absolute path required). Saved files: `psytrance_v7.hdaw` (498 KB),
`psytrance_v6_final.hdaw` (59.6 MB WAV companion).

---

## 10. (FIXED) Pitch too high — `psyarp` / `psych` roots + `OctaveRange` too hot

**Status:** FIXED in v7 (needs verification listen).

**Repro:** Earlier v6 used `OctaveRange=2..3` from F4 (53) with SuperSaw →
149–155 Hz spectral peak, competing in the vocal range.

**Fix:** bring arps into psytrance register: `OctaveRange=1`, trigger
chords from C2(36)+C3(48) (ArpMain) and Ab2(39)+C3(48) (ArpAlt), filter
cutoff 2000–2500 Hz, lower `psy_fm` acidLead stab root to A#2(47) monotonic.
`growl_bass` fundamentals set low (38=G1, 26=D1/C2).

**Future: automated tuning feedback loop.** User requested a
`timbre-lib`-style pipeline: solo-render each track (2 s), measure spectral
centroid / peak-bin / band energy, compare to per-role targets
(kick/sub <120 Hz, bass 60–250 Hz, arp/lead 400–3000 Hz carved, hat >6 kHz),
auto-adjust `rootNote` / `filter` cutoff / `OctaveRange` and re-render until
each role lands in its band. Prototype harness exists (`renders/v7_test.wav`
analysis already runs per-track RMS); extend it with FFT centroid + band
energy and a clamp-to-target loop — do not reuse timestretch-based pitch
retuning inside that loop without verification (lesson 23: one unclamped path
poisons the signal; timestretch can re-introduce pitch artifacts even when
the `rootNote` is correct).

---

## 11. (KNOWN) Misc MCP quirks hit

- `list_libraries` / `pool_list` large payloads exceed output guard →
  `fullResultPath` file; read it, don't re-call.
- `search_library` needs `libraryId` singular, not `libraryIds`.
- `set_automation_points` `mode:"replace"` vs `"append"` — always
  `"replace"` when rebuilding an arrangement.
- Engine restart drops to `New Project` (3 tracks) — unsaved work lost;
  save via absolute `filePath` after every phase.

---

## Deliverables at handoff

- **Guide:** `docs/psytrance-composition-guide.md` §0.5 (7 principles) + §5c
  (`growl_bass`/`psyarp` param tables, recipes, combining table) — 658 lines
- **Memory:** `MEMORY.md` #psytrance Sound-Design Canon
- **Renders (current engine, v0.25.1, post tempo-fix):**
  - `renders/psytrance_v6_final.wav` 59.6 MB (3:27, 145 BPM, F harm minor)
  - `renders/psytrance_v7.hdaw` / `renders/v7_test.wav` (before arrangement)
  - `renders/growl_test.wav` (growl_bass solo verification), etc.
- **Absolute-save contract:** `D:\\pdf\\roo projects\\hdaw3\\renders\\*.hdaw`

## Where to continue

1. Listen to `psytrance_v7.hdaw` / `v6_final.wav` (MCP reload `mcp-launch.bat`
   now generator-aware → `build\\HDAW_headless.exe` with v0.25.1 clamp fix).
2. Finish the tuning feedback loop (item 10) — automated centroid/band check.
3. The arrangement template (`Beats=500, sections Intro48/Build48/MainA128/
   Break64/MainB160/Outro52`) and 2-bar automation generators live in the
   `place_patterns` / MCP `mcpScript` cells above; reuse them.
4. `timbre-lib` TSV palette `timbre-lib/psy_sample_selection.tsv` remains
   valid for one-shot roles.
