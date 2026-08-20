#pragma once
#include <juce_core/juce_core.h>
#include <clap/factory/preset-discovery.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct CLAPModule;

// One loadable preset: the arguments for clap_plugin_preset_load.from_location.
struct CLAPPresetEntry
{
    juce::String name;
    juce::String loadKey;    // empty for FILE-kind presets (u-he/Surge)
    juce::String location;   // file path for FILE kind; empty for PLUGIN kind
    uint32_t locationKind = CLAP_PRESET_DISCOVERY_LOCATION_FILE;
};

// Per-module preset cache built via the CLAP preset-discovery factory
// (entry-level; the plugin-level preset-load extension does the loading).
// The registry caches one ModulePresets per module path for the process
// lifetime; the enumeration itself runs once per module (std::once_flag)
// and is immutable afterwards — readers only need the ready flag.
class CLAPPresetDatabase
{
public:
    static constexpr int kDefaultFileCap = 2000;

    class ModulePresets
    {
    public:
        // Production factory: derives the discovery factory + the module's
        // CLAP plugin ids from the module. The module is kept loaded until
        // the (possibly async) build finishes, then released.
        static std::shared_ptr<ModulePresets> getForModule(
            const juce::String& modulePath,
            const std::shared_ptr<CLAPModule>& module);

        // Registry access with an explicit factory (test seam / derived use).
        // Never builds while holding the registry lock.
        static std::shared_ptr<ModulePresets> getForModulePath(
            const juce::String& modulePath,
            const clap_preset_discovery_factory_t* factory,
            const std::vector<juce::String>& modulePluginIds,
            int fileCap);

        ModulePresets(const clap_preset_discovery_factory_t* factory,
                      std::vector<juce::String> modulePluginIds,
                      int fileCap);
        ~ModulePresets();

        // Builds once (blocking). Null factory / provider failures yield an
        // empty result — never throws.
        void ensureBuilt();
        // Kicks ensureBuilt() on a background thread once (spec: "indexing in
        // background threads is encouraged"); returns immediately.
        void startBuildAsync();
        bool isReady() const { return ready.load(std::memory_order_acquire); }

        // Keeps the module DSO loaded until the (possibly async) build ran.
        // First writer wins; ignored once the build is done.
        void setModuleKeepalive(std::shared_ptr<CLAPModule> module);

        // Valid only when isReady(); the vector is immutable after the build.
        // Null when the module has no presets for this plugin id.
        const std::vector<CLAPPresetEntry>* presetsFor(const juce::String& clapPluginId) const;

    private:
        void build();

        const clap_preset_discovery_factory_t* factory;
        std::vector<juce::String> modulePluginIds;
        int fileCap;

        std::mutex keepaliveMutex;
        std::shared_ptr<CLAPModule> moduleKeepalive;

        std::map<juce::String, std::vector<CLAPPresetEntry>> byPlugin;
        std::once_flag buildOnce;
        std::atomic<bool> ready{ false };
        std::atomic<bool> asyncStarted{ false };
        std::unique_ptr<std::thread> asyncThread;
    };

private:
    static std::mutex registryMutex;
    static std::map<juce::String, std::shared_ptr<ModulePresets>>& registry();
};
