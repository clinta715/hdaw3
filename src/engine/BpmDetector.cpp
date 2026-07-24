#include "BpmDetector.h"
#include <aubio.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace HDAW
{
    BpmDetector::Result BpmDetector::detect(const float* samples, int numSamples,
                                            double sampleRate, double maxSeconds)
    {
        Result result;
        if (samples == nullptr || numSamples <= 0 || sampleRate <= 0.0)
            return result;

        const int maxSamples = static_cast<int>(maxSeconds * sampleRate);
        const int n = (std::min)(numSamples, maxSamples);

        float maxAbs = 0.0f;
        for (int i = 0; i < n; ++i)
            maxAbs = (std::max)(maxAbs, std::abs(samples[i]));
        if (maxAbs < 1e-6f)
            return result;

        const uint_t buffer_size = 1024;
        const uint_t hop_size = 512;

        aubio_tempo_t* tempo = new_aubio_tempo("default", buffer_size, hop_size,
                                               static_cast<uint_t>(sampleRate));
        if (tempo == nullptr)
            return result;

        fvec_t* vec = new_fvec(hop_size);
        fvec_t* out = new_fvec(1);
        std::vector<double> beatTimes;

        int pos = 0;
        while (pos < n)
        {
            int remaining = n - pos;
            int chunk = (remaining < hop_size) ? remaining : hop_size;

            for (int i = 0; i < hop_size; ++i)
            {
                if (i < chunk)
                    fvec_set_sample(vec, samples[pos + i], i);
                else
                    fvec_set_sample(vec, 0.0f, i);
            }

            aubio_tempo_do(tempo, vec, out);

            if (aubio_tempo_get_last_s(tempo) != 0.0f)
                beatTimes.push_back(static_cast<double>(aubio_tempo_get_last_s(tempo)));

            pos += chunk;
        }

        del_fvec(vec);
        del_fvec(out);
        del_aubio_tempo(tempo);

        if (beatTimes.size() < 2)
            return result;

        std::vector<double> intervals;
        for (size_t i = 1; i < beatTimes.size(); ++i)
            intervals.push_back(beatTimes[i] - beatTimes[i - 1]);

        if (intervals.empty())
            return result;

        struct Bin { double bpm; int count; };
        std::vector<Bin> bins;

        for (double iv : intervals)
        {
            if (iv <= 0.0) continue;
            double bpm = 60.0 / iv;
            if (bpm < 40.0 || bpm > 220.0) continue;

            double rounded = std::round(bpm);
            bool found = false;
            for (auto& b : bins)
            {
                if (std::abs(b.bpm - rounded) < 2.5)
                {
                    b.count++;
                    found = true;
                    break;
                }
            }
            if (!found)
                bins.push_back({ rounded, 1 });
        }

        if (bins.empty())
            return result;

        auto best = std::max_element(bins.begin(), bins.end(),
            [](const Bin& a, const Bin& b) { return a.count < b.count; });

        result.bpm = best->bpm;
        result.confidence = static_cast<double>(best->count) /
                            static_cast<double>(intervals.size());
        return result;
    }
}