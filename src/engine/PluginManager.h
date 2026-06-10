#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>

namespace HDAW {

class PluginManager
{
public:
    PluginManager();
    ~PluginManager();

    void scanAll();
    bool isLoading() const { return scanning.load(); }

    const std::vector<juce::PluginDescription>& getPlugins() const { return knownPlugins; }

    std::unique_ptr<juce::AudioPluginInstance> createPluginInstance(
        const juce::PluginDescription& desc, juce::String& errorMessage,
        double sampleRate = 44100.0, int blockSize = 512);

    void loadCache();
    void saveCache();

    using ScanCallback = std::function<void()>;
    void setScanCompleteCallback(ScanCallback cb) { scanCallback = cb; }
    ScanCallback getScanCompleteCallback() const { return scanCallback; }

private:
    void onScanFinished();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    std::vector<juce::PluginDescription> knownPlugins;
    std::atomic<bool> scanning{false};
    ScanCallback scanCallback;
    juce::File cacheFile;
};

} // namespace HDAW
