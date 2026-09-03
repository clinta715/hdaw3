#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

struct ChainPreset {
    struct PluginRef { juce::String id, format, path, stateBase64; };
    struct Slot {
        juce::String fxType;
        bool bypassed = false;
        juce::String name;
        std::map<juce::String, double> params;          // "param_N" -> real-unit value
        PluginRef plugin;                                // only when fxType == "plugin"
        std::map<juce::String, juce::String> sampler;   // sampleFile, mode, rootNote, ... (strings)
        juce::String slicePoints;                        // space-separated normalized floats
        juce::String psyFmMatrix; double psyFmSweepRate = 0.0;
    };
    juce::String id, name;
    std::vector<Slot> slots;
};

class ChainLibrary {
public:
    explicit ChainLibrary(const juce::File& root);
    static ChainLibrary userLibrary();  // userApplicationDataDirectory/HDAW/chains (mirror McpTools_CompositionPattern.cpp:86-88)
    juce::String savePreset(const ChainPreset& p);   // root/user/<sanitized>.json, uniquified -N
    std::vector<ChainPreset> listPresets();          // scan *.json like PatternLibrary.cpp:392
    ChainPreset loadPreset(const juce::String& id);
    bool deletePreset(const juce::String& id);
private:
    juce::File root_, userDir_;
};
