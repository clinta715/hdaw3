# Bitwig Studio 6.1 — UI & Architecture Reference for HDAW

A consolidated design reference built from three independent sources so HDAW
(the JUCE 8 + React 19 DAW) can borrow Bitwig's proven idioms without
re-deriving them. Each section ends with **→ HDAW** takeaways that map the
Bitwig concept onto HDAW's existing architecture (see `AGENTS.md` and
`docs/architecture.md` for HDAW-side names).

## How this was obtained (method & caveats)

- **On-disk / process forensics** (fully reliable): the running process tree,
  the `BtWg` binary container format, `view-settings/` layout trees,
  `prefs/`, the `index/` browser database, and the `.bwproject` data model
  were all reconstructed as text from `%LOCALAPPDATA%\Bitwig Studio\` and the
  project folder.
- **Visual (CUA) inspection**: a Windows computer-use MCP drove Bitwig and
  captured the ARRANGE / MIX / EDIT views, the clip launcher, the EDIT clip
  inspector, the device chain, and the browser. (The model is text-only by
  default; vision had to be enabled via the `modalities` config flag, and the
  provider intermittently times out on large multimodal history — keep visual
  sessions short / fresh.)
- **Official user guide** (`bitwig.com/userguide/latest/`): chapters 5–12 and
  the device/modulator concept pages were read as text.
- **Caveat**: Bitwig's UI is a fully custom-rendered, opaque surface — it
  exposes **zero** UI-Automation children — so structured introspection of the
  live UI is impossible; everything visual is from screenshots. Actual visual
  proportions, exact colors, iconography, and animation timings are *not*
  captured here (only structure and behavior).

---

## 0. North-star mapping (Bitwig region ↔ HDAW region)

HDAW's fixed CSS-grid shell (`App.tsx` / `App.css`) already mirrors Bitwig's
mosaic closely. This is the canonical mapping used below.

| HDAW grid region | Bitwig equivalent | Notes |
|---|---|---|
| `transport` | Transport / display section | tempo, sig, position, key, ADD/EDIT |
| `headers` (left) | Track headers / **inspector** | Bitwig's left strip *is* the inspector in EDIT |
| `timeline` (center) | Arranger Timeline Panel | ruler + clip lanes |
| (center, top) | **Clip Launcher Panel** | session grid; HDAW has no equivalent yet |
| `browser` (right) | Browser Panel | unified indexed content DB |
| `bottom` (tabs) | **complementary_panel** with `.choice` | HDAW's "detail view" = Bitwig's selectable bottom frame |
| `clipedit` strip | Detail Editor Panel | piano/drum editor + note expressions |
| `status` | Window footer | context-sensitive interaction hints |

The three center **modes** — `ARRANGE` / `MIX` / `EDIT` — are Bitwig's
top-level view switch. HDAW instead swaps content inside the stable `bottom`
tab frame (Mixer / Piano Roll / Automation / FX / …). Both are valid; see §4
for the behavioral difference.

---

## 1. Process & engine architecture

Bitwig runs as **four cooperating processes**:

| Process | Role | Evidence |
|---|---|---|
| `Bitwig Studio.exe` | Launcher | cmdline |
| `BitwigStudioApp.exe` | **UI (JVM)** — framework codename **"float"** (`float-gui-widgets`, `float-document-master` log channels) | `Sub-system: JVM` |
| `BitwigAudioEngine-X64-AVX2.exe` | Native audio engine, **out of process** | args `… 44100 256 …` (sample rate + buffer on the cmdline) |
| `BitwigPluginHost-X64-AVX2.exe` | **Out-of-process plugin host**, child of engine | args `host all <enginePID>-1 16 44100 256 dpi-aware <GUID>` |

- UI ↔ engine talk over a port (the `server-port` file); lifecycle in logs:
  *Opening project → Starting engine → Connecting audio engine → Engine
  connected → Saving → Stopping engine*.
- An engine crash dump (`BITWIG_ENGINE.mdmp`, `0xC0000005`) sits on disk
  **while the UI kept running** → crash isolation is real and effective.
- The plugin host is a **child of the engine**, identified by a per-host GUID,
  and is handed the sample rate + buffer size on its command line.

**→ HDAW**: identical philosophy to HDAW's frontend↔engine WebSocket JSON-RPC
(port 8766) plus plugin process isolation (default ON). Bitwig validates the
multi-process split as the industry-grade choice. Two concrete notes: (1)
negotiate sample-rate/buffer on the host spawn line, not via a later message;
(2) keep the UI alive across an engine crash — HDAW's isolation should aim for
the same "engine dump, UI survives" property (see `docs/realtime-safety.md`).

---

## 2. File formats & on-disk layout

Everything under `%LOCALAPPDATA\Bitwig Studio\` and the project folder uses a
proprietary **`BtWg` container** (magic `BtWg0003` for project/view files,
`BtWg0001` for prefs). It is **binary but not encrypted** — payloads are
length-prefixed plaintext, so all strings (keys, paths, parameter ids) are
extractable. This matters: Bitwig's "binary" format is really a compact
key/value blob with interned strings, not an opaque codec.

| Path | Contents |
|---|---|
| `view-settings/<project-uuid>` | Per-project **panel layout tree** (see §3) |
| `prefs/<version>.prefs` | Versioned global preferences (paths, audio I/O, recent projects, display config) |
| `index/<hash>-<type>.index` + `<hash>-state/` | **Browser content database** (see §8) |
| `index/*.ids` | Interned string dictionaries: `categories`, `tags`, `creators`, `device-names`, `device-creators`, `file-name-words`, `clap-device-ids`, `vst3-device-ids` |
| `installed-packages/`, `library/` | Content packs + native `.bwdevice` library |
| `<project>/<name>.bwproject` | Single ~tens-of-MB `BtWg` container (v6; not the old XML dir) |
| `<project>/auto-backups/<name>/<name> [YYYY-MM-DD HHMMSS].bwproject` | Timestamped automatic backups |
| `<project>/samples/` | **Project media pool** — audio copied into the project |

**→ HDAW**: HDAW's project model is a JUCE `ValueTree` serialized to disk; the
takeaways are about *shape*, not format: (a) keep **per-project UI/layout
state separate from the musical data** (Bitwig's `view-settings` is keyed by
project uuid and never pollutes the `.bwproject`); (b) keep **automatic
timestamped backups** (cheap, high-value, matches a DAW user's expectations);
(c) the **audio pool** model — see §8 and the Cubase-pool convention already
adopted in `AGENTS.md`.

---

## 3. The layout tree (the key structural finding)

`view-settings/<uuid>` stores the entire window layout as **dot-path keys into
a nested splitter tree**. Reconstructed:

```
touch_keyboard_and_main_splitter            (optional on-screen keyboard | main)
├─ keyboard.is_shown
└─ inspector_and_main_splitter              (inspector | main+browser)
   ├─ inspector.is_shown                    ← LEFT
   └─ main_and_browser_splitter             (main | browser)
      ├─ browser_area.choice = browser      ← RIGHT
      └─ main                               ← CENTER, 3 view-modes:
         ├─ main.1.arranger_detail_splitter       ARRANGE
         ├─ main.2.mixer_detail_editor_splitter   MIX
         └─ main.3.editor_other_splitter          EDIT
              └─ complementary_panel.choice = devices   ← selectable BOTTOM
```

Two structural facts:

1. **Layout state is duplicated per view-mode.** `main.1.*`, `main.2.*`,
   `main.3.*` each carry a *full, independent* copy of splitter positions and
   sub-panel visibility. Switching ARRANGE→MIX→EDIT remembers each mode's
   geometry separately.
2. The bottom frame is a **single selectable panel** (`complementary_panel`
   with `.choice` = `devices` / `mixer` / detail-editor / …) — one stable slot,
   content swapped in place.

**Per-panel persisted state** (a ready-made feature checklist):

| Panel | Persisted state |
|---|---|
| Mixer | `are_meters_visible`, `are_clocks_visible`, `is_io_section_visible`, `are_sends_visible`, `is_clip_launcher_visible` (+ `_big_height`), `are_remote_controls_visible`, `are_comments_visible`, `is_device_chain_visible`, `is_crossfade_visible`, `fader_height`, `track_scroll_position`, `scene_scroll_position` |
| Detail editor | `is_automation_section_shown` + `_height`, `is_expression_section_shown` + `_height`, `pattern_note_lane_height`, `expression_note_lane_height`, `pattern_audio_lane_height`, `audio_editor_lane_height` |
| Devices | `device_scroll_position` |
| Arranger | `hide_clip_headers_except_cursor` |

**→ HDAW**:
- **Persist layout per view-mode, not globally.** HDAW's bottom tabs could
  remember splitter/visibility state per active tab the same way Bitwig stores
  `main.1/2/3`.
- The selectable bottom frame **validates** HDAW's north star ("bottom panel =
  detail view, tabs swap content in a stable frame"). Keep it a stable slot.
- The mixer visibility toggles are a feature surface worth adopting wholesale
  into `MixerStrip` (meters / clocks / IO / sends / device-chain / crossfade /
  comments visibility).

---

## 4. The three views: ARRANGE / MIX / EDIT

The bottom-left tabs do **not** just swap the bottom panel — they transform the
**track headers** and the **interaction model** (the window footer's hint line
changes per mode). The device chain panel and the browser are the only regions
that stay put across all three.

- **ARRANGE**: center = Clip Launcher grid *stacked above* the Arranger
  Timeline (both visible at once — unlike Ableton, which switches). Left =
  track headers. Footer hints: *DRAG Rectangular selection · CTRL+ALT+DRAG Pan
  · DOUBLE-CLICK Insert clip*.
- **MIX**: center = vertical **Mixer Panel**; track headers become full
  channel strips. Footer hints shift to mixer editing.
- **EDIT**: center = Arranger with the left strip replaced by the **clip
  inspector**; double-clicking a clip opens the Detail Editor (piano/drum).
  Footer hints: *ALT+CLICK Create Automation Clip · DOUBLE-CLICK Insert event
  · CTRL+DOUBLE-CLICK Insert HOLD event*.

**Selection-driven context switching** (observed live):
- Clicking an empty/filled **launcher slot** flips the left strip to **SLOT**
  mode (e.g. a "Has stop/rec button" checkbox) and the top-right context button
  to **CLIP**.
- Clicking a **track / arranger clip** flips the left strip to the **ARRANGER
  CLIP** inspector (see §10/§11).
- The footer hint line is **context-sensitive** to whatever panel the cursor is
  over — a discoverability mechanism HDAW's `status` region can mirror.

**→ HDAW**: HDAW's current model (bottom tabs swap content in a stable frame,
headers constant) is simpler and matches the "spatial stability" north star.
Bitwig's mode-transforms-headers approach is more powerful but more volatile.
A reasonable middle path: keep HDAW's stable frame, but make the **left
inspector context-sensitive to the selection** (track vs clip vs slot), exactly
as Bitwig does — that is high value and low layout-risk. Also adopt the
**context-sensitive footer hints** in the `status` region.

---

## 5. Clip Launcher (session view)

- A grid of **tracks × scenes**. A **scene** is a vertical column; triggering
  it launches that column's clips in sync.
- **Sub-scenes** exist for group tracks — miniature rows that trigger the
  group's contained clips and show mini clip-playheads while playing.
- **Launcher clip parameters** (in the inspector): a **Start/Stop** section
  (what portion plays — `Start`, and `Stop` when loop is off), a **Launch**
  section (**launch quantization**: 1/2…1/16 or 1/2/4/8 bars; "Use Project
  Setting" inherits from the Project Panel), and a **Next Action** section
  (what happens when the clip ends).
- **Copy freely between the two sequencers**: drag Arranger↔Launcher; drag a
  scene into the Arranger; drag a selection of Arranger clips into a scene.
- **Slide content**: ALT-drag horizontally over a clip's top half shifts its
  contained events without changing the clip length (SHIFT toggles snap).

**→ HDAW**: HDAW has no session/launcher view today (it is arranger-first). If
a launcher is ever added, the key ideas are: launch quantization as a
*performance* grid separate from the arrangement grid, per-clip Next Action,
and bidirectional drag between the two sequencers. Lower priority than the
arranger/mixer/editor work; note for the roadmap.

---

## 6. Arranger timeline

- **Snap settings** (bottom-right of any timeline editor), three independent
  anchors that compose: **Grid**, **Grid Offset** (preserves a clip's existing
  offset from the grid while moving), **Events** (snap to other clips' edges).
- **Automation follow** toggle: whether automation travels with a moved clip.
- **Consolidate** (`Edit → Consolidate` / `Ctrl+J`): Bitwig is nondestructive
  by default (a shortened clip still remembers its hidden tail); Consolidate
  solidifies and discards the unseen data.
- **Free scaling**: ALT-drag a clip edge to time-stretch/scale its contents
  (vs the normal bracket drag, which trims/extends without rescaling).
- **Automation lanes**: each track header has an Automation Lane button. The
  primary lane is a **"joker" lane** that follows the last-clicked parameter;
  **Pin** locks it; **Add Lane** fixes extra lanes. The parameter menu lists
  targets in **signal-flow order** (MIDI lanes → device chain, nested inside
  parents → mixer volume/pan). **MIDI automation lanes** (Pitch Bend / Channel
  Pressure / CC) are first-class. Editing: ALT-drag a segment to shape its
  transition curve; the **Pen tool** redraws then *optimizes to the minimum
  number of points*; ALT-drag a time selection's edge to time-scale points.
- Bitwig supports **two automation philosophies at once**: track-based
  (absolute, narrative) and **clip-based + relative** (clip-local, layered on
  top).

**→ HDAW**:
- Adopt the **three composable snap anchors** (grid / grid-offset / events) —
  grid-offset in particular is the non-obvious one that preserves feel when
  nudging off-grid material.
- The **joker lane + Pin + Add Lane** pattern is a clean UX for HDAW's
  automation lanes (avoids a giant parameter picker in the lane header).
- Make **MIDI CC/PB/aftertouch first-class automation targets** (HDAW already
  has automation; ensure MIDI streams are addressable as lanes).
- Decide explicitly on **clip-based vs track-based automation**: Bitwig has
  both; HDAW's `ValueTree` automation model should pick one as primary and
  document the choice (relates to the beats-vs-seconds boundary in
  `docs/architecture.md` — clip-local automation is naturally clip-relative).

---

## 7. Mix view

- **Vertical** orientation (traditional board). Track headers at top and
  channel strips near bottom are always visible; **View Toggles** show/hide the
  other sections (clip launcher, big meters, remotes, devices, sends, …) plus
  toggles for showing FX tracks and deactivated tracks.
- **Fold button** on a track header: for container devices (Drum Machine,
  Instrument Layer, FX Layer) it expands the channel strip sideways to expose
  each **layer** with its own volume/pan/sends — layers behave like sub-tracks.
- **Clip Launcher can load inside the Mixer**, rearranged vertically.
- **Big Meters** section (only when the in-mixer launcher is off).
- **Track Remotes** section: chosen parameter controls from many tracks
  side-by-side; count is configurable; tracks can *alias* device remote pages
  until you make your own.
- **Devices** section: a per-track list of top-level devices with **mini
  displays** (EQ curves for EQ+/EQ-5/EQ-2/Focus/Sculpt/Tilt; gain-reduction
  meters for Compressor+/Gate/Peak Limiter/…) and an **Expanded Device View**
  button on hover.
- **Sends** section: one knob per FX track, **Pre / Post / Auto** source,
  **color-coded** (yellow = post, blue = pre); click name to bypass;
  SHIFT-click to bypass all on a track.

**→ HDAW**: the highest-value borrowables for HDAW's `MixerStrip` / mixer tab:
(1) **container layers as sub-strips** via a fold control (maps onto HDAW's
layered/group tracks); (2) **inline mini-displays** (EQ curve, gain reduction)
in the device list — cheap, high-legibility; (3) **color-coded pre/post sends**
with per-send bypass; (4) **track remotes** (a small, mappable control surface
per track on the mixer) — a natural home for HDAW's remote/macro controls.

---

## 8. Browser = a unified, indexed content database

The browser is **not** a directory tree — it is a categorized, per-source
**content database** under `index/`:

- **Per plugin format**: `vst3-x64`, `vst2-x64`, `vst2-x86`, `clap-x64` (+
  `*-preset`, `vst-preset-bank`).
- **Per content type**: `bitwig-devices`, `bitwig-presets`,
  `bitwig-multi-samples`, `samples`, `midi`, `clips`, `curve`, `impulse`,
  `wavetable`, `soundfont`, `modules`, `modulators`, `project`,
  `demo-template`, `preset-discovery`.
- **Per-source scan state**: `<hash>-state/` directories.
- **Interned string dictionaries** for fast search/filter: `categories.ids`,
  `tags.ids`, `creators.ids`, `device-names.ids`, `device-creators.ids`,
  `file-name-words.ids`, plus format id tables.
- A **Collections** tree (user folders: e.g. `samples → _real_leads 132`,
  `_FXs 52`, …) over the same index, with **filter chips** (File Kind /
  Creator / Device / Tags) and a **search** box.
- A **Pop-up Browser** (the `+` on an empty slot / track Add-Device button) —
  a context-configured inline browser.

**→ HDAW**: this is the model behind a single unified browser and matches
HDAW's file-browser + Audio Pool direction. Concrete moves: (1) index by
**content type** *and* **plugin format**, with **per-source scan state** so
rescans are incremental; (2) **intern** categories/tags/creators/names as
shared dictionaries (fast faceted filtering, exactly the `tags`/`creator`
chips); (3) the **Audio Pool** (Cubase model already in `AGENTS.md`) is the
"samples/clips" source — reference-counted, drag-to-mint-clip; (4) offer a
**pop-up/inline browser** at every drop target (empty slot, Add-Device) rather
  than only the docked panel.

---

## 9. Devices & signal-flow typing

Devices sit in a per-track **device chain** between the sequencer and the
mixer. Bitwig defines **26 device categories** (Analysis, Audio FX, Clap,
Container, Cymbal, Delay, Distortion, Drum Kit, Dynamics, EQ, Filter,
Hardware, Hi-hat, Kick, MIDI, Modulation, Note FX, Organ, Percussion, Reverb,
Routing, Snare, Spectral, Synth, The Grid, Tom, Utility). Devices are **files**
(`.bwdevice` / `.bwpreset`) addressed by package path.

Clean signal-flow rules (good graph-typing for a routing engine):

- Every chain carries **both audio and note signals**.
- A non-NoteFX device passes notes through untouched; a non-AudioFX device
  passes audio through untouched → a device's *category* determines which
  signal it processes vs passes.
- Many devices expose a **Mix** (wet/dry) parameter.
- All audio paths are **stereo**.

**→ HDAW**: encode the same pass-through rule in `RoutingManager` / the
processor graph — type each node by what it transforms (audio / note / both)
so bus layout and graph wiring can be derived, not hand-specified. This also
keeps the latency/quality evaluation (AGENTS lessons 7–8) tractable: a
pass-through node adds no processing latency. The **Mix wet/dry** convention
is worth standardizing across HDAW's internal FX.

---

## 10. Note events & per-note expressions

The Detail Editor (piano-style or drum-style) plus the inspector expose a rich
per-note model — confirmed both visually and in the manual:

- **Note Expressions** = five per-note lanes: **Gain, Pan, Pitch, Timbre,
  Pressure** (a strictly richer model than velocity-only; this is what the
  EDIT-view inspector's "EXPRESSIONS" block showed).
- **Micro-pitch** editing mode (piano editor only).
- **Audition** toggle (drag-to-pitch previews audibly; clicking the keyboard
  triggers notes).
- **Quick Draw**: ALT-drag a run of equal-pitch notes at the grid; add SHIFT
  to free the pitch (step-sequence pitches). Drawing sets velocity/length by
  drag and remembers them as the clip's new defaults.
- **Note color modes**: by **Clip / Note Channel / Pitch Class**, with
  velocity → saturation.

**→ HDAW**: HDAW's piano roll / `NoteGrid` should treat **the five expressions
as first-class per-note data**, not just velocity — this is the same axis
HDAW's modulation system modulates. The **Quick Draw** gesture (ALT-drag a
row, SHIFT frees pitch) is a high-value, low-cost piano-roll interaction.
Pitch-class coloring is a cheap legibility win for the generative/scale-aware
workflow (`PhraseGenerator`).

---

## 11. Operators — the generative / probabilistic layer (highest-value takeaway)

Operators animate *when/how* events trigger, per selected event, in the
inspector. Four modes; each mode icon doubles as a **bypass toggle**; a fresh
event is **neutral** (plays every pass). Crucially, the randomized behavior is
**seed-driven** (the clip's **Seed** parameter) → *deterministic* randomness.

- **Chance** — probability the event fires (e.g. 50% over 4 loops ≈ 2 fires).
  Visualized as **dice dots** on the event (5 dots = 80–100% … 1 dot =
  0–20%). Has its own expression lane after velocity. Driven by the clip Seed.
- **Repeats** — one event retriggers into many. **Repeat Rate**: drag *up* =
  divide into N pieces (2–128, length-relative); drag *down* = beat-rate
  fractions (1/2…1/128, length-independent). **Repeat Curve** bunches repeats
  to the start (−) or end (+). Notes add velocity-end/curve.
- **Occurrence** — cycle-aware logic: fire only on loop N, or think in cycles
  of *k* and play loops 1/2/4/… each cycle.
- **Recurrence / Previous** — inter-event logic: play only if the previous
  event did (or didn't) fire; **Fill** maps events to a performance toggle.

**→ HDAW**: this is a structured, seed-driven, cycle-aware probabilistic layer
over notes/audio — a direct **generalization of HDAW's humanize/randomize** and
a natural extension point for `src/engine/PhraseGenerator.h`. Concrete
adoptions, in priority order:
1. **Per-note Chance with a clip Seed** + the **dice-dot visualization** (cheap
   UI, big legibility win).
2. **Repeats-with-curve** (ratchets) as a note property.
3. **Cycle-conditioned triggering** (Occurrence) for evolving loops.
4. Keep all of it **seed-deterministic** so it composes with HDAW's undo and
   the MCP/RPC surface (a given seed reproduces a given performance — essential
   for tests and for the "MCP feature parity" rule).

---

## 12. Modulation

- **Modulators** are special modules loadable **inside any device** *or* **on a
  track** (a track-level modulator drives all contained devices + mixer
  controls). This matches the `MODULATORS/0..N` tree seen in `.bwproject`
  (`MODULATORS/1/CONTENTS/RATE_BEAT`, `…/SHAPE`, `…/DEPTH`, `…/BIPOLAR`) and
  HDAW's per-track `MODULATION_LIST` / `ModulationManager` /
  `LFOModulationSource`.
- A parameter therefore has **five control sources**: direct value, MIDI
  controller, automation, remote control, or a modulator.
- Modulator rates are **beat-synced** (the rate grid: half/quarter/8th/16th/
  32nd + dotted + triplet, each with an icon).

**→ HDAW**: HDAW already has the right shape (per-track LFO modulation). The
manual confirms two things to keep: (1) **track-level modulators that fan out
to every device + mixer param** (not only device-internal ones); (2) the
**five-source control model** — when HDAW resolves a parameter's effective
value, the precedence/combination of {manual, MIDI, automation, remote,
modulator} should be explicit and documented (this also affects the
ReadModel/delta-sync path — modulated values are *derived*, like
`effectiveMuted`, and must not be deltaed naively; see AGENTS lesson 4 and
`docs/valuetree-listener-contract.md`).

---

## 13. Consolidated HDAW action checklist

Prioritized, cross-referenced to HDAW systems. "Cost" is a rough UI/engine
effort guess.

| # | Borrow from Bitwig | HDAW target | Cost | Why |
|---|---|---|---|---|
| 1 | Per-note **Chance + Seed** w/ dice-dot viz | `NoteGrid`, `PhraseGenerator` | med | Core generative pillar; deterministic |
| 2 | **5 note expressions** (gain/pan/pitch/timbre/pressure) as first-class | piano roll + modulation | med | Richer than velocity-only |
| 3 | **Context-sensitive left inspector** (track/clip/slot) | headers/inspector | low | High value, no layout reflow |
| 4 | **Context-sensitive footer hints** | `status` region | low | Discoverability |
| 5 | **Per-view-mode persisted layout** | UI store | low | Matches north star |
| 6 | **Composable snap** (grid / grid-offset / events) | timeline drag | low | grid-offset preserves feel |
| 7 | **Mixer mini-displays** (EQ curve, GR) + **color-coded pre/post sends** | `MixerStrip` | med | Legibility |
| 8 | **Joker automation lane + Pin + Add Lane**; MIDI lanes | automation | med | Clean lane UX |
| 9 | **Container layers as sub-strips** (fold) | grouped/layered tracks | med | Drum-machine idiom |
| 10 | **Unified browser**: typed indexes + interned dicts + pop-up browser | browser + Audio Pool | high | Faceted search, incremental scan |
| 11 | **Repeats-with-curve** (ratchets), **Occurrence** (cycle logic) | note model | med | Evolving generative loops |
| 12 | **Track-level modulators** fan-out + **5-source** param resolution | `ModulationManager` | med | Matches existing model; document precedence |
| 13 | **Auto timestamped backups** | project save | low | Cheap, expected |
| 14 | **Clip-based vs track-based automation** decision | automation model | low | Document the choice |
| 15 | **Session/launcher view** (launch quant, next action) | new view | high | Roadmap, not now |

Items 1–6 are the "quick wins" that align with HDAW's stated generative +
spatial-stability goals and touch no risky layout code.

---

## 14. Open questions / not determined

- Exact **visual proportions, palette, iconography, and motion timings** (vision
  sessions were short and the provider times out on long multimodal history).
- The precise **parameter-resolution precedence** among the five control
  sources (manual/MIDI/automation/remote/modulator) — the manual states they
  coexist but not the exact combine order; would need a focused read of ch16.
- **The Grid** (Bitwig's modular patcher) — its manual page is gzip-encoded on
  the host and was not decoded; it is also the least central to HDAW's current
  scope.
- Whether Bitwig's **clip-based automation** is stored per-clip or as a
  relative overlay on track automation (ch9 detail not fully captured).

These can be closed later with a fresh, short vision session (for visuals) or
targeted manual reads (for the two automation/modulation details).
