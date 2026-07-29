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

        for (size_t i = 0; i + 1 < clips.size(); ++i)
        {
            const auto& a = clips[i];
            const auto& b = clips[i + 1];
            double aEnd = a.startSec + a.durationSec;
            double bStart = b.startSec;
            double gap = bStart - aEnd; // negative = overlap, zero = touching, positive = gap

            double crossfadeLen = 0.0;
            if (gap < -1e-6)
            {
                // Overlapping: crossfade over the full overlap region.
                crossfadeLen = -gap;
            }
            else if (gap <= defaultCrossfadeSec + 1e-6)
            {
                // Adjacent (touching or within defaultCrossfadeSec): use default length.
                // Clamp to available fade space on each clip.
                crossfadeLen = std::min({ defaultCrossfadeSec, a.durationSec, b.durationSec });
            }
            else
            {
                // Too far apart: no crossfade.
                continue;
            }

            if (crossfadeLen <= 1e-6) continue;

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
