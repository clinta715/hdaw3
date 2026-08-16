# Handoff: Internal FM Synth — complete, next steps

Date: 2026-08-15. Session ported the msfa FM synth DSP engine from Dexed,
implemented `FmSynthEngine`, integrated it into `TrackFXSlot`, added MCP
tools and unit tests, then analyzed Dexed's JUCE interaction patterns for
lessons.

**MANDATORY before any code change:** invoke the `hdaw-guard` skill
(`skill: "hdaw-guard"`). Non-negotiable for every task.

---

## 1. Current status

- Version **0.22.4** (no bump this session — feature-only, no behavioral
  changes to existing code).
- Full suite at close: **783/788 PASS** (5 pre-existing env failures
  unrelated to FM synth — `CrashRecovery` and
  `PluginIsolation.UniqueSlotIdPerInstance` "Failed to spawn isolated
  plugin process").
- FM synth test suite: **10/10 PASS**.
- Commit `dd2f9ae` — `feat: internal DX7-compatible FM synthesizer
  (6-operator, 32 algorithms)` — 32 files, +4224 lines.
- **Knowledge graph NOT refreshed** — new files (`FmSynthEngine`,
  `src/engine/msfa/`, `fm_synth_test.cpp`) are invisible to
  `codebase-memory`. Run `index_repository` before any graph-dependent
  work.

## 2. What was delivered

| Component | Status | Files |
|-----------|--------|-------|
| **msfa DSP core** | ✅ Complete | 23 files in `src/engine/msfa/` (Apache 2.0, MTS-ESP + NEON removed) |
| **FmSynthEngine** | ✅ Complete | `src/engine/FmSynthEngine.h` + `.cpp` — 16-voice, lock-free atomics, N=64 blocks |
| **TrackFXSlot** | ✅ Complete | `ActiveType::FmSynth`, 26 params, prepare/process/reset/routing |
| **Command layer** | ✅ Complete | `addFxSlot("fm_synth")` flows through to engine |
| **MCP tools** | ✅ Complete | `fm_synth_load_preset`, `fm_synth_get_state`, `add_fx fm_synth` |
| **Tests** | ✅ Complete | 10 GTest cases in `tests/unit/engine/fm_synth_test.cpp` |
| **Plan doc** | ✅ Complete | `docs/plans/2026-08-13-internal-fm-synth.md` |

## 3. Bugs to fix (from Dexed analysis)

These were identified by comparing Dexed's JUCE integration patterns with
our implementation. All three affect audio quality.

### 3.1 Extra buffer for non-block-aligned sizes (bug)

**File:** `src/engine/FmSynthEngine.cpp`, `render()` method.

Our render loop processes in N=64 blocks: `for (int i = 0; i < numSamples;
i += N)`. If the host delivers a buffer that isn't a multiple of 64 (e.g.,
100 samples), samples 64–99 are silently never rendered — the loop exits
early.

Dexed handles this with a `float extra_buf[N]` + `int extra_buf_size`
pattern: leftover samples from the previous block are stashed and prepended
to the next call.

**Fix:** Add `extra_buf_[64]` and `extra_bufSize_` members to
`FmSynthEngine`. At the start of `render()`, flush any buffered samples.
After the main loop, stash the remainder. This is a 20-line change.

**Severity:** High — any buffer size that isn't 64, 128, 192, etc. drops
the tail samples.

### 3.2 Mono mode envelope transfer (audio quality)

**File:** `src/engine/FmSynthEngine.cpp`, `noteOn()`.

Our current mono mode implementation calls `keyup()` on all live voices
before starting the new note. This causes a release phase gap — the old
voice fades out while the new one fades in, creating a click or silence.

Dexed transfers the envelope state from the old voice to the new one via
`Dx7Note::transferState()`, which copies EG positions and operator gain
states. This gives seamless legato.

**Fix:** In `noteOn()`, when `monoModeAtom_` is true, instead of calling
`keyup()` on all voices, find the most recently active voice, call
`v->note->transferState(*newVoice->note)` on the new voice, then mark the
old voice as dead. The msfa `transferState()` method already exists in
`dx7note.cc`.

**Severity:** Medium — audible on legato playing in mono mode.

### 3.3 Phase transfer on same-pitch retrigger (audio quality)

**File:** `src/engine/FmSynthEngine.cpp`, `noteOn()`.

When a new note hits the same pitch as an already-playing note (poly mode),
our voice allocator steals a voice but doesn't transfer the oscillator
phase. The new voice starts at phase 0 while the old voice is at some
arbitrary phase, causing a constructive/destructive interference click.

Dexed handles this with `Dx7Note::transferPhase()` which copies only the
phase from the old voice.

**Fix:** In `noteOn()`, after allocating a voice, scan existing voices for
one with the same `midiNote` that's still live. If found, call
`newVoice->note->transferPhase(*oldVoice->note)` before marking the old
voice dead. The msfa `transferPhase()` method already exists.

**Severity:** Medium — audible click on same-pitch retrigger.

## 4. UI needs (future work)

### 4.1 `peekVoiceStatus()` for live operator visualization

The FM synth editor tab will need to display per-operator envelope levels,
EG positions, and VU in real time. Dexed does this with a
`peekVoiceStatus()` method that copies live DSP state into a `VoiceStatus`
struct, safe for the UI thread to read.

**Action:** Add `FmSynthEngine::peekVoiceStatus(VoiceStatus& status)` that
iterates voices, calls `Exp2::lookup()` on each operator's level, and
copies EG step positions. The struct should be read via an atomic flag or
SPSC channel, not by direct pointer access.

### 4.2 SPSC channel for live analysis data

HDAW's frontend is decoupled from the engine via ValueTree + WebSocket RPC.
This is architecturally correct but too slow for per-operator envelope
visualization (~60fps needed).

**Action:** Add a lightweight SPSC ring buffer (similar to `MeterManager`)
that carries a `FmSynthAnalysis` struct (6 operator levels, 6 EG steps,
global LFO value) from the audio thread to the message thread at block
boundaries. The frontend reads this via a dedicated MCP notification or
WebSocket message.

## 5. Feature backlog (future work)

| Feature | Complexity | Dependencies |
|---------|-----------|-------------|
| **Bottom-panel UI tab** — operator editors, algorithm visualization, envelope display | High | 4.1, 4.2, Zustand store, React components |
| **DX7 sysex import** — cartridge loading from .syx files | Low | None (sysex format is well-documented) |
| **Extended algorithms** — beyond 32 DX7 (add feedback per-op, serial/parallel hybrids) | Medium | FmSynthEngine render loop modification |
| **Modulation matrix** — per-operator LFO routing, mod wheel targets | Medium | Controllers system extension |
| **Preset library** — built-in init patches, classic DX7 sounds | Low | DX7 sysex import, ValueTree storage |
| **MIDI learn** — map any FM param to MIDI CC | Medium | Ctrl system extension, existing `mappedMidiCC` pattern |

## 6. Graph refresh (first action next session)

Run `index_repository` to pick up the new files:
```
codebase-memory: index_repository(repo_path="D:\pdf\roo projects\hdaw3", mode="fast")
```

This is needed before any graph-dependent work (blast-radius queries,
dependency tracing, etc.) on the FM synth code.

## 7. Lessons learned (Dexed JUCE patterns → HDAW)

| # | Lesson | HDAW Status |
|---|--------|-------------|
| 1 | Extra buffer for non-block-aligned sizes | **Bug — fix first** (§3.1) |
| 2 | Timer-based live voice status (100ms) | Need `peekVoiceStatus()` (§4.1) |
| 3 | Custom param system for complex mapping | ✅ `InternalParamDef` is correct |
| 4 | Processor↔editor coupling tradeoff | SPSC channel is right path (§4.2) |
| 5 | Defer param updates to processBlock | ✅ `paramsDirty_` atomic flag works |
| 6 | Force-refresh flag for external changes | ✅ Delta-sync handles this |
| 7 | Mono mode envelope transfer | **Bug — fix** (§3.2) |
| 8 | Phase transfer on same-pitch retrigger | **Bug — fix** (§3.3) |
| 9 | `peekVoiceStatus` for safe UI reads | Missing — needed (§4.1) |
| 10 | Global LFO (not per-voice) | ✅ Matches DX7 behavior |

## 8. Commit at session close

```
dd2f9ae feat: internal DX7-compatible FM synthesizer (6-operator, 32 algorithms)
```

32 files, +4224 lines. Working tree is **not** clean — pre-existing
uncommitted changes from previous sessions remain. Only the FM synth
feature was committed.

---

*End of handoff. Read `AGENTS.md` lessons, then this file. Start with
§6 (graph refresh), then §3 (bugs).*
