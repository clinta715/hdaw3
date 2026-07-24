#pragma once

namespace HDAW
{
    class BpmDetector
    {
    public:
        struct Result
        {
            double bpm = 0.0;
            double confidence = 0.0;
        };

        static Result detect(const float* samples, int numSamples,
                             double sampleRate, double maxSeconds = 30.0);
    };
}