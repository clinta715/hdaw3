#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace HDAW
{

// ── Envelope generator ──
// Pure-std curve generator producing (time, value) point lists. No JUCE, no
// global state; deterministic for a fixed seed (seed 0 = std::random_device).
class EnvelopeGenerator
{
public:
    enum class Shape {
        Ramp = 0,
        ADSR,
        Sine,
        Triangle,
        Saw,
        Square,
        Pulse,
        Staircase,
        SCurve,
        RandomWalk,
        Noise
    };

    struct Params
    {
        Shape shape = Shape::Ramp;
        double startTime = 0.0;
        double endTime = 4.0;
        double startValue = 0.0;
        double endValue = 1.0;
        double cycles = 1.0;
        double phase = 0.0;
        double densityPerSec = 8.0;
        double attack = 0.2;
        double decay = 0.15;
        double sustainLevel = 0.5;
        double release = 0.25;
        double smooth = 0.0;
        int steps = 8;
        uint64_t seed = 0; // 0 = non-deterministic (random_device); else reproducible
    };

    static const char* shapeName(Shape s);
    static std::vector<std::pair<double, double>> generate(const Params& params);
    static std::vector<std::pair<double, double>> smooth(const std::vector<std::pair<double, double>>& points, double amount);
    static std::vector<std::pair<double, double>> clampDensity(const std::vector<std::pair<double, double>>& points, double maxPerSec, size_t maxPoints);
};

} // namespace HDAW
