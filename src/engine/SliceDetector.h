#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace HDAW {

class SliceDetector
{
public:
    // Grid: divide region [0, len) into slices of `gridBeats` at given bpm/sampleRate.
    // Returns sorted frame indices including 0 and len.
    static std::vector<int64_t> grid (int64_t len, double sr, double bpm, double gridBeats)
    {
        std::vector<int64_t> pts;
        if (bpm <= 0.0 || gridBeats <= 0.0 || sr <= 0.0 || len <= 0)
            return pts;

        const double beatsTotal = (static_cast<double> (len) / sr) * (bpm / 60.0);
        const int count = std::max (1, static_cast<int> (std::round (beatsTotal / gridBeats)));

        for (int i = 0; i <= count; ++i)
            pts.push_back (static_cast<int64_t> (static_cast<double> (i) * len / count));

        if (! pts.empty())
            pts.back() = len;

        return pts;
    }

    // Transient: envelope-follow |x|, pick peaks above thresh*max with min spacing.
    // Always includes frame 0 and len as boundaries.
    static std::vector<int64_t> transient (const std::vector<float>& x, double sensitivity)
    {
        std::vector<int64_t> pts { 0 };
        if (x.empty())
            return pts;

        double maxv = 0.0;
        for (auto v : x)
            maxv = std::max (maxv, std::fabs (static_cast<double> (v)));

        if (maxv <= 1e-9)
            return pts;

        const double thresh = (1.0 - sensitivity) * maxv * 0.5;
        const int64_t minGap = std::max (static_cast<int64_t> (1),
                                         static_cast<int64_t> (x.size() / 64));
        double env = 0.0;
        int64_t last = -minGap;

        for (size_t i = 1; i < x.size(); ++i)
        {
            env = 0.999 * env + 0.001 * std::fabs (static_cast<double> (x[i]));
            if (env > thresh
                && static_cast<int64_t> (i) - last >= minGap
                && std::fabs (static_cast<double> (x[i])) > std::fabs (static_cast<double> (x[i - 1])))
            {
                pts.push_back (static_cast<int64_t> (i));
                last = static_cast<int64_t> (i);
                env = 0.0;
            }
        }

        pts.push_back (static_cast<int64_t> (x.size()));
        std::sort (pts.begin(), pts.end());
        return pts;
    }
};

} // namespace HDAW
