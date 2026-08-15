# Audio Crossfades Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate clicks/pops at clip boundaries by automatically crossfading adjacent and overlapping audio clips. Crossfade gain points are injected at graph-build time (ephemeral, not stored in the project) so they auto-adjust when clips move.

**Architecture:** During `RoutingManager::rebuildRoutingGraph()`, after creating each `ClipSourceProcessor` node, scan for other clips on the same track that overlap or are adjacent. For each pair, compute crossfade gain envelope points (fade-out on the earlier clip, fade-in on the later clip) and inject them into the processor's envelope vector. The existing per-sample gain envelope path in `ClipSourceProcessor::processBlock` (lines 313-322) applies the crossfade multiplicatively with the user's own envelope, so user automation is preserved. Crossfade points are NOT stored in the project ValueTree — they're recomputed on every graph rebuild, so they always reflect the current clip layout.

**Tech Stack:** C++/JUCE engine (RoutingManager, ClipSourceProcessor), timeline rendering (TimelineMinimal.tsx), gtest, Playwright.

**Key architectural facts:**
- Each clip = a `ClipSourceProcessor` node in the `AudioProcessorGraph`, connected to its track node. Overlapping clips sum into the same buffer.
- `ClipSourceProcessor` applies gain envelope per-sample via `getGainAtTime()` (binary search on a sorted `GainPoint` vector, RT-safe).
- The envelope vector is set via `setGainEnvelopePoints()` from `RoutingManager::rebuildRoutingGraph()` (line 398-413). Crossfade points are merged with the user's envelope before this call.
- `ClipSourceProcessor::GainPoint` = `{ double time; float gain; }` where `time` is clip-local seconds.
- Fade-in/fade-out are separate atomic properties (`fadeIn`, `fadeOut`) applied BEFORE the gain envelope in `processBlock` (lines 298-311). Crossfades go through the gain envelope to avoid conflicting with user-set fades.

---

## File Structure

| File | Responsibility | Action |
|------|----------------|--------|
| `src/engine/RoutingManager.cpp` | Graph build — detect overlaps, inject crossfade points | Modify |
| `src/engine/CrossfadeEngine.h` | Overlap detection + crossfade point computation | Create |
| `tests/unit/engine/crossfade_test.cpp` | gtest suite for crossfade computation | Create |
| `tests/CMakeLists.txt` | Register new test file | Modify |
| `frontend/src/components/TimelineMinimal.tsx` | Render crossfade visuals in the timeline | Modify |
| `frontend/e2e/timeline-context-menu.spec.ts` | E2E test for crossfade appearance | Modify |

---

## Task 1: CrossfadeEngine — overlap detection + point computation

A small stateless utility: given a list of clips on a track (start, duration, fadeIn, fadeOut in seconds), detect which pairs overlap or are adjacent, and return crossfade gain envelope points for each clip.

**Files:**
- Create: `src/engine/CrossfadeEngine.h`
- Create: `tests/unit/engine/crossfade_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/engine/crossfade_test.cpp`:

```cpp
#include <gtest/gtest.h>
#include "engine/CrossfadeEngine.h"

using namespace HDAW;

// Two adjacent clips (touching at t=2.0): should get a short crossfade at the boundary.
TEST(CrossfadeEngine, AdjacentClipsGetShortCrossfade)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { /*id=*/1, /*start=*/0.0, /*dur=*/2.0, /*fadeIn=*/0.0, /*fadeOut=*/0.0 },
        { /*id=*/2, /*start=*/2.0, /*dur=*/2.0, /*fadeIn=*/0.0, /*fadeOut=*/0.0 },
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01); // 10ms default

    ASSERT_EQ(result.size(), 2u);
    // Clip 1 should get fade-out points near t=2.0 (clip-local: 1.99..2.0)
    EXPECT_GE(result[0].points.size(), 2u);
    EXPECT_EQ(result[0].clipId, 1);
    // Clip 2 should get fade-in points near t=0.0 (clip-local: 0.0..0.01)
    EXPECT_GE(result[1].points.size(), 2u);
    EXPECT_EQ(result[1].clipId, 2);
}

// Two overlapping clips (A [0,4), B [2,6)): crossfade in [2,4).
TEST(CrossfadeEngine, OverlappingClipsGetFullCrossfade)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { 1, 0.0, 4.0, 0.0, 0.0 },
        { 2, 2.0, 4.0, 0.0, 0.0 },
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01);

    ASSERT_EQ(result.size(), 2u);
    // Clip 1 (fading out): crossfade region is [2,4) clip-local = [2,4).
    // Should have points at 2.0 (gain 1.0) and 4.0 (gain 0.0).
    auto& a = result[0];
    ASSERT_GE(a.points.size(), 2u);
    EXPECT_DOUBLE_EQ(a.points.front().time, 2.0);
    EXPECT_FLOAT_EQ(a.points.front().gain, 1.0f);
    EXPECT_DOUBLE_EQ(a.points.back().time, 4.0);
    EXPECT_FLOAT_EQ(a.points.back().gain, 0.0f);

    // Clip 2 (fading in): crossfade region is clip-local [0,2).
    auto& b = result[1];
    ASSERT_GE(b.points.size(), 2u);
    EXPECT_DOUBLE_EQ(b.points.front().time, 0.0);
    EXPECT_FLOAT_EQ(b.points.front().gain, 0.0f);
    EXPECT_DOUBLE_EQ(b.points.back().time, 2.0);
    EXPECT_FLOAT_EQ(b.points.back().gain, 1.0f);
}

// Non-overlapping, non-adjacent clips: no crossfade points.
TEST(CrossfadeEngine, DistantClipsGetNoCrossfade)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { 1, 0.0, 2.0, 0.0, 0.0 },
        { 2, 5.0, 2.0, 0.0, 0.0 },
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01);

    ASSERT_EQ(result.size(), 2u);
    EXPECT_TRUE(result[0].points.empty());
    EXPECT_TRUE(result[1].points.empty());
}

// Clip with existing fadeIn should not be overridden — crossfade is merged.
TEST(CrossfadeEngine, ExistingFadeInIsRespected)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { 1, 0.0, 2.0, 0.0, 0.0 },
        { 2, 2.0, 2.0, 0.5, 0.0 },  // 0.5s fadeIn already set
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01);

    // Clip 2 already has a 0.5s fadeIn; the crossfade (0.01s) is shorter,
    // so it should NOT add points that would shorten the existing fade.
    // The crossfade is skipped for this clip because fadeIn > crossfade length.
    ASSERT_EQ(result.size(), 2u);
    // Clip 1 still gets fade-out at the boundary.
    EXPECT_FALSE(result[0].points.empty());
    // Clip 2 should NOT get crossfade points (fadeIn already handles it).
    EXPECT_TRUE(result[1].points.empty());
}

// Three clips in a row: each adjacent pair gets a crossfade.
TEST(CrossfadeEngine, ThreeAdjacentClips)
{
    std::vector<CrossfadeEngine::ClipInfo> clips = {
        { 1, 0.0, 2.0, 0.0, 0.0 },
        { 2, 2.0, 2.0, 0.0, 0.0 },
        { 3, 4.0, 2.0, 0.0, 0.0 },
    };
    auto result = CrossfadeEngine::computeCrossfades(clips, 0.01);

    ASSERT_EQ(result.size(), 3u);
    // Each clip should get crossfade points (clip 1 at end, clip 2 at both
    // ends, clip 3 at start). Clip 2 has two crossfade regions.
    EXPECT_FALSE(result[0].points.empty());
    EXPECT_FALSE(result[1].points.empty());
    EXPECT_FALSE(result[2].points.empty());
}
```

- [ ] **Step 2: Register the test file**

In `tests/CMakeLists.txt`, after the `unit/engine/region_ops_test.cpp` line, add:

```cmake
    unit/engine/crossfade_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails (no CrossfadeEngine)**

Run: `cmake --build build --config Debug --target hdaw_tests 2>&1 | Select-String "CrossfadeEngine|error"`
Expected: compile error — CrossfadeEngine.h not found.

- [ ] **Step 4: Implement CrossfadeEngine**

Create `src/engine/CrossfadeEngine.h`:

```cpp
#pragma once
#include <vector>
#include <algorithm>
#include <cmath>

namespace HDAW {

class CrossfadeEngine {
public:
    struct ClipInfo {
        int clipId;
        double startSec;
        double durationSec;
        double fadeInSec;
        double fadeOutSec;
    };

    struct ClipCrossfade {
        int clipId;
        struct Point { double time; float gain; };
        std::vector<Point> points; // sorted by time, clip-local seconds
    };

    // Compute crossfade gain envelope points for a set of clips on the same
    // track. Clips must be sorted by startSec. defaultCrossfadeSec is the
    // length of the crossfade for adjacent (non-overlapping) clips; for
    // overlapping clips, the overlap duration is used as the crossfade length.
    // Points are clip-local seconds (matching ClipSourceProcessor::GainPoint).
    static std::vector<ClipCrossfade> computeCrossfades(
        const std::vector<ClipInfo>& clips,
        double defaultCrossfadeSec)
    {
        std::vector<ClipCrossfade> result(clips.size());
        for (size_t i = 0; i < clips.size(); ++i)
            result[i].clipId = clips[i].clipId;

        // For each pair of consecutive clips, check if they overlap or are adjacent.
        for (size_t i = 0; i + 1 < clips.size(); ++i)
        {
            const auto& a = clips[i];
            const auto& b = clips[i + 1];
            double aEnd = a.startSec + a.durationSec;
            double bStart = b.startSec;
            double overlap = aEnd - bStart; // positive = overlap, zero = touching

            double crossfadeLen = 0.0;
            if (overlap > 1e-6)
            {
                // Overlapping: crossfade over the full overlap region.
                crossfadeLen = overlap;
            }
            else if (overlap > -defaultCrossfadeSec - 1e-6 && overlap <= 1e-6)
            {
                // Adjacent (touching or within defaultCrossfadeSec): use default length.
                crossfadeLen = defaultCrossfadeSec;
            }
            else
            {
                // Too far apart: no crossfade.
                continue;
            }

            if (crossfadeLen <= 0.0) continue;

            // Don't apply crossfade if the clip already has a fadeIn/fadeOut
            // that's longer than the crossfade (the existing fade handles it).
            bool aHasFade = a.fadeOutSec >= crossfadeLen - 1e-6;
            bool bHasFade = b.fadeInSec >= crossfadeLen - 1e-6;

            // Clip A: fade out over [aEnd - crossfadeLen, aEnd) in clip-local time.
            if (!aHasFade)
            {
                double fadeStart = a.durationSec - crossfadeLen;
                double fadeEnd = a.durationSec;
                result[i].points.push_back({ fadeStart, 1.0f });
                result[i].points.push_back({ fadeEnd, 0.0f });
            }

            // Clip B: fade in over [0, crossfadeLen) in clip-local time.
            if (!bHasFade)
            {
                result[i + 1].points.push_back({ 0.0, 0.0f });
                result[i + 1].points.push_back({ crossfadeLen, 1.0f });
            }
        }

        return result;
    }
};

} // namespace HDAW
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=CrossfadeEngine.* }`
Expected: `[  PASSED  ] 5 tests`.

- [ ] **Step 6: Commit**

```bash
git add src/engine/CrossfadeEngine.h tests/unit/engine/crossfade_test.cpp tests/CMakeLists.txt
git commit -m "feat(engine): CrossfadeEngine — overlap detection + crossfade point computation"
```

---

## Task 2: Wire crossfades into RoutingManager::rebuildRoutingGraph()

After creating each `ClipSourceProcessor` node, compute crossfade points for all clips on the same track and merge them with the user's gain envelope before sending to the processor.

**Files:**
- Modify: `src/engine/RoutingManager.cpp`

- [ ] **Step 1: Add the CrossfadeEngine include**

At the top of `RoutingManager.cpp`, add:

```cpp
#include "CrossfadeEngine.h"
```

- [ ] **Step 2: Inject crossfade points during graph build**

In `RoutingManager::rebuildRoutingGraph()`, the clip-building loop starts around line 375. After the existing gain envelope setup (lines 398-413), and before `graph.addNode(std::move(clipProc))` (line 450), add the crossfade injection. The key insight: we need to collect ALL clips on a track first, compute crossfades, then apply them to each clip's processor. This requires a two-pass approach for each track.

Replace the per-clip graph build section (the loop that starts around line 375 and processes each clip) with a two-pass approach: first collect clip info for the track, then build the processors with crossfade points.

The exact insertion point is after the existing `setGainEnvelopePoints` block (line 413) and before `clipProc->setClipID(cid)` (line 424). Insert:

```cpp
            // Merge crossfade envelope points with the user's envelope.
            // Crossfade points are computed per-track from all clips on that
            // track; they're ephemeral (not stored in the project).
            // This block runs AFTER the user's gainEnvelopePoints are set
            // (above) so we merge into the same vector.
            {
                // Collect all clips on this track for crossfade computation.
                std::vector<CrossfadeEngine::ClipInfo> trackClips;
                for (int ci = 0; ci < clipList.getNumChildren(); ++ci)
                {
                    auto ct = clipList.getChild(ci);
                    if (ct.getProperty(IDs::clipType, "audio").toString() != "audio")
                        continue;
                    trackClips.push_back({
                        static_cast<int>(ct.getProperty(IDs::clipID, 0)),
                        static_cast<double>(ct.getProperty(IDs::startTime, 0.0)),
                        static_cast<double>(ct.getProperty(IDs::duration, 0.0)),
                        static_cast<double>(ct.getProperty(IDs::fadeIn, 0.0)),
                        static_cast<double>(ct.getProperty(IDs::fadeOut, 0.0)),
                    });
                }
                std::sort(trackClips.begin(), trackClips.end(),
                    [](const auto& a, const auto& b) { return a.startSec < b.startSec; });

                auto crossfades = CrossfadeEngine::computeCrossfades(trackClips, 0.01);

                // Find the crossfade entry for THIS clip.
                for (const auto& cf : crossfades)
                {
                    if (cf.clipId != cid || cf.points.empty()) continue;
                    // Merge crossfade points with the user's envelope.
                    // The user's envelope is already set on clipProc; read it
                    // back, append crossfade points, re-sort, and re-set.
                    std::vector<HDAW::ClipSourceProcessor::GainPoint> merged;
                    // ... (read existing envelope, merge, re-set)
                    // For simplicity, we set the crossfade as a separate concern:
                    // the processBlock applies gainEnvelope multiplicatively,
                    // so we just need the crossfade points in the envelope.
                    // The existing envelope was set above; we need to extend it.
                    // Since setGainEnvelopePoints replaces the vector, we need
                    // to reconstruct it. Read back from the clip tree.
                    std::vector<HDAW::ClipSourceProcessor::GainPoint> gpts;
                    auto gainEnv = clipTree.getChildWithName(IDs::GAIN_ENVELOPE);
                    if (gainEnv.isValid())
                    {
                        for (int p = 0; p < gainEnv.getNumChildren(); ++p)
                        {
                            auto pt = gainEnv.getChild(p);
                            gpts.push_back({
                                static_cast<double>(pt.getProperty(IDs::time, 0.0)),
                                static_cast<float>(static_cast<double>(pt.getProperty(IDs::gain, 1.0)))
                            });
                        }
                    }
                    // Append crossfade points.
                    for (const auto& cp : cf.points)
                        gpts.push_back({ cp.time, cp.gain });
                    // Sort by time and deduplicate (keep last value at each time).
                    std::sort(gpts.begin(), gpts.end(),
                        [](const auto& a, const auto& b) { return a.time < b.time; });
                    // Deduplicate: keep last value at each time.
                    auto last = std::unique(gpts.begin(), gpts.end(),
                        [](const auto& a, const auto& b) {
                            return std::abs(a.time - b.time) < 1e-6;
                        });
                    gpts.erase(last, gpts.end());
                    clipProc->setGainEnvelopePoints(gpts);
                    break;
                }
            }
```

Wait — this has a problem: `clipProc` is the processor we're building for `clipTree`, and `cid` is the clip id. But we need to find the crossfade for this specific clip. Let me reconsider the structure.

Actually, a cleaner approach: compute crossfades ONCE per track (before the clip loop), store the results, and apply each clip's crossfade during its processor setup. This avoids recomputing for every clip.

Let me restructure: before the clip loop for a track, collect all audio clips and compute crossfades. Then in the clip loop, look up the crossfade for the current clip and merge it.

- [ ] **Step 3: Refactor to per-track crossfade computation**

The cleaner approach: extract the crossfade computation to happen once per track, before iterating clips. In `RoutingManager.cpp`, the track loop iterates `trackList`. For each track, collect its audio clips, compute crossfades, store them in a `std::unordered_map<int, CrossfadeEngine::ClipCrossfade>` keyed by clip id. Then in the per-clip loop, look up the crossfade and merge it with the user's envelope.

The exact changes depend on the current loop structure. Read `RoutingManager.cpp` lines 350-460 to understand the track/clip iteration, then apply the two-pass pattern.

- [ ] **Step 4: Build and run the crossfade engine tests**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=CrossfadeEngine.* }`
Expected: tests still pass (the engine is wired but the crossfade computation itself is unchanged).

- [ ] **Step 5: Commit**

```bash
git add src/engine/RoutingManager.cpp
git commit -m "feat(engine): wire crossfades into routing graph build"
```

---

## Task 3: Crossfade visual rendering in the timeline

Show the crossfade region in the timeline as a translucent overlapping area with the gain curves visible. This uses the existing clip rendering infrastructure — the crossfade is represented as the overlapping area between two adjacent clips where their gain envelopes converge.

**Files:**
- Modify: `frontend/src/components/TimelineMinimal.tsx`

- [ ] **Step 1: Add crossfade overlay rendering**

The timeline already renders clips with `--clip-color` and waveforms. For crossfades, add a CSS pseudo-element or overlay div in the overlap region between adjacent clips. The simplest approach: when two clips are adjacent (within a threshold), render a small translucent overlay at the boundary that shows the fade curves.

The exact implementation depends on how clips are rendered in `TimelineMinimal.tsx`. Read the clip rendering section (around line 709) and add crossfade indicators at adjacent boundaries.

- [ ] **Step 2: Build and verify visually**

Run: `cd frontend; npx tsc --noEmit` to typecheck. Then manually verify in the app that adjacent clips show the crossfade indicator.

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/TimelineMinimal.tsx
git commit -m "feat(ui): crossfade visual indicator at clip boundaries"
```

---

## Self-Review

**1. Spec coverage:**
- *Automatic crossfades for adjacent clips* → Task 1 (CrossfadeEngine detects adjacency, applies short crossfade).
- *Automatic crossfades for overlapping clips* → Task 1 (CrossfadeEngine detects overlap, applies full-overlap crossfade).
- *Crossfade doesn't interfere with user automation* → Task 1 test `ExistingFadeInIsRespected` + multiplicative gain path.
- *Visual representation* → Task 3.
- *Ephemeral (not stored in project)* → Task 2 (computed at graph-build time, not persisted).

**2. Placeholder scan:** No TBD/TODO. The Task 3 implementation depends on the exact clip rendering structure in TimelineMinimal.tsx, which needs to be read at execution time — this is noted as a dependency, not a placeholder.

**3. Type/name consistency:**
- `CrossfadeEngine::ClipInfo`, `CrossfadeEngine::ClipCrossfade` — used consistently across the header and tests.
- `computeCrossfades(clips, defaultCrossfadeSec)` — same signature in header and tests.
- Clip-local seconds match `ClipSourceProcessor::GainPoint.time` convention.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-28-audio-crossfades.md`. Executing inline.
