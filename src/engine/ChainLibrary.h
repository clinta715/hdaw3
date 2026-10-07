#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <mutex>
#include <vector>

// Qt's qobjectdefs.h does `#define slots` (an object-like macro expanding to
// nothing), which collides with `ChainPreset::Slot`'s container member that is
// literally named `slots`: in a Qt TU the declaration below parses as
// `std::vector<Slot> ;` -> "declaration does not declare anything", and every
// use (`p.slots.empty()`) as `p.` -> "expected unqualified-id before '.'".
// Measured 2026-10-06 while wiring the patch verbs (PatchPreset.h compiled
// fine, but tests/integration/mcp/patch_preset_parity_test.cpp includes THIS
// header directly in a Qt TU and broke).
//
// The macro is therefore dropped for the body of this header and RESTORED at the
// end: Qt's `slots` must stay defined-and-empty for every later `public slots:`
// in a Qt TU (undefining it permanently makes `public slots:` a parse error in
// e.g. McpServer.h). Capture the fact it was defined, undef, restore after.
#ifdef slots
#  define HDAW_CHAINLIB_DEFER_QT_SLOTS 1
#  undef slots
#endif

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
        juce::String slicePointsOverride;                // user-pinned boundaries (normalized)
        juce::String sliceMeta;                          // frame:bandMask:strength triples
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
    // Which built-in roster the ctor seeds into <root>/_factory/:
    //   None    — seed nothing (a bare library over a caller-owned root).
    //   Chains  — the FX-chain roster: one file per factoryChainDefs() entry.
    //   Patches — the bass PATCH roster: one SINGLE-SLOT file per
    //             factoryPatchDefs() entry (a patch is one slot's state, so the
    //             chain factory roster is NOT patch content and vice versa).
    // An enum rather than two bools so a call site cannot silently pass the
    // wrong flag (`ChainLibrary(dir, true)` cannot be read as "seed patches").
    enum class Roster { None, Chains, Patches };
    explicit ChainLibrary(const juce::File& root, Roster roster = Roster::Chains);
    static const ChainLibrary& userLibrary();  // userApplicationDataDirectory/HDAW/chains (mirror src/mcp/McpTools_CompositionPattern.cpp)
    // Sibling root for SLOT PATCHES: userApplicationDataDirectory/HDAW/patches.
    // Same savePreset/listPresets/loadPreset/deletePreset semantics as
    // userLibrary(); factory-seeded with the 12 built-in bass PATCHES
    // (Roster::Patches, create-if-missing like the chain roster), then carries
    // whatever the user saves.
    static const ChainLibrary& patchLibrary();
    juce::String savePreset(const ChainPreset& p) const;   // root/user/<sanitized>.json, uniquified -N
    std::vector<ChainPreset> listPresets() const;    // scan *.json in _factory/ then user/ (factory first), like PatternLibrary.cpp:428
    ChainPreset loadPreset(const juce::String& id) const;
    bool deletePreset(const juce::String& id) const;       // refuses ids under _factory/
private:
    void seedFactoryPresetsIfMissing();       // Roster::Chains tail: write each built-in chain _factory/<name>.json only if absent
    void seedPatchFactoryPresetsIfMissing();  // Roster::Patches tail: same discipline, one-slot factoryPatchDefs() files
    juce::File root_, userDir_;
    mutable std::mutex mutex_;
};

} // namespace HDAW

// Restore Qt's `slots` for every TU that includes this header (see the note at
// the top): its empty definition is what keeps a later `public slots:` valid.
#ifdef HDAW_CHAINLIB_DEFER_QT_SLOTS
#  undef HDAW_CHAINLIB_DEFER_QT_SLOTS
#  define slots
#endif
