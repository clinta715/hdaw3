#include "engine/EnvelopeGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace HDAW
{
namespace
{

// ── Local deterministic PRNG (xorshift64star) ──
class XorShift64Star
{
public:
    explicit XorShift64Star(uint64_t seed) : state(seed)
    {
        if (state == 0)
            state = 0x9E3779B97F4A7C15ULL; // avoid the zero-state fixed point
    }

    uint64_t nextU64()
    {
        uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 2685821657736338717ULL;
    }

    double nextUnit() // [0, 1)
    {
        return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
    }

    double nextBipolar() // [-1, 1)
    {
        return 2.0 * nextUnit() - 1.0;
    }

private:
    uint64_t state;
};

uint64_t randomSeed()
{
    std::random_device rd;
    return (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
}

double clamp01(double v)
{
    return std::max(0.0, std::min(1.0, v));
}

constexpr double kPi = 3.14159265358979323846264338327950288;

// Curve c(t) in [0, 1]. RandomWalk / Noise are handled by generate() itself
// because they need the sequential RNG stream.
double curveValue(EnvelopeGenerator::Shape shape, double t, const EnvelopeGenerator::Params& p)
{
    switch (shape)
    {
        case EnvelopeGenerator::Shape::Ramp:
            return t;

        case EnvelopeGenerator::Shape::ADSR:
        {
            double a = std::max(0.0, p.attack);
            double d = std::max(0.0, p.decay);
            double r = std::max(0.0, p.release);
            double sum = a + d + r;
            if (sum > 1.0)
            {
                a /= sum;
                d /= sum;
                r /= sum;
            }
            const double hold = std::max(0.0, 1.0 - a - d - r);
            const double sustain = clamp01(p.sustainLevel);

            if (t < a) // attack: 0 -> 1
                return (a > 0.0) ? t / a : 1.0;

            if (t < a + d) // decay: 1 -> sustain
                return (d > 0.0) ? 1.0 - (t - a) / d * (1.0 - sustain) : sustain;

            const double sustainEnd = a + d + hold;
            if (t < sustainEnd) // hold
                return sustain;

            const double releaseFrac = std::max(0.0, 1.0 - sustainEnd);
            if (releaseFrac <= 0.0)
                return 0.0;
            return sustain * (1.0 - (t - sustainEnd) / releaseFrac);
        }

        case EnvelopeGenerator::Shape::Sine:
        case EnvelopeGenerator::Shape::Triangle:
        case EnvelopeGenerator::Shape::Saw:
        case EnvelopeGenerator::Shape::Square:
        case EnvelopeGenerator::Shape::Pulse:
        {
            double ph = std::fmod(p.cycles * t + p.phase, 1.0);
            if (ph < 0.0)
                ph += 1.0;

            switch (shape)
            {
                case EnvelopeGenerator::Shape::Sine:
                    return (std::sin(2.0 * kPi * ph) + 1.0) * 0.5;
                case EnvelopeGenerator::Shape::Triangle:
                {
                    const double bipolar = (ph < 0.5) ? (4.0 * ph - 1.0) : (3.0 - 4.0 * ph);
                    return (bipolar + 1.0) * 0.5;
                }
                case EnvelopeGenerator::Shape::Saw:
                    return ((2.0 * ph - 1.0) + 1.0) * 0.5;
                case EnvelopeGenerator::Shape::Square:
                {
                    const double bipolar = (ph < 0.5) ? 1.0 : -1.0;
                    return (bipolar + 1.0) * 0.5;
                }
                case EnvelopeGenerator::Shape::Pulse:
                default:
                    return (ph < 0.5) ? 1.0 : 0.0;
            }
        }

        case EnvelopeGenerator::Shape::Staircase:
        {
            const int steps = std::max(2, p.steps);
            double k = t * steps;
            if (k >= steps)
                k = steps - 1.0;
            const int idx = static_cast<int>(std::floor(k));
            return static_cast<double>(idx) / static_cast<double>(steps - 1);
        }

        case EnvelopeGenerator::Shape::SCurve:
            return t * t * (3.0 - 2.0 * t);

        case EnvelopeGenerator::Shape::RandomWalk:
        case EnvelopeGenerator::Shape::Noise:
        default:
            return 0.0;
    }
}

} // namespace

const char* EnvelopeGenerator::shapeName(Shape s)
{
    switch (s)
    {
        case Shape::Ramp:        return "ramp";
        case Shape::ADSR:        return "adsr";
        case Shape::Sine:        return "sine";
        case Shape::Triangle:    return "triangle";
        case Shape::Saw:         return "saw";
        case Shape::Square:      return "square";
        case Shape::Pulse:       return "pulse";
        case Shape::Staircase:   return "staircase";
        case Shape::SCurve:      return "sCurve";
        case Shape::RandomWalk:  return "randomWalk";
        case Shape::Noise:       return "noise";
        default:                 return "ramp";
    }
}

std::vector<std::pair<double, double>> EnvelopeGenerator::generate(const Params& params)
{
    Params p = params;
    if (std::isnan(p.startTime) || std::isnan(p.endTime) ||
        std::isnan(p.startValue) || std::isnan(p.endValue) ||
        std::isnan(p.cycles) || std::isnan(p.phase) ||
        std::isnan(p.densityPerSec) || std::isnan(p.attack) ||
        std::isnan(p.decay) || std::isnan(p.sustainLevel) ||
        std::isnan(p.release) || std::isnan(p.smooth))
        return {};

    if (p.endTime < p.startTime)
        std::swap(p.startTime, p.endTime);
    p.startValue = clamp01(p.startValue);
    p.endValue = clamp01(p.endValue);

    const double range = p.endTime - p.startTime;
    if (range < 1e-9)
        return {{p.startTime, p.startValue}};

    int count = static_cast<int>(std::lround(p.densityPerSec * range));
    if (count < 2)
        count = 2;
    if (count > 4096)
        count = 4096;

    XorShift64Star rng(p.seed == 0 ? randomSeed() : p.seed);

    std::vector<std::pair<double, double>> out;
    out.reserve(static_cast<size_t>(count) + 1);

    double walkC = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(count - 1);
        const double time = (i == count - 1) ? p.endTime : p.startTime + range * t;

        double c = 0.0;
        switch (p.shape)
        {
            case Shape::RandomWalk:
                c = walkC;
                walkC = clamp01(walkC + rng.nextBipolar() * 0.15);
                break;
            case Shape::Noise:
                c = rng.nextUnit();
                break;
            default:
                c = curveValue(p.shape, t, p);
                break;
        }

        const double value = clamp01(p.startValue + c * (p.endValue - p.startValue));
        out.emplace_back(time, value);
    }

    // Drop points closer than 1e-9 to the previous kept point (keep the first).
    std::vector<std::pair<double, double>> dedup;
    dedup.reserve(out.size());
    for (const auto& pt : out)
        if (dedup.empty() || pt.first - dedup.back().first >= 1e-9)
            dedup.push_back(pt);
    out.swap(dedup);

    // Guarantee: exact endTime as the last point.
    if (out.back().first != p.endTime)
    {
        if (p.endTime - out.back().first < 1e-9)
            out.back().first = p.endTime;
        else
            out.emplace_back(p.endTime, out.back().second);
    }

    if (p.smooth > 0.0)
    {
        const double amount = clamp01(p.smooth);
        for (size_t i = 1; i < out.size(); ++i)
            out[i].second = out[i - 1].second + (1.0 - amount) * (out[i].second - out[i - 1].second);
    }

    for (auto& pt : out)
        pt.second = clamp01(pt.second);

    return out;
}

std::vector<std::pair<double, double>> EnvelopeGenerator::smooth(const std::vector<std::pair<double, double>>& points, double amount)
{
    if (points.empty())
        return {};
    if (amount <= 0.0)
        return points;

    std::vector<std::pair<double, double>> out = points;
    const double a = clamp01(amount);
    for (size_t i = 1; i < out.size(); ++i)
        out[i].second = out[i - 1].second + (1.0 - a) * (out[i].second - out[i - 1].second);

    // Endpoints preserved exactly.
    out.front().second = points.front().second;
    out.back().second = points.back().second;
    return out;
}

std::vector<std::pair<double, double>> EnvelopeGenerator::clampDensity(const std::vector<std::pair<double, double>>& points, double maxPerSec, size_t maxPoints)
{
    if (points.empty())
        return {};
    if (maxPoints < 2)
        maxPoints = 2;

    size_t target = maxPoints;
    if (points.size() >= 2)
    {
        const double duration = points.back().first - points.front().first;
        if (duration > 0.0 && std::isfinite(maxPerSec) && maxPerSec > 0.0)
        {
            double byDensity = static_cast<double>(std::llround(maxPerSec * duration));
            if (byDensity < 2.0)
                byDensity = 2.0;
            target = std::min(target, static_cast<size_t>(byDensity));
        }
    }

    if (target >= points.size())
        return points;

    std::vector<std::pair<double, double>> out;
    out.reserve(target);
    const uint64_t n = points.size();
    for (size_t i = 0; i < target; ++i)
    {
        const uint64_t idx = (static_cast<uint64_t>(i) * (n - 1)) / (target - 1);
        out.push_back(points[static_cast<size_t>(idx)]);
    }
    return out;
}

} // namespace HDAW
