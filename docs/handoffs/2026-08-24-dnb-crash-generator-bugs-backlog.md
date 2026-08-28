# Handoff: DnB session 2 — verify/save crash + generator bugs + feature backlog (2026-08-24)

## Context

Continuation of the Voltage DnB MCP composition work:
`2026-08-23-voltage-dnb-mcp-composition.md` →
`2026-08-23-voltage-dnb-bugs-streamlining-shipped.md` → **this session**.

Full end-to-end DnB composition was rebuilt via MCP after a crash destroyed the
first attempt (details below). The final project renders correctly:
`renders/DnB_Voltage_Test.hdawproj` + `renders/DnB_Voltage_Test.wav`
(4:06, 44.1kHz/24-bit, verified non-silent, no clipping, correct dynamic arc).

**Nothing in the engine was changed this session** — this handoff is a bug
report + prioritized feature backlog. Note the date bug in the session: work
happened 2026-08-24; the earlier "2026-08-23" lineage is correct.

## The crash (highest priority bug)

### Repro

1. Start a project with long clips + reverb-heavy tracks (pad track: 4-minute
   clip, chorus + reverb).
2. Call MCP `verify_part` (composition.verifyPart RPC) on the pad track with a
   small `windowSeconds` (3–8 s).
3. Let the MCP request time out client-side (-32001) — call it twice.
4. Call MCP `save_project`.

Observed: engine process dies silently. No WER event, no minidump, log just
stops. MCP connection closes (-32000), client-side hdaw tools vanish (a
restarted server requires relaunching the MCP client/session, not just the
engine).

### Evidence

- `hdaw_debug.log` (pid 19444) ends at 13:13:55.722 with a full FX-slot
  teardown + rebuild burst: `FXSlotDtor` × 15 (all sampler/fm_synth/chorus/
  delay/reverb/compressor slots), then `FXSlotCtor` × 15, then
  `RoutingDiag rebuildFromValueTree: tracks=9 midiClips=24`. Then silence.
- The two prior `verify_part` calls on track 8 (Pad FM) had timed out at both
  8 s and 3 s windows. Their offline renders presumably kept running
  server-side (no cancellation on MCP timeout).
- Event log: only an *older* HDAW hang from that morning; nothing for this
  death.

### Suspected mechanism

`save_project` triggers a full routing-graph rebuild (log confirms
`rebuildFromValueTree`). The still-running (uncancelled, orphaned) verify
render thread touches FX slots / graph nodes the rebuild is destructing →
use-after-free or deadlock → silent death. This is the crash family documented
in `docs/postmortem-silent-clap-export.md` §6 and AGENTS.md lessons 12/14 —
but reachable now from an **internal-FX-only project** (no plugins involved).

Secondary: `verify_part` on the pad appeared to render far more than the
requested window (timed out even at `windowSeconds=3`), suggesting the window
is measured from song start, not from the clip/section of interest.

### Suggested fix direction

1. Cancellation tokens for MCP-triggered offline renders; MCP timeout (or any
   subsequent mutating RPC: save/load/export/rebuild) must cancel + join
   outstanding renders first.
2. `save_project` / `load_project` / `export_audio` / `rebuildRoutingGraph`
   must exclude concurrent render domains the same way export does (the
   pump-park/render-domain pattern already exists — apply it to the
   composition.audition/verify/autoGain render path).
3. Fatal-error telemetry: minidump on abort/unhandled exception + a last-gasp
   log line, so "silent death" is diagnosable.
4. `auto-backups/` appeared under `renders/` after the engine restart — if an
   autosave feature landed, verify it covers MCP-driven sessions; if not,
   that folder is unexplained and worth checking.

## Bugs surfaced (unfixed)

| # | Bug | Evidence / repro notes | Suspected area |
|---|-----|------------------------|----------------|
| B1 | `verify_part` render not cancelled on MCP timeout; races `save_project` rebuild → crash | See above | `CompositionCommands` render path + `rebuildRoutingGraph` exclusion |
| B2 | `generate_progression` writes note durations = full clip length | `beatsPerChord=16, durationBeats=64` → all 12 chord notes got `duration:64` (chords sustain over each other); starts correct | `PhraseGenerator` progression writer |
| B3 | `generate_phrase` style `BassLine` ignores beat grid | 9 notes over 16 beats at starts 1.778/3.556…, dur 1.6 — evenly-spaced drones, no quantization. Analyzer has `quantizationStrength`; generator lacks it | `PhraseGenerator` |
| B4 | `audition_plugin` on an existing slot errors `"track has no clips"` | Empty-track audition blocked; 8/23 fix removed the internal-FX gate but a clips-required gate remains on the existing-slot path (temp-probe path `trackIndex<0` may be the workaround — verify) | `AudioEngineCommands_Composition.cpp` (~900) |
| B5 | Rhythm DSL voice auto-embellishes with multi-pitch kit layering | `dsl="x---------x-----..."` single pitch came back with pitches 36/42/60 mixed in; undocumented, no opt-out; fights one-sample-per-track sampler designs | `RhythmPatternGenerator` DSL voice |
| B6 | Crash leaves zero telemetry | No WER event, no dump, log truncates | global fatal handler |

## Friction / UX items (behavior verified, adapted around)

- DSL rejects `.` as rest — only `-` works (`dsl error: Unexpected character '.'`).
  Document or accept `.`.
- `loop_clip` `repetitions` = **total copies** including the original (4 bars ×
  repetitions=32 → 128 bars), and clip duration reports in seconds
  (88.2759 s = 256 beats @174). Not a bug, but surprising; document in tool
  description.
- `duplicate_clip` has no length param — every section copy needed a manual
  `set_clip duration` trim to the section boundary. See feature F6.
- `verify_part` slow / window-from-zero (see B1 secondary).

## Feature suggestions (prioritized)

| # | Feature | Why (from this session) |
|---|---------|-------------------------|
| F1 | **Render cancellation + domain exclusion** (fix for B1) | The session-killer. Highest priority. |
| F2 | **Crash resilience**: minidump + rotating autosave; consider a `project.checkpoint` MCP tool | Checkpoint-saves-after-every-stage saved the rebuild; should be an engine feature, not operator discipline |
| F3 | **Multi-sample drum kits for the internal sampler** (keymap: pitch → sample within one slot) | Turns B5 into a feature: generators' multi-pitch output becomes a kit. Aligns with open S6 (slice-mode percussion cookbook) from the 8/23 handoff |
| F4 | **Seek-based windowed rendering** for verify_part/auto_gain (render [t, t+w], not [0, w]) | Two MCP timeouts on the pad; windows appear measured from song start |
| F5 | **Genre phrase styles** (e.g. a real "rolling DnB bassline": grid-locked 8th/16th rolls, ghost notes, passing tones); fix B2 durations; per-chord velocity/attack shaping for pads | I hand-wrote 61 bass notes + 16 pad chord notes because generator output wasn't musical |
| F6 | **Section-aware arrangement ops**: `duplicate_clip` with length, "fill bars X–Y", bars-based API, or MCP exposure of arranger chains | ~15 calls burned on section boundary math + trims |
| F7 | **Track templates** (save/load full stack: FX chain + params + samples + midi FX + fader) | Post-crash rebuild replayed ~40 scaffold calls; template = 1 call |
| F8 | **Sampler key-lock helper**: detect tonal sample pitch → set `rootNote` to lock to project key | The F# kick (`Bumble 50Hz (F#).wav`) was luck; Lekebusch kicks are pitch-labelled in filenames |

Still open from 8/23 handoff and reinforced by this session: **S2**
(analyze→pattern-library bridge), **S6** (slice-mode percussion cookbook ≈ F3),
**S7** (MCP composition cookbook — this session's pipeline is the content; see
"Verified pipeline" below).

## Session facts

- **Recovery discipline that worked**: incremental `save_project` checkpoints
  after each stage (scaffold / arrangement / mix). The rebuild replayed the
  original composition exactly — seeded generators are deterministic
  (`generate_phrase` seed=42/7/13 reproduced identical note lists incl.
  noteIds on fresh ids), so transcript-replay is a viable disaster recovery.
- **MCP restart procedure**: engine death kills the MCP connection; my hdaw
  tools vanished client-side. Restart = run `mcp-launch.bat` (kills stale
  holders, ships fresh binary) + reconnect the MCP client/session. Cannot be
  done from inside the dead session.
- **Assets used** (repro material): DX7 banks at
  `D:\pdf\Dexed Presets\80's Librairy\` (bassea.syx v14 GrwlBass 2,
  synplus.syx v22 SYN-LEAD 1, strings.syx v27 ANLG STG 1); Lekebusch samples
  at `E:\samples\_lekebusch\` (SAE 01 Bumble 50Hz (F#) kick, SAE 03 7x
  Kompressor snare, SAE 02 Bossmanne 8k hats, SAE 05 Bv12 Clave, SAE 06 Bv12
  Tom 160Hz); Voltage Vol.5 MIDI at `E:\midi\Voltage Vol.5 - MIDI\`
  (LD/BS/PL - Redline/Velvet Control analyzed for style extraction).
- **Composition parameters** (for the cookbook / regression):
  F# minor (root 6, mode 1), 174 BPM, 4/4; progression pattern 4
  (i–VII–VI–V: F#m–E–D–C#), 16 beats/chord; polyrhythms E(7,16) hats ×
  E(5,16) clave × E(3,8) toms; structure Intro(0-32)→Groove(64)→MainA(64-320)
  →Break(320-384)→Build(384-416)→MainB(416-608)→Outro(608-672), 672 beats.
- **WAV verification approach** (no ffmpeg available): PowerShell script
  parsing the 24-bit WAV directly — find `data` chunk (NOTE: engine writes a
  JUNK chunk before `fmt `; skip RIFF header from offset 12), probe RMS/peak
  at 11 positions. Script saved at
  `C:\Users\hapbt\AppData\Local\Temp\opencode\verify-wav.ps1`; results:
  intro rms 0.10 → Main A 0.24 → break 0.12 → Main B 0.17 → outro 0.14,
  max peak 0.61 (no clipping). PowerShell 5.1 has no `[BitConverter]::ToInt24`
  — read 3 bytes, sign-extend top 16 manually.
- **Verified pipeline** (candidate for S7 cookbook): recon assets → scaffold
  (tempo/TS/scale/tracks/FX/samples/sysex/one-shot) → CHECKPOINT → patterns
  (euclidean + explicit notes; expect B2/B3/B5 workarounds) → loop + sections
  → CHECKPOINT → FX params → automation lanes (paramID formula
  `100+slot*100+idx`; snare reverb 200+slot... reverb on slot1 = 202) → midi
  FX + gain envelopes → auto_gain (expect master globalScale ~0.81) →
  CHECKPOINT → export async + poll size → WAV probe. Avoid verify_part on
  long reverb tracks until B1/F4 land; verify via the gain-stage rms/peak
  reports and the final WAV probe instead.
- Engine restart mid-session is safe for saves: the post-restart
  `save_project` calls all succeeded (the crash was the render/save race, not
  save itself).

## Suggested next-step order

1. B1/F1 (crash fix + cancellation) — write a gtest reproducing the
   verify-timeout → save sequence first; follow hdaw-guard plan-first.
2. B6/F2 telemetry + autosave verification (check what `auto-backups/` is).
3. B2 + B3 + B5 generator musicality fixes (each is small, testable via
   existing gtest suites).
4. B4 audition gate (verify temp-probe path works as workaround).
5. F6/F7 ergonomics; F3 kit sampler (design work, pairs with S6/S2/S7).
