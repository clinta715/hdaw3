# HDAW → DAW Feature Parity Checklist

Gap analysis of HDAW against the standard feature set shared by
Ableton Live, Bitwig Studio, Cubase, SONAR, and Logic Pro.

Status: ✅ have · 🟡 partial · ❌ missing

Grounded against `README.md` (v0.13.0), `AGENTS.md`, and `FEATURES.md`.
Last updated: 2026-07-30

---

## 1. Audio recording & editing
- ✅ Record audio, trim, fades, crossfade handles, gain, normalize/reverse
- ✅ Timestretch (SoundTouch), slice at playhead/transient/region, region clipboard, gain envelope
- ✅ **Auto crossfades** between adjacent clips (CrossfadeEngine — auto-crossfades adjacent + overlapping audio clips at graph-build time; respects existing fadeIn/fadeOut)
- ✅ **Comping / take lanes** (basic: multiple takes per track, switch via context menu)
- ❌ **Audio warp / elastic markers** (Ableton Warp, Logic Flex Time) — per-transient beat editing, not just whole-clip stretch
- ✅ **Punch in/out** recording (uses loop region as boundaries, auto-stops at loop end)
- ❌ **Freeze / bounce-in-place** (render track to audio to save CPU)
- ❌ **Audio quantize to groove** (move transients to grid/groove template)
- ❌ **Offline FX processing chain** (apply FX destructively with preview)

## 2. MIDI editing
- ✅ Piano roll, velocity, CC record, quantize/humanize/transpose, merge, multi-select, copy/paste
- ✅ **Step sequencer / drum pattern editor** (StepSequencer component in bottom-panel tab)
- ✅ MIDI CC automation recording (mod wheel, expression, pitch bend via CC Rec button)
- 🟡 Controller lanes (velocity editing exists; full CC lane editing partial)
- ❌ **MIDI effects** (arpeggiator, chord, scale, note length, velocity curves — Ableton's MIDI FX rack)
- ❌ **MPE / note-expression** (per-note pitch/pressure/slide)
- ❌ **Groove/swing templates** (extract & apply groove)
- ❌ **Logical MIDI transforms** (Cubase Logical Editor — rule-based note ops)
- ❌ **Input quantize while recording**

## 3. Arrangement & timeline
- ✅ Tracks, clips, markers, tempo track, loop, rubber-band select, clipboard, zoom-to-fit, duplicate track
- ✅ **Track folders / grouping** (trackType audio/instrument/folder; folder chevron collapse/expand; mute/solo cascades from parent folders; folders skipped in audio graph)
- ✅ **Typed tracks** (audio / instrument / folder distinction via trackType property)
- ✅ **Ripple edit** (project.rippleDelete RPC + MCP tool; closes gaps on delete, shifts later content left)
- ✅ **Insert silence** (project.insertSilence — splits clips at boundary, shifts later content right)
- ✅ **Duplicate region** (project.duplicateRegion — copy selected range, insert at region end)
- ✅ **Clip-launch / session view** (SessionView grid with 8 scenes × N tracks, scene buttons, clip slots, view mode toggle, Tab shortcut, MCP tools)
- ✅ **Track colors, height, show/hide** (color picker, drag-resize handle, eye icon toggle; freeze state still missing)
- ❌ **Arrangement markers → navigation panel / chord track**

## 4. Mixing
- ✅ Mixer, fader/pan/mute/solo, VU, sends, buses, FX chain, PDC, automation
- ❌ **VCA / group faders**
- ❌ **Sidechain routing UI** (route bus into comp/EQ)
- ✅ **Loudness metering** (RMS + LUFS momentary in mixer strips)
- ❌ **Monitor section / talkback / dim / mono**
- ❌ **Surround / spatial / Dolby Atmos**
- ❌ **Per-channel input trim & phase invert**

## 5. Instruments & sound generation
- ✅ VST3 + CLAP hosting
- ❌ **Built-in synth** (default "Synth" track has no instrument yet)
- ❌ **Sampler / multisample instrument**
- ❌ **Drum machine / drum rack**
- ❌ **Stock instrument library** (README goal #3)
- ❌ **Render MIDI+instrument to audio** (bounce)

## 6. FX & processing
- ✅ Gain, EQ, compressor, reverb, delay + plugins
- ✅ **FX presets** (plugin factory presets via Presets button in FX chain; browse + select via pluginParam.listPrograms)
- ✅ **FX A/B comparison** (A/B button on each plugin FX slot; swap A↔B)
- ✅ **Modulation FX** (chorus, flanger, phaser via juce::dsp)
- ❌ **Distortion/saturation, filters, limiter/maximizer, tuner**
- ❌ **Spectrum analyzer / tuner / oscilloscope** (metering FX)
- ✅ **FX chain presets** (save/list/load/delete entire chains; RPC: `project.saveFxChainPreset`, `project.listFxChainPresets`, `project.loadFxChainPreset`, `project.deleteFxChainPreset`; MCP: `save_fx_chain`, `list_fx_chains`, `load_fx_chain`, `delete_fx_chain`)
- ❌ **Per-slot wet/dry (mix) knob, oversampling**

## 7. Automation
- ✅ Volume/pan/mute lanes, plugin param automation, recording, CC
- ✅ **Automation modes** (Read/Write/Touch/Latch with knob touch detection)
- ❌ **Bezier/curve automation shapes** (only linear points?)
- ❌ **Clip-based automation** (Ableton — automation lives in the clip, not the track)
- ❌ **Relative/trim automation**

## 8. Tempo & time
- ✅ Tempo track with points, time signature, BPM, metronome/count-in
- ✅ **Time-signature track** (per-bar time signature changes via UI)
- ❌ **Tempo detection/mapping from audio**
- ❌ **Swing/global groove quantize**

## 9. Routing & I/O
- ✅ Buses, sends, MIDI channel routing, input monitoring
- ❌ **Flexible I/O routing matrix** (any-in→any-out)
- ❌ **External hardware insert** (route out to gear and back)
- ❌ **Multi-out instruments, CV/gate**

## 10. Workflow & performance
- ✅ MCP server (38 tools, unique differentiator), prefs, undo/redo, plugin manager
- ✅ **Undo history UI** (History bottom-panel tab; full undo/redo stack; click any entry to jump)
- ✅ **Status bar** (BPM, time sig, sample rate, track, MIDI device, REC, selection count)
- ✅ **Plugin state save/load per slot** (base64 in FX_SLOT.pluginState; round-trip tested)
- ❌ **MIDI-learn / controller mapping / macro knobs**
- ❌ **Tagged browser with preview & favorites**
- ❌ **Project templates & autosave/backup**
- ❌ **Global search**

## 11. Import / export / interchange
- ✅ WAV/AIFF/MP3/FLAC/OGG + MIDI import, WAV export
- ❌ **Multitrack / stem export** (per-track bounce)
- ❌ **AAF/OMF interchange** (round-trip to other DAWs)
- ❌ **Video import & sync**
- ❌ **MP3/AAC export**

## 12. Project & data
- ✅ Save/load, full undo history
- 🟡 **Versioned file format** — provenance + `formatVersion` + migration hook landed (XML still, not binary); see `docs/architecture.md` § "Project File Metadata" (README goal #4)
- ❌ **Collect/consolidate files** (gather all media into project folder)
- ❌ **Project notes / metadata**

---

## Progress summary (v0.13.0)

| Category | ✅ Done | 🟡 Partial | ❌ Remaining |
|----------|---------|------------|-------------|
| 1. Audio recording & editing | 5 | 0 | 4 |
| 2. MIDI editing | 3 | 1 | 5 |
| 3. Arrangement & timeline | 8 | 0 | 1 |
| 4. Mixing | 2 | 0 | 5 |
| 5. Instruments & sound gen | 1 | 0 | 5 |
| 6. FX & processing | 4 | 0 | 4 |
| 7. Automation | 2 | 0 | 3 |
| 8. Tempo & time | 2 | 0 | 2 |
| 9. Routing & I/O | 1 | 0 | 3 |
| 10. Workflow & performance | 4 | 0 | 4 |
| 11. Import / export | 1 | 0 | 4 |
| 12. Project & data | 1 | 0 | 3 |
| **Total** | **34** | **1** | **43** |

**~44% complete** (34 of 78 checklist items).

### Highest-impact remaining items
These unlock the most downstream features or user-visible capability:

1. **Loudness metering** (LUFS/RMS) — quick win, high user value
2. **Automation modes** (Read/Write/Touch/Latch) — core mixing workflow
3. **Sidechain routing** — unlocks professional mixing
4. **Punch in/out** — recording workflow polish
5. **Modulation FX** (chorus/flanger/phaser) — quick juce_dsp wins
6. **Built-in synth** — first-launch experience (needs instrument track type)
