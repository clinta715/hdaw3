#pragma once
// Master FX param defs — shared between the engine's MasterBusProcessor
// (audio-side atomics) and the model layer (default-project stamping,
// command clamping, MCP readback). Header-only, JUCE-core only.
#include <juce_core/juce_core.h>
#include <vector>

namespace HDAW {

struct MasterFxParamDef { const char* name; float def; float min; float max; };

inline const std::vector<MasterFxParamDef>& masterFxParamDefs(const juce::String& fxType)
{
    static const std::vector<MasterFxParamDef> eq = {
        {"Frequency", 1000.0f, 20.0f, 20000.0f},
        {"Q",            0.7f,  0.1f,    10.0f},
        {"Gain",         0.0f, -24.0f,   24.0f},
    };
    static const std::vector<MasterFxParamDef> comp = {
        {"Threshold", -18.0f, -60.0f,   0.0f},
        {"Ratio",       4.0f,   1.0f,  20.0f},
        {"Attack",      5.0f,   0.1f, 100.0f},
        {"Release",   120.0f,  10.0f,1000.0f},
    };
    static const std::vector<MasterFxParamDef> lim = {
        {"Threshold",  -3.0f, -24.0f,   0.0f},
        {"Release",    80.0f,   1.0f, 500.0f},
    };
    static const std::vector<MasterFxParamDef> none = {};
    if (fxType == "eq")         return eq;
    if (fxType == "compressor") return comp;
    if (fxType == "limiter")    return lim;
    return none;
}

// Lesson-23 clamp helper: every entry point into master FX params clamps to
// the def range before the value reaches the DSP or the tree.
inline float clampMasterFxParam(const juce::String& fxType, int paramIndex, float value)
{
    const auto& defs = masterFxParamDefs(fxType);
    if (paramIndex < 0 || paramIndex >= (int) defs.size()) return value;
    return juce::jlimit(defs[(size_t) paramIndex].min, defs[(size_t) paramIndex].max, value);
}

} // namespace HDAW
