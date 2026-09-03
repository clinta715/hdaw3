#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <mutex>
#include <vector>

namespace HDAW {

// Named FX-chain preset: an ordered list of FX slots that can be saved to
// disk and re-applied to a track. Persisted as JSON under
// root/user/<sanitized>.json (see ChainLibrary).
//
// Threading: all ChainLibrary methods perform blocking file IO and are NOT
// realtime-safe. Call them from the message thread (or a background/test
// thread) — never from the audio thread.
//
// Versioning: ChainPreset::version is persisted as "version" in the JSON.
// Unknown future versions load best-effort: known fields are read, fields
// unknown to this build are ignored.
struct ChainPreset {
    int version = 1;
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
    static const ChainLibrary& userLibrary();  // userApplicationDataDirectory/HDAW/chains (mirror src/mcp/McpTools_CompositionPattern.cpp)
    juce::String savePreset(const ChainPreset& p) const;   // root/user/<sanitized>.json, uniquified -N
    std::vector<ChainPreset> listPresets() const;    // scan *.json like PatternLibrary.cpp:392
    ChainPreset loadPreset(const juce::String& id) const;
    bool deletePreset(const juce::String& id) const;       // refuses ids under _factory/
private:
    juce::File root_, userDir_;
    mutable std::mutex mutex_;
};

} // namespace HDAW
