# HDAW → DAW Feature Parity Checklist

Gap analysis of HDAW against the standard feature set shared by
Ableton Live, Bitwig Studio, Cubase, SONAR, and Logic Pro.

Status: ✅ have · 🟡 partial · ❌ missing

Grounded against `README.md` (v0.13.0) and `AGENTS.md`.

---

## 1. Audio recording & editing
- ✅ Record audio, trim, fades, crossfade handles, gain, normalize/reverse
- ✅ Timestretch (SoundTouch), slice at playhead/transient/region, region clipboard, gain envelope
- ❌ **Audio warp / elastic markers** (Ableton Warp, Logic Flex Time) — per-transient beat editing, not just whole-clip stretch
- ❌ **Auto crossfades** between adjacent clips (noted in README)
- ❌ **Comping / take lanes** (README goal #5) — multiple takes, lane selection, flatten
- ❌ **Punch in/out** recording (README goal #5)
- ❌ **Freeze / bounce-in-place** (render track to audio to save CPU)
- ❌ **Audio quantize to groove** (move transients to grid/groove template)
- ❌ **Offline FX processing chain** (apply FX destructively with preview)

## 2. MIDI editing
- ✅ Piano roll, velocity, CC record, quantize/humanize/transpose, merge, multi-select, copy/paste
- 🟡 Controller lanes (velocity exists; full CC lane editing partial)
- ❌ **Step sequencer / drum pattern editor**
- ❌ **MIDI effects** (arpeggiator, chord, scale, note length, velocity curves — Ableton's MIDI FX rack)
- ❌ **MPE / note-expression** (per-note pitch/pressure/slide)
- ❌ **Groove/swing templates** (extract & apply groove)
- ❌ **Logical MIDI transforms** (Cubase Logical Editor — rule-based note ops)
- ❌ **Input quantize while recording**

## 3. Arrangement & timeline
- ✅ Tracks, clips, markers, tempo track, loop, rubber-band select, clipboard, zoom-to-fit, duplicate track
- ❌ **Track folders / grouping** (edit & mix groups)
- ❌ **Typed tracks** (audio / instrument / bus / folder / group distinction)
- ❌ **Clip-launch / session view** (Ableton Session, Bitwig clip launcher) — non-linear scene triggering
- ❌ **Ripple edit** (auto-close gaps on delete)
- ❌ **Track colors, height, show/hide, freeze state**
- ❌ **Arrangement markers → navigation panel / chord track**

## 4. Mixing
- ✅ Mixer, fader/pan/mute/solo, VU, sends, buses, FX chain, PDC, automation
- ❌ **VCA / group faders**
- ❌ **Sidechain routing UI** (route bus into comp/EQ)
- ❌ **Loudness metering** (LUFS/RMS/true-peak, not just VU)
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
- ❌ **Modulation FX** (chorus, flanger, phaser, tremolo)
- ❌ **Distortion/saturation, filters, limiter/maximizer, tuner**
- ❌ **Spectrum analyzer / tuner / oscilloscope** (metering FX)
- ❌ **FX presets & preset browser**
- ❌ **Per-slot wet/dry (mix) knob, oversampling**

## 7. Automation
- ✅ Volume/pan/mute lanes, plugin param automation, recording, CC
- ❌ **Automation modes** (Read/Write/Touch/Latch)
- ❌ **Bezier/curve automation shapes** (only linear points?)
- ❌ **Clip-based automation** (Ableton — automation lives in the clip, not the track)
- ❌ **Relative/trim automation**

## 8. Tempo & time
- ✅ Tempo track with points, time signature, BPM, metronome/count-in
- ❌ **Time-signature track** (multiple sig changes) — verify
- ❌ **Tempo detection/mapping from audio**
- ❌ **Swing/global groove quantize**

## 9. Routing & I/O
- ✅ Buses, sends, MIDI channel routing, input monitoring
- ❌ **Flexible I/O routing matrix** (any-in→any-out)
- ❌ **External hardware insert** (route out to gear and back)
- ❌ **Multi-out instruments, CV/gate**

## 10. Workflow & performance
- ✅ MCP server (unique differentiator), prefs, undo/redo, plugin manager
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
- ❌ **Versioned/binary file format** (README goal #4)
- ❌ **Collect/consolidate files** (gather all media into project folder)
- ❌ **Project notes / metadata**
