#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include "CLAPPluginFormat.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>

namespace HDAW {

class PluginManager
{
public:
    PluginManager();
    ~PluginManager();

    using ScanProgressCallback = std::function<void(const juce::String& fileName, int completed, int total)>;

    void scanAll(ScanProgressCallback progressCb = nullptr);
    void abortScan() { abortRequested.store(true); }
    bool isLoading() const { return scanning.load(); }
    int getLastScanCrashCount() const { return lastScanCrashCount; }

    const std::vector<juce::PluginDescription>& getPlugins() const { return knownPlugins; }

    // Returns the default VST3/CLAP directories for the current platform.
    static juce::StringArray getVst3Dirs();
    static juce::StringArray getClapDirs();

    std::vector<juce::PluginDescription> getInstrumentPlugins() const;
    std::vector<juce::PluginDescription> getEffectPlugins() const;

    std::unique_ptr<juce::AudioPluginInstance> createPluginInstance(
        const juce::PluginDescription& desc, juce::String& errorMessage,
        double sampleRate = 44100.0, int blockSize = 512,
        bool isolated = false);

    void loadCache();
    void saveCache();

    // Blacklist
    bool isBlacklisted(const juce::String& pluginID) const;
    void blacklistPlugin(const juce::String& pluginID);
    void blacklistPlugin(const juce::String& pluginID, const juce::String& reason);
    void unblacklistPlugin(const juce::String& pluginID);
    const std::vector<juce::String>& getBlacklistedIDs() const { return blacklistedIDs; }
    juce::String getBlacklistReason(const juce::String& pluginID) const;
    void loadBlacklist();
    void saveBlacklist();

    using ScanCallback = std::function<void()>;
    void setScanCompleteCallback(ScanCallback cb) { scanCallback = cb; }
    ScanCallback getScanCompleteCallback() const { return scanCallback; }

    // Enumerate all .vst3/.clap files under the given directories. Public so
    // the frontend server can count plugin files for its directory-watcher
    // change detection (see FrontendServer::onPluginDirDebounceExpired).
    juce::Array<juce::File> findPluginFiles(const juce::StringArray& dirs);

private:
    void onScanFinished();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    std::vector<juce::PluginDescription> knownPlugins;
    std::atomic<bool> scanning{false};
    std::atomic<bool> abortRequested{false};
    ScanCallback scanCallback;
    juce::File cacheFile;

    std::vector<juce::String> blacklistedIDs;
    std::unordered_map<juce::String, juce::String> blacklistReasons;
    juce::File blacklistFile;

    // Isolated scanning
    juce::File scannerExePath;
    struct ScanResult { bool ok; bool isInstrument = false; int uid = 0; juce::String name, manufacturer, category, format, file, id, error; };
    ScanResult scanPluginIsolated(const juce::String& pluginPath);
    int lastScanCrashCount = 0;
};

} // namespace HDAW
