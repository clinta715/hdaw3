# Sampler Loop Crossfade Precompute

Date: 2026-08-15. Handoff item #3 from
`docs/handoffs/2026-08-15-drain-seam-and-docs-archive.md` §3.
Subsystem E from `docs/plans/2026-08-13-hise-derived-features-master-plan.md`.

## Goal

Sampler loop hard-switches at `loopEnd` → `loopStart`
(`SamplerVoice::render` :193-194), causing an audible click on
non-zero-crossing loops. Fix: precompute a short equal-power crossfade
(8 ms default) straddling `loopEnd`→`loopStart` at load time (message
thread), stored on the immutable `SamplerSound`; the voice blends
across it on each loop pass. No audio-thread allocation — the crossfade
table is part of the sound.

## Success Gates

- [ ] G1: Loop crossfade continuity — sample immediately before loopEnd
      blends to loopStart without discontinuity. Assert no >-20 dB jump
      at the wrap point (vs the current hard-switch which can hit 0 dB).
- [ ] G2: Existing sampler suites pass (`SamplerVoice.*`, `SamplerSound.*`,
      `SamplerEngine.*`, `SamplerFxSlot.*`, `SamplerInterpolator.*`).
- [ ] G3: Full `hdaw_tests.exe` passes (no regressions).
- [ ] G4: Build clean, no new anti-patterns.

## Dependency Map

- Blast radius: `SamplerSound.h` (new crossfade fields + Builder
  precompute), `SamplerVoice.h` (render loop near loopEnd).
- Upstream: `SamplerSound::Builder::build()` constructs the sound
  (message thread); `SamplerEngine` holds `shared_ptr<const SamplerSound>`.
- Downstream: `SamplerVoice::render()` reads from the crossfade buffer
  on the audio thread.
- Projections affected: none (immutable sound data, no ValueTree change).
- SPSC paths touched: none new (crossfade buffer built at load time,
  read-only on audio thread — same as `data[]`).
- God nodes: none (SamplerSound/SamplerVoice are leaf-level DSP classes).
- Gate 3: crossfade read is a simple float pointer deref + multiply —
  no alloc/lock/IO on audio thread. Verified by existing `audio-dsp-review`
  on the sampler.

## Pitfall Gates Triggered

- Gate 3 (audio-thread safety): render() must remain noexcept, no
  allocation. The crossfade read is a pointer deref + blend — safe.
- Gate 10 (rebuild restore): the crossfade buffer is part of the
  immutable SamplerSound, which survives rebuilds via the block-boundary
  swap (already tested). No new restore path needed.
- Lesson 8 (quality): equal-power crossfade (linear blend of
  sqrt-cos-weighted signals) preserves perceived loudness across the
  transition. The existing Lagrange interpolator handles fractional
  positions within the crossfade buffer.

## Design

### SamplerSound additions (`src/engine/SamplerSound.h`)

```cpp
// Crossfade buffer for seamless loop transitions. Precomputed at build
// time when loopEnabled is true. The buffer contains 2 * fadeLen samples:
// [0..fadeLen-1] = end-of-loop region (fading out),
// [fadeLen..2*fadeLen-1] = start-of-loop region (fading in).
// When the voice's readPos enters the crossfade zone
// [loopEndFrame - fadeLen, loopEndFrame + fadeLen), it reads from this
// buffer linearly instead of the original data, producing a smooth
// equal-power transition.
const float* crossfadeData[2] = { nullptr, nullptr };
int64_t crossfadeLength = 0;       // 2 * fadeLen (0 when no crossfade)
int64_t crossfadeFadeLen = 0;      // fadeLen in samples
int64_t crossfadeZoneStart = 0;    // loopEndFrame - fadeLen (start of zone)
static constexpr double kCrossfadeMs = 8.0; // crossfade duration
```

### SamplerSoundStorage additions

```cpp
std::unique_ptr<float[]> crossfadeOwned[2]; // owns crossfade buffer memory
```

### Builder::build() — crossfade precompute

When `loopEnabled` is true and the loop is long enough (> 4 samples):
1. Compute `fadeLen = (int64_t)(kCrossfadeMs / 1000.0 * nativeSampleRate)`.
2. Clamp `fadeLen` so the crossfade zone fits within the loop
   (`fadeLen = min(fadeLen, loopLen / 4)` — the zone is `2*fadeLen` and
   must stay within the loop).
3. Allocate `crossfadeOwned[ch]` of size `2 * fadeLen` per channel.
4. For each sample `i` in `[0, 2*fadeLen)`:
   - `alpha = i / (2.0 * fadeLen - 1)` (0→1 linear).
   - Source A (end of loop): `data[ch][loopEndFrame - fadeLen + i]` with
     wrap via modulo if needed for short loops.
   - Source B (start of loop): `data[ch][(loopStartFrame + i) % loopLen
     + loopStartFrame]` — wrapped within the loop.
   - `crossfade[ch][i] = A * (1 - alpha) + B * alpha`.
5. Set `crossfadeData[ch]`, `crossfadeLength`, `crossfadeFadeLen`,
   `crossfadeZoneStart`.

### SamplerVoice::render() — crossfade blend

In the loop-handling block (`:189-197`), before the existing hard-jump
logic, check if the voice is in the crossfade zone:

```cpp
if (looping && sound_->crossfadeLength > 0)
{
    const double zoneStart = static_cast<double>(sound_->crossfadeZoneStart);
    const double zoneEnd = zoneStart + static_cast<double>(sound_->crossfadeLength);
    if (readPos_ >= zoneStart && readPos_ < zoneEnd)
    {
        // Read from crossfade buffer (linear, no Lagrange — the buffer
        // is already the blended result).
        const int64_t idx = static_cast<int64_t>(readPos_ - zoneStart);
        // ... read crossfadeData[ch][idx] instead of data[ch][readPos_]
    }
}
```

The crossfade read replaces the normal Lagrange read for samples within
the zone. Outside the zone, normal reading continues. The existing
hard-jump (`readPos_ = loopStartF + (readPos_ - loopEndF)`) still
handles wrapping — the crossfade buffer's last sample aligns with
`loopStartF`, so the wrap is seamless.

### Reverse playback

For reverse (`dir_ < 0`), the crossfade zone mirrors:
`[loopStartFrame - fadeLen, loopStartFrame + fadeLen)`. The crossfade
buffer is the same (end→start blend), but the voice reads it in reverse
by mapping `readPos` to a reversed index into the buffer.

### Edge cases

- Loop too short for crossfade (< 4 samples): skip crossfade (no buffer
  allocated, `crossfadeLength == 0`), fall back to hard-jump.
- Crossfade zone extends past sample boundaries: clamp source indices
  to `[0, length)` with silence beyond.
- Non-looping sounds: no crossfade buffer allocated.
- OneShot/Slice mode: crossfade only applies to Classic looping.

## Steps

1. Add crossfade fields to SamplerSound + SamplerSoundStorage.
2. Implement crossfade precompute in Builder::build().
3. Modify SamplerVoice::render() to read from crossfade buffer in the zone.
4. Add gtest `LoopCrossfade.*` suite: continuity test (no >-20 dB jump
   at wrap), reverse crossfade, short-loop fallback, non-looping unaffected.
5. Verify existing sampler suites pass.
6. Full suite verification.
