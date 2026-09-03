#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <mutex>
#include <vector>

namespace HDAW {

// Named FX-chain preset: an ordered list of FX slots that can be saved to
// disk and re-applied to a track. Persisted as JSON under
// root/user/<sanitized>.json (user presets, writable) or
// root/_factory/<sanitized>.json (built-in factory presets seeded by the
// ChainLibrary constructor; never overwritten once present, never
// deletable). Ids are root-relative: "user/<name>.json" or
// "_factory/<name>.json" (see ChainLibrary).
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
    // True when the preset came from the read-only _factory/ tree (built-in
    // content). Set by readPresetFile from the id prefix, so both list
    // results and loadPreset results carry it.
    bool isFactory = false;
};

class ChainLibrary {
public:
    explicit ChainLibrary(const juce::File& root);
    static const ChainLibrary& userLibrary();  // userApplicationDataDirectory/HDAW/chains (mirror src/mcp/McpTools_CompositionPattern.cpp)
    juce::String savePreset(const ChainPreset& p) const;   // root/user/<sanitized>.json, uniquified -N
    std::vector<ChainPreset> listPresets() const;    // scan *.json in _factory/ then user/ (factory first), like PatternLibrary.cpp:428
    ChainPreset loadPreset(const juce::String& id) const;
    bool deletePreset(const juce::String& id) const;       // refuses ids under _factory/
private:
    void seedFactoryPresetsIfMissing();    // ctor tail: write each built-in _factory/<name>.json only if absent
    juce::File root_, userDir_;
    mutable std::mutex mutex_;
};

} // namespace HDAW
