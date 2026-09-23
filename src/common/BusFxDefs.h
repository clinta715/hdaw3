#pragma once
// Bus FX param defs — shared between the engine's FxBusProcessor (audio-side
// atomics) and the model/surface layers (command clamping, MCP/RPC readback).
// Header-only, juce_core only, mirroring common/MasterFxDefs.h.
//
// The names and ranges are copied verbatim from
// TrackFXSlot::getParamDefsForType (engine/TrackFXSlot.h) so a bus FX and a
// track FX of the same type agree on what "Room Size 0.5" or "Ratio 4" means:
// ONE parameter space for the surfaces, not two.
//
// The delay return is the FULL 5-param delay since slice C3 of
// docs/plans/2026-09-22-bus-fx-params.md: FxBusProcessor's delay runs the shared
// internal delay DSP (engine/InternalDelay.h, the same class TrackFXSlot uses),
// which genuinely honors Feedback, Mix, SyncToTempo and Division — so
// advertising them is not a fake param (G5). The defs below are pinned to
// TrackFXSlot::getParamDefsForType("delay") (which derives from
// InternalDelay::paramDefs()) by BusFxParam.DefTableMatchesTrackFxDefs.
//
// The `filter` return (slice E of docs/plans/2026-09-23-filter-bus.md) is the
// same move for the SVF: FxBusProcessor's filter chain runs the shared
// InternalFilter DSP (engine/InternalFilter.h, the same class TrackFXSlot uses),
// so a return can be high-passed. Its defs are pinned to
// TrackFXSlot::getParamDefsForType("filter") (which derives from
// InternalFilter::paramDefs()) by the same test.
#include <juce_core/juce_core.h>
#include <vector>

namespace HDAW {

struct BusFxParamDef { const char* name; float def; float min; float max; };

// The fx types FxBusProcessor::resetFxChain recognises — any other value
// builds a bus that enables no DSP at all (a silent passthrough). Shared so
// createBus's rejection, setBusFxParam's rejection and the read surfaces all
// name the same accepted set in the same order.
inline const std::vector<const char*>& busFxTypes()
{
    static const std::vector<const char*> types = { "reverb", "delay", "eq", "compressor", "filter" };
    return types;
}

inline const char* const busFxTypesText()
{
    return "reverb, delay, eq, compressor, filter";
}

inline const std::vector<BusFxParamDef>& busFxParamDefs(const juce::String& fxType)
{
    static const std::vector<BusFxParamDef> reverb = {
        {"Room Size",   0.5f,   0.0f,     1.0f},
        {"Damping",     0.5f,   0.0f,     1.0f},
        {"Wet Level",   0.3f,   0.0f,     1.0f},
        {"Dry Level",   0.7f,   0.0f,     1.0f},
        {"Width",       1.0f,   0.0f,     1.0f},
    };
    static const std::vector<BusFxParamDef> eq = {
        {"Frequency", 1000.0f,  20.0f, 20000.0f},
        {"Q",            0.7f,  0.1f,    10.0f},
        {"Gain",         0.0f, -24.0f,   24.0f},
    };
    static const std::vector<BusFxParamDef> comp = {
        {"Threshold", -20.0f, -80.0f,     0.0f},
        {"Ratio",       4.0f,   1.0f,    40.0f},
        {"Attack",      5.0f,   0.1f,   100.0f},
        {"Release",   100.0f,   1.0f,  2000.0f},
    };
    static const std::vector<BusFxParamDef> delay = {
        // The track delay's defs verbatim: Delay Time (manual seconds) plus the
        // feedback / mix / tempo-sync controls the shared InternalDelay DSP now
        // really applies on a return. Feedback's 0.99 top is the runaway guard.
        {"Delay Time",   0.5f, 0.01f,  5.0f  },
        {"Feedback",     0.3f, 0.0f,   0.99f },
        {"Mix",          0.5f, 0.0f,   1.0f  },
        {"SyncToTempo",  0.0f, 0.0f,   1.0f  },
        {"Division",     0.0f, 0.0f,   6.0f  },
    };
    static const std::vector<BusFxParamDef> filter = {
        // The track filter's defs verbatim (TrackFXSlot::getParamDefsForType,
        // which derives them from InternalFilter::paramDefs()): Cutoff Hz, the
        // Mode enum 0=lowpass / 1=highpass / 2=bandpass, and Resonance Q.
        // Mode is what makes a return high-passable — the classic dub move of
        // rolling the lows off a delay return so the repeats stop muddying the
        // bass — which the single PEAK-filter eq cannot express.
        {"Cutoff",    1000.0f,   20.0f, 20000.0f},
        {"Mode",         0.0f,    0.0f,     2.0f},
        {"Resonance",    0.7f,    0.1f,    10.0f},
    };
    static const std::vector<BusFxParamDef> none = {};
    if (fxType == "reverb")     return reverb;
    if (fxType == "eq")         return eq;
    if (fxType == "compressor") return comp;
    if (fxType == "delay")      return delay;
    if (fxType == "filter")     return filter;
    return none;
}

// Lesson-23 clamp helper: every entry point into bus FX params clamps to the
// def range before the value reaches the DSP or the tree.
inline float clampBusFxParam(const juce::String& fxType, int paramIndex, float value)
{
    const auto& defs = busFxParamDefs(fxType);
    if (paramIndex < 0 || paramIndex >= (int) defs.size()) return value;
    return juce::jlimit(defs[(size_t) paramIndex].min, defs[(size_t) paramIndex].max, value);
}

} // namespace HDAW
