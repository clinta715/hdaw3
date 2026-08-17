# Handoff: FM synth audio-quality fixes — all 3 Dexed-analysis bugs closed

Date: 2026-08-17. Session implemented the fixes from the Dexed JUCE-pattern
analysis (originally tracked in the archived
`2026-08-15-internal-fm-synth-session.md` handoff): partial-block sample
skipping, mono-mode release gap, same-pitch retrigger click, plus
`peekVoiceStatus()` as the UI/test foundation.

**MANDATORY before any code change:** invoke the `hdaw-guard` skill
(`skill: "hdaw-guard"`). Non-negotiable for every task.

---

## 1. Current status

- Full suite at close: **871/872 PASS** (1 failure:
  `RenderSequenceRelease.RebuildReleasesPreviousGraphChildren` — verified
  **flaky**, passes 2/2 in isolation; plugin-isolation domain untouched by
  this session's changes; timing-sensitive child-process polling under
  full-suite load).
- FM synth suite: **16/16 PASS** (10 original + 6 new this session).
- Knowledge graph refreshed (fast mode) at session start AND after all
  changes (completion contract item 8).
- Not version-bumped (bug fixes to a feature not yet in a release train —
  bump with the next feature batch if desired).

## 2. Commits this session

| Commit | Contents |
|--------|----------|
| `26f3885` | **fix(fm): carry partial-block samples across render calls** — Dexed extra-buffer pattern. `extra_buf_[64]`/`extra_buf_size_` members, flush → full-blocks → tail-stash structure, `computeBlock()` factors per-voice loop (identical accumulation order = bit-identical chunked output). Tests: `render(100)×3 ≡ render(300)` and `render(16)×8 ≡ render(128)` bit-identical (FAILED pre-fix at sample 114/16, PASS post-fix) |
| `030ff29` | **feat(fm): peekVoiceStatus snapshot** — `FmSynthEngine::FmVoiceStatus {amp[6], ampStep[6], pitchStep}`, picks live voice with highest keydownSeq (keydown preferred). Lock-free display-grade read (Dexed contract). msfa `VoiceStatus`/`peekVoiceStatus` had survived the port — wrapper only, no re-port |
| `0f779a5` | **fix(fm): transfer oscillator phase on same-pitch retrigger** — poly path scans for live voice at same pitch after `init()`, calls `transferPhase()`. Test accessors: `Dx7Note::getOpPhaseForTest`, `FmSynthEngine::getVoicePhaseForTest`/`getVoiceMidiNoteForTest`. Test: all 6 op phases equal after retrigger (FAILED pre-fix op 5: 21888 vs 7296) |
| `761904f` | **fix(fm): mono-mode legato via transferState** — mono path finds first live voice; keydown → `transferState` (full legato), releasing → `transferSignal`; old voice retired WITHOUT `keyup()`. `noteOff()` untouched (minimal scope). Slow-attack test patch (rate 30) makes the buggy restart land at ~0.003× sustain (300× discrimination margin) |

### 2.1 Implementation notes worth keeping

- **Allocator interaction (Task D deviation):** the old mono voice must
  stay `live` THROUGH `allocateVoice()` and be retired only after the
  post-init transfer — marking it dead first frees the slot, the allocator
  reuses it, and `transferState` self-transfers the freshly-init'd note
  (no-op). Guard `legatoSrc != v` covers the all-slots-full steal case.
- **Phase-equality test determinism:** both voices at the same pitch have
  identical `freq` → phase advance in lockstep, so equality holds at
  comparison time. Use block sizes that are multiples of 64 for these
  assertions (extra_buf interplay otherwise).
- **Envelope-restart test calibration:** the default init patch attacks
  nearly instantly (rate 99) — a restart sneaks past a `/2` amp check.
  Load a slow-attack patch (carrier rate 30 ≈ 62k samples) and render
  ~100k samples to the sustain plateau for a robust 300× margin.

## 3. Remaining from the original FM synth backlog

| Item | Status |
|------|--------|
| §4.2 SPSC analysis channel (live operator levels → frontend) | **Deferred** until the UI tab session (no consumer yet; peekVoiceStatus is the foundation) |
| Bottom-panel UI tab (operator editors, algorithm display) | Pending — needs §4.2 |
| DX7 sysex cartridge import (.syx) | Pending — low complexity |
| Full Dexed mono key-stack (return-to-highest-held on release) | **Deferred** — minimal legato shipped; noteOff() unchanged by design |
| Extended algorithms / modulation matrix / preset library / MIDI learn | Pending |

## 4. Watch items

- `RenderSequenceRelease.RebuildReleasesPreviousGraphChildren` flaked once
  under full-suite load (passes isolated). If it recurs, consider bumping
  its poll budget or splitting it into its own gtest shard.
- The `compositions/` directory and several `polyrhythm_drums_*.hdaw3`
  project files in the repo root are untracked user content — leave them
  alone.

## 5. First actions next session

1. `skill: "hdaw-guard"` (always).
2. If continuing FM work: the UI tab is the big rock — start with the
   SPSC analysis channel (§3 row 1), then the React tab in the bottom
   panel (Bitwig/Ableton fixed-tile idiom, no floating windows).
3. MCP parity note: `fm_synth_get_state` could expose
   `peekVoiceStatus` amps once the UI consumes them.

---

*End of handoff. Read `AGENTS.md` lessons first, then this file.*
