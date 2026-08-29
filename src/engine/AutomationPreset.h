#pragma once
#include "EnvelopeGenerator.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace HDAW
{

// ── Automation preset bank (plan 2026-08-29-jungle-dnb-feature-gaps.md P2-3) ──
// Named per-track automation recipes (pump / macro / openClose / riser /
// sine / square) expressed as EnvelopeGenerator segment params over BEAT
// windows. Pure std — no JUCE, no engine state; plan() is deterministic for a
// fixed seed because EnvelopeGenerator is deterministic for seed != 0.
//
// Unit convention (docs/architecture.md): this header speaks BEATS. The
// command layer (AudioEngineCommands::applyAutomationPreset) converts each
// segment's times to the seconds domain the ValueTree stores, and scales
// densityPerSec from per-beat to per-second (density 4.0 = a 0.25-beat grid)
// so the grid stays locked to beats at any tempo.
class AutomationPreset
{
public:
    enum class Preset {
        Pump = 0,
        Macro,
        OpenClose,
        Riser,
        Sine,
        Square
    };

    // One named recipe over one beat window. Optionals override the recipe's
    // documented defaults; midPoint only affects openClose (default: centre).
    struct PresetWindow
    {
        double start = 0.0;
        double end = 16.0;
        Preset preset = Preset::Pump;
        std::optional<double> startValue;
        std::optional<double> endValue;
        std::optional<double> cycles;
        std::optional<double> midPoint;
    };

    struct EnvelopePlan
    {
        std::vector<EnvelopeGenerator::Params> segments;
    };

    static const char* presetName(Preset p)
    {
        switch (p)
        {
            case Preset::Pump:      return "pump";
            case Preset::Macro:     return "macro";
            case Preset::OpenClose: return "openClose";
            case Preset::Riser:     return "riser";
            case Preset::Sine:      return "sine";
            case Preset::Square:    return "square";
            default:                return "pump";
        }
    }

    static std::optional<Preset> presetFromName(const std::string& name)
    {
        if (name == "pump")      return Preset::Pump;
        if (name == "macro")     return Preset::Macro;
        if (name == "openClose") return Preset::OpenClose;
        if (name == "riser")     return Preset::Riser;
        if (name == "sine")      return Preset::Sine;
        if (name == "square")    return Preset::Square;
        return std::nullopt;
    }

    struct PresetDoc
    {
        const char* name;
        const char* line;
    };

    // One doc line per preset, used to build the MCP tool description.
    inline static const PresetDoc kPresetDocumentation[] = {
        {"pump",      "triangle per-beat pump: one triangle cycle per beat between startValue (default 0.70) and endValue (default 1.00), 0.25-beat grid"},
        {"macro",     "linear macro ramp from startValue (default 0.15) to endValue (default 0.60) across the window — cutoff/macro sweep"},
        {"openClose", "down-then-up filter sweep: S-curve closes startValue (default 0.60) down to 0.05 at midPoint (default window centre), then ramps back up to endValue (default 0.90) — breakdown open"},
        {"riser",     "S-curve riser from startValue (default 0.10) to endValue (default 0.90) — drop build"},
        {"sine",      "sine wobble: startValue (default 0) to endValue (default 1); one full cycle per 4 beats (cycles default = window length / 4) — LFO-style movement"},
        {"square",    "square-wave gate: startValue (default 0) to endValue (default 1); one full cycle per 4 beats (cycles default = window length / 4) — rhythmic gating"}
    };
    inline static constexpr std::size_t kPresetDocumentationCount = 6;

    // Build the generator segments for one beat window. Times and density stay
    // in beats (density 4.0 = 0.25-beat grid). All values are clamped 0..1.
    // Returns an EMPTY plan when the window is invalid (end <= start).
    static EnvelopePlan plan(const PresetWindow& w, uint64_t seed)
    {
        EnvelopePlan out;
        if (!(w.end > w.start))
            return out;

        const double len = w.end - w.start;
        constexpr double kDensityPerBeat = 4.0; // 0.25-beat grid

        const auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
        const auto seg = [&](EnvelopeGenerator::Shape shape, double s, double e) {
            EnvelopeGenerator::Params p;
            p.shape = shape;
            p.startTime = s;
            p.endTime = e;
            p.densityPerSec = kDensityPerBeat;
            p.seed = seed;
            return p;
        };

        switch (w.preset)
        {
            case Preset::Pump:
            {
                auto p = seg(EnvelopeGenerator::Shape::Triangle, w.start, w.end);
                p.startValue = clamp01(w.startValue.value_or(0.70));
                p.endValue   = clamp01(w.endValue.value_or(1.00));
                p.cycles     = w.cycles.value_or(len); // one cycle per beat
                out.segments.push_back(p);
                break;
            }
            case Preset::Macro:
            {
                auto p = seg(EnvelopeGenerator::Shape::Ramp, w.start, w.end);
                p.startValue = clamp01(w.startValue.value_or(0.15));
                p.endValue   = clamp01(w.endValue.value_or(0.60));
                if (w.cycles) p.cycles = *w.cycles;
                out.segments.push_back(p);
                break;
            }
            case Preset::OpenClose:
            {
                const double mid = clamp01_mid(w.midPoint.value_or(w.start + len * 0.5), w.start, w.end);
                // Down leg: S-curve close from the open value down to 0.05.
                auto down = seg(EnvelopeGenerator::Shape::SCurve, w.start, mid);
                down.startValue = clamp01(w.startValue.value_or(0.60));
                down.endValue   = 0.05;
                out.segments.push_back(down);
                // Up leg: linear reopen from 0.05 up to endValue.
                auto up = seg(EnvelopeGenerator::Shape::Ramp, mid, w.end);
                up.startValue = 0.05;
                up.endValue   = clamp01(w.endValue.value_or(0.90));
                out.segments.push_back(up);
                break;
            }
            case Preset::Riser:
            {
                auto p = seg(EnvelopeGenerator::Shape::SCurve, w.start, w.end);
                p.startValue = clamp01(w.startValue.value_or(0.10));
                p.endValue   = clamp01(w.endValue.value_or(0.90));
                if (w.cycles) p.cycles = *w.cycles;
                out.segments.push_back(p);
                break;
            }
            case Preset::Sine:
            {
                auto p = seg(EnvelopeGenerator::Shape::Sine, w.start, w.end);
                p.startValue = clamp01(w.startValue.value_or(0.0));
                p.endValue   = clamp01(w.endValue.value_or(1.0));
                p.cycles     = w.cycles.value_or(len / 4.0); // 4-beat wobble
                out.segments.push_back(p);
                break;
            }
            case Preset::Square:
            {
                auto p = seg(EnvelopeGenerator::Shape::Square, w.start, w.end);
                p.startValue = clamp01(w.startValue.value_or(0.0));
                p.endValue   = clamp01(w.endValue.value_or(1.0));
                p.cycles     = w.cycles.value_or(len / 4.0); // 4-beat gate
                out.segments.push_back(p);
                break;
            }
        }
        return out;
    }

private:
    static double clamp01_mid(double mid, double start, double end)
    {
        return std::max(start, std::min(end, mid));
    }
};

} // namespace HDAW
