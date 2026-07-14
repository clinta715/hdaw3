#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <clap/all.h>
#include <memory>
#include <string>

struct CLAPModule
{
    void* handle = nullptr;
    clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    bool initialized = false;

    ~CLAPModule() { unload(); }

    bool load(const juce::String& path);
    void unload();
};

class CLAPPluginFormat : public juce::AudioPluginFormat
{
public:
    juce::String getName() const override { return "CLAP"; }
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }

    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                             const juce::String& fileOrIdentifier) override;

    bool fileMightContainThisPluginType(const juce::String& fileOrIdentifier) override;

    juce::String getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier) override;

    bool pluginNeedsRescanning(const juce::PluginDescription& desc) override;

    bool doesPluginStillExist(const juce::PluginDescription& desc) override;

    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath& directoriesToSearch,
                                            bool recursive, bool) override;

    juce::FileSearchPath getDefaultLocationsToSearch() override;

protected:
    void createPluginInstance(const juce::PluginDescription& desc,
                              double sampleRate, int blockSize,
                              PluginCreationCallback callback) override;

    bool requiresUnblockedMessageThreadDuringCreation(
        const juce::PluginDescription&) const override
    { return false; }
};
