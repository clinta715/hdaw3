# HDAW Feature Roadmap

Features needed to reach parity with commercial DAWs, organized by priority tier.

---

## Tier 1 — Critical (blocks basic DAW workflow)

- [ ] **Tempo track** — tempo automation with tempo points, gradual and instantaneous changes
- [ ] **Time signature changes** — per-bar time signature, UI to set/change
- [ ] **Automation → engine integration** — automation points actually control track parameters (volume, pan, mute) during playback
- [ ] **Automation recording** — capture fader/knob movements in real-time during playback
- [ ] **MIDI hardware input** — record from MIDI controllers and keyboards
- [ ] **Audio recording → timeline** — recorded audio becomes a clip on the track at the correct position
- [ ] **Take management** — multiple takes per track, take lanes, comping (selecting best parts from takes)
- [ ] **Count-in / pre-roll** — metronome count before recording starts
- [ ] **Input monitoring** — hear input signal while recording (with latency compensation)
- [ ] **Plugin delay compensation (PDC)** — compensate for plugin latency across the signal chain

## Tier 2 — High Priority (expected by most users)

### Audio Editing
- [ ] **Per-clip waveform editor** — destructive cut/splice/trim within a clip
- [ ] **Audio crossfades** — automatic and manual crossfades between adjacent clips
- [ ] **Clip gain overlay** — visual gain adjustment directly on timeline clips
- [ ] **Reverse audio** — reverse clip contents
- [ ] **Normalize** — gain normalization per clip or selection
- [ ] **Audio quantize** — snap audio transients to grid

### MIDI Editing
- [ ] **MIDI quantize** — snap notes to grid with strength control
- [ ] **MIDI transpose** — shift notes up/down by semitones/octaves
- [ ] **MIDI humanize** — add random timing/velocity variation
- [ ] **MIDI CC automation recording** — capture mod wheel, expression, pitch bend during playback
- [ ] **MIDI channel routing** — per-track MIDI channel selection (not hardcoded to 1)
- [ ] **MIDI output to hardware** — send MIDI to external devices

### Mixing & Routing
- [ ] **Sidechain routing** — route track output as sidechain input to plugins
- [ ] **VCA faders** — grouped volume control without routing changes
- [ ] **Bus FX chains** — full FX chain on group/FX buses (not just single built-in FX)
- [ ] **Wet/dry mix per FX slot** — blend processed and unprocessed signal
- [ ] **FX parameter automation** — automate plugin parameters from automation lanes
- [ ] **Insert FX pre/post fader** — toggle insert position relative to fader
- [ ] **Track freeze / bounce** — flatten track to audio to reduce CPU

### Plugin Hosting
- [ ] **Plugin presets** — save/recall/load plugin factory presets
- [ ] **Plugin state save/load per slot** — `getStateInformation`/`setStateInformation` wired to project
- [ ] **FX chain presets** — save/load entire FX chain configurations
- [ ] **FX A/B comparison** — compare two plugin states
- [ ] **Plugin categorization / tagging** — organize plugins by type/instrument/effect
- [ ] **MIDI effect plugins** — distinguish MIDI FX from audio FX
- [ ] **Instrument plugins** — distinguish instrument plugins from audio FX

### UI / UX
- [ ] **Multi-select clips** — select multiple clips in timeline (rubber band, Ctrl+click)
- [ ] **Clipboard for clips** — copy/paste clips in timeline
- [ ] **Marker track** — named markers at positions, marker navigation
- [ ] **Zoom-to-fit** — auto-zoom to show all content or selection
- [ ] **Horizontal scroll via mouse wheel** — scroll timeline with Shift+wheel or horizontal wheel
- [ ] **Drag-reorder FX slots** — reorder plugins in FX chain via drag
- [ ] **Search/filter in plugin list** — search plugins by name in add-FX menu
- [ ] **Undo history UI** — visual list of undo steps for navigation
- [ ] **Status bar** — show current tool, position, selection info

### Recording
- [ ] **Metronome audio rendering** — actual click sound during recording/playback
- [ ] **Overdub recording** — layer new takes on top of existing audio
- [ ] **Punch-in / punch-out** — record only within a defined region

### Export
- [ ] **Stem export** — export individual tracks as separate files
- [ ] **MP3 export** — MP3 encoding support
- [ ] **Selection export** — export only the selected region
- [ ] **Normalization on export** — normalize output level

### Project Management
- [ ] **Auto-save** — automatic periodic project backup
- [ ] **Project templates** — starter templates (empty, band, electronic, etc.)
- [ ] **Project consolidation** — collect all referenced audio files into project folder

## Tier 3 — Medium Priority (power user features)

### Audio Processing
- [ ] **Time-stretching** — adjust audio timing without changing pitch
- [ ] **Pitch-shifting** — adjust audio pitch without changing timing
- [ ] **Elastic audio / warping** — conform audio to tempo changes via warp markers
- [ ] **Spectral editing** — frequency-domain audio editing
- [ ] **Audio-to-MIDI conversion** — convert audio to MIDI notes

### Automation
- [ ] **Automation curve types** — step, exponential, S-curve (not just linear)
- [ ] **Automation write modes** — touch, latch, write, trim
- [ ] **Automation lanes for plugin parameters** — not just track volume/pan
- [ ] **Automation mute** — temporarily disable automation on a lane
- [ ] **Automation zoom/scale** — scale automation values visually

### Mixing
- [ ] **Mid-side processing** — width/stereo manipulation
- [ ] **Channel linking** — stereo pair linking
- [ ] **Gain staging visualization** — show signal level at each point in the chain
- [ ] **Trim plugin per track** — input gain adjustment before FX chain
- [ ] **Parallel processing** — parallel compression and other techniques

### Arrangement
- [ ] **Arranger track / sections** — define song sections (intro, verse, chorus), rearrange by dragging
- [ ] **Folder tracks** — track grouping with collapse/expand
- [ ] **Track templates** — save/load track configurations (name, color, FX, routing)
- [ ] **Track color picker** — user-selectable track colors (not just palette rotation)

### MIDI Advanced
- [ ] **Expression maps** — articulation management for orchestral libraries
- [ ] **MIDI chase events** — handle note-on at clip boundaries
- [ ] **Score notation view** — traditional music notation display
- [ ] **Chord track** — harmonic analysis and chord detection from MIDI

### UI / UX
- [ ] **Keyboard shortcut customization** — user-configurable key bindings
- [ ] **Workspace layouts** — save/restore screen configurations
- [ ] **Fullscreen mode** — maximize workspace
- [ ] **Floating windows / detachable panels** — tear off mixer, piano roll, etc.
- [ ] **Minimap / overview** — timeline overview for navigation
- [ ] **Ruler format toggle** — switch between bars/beats, seconds, samples
- [ ] **Color theme customization** — user-selectable accent colors, light theme option
- [ ] **Toolbar customization** — user-configurable toolbar buttons

### Project Management
- [ ] **Project versioning** — project snapshots and history
- [ ] **Undo tree** — non-linear undo with branching
- [ ] **Project-wide search** — find clips, notes, parameters
- [ ] **Project import** — import from other DAW formats (Ableton, FL, etc.)
- [ ] **Project statistics** — track count, clip count, duration, file sizes

## Tier 4 — Low Priority (nice-to-have)

- [ ] **Lyrics track** — lyric display synchronized with playback
- [ ] **Chord detection** — detect chords from MIDI input
- [ ] **Drum pattern generator** — dedicated drum pattern creation tool
- [ ] **Melody suggestion** — AI-assisted melody composition
- [ ] **Surround sound** — multi-channel audio support
- [ ] **Collaborative editing** — real-time multi-user editing
- [ ] **Light theme** — alternative light color scheme
- [ ] **Plugin sandboxing UI** — visual indicator for isolated plugins
- [ ] **Batch export queue** — queue multiple export jobs
- [ ] **Dithering options** — noise-shaped dithering for export
- [ ] **Metadata embedding** — artist, title, album in exported files
- [ ] **Project comparison / diff** — compare two project versions
- [ ] **Project encryption** — password-protected project files
- [ ] **Scale-aware input filtering** — constrain piano roll input to selected scale
- [ ] **MIDI scrub** — audition MIDI notes by scrubbing
- [ ] **MIDI monitor** — display incoming MIDI data

---

## Done

- [x] VST3 plugin hosting
- [x] CLAP plugin hosting
- [x] Plugin scanning with crash isolation
- [x] Piano roll editor (notes, velocity, CC)
- [x] Step sequencer
- [x] Mixer with per-track volume/pan/mute/solo
- [x] Automation points (basic model)
- [x] Audio clip playback with waveform display
- [x] MIDI clip playback
- [x] Phrase/chord/progression generator
- [x] Dark theme with centralized colors
- [x] Undo/redo across all operations
- [x] Snap-to-grid
- [x] Loop region with markers
- [x] Audio file import (WAV, AIFF, MP3, FLAC, OGG)
- [x] MIDI file import
- [x] Export (WAV, AIFF, FLAC)
- [x] MCP server (36 tools, 3 transports)
- [x] Plugin editor windows
- [x] Internal FX (EQ, compressor, reverb, delay)
- [x] Per-track sends
- [x] Master bus routing
- [x] Scale-aware piano roll
- [x] Clip move/trim/fade in timeline
