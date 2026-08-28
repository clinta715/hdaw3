# Jungle / Drum & Bass Composition Guide (HDAW MCP)

How to compose jungle and drum-and-bass tracks with the HDAW engine + MCP.
Distilled from the three 2026-08-29 sessions (core amen, ragga, atmospheric)
and the psytrance workflow that preceded them. Every number below is measured
from rendered audio. Reference work sits in the prompt: the article
"Jungle music" (Wikipedia) for style canon; the session handoff
`docs/handoffs/2026-08-29-jungle-dnb-composition-session.md` for the raw
lessons, feature gaps, and bugs; recipes are also encoded in
`tests/unit/engine/psytrance_composition_stress_test.cpp` patterns (the
`PsytranceComposition.*` tests are the closest canonical engine-side
templates — a dedicated `JungleComposition.*` suite is a planned follow-up).

---

## 0. Style canon (what we are making)

Jungle = early-90s UK rave + Jamaican sound-system culture; d&b is its
faster/industrial offshoot. The sound, from the Wikipedia research:

- **Rapid breakbeats, heavily syncopated percussive loops** — above all the
  **Amen break** (and other funk/jazz breaks), chopped and re-sequenced.
- **Deep basslines** — "bass quake pressure"; ragga/dub b-lines; sub + reese.
- **Samples + synth effects**: reggae/dancehall vocals and MC toasts, reggae
  organ, dub echo, industrial stabs. "Rhythm as melody", multi-tiered rhythms.
- Subgenres (pick one per track):
  - **Ragga jungle** — reggae organ skank + toasts + 808s; "Original Nuttah".
  - **Jump-up** — bass-line-led, playful, big wobble; "Super Sharp Shooter".
  - **Ambient / intelligent** (LTJ Bukem) — atmosphere and melody over relaxed
    or half-time breaks; washed, spacious.
  - **Darkcore/hardstep** — dark samples, industrial stabs.
- Tempos actually used here: 175 BPM (amen loops at 175) and 136 BPM (reggae
  stems) inside one track is fine — quantize each source by its own bar length.

## 1. The workflow (7 steps, verified)

1. **Index + analyze packs** — timbre-lib drivers (`analyze_dnb.py`,
   `analyze_ragga.py`, `analyze_psytrance.py`): stride-sample → DSP→CLAP→LLM
   sidecars. mp3 is fully supported by the sampler/export/library pipeline.
2. **Register via MCP** (`add_library` while an engine runs — script registry
   writes get clobbered) and `scan_library`.
3. **RE-SCAN after analysis completes** — tags only appear on a scan that
   runs after the sidecars landed. Confirm via `search_library` returning
   `tags`. If pack files carry FUTURE mtimes, forward-date the sidecars first
   (`os.utime`) or the incremental scan never applies them.
4. **Cluster** (`cluster_library` over the pack ids) → role clusters
   (breaks/drums, dark rumble bass, bass shots, tops, hats/shakers; ragga:
   vocal/phrase, riddim, drum, 808). `related_samples` is index-scoped —
   un-scanned libraries are invisible to it.
5. **Build the kit** — one sampler track per role; rootNotes at the sample's
   natural pitch (see §3). Cluster-informed picks save audition time.
6. **Score + produce** — section grammar (§4), FX + filter envelopes (§5).
7. **Render + verify** — canary-master (§6) then the measure loop (§7).

## 2. The kit (verified palettes)

Per-role starting points and the sample sources used (all registered HDAW libs):

| Role | Source packs | Notes |
|---|---|---|
| Break loops | `100 Amen Breaks By Veak - Volume 2` (100×@175) | 1-bar loops, trigger per bar, dur 4 |
| Breaks/loops | `Breaks`, `Drums - Full Drum Loops`, `Full_Drums`, `drum_loops` | alt textures |
| Sub bass | `Bass Shots` (BW_* named with key), `Basses` (Vixage loops named key+BPM) | pick by key |
| Bas loop | Vixage `DNBTypeBeat F 174`, `ClassicVix Dmin 150` | melodic loops |
| Rumble | `Rumble Loops` (Decode 2, F-key) | drop swells |
| Drums | `lingoturbo Mini Drum Pack` (kicks/snares/hats/rim/clap), `Drums` (CO_IA) | one-shots |
| Tops/shakers | `Drums - Top Loops`, `Drums - Shaker Loops` (BW_124) | texture layers |
| Reggae organ | `Rasta Vibrations 6` ORGAN stem (136 Gmn) | stab skank, see §5 |
| Riddim stems | `Rasta Vibrations 6` (organ/bass/drums/perc/lead/clav per riddim) | breakdown bed |
| Dub riddims | `Full Dub Riddims Big Reggae Sample Pack` (1+2) | whole-track beds |
| Chants/808s | `Ragga Stashkit Jayf` (CHANTS, PHRASES, 808, drum folders) | vocals, stabs |
| Pads | Antinomy Atmosphere textures (D/C/F keyed wav) | ambient variant |

**Faders** (starting points; three tracks all landed here): breaks .74–.85 ·
sub .62–.75 · bass .68–.75 · hats .62–.78 · snare .64–.85 · pads .45–.72 ·
dub bed .55 · chants .75 · 808 .70. Keep ruthless headroom: our mixes all
had true peaks 1.2–1.4 at faders 1.0 → the canary step (§6) is mandatory.

## 3. rootNotes (the #1 mix mistake otherwise)

RootNote = the MIDI pitch that plays the sample at NATURAL pitch. Verified map
for cross-pack reuse:

- kick 36 (pitchless), hats/breaks/one-shots 60
- F-key bass loop/shot 41 (F2); D-key bass 38 (D2); C shot 48 (C3)
- D pad 50 (D3); Antinomy F-pad 53
- G-minor Rasta stems 55 → send **53** for F minor (−2), **50** for D minor (−5)
- BW_D bass shot root 48 → send 38 (sub octave)

Transposing a chord/stem keeps its quality (Gmin−2 = Fmin). Pitching breaks up
+2 semitones (rootNote 62, send 60) = the classic pitched-break jungle move.

## 4. Score grammar (beats; 640 = ~3:42 at 175)

Section grid used in all three tracks (intro→build→mainA→mini→mainB→breakdown
→finale); the mini MUST keep the groove alive (filtered break + quiet hats) —
a dead-silent mini reads as a mistake, and the TRUE breakdown plus a loud
finale are what make the arc read as intentional.

| Section | Beats | Core amen | Sub | Percussion | Extras |
|---|---|---|---|---|---|
| intro | 0–64 | wisps | — | sparse hats | dub bed / pads / rev swells |
| build | 64–128 | enters (vel ~62) | offbeats from 96 | hats 8ths, rim | bass loop from 96 |
| mainA | 128–256 | full (94) | offbeat 8ths, octave drop at 16-bar marks | snare 2/4, 16th hats, rolls at 8-bar | clap on "and" of 3 |
| mini | 256–288 | hushed (48–68) | — | 8th hats | pads + lead |
| mainB | 288–416 | loop B (92) | prog continues | snare 2/4, denser hats | octave bass lift, active lead |
| breakdown | 416–448 | — | — | — | TRUE break: ridim bed/pads+lead solo |
| finale | 448–640 | A+C layered (pitch +2) | full, pump hotter | snare rolls, 16th hats/shares | downlifters, chants, 808s |

Per-role patterns (verified): **sub** offbeat 8ths at `b+0.5`, root per bar
(F minor: F2=41→Eb2=39→Db2=37→Eb2; D minor: D2=38→Bb1=34→F2=41→C2=36),
+octave-drop at every 16-beat turnaround, dur 0.35–0.4; **kick** sparse (on 1
of every 2 bars; 1+3 in the finale) — jungle is break-driven, not kick-driven;
**snare** on 2 & 4 with 16th ghost rolls; **rim** offbeat pings; **hats** 8ths
with 16th rolls at 8-bar boundaries. **Organ skank** (ragga): 2 stabs/bar in
build, 4/bar in mains — never more than 4, and place by BAR offsets (per-beat
offsets produced 1558 stabs = mush; per-bar = 420 = correct).

Chord pads (atmospheric): change every 4 bars; voicings in D minor = Dm
(50,53,57) → Bb (46,50,53) → F (41,45,48) → C (48,52,55).

## 5. Production stack (FX + filter envelopes)

Per-role chains (internal FX; param indices as `set_internal_fx_param` REAL
units — names from the engine spec):

- **Breaks**: reverb (mix .55, size .38 — the atmospheric wash) OR dry+comp for
  raw jungle; comp thr −16–−20; eq HP-sweep lane.
- **Snare**: reverb mix .30–.50. **Hats**: reverb .18–.25.
- **Sub**: comp (thr −20, ratio 3) + eq + per-beat pump on the built-in Volume
  lane (triangle 0.68–1.0 at 1 cycle/beat; gentler 0.82–1.0 for ambient).
- **Bass**: eq + comp; **rumble**: eq sweep; **pads**: chorus (rate 1.1, depth
  0.6, mix 0.5) + reverb + eq (slow filter swell lane).
- **Organ/lead/chants** (ragga): else delay (fb .4–.5, wet .3) + reverb; delay
  TIME is seconds — dotted-8th at 175 = 0.1286 s if you need the classic dub
  pattern (tempo-sync is a requested feature, not yet available).

**Automation contract (all verified):** `add_automation_lane {paramID}`
(FX param IDs = `100 + slotIndex*100 + paramIndex`; volume=1/pan=2/mute=3 are
BUILT-IN lanes per track — adding paramID 1 fails, target "Volume" instead) →
`generate_automation_envelope` (shapes ramp/sCurve/adsr/sine/triangle...;
value = startValue + curve*(end−start), so oscillators need start≠end) or
`set_automation_points` (time BEATS, values normalized 0..1) → **always
`set_automation_enabled=true`** (default off = silent no-op).

Verified sweep vocabulary: sub cutoff macro ramp (0.12→0.70 across the track);
breakdown close-then-open (sCurve down 0.58→0.05, ramp up 0.05→0.80 into the
drop); break HP rise build→mainA; rumble drop-open ramps; pad swell into the
finale; per-beat pump triangles (computed points, res 0.25–0.5 beats).

## 6. Mix + master (canary technique, mandatory)

1. Faders per §2. Render once at **master 0.25**.
2. truePeak = canaryPeak / 0.25; finalGain = min(0.90 / truePeak, 1.0).
3. Re-render at finalGain → peak ≈ 0.90, never clipped. All our jungle mixes
   needed 0.63–0.74 — the sum is hot by design; scale the master, not the
   faders, unless a single element dominates.

## 7. Verify loop (do on EVERY render)

Read the WAV back (24-bit PCM): RMS per section (the arc must dip hard at the
mini AND at the true breakdown, and the finale should read ≥ mainA; in
atmospheric tracks, judge the finale by band energy, not RMS), band energies
(sub 40–110 / bass 90–250 / body 300–2k / high >6k), peak, and — for
pump-heavy mixes — per-beat RMS depth (target ~35–45% for a strong pump).
When a section misbehaves, mute-to-isolate roles rather than guessing; the
#1 mutes: the break track and the sub track.

## 8. Trademark traps (hit and paid for, in order of severity)

1. **Clip-local beats**: note `start` is relative to its clip by default —
   keep one clip per role spanning the whole track at start=0, or pass
   `relative:false` to `add_notes` for timeline-absolute starts (P1-4,
   landed 2026-08-29).
2. **`add_notes` accepts duplicates** — loop generators that write the same
   pitch/start twice stack triggers (244 duplicate hats made a mini-break 3×
   louder); generate per-bar, not per-beat offsets.
3. **EQ-frequency automation does NOT attenuate** — the internal eq is a peak
   filter at Frequency/Q/Gain; a "filtered break" needs a real low/high-pass
   (feature request in flight; until then, drop velocities instead).
4. **Pump placement**: per-track built-in Volume/Pan/Mute lanes exist —
   write the pump with `set_automation_points mode:"replace"` on "Volume", not
   `add_automation_lane`.
5. **Mixing BPMs inside one track**: reggae stems live at their own tempo
   (136 → 5.148 beats/bar at 175) — quantize each source by its own bar.
6. **Future-mtime packs**: if the pack copy carries future file mtimes,
   sidecars never ingest (incremental scan compares sidecar-to-audio mtimes);
   forward-date the sidecars and re-scan. (Engine fixed 2026-08-29: a scan now
   also re-applies a sidecar when the stored entry has none of its data —
   `FileLibraryTest.SidecarAddedAfterScan*` regression pair.)
7. **Unit confusion at the wire**: beats for notes/automation times, seconds
   for exports; win paths for samples; `add_track`→text, `list_tracks`→`id`.

## 9. Where the evidence lives

- Deliverables (render + reloadable project): `.tmp_dnb_theme/jungle_amen_
  fmin_v1.*`, `jungle_ragga_v1.*`, `jungle_atmos_v1.*` (175 BPM, 640 beats).
- Handoff (raw lessons, feature gaps, bugs): `docs/handoffs/2026-08-29-jungle
  -dnb-composition-session.md`.
- Style research: Wikipedia "Jungle music" (fetch + read before significant
  jungle work; repeat the same ritual for jump-up / hardstep / drumfunk).
- Library indexes: `%APPDATA%\HDAW\libraries\registry.json`; sidecars live
  next to each sample (`.timbre.json`); cluster presets via
  `cluster_library saveAs`.
