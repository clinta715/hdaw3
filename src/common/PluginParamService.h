#pragma once
#include <string>
#include <vector>
#include <functional>

struct PluginParamSnapshot {
    int index = 0;
    std::string name;
    float value = 0.0f;
    std::string text;
    std::string label;
    bool automatable = true;
    // CLAP range metadata (plain units, like internal-FX defs). hasRange is
    // false when the host format exposes no ranges (VST3 via JUCE, older
    // isolated children) — consumers must check before mapping units.
    bool hasRange = false;
    double minVal = 0.0;
    double maxVal = 1.0;
    double defaultVal = 0.0;
    double plainValue = 0.0;
    bool stepped = false;
};

class PluginParamService {
public:
    virtual ~PluginParamService() = default;

    virtual std::vector<PluginParamSnapshot> getParams(int trackIndex, const std::string& pluginID) = 0;
    virtual std::string getParamText(int trackIndex, const std::string& pluginID, int paramIndex, float normalizedValue) = 0;
    virtual void setParam(int trackIndex, const std::string& pluginID, int paramIndex, float normalizedValue) = 0;

    using ParamChangeCallback = std::function<void(int paramIndex, float value)>;
    virtual void setParamChangeCallback(int trackIndex, const std::string& pluginID, ParamChangeCallback cb) = 0;

    virtual int getProgramCount(int trackIndex, const std::string& pluginID) = 0;
    virtual int getCurrentProgram(int trackIndex, const std::string& pluginID) = 0;
    virtual std::string getProgramName(int trackIndex, const std::string& pluginID, int programIndex) = 0;
    virtual void setCurrentProgram(int trackIndex, const std::string& pluginID, int programIndex) = 0;
};
